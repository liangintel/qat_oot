// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2018 - 2021 Intel Corporation */
#include <adf_accel_devices.h>
#include <adf_pf2vf_msg.h>
#include <adf_cfg.h>
#include <adf_common_drv.h>
#include <adf_transport_access_macros_gen4.h>
#include <adf_gen4vf_hw_csr_data.h>
#include "adf_4xxxvf_hw_data.h"
#include "icp_qat_hw.h"
#include "adf_pasid.h"
#include "adf_transport_internal.h"
#include "adf_gen4_hw_data.h"

static struct adf_hw_device_class adf_4xxxiov_class = {
	.name = ADF_4XXXVF_DEVICE_NAME,
	.type = DEV_4XXXVF,
	.instances = 0
};

#define ADF_4XXXIOV_DEFAULT_RING_TO_SRV_MAP \
	(ASYM | SYM << ADF_CFG_SERV_RING_PAIR_1_SHIFT | \
	ASYM << ADF_CFG_SERV_RING_PAIR_2_SHIFT | \
	SYM << ADF_CFG_SERV_RING_PAIR_3_SHIFT)

#define ADF_4XXXIOV_ASYM_SYM ADF_4XXXIOV_DEFAULT_RING_TO_SRV_MAP

#define ADF_4XXXIOV_DC \
	(COMP | COMP << ADF_CFG_SERV_RING_PAIR_1_SHIFT | \
	COMP << ADF_CFG_SERV_RING_PAIR_2_SHIFT | \
	COMP << ADF_CFG_SERV_RING_PAIR_3_SHIFT)

#define ADF_4XXXIOV_SYM \
	(SYM | SYM << ADF_CFG_SERV_RING_PAIR_1_SHIFT | \
	SYM << ADF_CFG_SERV_RING_PAIR_2_SHIFT | \
	SYM << ADF_CFG_SERV_RING_PAIR_3_SHIFT)

#define ADF_4XXXIOV_ASYM \
	(ASYM | ASYM << ADF_CFG_SERV_RING_PAIR_1_SHIFT | \
	ASYM << ADF_CFG_SERV_RING_PAIR_2_SHIFT | \
	ASYM << ADF_CFG_SERV_RING_PAIR_3_SHIFT)

#define ADF_4XXXIOV_ASYM_DC \
	(ASYM | ASYM << ADF_CFG_SERV_RING_PAIR_1_SHIFT | \
	COMP << ADF_CFG_SERV_RING_PAIR_2_SHIFT | \
	COMP << ADF_CFG_SERV_RING_PAIR_3_SHIFT)

#define ADF_4XXXIOV_SYM_DC \
	(SYM | SYM << ADF_CFG_SERV_RING_PAIR_1_SHIFT | \
	COMP << ADF_CFG_SERV_RING_PAIR_2_SHIFT | \
	COMP << ADF_CFG_SERV_RING_PAIR_3_SHIFT)

#define ADF_4XXXIOV_NA \
	(NA | NA << ADF_CFG_SERV_RING_PAIR_1_SHIFT | \
	NA << ADF_CFG_SERV_RING_PAIR_2_SHIFT | \
	NA << ADF_CFG_SERV_RING_PAIR_3_SHIFT)

struct adf_enabled_services {
	const char svcs_enabled[ADF_CFG_MAX_VAL_LEN_IN_BYTES];
	u16 rng_to_svc_msk;
};

static struct adf_enabled_services adf_4xxxiov_svcs[] = {
	{"dc", ADF_4XXXIOV_DC},
	{"sym", ADF_4XXXIOV_SYM},
	{"asym", ADF_4XXXIOV_ASYM},
	{"dc;asym", ADF_4XXXIOV_ASYM_DC},
	{"asym;dc", ADF_4XXXIOV_ASYM_DC},
	{"sym;dc", ADF_4XXXIOV_SYM_DC},
	{"dc;sym", ADF_4XXXIOV_SYM_DC},
	{"asym;sym", ADF_4XXXIOV_ASYM_SYM},
	{"sym;asym", ADF_4XXXIOV_ASYM_SYM},
};

