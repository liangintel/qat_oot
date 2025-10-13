// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2019 - 2020 Intel Corporation */
#include <adf_accel_devices.h>
#include <adf_pf2vf_msg.h>
#include <adf_common_drv.h>
#include <adf_transport_access_macros_vqat.h>
#include <adf_vqat_hw_csr_data.h>
#include "adf_vqat_hw_data.h"
#include "icp_qat_hw.h"
#include "adf_transport_internal.h"

static struct adf_hw_device_class vqat_class = {
	.name = ADF_VQAT_DEVICE_NAME,
	.type = DEV_VQAT,
	.instances = 0
};

static void *adf_vqat_data_memcpy(void *dest, const void *src, size_t count)
{
	size_t cnt = count >> 3;
	u64 *pdest = (u64 *)dest;
	const u64 *psrc = (u64 *)src;

	/* copy every eight bytes */
	while (cnt) {
		*pdest++ = *psrc++;
		cnt--;
	}

	cnt = count % 8;

	if (cnt)
		memcpy(pdest, psrc, cnt);

	return dest;
}

int adf_vqat_get_cap(struct adf_accel_dev *accel_dev)
{
	struct adf_accel_pci *pci_info = &accel_dev->accel_pci_dev;
	void __iomem *ext_bar_addr;
	struct adf_vqat_data *vqat_data;
	struct adf_vdcm_vqat_cap_blk *cap_blk;
	struct adf_vqat_cap *cap;
	u32 kpt_len;
	int cap_num = 0;

	if (!accel_dev->hw_device->priv_data ||
	    !pci_info->pci_bars[ADF_VQAT_EXT_BAR].virt_addr)
		return -EINVAL;

	ext_bar_addr = pci_info->pci_bars[ADF_VQAT_EXT_BAR].virt_addr;
	vqat_data = accel_dev->hw_device->priv_data;
	memcpy(&vqat_data->cap_size, ext_bar_addr, sizeof(vqat_data->cap_size));

	if (!vqat_data->cap_data)
		vqat_data->cap_data = kzalloc(vqat_data->cap_size, GFP_KERNEL);

	if (unlikely(!vqat_data->cap_data))
		return -ENOMEM;

	adf_vqat_data_memcpy(vqat_data->cap_data, ext_bar_addr,
			     vqat_data->cap_size);
	cap_blk = vqat_data->cap_data;
	cap = cap_blk->head;

	while (cap_num++ < cap_blk->number) {
		switch (cap->id) {
		case ADF_VQAT_CAP_DEV_FREQ_ID:
			accel_dev->hw_device->clock_frequency = cap->data.value;
			break;
		case ADF_VQAT_CAP_SVC_MAP_ID:
			accel_dev->hw_device->ring_to_svc_map = cap->data.value;
			break;
		case ADF_VQAT_CAP_SVC_MASK_ID:
			accel_dev->hw_device->accel_capabilities_mask =
				cap->data.value;
			break;
		case ADF_VQAT_CAP_SVC_DC_EXT_ID:
			accel_dev->hw_device->extended_dc_capabilities =
				cap->data.value;
			break;
		case ADF_VQAT_CAP_SVC_KPT_CERT_ID:
			accel_dev->hw_device->kpt_issue_cert_len =
				cap->len - ADF_VQAT_CAP_HDR_SIZE;
			kpt_len = accel_dev->hw_device->kpt_issue_cert_len;

			if (!accel_dev->hw_device->kpt_issue_cert)
				accel_dev->hw_device->kpt_issue_cert =
					kzalloc(kpt_len, GFP_KERNEL);

			if (unlikely(!accel_dev->hw_device->kpt_issue_cert)) {
				kfree(vqat_data->cap_data);
				vqat_data->cap_data = NULL;
				return -ENOMEM;
			}

			memcpy(accel_dev->hw_device->kpt_issue_cert, &cap->data,
			       kpt_len);
			break;
#ifdef QAT_UIO
		case ADF_VQAT_CAP_SVC_SYM_HASH_ID:
			accel_dev->hw_device->hash_capabilities_mask =
				cap->data.value;
			break;
		case ADF_VQAT_CAP_SVC_SYM_CIPHER_ID:
			accel_dev->hw_device->cipher_capabilities_mask =
				cap->data.value;
			break;
		case ADF_VQAT_CAP_SVC_ASYM_ID:
			accel_dev->hw_device->asym_capabilities_mask =
				cap->data.value;
			break;
#endif
		default:
			pr_warn("%s: Capability id %d is not supported here\n",
				__func__, cap->id);
			break;
		}
		cap = (struct adf_vqat_cap *)((u8 *)cap + cap->len);
	}

	return 0;
}

