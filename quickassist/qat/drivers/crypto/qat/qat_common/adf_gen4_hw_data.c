// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2022 Intel Corporation */
#include "adf_accel_devices.h"
#include "adf_dev_err.h"
#include "adf_gen4_hw_data.h"
#include "adf_common_drv.h"
#include "icp_qat_fw_init_admin.h"
#include "adf_heartbeat.h"
#include "adf_transport_internal.h"
#include "adf_transport_access_macros_gen4.h"
#include "adf_gen4_hw_csr_data.h"
#include "adf_cfg.h"
#include "qat_crypto.h"

#define ADF_CONST_TABLE_SIZE 1024

int adf_gen4_check_svc_to_hw_capabilities(struct adf_accel_dev *accel_dev,
					  u32 required_capability)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u32 hw_cap = hw_data->accel_capabilities_mask;

	hw_cap &= required_capability;
	if (hw_cap != required_capability)
		return -EFAULT;

	return 0;
}
EXPORT_SYMBOL_GPL(adf_gen4_check_svc_to_hw_capabilities);

void adf_gen4_enable_iosf_parity(struct adf_accel_dev *accel_dev)
{
	struct pci_dev *pdev = accel_dev->accel_pci_dev.pci_dev;
	u32 pfcgciosfprir;

	pci_cfg_access_lock(pdev);
	pci_read_config_dword(pdev, ADF_GEN4_PFCGC_IOSF_PRIR,
			      &pfcgciosfprir);
	pfcgciosfprir |= ADF_GEN4_PFCGC_IOSF_PRIR_MASK;
	pci_write_config_dword(pdev, ADF_GEN4_PFCGC_IOSF_PRIR,
			       pfcgciosfprir);
	pci_cfg_access_unlock(pdev);
}
EXPORT_SYMBOL_GPL(adf_gen4_enable_iosf_parity);

static int adf_get_fw_status(struct adf_accel_dev *accel_dev,
			     u8 *major, u8 *minor, u8 *patch)
{
	struct icp_qat_fw_init_admin_req req;
	struct icp_qat_fw_init_admin_resp resp;
	u32 ae_mask = 1;

	memset(&req, 0, sizeof(req));
	memset(&resp, 0, sizeof(resp));
	req.cmd_id = ICP_QAT_FW_STATUS_GET;

	if (adf_send_admin(accel_dev, &req, &resp, ae_mask))
		return -EFAULT;

	*major = resp.version_major_num;
	*minor = resp.version_minor_num;
	*patch = resp.version_patch_num;

	return 0;
}

static int adf_get_dc_extcapabilities(struct adf_accel_dev *accel_dev,
				      u32 *capabilities)
{
	struct icp_qat_fw_init_admin_req req;
	struct icp_qat_fw_init_admin_resp resp;
	u8 i;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u8 num_au = hw_data->get_num_accel_units(hw_data);
	u32 first_dc_ae = 0;

	for (i = 0; i < num_au; i++) {
		if (accel_dev->au_info->au[i].services &
		    ADF_ACCEL_COMPRESSION) {
			first_dc_ae = accel_dev->au_info->au[i].ae_mask;
			first_dc_ae &= ~(first_dc_ae - 1);
		}
	}

	memset(&req, 0, sizeof(req));
	memset(&resp, 0, sizeof(resp));
	req.cmd_id = ICP_QAT_FW_COMP_CAPABILITY_GET;

	if (likely(first_dc_ae)) {
		if (adf_send_admin(accel_dev, &req, &resp, first_dc_ae) ||
		    resp.status) {
			*capabilities = 0;
			return -EFAULT;
		}

		*capabilities = resp.extended_features;
	}

	return 0;
}