static u32 get_accel_mask(struct adf_accel_dev *accel_dev)
{
	return ADF_4XXXIOV_ACCELERATORS_MASK;
}

static u32 get_ae_mask(struct adf_accel_dev *accel_dev)
{
	return ADF_4XXXIOV_ACCELENGINES_MASK;
}

static u32 get_num_accels(struct adf_hw_device_data *self)
{
	return ADF_4XXXIOV_MAX_ACCELERATORS;
}

static u32 get_num_aes(struct adf_hw_device_data *self)
{
	return ADF_4XXXIOV_MAX_ACCELENGINES;
}

static u32 get_misc_bar_id(struct adf_hw_device_data *self)
{
	return ADF_4XXXIOV_PMISC_BAR;
}

static u32 get_etr_bar_id(struct adf_hw_device_data *self)
{
	return ADF_4XXXIOV_ETR_BAR;
}

static u32 get_uq_bar_id(struct adf_hw_device_data *self)
{
	return ADF_4XXXIOV_UQ_BAR;
}

static u32 get_clock_speed(struct adf_hw_device_data *self)
{
	/* CPP clock is half high-speed clock */
	return self->clock_frequency / 2;

}

static enum dev_sku_info get_sku(struct adf_hw_device_data *self)
{
	return DEV_SKU_VF;
}

static u32 get_pf2vm_offset(u32 i)
{
	return ADF_4XXXIOV_PF2VM_OFFSET;
}

static u32 get_vm2pf_offset(u32 i)
{
	return ADF_4XXXIOV_VM2PF_OFFSET;
}


static int adf_vf_int_noop(struct adf_accel_dev *accel_dev)
{
	return 0;
}

static void adf_vf_void_noop(struct adf_accel_dev *accel_dev)
{
}

