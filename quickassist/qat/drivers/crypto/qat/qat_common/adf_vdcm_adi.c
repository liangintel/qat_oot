// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2019 - 2021 Intel Corporation */

#include <linux/device.h>
#include <linux/uuid.h>
#include <linux/mdev.h>
#include <linux/iommu.h>
#include <linux/io.h>
#include <linux/msi.h>
#include <linux/version.h>
#include <linux/eventfd.h>
#include <linux/delay.h>
#if (KERNEL_VERSION(5, 11, 0) <= LINUX_VERSION_CODE)
#include <linux/vfio.h>
#endif
#include "adf_common_drv.h"
#include "adf_accel_devices.h"
#include "adf_transport_internal.h"
#include "adf_vdcm.h"
#include "qat_crypto.h"
#include "adf_adi.h"

#define ADI_IDX(adi) ((adi)->bank_idx)

static struct adf_vdcm_obj_mgr *adf_vqat_adi_class_parent_mgr;

static int adf_vdcm_pcie_rom_bar_hdl(struct adf_vdcm_vqat *vqat,
				     bool is_write, u32 offset,
				     void *data, u32 len,
				     void *reg_addr)
{
	union pcie_reg *reg = reg_addr;

	dev_dbg(mdev_dev(vqat->mdev), "Doesn't support expansion ROM BAR accesses\n");
	reg->val = 0;

	return len;
}

static int adf_vdcm_pcie_bar_base_addr_hdl(struct adf_vdcm_vqat *vqat,
					   bool is_write, u32 offset,
					   void *data, u32 len,
					   void *reg_addr)
{
	int ret = 0;
	union pcie_reg *reg = reg_addr;
	u32 bar_size = 0;
	u32 bar_attr = reg->val & 0xf;
	u32 base_addr = offset & ~0x3U;

	switch (base_addr) {
	case PCI_BASE_ADDRESS_0:
		bar_size = vqat->bar[ADF_VQAT_ETR_BAR].size;
		break;
	case PCI_BASE_ADDRESS_2:
		bar_size = vqat->bar[ADF_VQAT_PMISC_BAR].size;
		break;
	case PCI_BASE_ADDRESS_4:
		bar_size = vqat->bar[ADF_VQAT_EXT_BAR].size;
		break;
	default:
		pr_warn("%s: Invalid %d base address for bar handler\n",
			__func__, base_addr);
		ret = -EINVAL;
	}

	reg->val &= (~bar_size + 1);
	reg->val |= bar_attr;

	return ret == 0 ? len : 0;
}

static int adf_vdcm_pcie_ctl_sta_hdl(struct adf_vdcm_vqat *vqat,
				     bool is_write, u32 offset, void *data,
				     u32 len, void *reg_addr)
{
	int ret = 0;
	union pcie_reg *reg = reg_addr;

	if (reg->val & PCI_EXP_DEVCTL_BCR_FLR) {
		ret = vqat->ops->reset(vqat);
		reg->val &= ~PCI_EXP_DEVCTL_BCR_FLR;
	}

	return ret == 0 ? len : 0;
}

static int adf_vdcm_adi_vqat_do_reset(struct adf_vdcm_vqat *vqat,
				      bool restore_pasid)
{
	/* ADI reset */
	struct adf_adi_ep *adi = vqat->hw_priv;

	if (adi->adi_ops->reset)
		adi->adi_ops->reset(adi, restore_pasid);
	/* reset vQAT */
	adf_vdcm_vqat_reset_config(vqat);
	/* Reset the emulation part of the bars */
	adf_vdcm_vqat_msgq_reset(&vqat->iov_msgq);
	vqat->vintsrc = 0;
	vqat->vintmsk = 0;

	return 0;
}

#if (defined CONFIG_SIOV_IMS_SUPPORT || defined CONFIG_SIOV_MSIX)
static void adf_vqat_set_adi_irq(struct adf_vqat_irq_ctx *ctx,
				 enum adf_vqat_irq_op irq_op)
{
	struct adf_vdcm_vqat *vqat = ctx->data;
	struct adf_adi_ep *adi = (struct adf_adi_ep *)(vqat->hw_priv);

	if (irq_op == ADF_VQAT_IRQ_ENABLE)
		adi->adi_ops->irq_enable(adi);
	else
		adi->adi_ops->irq_disable(adi);
	adf_vdcm_set_vqat_msix_vector(vqat, ADF_VQAT_RING_IRQ, irq_op);
}

#ifdef CONFIG_SIOV_IMS_SUPPORT
#if (KERNEL_VERSION(5, 6, 0) == LINUX_VERSION_CODE)
static unsigned int adf_ims_irq_mask(struct msi_desc *desc)
{
	u32 mask_bits = desc->platform.masked;
	struct device *dev = desc->dev;
	struct mdev_device *mdev = mdev_from_dev(dev);
	struct adf_vdcm_vqat *vqat = mdev_get_drvdata(mdev);
	struct adf_adi_ep *adi = vqat->hw_priv;

	dev_info(dev, "%s: mask irq for adi %d\n", __func__, ADI_IDX(adi));

	if (adi->adi_ops->irq_disable)
		return adi->adi_ops->irq_disable(adi);

	return mask_bits;
}

static unsigned int adf_ims_irq_unmask(struct msi_desc *desc)
{
	u32 mask_bits = desc->platform.masked;
	struct device *dev = desc->dev;
	struct mdev_device *mdev = mdev_from_dev(dev);
	struct adf_vdcm_vqat *vqat = mdev_get_drvdata(mdev);
	struct adf_adi_ep *adi = vqat->hw_priv;

	dev_info(dev, "%s: unmask irq for adi %d\n", __func__, ADI_IDX(adi));

	if (adi->adi_ops->irq_enable)
		return adi->adi_ops->irq_enable(adi);

	return mask_bits;
}
#elif (KERNEL_VERSION(5, 8, 0) == LINUX_VERSION_CODE)
static void adf_ims_irq_mask(struct msi_desc *desc)
{
	struct device *dev = desc->dev;
	struct mdev_device *mdev = mdev_from_dev(dev);
	struct adf_vdcm_vqat *vqat = mdev_get_drvdata(mdev);
	struct adf_adi_ep *adi = vqat->hw_priv;

	if (adi->adi_ops->irq_disable)
		adi->adi_ops->irq_disable(adi);
}

