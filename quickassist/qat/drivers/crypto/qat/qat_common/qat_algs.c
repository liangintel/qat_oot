// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2021 Intel Corporation */
#ifndef QAT_AEAD_OLD_SUPPORTED
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/crypto.h>
#include <linux/version.h>
#include <crypto/internal/aead.h>
#if KERNEL_VERSION(5, 5, 0) <= LINUX_VERSION_CODE
#include <crypto/internal/skcipher.h>
#endif
#include <crypto/aes.h>
#if KERNEL_VERSION(5, 11, 0) <= LINUX_VERSION_CODE
#include <crypto/sha1.h>
#include <crypto/sha2.h>
#else
#include <crypto/sha.h>
#endif
#include <crypto/hash.h>
#include <crypto/algapi.h>
#include <crypto/authenc.h>
#include <crypto/scatterwalk.h>
#include <crypto/xts.h>
#include <linux/dma-mapping.h>
#include "adf_accel_devices.h"
#include "adf_transport.h"
#include "adf_common_drv.h"
#include "qat_crypto.h"
#include "icp_qat_hw.h"
#include "icp_qat_fw.h"
#include "icp_qat_fw_la.h"

#define QAT_AES_HW_CONFIG_ENC(alg, mode, aead_hash_cmp_len) \
	ICP_QAT_HW_CIPHER_CONFIG_BUILD(mode, alg, \
				       ICP_QAT_HW_CIPHER_NO_CONVERT, \
				       ICP_QAT_HW_CIPHER_ENCRYPT, \
				       aead_hash_cmp_len)

#define QAT_AES_HW_CONFIG_DEC(alg, mode, aead_hash_cmp_len) \
	ICP_QAT_HW_CIPHER_CONFIG_BUILD(mode, alg, \
				       ICP_QAT_HW_CIPHER_KEY_CONVERT, \
				       ICP_QAT_HW_CIPHER_DECRYPT, \
				       aead_hash_cmp_len)

#define QAT_AES_HW_CONFIG_DEC_NO_CONV(alg, mode, aead_hash_cmp_len) \
	ICP_QAT_HW_CIPHER_CONFIG_BUILD(mode, alg, \
				       ICP_QAT_HW_CIPHER_NO_CONVERT, \
				       ICP_QAT_HW_CIPHER_DECRYPT, \
				       aead_hash_cmp_len)

#define HW_CAP_AES_V2(accel_dev) \
	(GET_HW_DATA(accel_dev)->accel_capabilities_mask & \
	 ICP_ACCEL_CAPABILITIES_AES_V2)

static DEFINE_MUTEX(algs_lock);
static unsigned int active_devs;

/* Common content descriptor */
struct qat_alg_cd {
	union {
		struct qat_enc { /* Encrypt content desc */
			struct icp_qat_hw_cipher_algo_blk cipher;
			struct icp_qat_hw_auth_algo_blk hash;
		} qat_enc_cd;
		struct qat_dec { /* Decrytp content desc */
			struct icp_qat_hw_auth_algo_blk hash;
			struct icp_qat_hw_cipher_algo_blk cipher;
		} qat_dec_cd;
	};
} __aligned(64);

struct qat_alg_aead_ctx {
	struct qat_alg_cd *enc_cd;
	struct qat_alg_cd *dec_cd;
	dma_addr_t enc_cd_paddr;
	dma_addr_t dec_cd_paddr;
	struct icp_qat_fw_la_bulk_req enc_fw_req;
	struct icp_qat_fw_la_bulk_req dec_fw_req;
	struct crypto_shash *hash_tfm;
	enum icp_qat_hw_auth_algo qat_hash_alg;
	struct qat_crypto_instance *inst;
	char ipad[SHA512_BLOCK_SIZE];
	char opad[SHA512_BLOCK_SIZE];
};

