// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2021 Intel Corporation */
#include "adf_heartbeat_dbg.h"
#include "adf_common_drv.h"
#include "adf_cfg.h"
#include "adf_heartbeat.h"

static int qat_hb_common_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t qat_hb_counter_read(struct file *file,
				   char __user *user_buffer,
				   size_t count,
				   loff_t *ppos)
{
	char buf[16];
	unsigned int *value = (unsigned int *)file->private_data;
	int len = 0;

	if (*ppos > 0)
		return 0;

	len = scnprintf(buf, sizeof(buf), "%u\n", *value);
	if (len < 0)
		return -EFAULT;

	return simple_read_from_buffer(user_buffer, count, ppos, buf, len + 1);
}

static const struct file_operations qat_heartbeat_fops = {
	.open = qat_hb_common_open,
	.read = qat_hb_counter_read,
};

static ssize_t qat_dev_hb_read(struct file *file, char __user *user_buf,
			       size_t count, loff_t *ppos)
{
	struct adf_accel_dev *accel_dev =
		(struct adf_accel_dev *)file->private_data;
	enum adf_device_heartbeat_status hb_status;
	int hb = 0;
	char hb_log[8] = {0};
	size_t len = 0;

	if (*ppos > 0)
		return 0;

	if (adf_heartbeat_status(accel_dev, &hb_status)) {
		dev_err(&GET_DEV(accel_dev),
			"Error sending heartbeat");
		hb = -1;
	} else {
		if (hb_status != DEV_HB_ALIVE)
			hb = -1;
	}

	len = scnprintf(hb_log, sizeof(hb_log), "%d\n", hb);

	return simple_read_from_buffer(user_buf, count, ppos, hb_log, len + 1);
}

static const struct file_operations qat_dev_hb_fops = {
	.open = qat_hb_common_open,
	.read = qat_dev_hb_read,
};

#ifdef QAT_HB_FAIL_SIM
static ssize_t qat_hb_sim_fail_read(struct file *file, char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	struct adf_accel_dev *accel_dev =
		(struct adf_accel_dev *)file->private_data;
	char buf[16];
	size_t len = 0;
	int err = -1;

	if (*ppos > 0)
		return 0;

	err = adf_heartbeat_simulate_failure(accel_dev);

	len = scnprintf(buf, sizeof(buf), "%d\n", err);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len + 1);
}

static const struct file_operations qat_dev_hb_sim_fail_fops = {
	.open = qat_hb_common_open,
	.read = qat_hb_sim_fail_read,
};

#endif
int adf_heartbeat_dbg_add(struct adf_accel_dev *accel_dev)
{
	struct adf_heartbeat *hb = NULL;

	if (adf_heartbeat_init(accel_dev))
		return -EFAULT;

	/* Only create heartbeat debugfs entries on pf */
	if (accel_dev->is_vf)
		return 0;

	hb = accel_dev->heartbeat;
	hb->heartbeat = debugfs_create_file("heartbeat", 0400,
					    accel_dev->debugfs_dir,
					    accel_dev,
					    &qat_dev_hb_fops);
	if (!hb->heartbeat) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to create qat heartbeat debugfs entry.\n");
		goto err;
	}

	hb->heartbeat_sent =
			debugfs_create_file("heartbeat_sent", 0400,
					    accel_dev->debugfs_dir,
					    &hb->hb_sent_counter,
					    &qat_heartbeat_fops);
	if (!hb->heartbeat_sent) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to create heartbeat_sent debugfs entry.\n");
		goto err;
	}

	hb->heartbeat_failed =
			debugfs_create_file("heartbeat_failed", 0400,
					    accel_dev->debugfs_dir,
					    &hb->hb_failed_counter,
					    &qat_heartbeat_fops);
	if (!hb->heartbeat_failed) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to create heartbeat_failed debugfs entry.\n");
		goto err;
	}
#ifdef QAT_HB_FAIL_SIM
	hb->heartbeat_sim_fail =
			debugfs_create_file("heartbeat_sim_fail", 0400,
					    accel_dev->debugfs_dir,
					    accel_dev,
					    &qat_dev_hb_sim_fail_fops);
	if (!hb->heartbeat_sim_fail) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to create heartbeat_sim_fail debugfs entry.\n");
		goto err;
	}
#endif

	return 0;

err:
	debugfs_remove(hb->heartbeat);
	hb->heartbeat = NULL;
	debugfs_remove(hb->heartbeat_sent);
	hb->heartbeat_sent = NULL;
#ifdef QAT_HB_FAIL_SIM
	debugfs_remove(hb->heartbeat_sim_fail);
	hb->heartbeat_sim_fail = NULL;
#endif

	return -EFAULT;
}

void adf_heartbeat_dbg_del(struct adf_accel_dev *accel_dev)
{
	struct adf_heartbeat *hb = accel_dev->heartbeat;

	debugfs_remove(hb->heartbeat);
	hb->heartbeat = NULL;
	debugfs_remove(hb->heartbeat_sent);
	hb->heartbeat_sent = NULL;
	debugfs_remove(hb->heartbeat_failed);
	hb->heartbeat_failed = NULL;
#ifdef QAT_HB_FAIL_SIM
	debugfs_remove(hb->heartbeat_sim_fail);
	hb->heartbeat_sim_fail = NULL;
#endif
	adf_heartbeat_clean(accel_dev);
}
