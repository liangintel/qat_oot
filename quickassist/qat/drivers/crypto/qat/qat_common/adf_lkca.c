// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2021 Intel Corporation */

#include "adf_accel_devices.h"
#include "adf_common_drv.h"

int adf_lkca_register(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	int ret = 0;

	if (!list_empty(&accel_dev->crypto_list)) {
		if (hw_data->accel_capabilities_mask &
		    ADF_ACCEL_CAPABILITIES_CRYPTO_SYMMETRIC)
			ret = qat_algs_register();

		if (hw_data->accel_capabilities_mask &
		    ADF_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC)
			ret |= qat_asym_algs_register();
	}
	return ret;
}
EXPORT_SYMBOL_GPL(adf_lkca_register);

void adf_lkca_unregister(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;

	if (!list_empty(&accel_dev->crypto_list)) {
		if (hw_data->accel_capabilities_mask &
		    ADF_ACCEL_CAPABILITIES_CRYPTO_SYMMETRIC)
			qat_algs_unregister();
		if (hw_data->accel_capabilities_mask &
		    ADF_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC)
			qat_asym_algs_unregister();
	}
}
EXPORT_SYMBOL_GPL(adf_lkca_unregister);
