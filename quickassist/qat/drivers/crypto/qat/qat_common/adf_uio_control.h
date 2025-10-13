/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2021 Intel Corporation */
#ifndef QAT_UIO_CONTROL_H
#define QAT_UIO_CONTROL_H

#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/uio_driver.h>

#include "adf_uio.h"
#define UIO_MAX_NAME_LENGTH 32
#define INVALID_BUNDLE_INDEX 9999

struct pci_dev;

struct adf_uio_instance_rings {
	unsigned int user_pid;
	u16 ring_mask;
	struct list_head list;
};

struct adf_uio_control_bundle {
	struct kobject kobj;
	char name[UIO_MAX_NAME_LENGTH];
	u8 hardware_bundle_number;
	unsigned int device_minor;
	struct list_head list;
	struct mutex list_lock; /* protects list struct */
	struct mutex lock; /* protects rings_used and csr_addr */
	u16 rings_used;
	u32 rings_enabled;
	void *csr_addr;
	struct vm_area_struct *vma;
	struct uio_info uio_info;
	struct qat_uio_bundle_dev uio_priv;
};

struct adf_uio_control_accel {
	struct adf_accel_dev *accel_dev;
	struct kobject kobj;
	unsigned int nb_bundles;
	unsigned int first_minor;
	unsigned int last_minor;
	unsigned int num_ker_bundles;
	/* bundle[] must be last to allow dynamic size allocation. */
	struct adf_uio_control_bundle *bundle[0];
};

int adf_uio_sysfs_create(struct adf_accel_dev *accel_dev);
void adf_uio_sysfs_bundle_delete(struct adf_accel_dev *accel_dev,
				 unsigned int bundle_num);
void adf_uio_sysfs_delete(struct adf_accel_dev *accel_dev);
int adf_uio_sysfs_bundle_create(struct pci_dev *pdev,
				unsigned int bundle_num,
				struct adf_uio_control_accel *accel);

void adf_uio_accel_ref(struct adf_uio_control_accel *accel);
void adf_uio_accel_unref(struct adf_uio_control_accel *accel);
void adf_uio_bundle_ref(struct adf_uio_control_bundle *bundle);
void adf_uio_bundle_unref(struct adf_uio_control_bundle *bundle);

int
adf_find_hw_bundle_by_usr(struct adf_accel_dev *accel_dev, u32 uio_bundle_nr);

#endif /* end of include guard: QAT_UIO_CONTROL_H */