static void adf_ims_irq_unmask(struct msi_desc *desc)
{
	struct device *dev = desc->dev;
	struct mdev_device *mdev = mdev_from_dev(dev);
	struct adf_vdcm_vqat *vqat = mdev_get_drvdata(mdev);
	struct adf_adi_ep *adi = vqat->hw_priv;

	if (adi->adi_ops->irq_enable)
		adi->adi_ops->irq_enable(adi);
}
#endif

static void adf_ims_write_msg(struct msi_desc *desc, struct msi_msg *msg)
{
	int ims_index = desc->platform.msi_index;
	struct device *dev = desc->dev;
	struct mdev_device *mdev = mdev_from_dev(dev);
	struct adf_vdcm_vqat *vqat = mdev_get_drvdata(mdev);
	struct adf_adi_ep *adi = vqat->hw_priv;

	dev_info(dev, "%s: write msi message for adi %d,(index=%d,laddr=0x%x,haddr=0x%x,data=0x%x\n",
		 __func__, ADI_IDX(adi), ims_index,
		 msg->address_lo, msg->address_hi, msg->data);

	if (adi->adi_ops->irq_write_msi_msg)
		adi->adi_ops->irq_write_msi_msg(adi, msg);
}

struct platform_msi_ops adf_ims_ops = {
	.irq_mask		= adf_ims_irq_mask,
	.irq_unmask		= adf_ims_irq_unmask,
	.write_msg		= adf_ims_write_msg,
};

static void adf_vqat_cleanup_ims(struct adf_vdcm_vqat *vqat)
{
	struct device *dev = mdev_dev(vqat->mdev);
	struct adf_vqat_irq_ctx *irq_ctx = &vqat->irq_ctx[ADF_VQAT_RING_IRQ];
#if (KERNEL_VERSION(5, 6, 0) == LINUX_VERSION_CODE)
	struct adf_adi_ep *adi = (struct adf_adi_ep *)vqat->hw_priv;

	adf_vqat_irq_ctx_cleanup(irq_ctx);
	platform_msi_domain_free_irqs_group(dev, adi->ims_group);
#elif (KERNEL_VERSION(5, 8, 0) == LINUX_VERSION_CODE)

	adf_vqat_irq_ctx_cleanup(irq_ctx);
	dev_msi_domain_free_irqs(dev);
#endif
}

static int adf_vqat_setup_ims(struct adf_vdcm_vqat *vqat)
{
	int ret;
	struct device *dev = mdev_dev(vqat->mdev);
	struct msi_desc *desc;
	struct adf_accel_dev *accel_dev = vqat->parent;
	struct adf_vqat_irq_ctx *irq_ctx = &vqat->irq_ctx[ADF_VQAT_RING_IRQ];
	char name[ADF_VQAT_IRQ_NAME_SIZE];

#if (KERNEL_VERSION(5, 6, 0) == LINUX_VERSION_CODE)
	struct adf_adi_ep *adi = (struct adf_adi_ep *)vqat->hw_priv;

	/* for now, vqat only has one IMS entry */
	ret = platform_msi_domain_alloc_irqs_group(dev, 1, &adf_ims_ops,
						   &adi->ims_group);
	dev->is_platform_msi = IMS;
	if (ret < 0) {
		dev_err(dev, "Failed to alloc IMS entry with %d\n", ret);
		return ret;
	}

	desc = first_msi_entry_current_group(dev);
#elif (KERNEL_VERSION(5, 8, 0) == LINUX_VERSION_CODE)
	struct intel_iommu *iommu;
	struct adf_adi_ep *adi = (struct adf_adi_ep *)vqat->hw_priv;
	u8 bus, devfn;

	/* for now, vqat only has one IMS entry */
	iommu = device_to_iommu(&GET_DEV(accel_dev), &bus, &devfn);
	if (!iommu) {
		dev_err(dev, "Failed to retrieve iommu for parent\n");
		return -EFAULT;
	}
	dev->msi_domain = iommu->ir_dev_msi_domain;
	ret = dev_msi_domain_alloc_irqs(dev, 1, &adf_ims_ops);
	if (ret < 0) {
		dev_err(dev, "Failed to alloc IMS entry with %d\n", ret);
		return ret;
	}
	desc = first_msi_entry(dev);
#endif
	if (!desc) {
		dev_err(dev, "Invalid desc\n");
		adf_vqat_cleanup_ims(vqat);
		return -EFAULT;
	}
	snprintf(name, sizeof(name), "qatims%d.%d.%d",
		 accel_dev->accel_id, ADI_IDX(adi), ADF_VQAT_RING_IRQ);
	adf_vqat_irq_ctx_init(irq_ctx, name, adf_vqat_set_adi_irq, vqat);
	ret = adf_vqat_irq_ctx_set_irq_info(irq_ctx,
					    desc->irq,
					    1);
	if (ret) {
		dev_err(dev, "Failed to request set vqat_irq_ctx\n");
		adf_vqat_cleanup_ims(vqat);
		return ret;
	}

	return 0;
}
#else
static void adf_vqat_cleanup_ims(struct adf_vdcm_vqat *vqat)
{
	struct adf_vqat_irq_ctx *irq_ctx = &vqat->irq_ctx[ADF_VQAT_RING_IRQ];

	adf_vqat_irq_ctx_cleanup(irq_ctx);
}

static int adf_vqat_setup_ims(struct adf_vdcm_vqat *vqat)
{
	int ret;
	struct device *dev = mdev_dev(vqat->mdev);
	struct adf_accel_dev *accel_dev = vqat->parent;
	struct adf_vqat_irq_ctx *irq_ctx = &vqat->irq_ctx[ADF_VQAT_RING_IRQ];
	char name[ADF_VQAT_IRQ_NAME_SIZE];
	struct adf_accel_pci *pci_dev_info = &accel_dev->accel_pci_dev;
	struct msix_entry *msixe = pci_dev_info->msix_entries.entries;
	struct adf_adi_ep *adi = (struct adf_adi_ep *)vqat->hw_priv;

	if (!msixe) {
		dev_err(dev, "Internal error\n");
		return -EFAULT;
	}
	snprintf(name, sizeof(name), "qatims%d.%d.%d",
		 accel_dev->accel_id, ADI_IDX(adi), ADF_VQAT_RING_IRQ);
	adf_vqat_irq_ctx_init(irq_ctx, name, adf_vqat_set_adi_irq, vqat);
	ret = adf_vqat_irq_ctx_set_irq_info(irq_ctx,
					    msixe[adi->bank_idx].vector,
					    1);
	if (ret) {
		dev_err(dev, "Failed to request set vqat_irq_ctx\n");
		adf_vqat_cleanup_ims(vqat);
		return ret;
	}

	return 0;
}
#endif

