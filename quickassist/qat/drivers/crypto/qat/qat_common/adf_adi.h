/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2019 - 2021 Intel Corporation */
#ifndef ADF_ADI_H_
#define ADF_ADI_H_

#define MAX_ADI_NAME_LEN 64

enum adi_service_type {
	ADI_TYPE_INVALID = 0,
	ADI_TYPE_COMP,
	ADI_TYPE_SYM,
	ADI_TYPE_ASYM,
};

enum adi_status {
	ADI_STATUS_INVALID = 0,
	ADI_STATUS_IDLE,
	ADI_STATUS_ACTIVE,
};

struct adi_mmio_info {
	phys_addr_t phy_addr;
	void __iomem *virt_addr;
	u64 size;
};

struct adf_adi_ep {
	struct adf_accel_dev *parent;
	char name[MAX_ADI_NAME_LEN];
	s32 adi_idx;
	u32 bank_idx;
	/* Tx/Rx ring offsets within the parent bank */
	u32 tx_idx;
	u32 rx_idx;
	u32 pasid;
	enum adi_status status;
	enum adi_service_type type;
	struct adf_adi_ops *adi_ops;
	struct mutex lock;	/* lock for ADI alloc/free */
	int ims_group;
	void *hw_priv;
	bool reset_complete;
};

struct adf_adi_info {
	int adi_num;
	struct adf_adi_ep *adis;
};

struct adi_priv_data {
	struct adf_bar *etr_bar;
	struct adf_bar *misc_bar;
	struct adi_mmio_info etr_mmap;
};

struct msi_msg;
struct adf_adi_ops {
	int (*init)(struct adf_adi_ep *self);
	void (*destroy)(struct adf_adi_ep *self);
	int (*enable)(struct adf_adi_ep *self);
	int (*disable)(struct adf_adi_ep *self);
	int (*reset)(struct adf_adi_ep *self, bool restore_pasid);
	unsigned int (*irq_enable)(struct adf_adi_ep *self);
	unsigned int (*irq_disable)(struct adf_adi_ep *self);
	int (*irq_write_msi_msg)(struct adf_adi_ep *self, struct msi_msg *msg);
	int (*set_pasid)(struct adf_adi_ep *self, int pasid);
	int (*get_pasid)(struct adf_adi_ep *self);
	int (*get_mmio_info)(struct adf_adi_ep *self,
			     struct adi_mmio_info *mmio_info);
	int (*vreg_write)(struct adf_adi_ep *self,
			  u64 pos, void *buf, unsigned int len);
	int (*vreg_read)(struct adf_adi_ep *self,
			 u64 pos, void *buf, unsigned int len);
} __packed;

int adf_init_adis(struct adf_accel_dev *accel_dev);

void adf_exit_adis(struct adf_accel_dev *accel_dev);

struct adf_adi_ep *adf_adi_alloc(struct adf_accel_dev *accel_dev,
				 enum adi_service_type type);

int adf_adi_free(struct adf_adi_ep *adi);

int adf_get_num_avail_adis(struct adf_accel_dev *accel_dev,
			   enum adi_service_type type);

int adf_get_num_max_adis(struct adf_accel_dev *accel_dev,
			 enum adi_service_type type);

#endif
