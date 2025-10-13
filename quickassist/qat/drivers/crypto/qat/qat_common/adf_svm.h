/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2018, 2020 Intel Corporation */
#ifndef ADF_SVM_H_
#define ADF_SVM_H_

#include "adf_accel_devices.h"

typedef void (*cleanup_svm_orphan_fn)(void *priv, u32 pid);

void adf_init_svm(void);
void adf_exit_svm(void);
int adf_svm_device_init(struct adf_accel_dev *accel_dev);
void adf_svm_device_exit(struct adf_accel_dev *accel_dev);
int adf_svm_enable_svm(struct adf_accel_dev *accel_dev);
int adf_svm_disable_svm(struct adf_accel_dev *accel_dev);
int adf_svm_bind_bank_with_pid(struct adf_accel_dev *accel_dev,
			       u32 bank_nr, int pid,
			       cleanup_svm_orphan_fn,
			       void *cleanup_priv);
int adf_svm_unbind_bank_with_pid(struct adf_accel_dev *accel_dev,
				 int pid,
				 u32 bank_nr);

#endif