static irqreturn_t adf_vqat_wq_completion_isr(int irq, void *data)
{
	struct adf_vqat_irq_ctx *ctx = data;
	struct adf_vdcm_vqat *vqat = ctx->data;
	int ret;

	if (adf_vqat_irq_ctx_trigger(ctx)) {
		ret = eventfd_signal(adf_vqat_irq_ctx_trigger(ctx), 1);
		if (ret != 1) {
			dev_warn(mdev_dev(vqat->mdev),
				 "%s: eventfd signal failed (%d)\n",
				 __func__, ret);
		}
	} else {
		dev_err(mdev_dev(vqat->mdev),
			"%s : ambiguous interrupt\n", __func__);
	}

	return IRQ_HANDLED;
}

static int adf_vqat_request_irqs(struct adf_vdcm_vqat *vqat)
{
	struct device *dev = mdev_dev(vqat->mdev);
	int retv;
	struct adf_vqat_irq_ctx *irq_ctx = &vqat->irq_ctx[ADF_VQAT_RING_IRQ];

	retv = devm_request_irq(dev,
				adf_vqat_irq_ctx_irq_no(irq_ctx),
				adf_vqat_wq_completion_isr,
				0,
				adf_vqat_irq_ctx_name(irq_ctx),
				irq_ctx);
	if (retv) {
		dev_err(dev, "Failed to request irq%d with error %d\n",
			adf_vqat_irq_ctx_irq_no(irq_ctx),
			retv);
		return retv;
	}

	return retv;
}

static inline
void adf_vqat_release_irqs(struct adf_vdcm_vqat *vqat)
{
	int i;
	struct adf_vqat_irq_ctx *irq_ctx;
	struct device *dev = mdev_dev(vqat->mdev);

	for (i = 0; i < ADF_VQAT_IRQ_MAX; i++) {
		int irq;

		irq_ctx = &vqat->irq_ctx[i];
		irq = adf_vqat_irq_ctx_irq_no(irq_ctx);
		if (irq > 0)
			devm_free_irq(dev, irq, irq_ctx);
	}
}

#else
static void adf_vqat_cleanup_ims(struct adf_vdcm_vqat *vqat)
{
}

static int adf_vqat_setup_ims(struct adf_vdcm_vqat *vqat)
{
	dev_warn(mdev_dev(vqat->mdev),
		 "IMS is not supported\n!!");

	return -EINVAL;
}

static int adf_vqat_request_irqs(struct adf_vdcm_vqat *vqat)
{
	return -EINVAL;
}

static inline
void adf_vqat_release_irqs(struct adf_vdcm_vqat *vqat)
{
}
#endif

#if (KERNEL_VERSION(5, 11, 0) > LINUX_VERSION_CODE)
static int adf_vqat_get_pasid(struct adf_vdcm_vqat *vqat)
{
	dev_warn(&GET_DEV(vqat->parent),
		 "%s : Please update your kernel to the one which supports sIOV\n",
		 __func__);
	return -EINVAL;
}
#elif (KERNEL_VERSION(5, 16, 0) > LINUX_VERSION_CODE)
static int adf_vqat_get_pasid(struct adf_vdcm_vqat *vqat)
{
	struct vfio_group *vfio_group;
	struct iommu_domain *domain;
	struct adf_adi_ep *adi = (struct adf_adi_ep *)(vqat->hw_priv);
	struct device *dev = mdev_dev(vqat->mdev);
	int pasid;

	vfio_group = vfio_group_get_external_user_from_dev(dev);
	if (IS_ERR_OR_NULL(vfio_group)) {
		dev_err(mdev_dev(vqat->mdev),
			"%s : failed to get group for vqat\n",
			__func__);
		return -EFAULT;
	}

	domain = vfio_group_iommu_domain(vfio_group);
	if (IS_ERR_OR_NULL(domain)) {
		dev_err(mdev_dev(vqat->mdev),
			"%s : unable to get domain for adi %d\n",
			__func__, ADI_IDX(adi));
		vfio_group_put_external_user(vfio_group);
		return -EFAULT;
	}

	pasid = iommu_aux_get_pasid(domain, &GET_DEV(vqat->parent));
	if (pasid < 0) {
		dev_err(mdev_dev(vqat->mdev),
			"%s : unable to get pasid for adi %d\n",
			__func__, ADI_IDX(adi));
		vfio_group_put_external_user(vfio_group);
	}
	vqat->group = vfio_group;

	return pasid;
}
#else
static int adf_vqat_get_pasid(struct adf_vdcm_vqat *vqat)
{
	dev_warn(&GET_DEV(vqat->parent),
		 "%s : Please update your kernel to the one which supports sIOV\n",
		 __func__);
	return -EINVAL;
}
#endif

#if (KERNEL_VERSION(5, 2, 0) <= LINUX_VERSION_CODE && \
	KERNEL_VERSION(5, 16, 0) > LINUX_VERSION_CODE)
static inline int adf_vqat_parent_iommu_prepare(struct adf_accel_dev *parent)
{
	if (iommu_dev_feature_enabled(&GET_DEV(parent),
				      IOMMU_DEV_FEAT_AUX))
		return 0;

	if (iommu_dev_enable_feature(&GET_DEV(parent),
				     IOMMU_DEV_FEAT_AUX) < 0) {
		dev_warn(&GET_DEV(parent),
			 "%s : Can't enable iommu aux feature\n", __func__);
		return 0;
	}

	return 0;
}