int adf_gen4_init_chaining(struct adf_accel_dev *accel_dev)
{
	u32 service_chaining_enabled = 0;
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	char val[ADF_CFG_MAX_VAL_LEN_IN_BYTES];
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;

	/* Check if chaining of enabled services is selected by user */
	snprintf(key, sizeof(key), ADF_SERVICE_CHAINING_ENABLED);
	if (!adf_cfg_get_param_value(accel_dev, ADF_GENERAL_SEC, key, val)) {
		if (kstrtoint(val, 0, &service_chaining_enabled)) {
			dev_err(&GET_DEV(accel_dev),
				"Error converting ServiceChainingEnabled setting\n");
			return -EFAULT;
		}
		if (service_chaining_enabled > ADF_SRV_CHAINING_ENABLED) {
			dev_err(&GET_DEV(accel_dev),
				"Invalid ServiceChainingEnabled setting\n");
			return -EFAULT;
		}
	}
	accel_dev->chaining_enabled = (bool)service_chaining_enabled;

	/* Device specific checks for supported services combinations */
	if (hw_data->check_supported_services &&
	    hw_data->check_supported_services(accel_dev)) {
		dev_err(&GET_DEV(accel_dev),
			"HW does not support the configured services!\n");
		return -EFAULT;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(adf_gen4_init_chaining);

int adf_gen4_send_admin_init(struct adf_accel_dev *accel_dev)
{
	int ret = 0;
	struct icp_qat_fw_init_admin_req req;
	struct icp_qat_fw_init_admin_resp resp;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u32 ae_mask = hw_data->ae_mask;
	u32 admin_ae_mask = hw_data->admin_ae_mask;
	u8 num_au = hw_data->get_num_accel_units(hw_data);
	u8 i;
	u32 dc_capabilities = 0;

	for (i = 0; i < num_au; i++) {
		if (accel_dev->au_info->au[i].services ==
			ADF_ACCEL_SERVICE_NULL)
			ae_mask &= ~accel_dev->au_info->au[i].ae_mask;

		if (accel_dev->au_info->au[i].services !=
			ADF_ACCEL_ADMIN)
			admin_ae_mask &= ~accel_dev->au_info->au[i].ae_mask;
	}

	if (!accel_dev->admin) {
		dev_err(&GET_DEV(accel_dev), "adf_admin not available\n");
		return -EFAULT;
	}

	memset(&req, 0, sizeof(req));
	memset(&resp, 0, sizeof(resp));

	req.cmd_id = ICP_QAT_FW_CONSTANTS_CFG;
	req.init_cfg_sz = ADF_CONST_TABLE_SIZE;
	req.init_cfg_ptr = accel_dev->admin->const_tbl_addr;
	if (adf_send_admin(accel_dev, &req, &resp, admin_ae_mask)) {
		dev_err(&GET_DEV(accel_dev),
			"Error sending constants config message\n");
		return -EFAULT;
	}

	if (accel_dev->chaining_enabled) {
		memset(&req, 0, sizeof(req));
		memset(&resp, 0, sizeof(resp));
		req.cmd_id = ICP_QAT_FW_DC_CHAIN_INIT;
		if (adf_send_admin(accel_dev, &req, &resp, ae_mask)) {
			dev_err(&GET_DEV(accel_dev),
				"Error sending dc_chain message\n");
			return -EFAULT;
		}
	}

	memset(&req, 0, sizeof(req));
	memset(&resp, 0, sizeof(resp));
	req.cmd_id = ICP_QAT_FW_INIT_ME;
	if (accel_dev->at_enabled)
		req.fw_flags |= ICP_QAT_FW_INIT_AE_AT_ENABLE_FLAG;

	if (adf_send_admin(accel_dev, &req, &resp, ae_mask)) {
		dev_err(&GET_DEV(accel_dev),
			"Error sending init message\n");
		return -EFAULT;
	}

	memset(&req, 0, sizeof(req));
	memset(&resp, 0, sizeof(resp));
	req.cmd_id = ICP_QAT_FW_HEARTBEAT_TIMER_SET;
	req.init_cfg_ptr = accel_dev->admin->phy_hb_addr;
	if (adf_get_hb_timer(accel_dev, &req.heartbeat_ticks))
		return -EINVAL;

	if (adf_send_admin(accel_dev, &req, &resp, ae_mask))
		dev_err(&GET_DEV(accel_dev), "Heartbeat is not supported\n");

	ret = adf_get_dc_extcapabilities(accel_dev, &dc_capabilities);
	if (unlikely(ret)) {
		dev_err(&GET_DEV(accel_dev),
			"Could not get FW ext. capabilities\n");
	}

	accel_dev->hw_device->extended_dc_capabilities =
		dc_capabilities;

	adf_get_fw_status(accel_dev,
			  &accel_dev->fw_versions.fw_version_major,
			  &accel_dev->fw_versions.fw_version_minor,
			  &accel_dev->fw_versions.fw_version_patch);

	dev_info(&GET_DEV(accel_dev), "FW version: %d.%d.%d\n",
		 accel_dev->fw_versions.fw_version_major,
		 accel_dev->fw_versions.fw_version_minor,
		 accel_dev->fw_versions.fw_version_patch);

	return ret;
}
EXPORT_SYMBOL_GPL(adf_gen4_send_admin_init);


int adf_gen4_get_uq_base_addr(struct adf_accel_dev *accel_dev,
			      void **uq_base_addr,
			      u32 bank_number)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct adf_bar *uq_bar = NULL;
	void *base_addr = NULL;

	if (!uq_base_addr || !hw_data || bank_number >= hw_data->num_banks)
		return -EINVAL;

	uq_bar = &GET_BARS(accel_dev)[hw_data->get_uq_bar_id(hw_data)];
	base_addr = (void *)(uq_bar->base_addr + ADF_GEN4_UQ_BASE
		+ bank_number * ADF_UQ_WINDOW_SIZE_GEN4
		+ ADF_UQ_OFFSET_UNPRIV_GEN4);

	*uq_base_addr = base_addr;
	return 0;
}
EXPORT_SYMBOL_GPL(adf_gen4_get_uq_base_addr);

int adf_gen4_ring_pair_reset(struct adf_accel_dev *accel_dev,
			     u32 bank_number)
{
	struct adf_hw_device_data *hw_data;
	struct adf_bar *etr_bar;
	void __iomem *csr;

	hw_data = accel_dev->hw_device;
	if (bank_number >= hw_data->num_banks)
		return -EINVAL;

	etr_bar = &GET_BARS(accel_dev)[hw_data->get_etr_bar_id(hw_data)];
	csr = etr_bar->virt_addr;

	dev_dbg(&GET_DEV(accel_dev), "ring pair reset for bank:%d\n",
		bank_number);
	if (gen4_ring_pair_reset(csr, bank_number)) {
		dev_err(&GET_DEV(accel_dev),
			"ring pair reset failure (timeout)\n");
		return -EFAULT;
	}

	dev_dbg(&GET_DEV(accel_dev), "ring pair reset successfully\n");
	return 0;
}
EXPORT_SYMBOL_GPL(adf_gen4_ring_pair_reset);

int adf_gen4_ring_pair_drain(struct adf_accel_dev *accel_dev,
			     u32 bank_number, int timeout_ms)
{
	struct adf_hw_device_data *hw_data;
	struct adf_bar *etr_bar;
	void __iomem *csr;

	hw_data = accel_dev->hw_device;
	if (bank_number >= hw_data->num_banks)
		return -EINVAL;

	etr_bar = &GET_BARS(accel_dev)[hw_data->get_etr_bar_id(hw_data)];
	csr = etr_bar->virt_addr;

	dev_dbg(&GET_DEV(accel_dev), "ring pair drain for bank:%d\n",
		bank_number);
	if (gen4_ring_pair_drain(csr, bank_number, timeout_ms)) {
		dev_err(&GET_DEV(accel_dev),
			"ring pair drain failure (timeout)\n");
		return -EFAULT;
	}

	dev_dbg(&GET_DEV(accel_dev), "ring pair drain successfully\n");
	return 0;
}
EXPORT_SYMBOL_GPL(adf_gen4_ring_pair_drain);

void adf_gen4_config_ring_irq(struct adf_accel_dev *accel_dev,
			      u32 bank_number, u16 ring_mask)
{
	struct adf_hw_device_data *hw_data;
	struct adf_etr_data *etr_data;
	struct adf_etr_bank_data *bank;
	struct adf_hw_csr_ops *csr_ops;
	u32 enable_int_col_mask;

	hw_data = accel_dev->hw_device;
	etr_data = accel_dev->transport;
	bank = &etr_data->banks[bank_number];
	csr_ops = &hw_data->csr_info.csr_ops;
	enable_int_col_mask = csr_ops->get_int_col_ctl_enable_mask();
	if (!(ring_mask & hw_data->tx_rings_mask)) {
		csr_ops->write_csr_int_srcsel(bank->csr_addr, bank->bank_number,
					      0, csr_ops->get_src_sel_mask());
		csr_ops->write_csr_int_col_ctl(bank->csr_addr,
					       bank->bank_number,
					       bank->irq_coalesc_timer |
					       enable_int_col_mask);
	}
}
EXPORT_SYMBOL_GPL(adf_gen4_config_ring_irq);

static int qat_asym_inst_config(struct adf_accel_dev *accel_dev, int inst,
				int bank)
{
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	unsigned long val = bank;
	struct adf_hw_device_data *hw_device = accel_dev->hw_device;
	unsigned long tx_rx_offset = hw_device->tx_rx_gap;

	snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_BANK_NUM_ASYM, inst);
	if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
					key, &val, ADF_DEC))
		goto err;

	snprintf(key, sizeof(key),
		 ADF_CY "%d" ADF_ETRMGR_CORE_AFFINITY, inst);
	if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
					key, &val, ADF_DEC))
		goto err;

	val = ADF_DEFAULT_ASYM_RING_SIZE;
	snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_ASYM_SIZE, inst);
	if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC, key,
					&val, ADF_DEC))
		goto err;

	val = 0;
	snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_ASYM_TX, inst);
	if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC, key,
					&val, ADF_DEC))
		goto err;

	val = tx_rx_offset;
	snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_ASYM_RX, inst);
	if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC, key,
					&val, ADF_DEC))
		goto err;

	val = hw_device->coalescing_def_time;
	snprintf(key, sizeof(key), ADF_ETRMGR_COALESCE_TIMER_FORMAT, bank);
	if (adf_cfg_add_key_value_param(accel_dev, "Accelerator0", key,
					&val, ADF_DEC))
		goto err;

	return 0;
