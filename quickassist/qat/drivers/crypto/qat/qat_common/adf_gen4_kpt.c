// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2019 - 2021 Intel Corporation */

#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "adf_cfg_strings.h"
#include "adf_gen4_kpt.h"
#include "adf_gen4_hw_data.h"
#include "icp_qat_fw_init_admin.h"
#include "adf_cfg.h"

#define ADF_GEN4_KPT_CAP_MASK (0x10000)
#define ADF_GEN4_KPT_MAX_SWK_COUNT_PER_FNPASID (128)
#define ADF_GEN4_KPT_MAX_SWK_TTL (31536000)


static bool adf_cfg_kpt_enabled(struct adf_accel_dev *accel_dev)
{
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	char val[ADF_CFG_MAX_VAL_LEN_IN_BYTES];
	u32 kpt_enabled = 1;

	snprintf(key, sizeof(key), ADF_DEV_KPT_ENABLE);
	if (!adf_cfg_get_param_value(accel_dev, ADF_GENERAL_SEC, key, val)) {
		if (kstrtouint(val, 0, &kpt_enabled)) {
			dev_err(&GET_DEV(accel_dev), "Invalid kpt flag\n");
			return false;
		}
	}
	return (kpt_enabled != 0);
}

static bool get_kpt_capability(struct adf_accel_dev *accel_dev)
{
	struct pci_dev *pdev = accel_to_pci_dev(accel_dev);
	u32 val = 0;
	pci_read_config_dword(pdev, ADF_GEN4_FUSECTL0_OFFSET, &val);
	if (!(val & ADF_GEN4_KPT_CAP_MASK) &&
	    adf_cfg_kpt_enabled(accel_dev))
		return true;
	return false;
}

static int get_kpt_config(struct adf_accel_dev *accel_dev, void *virt_addr)
{
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	char val[ADF_CFG_MAX_VAL_LEN_IN_BYTES];
	u32 max_swk_count_per_fn = 0;
	u32 max_swk_count_per_pasid = 0;
	u32 max_swk_lifetime = 0;
	u32 swk_shared = 1;
	struct icp_qat_fw_init_admin_kpt_config_params *config_params =
		(struct icp_qat_fw_init_admin_kpt_config_params *)virt_addr;

	snprintf(key, sizeof(key), ADF_KPT_MAX_SWK_COUNT_PER_FN);
	if (!adf_cfg_get_param_value(accel_dev, ADF_GENERAL_SEC, key, val)) {
		if (kstrtouint(val, 0, &max_swk_count_per_fn)) {
			dev_err(&GET_DEV(accel_dev),
				"Invalid KptMaxSWKPerFn configuration\n");
			return -EFAULT;
		}
	}

	if (max_swk_count_per_fn > ADF_GEN4_KPT_MAX_SWK_COUNT_PER_FNPASID)
		max_swk_count_per_fn = ADF_GEN4_KPT_MAX_SWK_COUNT_PER_FNPASID;

	snprintf(key, sizeof(key), ADF_KPT_MAX_SWK_COUNT_PER_PASID);
	if (!adf_cfg_get_param_value(accel_dev, ADF_GENERAL_SEC, key, val)) {
		if (kstrtouint(val, 0, &max_swk_count_per_pasid)) {
			dev_err(&GET_DEV(accel_dev),
				"Invalid KptMaxSWKPerPASID configuration\n");
			return -EFAULT;
		}
	}

	if (max_swk_count_per_pasid > ADF_GEN4_KPT_MAX_SWK_COUNT_PER_FNPASID)
		max_swk_count_per_pasid =
			ADF_GEN4_KPT_MAX_SWK_COUNT_PER_FNPASID;

	snprintf(key, sizeof(key), ADF_KPT_MAX_SWK_TTL);
	if (!adf_cfg_get_param_value(accel_dev, ADF_GENERAL_SEC, key, val)) {
		if (kstrtouint(val, 0, &max_swk_lifetime)) {
			dev_err(&GET_DEV(accel_dev),
				"Invalid KptMaxSWKLifetime configuration\n");
			return -EFAULT;
		}
	}

	if (max_swk_lifetime > ADF_GEN4_KPT_MAX_SWK_TTL)
		max_swk_lifetime = ADF_GEN4_KPT_MAX_SWK_TTL;

	snprintf(key, sizeof(key), ADF_KPT_SWK_SHARED);
	if (!adf_cfg_get_param_value(accel_dev, ADF_GENERAL_SEC, key, val)) {
		if (kstrtouint(val, 0, &swk_shared)) {
			dev_err(&GET_DEV(accel_dev),
				"Invalid KptSWKShared configuration\n");
			return -EFAULT;
		}
	}

	config_params->swk_count_per_fn = max_swk_count_per_fn;
	config_params->swk_count_per_pasid = max_swk_count_per_pasid;
	config_params->swk_ttl_in_secs = max_swk_lifetime;
	config_params->swk_shared = swk_shared;

	return 0;
}