static inline void adf_vqat_parent_iommu_finish(struct adf_accel_dev *parent)
{
	if (!iommu_dev_feature_enabled(&GET_DEV(parent),
				       IOMMU_DEV_FEAT_AUX))
		return;
	if (iommu_dev_disable_feature(&GET_DEV(parent),
				      IOMMU_DEV_FEAT_AUX)) {
		dev_warn(&GET_DEV(parent),
			 "%s : Can't disable iommu aux feature\n", __func__);
	}
}

static inline void adf_vqat_cleanup_iommu(struct adf_vdcm_vqat *vqat)
{
}

static inline int adf_vqat_setup_iommu(struct adf_vdcm_vqat *vqat)
{
	if (!iommu_dev_feature_enabled(&GET_DEV(vqat->parent),
				       IOMMU_DEV_FEAT_AUX)) {
		dev_err(&GET_DEV(vqat->parent),
			"%s : IOMMU aux feature is not enabled\n", __func__);
		return -EINVAL;
	}
	/* Set mdev iommu device */
#if KERNEL_VERSION(5, 13, 0) <= LINUX_VERSION_CODE
	mdev_set_iommu_device(vqat->mdev,
			      &GET_DEV(vqat->parent));
	return 0;
#else
	if (mdev_set_iommu_device(mdev_dev(vqat->mdev),
				  &GET_DEV(vqat->parent)) < 0) {
		dev_err(mdev_dev(vqat->mdev),
			"%s : unable to set iommu device\n", __func__);
		return -EINVAL;
	}
	return 0;
#endif
}
#else
static void adf_vqat_cleanup_iommu(struct adf_vdcm_vqat *vqat)
{
}

static int adf_vqat_setup_iommu(struct adf_vdcm_vqat *vqat)
{
	return -EINVAL;
}

static inline int adf_vqat_parent_iommu_prepare(struct adf_accel_dev *parent)
{
	dev_warn(&GET_DEV(parent),
		 "%s : Please use kernel which supports sIOV\n",
		 __func__);
	return 0;
}

static inline void adf_vqat_parent_iommu_finish(struct adf_accel_dev *parent)
{
}
#endif

static
int adf_vdcm_adi_class_num_avail_insts(struct adf_accel_dev *parent,
				       struct adf_vqat_class *dclass)
{
	enum adi_service_type adi_type;

	if (!parent || !dclass)
		return -EINVAL;

	switch (adf_vqat_class_type(dclass)) {
	case QAT_VQAT_ADI_RP_SYM:
		adi_type = ADI_TYPE_SYM;
		break;
	case QAT_VQAT_ADI_RP_DC:
		adi_type = ADI_TYPE_COMP;
		break;
	case QAT_VQAT_ADI_RP_ASYM:
		adi_type = ADI_TYPE_ASYM;
		break;
	default:
		return 0;
	}

	return adf_get_num_avail_adis(parent, adi_type);
}

static int adf_vdcm_vqat_parent_prepare(struct adf_accel_dev *parent,
					struct adf_vqat_class *dclass)
{
	void *mgr = adf_vqat_adi_class_parent_mgr;
	int ret;

	ret = adf_vdcm_obj_mgr_ref_obj(mgr, parent, NULL);
	if (ret < 0)
		return ret;
	else
		return 0;
}

static void adf_vdcm_vqat_parent_finish(struct adf_accel_dev *parent,
					struct adf_vqat_class *dclass)
{
	void *mgr = adf_vqat_adi_class_parent_mgr;

	adf_vdcm_obj_mgr_unref_obj(mgr, parent, NULL);
}

static void adf_vqat_set_misc_irq(struct adf_vqat_irq_ctx *ctx,
				  enum adf_vqat_irq_op irq_op)
{
	struct adf_vdcm_vqat *vqat = ctx->data;

	adf_vdcm_set_vqat_msix_vector(vqat, ADF_VQAT_MISC_IRQ, irq_op);
}

static void adf_vqat_cleanup_misc_irq(struct adf_vdcm_vqat *vqat)
{
	struct adf_vqat_irq_ctx *irq_ctx = &vqat->irq_ctx[ADF_VQAT_MISC_IRQ];

	adf_vqat_irq_ctx_cleanup(irq_ctx);
}

static int adf_vqat_setup_misc_irq(struct adf_vdcm_vqat *vqat)
{
	struct adf_accel_dev *accel_dev = vqat->parent;
	struct adf_vqat_irq_ctx *irq_ctx = &vqat->irq_ctx[ADF_VQAT_MISC_IRQ];
	char name[ADF_VQAT_IRQ_NAME_SIZE];

	snprintf(name, sizeof(name), "qat%d-misc%d",
		 accel_dev->accel_id, ADF_VQAT_MISC_IRQ);
	adf_vqat_irq_ctx_init(irq_ctx, name, adf_vqat_set_misc_irq, vqat);
	adf_vqat_irq_ctx_set_irq_info(irq_ctx, 0, 0);

	return 0;
}

static void adf_vqat_cleanup_irqs(struct adf_vdcm_vqat *vqat)
{
	adf_vqat_cleanup_ims(vqat);
	adf_vqat_cleanup_misc_irq(vqat);
	kfree(vqat->irq_ctx);
	vqat->irq_ctx = NULL;
}

static int adf_vqat_init_irqs(struct adf_vdcm_vqat *vqat)
{
	vqat->irqs = ADF_VQAT_IRQ_MAX;
	vqat->irq_ctx = kcalloc(vqat->irqs,
				sizeof(*vqat->irq_ctx),
				GFP_KERNEL);
	if (!vqat->irq_ctx)
		return -ENOMEM;

	/* Setup misc irq */
	if (adf_vqat_setup_misc_irq(vqat) < 0) {
		dev_err(mdev_dev(vqat->mdev),
			"%s : unable to setup ims correctly\n", __func__);
		goto err_setup_misc_irq;
	}

	/* Setup IMS for adi irq */
	if (adf_vqat_setup_ims(vqat) < 0) {
		dev_err(mdev_dev(vqat->mdev),
			"%s : unable to setup ims correctly\n", __func__);
		goto err_setup_ims;
	}

	return 0;

err_setup_ims:
	adf_vqat_cleanup_misc_irq(vqat);
err_setup_misc_irq:
	kfree(vqat->irq_ctx);
	vqat->irq_ctx = NULL;
	return -EINVAL;
}

