// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2019 - 2021 Intel Corporation */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/iommu.h>
#include <linux/pci.h>
#include <linux/vfio.h>
#include <linux/mdev.h>
#include <linux/eventfd.h>
#include <linux/version.h>
#include "adf_accel_devices.h"
#include "adf_cfg.h"
#include "adf_common_drv.h"
#include "adf_vdcm.h"

/* TODO: VFIO private helpers, remove later */
#define VFIO_PCI_OFFSET_SHIFT   40
#define VFIO_PCI_OFFSET_TO_INDEX(off)   ((off) >> VFIO_PCI_OFFSET_SHIFT)
#define VFIO_PCI_INDEX_TO_OFFSET(index) ((u64)(index) << VFIO_PCI_OFFSET_SHIFT)
#define VFIO_PCI_OFFSET_MASK    (((u64)(1) << VFIO_PCI_OFFSET_SHIFT) - 1)

struct adf_vdcm_vqat_type {
	struct adf_vqat_class *class;
	struct attribute_group *ag;
};

struct adf_vdcm_type {
	struct list_head list;
	struct adf_vdcm_vqat_type *vqat_type;
	int activated;
};

struct adf_vdcm_ctx_blk {
	struct mdev_parent_ops *vdcm_ops;
	/* The list for created vQATs */
	struct list_head vqats;
	/* The lock for vqats list */
	struct mutex vqats_lock;
	/* The list for registered vqat_types */
	struct list_head vqat_types;
	/* The lock for vqat_types */
	struct mutex vqat_types_lock;
	struct dentry *debugfs_vdcm;
	struct adf_accel_compat_manager *cm;
	int active;
};

static struct service_hndl adf_vdcm_srv_hndl;
static struct adf_vdcm_vqat_type adf_vqat_types[QAT_VQAT_TYPES_MAX];

#if KERNEL_VERSION(5, 13, 0) <= LINUX_VERSION_CODE
static struct adf_vdcm_vqat_type *adf_vdcm_lookup_vqat_type(struct device *dev,
							    struct mdev_type *mtype);
#else
static struct adf_vdcm_vqat_type *adf_vdcm_lookup_vqat_type(struct device *dev,
							    const char *name);
#endif

static int vdcm_vqat_set_msi_trigger(struct adf_vdcm_vqat *vqat,
				     u32 flags, u32 index,
				     u32 start, u32 count,
				     void *data);

static inline bool adf_sriov_enabled(struct adf_accel_dev *accel_dev)
{
	if (accel_to_pci_dev(accel_dev)->is_physfn)
		return accel_dev->pf.vf_info ? true : false;
	else
		return false;
}

struct dentry *adf_vdcm_get_debugfs(struct adf_vdcm_ctx_blk *vdcm)
{
	return vdcm->debugfs_vdcm;
}

struct adf_accel_compat_manager *
adf_vdcm_get_cm(struct adf_vdcm_ctx_blk *vdcm)
{
	if (vdcm)
		return vdcm->cm;
	return NULL;
}

static inline void adf_vdcm_type_insert(struct adf_vdcm_ctx_blk *vdcm,
					struct adf_vdcm_type *type)
{
	mutex_lock(&vdcm->vqat_types_lock);
	list_add(&type->list, &vdcm->vqat_types);
	mutex_unlock(&vdcm->vqat_types_lock);
}

static inline void adf_vdcm_type_remove(struct adf_vdcm_ctx_blk *vdcm,
					struct adf_vdcm_type *type)
{
	mutex_lock(&vdcm->vqat_types_lock);
	list_del(&type->list);
	mutex_unlock(&vdcm->vqat_types_lock);
}

static
void adf_vdcm_type_unregister_all(struct adf_vdcm_ctx_blk *vdcm,
				  struct adf_accel_dev *parent)
{
	struct adf_vdcm_type *type, *tmp;
	struct adf_vdcm_vqat_type *vqat_type;
	struct adf_vdcm_vqat_ops *ops;

	list_for_each_entry_safe(type, tmp, &vdcm->vqat_types, list) {
		adf_vdcm_type_remove(vdcm, type);
		vqat_type = type->vqat_type;
		ops = adf_vqat_class_ops(vqat_type->class);
		ops->class_handler(vqat_type->class,
				   parent,
				   ADF_VDCM_NOTIFY_PARENT_UNREGISTER,
				   NULL);
		kfree(type);
	}
}

static
int adf_vdcm_type_register(struct adf_vdcm_ctx_blk *vdcm,
			   struct adf_accel_dev *parent,
			   struct adf_vdcm_vqat_type *vqat_type)
{
	struct adf_vdcm_type *type;
	struct adf_vdcm_vqat_ops *ops = adf_vqat_class_ops(vqat_type->class);

	type = kzalloc(sizeof(*type), GFP_KERNEL);
	if (!type)
		return -ENOMEM;

	if (ops->class_handler(vqat_type->class, parent,
			       ADF_VDCM_NOTIFY_PARENT_REGISTER,
			       NULL)) {
		dev_err(&GET_DEV(parent),
			"failed to notify type %d\n",
			adf_vqat_class_type(vqat_type->class));
		goto err;
	}

	type->vqat_type = vqat_type;
	adf_vdcm_type_insert(vdcm, type);

	return 0;
err:
	kfree(type);
	return -EFAULT;
}

static inline
void adf_vdcm_remove_vqat(struct adf_vdcm_ctx_blk *vdcm,
			  struct adf_vdcm_vqat *vqat)
{
	mutex_lock(&vdcm->vqats_lock);
	list_del(&vqat->list);
	mutex_unlock(&vdcm->vqats_lock);
}

static inline
int adf_vdcm_add_vqat(struct adf_vdcm_ctx_blk *vdcm,
		      struct adf_vdcm_vqat *vqat)
{
	mutex_lock(&vdcm->vqats_lock);
	list_add(&vqat->list, &vdcm->vqats);
	mutex_unlock(&vdcm->vqats_lock);

	return 0;
}

void adf_vdcm_set_vqat_msix_vector(struct adf_vdcm_vqat *vqat,
				   enum adf_vqat_irq irq,
				   enum adf_vqat_irq_op irq_op)
{
	u32 *ctrl = (u32 *)&vqat->msix_info.entries[irq]
				[PCI_MSIX_ENTRY_VECTOR_CTRL];
	u64 *pba = (u64 *)&vqat->msix_info.pba[irq / 64];

	if (irq_op == ADF_VQAT_IRQ_ENABLE) {
		*ctrl &= ~PCI_MSIX_ENTRY_CTRL_MASKBIT;
		if (test_and_clear_bit(irq, (unsigned long *)pba)) {
			dev_info(mdev_dev(vqat->mdev),
				 "%s : Trigger pending irq %d\n",
				 __func__, irq);
			adf_vdcm_notify_vqat(vqat, irq);
		}
	} else {
		*ctrl |= PCI_MSIX_ENTRY_CTRL_MASKBIT;
	}
}

void adf_vdcm_notify_vqat(struct adf_vdcm_vqat *vqat,
			  enum adf_vqat_irq irq)
{
	int ret;
	struct adf_vqat_irq_ctx *ctx = &vqat->irq_ctx[irq];
	struct eventfd_ctx *trigger = adf_vqat_irq_ctx_trigger(ctx);
	u32 *msix_vec_ctrl = (u32 *)&vqat->msix_info.entries[irq]
					[PCI_MSIX_ENTRY_VECTOR_CTRL];
	u64 *pba = (u64 *)&vqat->msix_info.pba[irq / 64];

