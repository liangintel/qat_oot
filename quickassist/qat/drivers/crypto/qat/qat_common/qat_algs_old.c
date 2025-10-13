// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2021 Intel Corporation */
#ifdef QAT_AEAD_OLD_SUPPORTED
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/crypto.h>
#include <crypto/internal/aead.h>
#include <crypto/aes.h>
#include <crypto/hash.h>
#if KERNEL_VERSION(5, 11, 0) <= LINUX_VERSION_CODE
#include <crypto/sha1.h>
#include <crypto/sha2.h>
#else
#include <crypto/sha.h>
#endif
#include <crypto/algapi.h>
#include <crypto/authenc.h>
#include <crypto/rng.h>
#include <linux/dma-mapping.h>
#include "adf_accel_devices.h"
#include "adf_transport.h"
#include "adf_common_drv.h"
#include "qat_crypto.h"
#include "icp_qat_hw.h"
#include "icp_qat_fw.h"
#include "icp_qat_fw_la.h"

#define QAT_AES_HW_CONFIG_CBC_ENC(alg, aead_hash_cmp_len) \
	ICP_QAT_HW_CIPHER_CONFIG_BUILD(ICP_QAT_HW_CIPHER_CBC_MODE, alg, \
				       ICP_QAT_HW_CIPHER_NO_CONVERT, \
				       ICP_QAT_HW_CIPHER_ENCRYPT, \
				       aead_hash_cmp_len)

#define QAT_AES_HW_CONFIG_CBC_DEC(alg, aead_hash_cmp_len) \
	ICP_QAT_HW_CIPHER_CONFIG_BUILD(ICP_QAT_HW_CIPHER_CBC_MODE, alg, \
				       ICP_QAT_HW_CIPHER_KEY_CONVERT, \
				       ICP_QAT_HW_CIPHER_DECRYPT, \
				       aead_hash_cmp_len)

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
	struct crypto_tfm *tfm;
	u8 salt[AES_BLOCK_SIZE];
	spinlock_t lock;	/* protects qat_alg_aead_ctx struct */
	char ipad[SHA512_BLOCK_SIZE];
	char opad[SHA512_BLOCK_SIZE];
};