static
struct adf_vdcm_vqat *adf_vdcm_adi_vqat_create(struct adf_accel_dev *parent,
					       struct mdev_device *mdev,
					       struct adf_vqat_class *dclass)
{
	struct adf_vdcm_vqat *vqat = NULL;
	struct adi_mmio_info mmio_info;
	struct adf_adi_ep *adi = NULL;
	enum adi_service_type adi_type;
	struct adf_vcfg_attr_desc reg_attr_desc;
	u16 subsystem_id;

	if (!dclass || !parent || !mdev)
		goto vqat_create_err;

	switch (adf_vqat_class_type(dclass)) {
	case QAT_VQAT_ADI_RP_SYM:
		adi_type = ADI_TYPE_SYM;
		subsystem_id = ADF_VQAT_SYM_PCI_SUBSYSTEM_ID;
		break;
	case QAT_VQAT_ADI_RP_ASYM:
		adi_type = ADI_TYPE_ASYM;
		subsystem_id = ADF_VQAT_ASYM_PCI_SUBSYSTEM_ID;
		break;
	case QAT_VQAT_ADI_RP_DC:
		adi_type = ADI_TYPE_COMP;
		subsystem_id = ADF_VQAT_DC_PCI_SUBSYSTEM_ID;
		break;
	default:
		goto vqat_create_err;
	}

	adi = adf_adi_alloc(parent, adi_type);
	if (!adi) {
		dev_err(mdev_dev(mdev), "No avialble ADI for vqat\n");
		goto vqat_create_err;
	}

	if (!adi->adi_ops) {
		dev_err(mdev_dev(mdev), "Invalid ADI ops\n");
		goto vqat_create_err;
	}

	if (adi->adi_ops->get_mmio_info(adi, &mmio_info)) {
		dev_err(mdev_dev(mdev), "Cannot get mmap info from ADI\n");
		goto vqat_create_err;
	}

	if (adi->adi_ops->reset(adi, false)) {
		dev_err(mdev_dev(mdev), "ADI reset failed\n");
		goto vqat_create_err;
	}

	/* Construct the vQAT */
	vqat = kzalloc(sizeof(*vqat), GFP_KERNEL);
	if (!vqat)
		goto vqat_create_err;

	mutex_init(&vqat->vdev_lock);
	/* Populate configuration space */
	vqat->vcfg = adf_vdcm_vcfg_init(ADF_VQAT_PCI_DEVICE_ID, subsystem_id,
					false);
	if (!vqat->vcfg) {
		dev_err(mdev_dev(mdev),
			"Failed to set vqat default configuration space\n");
		goto vqat_create_err;
	}

	reg_attr_desc.ro_mask = PCI_VENDOR_DEVICE_RO_MASK;
	reg_attr_desc.woc_mask = 0x0;
	if (adf_vdcm_set_vreg_attr(vqat->vcfg, PCI_CAP_ID_BASIC,
				   PCI_VENDOR_ID, &reg_attr_desc)) {
		dev_err(mdev_dev(mdev),
			"Failed to handle vqat vendor ID register or device ID register\n");
		goto vqat_create_err;
	}

	reg_attr_desc.ro_mask = PCI_COMMAND_STATUS_RO_MASK;
	reg_attr_desc.woc_mask = PCI_COMMAND_STATUS_WOC_MASK;
	if (adf_vdcm_set_vreg_attr(vqat->vcfg, PCI_CAP_ID_BASIC,
				   PCI_COMMAND, &reg_attr_desc)) {
		dev_err(mdev_dev(mdev),
			"Failed to handle vqat command register or status register\n");
		goto vqat_create_err;
	}

	if (adf_vdcm_register_vreg_handle(vqat->vcfg, PCI_CAP_ID_BASIC,
					  PCI_ROM_ADDRESS,
					  adf_vdcm_pcie_rom_bar_hdl)) {
		dev_err(mdev_dev(mdev),
			"Failed to handle vqat expansion rom bar\n");
		goto vqat_create_err;
	}

	if (adf_vdcm_register_vreg_handle(vqat->vcfg, PCI_CAP_ID_BASIC,
					  PCI_BASE_ADDRESS_0,
					  adf_vdcm_pcie_bar_base_addr_hdl)) {
		dev_err(mdev_dev(mdev),
			"Failed to handle vqat etr bar\n");
		goto vqat_create_err;
	}

	if (adf_vdcm_register_vreg_handle(vqat->vcfg, PCI_CAP_ID_BASIC,
					  PCI_BASE_ADDRESS_2,
					  adf_vdcm_pcie_bar_base_addr_hdl)) {
		dev_err(mdev_dev(mdev),
			"Failed to handle vqat misc bar\n");
		goto vqat_create_err;
	}

	if (adf_vdcm_register_vreg_handle(vqat->vcfg, PCI_CAP_ID_BASIC,
					  PCI_BASE_ADDRESS_4,
					  adf_vdcm_pcie_bar_base_addr_hdl)) {
		dev_err(mdev_dev(mdev),
			"Failed to handle vqat ext bar\n");
		goto vqat_create_err;
	}

	if (adf_vdcm_register_vreg_handle(vqat->vcfg, PCI_CAP_ID_EXP,
					  PCI_EXP_DEVCTL,
					  adf_vdcm_pcie_ctl_sta_hdl)) {
		dev_err(mdev_dev(mdev),
			"Failed to handle device control register or device status register\n");
		goto vqat_create_err;
	}

	/* Populate vQAT ETR bar info */
	if (adf_vdcm_vqat_bar_init(&vqat->bar[ADF_VQAT_ETR_BAR], "ETR",
				   0, 0,
				   ADF_VQAT_ETR_BAR_SIZE,
				   1, 1, ADF_VQAT_BAR_MMIO, 1) < 0) {
		dev_err(mdev_dev(mdev),
			"Failed to init vqat bar 0\n");
		goto vqat_create_err;
	}

	if (adf_vdcm_vqat_bar_add_sub_mmap_area(&vqat->bar[ADF_VQAT_ETR_BAR],
						mmio_info.phy_addr,
						mmio_info.virt_addr,
						0,
						0x1000, 1) < 0) {
		dev_err(mdev_dev(mdev),
			"Failed to init sub mmap area\n");
		goto vqat_create_err;
	}