#if KERNEL_VERSION(5, 5, 0) > LINUX_VERSION_CODE
struct qat_alg_ablkcipher_ctx {
#else
struct qat_alg_skcipher_ctx {
#endif
	struct icp_qat_hw_cipher_algo_blk *enc_cd;
	struct icp_qat_hw_cipher_algo_blk *dec_cd;
	dma_addr_t enc_cd_paddr;
	dma_addr_t dec_cd_paddr;
	struct icp_qat_fw_la_bulk_req enc_fw_req;
	struct icp_qat_fw_la_bulk_req dec_fw_req;
	struct qat_crypto_instance *inst;
#if KERNEL_VERSION(5, 5, 0) > LINUX_VERSION_CODE
	struct crypto_tfm *tfm;
#endif
	int mode;
#if KERNEL_VERSION(5, 5, 0) <= LINUX_VERSION_CODE
	struct crypto_skcipher *ftfm;
#endif
	bool fallback;
};

static int qat_get_inter_state_size(enum icp_qat_hw_auth_algo qat_hash_alg)
{
	switch (qat_hash_alg) {
	case ICP_QAT_HW_AUTH_ALGO_SHA1:
		return ICP_QAT_HW_SHA1_STATE1_SZ;
	case ICP_QAT_HW_AUTH_ALGO_SHA256:
		return ICP_QAT_HW_SHA256_STATE1_SZ;
	case ICP_QAT_HW_AUTH_ALGO_SHA512:
		return ICP_QAT_HW_SHA512_STATE1_SZ;
	default:
		return -EFAULT;
	};
	return -EFAULT;
}

static int qat_alg_do_precomputes(struct icp_qat_hw_auth_algo_blk *hash,
				  struct qat_alg_aead_ctx *ctx,
				  const u8 *auth_key,
				  unsigned int auth_keylen)
{
	SHASH_DESC_ON_STACK(shash, ctx->hash_tfm);
	struct sha1_state sha1;
	struct sha256_state sha256;
	struct sha512_state sha512;
	unsigned int block_size = crypto_shash_blocksize(ctx->hash_tfm);
	unsigned int digest_size = crypto_shash_digestsize(ctx->hash_tfm);
	__be32 *hash_state_out;
	__be64 *hash512_state_out;
	int i, offset;
	int ret = 0;
	const unsigned int SHA_BLOCK_SIZE = ARRAY_SIZE(ctx->ipad);

	if (block_size > SHA_BLOCK_SIZE || digest_size > SHA_BLOCK_SIZE)
		return -EFAULT;

	memset(ctx->ipad, 0, SHA_BLOCK_SIZE);
	memset(ctx->opad, 0, SHA_BLOCK_SIZE);
	memset(shash, 0, sizeof(struct shash_desc));

	shash->tfm = ctx->hash_tfm;

	if (auth_keylen > block_size) {
		ret = crypto_shash_digest(shash, auth_key, auth_keylen,
					  ctx->ipad);
		if (ret)
			return ret;

		memcpy(ctx->opad, ctx->ipad, digest_size);
	} else {
		memcpy(ctx->ipad, auth_key, auth_keylen);
		memcpy(ctx->opad, auth_key, auth_keylen);
	}

	for (i = 0; i < block_size; i++) {
		char *ipad_ptr = ctx->ipad + i;
		char *opad_ptr = ctx->opad + i;
		*ipad_ptr ^= 0x36;
		*opad_ptr ^= 0x5C;
	}

	if (crypto_shash_init(shash)) {
		ret = -EFAULT;
		goto precomputes_fault;
	}

	if (crypto_shash_update(shash, ctx->ipad, block_size)) {
		ret = -EFAULT;
		goto precomputes_fault;
	}

	hash_state_out = (__be32 *)hash->sha512.state1;
	hash512_state_out = (__be64 *)hash_state_out;

	switch (ctx->qat_hash_alg) {
	case ICP_QAT_HW_AUTH_ALGO_SHA1:
		if (crypto_shash_export(shash, &sha1)) {
			ret = -EFAULT;
			goto precomputes_fault;
		}
		for (i = 0; i < (digest_size / 4) && i < ARRAY_SIZE(sha1.state);
		     i++, hash_state_out++)
			*hash_state_out = cpu_to_be32(*(sha1.state + i));
		break;
	case ICP_QAT_HW_AUTH_ALGO_SHA256:
		if (crypto_shash_export(shash, &sha256)) {
			ret = -EFAULT;
			goto precomputes_fault;
		}
		for (i = 0; i < (digest_size / 4) &&
		     i < ARRAY_SIZE(sha256.state); i++, hash_state_out++)
			*hash_state_out = cpu_to_be32(*(sha256.state + i));
		break;
	case ICP_QAT_HW_AUTH_ALGO_SHA512:
		if (crypto_shash_export(shash, &sha512)) {
			ret = -EFAULT;
			goto precomputes_fault;
		}
		for (i = 0; i < (digest_size / 8) &&
		     i < ARRAY_SIZE(sha512.state); i++, hash512_state_out++)
			*hash512_state_out = cpu_to_be64(*(sha512.state + i));
		break;
	default:
		ret = -EFAULT;
		goto precomputes_fault;
	}

	if (crypto_shash_init(shash)) {
		ret = -EFAULT;
		goto precomputes_fault;
	}

	if (crypto_shash_update(shash, ctx->opad, block_size)) {
		ret = -EFAULT;
		goto precomputes_fault;
	}

	offset = round_up(qat_get_inter_state_size(ctx->qat_hash_alg), 8);
	if (offset < 0) {
		ret = -EFAULT;
		goto precomputes_fault;
	}

	hash_state_out = (__be32 *)(hash->sha512.state1 + offset);
	hash512_state_out = (__be64 *)hash_state_out;

	switch (ctx->qat_hash_alg) {
	case ICP_QAT_HW_AUTH_ALGO_SHA1:
		if (crypto_shash_export(shash, &sha1)) {
			ret = -EFAULT;
			goto precomputes_fault;
		}
		for (i = 0; i < (digest_size / 4) && i < ARRAY_SIZE(sha1.state);
		     i++, hash_state_out++)
			*hash_state_out = cpu_to_be32(*(sha1.state + i));
		break;
	case ICP_QAT_HW_AUTH_ALGO_SHA256:
		if (crypto_shash_export(shash, &sha256)) {
			ret = -EFAULT;
			goto precomputes_fault;
		}
		for (i = 0; i < (digest_size / 4) &&
		     i < ARRAY_SIZE(sha256.state); i++, hash_state_out++)
			*hash_state_out = cpu_to_be32(*(sha256.state + i));
		break;
	case ICP_QAT_HW_AUTH_ALGO_SHA512:
		if (crypto_shash_export(shash, &sha512)) {
			ret = -EFAULT;
			goto precomputes_fault;
		}
		for (i = 0; i < (digest_size / 8) &&
		     i < ARRAY_SIZE(sha512.state); i++, hash512_state_out++)
			*hash512_state_out = cpu_to_be64(*(sha512.state + i));
		break;
	default:
		ret = -EFAULT;
		goto precomputes_fault;
	}

precomputes_fault:
	memzero_explicit(ctx->ipad, SHA_BLOCK_SIZE);
	memzero_explicit(ctx->opad, SHA_BLOCK_SIZE);

	return ret;
}

static void qat_alg_init_hdr_no_iv_updt(struct icp_qat_fw_comn_req_hdr *header)
{
	ICP_QAT_FW_LA_CIPH_IV_FLD_FLAG_SET(header->serv_specif_flags,
					   ICP_QAT_FW_CIPH_IV_16BYTE_DATA);
	ICP_QAT_FW_LA_UPDATE_STATE_SET(header->serv_specif_flags,
				       ICP_QAT_FW_LA_NO_UPDATE_STATE);
}

static void qat_alg_init_common_hdr(struct icp_qat_fw_comn_req_hdr *header,
				    int aead)
{
	header->hdr_flags =
		ICP_QAT_FW_COMN_HDR_FLAGS_BUILD(ICP_QAT_FW_COMN_REQ_FLAG_SET);
	header->service_type = ICP_QAT_FW_COMN_REQ_CPM_FW_LA;
	header->comn_req_flags =
		ICP_QAT_FW_COMN_FLAGS_BUILD(QAT_COMN_CD_FLD_TYPE_64BIT_ADR,
					    QAT_COMN_PTR_TYPE_SGL);
	ICP_QAT_FW_LA_PARTIAL_SET(header->serv_specif_flags,
				  ICP_QAT_FW_LA_PARTIAL_NONE);

	qat_alg_init_hdr_no_iv_updt(header);

	ICP_QAT_FW_LA_PROTO_SET(header->serv_specif_flags,
				ICP_QAT_FW_LA_NO_PROTO);
}

static int qat_alg_aead_init_enc_session(struct crypto_aead *aead_tfm,
					 int alg,
					 struct crypto_authenc_keys *keys,
					 int mode)
{
	struct qat_alg_aead_ctx *ctx = crypto_aead_ctx(aead_tfm);
	unsigned int digestsize = crypto_aead_authsize(aead_tfm);
	struct qat_enc *enc_ctx = &ctx->enc_cd->qat_enc_cd;
	struct icp_qat_hw_cipher_algo_blk *cipher = &enc_ctx->cipher;
	struct icp_qat_hw_auth_algo_blk *hash =
		(struct icp_qat_hw_auth_algo_blk *)((char *)enc_ctx +
		sizeof(struct icp_qat_hw_auth_setup) + keys->enckeylen);
	struct icp_qat_fw_la_bulk_req *req_tmpl = &ctx->enc_fw_req;
	struct icp_qat_fw_comn_req_hdr_cd_pars *cd_pars = &req_tmpl->cd_pars;
	struct icp_qat_fw_comn_req_hdr *header = &req_tmpl->comn_hdr;
	void *ptr = &req_tmpl->cd_ctrl;
	struct icp_qat_fw_cipher_cd_ctrl_hdr *cipher_cd_ctrl = ptr;
	struct icp_qat_fw_auth_cd_ctrl_hdr *hash_cd_ctrl = ptr;

	/* CD setup */
	cipher->aes256_f8.cipher_config.val = QAT_AES_HW_CONFIG_ENC(alg, mode,
								    0);
	memcpy(cipher->aes256_f8.key, keys->enckey, keys->enckeylen);
	hash->sha512.inner_setup.auth_config.config =
		ICP_QAT_HW_AUTH_CONFIG_BUILD(ICP_QAT_HW_AUTH_MODE1,
					     ctx->qat_hash_alg, digestsize);
	hash->sha512.inner_setup.auth_counter.counter =
		cpu_to_be32(crypto_shash_blocksize(ctx->hash_tfm));

	if (qat_alg_do_precomputes(hash, ctx, keys->authkey, keys->authkeylen))
		return -EFAULT;

	/* Request setup */
	qat_alg_init_common_hdr(header, 1);
	header->service_cmd_id = ICP_QAT_FW_LA_CMD_CIPHER_HASH;
	ICP_QAT_FW_LA_DIGEST_IN_BUFFER_SET(header->serv_specif_flags,
					   ICP_QAT_FW_LA_DIGEST_IN_BUFFER);
	ICP_QAT_FW_LA_RET_AUTH_SET(header->serv_specif_flags,
				   ICP_QAT_FW_LA_RET_AUTH_RES);
	ICP_QAT_FW_LA_CMP_AUTH_SET(header->serv_specif_flags,
				   ICP_QAT_FW_LA_NO_CMP_AUTH_RES);
	cd_pars->u.s.content_desc_addr = ctx->enc_cd_paddr;
	cd_pars->u.s.content_desc_params_sz = sizeof(struct qat_alg_cd) >> 3;

	/* Cipher CD config setup */
	cipher_cd_ctrl->cipher_key_sz = keys->enckeylen >> 3;
	cipher_cd_ctrl->cipher_state_sz = AES_BLOCK_SIZE >> 3;
	cipher_cd_ctrl->cipher_cfg_offset = 0;
	ICP_QAT_FW_COMN_CURR_ID_SET(cipher_cd_ctrl, ICP_QAT_FW_SLICE_CIPHER);
	ICP_QAT_FW_COMN_NEXT_ID_SET(cipher_cd_ctrl, ICP_QAT_FW_SLICE_AUTH);
	/* Auth CD config setup */
	hash_cd_ctrl->hash_cfg_offset = ((char *)hash - (char *)cipher) >> 3;
	hash_cd_ctrl->hash_flags = ICP_QAT_FW_AUTH_HDR_FLAG_NO_NESTED;
	hash_cd_ctrl->inner_res_sz = digestsize;
	hash_cd_ctrl->final_sz = digestsize;

	switch (ctx->qat_hash_alg) {
	case ICP_QAT_HW_AUTH_ALGO_SHA1:
		hash_cd_ctrl->inner_state1_sz =
			round_up(ICP_QAT_HW_SHA1_STATE1_SZ, 8);
		hash_cd_ctrl->inner_state2_sz =
			round_up(ICP_QAT_HW_SHA1_STATE2_SZ, 8);
		break;
	case ICP_QAT_HW_AUTH_ALGO_SHA256:
		hash_cd_ctrl->inner_state1_sz = ICP_QAT_HW_SHA256_STATE1_SZ;
		hash_cd_ctrl->inner_state2_sz = ICP_QAT_HW_SHA256_STATE2_SZ;
		break;
	case ICP_QAT_HW_AUTH_ALGO_SHA512:
		hash_cd_ctrl->inner_state1_sz = ICP_QAT_HW_SHA512_STATE1_SZ;
		hash_cd_ctrl->inner_state2_sz = ICP_QAT_HW_SHA512_STATE2_SZ;
		break;
	default:
		break;
	}
	hash_cd_ctrl->inner_state2_offset = hash_cd_ctrl->hash_cfg_offset +
			((sizeof(struct icp_qat_hw_auth_setup) +
			 round_up(hash_cd_ctrl->inner_state1_sz, 8)) >> 3);
	ICP_QAT_FW_COMN_CURR_ID_SET(hash_cd_ctrl, ICP_QAT_FW_SLICE_AUTH);
	ICP_QAT_FW_COMN_NEXT_ID_SET(hash_cd_ctrl, ICP_QAT_FW_SLICE_DRAM_WR);
	return 0;
}

static int qat_alg_aead_init_dec_session(struct crypto_aead *aead_tfm,
					 int alg,
					 struct crypto_authenc_keys *keys,
					 int mode)
{
	struct qat_alg_aead_ctx *ctx = crypto_aead_ctx(aead_tfm);
	unsigned int digestsize = crypto_aead_authsize(aead_tfm);
	struct qat_dec *dec_ctx = &ctx->dec_cd->qat_dec_cd;
	struct icp_qat_hw_auth_algo_blk *hash = &dec_ctx->hash;
	struct icp_qat_hw_cipher_algo_blk *cipher =
		(struct icp_qat_hw_cipher_algo_blk *)((char *)dec_ctx +
		sizeof(struct icp_qat_hw_auth_setup) +
		roundup(crypto_shash_digestsize(ctx->hash_tfm), 8) * 2);
	struct icp_qat_fw_la_bulk_req *req_tmpl = &ctx->dec_fw_req;
	struct icp_qat_fw_comn_req_hdr_cd_pars *cd_pars = &req_tmpl->cd_pars;
	struct icp_qat_fw_comn_req_hdr *header = &req_tmpl->comn_hdr;
	void *ptr = &req_tmpl->cd_ctrl;
	struct icp_qat_fw_cipher_cd_ctrl_hdr *cipher_cd_ctrl = ptr;
	struct icp_qat_fw_auth_cd_ctrl_hdr *hash_cd_ctrl = ptr;
	struct icp_qat_fw_la_auth_req_params *auth_param =
		(struct icp_qat_fw_la_auth_req_params *)
		((char *)&req_tmpl->serv_specif_rqpars +
			ICP_QAT_FW_HASH_REQUEST_PARAMETERS_OFFSET);

	/* CD setup */
	cipher->aes256_f8.cipher_config.val = QAT_AES_HW_CONFIG_DEC(alg, mode,
								    0);
	memcpy(cipher->aes256_f8.key, keys->enckey, keys->enckeylen);
	hash->sha512.inner_setup.auth_config.config =
		ICP_QAT_HW_AUTH_CONFIG_BUILD(ICP_QAT_HW_AUTH_MODE1,
					     ctx->qat_hash_alg,
					     digestsize);
	hash->sha512.inner_setup.auth_counter.counter =
		cpu_to_be32(crypto_shash_blocksize(ctx->hash_tfm));

	if (qat_alg_do_precomputes(hash, ctx, keys->authkey, keys->authkeylen))
		return -EFAULT;

	/* Request setup */
	qat_alg_init_common_hdr(header, 1);
	header->service_cmd_id = ICP_QAT_FW_LA_CMD_HASH_CIPHER;
	ICP_QAT_FW_LA_DIGEST_IN_BUFFER_SET(header->serv_specif_flags,
					   ICP_QAT_FW_LA_DIGEST_IN_BUFFER);
	ICP_QAT_FW_LA_RET_AUTH_SET(header->serv_specif_flags,
				   ICP_QAT_FW_LA_NO_RET_AUTH_RES);
	ICP_QAT_FW_LA_CMP_AUTH_SET(header->serv_specif_flags,
				   ICP_QAT_FW_LA_CMP_AUTH_RES);
	cd_pars->u.s.content_desc_addr = ctx->dec_cd_paddr;
	cd_pars->u.s.content_desc_params_sz = sizeof(struct qat_alg_cd) >> 3;

	/* Cipher CD config setup */
	cipher_cd_ctrl->cipher_key_sz = keys->enckeylen >> 3;
	cipher_cd_ctrl->cipher_state_sz = AES_BLOCK_SIZE >> 3;
	cipher_cd_ctrl->cipher_cfg_offset =
		(sizeof(struct icp_qat_hw_auth_setup) +
		 roundup(crypto_shash_digestsize(ctx->hash_tfm), 8) * 2) >> 3;
	ICP_QAT_FW_COMN_CURR_ID_SET(cipher_cd_ctrl, ICP_QAT_FW_SLICE_CIPHER);
	ICP_QAT_FW_COMN_NEXT_ID_SET(cipher_cd_ctrl, ICP_QAT_FW_SLICE_DRAM_WR);

	/* Auth CD config setup */
	hash_cd_ctrl->hash_cfg_offset = 0;
	hash_cd_ctrl->hash_flags = ICP_QAT_FW_AUTH_HDR_FLAG_NO_NESTED;
	hash_cd_ctrl->inner_res_sz = digestsize;
	hash_cd_ctrl->final_sz = digestsize;

	switch (ctx->qat_hash_alg) {
	case ICP_QAT_HW_AUTH_ALGO_SHA1:
		hash_cd_ctrl->inner_state1_sz =
			round_up(ICP_QAT_HW_SHA1_STATE1_SZ, 8);
		hash_cd_ctrl->inner_state2_sz =
			round_up(ICP_QAT_HW_SHA1_STATE2_SZ, 8);
		break;
	case ICP_QAT_HW_AUTH_ALGO_SHA256:
		hash_cd_ctrl->inner_state1_sz = ICP_QAT_HW_SHA256_STATE1_SZ;
		hash_cd_ctrl->inner_state2_sz = ICP_QAT_HW_SHA256_STATE2_SZ;
		break;
	case ICP_QAT_HW_AUTH_ALGO_SHA512:
		hash_cd_ctrl->inner_state1_sz = ICP_QAT_HW_SHA512_STATE1_SZ;
		hash_cd_ctrl->inner_state2_sz = ICP_QAT_HW_SHA512_STATE2_SZ;
		break;
	default:
		break;
	}

	hash_cd_ctrl->inner_state2_offset = hash_cd_ctrl->hash_cfg_offset +
			((sizeof(struct icp_qat_hw_auth_setup) +
			 round_up(hash_cd_ctrl->inner_state1_sz, 8)) >> 3);
	auth_param->auth_res_sz = digestsize;
	ICP_QAT_FW_COMN_CURR_ID_SET(hash_cd_ctrl, ICP_QAT_FW_SLICE_AUTH);
	ICP_QAT_FW_COMN_NEXT_ID_SET(hash_cd_ctrl, ICP_QAT_FW_SLICE_CIPHER);
	return 0;
}

#if KERNEL_VERSION(5, 5, 0) > LINUX_VERSION_CODE
static void qat_alg_ablkcipher_init_com(struct qat_alg_ablkcipher_ctx *ctx,
					struct icp_qat_fw_la_bulk_req *req,
					struct icp_qat_hw_cipher_algo_blk *cd,
					const u8 *key, unsigned int keylen,
					int mode)
{
	struct icp_qat_fw_comn_req_hdr_cd_pars *cd_pars = &req->cd_pars;
	struct icp_qat_fw_comn_req_hdr *header = &req->comn_hdr;
	struct icp_qat_fw_cipher_cd_ctrl_hdr *cd_ctrl = (void *)&req->cd_ctrl;

	qat_alg_init_common_hdr(header, 0);
	header->service_cmd_id = ICP_QAT_FW_LA_CMD_CIPHER;
	cd_pars->u.s.content_desc_params_sz =
		sizeof(struct icp_qat_hw_cipher_algo_blk) >> 3;

	/* Make use of UCS hardware AES-CTR and AES-XTS ciphers for devices
	 * that support it
	 */

	if (HW_CAP_AES_V2(ctx->inst->accel_dev) &&
	    mode == ICP_QAT_HW_CIPHER_XTS_MODE) {
		/* set slice type */
		ICP_QAT_FW_LA_SLICE_TYPE_SET(header->serv_specif_flags,
					     ICP_QAT_FW_LA_USE_UCS_SLICE_TYPE);

		/* Store both XTS keys in CD, only first key is sent
		 * to the HW, second key is used for tweak calculation
		 */
		memcpy(cd->ucs_aes256_f8.key, key, keylen);
		keylen = keylen / 2;
	} else if (HW_CAP_AES_V2(ctx->inst->accel_dev) &&
		   mode == ICP_QAT_HW_CIPHER_CTR_MODE) {
		ICP_QAT_FW_LA_SLICE_TYPE_SET(header->serv_specif_flags,
					     ICP_QAT_FW_LA_USE_UCS_SLICE_TYPE);
		/* UCS slice requires key size to be a multiple of 16 */
		keylen = round_up(keylen, 16);
		memcpy(cd->ucs_aes256_f8.key, key, keylen);
	} else {
		memcpy(cd->aes256_f8.key, key, keylen);
	}

	/* Cipher CD config setup */
	cd_ctrl->cipher_key_sz = keylen >> 3;
	cd_ctrl->cipher_state_sz = AES_BLOCK_SIZE >> 3;
	cd_ctrl->cipher_cfg_offset = 0;
	ICP_QAT_FW_COMN_CURR_ID_SET(cd_ctrl, ICP_QAT_FW_SLICE_CIPHER);
	ICP_QAT_FW_COMN_NEXT_ID_SET(cd_ctrl, ICP_QAT_FW_SLICE_DRAM_WR);

	ctx->mode = mode;
}
#else
static void qat_alg_skcipher_init_com(struct qat_alg_skcipher_ctx *ctx,
				      struct icp_qat_fw_la_bulk_req *req,
				      struct icp_qat_hw_cipher_algo_blk *cd,
				      const u8 *key, unsigned int keylen,
				      int mode)
{
	struct icp_qat_fw_comn_req_hdr_cd_pars *cd_pars = &req->cd_pars;
	struct icp_qat_fw_comn_req_hdr *header = &req->comn_hdr;
	struct icp_qat_fw_cipher_cd_ctrl_hdr *cd_ctrl = (void *)&req->cd_ctrl;

	qat_alg_init_common_hdr(header, 0);

	header->service_cmd_id = ICP_QAT_FW_LA_CMD_CIPHER;
	cd_pars->u.s.content_desc_params_sz =
				sizeof(struct icp_qat_hw_cipher_algo_blk) >> 3;

	/* Make use of UCS hardware AES-CTR and AES-XTS ciphers for devices
	 * that support it
	 */

	if (HW_CAP_AES_V2(ctx->inst->accel_dev) &&
	    mode == ICP_QAT_HW_CIPHER_XTS_MODE) {
		/* set slice type */
		ICP_QAT_FW_LA_SLICE_TYPE_SET(header->serv_specif_flags,
					     ICP_QAT_FW_LA_USE_UCS_SLICE_TYPE);

		/* Store both XTS keys in CD, only first key is sent
		 * to the HW, second key is used for tweak calculation
		 */
		memcpy(cd->ucs_aes256_f8.key, key, keylen);
		keylen = keylen / 2;
	} else if (HW_CAP_AES_V2(ctx->inst->accel_dev) &&
		   mode == ICP_QAT_HW_CIPHER_CTR_MODE) {
		ICP_QAT_FW_LA_SLICE_TYPE_SET(header->serv_specif_flags,
					     ICP_QAT_FW_LA_USE_UCS_SLICE_TYPE);
		memcpy(cd->ucs_aes256_f8.key, key, keylen);
		/* UCS slice requires key size to be a multiple of 16 */
		keylen = round_up(keylen, 16);
	} else {
		memcpy(cd->aes256_f8.key, key, keylen);
	}

	/* Cipher CD config setup */
	cd_ctrl->cipher_key_sz = keylen >> 3;
	cd_ctrl->cipher_state_sz = AES_BLOCK_SIZE >> 3;
	cd_ctrl->cipher_cfg_offset = 0;
	ICP_QAT_FW_COMN_CURR_ID_SET(cd_ctrl, ICP_QAT_FW_SLICE_CIPHER);
	ICP_QAT_FW_COMN_NEXT_ID_SET(cd_ctrl, ICP_QAT_FW_SLICE_DRAM_WR);

	ctx->mode = mode;
}
#endif

#if KERNEL_VERSION(5, 5, 0) > LINUX_VERSION_CODE
static void qat_alg_ablkcipher_init_enc(struct qat_alg_ablkcipher_ctx *ctx,
					int alg, const uint8_t *key,
					unsigned int keylen, int mode)
{
	struct icp_qat_hw_cipher_algo_blk *enc_cd = ctx->enc_cd;
	struct icp_qat_fw_la_bulk_req *req = &ctx->enc_fw_req;
	struct icp_qat_fw_comn_req_hdr_cd_pars *cd_pars = &req->cd_pars;

	qat_alg_ablkcipher_init_com(ctx, req, enc_cd, key, keylen, mode);
	cd_pars->u.s.content_desc_addr = ctx->enc_cd_paddr;
	enc_cd->aes256_f8.cipher_config.val = QAT_AES_HW_CONFIG_ENC(alg, mode,
								    0);
}
#else
static void qat_alg_skcipher_init_enc(struct qat_alg_skcipher_ctx *ctx,
				      int alg, const uint8_t *key,
				      unsigned int keylen, int mode)
{
	struct icp_qat_hw_cipher_algo_blk *enc_cd = ctx->enc_cd;
	struct icp_qat_fw_la_bulk_req *req = &ctx->enc_fw_req;
	struct icp_qat_fw_comn_req_hdr_cd_pars *cd_pars = &req->cd_pars;

	qat_alg_skcipher_init_com(ctx, req, enc_cd, key, keylen, mode);
	cd_pars->u.s.content_desc_addr = ctx->enc_cd_paddr;
	enc_cd->aes256_f8.cipher_config.val = QAT_AES_HW_CONFIG_ENC(alg, mode,
								    0);
}
#endif

static void qat_alg_xts_reverse_key(const u8 *key_forward,
				    unsigned int keylen, u8 *key_reverse)
{
	struct crypto_aes_ctx aes_expanded;
	int nrounds;
	u8 *expanded_key;

#if KERNEL_VERSION(5, 4, 0) > LINUX_VERSION_CODE
	crypto_aes_expand_key(&aes_expanded, key_forward, keylen);
#else
	aes_expandkey(&aes_expanded, key_forward, keylen);
#endif
	expanded_key = (u8 *)aes_expanded.key_enc;

	if (keylen == AES_KEYSIZE_128) {
		nrounds = 10;
		memcpy(key_reverse,
		       expanded_key + (AES_BLOCK_SIZE * nrounds),
		       AES_BLOCK_SIZE);
	} else {
		/* AES_KEYSIZE_256 */
		nrounds = 14;
		memcpy(key_reverse,
		       expanded_key + (AES_BLOCK_SIZE * nrounds),
		       AES_BLOCK_SIZE);
		memcpy((key_reverse + AES_BLOCK_SIZE),
		       expanded_key +
			       (AES_BLOCK_SIZE * (nrounds - 1)),
		       AES_BLOCK_SIZE);
	}
}

#if KERNEL_VERSION(5, 5, 0) > LINUX_VERSION_CODE
static void qat_alg_ablkcipher_init_dec(struct qat_alg_ablkcipher_ctx *ctx,
					int alg, const uint8_t *key,
					unsigned int keylen, int mode)
{
	struct icp_qat_hw_cipher_algo_blk *dec_cd = ctx->dec_cd;
	struct icp_qat_fw_la_bulk_req *req = &ctx->dec_fw_req;
	struct icp_qat_fw_comn_req_hdr_cd_pars *cd_pars = &req->cd_pars;

	qat_alg_ablkcipher_init_com(ctx, req, dec_cd, key, keylen, mode);
	cd_pars->u.s.content_desc_addr = ctx->dec_cd_paddr;

	if (HW_CAP_AES_V2(ctx->inst->accel_dev) &&
	    mode == ICP_QAT_HW_CIPHER_XTS_MODE) {
		/* UCS slice does not support key reversing
		 * so ICP_QAT_HW_CIPHER_NO_CONVERT flag is used
		 */
		dec_cd->aes256_f8.cipher_config.val =
			QAT_AES_HW_CONFIG_DEC_NO_CONV(alg, mode, 0);

		/* In-place key reversal */
		qat_alg_xts_reverse_key(dec_cd->ucs_aes256_f8.key, keylen / 2,
					dec_cd->ucs_aes256_f8.key);
	} else if (mode != ICP_QAT_HW_CIPHER_CTR_MODE) {
		dec_cd->aes256_f8.cipher_config.val =
				QAT_AES_HW_CONFIG_DEC(alg, mode, 0);
	} else {
		dec_cd->aes256_f8.cipher_config.val =
				QAT_AES_HW_CONFIG_ENC(alg, mode, 0);
	}
}
#else
static void qat_alg_skcipher_init_dec(struct qat_alg_skcipher_ctx *ctx,
				      int alg, const uint8_t *key,
				      unsigned int keylen, int mode)
{
	struct icp_qat_hw_cipher_algo_blk *dec_cd = ctx->dec_cd;
	struct icp_qat_fw_la_bulk_req *req = &ctx->dec_fw_req;
	struct icp_qat_fw_comn_req_hdr_cd_pars *cd_pars = &req->cd_pars;

	qat_alg_skcipher_init_com(ctx, req, dec_cd, key, keylen, mode);
	cd_pars->u.s.content_desc_addr = ctx->dec_cd_paddr;

	if (HW_CAP_AES_V2(ctx->inst->accel_dev) &&
	    mode == ICP_QAT_HW_CIPHER_XTS_MODE) {
		/* UCS slice does not support key reversing
		 * so ICP_QAT_HW_CIPHER_NO_CONVERT flag is used
		 */
		dec_cd->aes256_f8.cipher_config.val =
			QAT_AES_HW_CONFIG_DEC_NO_CONV(alg, mode, 0);

		/* In-place key reversal */
		qat_alg_xts_reverse_key(dec_cd->ucs_aes256_f8.key, keylen / 2,
					dec_cd->ucs_aes256_f8.key);
	} else if (mode != ICP_QAT_HW_CIPHER_CTR_MODE) {
		dec_cd->aes256_f8.cipher_config.val =
				QAT_AES_HW_CONFIG_DEC(alg, mode, 0);
	} else {
		dec_cd->aes256_f8.cipher_config.val =
				QAT_AES_HW_CONFIG_ENC(alg, mode, 0);
	}
}
#endif

static int qat_alg_validate_key(int key_len, int *alg, int mode)
{
	if (mode != ICP_QAT_HW_CIPHER_XTS_MODE) {
		switch (key_len) {
		case AES_KEYSIZE_128:
			*alg = ICP_QAT_HW_CIPHER_ALGO_AES128;
			break;
		case AES_KEYSIZE_192:
			*alg = ICP_QAT_HW_CIPHER_ALGO_AES192;
			break;
		case AES_KEYSIZE_256:
			*alg = ICP_QAT_HW_CIPHER_ALGO_AES256;
			break;
		default:
			return -EINVAL;
		}
	} else {
		switch (key_len) {
		case AES_KEYSIZE_128 << 1:
			*alg = ICP_QAT_HW_CIPHER_ALGO_AES128;
			break;
		case AES_KEYSIZE_256 << 1:
			*alg = ICP_QAT_HW_CIPHER_ALGO_AES256;
			break;
		default:
			return -EINVAL;
		}
	}
	return 0;
}

static int qat_alg_aead_init_sessions(struct crypto_aead *tfm, const u8 *key,
				      unsigned int keylen,  int mode)
{
	struct crypto_authenc_keys keys;
	int alg;

	if (crypto_authenc_extractkeys(&keys, key, keylen))
		goto bad_key;

	if (qat_alg_validate_key(keys.enckeylen, &alg, mode))
		goto bad_key;

	if (qat_alg_aead_init_enc_session(tfm, alg, &keys, mode))
		goto error;

	if (qat_alg_aead_init_dec_session(tfm, alg, &keys, mode))
		goto error;

	return 0;
bad_key:
	return -EINVAL;
error:
	return -EFAULT;
}

#if KERNEL_VERSION(5, 5, 0) > LINUX_VERSION_CODE
static int qat_alg_ablkcipher_init_sessions(struct qat_alg_ablkcipher_ctx *ctx,
					    const u8 *key,
					    unsigned int keylen,
					    int mode)
{
	int alg;

	if (qat_alg_validate_key(keylen, &alg, mode))
		goto bad_key;

	qat_alg_ablkcipher_init_enc(ctx, alg, key, keylen, mode);
	qat_alg_ablkcipher_init_dec(ctx, alg, key, keylen, mode);
	return 0;
bad_key:
	crypto_tfm_set_flags(ctx->tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
	return -EINVAL;
}
#else
static int qat_alg_skcipher_init_sessions(struct qat_alg_skcipher_ctx *ctx,
					  const u8 *key,
					  unsigned int keylen,
					  int mode)
{
	int alg;

	if (qat_alg_validate_key(keylen, &alg, mode))
		return -EINVAL;

	qat_alg_skcipher_init_enc(ctx, alg, key, keylen, mode);
	qat_alg_skcipher_init_dec(ctx, alg, key, keylen, mode);
	return 0;
}
#endif

static int qat_alg_aead_rekey(struct crypto_aead *tfm, const uint8_t *key,
			      unsigned int keylen)
{
	struct qat_alg_aead_ctx *ctx = crypto_aead_ctx(tfm);

	memzero_explicit(ctx->enc_cd, sizeof(*ctx->enc_cd));
	memzero_explicit(ctx->dec_cd, sizeof(*ctx->dec_cd));
	memzero_explicit(&ctx->enc_fw_req, sizeof(ctx->enc_fw_req));
	memzero_explicit(&ctx->dec_fw_req, sizeof(ctx->dec_fw_req));

	return qat_alg_aead_init_sessions(tfm, key, keylen,
					  ICP_QAT_HW_CIPHER_CBC_MODE);
}

static int qat_alg_aead_newkey(struct crypto_aead *tfm, const uint8_t *key,
			       unsigned int keylen)
{
	struct qat_alg_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct qat_crypto_instance *inst = NULL;
	struct device *dev = NULL;
	int node = get_current_node();
	int ret;

	inst = qat_crypto_get_instance_node(node, SYM);
	if (!inst)
		return -EINVAL;
	dev = &GET_DEV(inst->accel_dev);
	ctx->inst = inst;
	ctx->enc_cd = dma_alloc_coherent(dev, sizeof(*ctx->enc_cd),
					 &ctx->enc_cd_paddr,
					 GFP_ATOMIC);
	if (!ctx->enc_cd) {
		ret = -ENOMEM;
		goto out_free_inst;
	}
	ctx->dec_cd = dma_alloc_coherent(dev, sizeof(*ctx->dec_cd),
					 &ctx->dec_cd_paddr,
					 GFP_ATOMIC);
	if (!ctx->dec_cd) {
		ret = -ENOMEM;
		goto out_free_enc;
	}

	ret = qat_alg_aead_init_sessions(tfm, key, keylen,
					 ICP_QAT_HW_CIPHER_CBC_MODE);
	if (ret)
		goto out_free_all;

	return 0;

out_free_all:
	memzero_explicit(ctx->dec_cd, sizeof(struct qat_alg_cd));
	dma_free_coherent(dev, sizeof(struct qat_alg_cd),
			  ctx->dec_cd, ctx->dec_cd_paddr);
	ctx->dec_cd = NULL;
out_free_enc:
	memzero_explicit(ctx->enc_cd, sizeof(struct qat_alg_cd));
	dma_free_coherent(dev, sizeof(struct qat_alg_cd),
			  ctx->enc_cd, ctx->enc_cd_paddr);
	ctx->enc_cd = NULL;
out_free_inst:
	ctx->inst = NULL;
	qat_crypto_put_instance(inst);
	return ret;
}

static int qat_alg_aead_setkey(struct crypto_aead *tfm, const uint8_t *key,
			       unsigned int keylen)
{
	struct qat_alg_aead_ctx *ctx = crypto_aead_ctx(tfm);

	if (ctx->enc_cd)
		return qat_alg_aead_rekey(tfm, key, keylen);
	else
		return qat_alg_aead_newkey(tfm, key, keylen);
}

static void qat_alg_free_bufl(struct qat_crypto_instance *inst,
			      struct qat_crypto_request *qat_req)
{
	struct device *dev = &GET_DEV(inst->accel_dev);
	struct qat_alg_buf_list *bl = qat_req->buf.bl;
	struct qat_alg_buf_list *blout = qat_req->buf.blout;
	dma_addr_t blp = qat_req->buf.blp;
	dma_addr_t blpout = qat_req->buf.bloutp;
	size_t sz = qat_req->buf.sz;
	size_t sz_out = qat_req->buf.sz_out;
	int i;

	for (i = 0; i < bl->num_bufs; i++)
		dma_unmap_single(dev, bl->bufers[i].addr,
				 bl->bufers[i].len, DMA_BIDIRECTIONAL);

	dma_unmap_single(dev, blp, sz, DMA_TO_DEVICE);
	if (bl->is_buf_dynamic)
		kfree(bl);
	if (blp != blpout) {
		/* If out of place operation dma unmap only data */
		int bufless = blout->num_bufs - blout->num_mapped_bufs;

		for (i = bufless; i < blout->num_bufs; i++) {
			dma_unmap_single(dev, blout->bufers[i].addr,
					 blout->bufers[i].len,
					 DMA_BIDIRECTIONAL);
		}
		dma_unmap_single(dev, blpout, sz_out, DMA_TO_DEVICE);
		if (blout->is_buf_dynamic)
			kfree(blout);
	}
}

static int qat_alg_sgl_to_bufl(struct qat_crypto_instance *inst,
			       struct scatterlist *sgl,
			       struct scatterlist *sglout,
			       struct qat_crypto_request *qat_req,
			       gfp_t flags)
{
	struct device *dev = &GET_DEV(inst->accel_dev);
	int i = 0, sg_nctr = 0, n = sg_nents(sgl);
	struct qat_alg_buf_list *bufl = NULL, *buflout = NULL;
	dma_addr_t blp = 0;
	dma_addr_t bloutp = 0;
	struct scatterlist *sg = NULL;
	size_t sz_out = 0, sz = 0;
	int node = dev_to_node(&GET_DEV(inst->accel_dev));

	if (unlikely(!n))
		return -EINVAL;

	if (n > QAT_MAX_PRE_ALLOCATED_BUFFS) {
		/* Assumption: For dm-crypt, max SGLs can be 4.
		 * If SGLs are more than 4, memory for bufflists is done
		 * at run time.
		 */
		sz = sizeof(struct qat_alg_buf_list) +
			(n * sizeof(struct qat_alg_buf));

		bufl = kzalloc_node(sz, flags, node);
		if (unlikely(!bufl))
			return -ENOMEM;
		bufl->is_buf_dynamic = true;
	} else {
		bufl = &qat_req->bufl;
		sz = sizeof(struct qat_alg_buf_list);
		bufl->is_buf_dynamic = false;
	}

	blp = dma_map_single(dev, bufl, sz, DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(dev, blp)))
		goto err;

	for_each_sg(sgl, sg, n, i) {
		int y = sg_nctr;

		if (!sg->length)
			continue;

		bufl->bufers[y].addr = dma_map_single(dev, sg_virt(sg),
						      sg->length,
						      DMA_BIDIRECTIONAL);
		bufl->bufers[y].len = sg->length;
		if (unlikely(dma_mapping_error(dev, bufl->bufers[y].addr)))
			goto err;
		sg_nctr++;
	}
	bufl->num_bufs = sg_nctr;
	qat_req->buf.bl = bufl;
	qat_req->buf.blp = blp;
	qat_req->buf.sz = sz;
	/* Handle out of place operation */
	if (sgl != sglout) {
		struct qat_alg_buf *bufers;

		n = sg_nents(sglout);
		if (n > QAT_MAX_PRE_ALLOCATED_BUFFS) {
			/* Assumption: For dm-crypt, max SGLs can be 4.
			 * If SGLs are more than 4, memory for bufflists is done
			 * at run time.
			 */
			sz_out = sizeof(struct qat_alg_buf_list) +
					(n * sizeof(struct qat_alg_buf));

			buflout = kzalloc_node(sz_out, flags, node);
			if (unlikely(!buflout))
				return -ENOMEM;
			buflout->is_buf_dynamic = true;
		} else {
			buflout = &qat_req->buflout;
			sz_out = sizeof(struct qat_alg_buf_list);
			buflout->is_buf_dynamic = false;
		}
		sg_nctr = 0;
		bloutp = dma_map_single(dev, buflout, sz_out, DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(dev, bloutp)))
			goto err;
		bufers = buflout->bufers;
		for_each_sg(sglout, sg, n, i) {
			int y = sg_nctr;

			if (!sg->length)
				continue;

			bufers[y].addr = dma_map_single(dev, sg_virt(sg),
							sg->length,
							DMA_BIDIRECTIONAL);
			if (unlikely(dma_mapping_error(dev, bufers[y].addr)))
				goto err;
			bufers[y].len = sg->length;
			sg_nctr++;
		}
		buflout->num_bufs = sg_nctr;
		buflout->num_mapped_bufs = sg_nctr;
		qat_req->buf.blout = buflout;
		qat_req->buf.bloutp = bloutp;
		qat_req->buf.sz_out = sz_out;
	} else {
		/* Otherwise set the src and dst to the same address */
		qat_req->buf.bloutp = qat_req->buf.blp;
		qat_req->buf.sz_out = 0;
	}
	return 0;
err:
	dev_err(dev, "Failed to map buf for dma\n");
	for (i = 0; i < n; i++)
		if (!dma_mapping_error(dev, bufl->bufers[i].addr))
			dma_unmap_single(dev, bufl->bufers[i].addr,
					 bufl->bufers[i].len,
					 DMA_BIDIRECTIONAL);

