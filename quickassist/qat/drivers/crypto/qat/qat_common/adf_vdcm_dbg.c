// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2019 - 2021 Intel Corporation */
#include <linux/device.h>
#include <linux/uuid.h>
#include <linux/mdev.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/seq_file.h>
#include "adf_common_drv.h"
#include "adf_accel_devices.h"
#include "adf_vdcm.h"
#include "adf_adi.h"
#include "adf_transport_access_macros_gen4.h"
#include "adf_gen4_hw_csr_data.h"

static DEFINE_MUTEX(etr_regs_read_lock);
static DEFINE_MUTEX(misc_regs_read_lock);

#define VQAT_DBG_STATUS_ENTRY		"status"
#define VQAT_DBG_CTRL_ENTRY		"ctrl"
#define VQAT_DBG_MAX_OUTPUT		32

enum vqat_dbg_property_id {
	/* 1 - 100 reserved for status properties */
	VQAT_DBG_STS_SVC = 0,
	VQAT_DBG_STS_BUNDLE,
	VQAT_DBG_STS_ADI_IDX,
	VQAT_DBG_STS_PASID,
	VQAT_DBG_STS_STATE,
	VQAT_DBG_CTRL_RESET,
	VQAT_DBG_CTRL_ENABLE,
};

struct vqat_dbg_property {
	enum vqat_dbg_property_id id;
	char *name;
	struct adf_vdcm_vqat *parent;
	struct list_head list;
	struct dentry *dentry;
};

static char *status_properties[] = {
	"service",
	"bundle",
	"adi_idx",
	"pasid",
	"state",
};

static char *ctrl_properties[] = {
	"reset",
	"enable",
};

struct vqat_dbg_reg_info {
	u32 offs;
	char *name;
};

static struct vqat_dbg_reg_info vqat_dbg_etr_regs[] = {
	{ADF_VQAT_R0_HEAD,            "RingHeadoffset[0]"},
	{ADF_VQAT_R1_HEAD,            "RingHeadoffset[1]"},
	{ADF_VQAT_R0_TAIL,            "RingTailOffset[0]"},
	{ADF_VQAT_R1_TAIL,            "RingTailOffset[1]"},
	{ADF_VQAT_R0_CONFIG,          "RingConfig[0]"},
	{ADF_VQAT_R1_CONFIG,          "RingConfig[1]"},
	{ADF_VQAT_R0_LBASE,           "RingLbase[0]"},
	{ADF_VQAT_R1_LBASE,           "RingLbase[1]"},
	{ADF_VQAT_R0_UBASE,           "RingUbase[0]"},
	{ADF_VQAT_R1_UBASE,           "RingUbase[1]"},
	{ADF_VQAT_RPRESETCTL,         "RPResetCtl"},
	{ADF_VQAT_RPRESETSTS,         "RPResetSts"},
};

static struct vqat_dbg_reg_info vqat_dbg_misc_regs[] = {
	{ADF_VQAT_VINTSOU,              "VIntSou"},
	{ADF_VQAT_VINTMSK,              "VIntMsk"},
	{ADF_VQAT_MSGQ_CFG,             "MsgQueueCfg"},
	{ADF_VQAT_MSGQ_TX_NOTIFIER,     "MsgTxQueue0Notifier"},
	{ADF_VQAT_MSGQ_RX_NOTIFIER,     "MsgRxQueue0Notifier"},
};

static int adf_vdcm_debug_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t adf_vdcm_debug_read_ctrl(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	return 0;
}