#ifdef QAT_UIO
void adf_vqat_cfg_get_accel_algo_cap(struct adf_accel_dev *accel_dev)
{
#ifndef QAT_LEGACY_ALGORITHMS
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;

	CLEAR_BIT(hw_data->asym_capabilities_mask, ADF_CY_ASYM_DH);
	CLEAR_BIT(hw_data->asym_capabilities_mask, ADF_CY_ASYM_DSA);
	CLEAR_BIT(hw_data->cipher_capabilities_mask, ADF_CY_SYM_CIPHER_AES_ECB);
	CLEAR_BIT(hw_data->cipher_capabilities_mask, ADF_CY_SYM_CIPHER_SM4_ECB);
	CLEAR_BIT(hw_data->hash_capabilities_mask, ADF_CY_SYM_HASH_SHA1);
	CLEAR_BIT(hw_data->hash_capabilities_mask, ADF_CY_SYM_HASH_SHA224);
	CLEAR_BIT(hw_data->hash_capabilities_mask, ADF_CY_SYM_HASH_SHA3_224);
#endif
}
#endif

static u32 get_accel_mask(struct adf_accel_dev *accel_dev)
{
	return ADF_VQAT_ACCELERATORS_MASK;
}

static u32 get_ae_mask(struct adf_accel_dev *accel_dev)
{
	return ADF_VQAT_ACCELENGINES_MASK;
}

static u32 get_num_accels(struct adf_hw_device_data *self)
{
	return ADF_VQAT_MAX_ACCELERATORS;
}

static u32 get_num_aes(struct adf_hw_device_data *self)
{
	return ADF_VQAT_MAX_ACCELENGINES;
}

static u32 get_misc_bar_id(struct adf_hw_device_data *self)
{
	return ADF_VQAT_PMISC_BAR;
}

static u32 get_etr_bar_id(struct adf_hw_device_data *self)
{
	return ADF_VQAT_ETR_BAR;
}

static enum dev_sku_info get_sku(struct adf_hw_device_data *self)
{
	return DEV_SKU_VF;
}

static u32 get_pf2vm_offset(u32 i)
{
	pr_err("%s: You are not supposed to be here\n", __func__);
	return 0;
}

static u32 get_vm2pf_offset(u32 i)
{
	pr_err("%s: You are not supposed to be here\n", __func__);
	return 0;
}

static int adf_vqat_int_noop(struct adf_accel_dev *accel_dev)
{
	return 0;
}

static void adf_vqat_void_noop(struct adf_accel_dev *accel_dev)
{
}

static u32 vqat_get_hw_cap(struct adf_accel_dev *accel_dev)
{
	return accel_dev->hw_device->accel_capabilities_mask;
}

int adf_vqat_get_ring_to_svc_map(struct adf_accel_dev *accel_dev,
				 u16 *ring_to_svc_map)
{
	*ring_to_svc_map = accel_dev->hw_device->ring_to_svc_map;

	return 0;
}

static void get_ring_svc_map_data(int ring_pair_index, u16 ring_to_svc_map,
				  u8 *serv_type, int *ring_index,
				  int *num_rings_per_srv, int bank_num)
{
	*serv_type = GET_SRV_TYPE(ring_to_svc_map, bank_num %
			ADF_CFG_NUM_SERVICES);
	*ring_index = 0;
	*num_rings_per_srv = ADF_VQAT_NUM_RINGS_PER_BANK / 2;
}

#ifdef QAT_UIO
static void adf_set_asym_rings_mask(struct adf_accel_dev *accel_dev)
{
	accel_dev->hw_device->asym_rings_mask = ADF_VQAT_DEF_ASYM_MASK;
}

static int adf_vqat_ring_pair_reset(struct adf_accel_dev *accel_dev,
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
	if (vqat_ring_pair_reset(csr, bank_number)) {
		dev_err(&GET_DEV(accel_dev),
			"ring pair reset failure (timeout)\n");
		return -EFAULT;
	}

	dev_dbg(&GET_DEV(accel_dev), "ring pair reset successfully\n");

	return 0;
}
#endif
static void enable_pf2vm_interrupt(struct adf_accel_dev *accel_dev)
{
	struct adf_accel_pci *pci_info = &accel_dev->accel_pci_dev;
	void __iomem *pmisc_bar_addr =
		pci_info->pci_bars[ADF_VQAT_PMISC_BAR].virt_addr;

	ADF_CSR_WR(pmisc_bar_addr, ADF_VQAT_VINTSOU,
		   BIT(ADF_VQAT_VINT_PF2VM_OFFSET));
	ADF_CSR_WR(pmisc_bar_addr, ADF_VQAT_VINTMSK, 0x0);
}

static void disable_pf2vm_interrupt(struct adf_accel_dev *accel_dev)
{
	struct adf_accel_pci *pci_info = &accel_dev->accel_pci_dev;
	void __iomem *pmisc_bar_addr =
		pci_info->pci_bars[ADF_VQAT_PMISC_BAR].virt_addr;

	ADF_CSR_WR(pmisc_bar_addr, ADF_VQAT_VINTMSK,
		   BIT(ADF_VQAT_VINT_PF2VM_OFFSET));
}

static int interrupt_active_pf2vm(struct adf_accel_dev *accel_dev)
{
	struct adf_accel_pci *pci_info = &accel_dev->accel_pci_dev;
	void __iomem *pmisc_bar_addr =
		pci_info->pci_bars[ADF_VQAT_PMISC_BAR].virt_addr;
	u32 irq_sou, irq_msk;

	irq_sou = ADF_CSR_RD(pmisc_bar_addr, ADF_VQAT_VINTSOU);
	if (!irq_sou)
		return 0;

	irq_msk = ADF_CSR_RD(pmisc_bar_addr, ADF_VQAT_VINTMSK);
	return irq_sou & ~irq_msk & BIT(ADF_VQAT_VINT_PF2VM_OFFSET);
}