	if (!dma_mapping_error(dev, blp))
		dma_unmap_single(dev, blp, sz, DMA_TO_DEVICE);
	if (bufl->is_buf_dynamic)
		kfree(bufl);
	if (sgl != sglout && buflout) {
		n = sg_nents(sglout);
		for (i = 0; i < n; i++)
			if (!dma_mapping_error(dev, buflout->bufers[i].addr))
				dma_unmap_single(dev, buflout->bufers[i].addr,
						 buflout->bufers[i].len,
						 DMA_BIDIRECTIONAL);
		if (!dma_mapping_error(dev, bloutp))
			dma_unmap_single(dev, bloutp, sz_out, DMA_TO_DEVICE);
		if (buflout->is_buf_dynamic)
			kfree(buflout);
	}
	return -ENOMEM;
}

static void qat_aead_alg_callback(struct icp_qat_fw_la_resp *qat_resp,
				  struct qat_crypto_request *qat_req)
{
	struct qat_alg_aead_ctx *ctx = qat_req->aead_ctx;
	struct qat_crypto_instance *inst = ctx->inst;
	struct aead_request *areq = qat_req->aead_req;
	u8 stat_filed = qat_resp->comn_resp.comn_status;
	int res = 0, qat_res = ICP_QAT_FW_COMN_RESP_CRYPTO_STAT_GET(stat_filed);