static ssize_t adf_vdcm_debug_read_status(struct file *file,
					  char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	struct vqat_dbg_property *prop = file->private_data;
	char buf[VQAT_DBG_MAX_OUTPUT];
	int len = 0;
	struct adf_adi_ep *adi = NULL;

	if (!prop || !prop->parent || !prop->parent->hw_priv)
		return 0;

	adi = (struct adf_adi_ep *)prop->parent->hw_priv;
	switch (prop->id) {
	case VQAT_DBG_STS_SVC:
		if (adi->type == ADI_TYPE_SYM)
			len = scnprintf(buf, sizeof(buf), "%s\n",
					"sym");
		else if (adi->type == ADI_TYPE_ASYM)
			len = scnprintf(buf, sizeof(buf), "%s\n",
					"asym");
		else if (adi->type == ADI_TYPE_COMP)
			len = scnprintf(buf, sizeof(buf), "%s\n",
					"dc");
		else
			len = scnprintf(buf, sizeof(buf), "%s\n",
					"unknown");
		break;
	case VQAT_DBG_STS_BUNDLE:
		len = scnprintf(buf, sizeof(buf), "%d\n",
				adi->bank_idx);
		break;
	case VQAT_DBG_STS_PASID:
		len = scnprintf(buf, sizeof(buf), "%d\n",
				adi->pasid);
		break;
	case VQAT_DBG_STS_STATE:
		if (adi->status == ADI_STATUS_IDLE)
			len = scnprintf(buf, sizeof(buf), "%s\n",
					"idle");
		else if (adi->status == ADI_STATUS_ACTIVE)
			len = scnprintf(buf, sizeof(buf), "%s\n",
					"active");
		else
			len = scnprintf(buf, sizeof(buf), "%s\n",
					"error");
		break;
	case VQAT_DBG_STS_ADI_IDX:
		len = scnprintf(buf, sizeof(buf), "%d\n",
				adi->adi_idx);
		break;
	default:
		break;
	}
	if (len < 0)
		return -EFAULT;
	return simple_read_from_buffer(user_buf, count, ppos, buf, len + 1);
}

static ssize_t adf_vdcm_debug_write_ctrl(struct file *file,
					 const char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct vqat_dbg_property *prop = file->private_data;
	char buf[VQAT_DBG_MAX_OUTPUT];
	struct adf_adi_ep *adi = NULL;
	int ret = -EINVAL;
	unsigned long minsz;

	if (!prop || !prop->parent || !prop->parent->hw_priv)
		return -EINVAL;

	adi = (struct adf_adi_ep *)prop->parent->hw_priv;

	if (!adi->adi_ops || !adi->adi_ops->reset)
		return -EINVAL;

	if (!user_buf)
		return -EINVAL;

	switch (prop->id) {
	case VQAT_DBG_CTRL_RESET:
		minsz = (sizeof(buf) >= count ? count : sizeof(buf));
		if (copy_from_user(buf, user_buf, minsz))
			return -EINVAL;

		if (!strncmp(buf, "1", 1))
			ret = adi->adi_ops->reset(adi, true);
		break;
	default:
		break;
	}

	return ret == 0 ? count : -EFAULT;
}

static const struct file_operations adf_vdcm_debug_status_fops = {
	.open = adf_vdcm_debug_open,
	.read = adf_vdcm_debug_read_status,
};

static const struct file_operations adf_vdcm_debug_ctrl_fops = {
	.open = adf_vdcm_debug_open,
	.read = adf_vdcm_debug_read_ctrl,
	.write = adf_vdcm_debug_write_ctrl,
};

static void *adf_misc_regs_start(struct seq_file *sfile, loff_t *pos)
{
	u32 num_regs = ARRAY_SIZE(vqat_dbg_misc_regs);

	mutex_lock(&misc_regs_read_lock);

	if (*pos == 0)
		return SEQ_START_TOKEN;

	if (*pos >= num_regs)
		return NULL;

	return pos;
}

static void *adf_misc_regs_next(struct seq_file *sfile, void *v, loff_t *pos)
{
	u32 num_regs = ARRAY_SIZE(vqat_dbg_misc_regs);

	if (++(*pos) > num_regs)
		return NULL;

	return pos;
}

static int adf_misc_regs_show(struct seq_file *sfile, void *v)
{
	struct adf_vdcm_vqat *vqat = sfile->private;
	u32 size = 4;
	u32 val;
	int ret;

	if (!vqat || !vqat->ops || !vqat->ops->mmio_read)
		return -EINVAL;

	if (v == SEQ_START_TOKEN) {
		seq_puts(sfile, "ID misc_reg_name        misc_reg_offs      misc_reg_val\n");
	} else {
		u32 idx = *((u32 *)v) - 1;

		ret = vqat->ops->mmio_read(vqat,
					   ADF_VQAT_PMISC_BAR,
					   vqat_dbg_misc_regs[idx].offs,
					   &val,
					   size);

		if (ret > 0) {
			seq_printf(sfile, "%-2d %-20s 0x%-16x 0x%-20x\n",
				   idx,
				   vqat_dbg_misc_regs[idx].name,
				   vqat_dbg_misc_regs[idx].offs,
				   val);
		} else {
			return -EFAULT;
		}
	}

	return 0;
}