static int enable_kpt(struct adf_accel_dev *accel_dev)
{
	struct icp_qat_fw_init_admin_req req;
	struct icp_qat_fw_init_admin_resp resp;
	dma_addr_t phy_kpt_config_addr;
	void *virt_kpt_config_addr;
#ifdef QAT_UIO
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	unsigned long val;
#endif
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u32 ae_mask = hw_data->admin_ae_mask;
	int status = 0;

	memset(&req, 0, sizeof(struct icp_qat_fw_init_admin_req));
	memset(&resp, 0, sizeof(struct icp_qat_fw_init_admin_resp));

	virt_kpt_config_addr = dma_alloc_coherent(&GET_DEV(accel_dev),
						  PAGE_SIZE,
						  &phy_kpt_config_addr,
						  GFP_KERNEL);
	if (!virt_kpt_config_addr) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to allocate DMA buffer\n");
		return -ENOMEM;
	}

	req.cmd_id = ICP_QAT_FW_KPT_ENABLE;
	if (!get_kpt_config(accel_dev, virt_kpt_config_addr)) {
		req.init_cfg_sz =
			sizeof(struct icp_qat_fw_init_admin_kpt_config_params);
		req.init_cfg_ptr = phy_kpt_config_addr;
	} else {
		status = -EFAULT;
		goto exit;
	}

	/* Issue ICP_QAT_FW_KPT_ENABLE via init/admin interface to
	 * 1, enable kpt capability
	 * 2, load device per-part key and signature
	 * If the respone status is ICP_QAT_FW_INIT_RESP_STATUS_FAIL, it
	 * indicates KPT is disabled.
	 */

	if (adf_send_admin(accel_dev, &req, &resp, ae_mask)) {
		if (resp.status == ICP_QAT_FW_INIT_RESP_STATUS_FAIL) {
			hw_data->accel_capabilities_mask &=
				~ICP_ACCEL_CAPABILITIES_KPT2;
#ifdef QAT_UIO
			/* Update HW capability configuration */
			snprintf(key, sizeof(key), ADF_DEV_CAPABILITIES_MASK);
			val = hw_data->accel_capabilities_mask;
			if (adf_cfg_add_key_value_param(accel_dev,
							ADF_GENERAL_SEC,
							key,
							(void *)val,
							ADF_HEX))
				status = -EFAULT;
#endif
		} else {
			dev_err(&GET_DEV(accel_dev), "Failed to enable KPT\n");
			status = -EFAULT;
		}
		goto exit;
	}
	dev_info(&GET_DEV(accel_dev), "KPT is enabled\n");

exit:
	dma_free_coherent(&GET_DEV(accel_dev),
			  PAGE_SIZE,
			  virt_kpt_config_addr,
			  phy_kpt_config_addr);
	return status;
}

int adf_gen4_config_kpt(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;

	if (hw_data->accel_capabilities_mask & ICP_ACCEL_CAPABILITIES_KPT2)
		if (enable_kpt(accel_dev))
			return -EFAULT;
	return 0;
}
EXPORT_SYMBOL_GPL(adf_gen4_config_kpt);

int adf_gen4_init_kpt(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;

	if (get_kpt_capability(accel_dev))
		hw_data->accel_capabilities_mask |=
			ICP_ACCEL_CAPABILITIES_KPT2;
	else
		hw_data->accel_capabilities_mask &=
			~ICP_ACCEL_CAPABILITIES_KPT2;
	return 0;
}
EXPORT_SYMBOL_GPL(adf_gen4_init_kpt);
