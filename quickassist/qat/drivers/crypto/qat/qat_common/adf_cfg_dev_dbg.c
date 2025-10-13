// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2021 Intel Corporation */
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/seq_file.h>
#include "adf_cfg.h"
#include "adf_cfg_dev_dbg.h"
#include "adf_common_drv.h"

static DEFINE_MUTEX(qat_cfg_read_lock);

static void *qat_dev_cfg_start(struct seq_file *sfile, loff_t *pos)
{
	struct adf_cfg_device_data *dev_cfg = sfile->private;

	mutex_lock(&qat_cfg_read_lock);
	return seq_list_start(&dev_cfg->sec_list, *pos);
}

static int qat_dev_cfg_show(struct seq_file *sfile, void *v)
{
	struct list_head *list;
	struct adf_cfg_section *sec =
				list_entry(v, struct adf_cfg_section, list);

	seq_printf(sfile, "[%s]\n", sec->name);
	list_for_each(list, &sec->param_head) {
		struct adf_cfg_key_val *ptr =
			list_entry(list, struct adf_cfg_key_val, list);
		seq_printf(sfile, "%s = %s\n", ptr->key, ptr->val);
	}
	return 0;
}

static void *qat_dev_cfg_next(struct seq_file *sfile, void *v, loff_t *pos)
{
	struct adf_cfg_device_data *dev_cfg = sfile->private;

	return seq_list_next(v, &dev_cfg->sec_list, pos);
}

static void qat_dev_cfg_stop(struct seq_file *sfile, void *v)
{
	mutex_unlock(&qat_cfg_read_lock);
}

static const struct seq_operations qat_dev_cfg_sops = {
	.start = qat_dev_cfg_start,
	.next = qat_dev_cfg_next,
	.stop = qat_dev_cfg_stop,
	.show = qat_dev_cfg_show
};

static int qat_dev_cfg_open(struct inode *inode, struct file *file)
{
	int ret = seq_open(file, &qat_dev_cfg_sops);

	if (!ret) {
		struct seq_file *seq_f = file->private_data;

		seq_f->private = inode->i_private;
	}
	return ret;
}

static const struct file_operations qat_dev_cfg_fops = {
	.open = qat_dev_cfg_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release
};

/**
 * adf_cfg_dev_dbg_add() - Create debugfs entry for device configuration
 * @accel_dev:  Pointer to acceleration device.
 * *
 * Return: 0 on success, error code otherwise.
 */
int adf_cfg_dev_dbg_add(struct adf_accel_dev *accel_dev)
{
	struct adf_cfg_device_data *dev_cfg_data = accel_dev->cfg;

	/* accel_dev->debugfs_dir should always be non-NULL here */
	dev_cfg_data->debug = debugfs_create_file("dev_cfg", 0400,
						  accel_dev->debugfs_dir,
						  dev_cfg_data,
						  &qat_dev_cfg_fops);
	if (!dev_cfg_data->debug) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to create qat cfg debugfs entry.\n");
		return -EFAULT;
	}

	return 0;
}

/**
 * adf_cfg_dev_dbg_remove() - Remove debugfs entry for device configuration
 * @accel_dev:  Pointer to acceleration device.
 * *
 * Return: void
 */
void adf_cfg_dev_dbg_remove(struct adf_accel_dev *accel_dev)
{
	struct adf_cfg_device_data *dev_cfg_data = accel_dev->cfg;

	debugfs_remove(dev_cfg_data->debug);
	dev_cfg_data->debug = NULL;
}
