/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2020 Intel Corporation */

#ifndef ADF_VDCM_CAPS_H
#define ADF_VDCM_CAPS_H

enum adf_vqat_cap_id {
	ADF_VQAT_CAP_INVALID_ID,
	ADF_VQAT_CAP_DEV_FREQ_ID,
	ADF_VQAT_CAP_SVC_MAP_ID,
	ADF_VQAT_CAP_SVC_MASK_ID,
	ADF_VQAT_CAP_SVC_DC_EXT_ID,
	ADF_VQAT_CAP_SVC_KPT_CERT_ID,
	ADF_VQAT_CAP_SVC_SYM_CIPHER_ID,
	ADF_VQAT_CAP_SVC_SYM_HASH_ID,
	ADF_VQAT_CAP_SVC_ASYM_ID,
	ADF_VQAT_CAP_MAX_IDS,
};

#define ADF_VQAT_CAP_ATTR_INT	0
#define ADF_VQAT_CAP_ATTR_STR	1

enum vqat_type;
struct adf_vqat_class;

struct adf_vqat_cap {
	u16 id;
	u16 len;
	u32 rsvd;
	union {
		u64 value;
		u8 str[0];
	} data;
};

struct adf_vdcm_vqat_cap_blk {
	/* Indicates the length of this block */
	u16 len;
	/* Indicates the number of caps */
	u16 number;
	u32 rsvd;
	/* caps chain */
	struct adf_vqat_cap head[0];
};

#ifdef CONFIG_CRYPTO_DEV_QAT_VDCM
struct adf_vdcm_vqat_cap {
	struct adf_vdcm_vqat_cap_blk *blk;
};

static inline
void *adf_vqat_caps_blk(struct adf_vdcm_vqat_cap *vcap)
{
	return ((void *)vcap->blk);
}

static inline
void adf_vqat_caps_set_blk(struct adf_vdcm_vqat_cap *vcap,
			   struct adf_vdcm_vqat_cap_blk *blk)
{
	vcap->blk = blk;
}

static inline
void adf_vqat_caps_clone_blk(struct adf_vdcm_vqat_cap *vcap,
			     struct adf_vdcm_vqat_cap *src_vcap)
{
	vcap->blk = src_vcap->blk;
}

static inline
u16 adf_vqat_caps_blk_size(struct adf_vdcm_vqat_cap *vcap)
{
	return vcap->blk->len;
}

static inline
u16 adf_vqat_caps_blk_num(struct adf_vdcm_vqat_cap *vcap)
{
	return vcap->blk->number;
}

static inline u16 adf_vqat_cap_header_size(void)
{
	return offsetof(struct adf_vqat_cap, data);
}

static inline
struct adf_vqat_cap *adf_vqat_caps_next_cap(struct adf_vdcm_vqat_cap *vcap,
					    struct adf_vqat_cap *curr)
{
	if (!curr)
		return (struct adf_vqat_cap *)vcap->blk->head;
	return  (struct adf_vqat_cap *)((u8 *)curr + curr->len);
}

struct adf_vqat_enabled_cap {
	u16 id;
	u16 type;
	u16 len;
	int (*get_value)(struct adf_accel_dev *parent,
			 enum vqat_type type, u64 *v);
};

struct adf_vqat_enabled_caps {
#define ADF_VDCM_MAX_ENABLED_CAPS	8
	struct adf_vqat_enabled_cap caps[ADF_VDCM_MAX_ENABLED_CAPS];
	u16 total;
	u16 number;
};

u16 adf_vqat_populate_cap(struct adf_vqat_cap *cap,
			  struct adf_vqat_enabled_cap *enabled_cap,
			  struct adf_accel_dev *parent,
			  enum vqat_type type);
static inline
void adf_vqat_enabled_caps_init(struct adf_vqat_enabled_caps *enabled)
{
	memset(enabled, 0, sizeof(*enabled));
	enabled->total = ADF_VDCM_MAX_ENABLED_CAPS;
}

static inline
void adf_vqat_enabled_caps_cleanup(struct adf_vqat_enabled_caps *enabled)
{
	enabled->total = 0;
}

static inline
int adf_vqat_enabled_caps_add(struct adf_vqat_enabled_caps *enabled,
			      u16 id, u16 type, u16 len,
			      int (*get_value)(struct adf_accel_dev *parent,
					       enum vqat_type type, u64 *v))
{
	struct adf_vqat_enabled_cap *cap;

	if (enabled->number >= enabled->total)
		return -EINVAL;
	cap = &enabled->caps[enabled->number++];
	cap->id = id;
	cap->type = type;
	cap->len = (type == ADF_VQAT_CAP_ATTR_INT) ? sizeof(u64) : len;
	cap->get_value = get_value;

	return adf_vqat_cap_header_size() + cap->len;
}

static inline
int adf_vqat_enabled_caps_num(struct adf_vqat_enabled_caps *enabled)
{
	return enabled->number;
}

int adf_vdcm_alloc_vqat_svc_cap_def(struct adf_vdcm_vqat_cap *vcap,
				    struct adf_accel_dev *parent,
				    struct adf_vqat_class *dclass);
void adf_vdcm_free_vqat_svc_cap_def(struct adf_vdcm_vqat_cap *vcap,
				    struct adf_accel_dev *parent,
				    struct adf_vqat_class *dclass);
#endif /* CONFIG_CRYPTO_DEV_QAT_VDCM */
#endif /*ADF_VDCM_CPAS_H*/