	qat_alg_free_bufl(inst, qat_req);
	if (unlikely(qat_res != ICP_QAT_FW_COMN_STATUS_FLAG_OK))
		res = -EBADMSG;
	areq->base.complete(&areq->base, res);
}

static void qat_alg_update_iv_ctr_mode(struct qat_crypto_request *qat_req)
{
	u64 *iv_lo;
	u64 *iv_hi;
	u64 iv_lo_prev;
	u64 cipher_len;

#if KERNEL_VERSION(5, 5, 0) > LINUX_VERSION_CODE
	struct ablkcipher_request *areq = qat_req->ablkcipher_req;

	cipher_len = areq->nbytes;
	memcpy(qat_req->iv, areq->info, AES_BLOCK_SIZE);
#else
	struct skcipher_request *areq = qat_req->skcipher_req;

	cipher_len = areq->cryptlen;
	memcpy(qat_req->iv, areq->iv, AES_BLOCK_SIZE);
#endif
	iv_lo = &qat_req->iv_lo;
	iv_hi = &qat_req->iv_hi;

	*iv_lo = be64_to_cpu(*iv_lo);
	*iv_hi = be64_to_cpu(*iv_hi);
	iv_lo_prev = *iv_lo;

	*iv_lo += (roundup(cipher_len, AES_BLOCK_SIZE) / AES_BLOCK_SIZE);
	if (*iv_lo < iv_lo_prev)
		(*iv_hi)++;

	*iv_lo = cpu_to_be64(*iv_lo);
	*iv_hi = cpu_to_be64(*iv_hi);
}

