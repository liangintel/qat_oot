// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2019 - 2020 Intel Corporation */
#include <linux/device.h>
#include <linux/io.h>
#include <linux/msi.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include "adf_accel_devices.h"
#include "adf_gen4_adi_hal.h"
#include "adf_transport_internal.h"
#include "adf_transport_access_macros_gen4.h"
#include "adf_adi.h"
#include "adf_vdcm.h"

#define ADI_RESET_TIMEOUT_MS		5000
#define ADI_RESET_POLLING_INTERVAL	20
#define ADI_MMAP_SIZE	0x2000
#define GEN4_CSR_OFFSET(bank, ofs) \
	(ADF_RING_CSR_ADDR_OFFSET_GEN4 + \
	(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
	(ofs))

static int hal_rp_set_pasid(struct adf_adi_ep *adi, int pasid);

static int hal_rp_init(struct adf_adi_ep *adi)
{
	struct adf_accel_dev *accel_dev = NULL;
	struct adf_bar *etr_bar = NULL;
	struct adf_bar *misc_bar = NULL;
	struct adi_priv_data *priv = NULL;
	struct adf_hw_device_data *hw_data = NULL;
#ifndef CONFIG_SIOV_IMS_SUPPORT
	int i;
#endif

	if (!adi || !adi->parent || !adi->parent->hw_device)
		return -EINVAL;

	accel_dev = adi->parent;
	hw_data = adi->parent->hw_device;

	if (adi->hw_priv) {
		dev_err(&GET_DEV(accel_dev),
			"Init RP%d but it is already initialized.\n",
			adi->bank_idx);
		return -EEXIST;
	}

	etr_bar = &GET_BARS(accel_dev)[hw_data->get_etr_bar_id(hw_data)];
	if (!etr_bar)
		return -ENOMEM;

	misc_bar = &GET_BARS(accel_dev)[hw_data->get_misc_bar_id(hw_data)];
	if (!misc_bar)
		return -ENOMEM;

#ifndef CONFIG_SIOV_IMS_SUPPORT
	/* Check if MSIx meets requirement:
	 * 1:1 mapping in irq routing table is required for adi->bank_idx.
	 * We don't allow user to change this mapping
	 * unless restarting device
	 **/
	if (ADF_CSR_RD(misc_bar->virt_addr,
		       ADF_4XXX_MSIX_RTTABLE_OFFSET(adi->bank_idx)) !=
	    adi->bank_idx) {
		dev_err(&GET_DEV(accel_dev),
			"MSIx routing table check failure for adi%d!\n",
			adi->bank_idx);
		return -EFAULT;
	}
	for (i = 0; i <= hw_data->num_banks; i++) {
		if (i == adi->bank_idx)
			continue;
		if (ADF_CSR_RD(misc_bar->virt_addr,
			       ADF_4XXX_MSIX_RTTABLE_OFFSET(i)) ==
		    adi->bank_idx) {
			dev_err(&GET_DEV(accel_dev),
				"Failed to check MSIx routing table for adi%d!\n",
				adi->bank_idx);

			return -EFAULT;
		}
	}
#endif
	priv = kzalloc(sizeof(*priv), GFP_KERNEL);

	if (!priv)
		return -ENOMEM;

	adi->hw_priv = priv;
	priv->etr_bar = etr_bar;
	priv->misc_bar = misc_bar;
	priv->etr_mmap.phy_addr =
		etr_bar->base_addr + ADF_WQM_CSR_WQ_BASE(adi->bank_idx);
	priv->etr_mmap.virt_addr =
		etr_bar->virt_addr + ADF_WQM_CSR_WQ_BASE(adi->bank_idx);
	priv->etr_mmap.size = ADI_MMAP_SIZE;
	dev_dbg(&GET_DEV(accel_dev),
		"%s (bank=%d) mmap virt addr: 0x%llx, phy addr: 0x%llx, size=0x%08x.\n",
		adi->name, adi->bank_idx,
		(u64)priv->etr_mmap.virt_addr,
		priv->etr_mmap.phy_addr,
		(int)priv->etr_mmap.size);

	return 0;
}

static void hal_rp_destroy(struct adf_adi_ep *adi)
{
	if (adi && adi->hw_priv) {
		kfree(adi->hw_priv);
		adi->hw_priv = NULL;
	}
}

static int gen4_adi_rp_toggle(struct adf_adi_ep *adi, bool enable)
{
	struct adi_priv_data *priv;
	struct adf_bar *etr;

	if (!adi || !adi->parent || !adi->hw_priv)
		return -EINVAL;

	priv = adi->hw_priv;

	if (!priv->etr_bar)
		return -EINVAL;

	etr = priv->etr_bar;
	/* Configure WQ for ADI mode */
	if (enable)
		adf_csr_fetch_and_or(etr->virt_addr,
				     ADF_WQM_CSR_PASIDCTL(adi->bank_idx),
				     ADF_PASIDCTL_MASK_ADIMODE);
	else
		adf_csr_fetch_and_and(etr->virt_addr,
				      ADF_WQM_CSR_PASIDCTL(adi->bank_idx),
				      ~ADF_PASIDCTL_MASK_ADIMODE);
	return 0;
}

static int hal_rp_enable(struct adf_adi_ep *adi)
{
	return gen4_adi_rp_toggle(adi, true);
}

static int hal_rp_disable(struct adf_adi_ep *adi)
{
	return gen4_adi_rp_toggle(adi, false);
}

static int hal_rp_reset(struct adf_adi_ep *adi, bool restore_pasid)
{
	struct adf_accel_dev *accel_dev;
	struct adf_hw_device_data *hw_data;
	int ret;

	if (!adi || !adi->parent || !adi->parent->hw_device)
		return -EINVAL;

	accel_dev = adi->parent;
	hw_data = accel_dev->hw_device;

	if (!hw_data->ring_pair_reset)
		return -EFAULT;

	ret = hw_data->ring_pair_reset(accel_dev, adi->bank_idx);

	if (!ret && adi->pasid > 0 && restore_pasid)
		ret = hal_rp_set_pasid(adi, adi->pasid);

	return ret;
}

static unsigned int hal_rp_irq_enable(struct adf_adi_ep *adi)
{
	struct adf_accel_dev *accel_dev = NULL;
	struct adi_priv_data *priv = NULL;
	struct adf_bar *pmisc = NULL;
	void __iomem *pmisc_addr = NULL;

	if (!adi || !adi->parent || !adi->hw_priv)
		return 0;

	accel_dev = adi->parent;
	priv = (struct adi_priv_data *)adi->hw_priv;

	dev_dbg(&GET_DEV(accel_dev), "%s: unmask irq for adi %d\n",
		__func__, adi->adi_idx);

	if (!priv->misc_bar)
		return 0;

	pmisc = priv->misc_bar;
	pmisc_addr = pmisc->virt_addr;

#if defined CONFIG_SIOV_IMS_SUPPORT
	ADF_CSR_WR(pmisc_addr,
		   ADF_WQM_CSR_IMSMSIXVECCNTL(adi->bank_idx),
		   0);
#else
	ADF_CSR_WR(pmisc_addr,
		   ADF_WQM_CSR_MSIXVECCNTL(adi->bank_idx),
		   0);
#endif
	ADF_CSR_WR(pmisc_addr,
		   ADF_WQM_CSR_RPINTMSK(adi->bank_idx),
		   0);

	dev_dbg(&GET_DEV(accel_dev), "%s: unmask irq for adi %d completes\n",
		__func__, adi->adi_idx);

	return 0;
}

static unsigned int hal_rp_irq_disable(struct adf_adi_ep *adi)
{
	struct adf_accel_dev *accel_dev = NULL;
	struct adi_priv_data *priv = NULL;
	struct adf_bar *pmisc = NULL;
	void __iomem *pmisc_addr = NULL;

	if (!adi || !adi->parent || !adi->hw_priv)
		return 0;

	accel_dev = adi->parent;
	priv = (struct adi_priv_data *)adi->hw_priv;

	if (!priv->misc_bar)
		return 0;

	pmisc = priv->misc_bar;
	pmisc_addr = pmisc->virt_addr;

	dev_dbg(&GET_DEV(accel_dev), "%s: mask irq for adi %d\n",
		__func__, adi->adi_idx);

#if defined CONFIG_SIOV_IMS_SUPPORT
	ADF_CSR_WR(pmisc_addr,
		   ADF_WQM_CSR_IMSMSIXVECCNTL(adi->bank_idx),
		   PCI_MSIX_ENTRY_CTRL_MASKBIT);
#else
	ADF_CSR_WR(pmisc_addr,
		   ADF_WQM_CSR_MSIXVECCNTL(adi->bank_idx),
		   PCI_MSIX_ENTRY_CTRL_MASKBIT);
#endif
	ADF_CSR_WR(pmisc_addr,
		   ADF_WQM_CSR_RPINTMSK(adi->bank_idx),
		   ADF_WQM_CSR_RPINT_MASK);

	return PCI_MSIX_ENTRY_CTRL_MASKBIT;
}

static int hal_rp_irq_write_msg(struct adf_adi_ep *adi, struct msi_msg *msg)
{
	struct adf_accel_dev *accel_dev = NULL;
	struct adi_priv_data *priv = NULL;
	struct adf_bar *pmisc = NULL;
	void __iomem *pmisc_addr = NULL;

	if (!adi || !adi->parent || !adi->hw_priv)
		return -EINVAL;

	accel_dev = adi->parent;
	priv = (struct adi_priv_data *)adi->hw_priv;

	if (!priv->misc_bar)
		return -EINVAL;

	pmisc = priv->misc_bar;
	pmisc_addr = pmisc->virt_addr;

	dev_dbg(&GET_DEV(accel_dev),
		"%s: write msi message for adi %d\n",
		__func__, adi->adi_idx);

	ADF_CSR_WR(pmisc_addr, ADF_WQM_CSR_IMSMSIXLTBL(adi->bank_idx),
		   msg->address_lo);
	ADF_CSR_WR(pmisc_addr, ADF_WQM_CSR_IMSMSIXUTBL(adi->bank_idx),
		   msg->address_hi);
	ADF_CSR_WR(pmisc_addr, ADF_WQM_CSR_IMSMSIXDATA(adi->bank_idx),
		   msg->data);
	return 0;
}

static int hal_rp_get_mmio(struct adf_adi_ep *adi,
			   struct adi_mmio_info *mmio_info)
{
	struct adi_priv_data *priv;

	if (!adi || !adi->parent || !adi->hw_priv ||
	    !mmio_info)
		return -EINVAL;

	priv = (struct adi_priv_data *)adi->hw_priv;
	memcpy(mmio_info, &priv->etr_mmap, sizeof(*mmio_info));

	return 0;
}

static int hal_rp_set_pasid(struct adf_adi_ep *adi, int pasid)
{
	u32 val;
	struct adi_priv_data *priv;
	struct adf_bar *etr;

	if (!adi || !adi->parent || !adi->hw_priv)
		return -EINVAL;

	priv = adi->hw_priv;

	if (!priv->etr_bar)
		return -EINVAL;

	etr = priv->etr_bar;
	val = ADF_CSR_RD(etr->virt_addr, ADF_WQM_CSR_PASIDCTL(adi->bank_idx));
	val &= ~ADF_PASIDCTL_PASID_MASK;
	val |= (pasid & ADF_PASIDCTL_PASID_MASK);

	ADF_CSR_WR(etr->virt_addr, ADF_WQM_CSR_PASIDCTL(adi->bank_idx),
		   val | ADF_PASIDCTL_MASK_ADIMODE);
	adi->pasid = pasid;

	return 0;
}

static int hal_rp_get_pasid(struct adf_adi_ep *adi)
{
	if (adi)
		return adi->pasid;
	return -1;
}

static int gen4_adi_mmio_rw32(struct adf_adi_ep *adi, u64 pos,
			      void *buf, unsigned int len, bool is_write)
{
	struct adf_accel_dev *accel_dev;
	struct adi_priv_data *priv;
	void __iomem *base_addr;
	u32 offset = 0;
	u32 pasid_val;

	if (!adi || !adi->parent || !adi->hw_priv || !buf)
		return -EINVAL;

	accel_dev = adi->parent;
	priv = adi->hw_priv;

	dev_dbg(&GET_DEV(accel_dev),
		"%s: ETR %s %d bytes at vreg offset@0x%llx\n",
		__func__, is_write ? "write" : "read", len, pos);

	if (len != 4)
		return -EINVAL;

	if (!priv->etr_bar || !priv->etr_bar->virt_addr)
		return -EINVAL;

	base_addr = priv->etr_bar->virt_addr;
	switch (pos) {
	case ADF_VQAT_R0_CONFIG:
		offset = GEN4_CSR_OFFSET(adi->bank_idx,
					 ADF_RING_CSR_RING_CONFIG_GEN4);
		break;
	case ADF_VQAT_R1_CONFIG:
		offset = GEN4_CSR_OFFSET(adi->bank_idx,
					 ADF_RING_CSR_RING_CONFIG_GEN4 + 4);
		break;
	case ADF_VQAT_R0_LBASE:
		offset = GEN4_CSR_OFFSET(adi->bank_idx,
					 ADF_RING_CSR_RING_LBASE_GEN4);
		break;
	case ADF_VQAT_R1_LBASE:
		offset = GEN4_CSR_OFFSET(adi->bank_idx,
					 ADF_RING_CSR_RING_LBASE_GEN4 + 4);
		break;
	case ADF_VQAT_R0_UBASE:
		offset = GEN4_CSR_OFFSET(adi->bank_idx,
					 ADF_RING_CSR_RING_UBASE_GEN4);
		break;
	case ADF_VQAT_R1_UBASE:
		offset = GEN4_CSR_OFFSET(adi->bank_idx,
					 ADF_RING_CSR_RING_UBASE_GEN4 + 4);
		break;
	case ADF_VQAT_R0_HEAD:
		offset = GEN4_CSR_OFFSET(adi->bank_idx,
					 ADF_RING_CSR_RING_HEAD);
		break;
	case ADF_VQAT_R1_HEAD:
		offset = GEN4_CSR_OFFSET(adi->bank_idx,
					 ADF_RING_CSR_RING_HEAD + 4);
		break;
	case ADF_VQAT_R0_TAIL:
		offset = GEN4_CSR_OFFSET(adi->bank_idx,
					 ADF_RING_CSR_RING_TAIL);
		break;
	case ADF_VQAT_R1_TAIL:
		offset = GEN4_CSR_OFFSET(adi->bank_idx,
					 ADF_RING_CSR_RING_TAIL + 4);
		break;
	case ADF_VQAT_RPRESETCTL:
		offset = ADF_WQM_CSR_RPRESETCTL(adi->bank_idx);

		/* TODO: we need to translate guest pasid value to host pasid
		 * value when rpresetctl register is enabled for pasid level
		 * reset
		 */
		if (is_write) {
			if (ADF_GET_RPRESETCTL_VALUE(*(u32 *)buf) ==
			    PASID_LEVEL_RESET) {
				dev_dbg(&GET_DEV(accel_dev),
					"%s: unsupported pasid level reset\n",
					__func__);
				return -EINVAL;
			}

			if (adi->reset_complete) {
				if (ADF_GET_RPRESETCTL_VALUE(*(u32 *)buf) ==
				    RING_LEVEL_RESET)
					adi->reset_complete = false;
			} else {
				if (ADF_GET_RPRESETCTL_VALUE(*(u32 *)buf) ==
				    RING_LEVEL_ABORT)
					break;
				return -EINVAL;
			}
		}
		break;
	case ADF_VQAT_RPRESETSTS:
		offset = ADF_WQM_CSR_RPRESETSTS(adi->bank_idx);

		/* Assume rp reset complete when rpresetsts register
		 * bit#0 is set and write rpresetsts register bit#0 as 1
		 */
		if (is_write) {
			if (ADF_CSR_RD(base_addr, offset) &
			    ADF_WQM_CSR_RPRESETSTS_MASK &&
			    *(u32 *)buf == ADF_WQM_CSR_RPRESETSTS_MASK)
				adi->reset_complete = true;
		}
		break;
	default:
		dev_dbg(&GET_DEV(accel_dev),
			"%s: unsupported register access on adi %d\n",
			__func__, adi->adi_idx);
		return -EINVAL;
	}

	dev_dbg(&GET_DEV(accel_dev),
		"%s: base_addr = %llx, translated offset = 0x%x\n",
		__func__, (u64)base_addr, offset);

	if (is_write)
		ADF_CSR_WR(base_addr, offset, *(u32 *)buf);
	else
		*(u32 *)buf = ADF_CSR_RD(base_addr, offset);

	/* After rpresetsts bit#0 is set, restore pasid value */
	if (is_write && pos == ADF_VQAT_RPRESETSTS) {
		pasid_val = ADF_CSR_RD(base_addr,
				       ADF_WQM_CSR_PASIDCTL(adi->bank_idx));

		if (!ADF_GET_PASIDCTL_PASID_VALUE(pasid_val) && adi->pasid > 0)
			if (hal_rp_set_pasid(adi, adi->pasid))
				return -EINVAL;
	}

	return (int)len;
}

static int hal_vreg_write(struct adf_adi_ep *adi,
			  u64 pos, void *buf, unsigned int len)
{
	if (!adi || !buf || !len)
		return -EINVAL;

	return gen4_adi_mmio_rw32(adi, pos, buf, len, true);
}

static int hal_vreg_read(struct adf_adi_ep *adi,
			 u64 pos, void *buf, unsigned int len)
{
	if (!adi || !buf || !len)
		return -EINVAL;

	return gen4_adi_mmio_rw32(adi, pos, buf, len, false);
}

static struct adf_adi_ops gen4_adi_ops = {
	.init = hal_rp_init,
	.destroy = hal_rp_destroy,
	.enable = hal_rp_enable,
	.disable = hal_rp_disable,
	.reset = hal_rp_reset,
	.irq_enable = hal_rp_irq_enable,
	.irq_disable = hal_rp_irq_disable,
	.irq_write_msi_msg = hal_rp_irq_write_msg,
	.set_pasid = hal_rp_set_pasid,
	.get_pasid = hal_rp_get_pasid,
	.get_mmio_info = hal_rp_get_mmio,
	.vreg_write = hal_vreg_write,
	.vreg_read = hal_vreg_read,
};

void gen4_init_adi_ops(struct adf_adi_ops **adi_ops)
{
	if (adi_ops)
		*adi_ops = &gen4_adi_ops;
}
EXPORT_SYMBOL_GPL(gen4_init_adi_ops);
