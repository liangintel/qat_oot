// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2021 Intel Corporation */
#include <linux/firmware.h>
#include <linux/pci.h>
#include "adf_cfg.h"
#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "icp_qat_uclo.h"

#define MMP_VERSION_LEN 4
#define ADF_SELF_TEST_FW	"rsa_sha_kat.bin"

struct adf_mmp_version_s {
	u8 ver_val[MMP_VERSION_LEN];
};

static int adf_ae_fw_integr_test(struct adf_accel_dev *accel_dev)
{
	struct adf_fw_loader_data *loader_data = accel_dev->fw_loader;
	char obj_name[] = ADF_SELF_TEST_FW;

	u32 fw_size = loader_data->uof_fw->size;
	void *fw_addr = (void *)loader_data->uof_fw->data;

	if (qat_uclo_map_obj(loader_data->fw_loader, fw_addr,
			     fw_size, obj_name)) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to map KAT firmware\n");
		return -EFAULT;
	}
	/* set mask to 0 to skip loading process */
	loader_data->fw_loader->cfg_ae_mask = 0;
	if (qat_uclo_wr_all_uimage(loader_data->fw_loader)) {
		dev_err(&GET_DEV(accel_dev),
			"FW integrity self-test failed!\n");
		return -EFAULT;
	}

	dev_info(&GET_DEV(accel_dev),
		 "FW integrity self-test passed!\n");
	qat_uclo_del_obj(loader_data->fw_loader);
	return 0;
}

int adf_ae_fw_load(struct adf_accel_dev *accel_dev)
{
	struct adf_fw_loader_data *loader_data = accel_dev->fw_loader;
	struct adf_hw_device_data *hw_device = accel_dev->hw_device;
	void *fw_addr, *mmp_addr;
	u32 fw_size, mmp_size;
	s32 i = 0;
	u32 max_objs = 1;
	char *obj_name = NULL;
	struct adf_mmp_version_s mmp_ver = { {0} };
	bool load_mmp = false;

	if (!hw_device->fw_name)
		return 0;

	if (hw_device->qat_aux_enable) {
		if (hw_device->aux_ops->get_aux_fw_name &&
		    hw_device->aux_ops->get_aux_fw_name(accel_dev)) {
			dev_err(&GET_DEV(accel_dev),
				"Failed to get aux fw name\n");
			return -EFAULT;
		}
	}

	if (request_firmware(&loader_data->uof_fw, hw_device->fw_name,
			     &accel_dev->accel_pci_dev.pci_dev->dev)) {
		dev_err(&GET_DEV(accel_dev), "Failed to load UOF firmware %s\n",
			hw_device->fw_name);
		goto out_err;
	}

	fw_size = loader_data->uof_fw->size;
	fw_addr = (void *)loader_data->uof_fw->data;

	if (hw_device->fw_integr_selftest && adf_ae_fw_integr_test(accel_dev))
		goto out_err;

	load_mmp = hw_device->load_mmp_always ||
		(hw_device->accel_capabilities_mask &
			ADF_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC);

	if (load_mmp) {
		if (request_firmware(&loader_data->mmp_fw,
				     hw_device->fw_mmp_name,
				     &accel_dev->accel_pci_dev.pci_dev->dev)) {
			dev_err(&GET_DEV(accel_dev),
				"Failed to request MMP firmware %s\n",
				hw_device->fw_mmp_name);
			return -EFAULT;
		}

		mmp_size = loader_data->mmp_fw->size;
		mmp_addr = (void *)loader_data->mmp_fw->data;

		memcpy(&mmp_ver, mmp_addr, MMP_VERSION_LEN);

		accel_dev->fw_versions.mmp_version_major = mmp_ver.ver_val[0];
		accel_dev->fw_versions.mmp_version_minor = mmp_ver.ver_val[1];
		accel_dev->fw_versions.mmp_version_patch = mmp_ver.ver_val[2];

		if (qat_uclo_wr_mimage(loader_data->fw_loader, mmp_addr,
				       mmp_size)) {
			dev_err(&GET_DEV(accel_dev), "Failed to load MMP\n");
			goto out_err;
		}
	}

	if (hw_device->get_objs_num)
		max_objs = hw_device->get_objs_num(accel_dev);

	for (i = max_objs - 1; i >= 0; i--) {
		/* obj_name is used to indicate the firmware name in MOF;
		 * for CPM1X: AE0 must be loaded at end for authentication;
		 * for CPM20: AE0 must be loaded last but admin AE
		 */
		if (hw_device->get_obj_name &&
		    hw_device->get_obj_cfg_ae_mask) {
			unsigned long service_mask = hw_device->service_mask;
			enum adf_accel_unit_services service_type =
				ADF_ACCEL_SERVICE_NULL;

			if (hw_device->get_service_type)
				service_type = hw_device->get_service_type
							(accel_dev, i);
			else
				service_type = BIT(i);

			if (service_mask && !(service_mask & service_type))
				continue;
			obj_name = (char *)hw_device->get_obj_name(accel_dev,
							   service_type);
			if (!obj_name) {
				dev_err(&GET_DEV(accel_dev),
					"Invalid object (service = 0x%x)\n",
					service_type);
				goto out_err;
			}
			if (!hw_device->get_obj_cfg_ae_mask(accel_dev,
							    service_type))
				continue;
			if (qat_uclo_set_cfg_ae_mask(
				loader_data->fw_loader,
				hw_device->get_obj_cfg_ae_mask(accel_dev,
							       service_type))) {
				dev_err(&GET_DEV(accel_dev),
					"Invalid config AE mask\n");
				goto out_err;
			}
		}

		if (qat_uclo_map_obj(loader_data->fw_loader, fw_addr,
				     fw_size, obj_name)) {
			dev_err(&GET_DEV(accel_dev),
				"Failed to map UOF firmware\n");
			goto out_err;
		}
		if (qat_uclo_wr_all_uimage(loader_data->fw_loader)) {
			dev_err(&GET_DEV(accel_dev),
				"Failed to load UOF firmware\n");
			goto out_err;
		}
		if (accel_dev->hw_device->qat_aux_enable) {
			if (!obj_name) {
				goto out_err;
			}
			if (!strcmp(obj_name, accel_dev->hw_device->fw_aux_obj))
				accel_dev->au_info->aux_ae_mask =
					qat_uclo_get_aux_ae_mask(loader_data,
								 max_objs, i);
		}
		qat_uclo_del_obj(loader_data->fw_loader);
		obj_name = NULL;
	}

	return 0;

out_err:
	adf_ae_fw_release(accel_dev);
	return -EFAULT;
}