err:
	return -EINVAL;
}

static int qat_sym_inst_config(struct adf_accel_dev *accel_dev, int inst,
			       int bank)
{
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	unsigned long val = bank;
	struct adf_hw_device_data *hw_device = accel_dev->hw_device;
	unsigned long tx_rx_offset = hw_device->tx_rx_gap;

	snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_BANK_NUM_SYM, inst);
	if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
					key, &val, ADF_DEC))
		goto err;

	snprintf(key, sizeof(key),
		 ADF_CY "%d" ADF_ETRMGR_CORE_AFFINITY, inst);
	if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
					key, &val, ADF_DEC))
		goto err;

	val = ADF_DEFAULT_SYM_RING_SIZE;
	snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_SYM_SIZE, inst);
	if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC, key,
					&val, ADF_DEC))
		goto err;

	val = 0;
	snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_SYM_TX, inst);
	if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC, key,
					&val, ADF_DEC))
		goto err;

	val = tx_rx_offset;
	snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_SYM_RX, inst);
	if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC, key,
					&val, ADF_DEC))
		goto err;

	val = hw_device->coalescing_def_time;
	snprintf(key, sizeof(key), ADF_ETRMGR_COALESCE_TIMER_FORMAT, bank);
	if (adf_cfg_add_key_value_param(accel_dev, "Accelerator0", key,
					&val, ADF_DEC))
		goto err;

	return 0;