static void qat_alg_update_iv_cbc_mode(struct qat_crypto_request *qat_req)
{
	u64 cipher_len;
	u64 iv_offset;
	struct scatterlist *sgl;
#if KERNEL_VERSION(5, 5, 0) > LINUX_VERSION_CODE
	struct ablkcipher_request *areq = qat_req->ablkcipher_req;

	cipher_len = areq->nbytes;
#else
	struct skcipher_request *areq = qat_req->skcipher_req;

	cipher_len = areq->cryptlen;
#endif
	iv_offset = cipher_len - AES_BLOCK_SIZE;

	if (qat_req->encryption)
		sgl = areq->dst;
	else
		sgl = areq->src;

	scatterwalk_map_and_copy(qat_req->iv, sgl,
				 iv_offset, AES_BLOCK_SIZE, 0);
}

static void qat_alg_update_iv(struct qat_crypto_request *qat_req)
{
#if KERNEL_VERSION(5, 5, 0) > LINUX_VERSION_CODE
	struct qat_alg_ablkcipher_ctx *ctx = qat_req->ablkcipher_ctx;
#else
	struct qat_alg_skcipher_ctx *ctx = qat_req->skcipher_ctx;
#endif
	struct device *dev = &GET_DEV(ctx->inst->accel_dev);

	switch (ctx->mode) {
	case ICP_QAT_HW_CIPHER_CTR_MODE:
		qat_alg_update_iv_ctr_mode(qat_req);
		break;
	case ICP_QAT_HW_CIPHER_CBC_MODE:
		qat_alg_update_iv_cbc_mode(qat_req);
		break;
	case ICP_QAT_HW_CIPHER_XTS_MODE:
		break;
	default:
		dev_warn(dev, "Unsupported IV update for cipher mode %d\n",
			 ctx->mode);
	}
}

#if KERNEL_VERSION(5, 5, 0) > LINUX_VERSION_CODE
static void qat_ablkcipher_alg_callback(struct icp_qat_fw_la_resp *qat_resp,
					struct qat_crypto_request *qat_req)
{
	struct qat_alg_ablkcipher_ctx *ctx = qat_req->ablkcipher_ctx;
	struct qat_crypto_instance *inst = ctx->inst;
	struct ablkcipher_request *areq = qat_req->ablkcipher_req;
	u8 stat_filed = qat_resp->comn_resp.comn_status;
	int res = 0, qat_res = ICP_QAT_FW_COMN_RESP_CRYPTO_STAT_GET(stat_filed);

	qat_alg_free_bufl(inst, qat_req);
	if (unlikely(qat_res != ICP_QAT_FW_COMN_STATUS_FLAG_OK))
		res = -EINVAL;

	if (qat_req->encryption)
		qat_alg_update_iv(qat_req);

	memcpy(areq->info, qat_req->iv, AES_BLOCK_SIZE);

	areq->base.complete(&areq->base, res);
}
#else
static void qat_skcipher_alg_callback(struct icp_qat_fw_la_resp *qat_resp,
				      struct qat_crypto_request *qat_req)
{
	struct qat_alg_skcipher_ctx *ctx = qat_req->skcipher_ctx;
	struct skcipher_request *areq = qat_req->skcipher_req;
	struct qat_crypto_instance *inst = ctx->inst;
	u8 stat_filed = qat_resp->comn_resp.comn_status;
	int res = 0, qat_res = ICP_QAT_FW_COMN_RESP_CRYPTO_STAT_GET(stat_filed);

	qat_alg_free_bufl(inst, qat_req);
	if (unlikely(qat_res != ICP_QAT_FW_COMN_STATUS_FLAG_OK))
		res = -EINVAL;

	if (qat_req->encryption)
		qat_alg_update_iv(qat_req);

	memcpy(areq->iv, qat_req->iv, AES_BLOCK_SIZE);

	areq->base.complete(&areq->base, res);
}
#endif

void qat_alg_callback(void *resp)
{
	struct icp_qat_fw_la_resp *qat_resp = resp;
	struct qat_crypto_request *qat_req =
				(void *)(__force long)qat_resp->opaque_data;

	qat_req->cb(qat_resp, qat_req);
}

static int qat_alg_aead_dec(struct aead_request *areq)
{
	struct crypto_aead *aead_tfm = crypto_aead_reqtfm(areq);
	struct crypto_tfm *tfm = crypto_aead_tfm(aead_tfm);
	struct qat_alg_aead_ctx *ctx = crypto_tfm_ctx(tfm);
	struct qat_crypto_request *qat_req = aead_request_ctx(areq);
	struct icp_qat_fw_la_cipher_req_params *cipher_param;
	struct icp_qat_fw_la_auth_req_params *auth_param;
	struct icp_qat_fw_la_bulk_req *msg;
	int digst_size = crypto_aead_authsize(aead_tfm);
	gfp_t flags = qat_algs_alloc_flags(&areq->base);
	u32 cipher_length = areq->cryptlen - digst_size;
	int ret;

	if (cipher_length % AES_BLOCK_SIZE != 0)
		return -EINVAL;

	ret = qat_alg_sgl_to_bufl(ctx->inst, areq->src, areq->dst, qat_req, flags);
	if (unlikely(ret))
		return ret;

	msg = &qat_req->req;
	*msg = ctx->dec_fw_req;
	qat_req->aead_ctx = ctx;
	qat_req->aead_req = areq;
	qat_req->cb = qat_aead_alg_callback;
	qat_req->req.comn_mid.opaque_data = (uint64_t)(__force long)qat_req;
	qat_req->req.comn_mid.src_data_addr = qat_req->buf.blp;
	qat_req->req.comn_mid.dest_data_addr = qat_req->buf.bloutp;
	cipher_param = (void *)&qat_req->req.serv_specif_rqpars;
	cipher_param->cipher_length = cipher_length;
	cipher_param->cipher_offset = areq->assoclen;
	memcpy(cipher_param->u.cipher_IV_array, areq->iv, AES_BLOCK_SIZE);
	auth_param = (void *)((uint8_t *)cipher_param +
			ICP_QAT_FW_HASH_REQUEST_PARAMETERS_OFFSET);
	auth_param->auth_off = 0;
	auth_param->auth_len = areq->assoclen + cipher_param->cipher_length;

	do {
		ret = adf_send_message(ctx->inst->sym_tx, (uint32_t *)msg);
		if (ret)
			cond_resched();
	} while (ret == -EAGAIN);

	return -EINPROGRESS;
}

static int qat_alg_aead_enc(struct aead_request *areq)
{
	struct crypto_aead *aead_tfm = crypto_aead_reqtfm(areq);
	struct crypto_tfm *tfm = crypto_aead_tfm(aead_tfm);
	struct qat_alg_aead_ctx *ctx = crypto_tfm_ctx(tfm);
	struct qat_crypto_request *qat_req = aead_request_ctx(areq);
	struct icp_qat_fw_la_cipher_req_params *cipher_param;
	struct icp_qat_fw_la_auth_req_params *auth_param;
	gfp_t flags = qat_algs_alloc_flags(&areq->base);
	struct icp_qat_fw_la_bulk_req *msg;
	u8 *iv = areq->iv;
	int ret;

	if (areq->cryptlen % AES_BLOCK_SIZE != 0)
		return -EINVAL;

	ret = qat_alg_sgl_to_bufl(ctx->inst, areq->src, areq->dst, qat_req, flags);
	if (unlikely(ret))
		return ret;

	msg = &qat_req->req;
	*msg = ctx->enc_fw_req;
	qat_req->aead_ctx = ctx;
	qat_req->aead_req = areq;
	qat_req->cb = qat_aead_alg_callback;
	qat_req->req.comn_mid.opaque_data = (uint64_t)(__force long)qat_req;
	qat_req->req.comn_mid.src_data_addr = qat_req->buf.blp;
	qat_req->req.comn_mid.dest_data_addr = qat_req->buf.bloutp;
	cipher_param = (void *)&qat_req->req.serv_specif_rqpars;
	auth_param = (void *)((uint8_t *)cipher_param +
			ICP_QAT_FW_HASH_REQUEST_PARAMETERS_OFFSET);

	memcpy(cipher_param->u.cipher_IV_array, iv, AES_BLOCK_SIZE);
	cipher_param->cipher_length = areq->cryptlen;
	cipher_param->cipher_offset = areq->assoclen;

	auth_param->auth_off = 0;
	auth_param->auth_len = areq->assoclen + areq->cryptlen;

	do {
		ret = adf_send_message(ctx->inst->sym_tx, (uint32_t *)msg);
		if (ret)
			cond_resched();
	} while (ret == -EAGAIN);

	return -EINPROGRESS;
}

#if KERNEL_VERSION(5, 5, 0) > LINUX_VERSION_CODE
static int qat_alg_ablkcipher_rekey(struct qat_alg_ablkcipher_ctx *ctx,
				    const u8 *key, unsigned int keylen,
				    int mode)
{
	memzero_explicit(ctx->enc_cd, sizeof(*ctx->enc_cd));
	memzero_explicit(ctx->dec_cd, sizeof(*ctx->dec_cd));
	memzero_explicit(&ctx->enc_fw_req, sizeof(ctx->enc_fw_req));
	memzero_explicit(&ctx->dec_fw_req, sizeof(ctx->dec_fw_req));

	return qat_alg_ablkcipher_init_sessions(ctx, key, keylen, mode);
}

static int qat_alg_ablkcipher_newkey(struct qat_alg_ablkcipher_ctx *ctx,
				     const u8 *key, unsigned int keylen,
				     int mode)
{
	struct qat_crypto_instance *inst = NULL;
	struct device *dev;
	int node = get_current_node();
	int ret;

	inst = qat_crypto_get_instance_node(node, SYM);
	if (!inst)
		return -EINVAL;
	dev = &GET_DEV(inst->accel_dev);
	ctx->inst = inst;
	ctx->enc_cd = dma_alloc_coherent(dev, sizeof(*ctx->enc_cd),
					 &ctx->enc_cd_paddr,
					 GFP_ATOMIC);
	if (!ctx->enc_cd) {
		ret = -ENOMEM;
		goto out_free_instance;
	}
	ctx->dec_cd = dma_alloc_coherent(dev, sizeof(*ctx->dec_cd),
					 &ctx->dec_cd_paddr,
					 GFP_ATOMIC);
	if (!ctx->dec_cd) {
		ret = -ENOMEM;
		goto out_free_enc;
	}

	ret = qat_alg_ablkcipher_init_sessions(ctx, key, keylen, mode);
	if (ret)
		goto out_free_all;

	return 0;

out_free_all:
	memzero_explicit(ctx->dec_cd, sizeof(*ctx->dec_cd));
	dma_free_coherent(dev, sizeof(*ctx->dec_cd),
			  ctx->dec_cd, ctx->dec_cd_paddr);
	ctx->dec_cd = NULL;
out_free_enc:
	memzero_explicit(ctx->enc_cd, sizeof(*ctx->enc_cd));
	dma_free_coherent(dev, sizeof(*ctx->enc_cd),
			  ctx->enc_cd, ctx->enc_cd_paddr);
	ctx->enc_cd = NULL;
out_free_instance:
	ctx->inst = NULL;
	qat_crypto_put_instance(inst);
	return ret;
}