void adf_ae_fw_release(struct adf_accel_dev *accel_dev)
{
	struct adf_fw_loader_data *loader_data = accel_dev->fw_loader;
	struct adf_hw_device_data *hw_device = accel_dev->hw_device;

	if (!hw_device->fw_name)
		return;

	qat_uclo_del_obj(loader_data->fw_loader);
	if (loader_data->fw_loader->mobj_handle)
		qat_uclo_del_mof(loader_data->fw_loader);
	qat_hal_deinit(loader_data->fw_loader);
	loader_data->fw_loader = NULL;

	if (loader_data->mmp_fw) {
		release_firmware(loader_data->mmp_fw);
		loader_data->mmp_fw = NULL;
	}

	if (loader_data->uof_fw) {
		release_firmware(loader_data->uof_fw);
		loader_data->uof_fw = NULL;
	}
}

int adf_ae_start(struct adf_accel_dev *accel_dev)
{
	struct adf_fw_loader_data *loader_data = accel_dev->fw_loader;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u32 ae_ctr;

	if (!hw_data->fw_name)
		return 0;

	ae_ctr = qat_hal_start(loader_data->fw_loader);
	dev_info(&GET_DEV(accel_dev),
		 "qat_dev%d started %d acceleration engines\n",
		 accel_dev->accel_id, ae_ctr);
	return 0;
}

int adf_ae_stop(struct adf_accel_dev *accel_dev)
{
	struct adf_fw_loader_data *loader_data = accel_dev->fw_loader;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u32 ae_ctr, ae, max_aes = GET_MAX_ACCELENGINES(accel_dev);

	if (!hw_data->fw_name)
		return 0;

	for (ae = 0, ae_ctr = 0; ae < max_aes; ae++) {
		if (hw_data->ae_mask & (1 << ae)) {
			qat_hal_stop(loader_data->fw_loader, ae, 0xFF);
			ae_ctr++;
		}
	}
	dev_info(&GET_DEV(accel_dev),
		 "qat_dev%d stopped %d acceleration engines\n",
		 accel_dev->accel_id, ae_ctr);
	return 0;
}

static int adf_ae_reset(struct adf_accel_dev *accel_dev, int ae)
{
	struct adf_fw_loader_data *loader_data = accel_dev->fw_loader;

	qat_hal_reset(loader_data->fw_loader);
	if (qat_hal_clr_reset(loader_data->fw_loader))
		return -EFAULT;

	return 0;
}

int adf_ae_init(struct adf_accel_dev *accel_dev)
{
	struct adf_fw_loader_data *loader_data;
	struct adf_hw_device_data *hw_device = accel_dev->hw_device;

	if (!hw_device->fw_name)
		return 0;

	loader_data = kzalloc(sizeof(*loader_data), GFP_KERNEL);
	if (!loader_data)
		return -ENOMEM;

	accel_dev->fw_loader = loader_data;
	if (qat_hal_init(accel_dev)) {
		dev_err(&GET_DEV(accel_dev), "Failed to init the AEs\n");
		kfree(loader_data);
		return -EFAULT;
	}
	if (adf_ae_reset(accel_dev, 0)) {
		dev_err(&GET_DEV(accel_dev), "Failed to reset the AEs\n");
		qat_hal_deinit(loader_data->fw_loader);
		kfree(loader_data);
		return -EFAULT;
	}
	return 0;
}

int adf_ae_shutdown(struct adf_accel_dev *accel_dev)
{
	struct adf_fw_loader_data *loader_data = accel_dev->fw_loader;
	struct adf_hw_device_data *hw_device = accel_dev->hw_device;

	if (!hw_device->fw_name)
		return 0;

	qat_hal_deinit(loader_data->fw_loader);
	kfree(accel_dev->fw_loader);
	accel_dev->fw_loader = NULL;
	return 0;
}