static int get_int_active_bundles(struct adf_accel_dev *accel_dev)
{
	return 1;
}

static int adf_vqat_capabilities_init(struct adf_accel_dev *accel_dev)
{
	return 0;
}

static void adf_vqat_config_ring_irq(struct adf_accel_dev *accel_dev,
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

void adf_init_hw_data_vqat(struct adf_hw_device_data *hw_data)
{
	struct adf_vqat_data *vqat_data;

	vqat_data = kzalloc(sizeof(*vqat_data), GFP_KERNEL);
	hw_data->dev_class = &vqat_class;
	hw_data->num_banks = ADF_VQAT_ETR_MAX_BANKS;
	hw_data->num_rings_per_bank = ADF_VQAT_NUM_RINGS_PER_BANK;
	hw_data->num_accel = ADF_VQAT_MAX_ACCELERATORS;
	hw_data->num_logical_accel = 1;
	hw_data->num_engines = ADF_VQAT_MAX_ACCELENGINES;
	hw_data->tx_rx_gap = ADF_VQAT_RX_RINGS_OFFSET;
	hw_data->tx_rings_mask = ADF_VQAT_TX_RINGS_MASK;
	hw_data->alloc_irq = adf_vqat_isr_resource_alloc;
	hw_data->free_irq = adf_vqat_isr_resource_free;
	hw_data->enable_error_correction = adf_vqat_void_noop;
	hw_data->init_admin_comms = adf_vqat_int_noop;
	hw_data->exit_admin_comms = adf_vqat_void_noop;
	hw_data->send_admin_init = adf_vqat2vdcm_init;
	hw_data->init_arb = adf_vqat_int_noop;
	hw_data->exit_arb = adf_vqat_void_noop;
	hw_data->disable_iov = adf_vqat2vdcm_shutdown;
	hw_data->get_accel_mask = get_accel_mask;
	hw_data->get_ae_mask = get_ae_mask;
	hw_data->get_num_accels = get_num_accels;
	hw_data->get_num_aes = get_num_aes;
	hw_data->get_etr_bar_id = get_etr_bar_id;
	hw_data->get_misc_bar_id = get_misc_bar_id;
	hw_data->get_pf2vf_offset = get_pf2vm_offset;
	hw_data->get_vf2pf_offset = get_vm2pf_offset;
	hw_data->get_sku = get_sku;
	hw_data->enable_ints = adf_vqat_void_noop;
	hw_data->enable_vf2pf_comms = adf_enable_vqat_iov;
	hw_data->disable_vf2pf_comms = adf_disable_vqat_iov;
	hw_data->min_iov_compat_ver = ADF_VQAT_COMPATIBILITY_VERSION;
	hw_data->get_accel_cap = vqat_get_hw_cap;
	hw_data->get_ring_svc_map_data = get_ring_svc_map_data;
	hw_data->reset_device = adf_reset_flr;
	hw_data->restore_device = adf_dev_restore;
#ifdef QAT_UIO
	hw_data->get_ring_to_svc_map = adf_vqat_get_ring_to_svc_map;
	hw_data->config_device = adf_config_device;
	hw_data->set_asym_rings_mask = adf_set_asym_rings_mask;
	hw_data->ring_pair_reset = adf_vqat_ring_pair_reset;
#endif
	hw_data->config_ring_irq = adf_vqat_config_ring_irq;
	hw_data->enable_pf2vf_interrupt = enable_pf2vm_interrupt;
	hw_data->disable_pf2vf_interrupt = disable_pf2vm_interrupt;
	hw_data->interrupt_active_pf2vf = interrupt_active_pf2vm;
	hw_data->get_int_active_bundles = get_int_active_bundles;
	hw_data->get_capabilities_ex = adf_vqat_capabilities_init;
	hw_data->dev_class->instances++;
	hw_data->priv_data = vqat_data;
	vqat_init_hw_csr_info(&hw_data->csr_info);
	hw_data->default_coalesce_timer = ADF_VQAT_ACCEL_DEF_COALESCE_TIMER;
	hw_data->coalescing_min_time = ADF_VQAT_COALESCING_MIN_TIME;
	hw_data->coalescing_max_time = ADF_VQAT_COALESCING_MAX_TIME;
	hw_data->coalescing_def_time = ADF_VQAT_COALESCING_DEF_TIME;
}

void adf_clean_hw_data_vqat(struct adf_hw_device_data *hw_data)
{
	struct adf_vqat_data *vqat_data;

	vqat_data = hw_data->priv_data;

	kfree(vqat_data->cap_data);
	kfree(vqat_data);
	vqat_data = NULL;
	kfree(hw_data->kpt_issue_cert);
	hw_data->kpt_issue_cert = NULL;

	hw_data->dev_class->instances--;
}