	if (unlikely(!trigger)) {
		dev_warn(mdev_dev(vqat->mdev),
			 "%s : ambiguous irq %d as it is not enabled yet\n",
			 __func__, irq);
		return;
	}
	dev_dbg(mdev_dev(vqat->mdev),
		"%s : Assert vqat irq %d\n", __func__, irq);
	if (*msix_vec_ctrl & PCI_MSIX_ENTRY_CTRL_MASKBIT) {
		dev_info(mdev_dev(vqat->mdev),
			 "%s : Set pending bit for irq %d\n",
			 __func__, irq);
		set_bit(irq, (unsigned long *)pba);
		return;
	}
	ret = eventfd_signal(trigger, 1);
	if (ret != 1)
		dev_warn(mdev_dev(vqat->mdev),
			 "%s: failed to signal event with %d\n",
			 __func__, ret);
}

void adf_vdcm_notify_vqat_iov(struct adf_vdcm_vqat *vqat, u32 queue)
{
	vqat->vintsrc |= (1 << queue);
	if (vqat->vintmsk & (1 << queue))
		return;

	adf_vdcm_notify_vqat(vqat, ADF_VQAT_MISC_IRQ);
}

#if KERNEL_VERSION(5, 13, 0) <= LINUX_VERSION_CODE
static int adf_vdcm_vqat_create(struct mdev_device *mdev)
#else
static int adf_vdcm_vqat_create(struct kobject *kobj, struct mdev_device *mdev)
#endif
{
	struct device *par_dev = mdev_parent_dev(mdev);
	struct adf_vdcm_vqat *vqat;
	struct pci_dev *pdev = container_of(par_dev, struct pci_dev, dev);
	struct adf_vdcm_vqat_type *vqat_type;
	struct adf_accel_dev *parent;
	struct adf_vdcm_vqat_ops *ops;

#if KERNEL_VERSION(5, 13, 0) <= LINUX_VERSION_CODE
	vqat_type = adf_vdcm_lookup_vqat_type(par_dev, mdev->type);
	if (!vqat_type || !vqat_type->ag)
		return -EOPNOTSUPP;

	dev_info(mdev_dev(mdev), "Create mdev %p for %s-%s\n",
		 mdev, dev_driver_string(par_dev), vqat_type->ag->name);
#else
	vqat_type = adf_vdcm_lookup_vqat_type(par_dev, kobject_name(kobj));
	dev_info(mdev_dev(mdev), "Create mdev %p for %s\n",
		 mdev, kobject_name(kobj));
#endif

	if (vqat_type) {
		parent = adf_devmgr_pci_to_accel_dev(pdev);
		if (!parent) {
			dev_err(mdev_dev(mdev), "Unknown parent device\n");
			return -ENODEV;
		}
		ops = adf_vqat_class_ops(vqat_type->class);
		if (!ops) {
			dev_err(mdev_dev(mdev),
				"No operation was found for vqat type %d\n",
				adf_vqat_class_type(vqat_type->class));
			return -ENODEV;
		}
		vqat = ops->create(parent, mdev, vqat_type->class);
		if (!vqat) {
			dev_err(mdev_dev(mdev),
				"Failed to create a vqat instance\n");
			return -ENOSPC;
		}
		vqat->ops = adf_vqat_class_ops(vqat_type->class);
		vqat->dclass = vqat_type->class;
		if (adf_vdcm_add_vqat_dbg(parent, vqat)) {
			ops->destroy(parent, vqat);
			dev_err(mdev_dev(mdev),
				"Failed to create a vqat debug entry\n");
			return -EFAULT;
		}
		adf_vdcm_add_vqat(parent->vdcm, vqat);
	} else {
		dev_err(mdev_dev(mdev), "%s :Unsupported vqat type\n",
			__func__);
		return -ENOTSUPP;
	}

	return 0;
}

#if (KERNEL_VERSION(5, 11, 0) <= LINUX_VERSION_CODE)
static void adf_vdcm_vqat_request(struct mdev_device *mdev, unsigned int count)
{
	struct adf_vdcm_vqat *vqat = mdev_get_drvdata(mdev);

	if (vqat->req_trigger)
		eventfd_signal(vqat->req_trigger, 1);
	else
		dev_warn(mdev_dev(mdev),
			 "No client wants this event!");
}
#endif

static int adf_vdcm_vqat_remove(struct mdev_device *mdev)
{
	struct adf_vdcm_vqat *vqat = mdev_get_drvdata(mdev);
	struct adf_accel_dev *parent = vqat->parent;

	dev_info(mdev_dev(mdev), "%s : vqat %p\n", __func__, vqat);
	if (!parent) {
		dev_err(mdev_dev(mdev), "Unknown parent device\n");
		return -ENODEV;
	}
	adf_vdcm_remove_vqat(parent->vdcm, vqat);
	adf_vdcm_del_vqat_dbg(vqat);
	(*vqat->ops->destroy)(parent, vqat);

	return 0;
}

static int adf_vdcm_vqat_open(struct mdev_device *mdev)
{
	struct adf_vdcm_vqat *vqat = mdev_get_drvdata(mdev);
	int ret;

	dev_info(mdev_dev(mdev), "%s : mdev %p\n", __func__, mdev);
	adf_dev_get(vqat->parent);
	ret = (*vqat->ops->open)(vqat);
	if (ret)
		adf_dev_put(vqat->parent);

	return ret;
}

static void adf_vdcm_vqat_release(struct mdev_device *mdev)
{
	struct adf_vdcm_vqat *vqat = mdev_get_drvdata(mdev);

	dev_info(mdev_dev(mdev), "%s : mdev %p\n", __func__, mdev);
	vdcm_vqat_set_msi_trigger(vqat,
				  VFIO_IRQ_SET_DATA_NONE |
				  VFIO_IRQ_SET_ACTION_TRIGGER,
				  VFIO_PCI_MSI_IRQ_INDEX,
				  0, 0, NULL);
	(*vqat->ops->release)(vqat);
	adf_dev_put(vqat->parent);
}

static ssize_t adf_vdcm_vqat_mmio_rw(struct adf_vdcm_vqat *vqat, int bar,
				     u64 pos, char __user *buf, size_t count,
				     bool is_write)
{
	u32 attr = vqat->bar[bar].attr;
	u64 bar_sz = vqat->bar[bar].size;
	u16 val;

	dev_dbg(mdev_dev(vqat->mdev), "%s : %s %lu from %llu at bar %d\n",
		__func__, is_write ? "writing" : "reading",
		count, pos, bar);

	/* Sanity check */
	if ((attr & VFIO_REGION_INFO_FLAG_MMAP) &&
	    !(attr & VFIO_REGION_INFO_FLAG_CAPS)) {
		dev_err(mdev_dev(vqat->mdev), "Shouldn't be here\n");
		return -EINVAL;
	}

	if ((!(attr & VFIO_REGION_INFO_FLAG_WRITE)) && is_write) {
		dev_err(mdev_dev(vqat->mdev), "bar %d is not writable\n", bar);
		return -EINVAL;
	}

	if ((pos + count) > bar_sz) {
		dev_warn(mdev_dev(vqat->mdev),
			 "%s bar from pos %llu is out of size %llu\n",
			 is_write ? "writing" : "reading",
			 pos + count, bar_sz);
		return -EINVAL;
	}

	if (!vqat->vcfg || !vqat->vcfg->regmap)
		return -EINVAL;

	memcpy(&val, vqat->vcfg->regmap + PCI_COMMAND, 2);
	if (!(val & PCI_COMMAND_MEMORY)) {
		dev_err(mdev_dev(vqat->mdev), "Doesn't support memory space accesses\n");
		return -EINVAL;
	}

	if (is_write)
		return vqat->ops->mmio_write(vqat, bar, pos, buf, count);
	else
		return vqat->ops->mmio_read(vqat, bar, pos, buf, count);
}