static void adf_misc_regs_stop(struct seq_file *sfile, void *v)
{
	mutex_unlock(&misc_regs_read_lock);
}

static const struct seq_operations adf_misc_regs_sops = {
	.start = adf_misc_regs_start,
	.next = adf_misc_regs_next,
	.stop = adf_misc_regs_stop,
	.show = adf_misc_regs_show
};

static int adf_vdcm_misc_regs_open(struct inode *inode, struct file *file)
{
	int ret = seq_open(file, &adf_misc_regs_sops);

	if (!ret) {
		struct seq_file *seq_f = file->private_data;

		seq_f->private = inode->i_private;
	}
	return ret;
}

static const struct file_operations adf_vdcm_misc_dbg_fops = {
	.owner = THIS_MODULE,
	.open = adf_vdcm_misc_regs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release
};

static void *adf_etr_regs_start(struct seq_file *sfile, loff_t *pos)
{
	u32 num_regs = ARRAY_SIZE(vqat_dbg_etr_regs);

	mutex_lock(&etr_regs_read_lock);

	if (*pos == 0)
		return SEQ_START_TOKEN;

	if (*pos >= num_regs)
		return NULL;

	return pos;
}

static void *adf_etr_regs_next(struct seq_file *sfile, void *v, loff_t *pos)
{
	u32 num_regs = ARRAY_SIZE(vqat_dbg_etr_regs);

	if (++(*pos) > num_regs)
		return NULL;

	return pos;
}

static int adf_etr_regs_show(struct seq_file *sfile, void *v)
{
	struct adf_vdcm_vqat *vqat = sfile->private;
	struct adf_adi_ep *adi = NULL;
	u32 size = 4;
	u32 val;
	int ret;

	if (!vqat || !vqat->hw_priv)
		return -EINVAL;

	adi = (struct adf_adi_ep *)vqat->hw_priv;

	if (!adi->adi_ops || !adi->adi_ops->vreg_read)
		return -EINVAL;

	if (v == SEQ_START_TOKEN) {
		seq_puts(sfile, "ID etr_reg_name	        etr_reg_offs	   etr_reg_val\n");
	} else {
		u32 idx = *((u32 *)v) - 1;

		ret = adi->adi_ops->vreg_read(adi,
					      vqat_dbg_etr_regs[idx].offs,
					      &val,
					      size);

		if (ret > 0) {
			seq_printf(sfile, "%-2d %-20s 0x%-16x 0x%-20x\n",
				   idx,
				   vqat_dbg_etr_regs[idx].name,
				   vqat_dbg_etr_regs[idx].offs,
				   val);
		} else {
			return -EFAULT;
		}
	}

	return 0;
}

static void adf_etr_regs_stop(struct seq_file *sfile, void *v)
{
	mutex_unlock(&etr_regs_read_lock);
}

static const struct seq_operations adf_etr_regs_sops = {
	.start = adf_etr_regs_start,
	.next = adf_etr_regs_next,
	.stop = adf_etr_regs_stop,
	.show = adf_etr_regs_show
};

static int adf_vdcm_etr_regs_open(struct inode *inode, struct file *file)
{
	int ret = seq_open(file, &adf_etr_regs_sops);

	if (!ret) {
		struct seq_file *seq_f = file->private_data;

		seq_f->private = inode->i_private;
	}
	return ret;
}

static const struct file_operations adf_vdcm_etr_dbg_fops = {
	.owner = THIS_MODULE,
	.open = adf_vdcm_etr_regs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release
};

static inline void adf_vdcm_cleanup_properties(struct adf_vdcm_vqat *vqat)
{
	struct vqat_dbg_property *prop;
	struct list_head *ptr, *tmp;

	list_for_each_prev_safe(ptr, tmp, &vqat->prop_list) {
		prop = list_entry(ptr, struct vqat_dbg_property, list);
		list_del(ptr);
		kfree(prop);
	}
}