	vqat->msgqcfg = adf_iov_vdcm_build_msgqcfg(1, ADF_VQAT_MSGQ_BAR_OFS,
						   ADF_VQAT_MSGQ_SIZE);

	if (adf_vdcm_vqat_bar_init(&vqat->bar[ADF_VQAT_PMISC_BAR], "PMISC",
				   0, 0, ADF_VQAT_PMISC_BAR_SIZE,
				   1, 1, ADF_VQAT_BAR_MIX, 1) < 0)	{
		dev_err(mdev_dev(mdev),
			"Failed to init vqat pmisc bar\n");
		goto vqat_create_err;
	}

	if (adf_vdcm_vqat_msgq_init(&vqat->iov_msgq) < 0) {
		dev_err(mdev_dev(mdev),
			"Failed to init iov msgq\n");
		goto vqat_create_err;
	}

	if (adf_vdcm_vqat_bar_add_sub_mmap_area(&vqat->bar[ADF_VQAT_PMISC_BAR],
						vqat->iov_msgq.pbase,
						vqat->iov_msgq.vbase,
						ADF_VQAT_MSGQ_BAR_OFS,
						ADF_VQAT_MSGQ_MEMSIZE,
						0) < 0) {
		dev_err(mdev_dev(mdev),
			"Failed to init sub mmap area\n");
		goto vqat_create_err;
	}

	if (adf_init_vdcm_iov_agent(&vqat->iov_agent, vqat, 0) < 0) {
		dev_err(mdev_dev(mdev),
			"Failed to init iov agent\n");
		goto vqat_create_err;
	}

	if (adf_vdcm_alloc_vqat_svc_cap_def(&vqat->vcap, parent, dclass)) {
		dev_err(mdev_dev(mdev),
			"Failed to alloc vqat svc cap block\n");
		goto vqat_create_err;
	}
	if (adf_vdcm_vqat_bar_init(&vqat->bar[ADF_VQAT_EXT_BAR], "EXT",
				   0, 0, ADF_VQAT_EXT_BAR_SIZE,
				   1, 0, ADF_VQAT_BAR_MEM, 0)) {
		dev_err(mdev_dev(mdev),
			"Failed to init vqat ext bar\n");
		goto vqat_create_err;
	}

	dev_info(mdev_dev(mdev), "Created vQAT using ADI %d, config size %d, bar0(size %lld, addr 0x%llx, attr %x),bar1(size %lld, addr 0x%llx, attr %x)\n",
		 ADI_IDX(adi),
		 vqat->vcfg->size,
		 vqat->bar[0].size, vqat->bar[0].base_addr, vqat->bar[0].attr,
		 vqat->bar[1].size, vqat->bar[1].base_addr, vqat->bar[1].attr
		);

	/* Populate the vqat context */
	vqat->hw_priv = (void *)adi;
	vqat->parent = parent;
	vqat->mdev = mdev;
	mdev_set_drvdata(mdev, vqat);

	if (adf_vqat_init_irqs(vqat) < 0) {
		dev_err(mdev_dev(vqat->mdev),
			"%s : unable to setup irqs\n", __func__);
		goto vqat_create_err;
	}

	if (adf_vqat_setup_iommu(vqat) < 0) {
		dev_err(mdev_dev(vqat->mdev),
			"%s : unable to set iommu device\n", __func__);
		goto vqat_create_err_iommu;
	}

	return vqat;

vqat_create_err_iommu:
	adf_vqat_cleanup_irqs(vqat);
vqat_create_err:
	if (vqat) {
		adf_vdcm_vqat_bar_cleanup(&vqat->bar[ADF_VQAT_EXT_BAR]);
		adf_vdcm_free_vqat_svc_cap_def(&vqat->vcap, parent, dclass);
		adf_cleanup_vdcm_iov_agent(&vqat->iov_agent);
		adf_vdcm_vqat_msgq_cleanup(&vqat->iov_msgq);
		adf_vdcm_vqat_bar_cleanup(&vqat->bar[ADF_VQAT_ETR_BAR]);
		adf_vdcm_vqat_bar_cleanup(&vqat->bar[ADF_VQAT_PMISC_BAR]);
		adf_vdcm_vcfg_destroy(&vqat->vcfg);
		mutex_destroy(&vqat->vdev_lock);
		kfree(vqat);
	}
	if (adi)
		adf_adi_free(adi);

	return NULL;
}

static void adf_vdcm_adi_vqat_destroy(struct adf_accel_dev *parent,
				      struct adf_vdcm_vqat *vqat)
{
	struct adf_adi_ep *adi = vqat->hw_priv;

	dev_info(mdev_dev(vqat->mdev), "%s : destroy vqat %p with adi %d\n",
		 __func__, vqat, ADI_IDX(adi));

	/* Reset the ADI first anyway */
	if (adi->adi_ops && adi->adi_ops->reset)
		adi->adi_ops->reset(adi, false);

	adf_vqat_cleanup_iommu(vqat);
	adf_vqat_cleanup_irqs(vqat);
	adf_vdcm_vqat_bar_cleanup(&vqat->bar[ADF_VQAT_EXT_BAR]);
	adf_vdcm_free_vqat_svc_cap_def(&vqat->vcap, vqat->parent,
				       vqat->dclass);
	adf_cleanup_vdcm_iov_agent(&vqat->iov_agent);
	adf_vdcm_vqat_msgq_cleanup(&vqat->iov_msgq);
	adf_vdcm_vqat_bar_cleanup(&vqat->bar[ADF_VQAT_ETR_BAR]);
	adf_vdcm_vqat_bar_cleanup(&vqat->bar[ADF_VQAT_PMISC_BAR]);
	adf_vdcm_vcfg_destroy(&vqat->vcfg);
	mutex_destroy(&vqat->vdev_lock);
	kfree(vqat);
	adf_adi_free(adi);
}