static ssize_t adf_vdcm_vqat_rw(struct mdev_device *mdev, char __user *buf,
				size_t count, loff_t *ppos, bool is_write)
{
	struct adf_vdcm_vqat *vqat = mdev_get_drvdata(mdev);
	unsigned int index = VFIO_PCI_OFFSET_TO_INDEX(*ppos);
	u64 pos = *ppos & VFIO_PCI_OFFSET_MASK;
	ssize_t ret = -EINVAL;

	if (index >= VFIO_PCI_NUM_REGIONS) {
		dev_err(mdev_dev(mdev),
			"%s : read out of range, index is %d\n",
			__func__, index);
		return -EINVAL;
	}

	switch (index) {
	case VFIO_PCI_CONFIG_REGION_INDEX:
	{
		ssize_t remained = 0;
		dev_dbg(mdev_dev(mdev),
			"%s : %s %lu from %llu at cfg space\n",
			__func__, is_write ? "writing" : "reading",
			count, pos);
		/* Sanity check */
		if ((pos + count) > vqat->vcfg->size) {
			dev_dbg(mdev_dev(mdev),
				"%s cfg from pos %llu is out of size %d\n",
				is_write ? "writing" : "reading",
				pos + count, vqat->vcfg->size);
			if (pos >= vqat->vcfg->size) {
				if (!is_write)
					memset(buf, 0, count);
				return count;
			}
			remained = pos + count - vqat->vcfg->size;
			count = vqat->vcfg->size - pos;
		}
		if (is_write)
			ret = vqat->ops->cfg_write(vqat, pos, buf, count);
		else
			ret = vqat->ops->cfg_read(vqat, pos, buf, count);
		if (remained && !is_write && !ret)
			memset(buf + count, 0, remained);
		break;
	}
	case VFIO_PCI_BAR0_REGION_INDEX:
		ret = adf_vdcm_vqat_mmio_rw(vqat, ADF_VQAT_ETR_BAR,
					    pos, buf, count, is_write);
		break;
	case VFIO_PCI_BAR2_REGION_INDEX:
		ret = adf_vdcm_vqat_mmio_rw(vqat, ADF_VQAT_PMISC_BAR,
					    pos, buf, count, is_write);
		break;
	case VFIO_PCI_BAR4_REGION_INDEX:
		ret = adf_vdcm_vqat_mmio_rw(vqat, ADF_VQAT_EXT_BAR,
					    pos, buf, count, is_write);
		break;
	default:
		dev_info(mdev_dev(mdev),
			 "Unsupported regions %d for RW operations\n", index);
		ret = -EINVAL;
	}

	return ret == 0 ? count : ret;
}

static ssize_t adf_vdcm_vqat_read(struct mdev_device *mdev, char __user *buf,
				  size_t count, loff_t *ppos)
{
	ssize_t done = 0;
	int ret;

	while (count) {
		size_t filled;

		if (count >= 8 && !(*ppos % 8)) {
			u64 val;

			ret = adf_vdcm_vqat_rw(mdev, (char *)&val, sizeof(val),
					       ppos, false);
			if (ret <= 0)
				goto read_err;

			if (copy_to_user(buf, &val, sizeof(val)))
				goto read_err;

			filled = 8;
		} else if (count >= 4 && !(*ppos % 4)) {
			u32 val;

			ret = adf_vdcm_vqat_rw(mdev, (char *)&val, sizeof(val),
					       ppos, false);
			if (ret <= 0)
				goto read_err;

			if (copy_to_user(buf, &val, sizeof(val)))
				goto read_err;

			filled = 4;
		} else if (count >= 2 && !(*ppos % 2)) {
			u16 val;

			ret = adf_vdcm_vqat_rw(mdev, (char *)&val, sizeof(val),
					       ppos, false);
			if (ret <= 0)
				goto read_err;

			if (copy_to_user(buf, &val, sizeof(val)))
				goto read_err;

			filled = 2;
		} else {
			u8 val;

			ret = adf_vdcm_vqat_rw(mdev, &val, sizeof(val), ppos,
					       false);
			if (ret <= 0)
				goto read_err;

			if (copy_to_user(buf, &val, sizeof(val)))
				goto read_err;

			filled = 1;
		}

		count -= filled;
		done += filled;
		*ppos += filled;
		buf += filled;
	}

	return done;

read_err:
	return -EFAULT;
}

static ssize_t adf_vdcm_vqat_write(struct mdev_device *mdev,
				   const char __user *buf,
				   size_t count, loff_t *ppos)
{
	ssize_t done = 0;
	int ret;

	while (count) {
		size_t filled;

		if (count >= 8 && !(*ppos % 8)) {
			u64 val;

			if (copy_from_user(&val, buf, sizeof(val)))
				goto write_err;

			ret = adf_vdcm_vqat_rw(mdev, (char *)&val, sizeof(val),
					       ppos, true);
			if (ret <= 0)
				goto write_err;

			filled = 8;
		} else if (count >= 4 && !(*ppos % 4)) {
			u32 val;

			if (copy_from_user(&val, buf, sizeof(val)))
				goto write_err;

			ret = adf_vdcm_vqat_rw(mdev, (char *)&val, sizeof(val),
					       ppos, true);
			if (ret <= 0)
				goto write_err;

			filled = 4;
		} else if (count >= 2 && !(*ppos % 2)) {
			u16 val;

			if (copy_from_user(&val, buf, sizeof(val)))
				goto write_err;

			ret = adf_vdcm_vqat_rw(mdev, (char *)&val,
					       sizeof(val), ppos, true);
			if (ret <= 0)
				goto write_err;

			filled = 2;
		} else {
			u8 val;

			if (copy_from_user(&val, buf, sizeof(val)))
				goto write_err;

			ret = adf_vdcm_vqat_rw(mdev, &val, sizeof(val),
					       ppos, true);
			if (ret <= 0)
				goto write_err;

			filled = 1;
		}

		count -= filled;
		done += filled;
		*ppos += filled;
		buf += filled;
	}

	return done;
write_err:
	return -EFAULT;
}

static int adf_vdcm_vqat_mmap(struct mdev_device *mdev,
			      struct vm_area_struct *vma)
{
	struct adf_vdcm_vqat *vqat = mdev_get_drvdata(mdev);
	unsigned int index, bar_idx;
	u64 req_size, offset, pfn;
	bool is_io;

	index = vma->vm_pgoff >> (VFIO_PCI_OFFSET_SHIFT - PAGE_SHIFT);
	switch (index) {
	case VFIO_PCI_BAR0_REGION_INDEX:
		bar_idx = ADF_VQAT_ETR_BAR;
		break;
	case VFIO_PCI_BAR2_REGION_INDEX:
		bar_idx = ADF_VQAT_PMISC_BAR;
		break;
	case VFIO_PCI_BAR4_REGION_INDEX:
		bar_idx = ADF_VQAT_EXT_BAR;
		break;
	default:
		dev_err(mdev_dev(mdev),
			"%s :mdev %p, unsupported region %u for mmap\n",
			__func__, mdev, index);
		return -EINVAL;
	}

	offset = (vma->vm_pgoff << PAGE_SHIFT) & VFIO_PCI_OFFSET_MASK;
	if (vma->vm_end < vma->vm_start)
		return -EINVAL;

	if ((vma->vm_flags & VM_SHARED) == 0)
		return -EINVAL;