err:
	return -EINVAL;
}

int adf_gen4_qat_crypto_dev_config(struct adf_accel_dev *accel_dev)
{
	int banks = GET_MAX_BANKS(accel_dev);
	u16 serv_type = 0;
	int bank = 0;
	unsigned long num_asym = 0;
	unsigned long num_sym = 0;
	unsigned long num_cy = 0;
	struct adf_hw_device_data *hw_device = accel_dev->hw_device;

	if (adf_cfg_section_add(accel_dev, ADF_KERNEL_SEC))
		goto err;
	if (adf_cfg_section_add(accel_dev, "Accelerator0"))
		goto err;

	while (bank < banks) {
		serv_type = GET_SRV_TYPE(hw_device->ring_to_svc_map,
					 bank % ADF_CFG_NUM_SERVICES);
		switch (serv_type) {
		case ASYM:
			if (qat_asym_inst_config(accel_dev, num_asym, bank))
				goto err;
			num_asym++;
			break;
		case SYM:
			if (qat_sym_inst_config(accel_dev, num_sym, bank))
				goto err;
			num_sym++;
			break;
		default:
			break;
		}
		bank++;
	}

	num_cy = max(num_asym, num_sym);
	if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
					ADF_NUM_CY, &num_cy, ADF_DEC))
		goto err;

	set_bit(ADF_STATUS_CONFIGURED, &accel_dev->status);
	return 0;