static int adf_vdcm_adi_vqat_open(struct adf_vdcm_vqat *vqat)
{
	int pasid;
	struct adf_adi_ep *adi = vqat->hw_priv;
	struct device *dev = mdev_dev(vqat->mdev);

	dev_info(dev, "%s : open vqat %p with adi %d\n",
		 __func__, vqat, ADI_IDX(adi));

	/* Get the PASID */
	pasid = adf_vqat_get_pasid(vqat);
	if (pasid < 0) {
		dev_err(mdev_dev(vqat->mdev),
			"%s : unable to get pasid for adi %d\n",
			__func__, ADI_IDX(adi));
		return -EINVAL;
	}
	/* Program the allocated PASID*/
	adi->adi_ops->set_pasid(adi, pasid);
	dev_info(dev, "%s : get pasid %d for ADI %d\n",
		 __func__, pasid, ADI_IDX(adi));
	/* Request IRQ */
	return adf_vqat_request_irqs(vqat);
}

static void adf_vdcm_adi_vqat_release(struct adf_vdcm_vqat *vqat)
{
	dev_info(mdev_dev(vqat->mdev), "Release vqat %p\n", vqat);
	adf_vqat_release_irqs(vqat);
#if (KERNEL_VERSION(5, 11, 0) <= LINUX_VERSION_CODE)
	vfio_group_put_external_user(vqat->group);
#endif
	adf_vdcm_adi_vqat_do_reset(vqat, false);
}

static int adf_vdcm_adi_vqat_cfg_read(struct adf_vdcm_vqat *vqat,
				      unsigned int pos, void *buf,
				      unsigned int len)
{
	return adf_vdcm_vcfg_rw(vqat,
				pos,
				buf,
				len,
				false);
}

static int adf_vdcm_adi_vqat_cfg_write(struct adf_vdcm_vqat *vqat,
				       unsigned int pos, void *buf,
				       unsigned int len)
{
	return adf_vdcm_vcfg_rw(vqat,
				pos,
				buf,
				len,
				true);
}

static int adf_vdcm_adi_ext_bar_read(struct adf_vdcm_vqat *vqat,
				     u64 pos, void *buf,
				     unsigned int len)
{
	struct adf_vdcm_vqat_cap *vcap = &vqat->vcap;

	if (pos + len <= adf_vqat_caps_blk_size(vcap))
		memcpy(buf, (u8 *)adf_vqat_caps_blk(vcap) + pos, len);
	else
		memset(buf, 0xff, len);

	return len;
}

static int adf_vdcm_adi_misc_bar_read(struct adf_vdcm_vqat *vqat,
				      u64 pos, void *buf,
				      unsigned int len)
{
	int ret;
	void *from;

	switch (pos) {
	case ADF_VQAT_VINTSOU:
		from = &vqat->vintsrc;
		ret = min_t(unsigned int, len, sizeof(vqat->vintsrc));
		break;
	case ADF_VQAT_VINTMSK:
		from = &vqat->vintmsk;
		ret = min_t(unsigned int, len, sizeof(vqat->vintmsk));
		break;
	case ADF_VQAT_MSGQ_CFG:
		from = &vqat->msgqcfg;
		ret = min_t(unsigned int, len, sizeof(vqat->msgqcfg));
		break;
	case ADF_VQAT_MSGQ_TX_NOTIFIER:
		from = &vqat->iov_msgq.tx_notifier;
		ret = min_t(unsigned int, len,
			    sizeof(vqat->iov_msgq.tx_notifier));
		break;
	case ADF_VQAT_MSGQ_RX_NOTIFIER:
		from = &vqat->iov_msgq.rx_notifier;
		ret = min_t(unsigned int, len,
			    sizeof(vqat->iov_msgq.rx_notifier));
		break;
	default:
		return -EINVAL;
	}

	memcpy(buf, from, ret);
	if (len > ret) {
		dev_warn(mdev_dev(vqat->mdev),
			 "%s: access beyond the boundary of the vqat register at 0x%llx\n",
			 __func__, pos);
		memset(buf + ret, 0, len - ret);
	} else if (len < ret) {
		dev_warn(mdev_dev(vqat->mdev),
			 "%s: access partial of vqat register at 0x%llx\n",
			 __func__, pos);
	}

	return len;
}

static int adf_vdcm_adi_misc_bar_write(struct adf_vdcm_vqat *vqat,
				       u64 pos, void *buf,
				       unsigned int len)
{
	int ret;
	void *to;

	switch (pos) {
	case ADF_VQAT_VINTSOU:
		ret = min_t(unsigned int, len, sizeof(vqat->vintsrc));
		to = &vqat->vintsrc;
		memcpy(to, buf, ret);
		/* W1C register */
		if (vqat->vintsrc == 1)
			vqat->vintsrc = 0;
		break;
	case ADF_VQAT_VINTMSK:
		to = &vqat->vintmsk;
		ret = min_t(unsigned int, len, sizeof(vqat->vintmsk));
		memcpy(to, buf, ret);
		break;
	case ADF_VQAT_MSGQ_TX_NOTIFIER:
		to = &vqat->iov_msgq.tx_notifier;
		ret = min_t(unsigned int, len,
			    sizeof(vqat->iov_msgq.tx_notifier));
		memcpy(to, buf, ret);
		if (vqat->iov_msgq.tx_notifier > 0)
			adf_vdcm_iov_handle_vqat_msg(&vqat->iov_agent, vqat);
		break;
	case ADF_VQAT_MSGQ_RX_NOTIFIER:
		to = &vqat->iov_msgq.rx_notifier;
		ret = min_t(unsigned int, len,
			    sizeof(vqat->iov_msgq.rx_notifier));
		memcpy(to, buf, ret);
		break;
	default:
		return -EINVAL;
	}

	if (len > ret)
		dev_warn(mdev_dev(vqat->mdev),
			 "%s: access beyond the boundary of the vqat register @0x%llx\n",
			 __func__, pos);
	else if (len < ret)
		dev_warn(mdev_dev(vqat->mdev),
			 "%s: access partial of vqat register at 0x%llx\n",
			 __func__, pos);

	return ret;
}