struct qat_alg_ablkcipher_ctx {
	struct icp_qat_hw_cipher_algo_blk *enc_cd;
	struct icp_qat_hw_cipher_algo_blk *dec_cd;
	dma_addr_t enc_cd_paddr;
	dma_addr_t dec_cd_paddr;
	struct icp_qat_fw_la_bulk_req enc_fw_req;
	struct icp_qat_fw_la_bulk_req dec_fw_req;
	struct qat_crypto_instance *inst;
	struct crypto_tfm *tfm;
	spinlock_t lock;	/* protects qat_alg_ablkcipher_ctx struct */
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

static void qat_alg_init_hdr_iv_updt(struct icp_qat_fw_comn_req_hdr *header)
{
	ICP_QAT_FW_LA_CIPH_IV_FLD_FLAG_SET(header->serv_specif_flags,
					   ICP_QAT_FW_CIPH_IV_64BIT_PTR);
	ICP_QAT_FW_LA_UPDATE_STATE_SET(header->serv_specif_flags,
				       ICP_QAT_FW_LA_UPDATE_STATE);
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
	if (aead)
		qat_alg_init_hdr_no_iv_updt(header);
	else
		qat_alg_init_hdr_iv_updt(header);

	ICP_QAT_FW_LA_PROTO_SET(header->serv_specif_flags,
				ICP_QAT_FW_LA_NO_PROTO);
}

static int qat_alg_aead_init_enc_session(struct qat_alg_aead_ctx *ctx,
					 int alg,
					 struct crypto_authenc_keys *keys)
{
	struct crypto_aead *aead_tfm = __crypto_aead_cast(ctx->tfm);
	unsigned int digestsize = crypto_aead_crt(aead_tfm)->authsize;
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
	cipher->aes256_f8.cipher_config.val = QAT_AES_HW_CONFIG_CBC_ENC(alg,
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

static int qat_alg_aead_init_dec_session(struct qat_alg_aead_ctx *ctx,
					 int alg,
					 struct crypto_authenc_keys *keys)
{
	struct crypto_aead *aead_tfm = __crypto_aead_cast(ctx->tfm);
	unsigned int digestsize = crypto_aead_crt(aead_tfm)->authsize;
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
	cipher->aes256_f8.cipher_config.val = QAT_AES_HW_CONFIG_CBC_DEC(alg,
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

static void qat_alg_ablkcipher_init_com(struct qat_alg_ablkcipher_ctx *ctx,
					struct icp_qat_fw_la_bulk_req *req,
					struct icp_qat_hw_cipher_algo_blk *cd,
					const u8 *key, unsigned int keylen)
{
	struct icp_qat_fw_comn_req_hdr_cd_pars *cd_pars = &req->cd_pars;
	struct icp_qat_fw_comn_req_hdr *header = &req->comn_hdr;
	struct icp_qat_fw_cipher_cd_ctrl_hdr *cd_ctrl = (void *)&req->cd_ctrl;

	memcpy(cd->aes256_f8.key, key, keylen);
	qat_alg_init_common_hdr(header, 0);

	header->service_cmd_id = ICP_QAT_FW_LA_CMD_CIPHER;
	cd_pars->u.s.content_desc_params_sz =
				sizeof(struct icp_qat_hw_cipher_algo_blk) >> 3;
	/* Cipher CD config setup */
	cd_ctrl->cipher_key_sz = keylen >> 3;
	cd_ctrl->cipher_state_sz = AES_BLOCK_SIZE >> 3;
	cd_ctrl->cipher_cfg_offset = 0;
	ICP_QAT_FW_COMN_CURR_ID_SET(cd_ctrl, ICP_QAT_FW_SLICE_CIPHER);
	ICP_QAT_FW_COMN_NEXT_ID_SET(cd_ctrl, ICP_QAT_FW_SLICE_DRAM_WR);
}

static void qat_alg_ablkcipher_init_enc(struct qat_alg_ablkcipher_ctx *ctx,
					int alg, const uint8_t *key,
					unsigned int keylen)
{
	struct icp_qat_hw_cipher_algo_blk *enc_cd = ctx->enc_cd;
	struct icp_qat_fw_la_bulk_req *req = &ctx->enc_fw_req;
	struct icp_qat_fw_comn_req_hdr_cd_pars *cd_pars = &req->cd_pars;

	qat_alg_ablkcipher_init_com(ctx, req, enc_cd, key, keylen);
	cd_pars->u.s.content_desc_addr = ctx->enc_cd_paddr;
	enc_cd->aes256_f8.cipher_config.val = QAT_AES_HW_CONFIG_CBC_ENC(alg,
									0);
}

static void qat_alg_ablkcipher_init_dec(struct qat_alg_ablkcipher_ctx *ctx,
					int alg, const uint8_t *key,
					unsigned int keylen)
{
	struct icp_qat_hw_cipher_algo_blk *dec_cd = ctx->dec_cd;
	struct icp_qat_fw_la_bulk_req *req = &ctx->dec_fw_req;
	struct icp_qat_fw_comn_req_hdr_cd_pars *cd_pars = &req->cd_pars;

	qat_alg_ablkcipher_init_com(ctx, req, dec_cd, key, keylen);
	cd_pars->u.s.content_desc_addr = ctx->dec_cd_paddr;
	dec_cd->aes256_f8.cipher_config.val = QAT_AES_HW_CONFIG_CBC_DEC(alg,
									0);
}

static int qat_alg_validate_key(int key_len, int *alg)
{
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
	return 0;
}

static int qat_alg_aead_init_sessions(struct qat_alg_aead_ctx *ctx,
				      const u8 *key, unsigned int keylen)
{
	struct crypto_authenc_keys keys;
	int alg;
	int res;

	if (crypto_get_default_rng())
		return -EFAULT;
	res = crypto_rng_get_bytes(crypto_default_rng, ctx->salt,
				   AES_BLOCK_SIZE);
	crypto_put_default_rng();
	if (res)
		return -EFAULT;

	if (crypto_authenc_extractkeys(&keys, key, keylen))
		goto bad_key;

	if (qat_alg_validate_key(keys.enckeylen, &alg))
		goto bad_key;

	if (qat_alg_aead_init_enc_session(ctx, alg, &keys))
		goto error;

	if (qat_alg_aead_init_dec_session(ctx, alg, &keys))
		goto error;

	return 0;
bad_key:
	crypto_tfm_set_flags(ctx->tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
	return -EINVAL;
error:
	return -EFAULT;
}

static int qat_alg_ablkcipher_init_sessions(struct qat_alg_ablkcipher_ctx *ctx,
					    const u8 *key,
					    unsigned int keylen)
{
	int alg;

	if (qat_alg_validate_key(keylen, &alg))
		goto bad_key;

	qat_alg_ablkcipher_init_enc(ctx, alg, key, keylen);
	qat_alg_ablkcipher_init_dec(ctx, alg, key, keylen);
	return 0;
bad_key:
	crypto_tfm_set_flags(ctx->tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
	return -EINVAL;
}

static int qat_alg_aead_rekey(struct crypto_aead *tfm, const uint8_t *key,
			      unsigned int keylen)
{
	struct qat_alg_aead_ctx *ctx = crypto_aead_ctx(tfm);

	memzero_explicit(ctx->enc_cd, sizeof(*ctx->enc_cd));
	memzero_explicit(ctx->dec_cd, sizeof(*ctx->dec_cd));
	memzero_explicit(&ctx->enc_fw_req, sizeof(ctx->enc_fw_req));
	memzero_explicit(&ctx->dec_fw_req, sizeof(ctx->dec_fw_req));

	return qat_alg_aead_init_sessions(ctx, key, keylen);
}

static int qat_alg_aead_newkey(struct crypto_aead *tfm, const uint8_t *key,
			       unsigned int keylen)
{
	struct qat_alg_aead_ctx *ctx = crypto_aead_ctx(tfm);
	struct qat_crypto_instance *inst = NULL;
	struct device *dev;
	int node = get_current_node();
	int ret;

	spin_lock(&ctx->lock);
	inst = qat_crypto_get_instance_node(node, SYM);
	if (!inst) {
		spin_unlock(&ctx->lock);
		return -EINVAL;
	}
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
	spin_unlock(&ctx->lock);
	ret = qat_alg_aead_init_sessions(ctx, key, keylen);
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
	spin_unlock(&ctx->lock);
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
			       struct scatterlist *assoc, int assoclen,
			       struct scatterlist *sgl,
			       struct scatterlist *sglout, u8 *iv,
			       u8 ivlen,
			       struct qat_crypto_request *qat_req)
{
	struct device *dev = &GET_DEV(inst->accel_dev);
	int i = 0, bufs = 0, sg_nctr = 0;
	int n = sg_nents(sgl), assoc_n = sg_nents(assoc);
	struct qat_alg_buf_list *bufl = NULL;
	struct qat_alg_buf_list *buflout = NULL;
	dma_addr_t blp = 0;
	dma_addr_t bloutp = 0;
	struct scatterlist *sg = NULL;
	size_t sz_out = 0, sz = 0;
	int node = dev_to_node(&GET_DEV(inst->accel_dev));

	if (unlikely(!n))
		return -EINVAL;

	if ((n + assoc_n) > QAT_MAX_PRE_ALLOCATED_BUFFS) {
		/* Assumption: For dm-crypt, max SGLs can be 4.
		 * If SGLs are more than 4, memory for bufflists is done
		 * at run time.
		 */
		sz = sizeof(struct qat_alg_buf_list) +
			((n + assoc_n) * sizeof(struct qat_alg_buf));

		bufl = kzalloc_node(sz, GFP_ATOMIC, node);
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

	for_each_sg(assoc, sg, assoc_n, i) {
		if (!sg->length)
			continue;

		if (!(assoclen > 0))
			break;

		bufl->bufers[bufs].addr =
			dma_map_single(dev, sg_virt(sg),
				       min_t(int, assoclen, sg->length),
				       DMA_BIDIRECTIONAL);
		bufl->bufers[bufs].len = min_t(int, assoclen, sg->length);
		if (unlikely(dma_mapping_error(dev, bufl->bufers[bufs].addr)))
			goto err;
		bufs++;
		assoclen -= sg->length;
	}

	if (ivlen) {
		bufl->bufers[bufs].addr = dma_map_single(dev, iv, ivlen,
							 DMA_BIDIRECTIONAL);
		bufl->bufers[bufs].len = ivlen;
		if (unlikely(dma_mapping_error(dev, bufl->bufers[bufs].addr)))
			goto err;
		bufs++;
	}

	for_each_sg(sgl, sg, n, i) {
		int y = sg_nctr + bufs;

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
	bufl->num_bufs = sg_nctr + bufs;
	qat_req->buf.bl = bufl;
	qat_req->buf.blp = blp;
	qat_req->buf.sz = sz;
	/* Handle out of place operation */
	if (sgl != sglout) {
		struct qat_alg_buf *bufers;

		n = sg_nents(sglout);
		if ((n + assoc_n) > QAT_MAX_PRE_ALLOCATED_BUFFS) {
			/* Assumption: For dm-crypt, max SGLs can be 4.
			 * If SGLs are more than 4, memory for bufflists is done
			 * at run time.
			 */
			sz_out = sizeof(struct qat_alg_buf_list) +
				((n + assoc_n) * sizeof(struct qat_alg_buf));

			buflout = kzalloc_node(sz, GFP_ATOMIC, node);
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
		/*
		 * For out of place operation dma map only data and
		 * reuse assoc mapping and iv
		 */
		for (i = 0; i < bufs; i++) {
			bufers[i].len = bufl->bufers[i].len;
			bufers[i].addr = bufl->bufers[i].addr;
		}
		for_each_sg(sglout, sg, n, i) {
			int y = sg_nctr + bufs;

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
		buflout->num_bufs = sg_nctr + bufs;
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
	for (i = 0; i < n + bufs; i++)
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
		for (i = bufs; i < n + bufs; i++)
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

static void qat_ablkcipher_alg_callback(struct icp_qat_fw_la_resp *qat_resp,
					struct qat_crypto_request *qat_req)
{
	struct qat_alg_ablkcipher_ctx *ctx = qat_req->ablkcipher_ctx;
	struct qat_crypto_instance *inst = ctx->inst;
	struct ablkcipher_request *areq = qat_req->ablkcipher_req;
	u8 stat_filed = qat_resp->comn_resp.comn_status;
	struct device *dev = &GET_DEV(ctx->inst->accel_dev);
	int res = 0, qat_res = ICP_QAT_FW_COMN_RESP_CRYPTO_STAT_GET(stat_filed);

	qat_alg_free_bufl(inst, qat_req);
	if (unlikely(qat_res != ICP_QAT_FW_COMN_STATUS_FLAG_OK))
		res = -EINVAL;

	memcpy(areq->info, qat_req->iv, AES_BLOCK_SIZE);
	dma_free_coherent(dev, AES_BLOCK_SIZE, qat_req->iv,
			  qat_req->iv_paddr);

	areq->base.complete(&areq->base, res);
}

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
	int digst_size = crypto_aead_crt(aead_tfm)->authsize;
	u32 cipher_length = areq->cryptlen - digst_size;
	int ret;

	if (cipher_length % AES_BLOCK_SIZE != 0)
		return -EINVAL;

	ret = qat_alg_sgl_to_bufl(ctx->inst, areq->assoc, areq->assoclen,
				  areq->src, areq->dst, areq->iv,
				  AES_BLOCK_SIZE, qat_req);
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
	cipher_param->cipher_offset = areq->assoclen + AES_BLOCK_SIZE;
	memcpy(cipher_param->u.cipher_IV_array, areq->iv, AES_BLOCK_SIZE);
	auth_param = (void *)((uint8_t *)cipher_param +
			ICP_QAT_FW_HASH_REQUEST_PARAMETERS_OFFSET);
	auth_param->auth_off = 0;
	auth_param->auth_len = areq->assoclen +
				cipher_param->cipher_length + AES_BLOCK_SIZE;
	do {
		ret = adf_send_message(ctx->inst->sym_tx, (uint32_t *)msg);
		if (ret)
			cond_resched();
	} while (ret == -EAGAIN);

	return -EINPROGRESS;
}

static int qat_alg_aead_enc_internal(struct aead_request *areq, uint8_t *iv,
				     int enc_iv)
{
	struct crypto_aead *aead_tfm = crypto_aead_reqtfm(areq);
	struct crypto_tfm *tfm = crypto_aead_tfm(aead_tfm);
	struct qat_alg_aead_ctx *ctx = crypto_tfm_ctx(tfm);
	struct qat_crypto_request *qat_req = aead_request_ctx(areq);
	struct icp_qat_fw_la_cipher_req_params *cipher_param;
	struct icp_qat_fw_la_auth_req_params *auth_param;
	struct icp_qat_fw_la_bulk_req *msg;
	int ret;

	if (areq->cryptlen % AES_BLOCK_SIZE != 0)
		return -EINVAL;

	ret = qat_alg_sgl_to_bufl(ctx->inst, areq->assoc, areq->assoclen,
				  areq->src, areq->dst, iv, AES_BLOCK_SIZE,
				  qat_req);
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

	if (enc_iv) {
		cipher_param->cipher_length = areq->cryptlen + AES_BLOCK_SIZE;
		cipher_param->cipher_offset = areq->assoclen;
	} else {
		memcpy(cipher_param->u.cipher_IV_array, iv, AES_BLOCK_SIZE);
		cipher_param->cipher_length = areq->cryptlen;
		cipher_param->cipher_offset = areq->assoclen + AES_BLOCK_SIZE;
	}
	auth_param->auth_off = 0;
	auth_param->auth_len = areq->assoclen + areq->cryptlen + AES_BLOCK_SIZE;

	do {
		ret = adf_send_message(ctx->inst->sym_tx, (uint32_t *)msg);
		if (ret)
			cond_resched();
	} while (ret == -EAGAIN);

	return -EINPROGRESS;
}

static int qat_alg_aead_enc(struct aead_request *areq)
{
	return qat_alg_aead_enc_internal(areq, areq->iv, 0);
}

static int qat_alg_aead_genivenc(struct aead_givcrypt_request *req)
{
	struct crypto_aead *aead_tfm = crypto_aead_reqtfm(&req->areq);
	struct crypto_tfm *tfm = crypto_aead_tfm(aead_tfm);
	struct qat_alg_aead_ctx *ctx = crypto_tfm_ctx(tfm);
	__be64 seq;

	memcpy(req->giv, ctx->salt, AES_BLOCK_SIZE);
	seq = cpu_to_be64(req->seq);
	memcpy(req->giv + AES_BLOCK_SIZE - sizeof(uint64_t),
	       &seq, sizeof(uint64_t));
	return qat_alg_aead_enc_internal(&req->areq, req->giv, 1);
}

static int qat_alg_ablkcipher_rekey(struct qat_alg_ablkcipher_ctx *ctx,
				    const u8 *key,
				    unsigned int keylen)
{
	memzero_explicit(ctx->enc_cd, sizeof(*ctx->enc_cd));
	memzero_explicit(ctx->dec_cd, sizeof(*ctx->dec_cd));
	memzero_explicit(&ctx->enc_fw_req, sizeof(ctx->enc_fw_req));
	memzero_explicit(&ctx->dec_fw_req, sizeof(ctx->dec_fw_req));

	return qat_alg_ablkcipher_init_sessions(ctx, key, keylen);
}

static int qat_alg_ablkcipher_newkey(struct qat_alg_ablkcipher_ctx *ctx,
				     const u8 *key,
				     unsigned int keylen)
{
	struct qat_crypto_instance *inst = NULL;
	struct device *dev;
	int node = get_current_node();
	int ret;

	spin_lock(&ctx->lock);
	inst = qat_crypto_get_instance_node(node, SYM);
	if (!inst) {
		spin_unlock(&ctx->lock);
		return -EINVAL;
	}
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

	spin_unlock(&ctx->lock);
	ret = qat_alg_ablkcipher_init_sessions(ctx, key, keylen);
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
	spin_unlock(&ctx->lock);
	return ret;
}

static int qat_alg_ablkcipher_setkey(struct crypto_ablkcipher *tfm,
				     const u8 *key,
				     unsigned int keylen)
{
	struct qat_alg_ablkcipher_ctx *ctx = crypto_ablkcipher_ctx(tfm);

	if (ctx->enc_cd)
		return qat_alg_ablkcipher_rekey(ctx, key, keylen);
	else
		return qat_alg_ablkcipher_newkey(ctx, key, keylen);
}

static int qat_alg_ablkcipher_encrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *atfm = crypto_ablkcipher_reqtfm(req);
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(atfm);
	struct qat_alg_ablkcipher_ctx *ctx = crypto_tfm_ctx(tfm);
	struct qat_crypto_request *qat_req = ablkcipher_request_ctx(req);
	struct icp_qat_fw_la_cipher_req_params *cipher_param;
	struct icp_qat_fw_la_bulk_req *msg;
	struct device *dev = &GET_DEV(ctx->inst->accel_dev);
	int ret;

	if (req->nbytes == 0)
		return 0;

	qat_req->iv = dma_alloc_coherent(dev, AES_BLOCK_SIZE,
					 &qat_req->iv_paddr, GFP_ATOMIC);
	if (!qat_req->iv)
		return -ENOMEM;

	ret = qat_alg_sgl_to_bufl(ctx->inst, NULL, 0, req->src, req->dst,
				  NULL, 0, qat_req);
	if (unlikely(ret)) {
		dma_free_coherent(dev, AES_BLOCK_SIZE, qat_req->iv,
				  qat_req->iv_paddr);
		return ret;
	}

	msg = &qat_req->req;
	*msg = ctx->enc_fw_req;
	qat_req->ablkcipher_ctx = ctx;
	qat_req->ablkcipher_req = req;
	qat_req->cb = qat_ablkcipher_alg_callback;
	qat_req->req.comn_mid.opaque_data = (uint64_t)(__force long)qat_req;
	qat_req->req.comn_mid.src_data_addr = qat_req->buf.blp;
	qat_req->req.comn_mid.dest_data_addr = qat_req->buf.bloutp;
	cipher_param = (void *)&qat_req->req.serv_specif_rqpars;
	cipher_param->cipher_length = req->nbytes;
	cipher_param->cipher_offset = 0;
	cipher_param->u.s.cipher_IV_ptr = qat_req->iv_paddr;
	memcpy(qat_req->iv, req->info, AES_BLOCK_SIZE);

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

static int qat_alg_ablkcipher_decrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *atfm = crypto_ablkcipher_reqtfm(req);
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(atfm);
	struct qat_alg_ablkcipher_ctx *ctx = crypto_tfm_ctx(tfm);
	struct qat_crypto_request *qat_req = ablkcipher_request_ctx(req);
	struct icp_qat_fw_la_cipher_req_params *cipher_param;
	struct icp_qat_fw_la_bulk_req *msg;
	struct device *dev = &GET_DEV(ctx->inst->accel_dev);
	int ret;

	if (req->nbytes == 0)
		return 0;

	qat_req->iv = dma_alloc_coherent(dev, AES_BLOCK_SIZE,
					 &qat_req->iv_paddr, GFP_ATOMIC);
	if (!qat_req->iv)
		return -ENOMEM;

	ret = qat_alg_sgl_to_bufl(ctx->inst, NULL, 0, req->src, req->dst,
				  NULL, 0, qat_req);
	if (unlikely(ret)) {
		dma_free_coherent(dev, AES_BLOCK_SIZE, qat_req->iv,
				  qat_req->iv_paddr);
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
	cipher_param = (void *)&qat_req->req.serv_specif_rqpars;
	cipher_param->cipher_length = req->nbytes;
	cipher_param->cipher_offset = 0;
	cipher_param->u.s.cipher_IV_ptr = qat_req->iv_paddr;
	memcpy(qat_req->iv, req->info, AES_BLOCK_SIZE);

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

static int qat_alg_aead_init(struct crypto_tfm *tfm,
			     enum icp_qat_hw_auth_algo hash,
			     const char *hash_name)
{
	struct qat_alg_aead_ctx *ctx = crypto_tfm_ctx(tfm);

	ctx->hash_tfm = crypto_alloc_shash(hash_name, 0, 0);
	if (IS_ERR(ctx->hash_tfm))
		return -EFAULT;
	spin_lock_init(&ctx->lock);
	ctx->qat_hash_alg = hash;
	crypto_aead_set_reqsize(__crypto_aead_cast(tfm),
				sizeof(struct aead_request) +
				sizeof(struct qat_crypto_request));
	ctx->tfm = tfm;
	return 0;
}

#ifdef QAT_LEGACY_ALGORITHMS
static int qat_alg_aead_sha1_init(struct crypto_tfm *tfm)
{
	return qat_alg_aead_init(tfm, ICP_QAT_HW_AUTH_ALGO_SHA1, "sha1");
}
#endif

static int qat_alg_aead_sha256_init(struct crypto_tfm *tfm)
{
	return qat_alg_aead_init(tfm, ICP_QAT_HW_AUTH_ALGO_SHA256, "sha256");
}

static int qat_alg_aead_sha512_init(struct crypto_tfm *tfm)
{
	return qat_alg_aead_init(tfm, ICP_QAT_HW_AUTH_ALGO_SHA512, "sha512");
}

static void qat_alg_aead_exit(struct crypto_tfm *tfm)
{
	struct qat_alg_aead_ctx *ctx = crypto_tfm_ctx(tfm);
	struct qat_crypto_instance *inst = ctx->inst;
	struct device *dev;

	if (!IS_ERR(ctx->hash_tfm))
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

static int qat_alg_ablkcipher_init(struct crypto_tfm *tfm)
{
	struct qat_alg_ablkcipher_ctx *ctx = crypto_tfm_ctx(tfm);

	spin_lock_init(&ctx->lock);
	tfm->crt_ablkcipher.reqsize = sizeof(struct ablkcipher_request) +
					sizeof(struct qat_crypto_request);
	ctx->tfm = tfm;
	return 0;
}

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
	}
	if (ctx->dec_cd) {
		memzero_explicit(ctx->dec_cd,
				 sizeof(struct icp_qat_hw_cipher_algo_blk));
		dma_free_coherent(dev,
				  sizeof(struct icp_qat_hw_cipher_algo_blk),
				  ctx->dec_cd, ctx->dec_cd_paddr);
	}
	qat_crypto_put_instance(inst);
}

#ifdef QAT_LEGACY_ALGORITHMS
static struct crypto_alg qat_legacy_algs[] = { {
	.cra_name = "authenc(hmac(sha1),cbc(aes))",
	.cra_driver_name = "qat_aes_cbc_hmac_sha1",
	.cra_priority = 4001,
	.cra_flags = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_ASYNC,
	.cra_blocksize = AES_BLOCK_SIZE,
	.cra_ctxsize = sizeof(struct qat_alg_aead_ctx),
	.cra_alignmask = 0,
	.cra_type = &crypto_aead_type,
	.cra_module = THIS_MODULE,
	.cra_init = qat_alg_aead_sha1_init,
	.cra_exit = qat_alg_aead_exit,
	.cra_u = {
		.aead = {
			.setkey = qat_alg_aead_setkey,
			.decrypt = qat_alg_aead_dec,
			.encrypt = qat_alg_aead_enc,
			.givencrypt = qat_alg_aead_genivenc,
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA1_DIGEST_SIZE,
		},
	},
} };
#endif

static struct crypto_alg qat_algs[] = { {
	.cra_name = "authenc(hmac(sha256),cbc(aes))",
	.cra_driver_name = "qat_aes_cbc_hmac_sha256",
	.cra_priority = 4001,
	.cra_flags = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_ASYNC,
	.cra_blocksize = AES_BLOCK_SIZE,
	.cra_ctxsize = sizeof(struct qat_alg_aead_ctx),
	.cra_alignmask = 0,
	.cra_type = &crypto_aead_type,
	.cra_module = THIS_MODULE,
	.cra_init = qat_alg_aead_sha256_init,
	.cra_exit = qat_alg_aead_exit,
	.cra_u = {
		.aead = {
			.setkey = qat_alg_aead_setkey,
			.decrypt = qat_alg_aead_dec,
			.encrypt = qat_alg_aead_enc,
			.givencrypt = qat_alg_aead_genivenc,
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA256_DIGEST_SIZE,
		},
	},
}, {
	.cra_name = "authenc(hmac(sha512),cbc(aes))",
	.cra_driver_name = "qat_aes_cbc_hmac_sha512",
	.cra_priority = 4001,
	.cra_flags = CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_ASYNC,
	.cra_blocksize = AES_BLOCK_SIZE,
	.cra_ctxsize = sizeof(struct qat_alg_aead_ctx),
	.cra_alignmask = 0,
	.cra_type = &crypto_aead_type,
	.cra_module = THIS_MODULE,
	.cra_init = qat_alg_aead_sha512_init,
	.cra_exit = qat_alg_aead_exit,
	.cra_u = {
		.aead = {
			.setkey = qat_alg_aead_setkey,
			.decrypt = qat_alg_aead_dec,
			.encrypt = qat_alg_aead_enc,
			.givencrypt = qat_alg_aead_genivenc,
			.ivsize = AES_BLOCK_SIZE,
			.maxauthsize = SHA512_DIGEST_SIZE,
		},
	},
}, {
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
			.setkey = qat_alg_ablkcipher_setkey,
			.decrypt = qat_alg_ablkcipher_blk_decrypt,
			.encrypt = qat_alg_ablkcipher_blk_encrypt,
			.min_keysize = AES_MIN_KEY_SIZE,
			.max_keysize = AES_MAX_KEY_SIZE,
			.ivsize = AES_BLOCK_SIZE,
		},
	},
} };

int qat_algs_register(void)
{
	int ret = 0;

	mutex_lock(&algs_lock);
	if (++active_devs == 1) {
		int i;

		for (i = 0; i < ARRAY_SIZE(qat_algs); i++)
			qat_algs[i].cra_flags =
				(qat_algs[i].cra_type == &crypto_aead_type) ?
				CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_ASYNC :
				CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC;

		ret = crypto_register_algs(qat_algs, ARRAY_SIZE(qat_algs));
		if (ret)
			goto unlock;
#ifdef QAT_LEGACY_ALGORITHMS
		ret = crypto_register_algs(qat_legacy_algs,
					   ARRAY_SIZE(qat_legacy_algs));
		if (ret)
			crypto_unregister_algs(qat_algs, ARRAY_SIZE(qat_algs));
#endif
	}
unlock:
	mutex_unlock(&algs_lock);
	return ret;
}

void qat_algs_unregister(void)
{
	mutex_lock(&algs_lock);
	if (--active_devs == 0) {
#ifdef QAT_LEGACY_ALGORITHMS
		crypto_unregister_algs(qat_legacy_algs,
				       ARRAY_SIZE(qat_legacy_algs));
#endif
		crypto_unregister_algs(qat_algs, ARRAY_SIZE(qat_algs));
	}

	mutex_unlock(&algs_lock);
}
#endif