err:
	dev_err(&GET_DEV(accel_dev), "Failed to start QAT accel dev\n");
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(adf_gen4_qat_crypto_dev_config);

/**
 * adf_gen4_set_ssm_wdtimer() - Initialize the slice hang watchdog timer.
 * @accel_dev: Structure holding accelerator data
 *
 * Return: 0 on success, error code otherwise
 */
int adf_gen4_set_ssm_wdtimer(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_device = accel_dev->hw_device;
	struct adf_bar *misc_bar =
		&GET_BARS(accel_dev)[hw_device->get_misc_bar_id(hw_device)];
	void __iomem *csr = misc_bar->virt_addr;
	u32 wdt_val_h = (u32)(ADF_GEN4_SSM_WDT_64BIT_DEFAULT_VALUE >> 32);
	u32 wdt_val_l = (u32)ADF_GEN4_SSM_WDT_64BIT_DEFAULT_VALUE;
	u32 accel;

	/* Convert timer values from milliseconds to CPP clock cycles. */

	for (accel = 0; accel < hw_device->get_num_accels(hw_device); accel++) {
		/* Configures Slice Hang watchdogs */
		ADF_CSR_WR(csr, ADF_GEN4_SSMWDTL_OFFSET(accel), wdt_val_l);
		ADF_CSR_WR(csr, ADF_GEN4_SSMWDTH_OFFSET(accel), wdt_val_h);
		ADF_CSR_WR(csr, ADF_GEN4_SSMWDTPKEL_OFFSET(accel), wdt_val_l);
		ADF_CSR_WR(csr, ADF_GEN4_SSMWDTPKEH_OFFSET(accel), wdt_val_h);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(adf_gen4_set_ssm_wdtimer);

/*
 * The vector routing table is be used to select the MSI-X entry to use for
 * each interrupt source.
 * The first ADF_GEN4_ETR_MAX_BANKS entries correspond to ring interrupts.
 * The final entry corresponds to VM2PF or error interrupts.
 * This vector table could be used to configure one MSI-X entry to be shared
 * between multiple interrupt sources.
 *
 * The default routing is set to have a one to one correspondence between the
 * interrupt source and the MSI-X entry used.
 */
void adf_gen4_set_msix_default_rttable(struct adf_accel_dev *accel_dev)
{
	void __iomem *csr;
	int i;

	csr = (&GET_BARS(accel_dev)[ADF_GEN4_PMISC_BAR])->virt_addr;
	for (i = 0; i <= ADF_GEN4_ETR_MAX_BANKS; i++)
		ADF_CSR_WR(csr, ADF_4XXX_MSIX_RTTABLE_OFFSET(i), i);
}
EXPORT_SYMBOL_GPL(adf_gen4_set_msix_default_rttable);

uint32_t adf_gen4_get_obj_cfg_ae_mask(struct adf_accel_dev *accel_dev,
				      enum adf_accel_unit_services service)
{
	u32 ae_mask = 0;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u32 num_au = hw_data->get_num_accel_units(hw_data);
	struct adf_accel_unit *accel_unit = accel_dev->au_info->au;
	u32 i = 0;

	if (service == ADF_ACCEL_SERVICE_NULL)
		return 0;

	for (i = 0; i < num_au; i++) {
		if (accel_unit[i].services == service)
			ae_mask |= accel_unit[i].ae_mask;
	}

	return ae_mask;
}
EXPORT_SYMBOL_GPL(adf_gen4_get_obj_cfg_ae_mask);

enum adf_accel_unit_services
	adf_gen4_get_service_type(struct adf_accel_dev *accel_dev, s32 obj_num)
{
	struct adf_accel_unit *accel_unit;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u8 num_au = hw_data->get_num_accel_units(hw_data);
	int i;

	if (!hw_data->service_to_load_mask)
		return ADF_ACCEL_SERVICE_NULL;

	if (accel_dev->au_info && accel_dev->au_info->au)
		accel_unit = accel_dev->au_info->au;
	else
		return ADF_ACCEL_SERVICE_NULL;

	for (i = num_au - 2; i >= 0; i--) {
		if (hw_data->service_to_load_mask &
			accel_unit[i].services) {
			hw_data->service_to_load_mask &=
				~accel_unit[i].services;
			return accel_unit[i].services;
		}
	}

	/* admin AE should be loaded last */
	if (hw_data->service_to_load_mask & accel_unit[num_au - 1].services) {
		hw_data->service_to_load_mask &=
			~accel_unit[num_au - 1].services;
		return accel_unit[num_au - 1].services;
	}

	return ADF_ACCEL_SERVICE_NULL;
}
EXPORT_SYMBOL_GPL(adf_gen4_get_service_type);

void adf_gen4_cfg_get_accel_algo_cap(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u32 cipher_capabilities_mask = 0;
	u32 hash_capabilities_mask = 0;
	u32 accel_capabilities_mask = 0;
	u32 asym_capabilities_mask = 0;

	if (hw_data->get_accel_cap) {
		accel_capabilities_mask =
		hw_data->get_accel_cap(accel_dev);
	}

	if (accel_capabilities_mask & ICP_ACCEL_CAPABILITIES_CIPHER) {
		SET_BIT(cipher_capabilities_mask, ADF_CY_SYM_CIPHER_NULL);
#ifdef QAT_LEGACY_ALGORITHMS
		SET_BIT(cipher_capabilities_mask, ADF_CY_SYM_CIPHER_AES_ECB);
#endif
		SET_BIT(cipher_capabilities_mask, ADF_CY_SYM_CIPHER_AES_CBC);
		SET_BIT(cipher_capabilities_mask, ADF_CY_SYM_CIPHER_AES_CTR);
		SET_BIT(cipher_capabilities_mask, ADF_CY_SYM_CIPHER_AES_XTS);
	}

	if (accel_capabilities_mask & ICP_ACCEL_CAPABILITIES_AUTHENTICATION) {
#ifdef QAT_LEGACY_ALGORITHMS
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_SHA1);
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_SHA224);
#endif
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_SHA256);
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_SHA384);
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_SHA512);
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_AES_XCBC);
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_AES_CMAC);
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_AES_CBC_MAC);
	}

	if ((accel_capabilities_mask & ICP_ACCEL_CAPABILITIES_CIPHER) &&
	    (accel_capabilities_mask &
		   ICP_ACCEL_CAPABILITIES_AUTHENTICATION)) {
		SET_BIT(cipher_capabilities_mask, ADF_CY_SYM_CIPHER_AES_CCM);
		SET_BIT(cipher_capabilities_mask, ADF_CY_SYM_CIPHER_AES_GCM);
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_AES_CCM);
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_AES_GCM);
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_AES_GMAC);
	}

	if (accel_capabilities_mask & ICP_ACCEL_CAPABILITIES_SHA3)
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_SHA3_256);

	if (accel_capabilities_mask & ICP_ACCEL_CAPABILITIES_CHACHA_POLY) {
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_POLY);
		SET_BIT(cipher_capabilities_mask, ADF_CY_SYM_CIPHER_CHACHA);
	}

	if (accel_capabilities_mask & ICP_ACCEL_CAPABILITIES_ZUC) {
		SET_BIT(cipher_capabilities_mask, ADF_CY_SYM_CIPHER_ZUC_EEA3);
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_ZUC_EIA3);
	}

	if (accel_capabilities_mask & ICP_ACCEL_CAPABILITIES_SM3)
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_SM3);

	if (accel_capabilities_mask & ICP_ACCEL_CAPABILITIES_SHA3_EXT) {
#ifdef QAT_LEGACY_ALGORITHMS
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_SHA3_224);
#endif
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_SHA3_256);
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_SHA3_384);
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_SHA3_512);
	}

	if (accel_capabilities_mask & ICP_ACCEL_CAPABILITIES_SM4) {
#ifdef QAT_LEGACY_ALGORITHMS
		SET_BIT(cipher_capabilities_mask, ADF_CY_SYM_CIPHER_SM4_ECB);
#endif
		SET_BIT(cipher_capabilities_mask, ADF_CY_SYM_CIPHER_SM4_CBC);
		SET_BIT(cipher_capabilities_mask, ADF_CY_SYM_CIPHER_SM4_CTR);
	}

	if (accel_capabilities_mask &
		ICP_ACCEL_CAPABILITIES_WIRELESS_CRYPTO_EXT) {
#ifdef QAT_LEGACY_ALGORITHMS
		SET_BIT(cipher_capabilities_mask, ADF_CY_SYM_CIPHER_AES_F8);
#endif
		SET_BIT(cipher_capabilities_mask,
			ADF_CY_SYM_CIPHER_SNOW3G_UEA2);
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_SNOW3G_UIA2);
	}

	if (accel_capabilities_mask &
		ICP_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC) {
#ifdef QAT_LEGACY_ALGORITHMS
		SET_BIT(asym_capabilities_mask, ADF_CY_ASYM_DH);
		SET_BIT(asym_capabilities_mask, ADF_CY_ASYM_DSA);
#endif
		SET_BIT(asym_capabilities_mask, ADF_CY_ASYM_RSA);
		SET_BIT(asym_capabilities_mask, ADF_CY_ASYM_ECC);
		SET_BIT(asym_capabilities_mask, ADF_CY_ASYM_ECDH);
		SET_BIT(asym_capabilities_mask, ADF_CY_ASYM_ECDSA);
		SET_BIT(asym_capabilities_mask, ADF_CY_ASYM_KEY);
		SET_BIT(asym_capabilities_mask, ADF_CY_ASYM_LARGE_NUMBER);
		SET_BIT(asym_capabilities_mask, ADF_CY_ASYM_PRIME);
	}