static int qat_alg_ablkcipher_setkey(struct crypto_ablkcipher *tfm,
				     const u8 *key, unsigned int keylen,
				     int mode)
{
	struct qat_alg_ablkcipher_ctx *ctx = crypto_ablkcipher_ctx(tfm);

	if (ctx->enc_cd)
		return qat_alg_ablkcipher_rekey(ctx, key, keylen, mode);
	else
		return qat_alg_ablkcipher_newkey(ctx, key, keylen, mode);
}
#else
static int qat_alg_skcipher_rekey(struct qat_alg_skcipher_ctx *ctx,
				  const u8 *key, unsigned int keylen,
				  int mode)
{
	memzero_explicit(ctx->enc_cd, sizeof(*ctx->enc_cd));
	memzero_explicit(ctx->dec_cd, sizeof(*ctx->dec_cd));
	memzero_explicit(&ctx->enc_fw_req, sizeof(ctx->enc_fw_req));
	memzero_explicit(&ctx->dec_fw_req, sizeof(ctx->dec_fw_req));

	return qat_alg_skcipher_init_sessions(ctx, key, keylen, mode);
}

static int qat_alg_skcipher_newkey(struct qat_alg_skcipher_ctx *ctx,
				   const u8 *key, unsigned int keylen,
				   int mode)
{
	struct qat_crypto_instance *inst = NULL;
	struct device *dev;
	int node = get_current_node();
	int ret;

	inst = qat_crypto_get_instance_node(node, SYM);
	if (!inst)
		return -EINVAL;
	dev = &GET_DEV(inst->accel_dev);
	ctx->inst = inst;
	ctx->enc_cd = dma_alloc_coherent(dev, sizeof(*ctx->enc_cd),
					 &ctx->enc_cd_paddr,
					 GFP_ATOMIC);
	if (!ctx->enc_cd) {
		ret = -ENOMEM;
		goto out_free_instance;
	}
	ctx->dec_cd = dma_alloc_coherent(dev, sizeof(*ctx->dec_cd),
					 &ctx->dec_cd_paddr,
					 GFP_ATOMIC);
	if (!ctx->dec_cd) {
		ret = -ENOMEM;
		goto out_free_enc;
	}

	ret = qat_alg_skcipher_init_sessions(ctx, key, keylen, mode);
	if (ret)
		goto out_free_all;

	return 0;

out_free_all:
	memzero_explicit(ctx->dec_cd, sizeof(*ctx->dec_cd));
	dma_free_coherent(dev, sizeof(*ctx->dec_cd),
			  ctx->dec_cd, ctx->dec_cd_paddr);
	ctx->dec_cd = NULL;
out_free_enc:
	memzero_explicit(ctx->enc_cd, sizeof(*ctx->enc_cd));
	dma_free_coherent(dev, sizeof(*ctx->enc_cd),
			  ctx->enc_cd, ctx->enc_cd_paddr);
	ctx->enc_cd = NULL;
out_free_instance:
	ctx->inst = NULL;
	qat_crypto_put_instance(inst);
	return ret;
}

static int qat_alg_skcipher_setkey(struct crypto_skcipher *tfm,
				   const u8 *key, unsigned int keylen,
				   int mode)
{
	struct qat_alg_skcipher_ctx *ctx = crypto_skcipher_ctx(tfm);

	if (ctx->enc_cd)
		return qat_alg_skcipher_rekey(ctx, key, keylen, mode);
	else
		return qat_alg_skcipher_newkey(ctx, key, keylen, mode);
}
#endif

#if KERNEL_VERSION(5, 5, 0) > LINUX_VERSION_CODE
static int qat_alg_ablkcipher_cbc_setkey(struct crypto_ablkcipher *tfm,
					 const u8 *key, unsigned int keylen)
{
	return qat_alg_ablkcipher_setkey(tfm, key, keylen,
					 ICP_QAT_HW_CIPHER_CBC_MODE);
}
#else
static int qat_alg_skcipher_cbc_setkey(struct crypto_skcipher *tfm,
				       const u8 *key, unsigned int keylen)
{
	return qat_alg_skcipher_setkey(tfm, key, keylen,
					 ICP_QAT_HW_CIPHER_CBC_MODE);
}
#endif

#if KERNEL_VERSION(5, 5, 0) > LINUX_VERSION_CODE
static int qat_alg_ablkcipher_ctr_setkey(struct crypto_ablkcipher *tfm,
					 const u8 *key, unsigned int keylen)
{
	return qat_alg_ablkcipher_setkey(tfm, key, keylen,
					 ICP_QAT_HW_CIPHER_CTR_MODE);
}
#else
static int qat_alg_skcipher_ctr_setkey(struct crypto_skcipher *tfm,
				       const u8 *key, unsigned int keylen)
{
	return qat_alg_skcipher_setkey(tfm, key, keylen,
					 ICP_QAT_HW_CIPHER_CTR_MODE);
}
#endif

#if KERNEL_VERSION(5, 5, 0) > LINUX_VERSION_CODE
static int qat_alg_ablkcipher_xts_setkey(struct crypto_ablkcipher *tfm,
					 const u8 *key, unsigned int keylen)
{
	return qat_alg_ablkcipher_setkey(tfm, key, keylen,
					 ICP_QAT_HW_CIPHER_XTS_MODE);
}
#else
static int qat_alg_skcipher_xts_setkey(struct crypto_skcipher *tfm,
				       const u8 *key, unsigned int keylen)
{
	struct qat_alg_skcipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	int ret;

	ret = xts_verify_key(tfm, key, keylen);
	if (ret)
		return ret;

	if (keylen >> 1 == AES_KEYSIZE_192) {
		ret = crypto_skcipher_setkey(ctx->ftfm, key, keylen);
		if (ret)
			return ret;

		ctx->fallback = true;

		return 0;
	}

	ctx->fallback = false;

	return qat_alg_skcipher_setkey(tfm, key, keylen,
					 ICP_QAT_HW_CIPHER_XTS_MODE);
}
#endif

#if KERNEL_VERSION(5, 5, 0) > LINUX_VERSION_CODE
static int qat_alg_xts_calculate_tweak(struct qat_crypto_request *qat_req,
				       struct qat_alg_ablkcipher_ctx *ctx,
				       u8 *iv)
{
	/* calculate tweak and store in cipher_param->u.cipher_IV_array */
	struct icp_qat_hw_cipher_algo_blk *cd;
	u8 *xts_key2;
	struct icp_qat_fw_la_cipher_req_params *cipher_param =
		(void *)&qat_req->req.serv_specif_rqpars;
	struct icp_qat_fw_cipher_cd_ctrl_hdr *cd_ctrl =
		(void *)&qat_req->req.cd_ctrl;
	unsigned int keylen = cd_ctrl->cipher_key_sz << 3;
	struct crypto_cipher *tweak_tfm;
	int ret;

	/* get second half of the XTS key */
	if (qat_req->encryption)
		cd = ctx->enc_cd;
	else
		cd = ctx->dec_cd;
	xts_key2 = cd->ucs_aes256_f8.key + keylen;

	tweak_tfm = crypto_alloc_cipher("aes", 0, 0);
	if (IS_ERR(tweak_tfm))
		return PTR_ERR(tweak_tfm);

	ret = crypto_cipher_setkey(tweak_tfm, xts_key2, keylen);
	if (unlikely(ret))
		goto free;

	crypto_cipher_encrypt_one(tweak_tfm,
				  (u8 *)cipher_param->u.cipher_IV_array, iv);

free:
	crypto_free_cipher(tweak_tfm);

	return ret;
}
#else
static int qat_alg_xts_calculate_tweak(struct qat_crypto_request *qat_req,
				       struct qat_alg_skcipher_ctx *ctx, u8 *iv)
{
	/* calculate tweak and store in cipher_param->u.cipher_IV_array */
	struct icp_qat_hw_cipher_algo_blk *cd;
	u8 *xts_key2;
	struct icp_qat_fw_la_cipher_req_params *cipher_param =
		(void *)&qat_req->req.serv_specif_rqpars;
	struct icp_qat_fw_cipher_cd_ctrl_hdr *cd_ctrl =
		(void *)&qat_req->req.cd_ctrl;
	unsigned int keylen = cd_ctrl->cipher_key_sz << 3;
	struct scatterlist sg_src, sg_dst;
	DECLARE_CRYPTO_WAIT(wait);
	struct skcipher_request *tweak_req = NULL;
	struct crypto_skcipher *tweak_tfm;
	int ret;

	/* get second half of the XTS key */
	if (qat_req->encryption)
		cd = ctx->enc_cd;
	else
		cd = ctx->dec_cd;
	xts_key2 = cd->ucs_aes256_f8.key + keylen;

	tweak_tfm = crypto_alloc_skcipher("ecb(aes)", 0, 0);
	if (IS_ERR(tweak_tfm))
		return PTR_ERR(tweak_tfm);

	ret = crypto_skcipher_setkey(tweak_tfm, xts_key2, keylen);
	if (unlikely(ret))
		goto free;

	tweak_req = skcipher_request_alloc(tweak_tfm, GFP_KERNEL);
	if (unlikely(!tweak_req)) {
		ret = -ENOMEM;
		goto free;
	}

	skcipher_request_set_callback(tweak_req,
				      CRYPTO_TFM_REQ_MAY_BACKLOG |
					      CRYPTO_TFM_REQ_MAY_SLEEP,
				      crypto_req_done, &wait);

	sg_init_one(&sg_src, iv, AES_BLOCK_SIZE);
	sg_init_one(&sg_dst, (u8 *)cipher_param->u.cipher_IV_array,
		    AES_BLOCK_SIZE);
	skcipher_request_set_crypt(tweak_req, &sg_src, &sg_dst, AES_BLOCK_SIZE,
				   NULL);

	ret = crypto_wait_req(crypto_skcipher_encrypt(tweak_req), &wait);

free:
	crypto_free_skcipher(tweak_tfm);
	skcipher_request_free(tweak_req);

	return ret;
}
#endif

static int qat_alg_set_req_iv(struct qat_crypto_request *qat_req)
{
	struct icp_qat_fw_la_cipher_req_params *cipher_param;
	int ret;

#if KERNEL_VERSION(5, 5, 0) > LINUX_VERSION_CODE
	struct qat_alg_ablkcipher_ctx *ctx = qat_req->ablkcipher_ctx;
	struct ablkcipher_request *req = qat_req->ablkcipher_req;
	u8 *iv = req->info;
#else
	struct qat_alg_skcipher_ctx *ctx = qat_req->skcipher_ctx;
	struct skcipher_request *req = qat_req->skcipher_req;
	u8 *iv = req->iv;
#endif

	if (HW_CAP_AES_V2(ctx->inst->accel_dev) &&
	    ctx->mode == ICP_QAT_HW_CIPHER_XTS_MODE) {
		ret = qat_alg_xts_calculate_tweak(qat_req, ctx, iv);
		if (unlikely(ret))
			return ret;
	} else {
		cipher_param = (void *)&qat_req->req.serv_specif_rqpars;
		memcpy(cipher_param->u.cipher_IV_array, iv, AES_BLOCK_SIZE);
	}

	return 0;
}

#if KERNEL_VERSION(5, 5, 0) > LINUX_VERSION_CODE
static int qat_alg_ablkcipher_encrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *atfm = crypto_ablkcipher_reqtfm(req);
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(atfm);
	struct qat_alg_ablkcipher_ctx *ctx = crypto_tfm_ctx(tfm);
	struct qat_crypto_request *qat_req = ablkcipher_request_ctx(req);
	struct icp_qat_fw_la_cipher_req_params *cipher_param;
	struct icp_qat_fw_la_bulk_req *msg;
	gfp_t flags = qat_algs_alloc_flags(&req->base);
	int ret;

	if (req->nbytes == 0)
		return 0;

	ret = qat_alg_sgl_to_bufl(ctx->inst,
				  req->src, req->dst, qat_req, flags);

	msg = &qat_req->req;
	*msg = ctx->enc_fw_req;
	qat_req->ablkcipher_ctx = ctx;
	qat_req->ablkcipher_req = req;
	qat_req->cb = qat_ablkcipher_alg_callback;
	qat_req->req.comn_mid.opaque_data = (uint64_t)(__force long)qat_req;
	qat_req->req.comn_mid.src_data_addr = qat_req->buf.blp;
	qat_req->req.comn_mid.dest_data_addr = qat_req->buf.bloutp;
	qat_req->encryption = true;
	cipher_param = (void *)&qat_req->req.serv_specif_rqpars;
	cipher_param->cipher_length = req->nbytes;
	cipher_param->cipher_offset = 0;

	ret = qat_alg_set_req_iv(qat_req);
	if (unlikely(ret))
		return ret;

	do {
		ret = adf_send_message(ctx->inst->sym_tx, (uint32_t *)msg);
		if (ret)
			cond_resched();
	} while (ret == -EAGAIN);

	return -EINPROGRESS;
}