u32 adf_4xxxvf_get_hw_cap(struct adf_accel_dev *accel_dev)
{
	struct pci_dev *pdev = accel_dev->accel_pci_dev.pci_dev;
	u32 vffusectl1;
	u32 capabilities_sym, capabilities_sym_cipher, capabilities_sym_auth,
		capabilities_asym, capabilities_dc;

	/* Get fused capabilities */
	pci_read_config_dword(pdev, ADF_4XXXIOV_VFFUSECTL1_OFFSET,
			      &vffusectl1);
	capabilities_sym_cipher = ICP_ACCEL_CAPABILITIES_HKDF |
				  ICP_ACCEL_CAPABILITIES_SM4 |
				  ICP_ACCEL_CAPABILITIES_CHACHA_POLY |
				  ICP_ACCEL_CAPABILITIES_AESGCM_SPC |
				  ICP_ACCEL_CAPABILITIES_AES_V2;
	capabilities_sym_auth = ICP_ACCEL_CAPABILITIES_SM3 |
				ICP_ACCEL_CAPABILITIES_SHA3 |
				ICP_ACCEL_CAPABILITIES_SHA3_EXT;

	/* A set bit in vffusectl1 means the feature is OFF in this SKU */
	if (vffusectl1 & ICP_ACCEL_4XXXVF_MASK_CIPHER_SLICE) {
		capabilities_sym_cipher &= ~ICP_ACCEL_CAPABILITIES_HKDF;
		capabilities_sym_cipher &= ~ICP_ACCEL_CAPABILITIES_SM4;
	}

	if (vffusectl1 & ICP_ACCEL_4XXXVF_MASK_UCS_SLICE) {
		capabilities_sym_cipher &= ~ICP_ACCEL_CAPABILITIES_CHACHA_POLY;
		capabilities_sym_cipher &= ~ICP_ACCEL_CAPABILITIES_AESGCM_SPC;
		capabilities_sym_cipher &= ~ICP_ACCEL_CAPABILITIES_AES_V2;
	}

	if (vffusectl1 & ICP_ACCEL_4XXXVF_MASK_AUTH_SLICE) {
		capabilities_sym_auth &= ~ICP_ACCEL_CAPABILITIES_SM3;
		capabilities_sym_auth &= ~ICP_ACCEL_CAPABILITIES_SHA3;
		capabilities_sym_auth &= ~ICP_ACCEL_CAPABILITIES_SHA3_EXT;
	}

	if (vffusectl1 & ICP_ACCEL_4XXXVF_MASK_SMX_SLICE) {
		capabilities_sym_cipher &= ~ICP_ACCEL_CAPABILITIES_SM4;
		capabilities_sym_auth &= ~ICP_ACCEL_CAPABILITIES_SM3;
	}

	if (capabilities_sym_cipher)
		capabilities_sym_cipher |= ICP_ACCEL_CAPABILITIES_CIPHER;

	if (capabilities_sym_auth)
		capabilities_sym_auth |= ICP_ACCEL_CAPABILITIES_AUTHENTICATION;

	capabilities_sym = capabilities_sym_cipher | capabilities_sym_auth;

	if (capabilities_sym)
		capabilities_sym |= ICP_ACCEL_CAPABILITIES_CRYPTO_SYMMETRIC;

	capabilities_asym = ICP_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC |
			  ICP_ACCEL_CAPABILITIES_SM2 |
			  ICP_ACCEL_CAPABILITIES_ECEDMONT;

	if (vffusectl1 & ICP_ACCEL_4XXXVF_MASK_PKE_SLICE) {
		capabilities_asym &= ~ICP_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC;
		capabilities_asym &= ~ICP_ACCEL_CAPABILITIES_SM2;
		capabilities_asym &= ~ICP_ACCEL_CAPABILITIES_ECEDMONT;
	}

	capabilities_dc = ICP_ACCEL_CAPABILITIES_COMPRESSION |
			  ICP_ACCEL_CAPABILITIES_LZ4_COMPRESSION |
			  ICP_ACCEL_CAPABILITIES_LZ4S_COMPRESSION |
			  ICP_ACCEL_CAPABILITIES_CNV_INTEGRITY64;

	if (vffusectl1 & ICP_ACCEL_4XXXVF_MASK_COMPRESS_SLICE) {
		capabilities_dc &= ~ICP_ACCEL_CAPABILITIES_COMPRESSION;
		capabilities_dc &= ~ICP_ACCEL_CAPABILITIES_LZ4_COMPRESSION;
		capabilities_dc &= ~ICP_ACCEL_CAPABILITIES_LZ4S_COMPRESSION;
		capabilities_dc &= ~ICP_ACCEL_CAPABILITIES_CNV_INTEGRITY64;
	}

	return capabilities_sym | capabilities_dc | capabilities_asym;
}

#ifdef QAT_UIO
static void adf_set_asym_rings_mask(struct adf_accel_dev *accel_dev)
{
	accel_dev->hw_device->asym_rings_mask = ADF_4XXX_DEF_ASYM_MASK;
}

#endif
static void enable_pf2vm_interrupt(struct adf_accel_dev *accel_dev)
{
	struct adf_accel_pci *pci_info = &accel_dev->accel_pci_dev;
	void __iomem *pmisc_bar_addr =
		pci_info->pci_bars[ADF_4XXXIOV_PMISC_BAR].virt_addr;

	ADF_CSR_WR(pmisc_bar_addr, ADF_4XXXIOV_VINTMSKPF2VM_OFFSET, 0x0);
}

static void disable_pf2vm_interrupt(struct adf_accel_dev *accel_dev)
{
	struct adf_accel_pci *pci_info = &accel_dev->accel_pci_dev;
	void __iomem *pmisc_bar_addr =
		pci_info->pci_bars[ADF_4XXXIOV_PMISC_BAR].virt_addr;

	ADF_CSR_WR(pmisc_bar_addr, ADF_4XXXIOV_VINTMSKPF2VM_OFFSET, BIT(0));
}