	req_size = vma->vm_end - vma->vm_start;
	/* Sanity check */
	if ((offset + req_size < offset) ||
	    (offset + req_size > vqat->bar[bar_idx].size)) {
		dev_err(mdev_dev(mdev), "Out of range mmap\n");
		return -EOVERFLOW;
	}

	vma->vm_private_data = vqat;
	/* TODO: callback for vma close? */
	/* vma->vm_ops = &adf_vdcm_mmap_operation; */
	pfn = adf_vdcm_vqat_lookup_mmap_space(&vqat->bar[bar_idx],
					      offset, req_size, &is_io) >>
					      PAGE_SHIFT;
	if (!pfn) {
		dev_err(mdev_dev(mdev),
			"Unable to find mapped pfn in bar %d at ofs 0x%llx\n",
			bar_idx, offset);
		return -EINVAL;
	}

	if (is_io) {
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
		vma->vm_pgoff = pfn;
	}
	dev_dbg(mdev_dev(mdev),
		"Page%llu with %llu bytes in bar%d mappped to 0x%llx\n",
		pfn, req_size, bar_idx, offset);

	return remap_pfn_range(vma, vma->vm_start, pfn, req_size,
			       vma->vm_page_prot);
}

static int vdcm_vqat_get_device_info(struct adf_vdcm_vqat *vqat,
				     struct vfio_device_info *info)
{
	info->flags = VFIO_DEVICE_FLAGS_PCI | VFIO_DEVICE_FLAGS_RESET;
	info->num_regions = VFIO_PCI_NUM_REGIONS;
	info->num_irqs = VFIO_PCI_NUM_IRQS;

	return 0;
}

static int vdcm_vqat_get_region_info(struct adf_vdcm_vqat *vqat,
				     struct vfio_region_info *info,
				     void __user *user_cap)
{
	struct vfio_info_cap caps = { .buf = NULL, .size = 0 };
	struct vfio_region_info_cap_sparse_mmap *sparse;
	struct adf_vqat_bar *vbar = NULL;

	if (info->index >= VFIO_PCI_NUM_REGIONS)
		return -EINVAL;

	switch (info->index) {
	case VFIO_PCI_CONFIG_REGION_INDEX:
		info->offset = VFIO_PCI_INDEX_TO_OFFSET(info->index);
		info->size = vqat->vcfg->size;
		info->flags = VFIO_REGION_INFO_FLAG_READ |
			VFIO_REGION_INFO_FLAG_WRITE;
		break;
	case VFIO_PCI_BAR0_REGION_INDEX:
		vbar = &vqat->bar[ADF_VQAT_ETR_BAR];
		info->offset = VFIO_PCI_INDEX_TO_OFFSET(info->index);
		info->size = vbar->size;
		info->flags = vbar->attr;
		break;
	case VFIO_PCI_BAR2_REGION_INDEX:
		vbar = &vqat->bar[ADF_VQAT_PMISC_BAR];
		info->offset = VFIO_PCI_INDEX_TO_OFFSET(info->index);
		info->size = vbar->size;
		info->flags = vbar->attr;
		break;
	case VFIO_PCI_BAR4_REGION_INDEX:
		vbar = &vqat->bar[ADF_VQAT_EXT_BAR];
		info->offset = VFIO_PCI_INDEX_TO_OFFSET(info->index);
		info->size = vbar->size;
		info->flags = vbar->attr;
		break;
	default:
		info->offset = VFIO_PCI_INDEX_TO_OFFSET(info->index);
		info->size = 0;
		info->flags = 0;
		break;
	}

	if ((info->flags & VFIO_REGION_INFO_FLAG_CAPS) &&
	    (vbar && vbar->num_sub_mmap_area > 0)) {
		int i;
		size_t alloc_sz = sizeof(*sparse) +
			vbar->num_sub_mmap_area * sizeof(*sparse->areas);

		sparse = kzalloc(alloc_sz, GFP_KERNEL);
		if (!sparse)
			return -ENOMEM;
		for (i = 0; i < vbar->num_sub_mmap_area; i++) {
			sparse->areas[i].offset =
				vbar->sub_mmap_areas[i].bar_ofs;
			sparse->areas[i].size =
				vbar->sub_mmap_areas[i].size;
		}
		sparse->nr_areas =  vbar->num_sub_mmap_area;
#if (KERNEL_VERSION(4, 15, 0) < LINUX_VERSION_CODE)
		sparse->header.id = VFIO_REGION_INFO_CAP_SPARSE_MMAP;
		sparse->header.version = 1;
		vfio_info_add_capability(&caps,
					 &sparse->header, alloc_sz);
#else
		vfio_info_add_capability(&caps,
					 VFIO_REGION_INFO_CAP_SPARSE_MMAP,
					 sparse);
#endif

		dev_dbg(mdev_dev(vqat->mdev),
			"%s: cap size is %ld, info size is %d\n",
			__func__, caps.size, info->argsz);
		if (caps.size) {
			if (info->argsz < sizeof(*info) + caps.size) {
				info->argsz = sizeof(*info) + caps.size;
				info->cap_offset = 0;
			} else {
				vfio_info_cap_shift(&caps, sizeof(*info));
				if (copy_to_user((void __user *)user_cap,
						 caps.buf, caps.size)) {
					kfree(caps.buf);
					kfree(sparse);
					return -EFAULT;
				}
				info->cap_offset = sizeof(*info);
			}
			kfree(caps.buf);
		}
		kfree(sparse);
	}

	return 0;
}

static inline int adf_vdcm_vqat_get_irq_count(struct adf_vdcm_vqat *vqat,
					      int irq_type)
{
	/* We have ADF_VQAT_IRQ_MAX IRQ events, but only 1 REQ event */
	if (irq_type == VFIO_PCI_MSIX_IRQ_INDEX)
		return vqat->irqs;
	else if (irq_type == VFIO_PCI_REQ_IRQ_INDEX)
		return 1;
	else
		return 0;
}

static int vdcm_vqat_get_irq_info(struct adf_vdcm_vqat *vqat,
				  struct vfio_irq_info *info)
{
	info->count = adf_vdcm_vqat_get_irq_count(vqat, info->index);
	if (info->count == 0)
		return -EINVAL;

	info->flags = VFIO_IRQ_INFO_EVENTFD;
	info->flags |= VFIO_IRQ_INFO_NORESIZE;

	return 0;
}

static int adf_vdcm_vqat_set_eventfd(struct adf_vdcm_vqat *vqat,
				     struct adf_vqat_irq_ctx *ctx,
				     int fd)
{
	struct eventfd_ctx *trigger = adf_vqat_irq_ctx_trigger(ctx);
	int ret;

	dev_dbg(mdev_dev(vqat->mdev),
		"%s: setting trigger fd %d for irq %s\n",
		__func__, fd, adf_vqat_irq_ctx_name(ctx));

	if (trigger) {
		if (ctx->set_irq)
			ctx->set_irq(ctx, ADF_VQAT_IRQ_DISABLE);
		if (ctx->producer)
			irq_bypass_unregister_producer(ctx->producer);
		if (trigger) {
			adf_vqat_irq_ctx_set_trigger(ctx, NULL);
			eventfd_ctx_put(trigger);
		}
	}
	if (fd < 0)
		return 0;

	trigger = eventfd_ctx_fdget(fd);
	if (IS_ERR(trigger)) {
		dev_err(mdev_dev(vqat->mdev),
			"%s: eventfd_ctx_fdget failed\n",
			__func__);
		return PTR_ERR(trigger);
	}
	adf_vqat_irq_ctx_set_trigger(ctx, trigger);
	if (ctx->producer) {
		ctx->producer->token = trigger;
		ret = irq_bypass_register_producer(ctx->producer);
		if (unlikely(ret))
			dev_warn(mdev_dev(vqat->mdev),
				 "error %d for irq bypass producer regist\n",
				 ret);
	}
	if (ctx->set_irq)
		ctx->set_irq(ctx, ADF_VQAT_IRQ_ENABLE);

	return 0;
}