static int adf_vdcm_adi_vqat_mmio_read(struct adf_vdcm_vqat *vqat,
				       int bar, u64 pos, void *buf,
				       unsigned int len)
{
	struct adf_adi_ep *adi = (struct adf_adi_ep *)(vqat->hw_priv);

	if (!adi || !adi->adi_ops || !adi->adi_ops->vreg_read)
		return -EFAULT;

	dev_dbg(mdev_dev(vqat->mdev), "%s: read vqat %p bar%d with adi %d\n",
		__func__, vqat, bar, ADI_IDX(adi));

	if (bar == ADF_VQAT_ETR_BAR)
		return adi->adi_ops->vreg_read(adi, pos, buf, len);
	else if (bar == ADF_VQAT_PMISC_BAR)
		return adf_vdcm_adi_misc_bar_read(vqat, pos, buf, len);
	else if (bar == ADF_VQAT_EXT_BAR)
		return adf_vdcm_adi_ext_bar_read(vqat, pos, buf, len);

	dev_err(mdev_dev(vqat->mdev),
		"%s: Invalid access to Bar#%d\n",
		__func__, bar);

	return -EINVAL;
}

static int adf_vdcm_adi_vqat_mmio_write(struct adf_vdcm_vqat *vqat,
					int bar, u64 pos, void *buf,
					unsigned int len)
{
	struct adf_adi_ep *adi = (struct adf_adi_ep *)(vqat->hw_priv);

	if (!adi || !adi->adi_ops || !adi->adi_ops->vreg_write)
		return -EFAULT;

	dev_dbg(mdev_dev(vqat->mdev), "%s: write vqat %p bar%d with adi %d\n",
		__func__, vqat, bar, ADI_IDX(adi));

	if (bar == ADF_VQAT_ETR_BAR)
		return adi->adi_ops->vreg_write(adi, pos, buf, len);
	else if (bar == ADF_VQAT_PMISC_BAR)
		return adf_vdcm_adi_misc_bar_write(vqat, pos, buf, len);

	dev_err(mdev_dev(vqat->mdev),
		"%s: Invalid access to Bar#%d\n",
		__func__, bar);

	return -EINVAL;
}

static int adf_vdcm_adi_vqat_reset(struct adf_vdcm_vqat *vqat)
{
	dev_dbg(&GET_DEV(vqat->parent), "%s\n", __func__);

	return adf_vdcm_adi_vqat_do_reset(vqat, true);
}

static int adf_vqat_adi_class_handler(struct adf_vqat_class *dclass,
				      struct adf_accel_dev *parent,
				      enum adf_vqat_class_op_func func,
				      void *func_data)
{
	int ret = 0;

	switch (func) {
	case ADF_VDCM_GET_NUM_AVAIL_INSTS:
		ret = adf_vdcm_adi_class_num_avail_insts(parent, dclass);
		break;
	case ADF_VDCM_NOTIFY_PARENT_REGISTER:
		ret = adf_vdcm_vqat_parent_prepare(parent, dclass);
		break;
	case ADF_VDCM_NOTIFY_PARENT_UNREGISTER:
		adf_vdcm_vqat_parent_finish(parent, dclass);
		break;
	default:
		break;
	}

	return ret;
}

static struct adf_vdcm_vqat_ops adf_vdcm_adi_vqat_ops = {
	.class_handler = adf_vqat_adi_class_handler,
	.create = adf_vdcm_adi_vqat_create,
	.destroy = adf_vdcm_adi_vqat_destroy,
	.prepare_cap = adf_vqat_prepare_caps,
	.populate_cap = adf_vqat_populate_caps,
	.open = adf_vdcm_adi_vqat_open,
	.release = adf_vdcm_adi_vqat_release,
	.cfg_read = adf_vdcm_adi_vqat_cfg_read,
	.cfg_write = adf_vdcm_adi_vqat_cfg_write,
	.mmio_read = adf_vdcm_adi_vqat_mmio_read,
	.mmio_write = adf_vdcm_adi_vqat_mmio_write,
	.reset = adf_vdcm_adi_vqat_reset,
};

static struct adf_vqat_class adf_vqat_adi_class_sym = {
	.type = QAT_VQAT_ADI_RP_SYM,
	.ops = &adf_vdcm_adi_vqat_ops,
};

static struct adf_vqat_class adf_vqat_adi_class_asym = {
	.type = QAT_VQAT_ADI_RP_ASYM,
	.ops = &adf_vdcm_adi_vqat_ops,
};

static struct adf_vqat_class adf_vqat_adi_class_dc = {
	.type = QAT_VQAT_ADI_RP_DC,
	.ops = &adf_vdcm_adi_vqat_ops,
};

static int adf_vqat_adi_class_parent_init(void *obj, void *cb_data, s64 *p_res)
{
	return adf_vqat_parent_iommu_prepare(obj);
}

static int adf_vqat_adi_class_parent_cleanup(void *obj, void *cb_data, s64 res)
{
	adf_vqat_parent_iommu_finish(obj);

	return 0;
}

int adf_vdcm_init_vqat_adi(void)
{
	int ret = 0;
	void *parent_mgr;

	parent_mgr = adf_vdcm_obj_mgr_new(adf_vqat_adi_class_parent_init,
					  adf_vqat_adi_class_parent_cleanup,
					  NULL);
	if (!parent_mgr)
		return -EFAULT;

	ret = adf_vdcm_register_vqat_class(&adf_vqat_adi_class_sym);
	if (ret)
		goto err_sym;

	ret = adf_vdcm_register_vqat_class(&adf_vqat_adi_class_asym);
	if (ret)
		goto err_asym;

	ret = adf_vdcm_register_vqat_class(&adf_vqat_adi_class_dc);
	if (ret)
		goto err_dc;

	adf_vqat_adi_class_parent_mgr = parent_mgr;

	return 0;
err_dc:
	adf_vdcm_unregister_vqat_class(&adf_vqat_adi_class_asym);
err_asym:
	adf_vdcm_unregister_vqat_class(&adf_vqat_adi_class_sym);
err_sym:
	adf_vdcm_obj_mgr_destroy(parent_mgr);

	return ret;
}

void adf_vdcm_cleanup_vqat_adi(void)
{
	adf_vdcm_unregister_vqat_class(&adf_vqat_adi_class_dc);
	adf_vdcm_unregister_vqat_class(&adf_vqat_adi_class_asym);
	adf_vdcm_unregister_vqat_class(&adf_vqat_adi_class_sym);
	adf_vdcm_obj_mgr_destroy(adf_vqat_adi_class_parent_mgr);
	adf_vqat_adi_class_parent_mgr = NULL;
}