static int interrupt_active_pf2vm(struct adf_accel_dev *accel_dev)
{
	struct adf_accel_pci *pci_info = &accel_dev->accel_pci_dev;
	void __iomem *pmisc_bar_addr =
		pci_info->pci_bars[ADF_4XXXIOV_PMISC_BAR].virt_addr;
	u32 v_sou, v_msk;

	v_sou = ADF_CSR_RD(pmisc_bar_addr, ADF_4XXXIOV_VINTSOUPF2VM_OFFSET);
	v_msk = ADF_CSR_RD(pmisc_bar_addr, ADF_4XXXIOV_VINTMSKPF2VM_OFFSET);

	return ((v_sou & ~v_msk) & BIT(0)) ? 1 : 0;
}

static int get_int_active_bundles(struct adf_accel_dev *accel_dev)
{
	struct adf_accel_pci *pci_info = &accel_dev->accel_pci_dev;
	void __iomem *pmisc_bar_addr =
		pci_info->pci_bars[ADF_4XXXIOV_PMISC_BAR].virt_addr;
	u32 v_sou, v_msk;

	v_sou = ADF_CSR_RD(pmisc_bar_addr, ADF_4XXXIOV_VINTSOU_OFFSET);
	v_msk = ADF_CSR_RD(pmisc_bar_addr, ADF_4XXXIOV_VINTMSK_OFFSET);

	return v_sou & ~v_msk & 0xF;
}

static void get_ring_svc_map_data(int ring_pair_index, u16 ring_to_svc_map,
				  u8 *serv_type, int *ring_index,
				  int *num_rings_per_srv, int bank_num)
{
	*serv_type = GET_SRV_TYPE(ring_to_svc_map, bank_num %
			ADF_CFG_NUM_SERVICES);
	*ring_index = 0;
	*num_rings_per_srv = ADF_4XXXIOV_NUM_RINGS_PER_BANK / 2;
}