static inline int adf_vdcm_vqat_clear_eventfd(struct adf_vdcm_vqat *vqat,
					      struct adf_vqat_irq_ctx *ctx)
{
	return adf_vdcm_vqat_set_eventfd(vqat, ctx, -1);
}

static inline void adf_vdcm_vqat_trig_eventfd(struct adf_vdcm_vqat *vqat,
					      struct adf_vqat_irq_ctx *ctx)
{
	struct eventfd_ctx *trigger = adf_vqat_irq_ctx_trigger(ctx);

	if (trigger)
		eventfd_signal(trigger, 1);
}

static int vdcm_vqat_set_msi_trigger(struct adf_vdcm_vqat *vqat,
				     u32 flags, u32 index,
				     u32 start, u32 count,
				     void *data)
{
	struct adf_vqat_irq_ctx *ctx;
	int i;
	u32 *fds = data;

	dev_dbg(mdev_dev(vqat->mdev),
		"%s: index=%u, start=%u, count=%u, flag=0x%x\n",
		__func__, index, start, count, flags);

	if (start >= ADF_VQAT_IRQ_MAX || count > ADF_VQAT_IRQ_MAX ||
	    (start > ADF_VQAT_IRQ_MAX - count)) {
		dev_err(mdev_dev(vqat->mdev),
			"%s: invalid irq (start=%u, count=%u)\n",
			__func__, start, count);
		return -EINVAL;
	}

	if (!count && (flags & VFIO_IRQ_SET_DATA_NONE)) {
		for (i = 0; i < ADF_VQAT_IRQ_MAX; i++) {
			/* Clear the interrupt */
			ctx = &vqat->irq_ctx[i];
			adf_vdcm_vqat_clear_eventfd(vqat, ctx);
		}
		return 0;
	}

	for (i = 0; i < count; i++) {
		ctx = &vqat->irq_ctx[start + i];
		if (flags & VFIO_IRQ_SET_DATA_NONE) {
			/* Trig the interrupt */
			adf_vdcm_vqat_trig_eventfd(vqat, ctx);
		} else if (flags & VFIO_IRQ_SET_DATA_EVENTFD) {
			/* Set the interrupt */
			if (adf_vdcm_vqat_set_eventfd(vqat, ctx,
						      fds[i]) < 0) {
				while (--i >= 0) {
					ctx = &vqat->irq_ctx[start + i];
					adf_vdcm_vqat_clear_eventfd(vqat, ctx);
				}
				return -EINVAL;
			}
		} else {
			dev_err(mdev_dev(vqat->mdev),
				"%s: invalid flag 0x%x\n",
				__func__, flags);
			return -EINVAL;
		}
	}

	return 0;
}

static int vdcm_vqat_set_irqs(struct adf_vdcm_vqat *vqat, u32 flags,
			      u32 index, u32 start,
			      u32 count, void *data)
{
	int ret = 0;

	if (index != VFIO_PCI_MSIX_IRQ_INDEX) {
		dev_err(mdev_dev(vqat->mdev),
			"%s:unsupported irq type %u for vqat\n",
			__func__, index);
		return -EINVAL;
	}

	switch (flags & VFIO_IRQ_SET_ACTION_TYPE_MASK) {
	case VFIO_IRQ_SET_ACTION_MASK:
	case VFIO_IRQ_SET_ACTION_UNMASK:
		/* TODO: Need masking support exported */
		break;
	case VFIO_IRQ_SET_ACTION_TRIGGER:
		ret = vdcm_vqat_set_msi_trigger(vqat, flags, index,
						start, count, data);

		break;
	}

	return ret;
}

static int vdcm_set_ctx_trigger_single(struct eventfd_ctx **ctx,
				       u32 count, u32 flags,
				       void *data)
{
	if (flags & VFIO_IRQ_SET_DATA_NONE) {
		if (*ctx) {
			eventfd_ctx_put(*ctx);
			*ctx = NULL;
			return 0;
		}
	} else if (flags & VFIO_IRQ_SET_DATA_BOOL) {
		u8 trigger;

		if (!count)
			return -EINVAL;

		trigger = *(u8 *)data;
		if (trigger && *ctx)
			eventfd_signal(*ctx, 1);

		return 0;
	} else if (flags & VFIO_IRQ_SET_DATA_EVENTFD) {
		s32 fd;

		if (!count)
			return -EINVAL;

		fd = *(s32 *)data;
		if (fd == -1) {
			if (*ctx)
				eventfd_ctx_put(*ctx);
			*ctx = NULL;
		} else if (fd >= 0) {
			struct eventfd_ctx *efdctx;

			efdctx = eventfd_ctx_fdget(fd);
			if (IS_ERR(efdctx))
				return PTR_ERR(efdctx);

			if (*ctx)
				eventfd_ctx_put(*ctx);

			*ctx = efdctx;
		}
		return 0;
	}

	return -EINVAL;
}

static long adf_vdcm_vqat_ioctl(struct mdev_device *mdev, unsigned int cmd,
				unsigned long arg)
{
	struct adf_vdcm_vqat *vqat = mdev_get_drvdata(mdev);
	unsigned long minsz;
	long ret = 0;

	switch (cmd) {
	case VFIO_DEVICE_GET_INFO:
	{
		struct vfio_device_info info;

		dev_dbg(mdev_dev(mdev), "%s : DEV_GET_INFO for vqat %p\n",
			__func__, vqat);
		minsz = offsetofend(struct vfio_device_info, num_irqs);
		if (copy_from_user(&info, (void __user *)arg, minsz))
			return -EFAULT;

		if (info.argsz < minsz)
			return -EINVAL;

		if (vdcm_vqat_get_device_info(vqat, &info))
			return -EINVAL;

		if (copy_to_user((void __user *)arg, &info, minsz))
			return -EFAULT;

		return 0;
	}
	case VFIO_DEVICE_GET_REGION_INFO:
	{
		struct vfio_region_info info;
		void __user *cap;

		minsz = offsetofend(struct vfio_region_info, offset);
		if (copy_from_user(&info, (void __user *)arg, minsz))
			return -EFAULT;

		if (info.argsz < minsz)
			return -EINVAL;
		else if (info.argsz > minsz)
			cap = (void __user *)(arg + sizeof(info));
		else
			cap = NULL;

		ret = vdcm_vqat_get_region_info(vqat, &info, cap);
		if (ret)
			return ret;

		if (copy_to_user((void __user *)arg, &info, minsz))
			return -EFAULT;

		return 0;
	}
	case VFIO_DEVICE_GET_IRQ_INFO:
	{
		struct vfio_irq_info info;

		minsz = offsetofend(struct vfio_irq_info, count);
		if (copy_from_user(&info, (void __user *)arg, minsz))
			return -EFAULT;

		if (info.argsz < minsz || info.index >= VFIO_PCI_NUM_IRQS)
			return -EINVAL;

		ret = vdcm_vqat_get_irq_info(vqat, &info);
		if (ret)
			return ret;

		if (copy_to_user((void __user *)arg, &info, minsz))
			return -EFAULT;

		return 0;
	}
	case VFIO_DEVICE_SET_IRQS:
	{
		struct vfio_irq_set hdr;
		u8 *data = NULL;
		size_t data_size = 0;

		minsz = offsetofend(struct vfio_irq_set, count);
		if (copy_from_user(&hdr, (void __user *)arg, minsz))
			return -EFAULT;

		if (!(hdr.flags & VFIO_IRQ_SET_DATA_NONE)) {
			int max = adf_vdcm_vqat_get_irq_count(vqat, hdr.index);

			ret = vfio_set_irqs_validate_and_prepare
				(&hdr, max, VFIO_PCI_NUM_IRQS, &data_size);
			if (ret || data_size == 0) {
				dev_err(mdev_dev(vqat->mdev),
					"%s: failed with %ld\n", __func__, ret);
				return -EINVAL;
			}
			data = memdup_user((void __user *)(arg + minsz),
					   data_size);
			if (IS_ERR(data))
				return PTR_ERR(data);
		}

		mutex_lock(&vqat->vdev_lock);
		if (hdr.index == VFIO_PCI_REQ_IRQ_INDEX)
			ret = vdcm_set_ctx_trigger_single(&vqat->req_trigger,
							  hdr.count,
							  hdr.flags,
							  data);
		else
			ret = vdcm_vqat_set_irqs(vqat, hdr.flags, hdr.index,
						 hdr.start, hdr.count, data);
		mutex_unlock(&vqat->vdev_lock);
		kfree(data);

		return ret;
	}
	case VFIO_DEVICE_RESET:
		if (vqat->ops->reset)
			return vqat->ops->reset(vqat);
		dev_err(mdev_dev(vqat->mdev), "Unsupported reset op\n");
		return -EINVAL;
	default:
		dev_err(mdev_dev(vqat->mdev), "Unsupported ioctl op\n");
		return -EINVAL;
	}

	return 0;
}

