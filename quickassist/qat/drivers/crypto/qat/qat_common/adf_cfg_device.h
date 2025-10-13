/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2021 Intel Corporation */
#ifndef ADF_CFG_DEVICE_H_
#define ADF_CFG_DEVICE_H_

#include "adf_cfg.h"
#include "adf_cfg_bundle.h"
#include "adf_cfg_instance.h"
#include "adf_common_drv.h"

#if defined(QAT_UIO) || defined(QAT_ESXI)
static inline void adf_cfg_fw_string_to_id(char *str,
					   struct adf_accel_dev *accel_dev,
					   enum adf_cfg_fw_image_type **fw)
{
	if (!strncmp(str, ADF_SERVICES_DEFAULT,
		     sizeof(ADF_SERVICES_DEFAULT)))
		**fw = ADF_FW_IMAGE_DEFAULT;
	else if (!strncmp(str, ADF_SERVICES_CRYPTO,
			  sizeof(ADF_SERVICES_CRYPTO)))
		**fw = ADF_FW_IMAGE_CRYPTO;
	else if (!strncmp(str, ADF_SERVICES_COMPRESSION,
			  sizeof(ADF_SERVICES_COMPRESSION)))
		**fw = ADF_FW_IMAGE_COMPRESSION;
	else if (!strncmp(str, ADF_SERVICES_CUSTOM1,
			  sizeof(ADF_SERVICES_CUSTOM1))) {
		**fw = ADF_FW_IMAGE_CUSTOM1;
	} else	{
		**fw = ADF_FW_IMAGE_DEFAULT;
		dev_warn(&GET_DEV(accel_dev),
			 "Invalid ServicesProfile: %s,", str);
	}
}
#endif /* QAT_UIO || QAT_ESXI */

struct adf_cfg_device {
	/* contains all the bundles info */
	struct adf_cfg_bundle **bundles;
	/* contains all the instances info */
	struct adf_cfg_instance **instances;
	int bundle_num;
	int instance_index;
	char name[ADF_CFG_MAX_STR_LEN];
	int dev_id;
	u16 total_num_inst;
	u16 bundles_free;
	int adi_num;
};

int adf_cfg_get_ring_pairs(struct adf_cfg_device *device,
			   struct adf_cfg_instance *inst,
			   const char *process_name,
			   struct adf_accel_dev *accel_dev);

int adf_cfg_device_init(struct adf_cfg_device *device,
			struct adf_accel_dev *accel_dev);

void adf_cfg_device_clear(struct adf_cfg_device *device,
			  struct adf_accel_dev *accel_dev);

void adf_cfg_device_clear_all(struct adf_accel_dev *accel_dev);

int adf_cfg_get_intr_inst(struct adf_accel_dev *accel_dev,
			  u16 *num_of_intr_inst);

int adf_cfg_get_num_of_inst(struct adf_accel_dev *accel_dev,
			    u16 *num_cy_inst,
			    u16 *num_dc_inst);
int adf_cfg_get_services_enabled(struct adf_accel_dev *accel_dev,
				 u16 *serv_ena_mask);
void adf_cfg_get_accel_algo_cap(struct adf_accel_dev *accel_dev);

int update_accel_cap_mask(struct adf_accel_dev *accel_dev);
int update_accel_cap_mask_c62x(struct adf_accel_dev *accel_dev);
int adf_cfg_check_deprecated_params(struct adf_accel_dev *accel_dev);
#endif /* !ADF_CFG_DEVICE_H_ */