#ifdef NON_GPL_COMMON
	hw_data->cipher_capabilities_mask = cipher_capabilities_mask;
	hw_data->hash_capabilities_mask = hash_capabilities_mask;
	hw_data->asym_capabilities_mask = asym_capabilities_mask;
#endif
}
EXPORT_SYMBOL_GPL(adf_gen4_cfg_get_accel_algo_cap);

u32 adf_gen4_get_slices_for_svc(struct adf_accel_dev *accel_dev,
				enum adf_svc_type svc)
{
	struct rl_slice_cnt *slices =
		&GET_HW_DATA(accel_dev)->rl_data.slice_cnt;
	u32 ret;

	switch (svc) {
	case ADF_SVC_ASYM:
		ret = slices->pke_slice_cnt;
		break;
	case ADF_SVC_SYM:
		/*
		 * For symmetric crypto, slice tokens are
		 * relative to the UCS slice.
		 */
		ret = slices->ucs_slice_cnt;
		break;
	case ADF_SVC_DC:
		ret = slices->dcpr_slice_cnt;
		break;
	default:
		ret = 0;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(adf_gen4_get_slices_for_svc);

u32 adf_gen4_get_num_svc_aes(struct adf_accel_dev *accel_dev,
			     enum adf_svc_type svc)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;

	return (hw_data->get_num_aes(hw_data) - 1);
}
EXPORT_SYMBOL_GPL(adf_gen4_get_num_svc_aes);