#if KERNEL_VERSION(5, 13, 0) <= LINUX_VERSION_CODE
static ssize_t
available_instances_show(struct mdev_type *mtype,
			 struct mdev_type_attribute *attr, char *buf)
#else
static ssize_t
available_instances_show(struct kobject *kobj, struct device *dev, char *buf)
#endif
{
#if KERNEL_VERSION(5, 13, 0) <= LINUX_VERSION_CODE
	struct device *dev = mtype_get_parent_dev(mtype);
#endif
	struct pci_dev *pdev = container_of(dev, struct pci_dev, dev);
	struct adf_accel_dev *accel_dev = NULL;
	struct adf_vdcm_vqat_type *vqat_type;

#if KERNEL_VERSION(5, 13, 0) <= LINUX_VERSION_CODE
	vqat_type = adf_vdcm_lookup_vqat_type(dev, mtype);
#else
	vqat_type = adf_vdcm_lookup_vqat_type(dev, kobject_name(kobj));
#endif

	if (vqat_type) {
		accel_dev = adf_devmgr_pci_to_accel_dev(pdev);
		if (accel_dev) {
			struct adf_vdcm_vqat_ops *ops =
				adf_vqat_class_ops(vqat_type->class);
			int num;

			if (!ops) {
				dev_err(dev,
					"No operation is found for type %d\n",
					adf_vqat_class_type(vqat_type->class));
				return 0;
			}
			num = ops->class_handler(vqat_type->class,
						 accel_dev,
						 ADF_VDCM_GET_NUM_AVAIL_INSTS,
						 NULL);
			return snprintf(buf, PAGE_SIZE, "%d\n", num);
		}
		dev_err(dev, "No parent device is found\n");
		return 0;
	}
	dev_err(dev, "Unsupported type yet\n");
	return 0;
}

MDEV_TYPE_ATTR_RO(available_instances);

#if KERNEL_VERSION(5, 13, 0) <= LINUX_VERSION_CODE
static ssize_t
name_show(struct mdev_type *mtype, struct mdev_type_attribute *attr, char *buf)
#else
static ssize_t
name_show(struct kobject *kobj, struct device *dev, char *buf)
#endif
{
#if KERNEL_VERSION(5, 13, 0) <= LINUX_VERSION_CODE
	struct adf_vdcm_vqat_type *vqat_type;
	struct device *par_dev = mtype_get_parent_dev(mtype);

	vqat_type = adf_vdcm_lookup_vqat_type(par_dev, mtype);
	if (!vqat_type || !vqat_type->ag)
		return -EOPNOTSUPP;

	return snprintf(buf, PAGE_SIZE, "%s-%s\n", dev_driver_string(par_dev),
			vqat_type->ag->name);
#else
	return snprintf(buf, PAGE_SIZE, "%s\n", kobject_name(kobj));
#endif
}

MDEV_TYPE_ATTR_RO(name);

#if KERNEL_VERSION(5, 13, 0) <= LINUX_VERSION_CODE
static ssize_t device_api_show(struct mdev_type *mtype,
			       struct mdev_type_attribute *attr, char *buf)
#else
static ssize_t device_api_show(struct kobject *kobj, struct device *dev,
			       char *buf)
#endif
{
	return snprintf(buf, PAGE_SIZE, "%s\n", VFIO_DEVICE_API_PCI_STRING);
}
MDEV_TYPE_ATTR_RO(device_api);

static struct attribute *qat_mdev_types_attrs[] = {
	&mdev_type_attr_name.attr,
	&mdev_type_attr_device_api.attr,
	&mdev_type_attr_available_instances.attr,
	NULL,
};

static struct attribute_group qat_vqat_vf_type_group = {
	.name  = QAT_VQAT_TYPE_VF_NAME,
	.attrs = qat_mdev_types_attrs,
};

static struct attribute_group *qat_vqat_vf_type_groups[] = {
	&qat_vqat_vf_type_group,
	NULL,
};

static struct mdev_parent_ops qat_vqat_vf_ops = {
	.supported_type_groups = qat_vqat_vf_type_groups,
	.create = adf_vdcm_vqat_create,
	.remove = adf_vdcm_vqat_remove,
#if KERNEL_VERSION(5, 15, 0) <= LINUX_VERSION_CODE
	.open_device = adf_vdcm_vqat_open,
	.close_device = adf_vdcm_vqat_release,
#else
	.open = adf_vdcm_vqat_open,
	.release = adf_vdcm_vqat_release,
#endif
	.read = adf_vdcm_vqat_read,
	.write = adf_vdcm_vqat_write,
	.mmap = adf_vdcm_vqat_mmap,
	.ioctl = adf_vdcm_vqat_ioctl,
};

static struct attribute_group qat_vqat_adi_sym_type_group = {
	.name  = QAT_VQAT_TYPE_ADI_SYM_NAME,
	.attrs = qat_mdev_types_attrs,
};

static struct attribute_group qat_vqat_adi_asym_type_group = {
	.name  = QAT_VQAT_TYPE_ADI_ASYM_NAME,
	.attrs = qat_mdev_types_attrs,
};

static struct attribute_group qat_vqat_adi_dc_type_group = {
	.name  = QAT_VQAT_TYPE_ADI_DC_NAME,
	.attrs = qat_mdev_types_attrs,
};

static struct attribute_group *qat_vqat_adi_type_groups[] = {
	&qat_vqat_adi_sym_type_group,
	&qat_vqat_adi_asym_type_group,
	&qat_vqat_adi_dc_type_group,
	NULL,
};