static int qat_alg_ablkcipher_blk_encrypt(struct ablkcipher_request *req)
{
	if (req->nbytes % AES_BLOCK_SIZE != 0)
		return -EINVAL;

	return qat_alg_ablkcipher_encrypt(req);
}
#else
static int qat_alg_skcipher_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *stfm = crypto_skcipher_reqtfm(req);
	struct crypto_tfm *tfm = crypto_skcipher_tfm(stfm);
	struct qat_alg_skcipher_ctx *ctx = crypto_tfm_ctx(tfm);
	struct qat_crypto_request *qat_req = skcipher_request_ctx(req);
	struct icp_qat_fw_la_cipher_req_params *cipher_param;
	gfp_t flags = qat_algs_alloc_flags(&req->base);
	struct icp_qat_fw_la_bulk_req *msg;
	int ret;

	if (req->cryptlen == 0)
		return 0;

	ret = qat_alg_sgl_to_bufl(ctx->inst, req->src, req->dst, qat_req, flags);
	if (unlikely(ret))
		return ret;

	msg = &qat_req->req;
	*msg = ctx->enc_fw_req;
	qat_req->skcipher_ctx = ctx;
	qat_req->skcipher_req = req;
	qat_req->cb = qat_skcipher_alg_callback;
	qat_req->req.comn_mid.opaque_data = (uint64_t)(__force long)qat_req;
	qat_req->req.comn_mid.src_data_addr = qat_req->buf.blp;
	qat_req->req.comn_mid.dest_data_addr = qat_req->buf.bloutp;
	qat_req->encryption = true;
	cipher_param = (void *)&qat_req->req.serv_specif_rqpars;
	cipher_param->cipher_length = req->cryptlen;
	cipher_param->cipher_offset = 0;

	ret = qat_alg_set_req_iv(qat_req);
	if (unlikely(ret))
		return ret;

	do {
		ret = adf_send_message(ctx->inst->sym_tx, (uint32_t *)msg);
		if (ret)
			cond_resched();
	} while (ret == -EAGAIN);

	return -EINPROGRESS;
}

static int qat_alg_skcipher_blk_encrypt(struct skcipher_request *req)
{
	if (req->cryptlen % AES_BLOCK_SIZE != 0)
		return -EINVAL;

	return qat_alg_skcipher_encrypt(req);
}

static int qat_alg_skcipher_xts_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *stfm = crypto_skcipher_reqtfm(req);
	struct qat_alg_skcipher_ctx *ctx = crypto_skcipher_ctx(stfm);
	struct skcipher_request *nreq = skcipher_request_ctx(req);

	if (req->cryptlen < XTS_BLOCK_SIZE)
		return -EINVAL;

	if (ctx->fallback) {
		memcpy(nreq, req, sizeof(*req));
		skcipher_request_set_tfm(nreq, ctx->ftfm);
		return crypto_skcipher_encrypt(nreq);
	}

	return qat_alg_skcipher_encrypt(req);
}
#endif

#if KERNEL_VERSION(5, 5, 0) > LINUX_VERSION_CODE
static int qat_alg_ablkcipher_decrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *atfm = crypto_ablkcipher_reqtfm(req);
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(atfm);
	struct qat_alg_ablkcipher_ctx *ctx = crypto_tfm_ctx(tfm);
	struct qat_crypto_request *qat_req = ablkcipher_request_ctx(req);
	struct icp_qat_fw_la_cipher_req_params *cipher_param;
	struct icp_qat_fw_la_bulk_req *msg;
	gfp_t flags = qat_algs_alloc_flags(&req->base);
	int ret;

	if (req->nbytes == 0)
		return 0;

	ret = qat_alg_sgl_to_bufl(ctx->inst,
				  req->src, req->dst, qat_req, flags);
	if (unlikely(ret)) {
		return ret;
	}

	msg = &qat_req->req;
	*msg = ctx->dec_fw_req;
	qat_req->ablkcipher_ctx = ctx;
	qat_req->ablkcipher_req = req;
	qat_req->cb = qat_ablkcipher_alg_callback;
	qat_req->req.comn_mid.opaque_data = (uint64_t)(__force long)qat_req;
	qat_req->req.comn_mid.src_data_addr = qat_req->buf.blp;
	qat_req->req.comn_mid.dest_data_addr = qat_req->buf.bloutp;
	qat_req->encryption = false;
	cipher_param = (void *)&qat_req->req.serv_specif_rqpars;
	cipher_param->cipher_length = req->nbytes;
	cipher_param->cipher_offset = 0;

	ret = qat_alg_set_req_iv(qat_req);
	if (unlikely(ret))
		return ret;

	qat_alg_update_iv(qat_req);

	do {
		ret = adf_send_message(ctx->inst->sym_tx, (uint32_t *)msg);
		if (ret)
			cond_resched();
	} while (ret == -EAGAIN);

	return -EINPROGRESS;
}

static int qat_alg_ablkcipher_blk_decrypt(struct ablkcipher_request *req)
{
	if (req->nbytes % AES_BLOCK_SIZE != 0)
		return -EINVAL;

	return qat_alg_ablkcipher_decrypt(req);
}
#else
static int qat_alg_skcipher_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *stfm = crypto_skcipher_reqtfm(req);
	struct crypto_tfm *tfm = crypto_skcipher_tfm(stfm);
	struct qat_alg_skcipher_ctx *ctx = crypto_tfm_ctx(tfm);
	struct qat_crypto_request *qat_req = skcipher_request_ctx(req);
	struct icp_qat_fw_la_cipher_req_params *cipher_param;
	gfp_t flags = qat_algs_alloc_flags(&req->base);
	struct icp_qat_fw_la_bulk_req *msg;

	int ret;

	if (req->cryptlen == 0)
		return 0;

	ret = qat_alg_sgl_to_bufl(ctx->inst, req->src, req->dst, qat_req, flags);
	if (unlikely(ret))
		return ret;

	msg = &qat_req->req;
	*msg = ctx->dec_fw_req;
	qat_req->skcipher_ctx = ctx;
	qat_req->skcipher_req = req;
	qat_req->cb = qat_skcipher_alg_callback;
	qat_req->req.comn_mid.opaque_data = (uint64_t)(__force long)qat_req;
	qat_req->req.comn_mid.src_data_addr = qat_req->buf.blp;
	qat_req->req.comn_mid.dest_data_addr = qat_req->buf.bloutp;
	qat_req->encryption = false;
	cipher_param = (void *)&qat_req->req.serv_specif_rqpars;
	cipher_param->cipher_length = req->cryptlen;
	cipher_param->cipher_offset = 0;

	ret = qat_alg_set_req_iv(qat_req);
	if (unlikely(ret))
		return ret;

	qat_alg_update_iv(qat_req);

	do {
		ret = adf_send_message(ctx->inst->sym_tx, (uint32_t *)msg);
		if (ret)
			cond_resched();
	} while (ret == -EAGAIN);

	return -EINPROGRESS;
}

static int qat_alg_skcipher_blk_decrypt(struct skcipher_request *req)
{
	if (req->cryptlen % AES_BLOCK_SIZE != 0)
		return -EINVAL;

	return qat_alg_skcipher_decrypt(req);
}

static int qat_alg_skcipher_xts_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *stfm = crypto_skcipher_reqtfm(req);
	struct qat_alg_skcipher_ctx *ctx = crypto_skcipher_ctx(stfm);
	struct skcipher_request *nreq = skcipher_request_ctx(req);

	if (req->cryptlen < XTS_BLOCK_SIZE)
		return -EINVAL;

	if (ctx->fallback) {
		memcpy(nreq, req, sizeof(*req));
		skcipher_request_set_tfm(nreq, ctx->ftfm);
		return crypto_skcipher_decrypt(nreq);
	}

	return qat_alg_skcipher_decrypt(req);
}
#endif

static int qat_alg_aead_init(struct crypto_aead *tfm,
			     enum icp_qat_hw_auth_algo hash,
			     const char *hash_name)
{
	struct qat_alg_aead_ctx *ctx = crypto_aead_ctx(tfm);

	ctx->hash_tfm = crypto_alloc_shash(hash_name, 0, 0);
	if (IS_ERR(ctx->hash_tfm))
		return PTR_ERR(ctx->hash_tfm);
	ctx->qat_hash_alg = hash;
	crypto_aead_set_reqsize(tfm, sizeof(struct qat_crypto_request));
	return 0;
}

#ifdef QAT_LEGACY_ALGORITHMS
static int qat_alg_aead_sha1_init(struct crypto_aead *tfm)
{
	return qat_alg_aead_init(tfm, ICP_QAT_HW_AUTH_ALGO_SHA1, "sha1");
}
#endif

static int qat_alg_aead_sha256_init(struct crypto_aead *tfm)
{
	return qat_alg_aead_init(tfm, ICP_QAT_HW_AUTH_ALGO_SHA256, "sha256");
}

static int qat_alg_aead_sha512_init(struct crypto_aead *tfm)
{
	return qat_alg_aead_init(tfm, ICP_QAT_HW_AUTH_ALGO_SHA512, "sha512");
}

static void qat_alg_aead_exit(struct crypto_aead *tfm)
{
	struct qat_alg_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct qat_crypto_instance *inst = ctx->inst;
	struct device *dev;

	crypto_free_shash(ctx->hash_tfm);

	if (!inst)
		return;

	dev = &GET_DEV(inst->accel_dev);
	if (ctx->enc_cd) {
		memzero_explicit(ctx->enc_cd, sizeof(struct qat_alg_cd));
		dma_free_coherent(dev, sizeof(struct qat_alg_cd),
				  ctx->enc_cd, ctx->enc_cd_paddr);
	}
	if (ctx->dec_cd) {
		memzero_explicit(ctx->dec_cd, sizeof(struct qat_alg_cd));
		dma_free_coherent(dev, sizeof(struct qat_alg_cd),
				  ctx->dec_cd, ctx->dec_cd_paddr);
	}
	qat_crypto_put_instance(inst);
}

#if KERNEL_VERSION(5, 5, 0) > LINUX_VERSION_CODE
static int qat_alg_ablkcipher_init(struct crypto_tfm *tfm)
{
	struct qat_alg_ablkcipher_ctx *ctx = crypto_tfm_ctx(tfm);

	tfm->crt_ablkcipher.reqsize = sizeof(struct qat_crypto_request);
	ctx->tfm = tfm;
	return 0;
}
#else
static int qat_alg_skcipher_init(struct crypto_skcipher *tfm)
{
	crypto_skcipher_set_reqsize(tfm, sizeof(struct qat_crypto_request));
	return 0;
}

static int qat_alg_skcipher_init_xts(struct crypto_skcipher *tfm)
{
	struct qat_alg_skcipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	int reqsize;

	ctx->ftfm = crypto_alloc_skcipher("xts(aes)", 0,
					  CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(ctx->ftfm))
		return PTR_ERR(ctx->ftfm);

	reqsize = max(sizeof(struct qat_crypto_request),
		      sizeof(struct skcipher_request) +
		      crypto_skcipher_reqsize(ctx->ftfm));
	crypto_skcipher_set_reqsize(tfm, reqsize);

	return 0;
}
#endif

#if KERNEL_VERSION(5, 5, 0) > LINUX_VERSION_CODE
static void qat_alg_ablkcipher_exit(struct crypto_tfm *tfm)
{
	struct qat_alg_ablkcipher_ctx *ctx = crypto_tfm_ctx(tfm);
	struct qat_crypto_instance *inst = ctx->inst;
	struct device *dev;

	if (!inst)
		return;

	dev = &GET_DEV(inst->accel_dev);
	if (ctx->enc_cd) {
		memzero_explicit(ctx->enc_cd,
				 sizeof(struct icp_qat_hw_cipher_algo_blk));
		dma_free_coherent(dev,
				  sizeof(struct icp_qat_hw_cipher_algo_blk),
				  ctx->enc_cd, ctx->enc_cd_paddr);
		ctx->enc_cd = NULL;
	}
	if (ctx->dec_cd) {
		memzero_explicit(ctx->dec_cd,
				 sizeof(struct icp_qat_hw_cipher_algo_blk));
		dma_free_coherent(dev,
				  sizeof(struct icp_qat_hw_cipher_algo_blk),
				  ctx->dec_cd, ctx->dec_cd_paddr);
		ctx->dec_cd = NULL;
	}
	qat_crypto_put_instance(inst);
}
#else
static void qat_alg_skcipher_exit(struct crypto_skcipher *tfm)
{
	struct qat_alg_skcipher_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct qat_crypto_instance *inst = ctx->inst;
	struct device *dev;

	if (!inst)
		return;

	dev = &GET_DEV(inst->accel_dev);
	if (ctx->enc_cd) {
		memzero_explicit(ctx->enc_cd,
				 sizeof(struct icp_qat_hw_cipher_algo_blk));
		dma_free_coherent(dev,
				  sizeof(struct icp_qat_hw_cipher_algo_blk),
				  ctx->enc_cd, ctx->enc_cd_paddr);
		ctx->enc_cd = NULL;
	}
	if (ctx->dec_cd) {
		memzero_explicit(ctx->dec_cd,
				 sizeof(struct icp_qat_hw_cipher_algo_blk));
		dma_free_coherent(dev,
				  sizeof(struct icp_qat_hw_cipher_algo_blk),
				  ctx->dec_cd, ctx->dec_cd_paddr);
		ctx->dec_cd = NULL;
	}
	qat_crypto_put_instance(inst);
}