static int adf_vdcm_dbg_populate(struct adf_vdcm_vqat *vqat)
{
	int ret = -EINVAL;
	int i, j;
	struct vqat_dbg_property *prop;
	struct dentry *entry;

	if (!vqat)
		return -EINVAL;

	INIT_LIST_HEAD(&vqat->prop_list);

	for (i = 0; i < ARRAY_SIZE(status_properties); i++) {
		prop = kzalloc(sizeof(*prop), GFP_KERNEL);
		if (!prop) {
			ret = -ENOMEM;
			goto dbg_populate_err;
		}

		prop->name = status_properties[i];
		prop->id = i;
		prop->parent = vqat;
		entry = debugfs_create_file(prop->name, 0400,
					    vqat->debug.status_dbgdir,
					    prop,
					    &adf_vdcm_debug_status_fops);

		if (!entry) {
			ret = -EFAULT;
			kfree(prop);
			goto dbg_populate_err;
		}

		prop->dentry = entry;
		list_add_tail(&prop->list, &vqat->prop_list);
	}

	j = i;

	for (i = 0; i < ARRAY_SIZE(ctrl_properties); i++) {
		prop = kzalloc(sizeof(*prop), GFP_KERNEL);
		if (!prop) {
			ret = -ENOMEM;
			goto dbg_populate_err;
		}

		prop->name = ctrl_properties[i];
		prop->id = j++;
		prop->parent = vqat;
		entry = debugfs_create_file(prop->name, 0600,
					    vqat->debug.ctrl_dbgdir,
					    prop,
					    &adf_vdcm_debug_ctrl_fops);

		if (!entry) {
			ret = -EFAULT;
			kfree(prop);
			goto dbg_populate_err;
		}

		prop->dentry = entry;
		list_add_tail(&prop->list, &vqat->prop_list);
	}

	return 0;

dbg_populate_err:
	adf_vdcm_cleanup_properties(vqat);
	return ret;
}

int adf_vdcm_add_vqat_dbg(struct adf_accel_dev *accel_dev,
			  struct adf_vdcm_vqat *vqat)
{
	const char *name;
	struct dentry *debugfs_vdcm;

	if (!accel_dev || !accel_dev->vdcm || !vqat || !vqat->mdev)
		return -EINVAL;

	name = dev_name(mdev_dev(vqat->mdev));
	debugfs_vdcm = adf_vdcm_get_debugfs(accel_dev->vdcm);
	vqat->debug.dev_dbgdir = debugfs_create_dir(name,
						    debugfs_vdcm);
	if (!vqat->debug.dev_dbgdir) {
		dev_err(mdev_dev(vqat->mdev),
			"Failed to create vqat debug dir\n");
		return -EFAULT;
	}

	vqat->debug.status_dbgdir = debugfs_create_dir(VQAT_DBG_STATUS_ENTRY,
						       vqat->debug.dev_dbgdir);
	if (!vqat->debug.status_dbgdir) {
		dev_err(mdev_dev(vqat->mdev),
			"Failed to create vqat status debug dir\n");
		goto add_dbg_err;
	}

	vqat->debug.etr_dbg = debugfs_create_file("etr_regs", 0400,
						  vqat->debug.status_dbgdir,
						  vqat,
						  &adf_vdcm_etr_dbg_fops);
	if (!vqat->debug.etr_dbg) {
		dev_err(mdev_dev(vqat->mdev),
			"Failed to create vqat etr debug dentry\n");
		goto add_dbg_err;
	}

	vqat->debug.misc_dbg = debugfs_create_file("misc_regs", 0400,
						   vqat->debug.status_dbgdir,
						   vqat,
						   &adf_vdcm_misc_dbg_fops);
	if (!vqat->debug.misc_dbg) {
		dev_err(mdev_dev(vqat->mdev),
			"Failed to create vqat misc debug dentry\n");
		goto add_dbg_err;
	}

	vqat->debug.ctrl_dbgdir = debugfs_create_dir(VQAT_DBG_CTRL_ENTRY,
						     vqat->debug.dev_dbgdir);
	if (!vqat->debug.ctrl_dbgdir) {
		dev_err(mdev_dev(vqat->mdev),
			"Failed to create vqat control debug dir\n");
		goto add_dbg_err;
	}

	if (adf_vdcm_dbg_populate(vqat)) {
		dev_err(mdev_dev(vqat->mdev),
			"Failed to populate vqat debug entries\n");
		goto add_dbg_err;
	}

	return 0;

add_dbg_err:
	debugfs_remove_recursive(vqat->debug.dev_dbgdir);
	return -EFAULT;
}

void adf_vdcm_del_vqat_dbg(struct adf_vdcm_vqat *vqat)
{
	adf_vdcm_cleanup_properties(vqat);
	debugfs_remove_recursive(vqat->debug.dev_dbgdir);
	vqat->debug.dev_dbgdir = NULL;
}