static struct mdev_parent_ops qat_vqat_adi_ops  = {
	.supported_type_groups = qat_vqat_adi_type_groups,
	.create = adf_vdcm_vqat_create,
	.remove = adf_vdcm_vqat_remove,
#if (KERNEL_VERSION(5, 11, 0) <= LINUX_VERSION_CODE)
	.request = adf_vdcm_vqat_request,
#endif
#if KERNEL_VERSION(5, 15, 0) <= LINUX_VERSION_CODE
	.open_device = adf_vdcm_vqat_open,
	.close_device = adf_vdcm_vqat_release,
#else
	.open = adf_vdcm_vqat_open,
	.release = adf_vdcm_vqat_release,
#endif
	.read = adf_vdcm_vqat_read,
	.write = adf_vdcm_vqat_write,
	.mmap = adf_vdcm_vqat_mmap,
	.ioctl = adf_vdcm_vqat_ioctl,
};

static void adf_vdcm_init_vqat_ops_table(void)
{
	struct adf_vdcm_vqat_type *types = adf_vqat_types;

	types[QAT_VQAT_TYPE_VF].ag = &qat_vqat_vf_type_group;
	types[QAT_VQAT_ADI_RP_SYM].ag = &qat_vqat_adi_sym_type_group;
	types[QAT_VQAT_ADI_RP_ASYM].ag = &qat_vqat_adi_asym_type_group;
	types[QAT_VQAT_ADI_RP_DC].ag = &qat_vqat_adi_dc_type_group;
}

static void adf_vdcm_cleanup_vqat_ops_table(void)
{
}

static inline
struct adf_vdcm_vqat_type *adf_vdcm_vqat_type_by_id(enum vqat_type type_id)
{
	if (type_id >= QAT_VQAT_TYPES_MAX)
		return NULL;
	return &adf_vqat_types[type_id];
}

#if KERNEL_VERSION(5, 13, 0) <= LINUX_VERSION_CODE
static struct adf_vdcm_vqat_type *adf_vdcm_lookup_vqat_type(struct device *dev,
							    struct mdev_type *mtype)
{
	struct adf_accel_dev *parent;
	struct pci_dev *pdev;
	struct attribute_group *group;
	unsigned int group_id;
	int i;

	pdev = container_of(dev, struct pci_dev, dev);
	group_id = mtype_get_type_group_id(mtype);
	parent = adf_devmgr_pci_to_accel_dev(pdev);

	if (!parent || !parent->vdcm || !parent->vdcm->vdcm_ops ||
	    !parent->vdcm->vdcm_ops->supported_type_groups[group_id])
		return NULL;

	group = parent->vdcm->vdcm_ops->supported_type_groups[group_id];
	for (i = 0; i < QAT_VQAT_TYPES_MAX; i++) {
		if (!strncmp(group->name, adf_vqat_types[i].ag->name,
			     QAT_VQAT_TYPE_NAME_MAX_LEN))
			return &adf_vqat_types[i];
	}

	return NULL;
}
#else
static struct adf_vdcm_vqat_type *adf_vdcm_lookup_vqat_type(struct device *dev,
							    const char *name)
{
	int i;
	char name1[QAT_VQAT_TYPE_NAME_MAX_LEN];

	for (i = 0; i < QAT_VQAT_TYPES_MAX; i++) {
		snprintf(name1, QAT_VQAT_TYPE_NAME_MAX_LEN, "%s-%s",
			 dev_driver_string(dev), adf_vqat_types[i].ag->name);

		if (!strncmp(name, name1, QAT_VQAT_TYPE_NAME_MAX_LEN))
			return &adf_vqat_types[i];
	}

	return NULL;
}
#endif

int adf_vdcm_register_vqat_class(struct adf_vqat_class *class)
{
	struct adf_vdcm_vqat_type *types = adf_vqat_types;
	enum vqat_type type = adf_vqat_class_type(class);

	if (type >= QAT_VQAT_TYPES_MAX) {
		pr_err("Invalid type %d when registering vqat ops\n", type);
		return -EINVAL;
	}
	types[type].class = class;
	mutex_init(&class->class_lock);

	return 0;
}

void adf_vdcm_unregister_vqat_class(struct adf_vqat_class *class)
{
	struct adf_vdcm_vqat_type *types = adf_vqat_types;
	enum vqat_type type = adf_vqat_class_type(class);

	if (type >= QAT_VQAT_TYPES_MAX) {
		pr_err("Invalid type %d when unregistering vqat ops\n", type);
		return;
	}
	mutex_destroy(&class->class_lock);
	types[type].class = NULL;
}

static inline bool adf_vqat_type_exclude_sriov(enum vqat_type type)
{
	if (type >= QAT_VQAT_ADI_RP_MIN && type < QAT_VQAT_ADI_RP_MAX)
		return true;
	else
		return false;
}

static void adf_vdcm_deactivate_vqat_types(struct adf_vdcm_ctx_blk *vdcm,
					   struct  adf_accel_dev *accel_dev)
{
	struct adf_vdcm_type *type;

	list_for_each_entry(type, &vdcm->vqat_types, list)
		type->activated = 0;
}

static int adf_vdcm_activate_vqat_types(struct adf_vdcm_ctx_blk *vdcm,
					struct adf_accel_dev *accel_dev)
{
	struct adf_vdcm_type *type;
	int num = 0;
	bool sriov_enabled = adf_sriov_enabled(accel_dev);

	list_for_each_entry(type, &vdcm->vqat_types, list) {
		enum vqat_type t = adf_vqat_class_type(type->vqat_type->class);
		if (!sriov_enabled ||
		    !adf_vqat_type_exclude_sriov(t)) {
			type->activated = 1;
			num++;
		}
	}

	return num;
}

static inline void adf_vdcm_activate(struct adf_vdcm_ctx_blk *vdcm, int flag)
{
	vdcm->active = flag;
}

static inline int adf_vdcm_activated(struct adf_vdcm_ctx_blk *vdcm)
{
	return vdcm->active;
}

static void adf_vdcm_deactivate_parent(struct adf_vdcm_ctx_blk *vdcm,
				       struct adf_accel_dev *accel_dev)
{
	struct device *dev = &GET_DEV(accel_dev);

	if (!adf_vdcm_activated(vdcm))
		return;

	adf_vdcm_activate(vdcm, 0);
	mdev_unregister_device(dev);
	debugfs_remove(vdcm->debugfs_vdcm);
	adf_vdcm_deactivate_vqat_types(vdcm, accel_dev);
}

static int adf_vdcm_activate_parent(struct adf_vdcm_ctx_blk *vdcm,
				    struct adf_accel_dev *accel_dev)
{
	struct device *dev = &GET_DEV(accel_dev);
	int ret;

	if (adf_vdcm_activated(vdcm)) {
		dev_warn(dev, "vdcm is already activated!\n");
		return 0;
	}

	if (!adf_vdcm_activate_vqat_types(vdcm, accel_dev)) {
		dev_info(dev, "No supported vqat types were activated!\n");
		return 0;
	}

	if (accel_dev->debugfs_dir) {
		vdcm->debugfs_vdcm =
				debugfs_create_dir("vqat",
						   accel_dev->debugfs_dir);
		if (!vdcm->debugfs_vdcm) {
			dev_err(dev, "Failed to register vqat debugfs\n");
			goto err_debugfs_vdcm;
		}
	}

	ret = mdev_register_device(dev, vdcm->vdcm_ops);
	if (ret < 0) {
		dev_err(dev, "Failed to register mdev ops with error %d\n",
			ret);
		goto err_vdcm_register;
	}
	adf_vdcm_activate(vdcm, 1);

	return 0;

err_vdcm_register:
	debugfs_remove(vdcm->debugfs_vdcm);
err_debugfs_vdcm:
	adf_vdcm_deactivate_vqat_types(vdcm, accel_dev);

	return -EFAULT;
}

