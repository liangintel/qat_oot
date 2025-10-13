/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2021 Intel Corporation */
#ifndef _QAT_CRYPTO_INSTANCE_H_
#define _QAT_CRYPTO_INSTANCE_H_

#include <crypto/aes.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/version.h>
#include "adf_accel_devices.h"
#include "icp_qat_fw_la.h"

#define SEC ADF_KERNEL_SEC
#define ADF_DEFAULT_ASYM_RING_SIZE 128
#define ADF_DEFAULT_SYM_RING_SIZE 512
#define QAT_MAX_PRE_ALLOCATED_BUFFS 4

struct qat_crypto_instance {
	struct adf_etr_ring_data *sym_tx;
	struct adf_etr_ring_data *sym_rx;
	struct adf_etr_ring_data *pke_tx;
	struct adf_etr_ring_data *pke_rx;
	struct adf_accel_dev *accel_dev;
	struct list_head list;
	unsigned long state;
	int id;
	atomic_t refctr;
};

struct qat_crypto_request_buffs {
	struct qat_alg_buf_list *bl;
	dma_addr_t blp;
	struct qat_alg_buf_list *blout;
	dma_addr_t bloutp;
	size_t sz;
	size_t sz_out;
};

struct qat_crypto_request;

struct qat_alg_buf {
	u32 len;
	u32 resrvd;
	u64 addr;
} __packed;

struct qat_alg_buf_list {
	u64 resrvd;
	u32 num_bufs;
	u32 num_mapped_bufs;
	struct qat_alg_buf bufers[QAT_MAX_PRE_ALLOCATED_BUFFS];
	bool is_buf_dynamic;
} __packed __aligned(64);

struct qat_crypto_request {
	struct icp_qat_fw_la_bulk_req req;
	union {
		struct qat_alg_aead_ctx *aead_ctx;
#if KERNEL_VERSION(5, 5, 0) > LINUX_VERSION_CODE
		struct qat_alg_ablkcipher_ctx *ablkcipher_ctx;
#else
		struct qat_alg_skcipher_ctx *skcipher_ctx;
#endif
	};
	union {
		struct aead_request *aead_req;
#if KERNEL_VERSION(5, 5, 0) > LINUX_VERSION_CODE
		struct ablkcipher_request *ablkcipher_req;
#else
		struct skcipher_request *skcipher_req;
#endif
	};
	struct qat_crypto_request_buffs buf;
	void (*cb)(struct icp_qat_fw_la_resp *resp,
		   struct qat_crypto_request *req);
	union {
		struct {
			u64 iv_hi;
			u64 iv_lo;
		};
		u8 iv[AES_BLOCK_SIZE];
	};
	bool encryption;
	struct qat_alg_buf_list bufl;
	struct qat_alg_buf_list buflout;
};

static inline gfp_t qat_algs_alloc_flags(struct crypto_async_request *req)
{
	return req->flags & CRYPTO_TFM_REQ_MAY_SLEEP ? GFP_KERNEL : GFP_ATOMIC;
}

#endif