static int get_ring_to_svc_map(struct adf_accel_dev *accel_dev,
			       u16 *ring_to_svc_map)
{
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	char val[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	u32 i = 0;

	if (accel_dev->hw_device->get_ring_to_svc_done)
		return 0;

	/* Get the services enabled by user if provided.
	 * The function itself will also be called during the driver probe
	 * procedure where no ServicesEnable is provided. Then the device
	 * should still start with default configuration without
	 * ServicesEnable. Hence it still returns 0 when the
	 * adf_cfg_get_param_value() function returns failure.
	 */
	snprintf(key, sizeof(key), ADF_SERVICES_ENABLED);
	if (adf_cfg_get_param_value(accel_dev, ADF_GENERAL_SEC, key, val))
		return 0;

	for (i = 0; i < ARRAY_SIZE(adf_4xxxiov_svcs); i++) {
		if (!strncmp(val, adf_4xxxiov_svcs[i].svcs_enabled,
			     ADF_CFG_MAX_KEY_LEN_IN_BYTES)) {
			*ring_to_svc_map = adf_4xxxiov_svcs[i].rng_to_svc_msk;
			return 0;
		}
	}

	dev_err(&GET_DEV(accel_dev), "Invalid services enabled: %s\n", val);
	return -EFAULT;
}

static int adf_4xxxvf_ring_pair_reset(struct adf_accel_dev *accel_dev,
				   u32 bank_number)
{
	u32 msg_type = ADF_VF2PF_MSGTYPE_RP_RESET;
	u32 msg_data = bank_number;
	int ret = 0;

	if (bank_number >= accel_dev->hw_device->num_banks)
		return -EINVAL;

	dev_dbg(&GET_DEV(accel_dev), "ring pair reset for bank:%d\n",
		bank_number);
	mutex_lock(&accel_dev->vf.rpreset_lock);
	init_completion(&accel_dev->vf.iov_msg_completion);
	accel_dev->vf.rpreset_sts = RPRESET_SUCCESS;
	if (adf_iov_putmsg(accel_dev, msg_type, msg_data, 0)) {
		dev_err(&GET_DEV(accel_dev),
			"vf ring pair reset failure (vf2pf msg error)\n");
		ret = -EFAULT;
		goto out;
	}
	if (!wait_for_completion_timeout(&accel_dev->vf.iov_msg_completion,
					 5000)) {
		dev_err(&GET_DEV(accel_dev),
			"vf ring pair reset failure (pf2vf msg timeout)\n");
		ret = -EFAULT;
		goto out;
	}
	if (accel_dev->vf.rpreset_sts != RPRESET_SUCCESS) {
		dev_err(&GET_DEV(accel_dev),
			"vf ring pair reset failure (pf reports error)\n");
		ret = -EFAULT;
		goto out;
	}
	dev_dbg(&GET_DEV(accel_dev), "ring pair reset successfully\n");

out:
	mutex_unlock(&accel_dev->vf.rpreset_lock);
	return ret;
}

static void adf_4xxxvf_config_ring_irq(struct adf_accel_dev *accel_dev,
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

static int adf_4xxxvf_get_uq_base_addr(struct adf_accel_dev *accel_dev,
				       void **uq_base_addr,
				       u32 bank_number)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct adf_bar *uq_bar = NULL;
	void __iomem *base_addr = NULL;

	if (!uq_base_addr || !hw_data || bank_number >= hw_data->num_banks)
		return -EINVAL;

	uq_bar = &GET_BARS(accel_dev)[hw_data->get_uq_bar_id(hw_data)];
	base_addr = (void *)(uq_bar->base_addr + ADF_4XXXIOV_UQ_BASE +
		bank_number * ADF_UQ_WINDOW_SIZE_GEN4 +
		ADF_UQ_OFFSET_UNPRIV_GEN4);

	dev_dbg(&GET_DEV(accel_dev), "UQ base addr of bank[%d] is 0x%llx\n",
		bank_number, (u64)base_addr);

	*uq_base_addr = base_addr;
	return 0;
}

static int adf_4xxxvf_check_supported_services(struct adf_accel_dev *accel_dev)
{
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	char val[ADF_CFG_MAX_VAL_LEN_IN_BYTES];
	char *services_str = NULL;

	/* Check if extended DC capabilities enabled */
	if (accel_dev->chaining_enabled) {

		/* Get the services enabled by user */
		snprintf(key, sizeof(key), ADF_SERVICES_ENABLED);
		if (adf_cfg_get_param_value(accel_dev, ADF_GENERAL_SEC, key, val)) {
			dev_err(&GET_DEV(accel_dev), "Can't get %s\n",
				ADF_SERVICES_ENABLED);
			return -EFAULT;
		}
		services_str = val;

		/* For 4xxx device, only DC service allowed for DC Chaining mode */
		if (strncmp(services_str,
			     ADF_SERVICE_DC,
			     ADF_CFG_MAX_VAL_LEN_IN_BYTES))
			return -EFAULT;
	}
	return 0;
}

void adf_init_hw_data_4xxxiov(struct adf_hw_device_data *hw_data)
{
	hw_data->dev_class = &adf_4xxxiov_class;
	hw_data->instance_id = adf_4xxxiov_class.instances++;
	hw_data->num_banks = ADF_4XXXIOV_ETR_MAX_BANKS;
	hw_data->num_rings_per_bank = ADF_4XXXIOV_NUM_RINGS_PER_BANK;
	hw_data->num_accel = ADF_4XXXIOV_MAX_ACCELERATORS;
	hw_data->num_logical_accel = 1;
	hw_data->num_engines = ADF_4XXXIOV_MAX_ACCELENGINES;
	hw_data->tx_rx_gap = ADF_4XXXIOV_RX_RINGS_OFFSET;
	hw_data->tx_rings_mask = ADF_4XXXIOV_TX_RINGS_MASK;
	hw_data->alloc_irq = adf_vf_isr_resource_alloc;
	hw_data->free_irq = adf_vf_isr_resource_free;
	hw_data->enable_error_correction = adf_vf_void_noop;
	hw_data->init_admin_comms = adf_vf_int_noop;
	hw_data->exit_admin_comms = adf_vf_void_noop;
	hw_data->send_admin_init = adf_vf2pf_init;
	hw_data->init_arb = adf_vf_int_noop;
	hw_data->exit_arb = adf_vf_void_noop;
	hw_data->disable_iov = adf_vf2pf_shutdown;
	hw_data->get_accel_mask = get_accel_mask;
	hw_data->get_ae_mask = get_ae_mask;
	hw_data->get_num_accels = get_num_accels;
	hw_data->get_num_aes = get_num_aes;
	hw_data->get_etr_bar_id = get_etr_bar_id;
	hw_data->get_uq_bar_id = get_uq_bar_id;
	hw_data->get_misc_bar_id = get_misc_bar_id;
	hw_data->get_pf2vf_offset = get_pf2vm_offset;
	hw_data->get_vf2pf_offset = get_vm2pf_offset;
	hw_data->get_vf_rl_info = adf_get_vf_rl_info;
	hw_data->pfvf_type_shift = ADF_PFVF_2X_MSGTYPE_SHIFT;
	hw_data->pfvf_type_mask = ADF_PFVF_2X_MSGTYPE_MASK;
	hw_data->pfvf_data_shift = ADF_PFVF_2X_MSGDATA_SHIFT;
	hw_data->pfvf_data_mask = ADF_PFVF_2X_MSGDATA_MASK;
	hw_data->get_clock_speed = get_clock_speed;
	hw_data->get_sku = get_sku;
	hw_data->enable_ints = adf_vf_void_noop;
	hw_data->enable_vf2pf_comms = adf_enable_vf2pf_comms;
	hw_data->disable_vf2pf_comms = adf_disable_vf2pf_comms;
	hw_data->reset_device = adf_reset_flr;
	hw_data->restore_device = adf_dev_restore;
	hw_data->min_iov_compat_ver = ADF_PFVF_COMPATIBILITY_VERSION;
	hw_data->ring_to_svc_map = ADF_4XXXIOV_DEFAULT_RING_TO_SRV_MAP;
	hw_data->get_ring_svc_map_data = get_ring_svc_map_data;
	hw_data->get_ring_to_svc_map = get_ring_to_svc_map;
	hw_data->check_supported_services = adf_4xxxvf_check_supported_services;
	hw_data->get_accel_cap = adf_4xxxvf_get_hw_cap;
#ifdef QAT_UIO
	hw_data->config_device = adf_config_device;
	hw_data->set_asym_rings_mask = adf_set_asym_rings_mask;
#endif
#if defined(QAT_UIO) || defined(QAT_ESXI)
	hw_data->update_accel_cap_mask = update_accel_cap_mask;
#endif /* QAT_UIO || QAT_ESXI */
#ifdef NON_GPL_COMMON
	hw_data->get_accel_algo_cap = adf_gen4_cfg_get_accel_algo_cap;
#endif
	hw_data->ring_pair_reset = adf_4xxxvf_ring_pair_reset;
	hw_data->config_ring_irq = adf_4xxxvf_config_ring_irq;
	hw_data->enable_pf2vf_interrupt = enable_pf2vm_interrupt;
	hw_data->disable_pf2vf_interrupt = disable_pf2vm_interrupt;
	hw_data->interrupt_active_pf2vf = interrupt_active_pf2vm;
	hw_data->get_int_active_bundles = get_int_active_bundles;
	hw_data->config_bank_pasid = adf_pasid_config_bank;
	hw_data->get_uq_base_addr = adf_4xxxvf_get_uq_base_addr;
	gen4vf_init_hw_csr_info(&hw_data->csr_info);
	hw_data->default_coalesce_timer = ADF_4XXXIOV_ACCEL_DEF_COALESCE_TIMER;
	hw_data->coalescing_min_time = ADF_4XXXIOV_COALESCING_MIN_TIME;
	hw_data->coalescing_max_time = ADF_4XXXIOV_COALESCING_MAX_TIME;
	hw_data->coalescing_def_time = ADF_4XXXIOV_COALESCING_DEF_TIME;
}

void adf_clean_hw_data_4xxxiov(struct adf_hw_device_data *hw_data)
{
	hw_data->dev_class->instances--;
}