void adf_vdcm_unregister_vqat_parent(struct adf_vdcm_ctx_blk *vdcm,
				     struct adf_accel_dev *accel_dev)
{
	if (vdcm) {
		adf_vdcm_cleanup_compat_manager(accel_dev, &vdcm->cm);
		adf_vdcm_deactivate_parent(vdcm, accel_dev);
		adf_vdcm_type_unregister_all(vdcm, accel_dev);
		mutex_destroy(&vdcm->vqat_types_lock);
		mutex_destroy(&vdcm->vqats_lock);
		kfree(vdcm);
	}
}
EXPORT_SYMBOL(adf_vdcm_unregister_vqat_parent);

struct adf_vdcm_ctx_blk *
adf_vdcm_register_vqat_parent(struct adf_accel_dev *accel_dev,
			      int total, enum vqat_type types[])
{
	struct adf_vdcm_ctx_blk *vdcm = accel_dev->vdcm;
	struct device *dev = &accel_to_pci_dev(accel_dev)->dev;
	struct mdev_parent_ops *vdcm_ops = NULL;
	struct adf_vdcm_vqat_type *vqat_type;
	int i;

	if (vdcm) {
		dev_info(dev, "Re-registering vqat parent\n");
		adf_vdcm_unregister_vqat_parent(vdcm, accel_dev);
	}

	vdcm = kzalloc(sizeof(*vdcm), GFP_KERNEL);
	if (!vdcm)
		return NULL;

	/* Init lock and list */
	mutex_init(&vdcm->vqats_lock);
	INIT_LIST_HEAD(&vdcm->vqats);
	mutex_init(&vdcm->vqat_types_lock);
	INIT_LIST_HEAD(&vdcm->vqat_types);

	for (i = 0; i < total; i++) {
		if (types[i] >= QAT_VQAT_TYPES_MAX)
			continue;
		if (types[i] == QAT_VQAT_TYPE_VF)
			vdcm_ops = &qat_vqat_vf_ops;
		else
			vdcm_ops = &qat_vqat_adi_ops;
		vqat_type = adf_vdcm_vqat_type_by_id(types[i]);
		if (!vqat_type)
			continue;
		if (adf_vdcm_type_register(vdcm, accel_dev, vqat_type) < 0)
			goto err_vdcm_ops;
	}

	if (!vdcm_ops) {
		dev_err(dev, "Invalid registered vqat types\n");
		goto err_vdcm_ops;
	}
	vdcm->vdcm_ops = vdcm_ops;

	if (adf_vdcm_init_compat_manager(accel_dev, &vdcm->cm)) {
		dev_err(dev, "Failed to init vdcm compat manager\n");
		goto err_vdcm_ops;
	}
	return vdcm;

err_vdcm_ops:
	adf_vdcm_type_unregister_all(vdcm, accel_dev);
	mutex_destroy(&vdcm->vqat_types_lock);
	mutex_destroy(&vdcm->vqats_lock);
	kfree(vdcm);
	return NULL;
}
EXPORT_SYMBOL(adf_vdcm_register_vqat_parent);

static int
adf_vdcm_notify_vqat_event(struct adf_vdcm_ctx_blk *vdcm,
			   void (*notify)(struct adf_iov_vx_agent *,
					  struct adf_vdcm_vqat *vqat))
{
	struct adf_vdcm_vqat *vqat;

	mutex_lock(&vdcm->vqats_lock);
	list_for_each_entry(vqat, &vdcm->vqats, list) {
		if (!vqat->iov_agent.init)
			continue;
		(*notify)(&vqat->iov_agent, vqat);
	}
	mutex_unlock(&vdcm->vqats_lock);

	return 0;
}

static int adf_vdcm_event_init(struct adf_accel_dev *accel_dev)
{
	return 0;
}

static int adf_vdcm_event_start(struct adf_accel_dev *accel_dev)
{
	struct adf_vdcm_ctx_blk *vdcm = accel_dev->vdcm;
	struct device *dev = &GET_DEV(accel_dev);

	if (!vdcm)
		return 0;

	dev_dbg(dev, "event start received at qat vdcm\n");
	if (adf_vdcm_activate_parent(vdcm, accel_dev) < 0) {
		dev_err(dev, "Failed to activate qat vdcm parent\n");
		return -EFAULT;
	}
	return 0;
}

static int adf_vdcm_event_stop(struct adf_accel_dev *accel_dev)
{
	struct adf_vdcm_ctx_blk *vdcm = accel_dev->vdcm;
	struct device *dev = &GET_DEV(accel_dev);

	if (!vdcm || !adf_vdcm_activated(vdcm))
		return 0;

	dev_dbg(dev, "event stop received at qat vdcm\n");
	adf_vdcm_notify_vqat_event(vdcm, adf_vdcm2vqat_restarting);
	/* Leave the housekeeping to the framework */
	adf_vdcm_deactivate_parent(vdcm, accel_dev);

	return 0;
}

static int adf_vdcm_event_shutdown(struct adf_accel_dev *accel_dev)
{
	return 0;
}

static int adf_vdcm_event_fatal_err(struct adf_accel_dev *accel_dev)
{
	struct adf_vdcm_ctx_blk *vdcm = accel_dev->vdcm;
	struct device *dev = &GET_DEV(accel_dev);

	if (!vdcm || !adf_vdcm_activated(vdcm))
		return 0;

	dev_dbg(dev, "event fatal_error received at qat vdcm\n");

	return adf_vdcm_notify_vqat_event(vdcm, adf_vdcm2vqat_fatal_error);
}

static int adf_vdcm_event_restarting(struct adf_accel_dev *accel_dev)
{
	return 0;
}

static int adf_vdcm_event_handler(struct adf_accel_dev *accel_dev,
				  enum adf_event event)
{
	int ret = 0;

	switch (event) {
	case ADF_EVENT_INIT:
		ret = adf_vdcm_event_init(accel_dev);
		break;
	case ADF_EVENT_RESTARTING:
		ret = adf_vdcm_event_restarting(accel_dev);
		break;
	case ADF_EVENT_RESTARTED:
		break;
	case ADF_EVENT_START:
		ret = adf_vdcm_event_start(accel_dev);
		break;
	case ADF_EVENT_STOP:
		ret = adf_vdcm_event_stop(accel_dev);
		break;
	case ADF_EVENT_ERROR:
		ret = adf_vdcm_event_fatal_err(accel_dev);
		break;
	case ADF_EVENT_SHUTDOWN:
		ret = adf_vdcm_event_shutdown(accel_dev);
		break;
	default:
		break;
	}

	return ret;
}

int adf_vdcm_init(void)
{
	memset(&adf_vdcm_srv_hndl, 0, sizeof(adf_vdcm_srv_hndl));
	adf_vdcm_srv_hndl.event_hld = adf_vdcm_event_handler;
	adf_vdcm_srv_hndl.name = "adf_vdcm_event_handler";
	if (adf_service_register(&adf_vdcm_srv_hndl) < 0) {
		pr_err("Failed to register service for vdcm\n");
		return -EINVAL;
	}

	adf_vdcm_init_vqat_ops_table();

	if (adf_vdcm_init_vqat_adi()) {
		pr_err("Failed to register vqat-adi\n");
		goto err_init_vqat_adi;
	}

	return 0;

err_init_vqat_adi:
	adf_service_unregister(&adf_vdcm_srv_hndl);

	return -EINVAL;
}

int adf_vdcm_cleanup(void)
{
	adf_vdcm_cleanup_vqat_adi();
	adf_vdcm_cleanup_vqat_ops_table();

	return adf_service_unregister(&adf_vdcm_srv_hndl);
}