static void qat_alg_skcipher_exit_xts(struct crypto_skcipher *tfm)
{
	struct qat_alg_skcipher_ctx *ctx = crypto_skcipher_ctx(tfm);

	if (ctx->ftfm)
		crypto_free_skcipher(ctx->ftfm);

	qat_alg_skcipher_exit(tfm);
}
#endif

#ifdef QAT_LEGACY_ALGORITHMS
static struct aead_alg qat_legacy_aeads[] = { {
	.base = {
		.cra_name = "authenc(hmac(sha1),cbc(aes))",
		.cra_driver_name = "qat_aes_cbc_hmac_sha1",
		.cra_priority = 4001,
		.cra_flags = CRYPTO_ALG_ASYNC,
		.cra_blocksize = AES_BLOCK_SIZE,
		.cra_ctxsize = sizeof(struct qat_alg_aead_ctx),
		.cra_module = THIS_MODULE,
	},
	.init = qat_alg_aead_sha1_init,
	.exit = qat_alg_aead_exit,
	.setkey = qat_alg_aead_setkey,
	.decrypt = qat_alg_aead_dec,
	.encrypt = qat_alg_aead_enc,
	.ivsize = AES_BLOCK_SIZE,
	.maxauthsize = SHA1_DIGEST_SIZE,
} };
#endif

static struct aead_alg qat_aeads[] = { {
	.base = {
		.cra_name = "authenc(hmac(sha256),cbc(aes))",
		.cra_driver_name = "qat_aes_cbc_hmac_sha256",
		.cra_priority = 4001,
		.cra_flags = CRYPTO_ALG_ASYNC,
		.cra_blocksize = AES_BLOCK_SIZE,
		.cra_ctxsize = sizeof(struct qat_alg_aead_ctx),
		.cra_module = THIS_MODULE,
	},
	.init = qat_alg_aead_sha256_init,
	.exit = qat_alg_aead_exit,
	.setkey = qat_alg_aead_setkey,
	.decrypt = qat_alg_aead_dec,
	.encrypt = qat_alg_aead_enc,
	.ivsize = AES_BLOCK_SIZE,
	.maxauthsize = SHA256_DIGEST_SIZE,
}, {
	.base = {
		.cra_name = "authenc(hmac(sha512),cbc(aes))",
		.cra_driver_name = "qat_aes_cbc_hmac_sha512",
		.cra_priority = 4001,
		.cra_flags = CRYPTO_ALG_ASYNC,
		.cra_blocksize = AES_BLOCK_SIZE,
		.cra_ctxsize = sizeof(struct qat_alg_aead_ctx),
		.cra_module = THIS_MODULE,
	},
	.init = qat_alg_aead_sha512_init,
	.exit = qat_alg_aead_exit,
	.setkey = qat_alg_aead_setkey,
	.decrypt = qat_alg_aead_dec,
	.encrypt = qat_alg_aead_enc,
	.ivsize = AES_BLOCK_SIZE,
	.maxauthsize = SHA512_DIGEST_SIZE,
} };

#if KERNEL_VERSION(5, 5, 0) > LINUX_VERSION_CODE
static struct crypto_alg qat_algs[] = { {
	.cra_name = "cbc(aes)",
	.cra_driver_name = "qat_aes_cbc",
	.cra_priority = 4001,
	.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize = AES_BLOCK_SIZE,
	.cra_ctxsize = sizeof(struct qat_alg_ablkcipher_ctx),
	.cra_alignmask = 0,
	.cra_type = &crypto_ablkcipher_type,
	.cra_module = THIS_MODULE,
	.cra_init = qat_alg_ablkcipher_init,
	.cra_exit = qat_alg_ablkcipher_exit,
	.cra_u = {
		.ablkcipher = {
			.setkey = qat_alg_ablkcipher_cbc_setkey,
			.decrypt = qat_alg_ablkcipher_blk_decrypt,
			.encrypt = qat_alg_ablkcipher_blk_encrypt,
			.min_keysize = AES_MIN_KEY_SIZE,
			.max_keysize = AES_MAX_KEY_SIZE,
			.ivsize = AES_BLOCK_SIZE,
		},
	},
}, {
	.cra_name = "ctr(aes)",
	.cra_driver_name = "qat_aes_ctr",
	.cra_priority = 4001,
	.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize = 1,
	.cra_ctxsize = sizeof(struct qat_alg_ablkcipher_ctx),
	.cra_alignmask = 0,
	.cra_type = &crypto_ablkcipher_type,
	.cra_module = THIS_MODULE,
	.cra_init = qat_alg_ablkcipher_init,
	.cra_exit = qat_alg_ablkcipher_exit,
	.cra_u = {
		.ablkcipher = {
			.setkey = qat_alg_ablkcipher_ctr_setkey,
			.decrypt = qat_alg_ablkcipher_decrypt,
			.encrypt = qat_alg_ablkcipher_encrypt,
			.min_keysize = AES_MIN_KEY_SIZE,
			.max_keysize = AES_MAX_KEY_SIZE,
			.ivsize = AES_BLOCK_SIZE,
		},
	},
}, {
	.cra_name = "xts(aes)",
	.cra_driver_name = "qat_aes_xts",
	.cra_priority = 4001,
	.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize = AES_BLOCK_SIZE,
	.cra_ctxsize = sizeof(struct qat_alg_ablkcipher_ctx),
	.cra_alignmask = 0,
	.cra_type = &crypto_ablkcipher_type,
	.cra_module = THIS_MODULE,
	.cra_init = qat_alg_ablkcipher_init,
	.cra_exit = qat_alg_ablkcipher_exit,
	.cra_u = {
		.ablkcipher = {
			.setkey = qat_alg_ablkcipher_xts_setkey,
			.decrypt = qat_alg_ablkcipher_blk_decrypt,
			.encrypt = qat_alg_ablkcipher_blk_encrypt,
			.min_keysize = 2 * AES_MIN_KEY_SIZE,
			.max_keysize = 2 * AES_MAX_KEY_SIZE,
			.ivsize = AES_BLOCK_SIZE,
		},
	},
} };
#else
static struct skcipher_alg qat_skciphers[] = { {
	.base.cra_name = "cbc(aes)",
	.base.cra_driver_name = "qat_aes_cbc",
	.base.cra_priority = 4001,
	.base.cra_flags = CRYPTO_ALG_ASYNC,
	.base.cra_blocksize = AES_BLOCK_SIZE,
	.base.cra_ctxsize = sizeof(struct qat_alg_skcipher_ctx),
	.base.cra_alignmask = 0,
	.base.cra_module = THIS_MODULE,
	.init = qat_alg_skcipher_init,
	.exit = qat_alg_skcipher_exit,
	.setkey = qat_alg_skcipher_cbc_setkey,
	.decrypt = qat_alg_skcipher_blk_decrypt,
	.encrypt = qat_alg_skcipher_blk_encrypt,
	.min_keysize = AES_MIN_KEY_SIZE,
	.max_keysize = AES_MAX_KEY_SIZE,
	.ivsize = AES_BLOCK_SIZE,
}, {
	.base.cra_name = "ctr(aes)",
	.base.cra_driver_name = "qat_aes_ctr",
	.base.cra_priority = 4001,
	.base.cra_flags = CRYPTO_ALG_ASYNC,
	.base.cra_blocksize = 1,
	.base.cra_ctxsize = sizeof(struct qat_alg_skcipher_ctx),
	.base.cra_alignmask = 0,
	.base.cra_module = THIS_MODULE,
	.init = qat_alg_skcipher_init,
	.exit = qat_alg_skcipher_exit,
	.setkey = qat_alg_skcipher_ctr_setkey,
	.decrypt = qat_alg_skcipher_decrypt,
	.encrypt = qat_alg_skcipher_encrypt,
	.min_keysize = AES_MIN_KEY_SIZE,
	.max_keysize = AES_MAX_KEY_SIZE,
	.ivsize = AES_BLOCK_SIZE,
}, {
	.base.cra_name = "xts(aes)",
	.base.cra_driver_name = "qat_aes_xts",
	.base.cra_priority = 4001,
	.base.cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_NEED_FALLBACK,
	.base.cra_blocksize = AES_BLOCK_SIZE,
	.base.cra_ctxsize = sizeof(struct qat_alg_skcipher_ctx),
	.base.cra_alignmask = 0,
	.base.cra_module = THIS_MODULE,
	.init = qat_alg_skcipher_init_xts,
	.exit = qat_alg_skcipher_exit_xts,
	.setkey = qat_alg_skcipher_xts_setkey,
	.decrypt = qat_alg_skcipher_xts_decrypt,
	.encrypt = qat_alg_skcipher_xts_encrypt,
	.min_keysize = 2 * AES_MIN_KEY_SIZE,
	.max_keysize = 2 * AES_MAX_KEY_SIZE,
	.ivsize = AES_BLOCK_SIZE,
} };
#endif
int qat_algs_register(void)
{
	int ret = 0, i;

	mutex_lock(&algs_lock);
	if (++active_devs != 1)
		goto unlock;
#if KERNEL_VERSION(5, 5, 0) > LINUX_VERSION_CODE
	for (i = 0; i < ARRAY_SIZE(qat_algs); i++)
		qat_algs[i].cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER |
					CRYPTO_ALG_ASYNC;

	ret = crypto_register_algs(qat_algs, ARRAY_SIZE(qat_algs));
#else
	ret = crypto_register_skciphers(qat_skciphers,
					ARRAY_SIZE(qat_skciphers));
#endif
	if (ret)
		goto unlock;

	for (i = 0; i < ARRAY_SIZE(qat_aeads); i++)
		qat_aeads[i].base.cra_flags = CRYPTO_ALG_ASYNC;

	ret = crypto_register_aeads(qat_aeads, ARRAY_SIZE(qat_aeads));
	if (ret)
		goto unreg_algs;

#ifdef QAT_LEGACY_ALGORITHMS
	ret = crypto_register_aeads(qat_legacy_aeads,
				    ARRAY_SIZE(qat_legacy_aeads));
	if (ret)
		crypto_unregister_aeads(qat_aeads, ARRAY_SIZE(qat_aeads));
#endif

unlock:
	mutex_unlock(&algs_lock);
	return ret;

unreg_algs:
#if KERNEL_VERSION(5, 5, 0) > LINUX_VERSION_CODE
	crypto_unregister_algs(qat_algs, ARRAY_SIZE(qat_algs));
#else
	crypto_unregister_skciphers(qat_skciphers, ARRAY_SIZE(qat_skciphers));
#endif
	goto unlock;
}

void qat_algs_unregister(void)
{
	mutex_lock(&algs_lock);
	if (--active_devs != 0)
		goto unlock;

#ifdef QAT_LEGACY_ALGORITHMS
	crypto_unregister_aeads(qat_legacy_aeads,
				ARRAY_SIZE(qat_legacy_aeads));
#endif

	crypto_unregister_aeads(qat_aeads, ARRAY_SIZE(qat_aeads));

#if KERNEL_VERSION(5, 5, 0) > LINUX_VERSION_CODE
	crypto_unregister_algs(qat_algs, ARRAY_SIZE(qat_algs));
#else
	crypto_unregister_skciphers(qat_skciphers, ARRAY_SIZE(qat_skciphers));
#endif

unlock:
	mutex_unlock(&algs_lock);
}
#endif
