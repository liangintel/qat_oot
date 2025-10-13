/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2020 Intel Corporation */

#ifndef ADF_PASID_H_
#define ADF_PASID_H_
#include "adf_accel_devices.h"
#include "adf_svm.h"

void adf_pasid_init(void);
void adf_pasid_destroy(void);
int adf_pasid_bind_bank_with_pid(struct adf_accel_dev *accel_dev,
				 u32 bank_nr, int pid,
				 cleanup_svm_orphan_fn,
				 void *cleanup_priv);
int adf_pasid_unbind_bank_with_pid(struct adf_accel_dev *accel_dev,
				   int pid,
				   u32 bank_nr);
int adf_pasid_config_bank(struct adf_accel_dev *accel_dev,
			  u32 bank_number,
			  bool enable,
			  bool at, bool adi, bool priv,
			  int pasid);

#endif
