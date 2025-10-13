// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2021 Intel Corporation */
#include <linux/list.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "adf_cfg.h"

#define ADF_DEV_PROCESSES_MAX_MINOR	(255)
#define ADF_DEV_PROCESSES_BASE_MINOR       (0)

#define ADF_DEV_PROCESSES_NAME "qat_dev_processes"
#define ADF_DEV_PROCESSES_SEMAPHORE_CNT 1

static int adf_processes_open(struct inode *inp, struct file *fp);
static int adf_processes_release(struct inode *inp, struct file *fp);

static ssize_t adf_processes_read(struct file *fp,
				  char __user *buf,
				  size_t count,
				  loff_t *pos);

static ssize_t adf_processes_write(struct file *fp,
				   const char __user *buf,
				   size_t count, loff_t *pos);

struct adf_processes_priv_data {
	char   name[ADF_CFG_MAX_SECTION_LEN_IN_BYTES];
	int    read_flag;
	struct list_head list;
};

struct adf_chr_drv_info {
	struct module       *owner;
	unsigned int	    major;
	unsigned int	    min_minor;
	unsigned int	    max_minor;
	char		*name;
	const struct file_operations *file_ops;
	struct cdev     drv_cdev;
	struct class    *drv_class;
	unsigned int	num_devices;
};

static const struct file_operations adf_processes_ops = {
	.owner = THIS_MODULE,
	.open = adf_processes_open,
	.release = adf_processes_release,
	.read  = adf_processes_read,
	.write = adf_processes_write,
};

static struct adf_chr_drv_info adf_processes_drv_info = {
	.owner = THIS_MODULE,
	.major = 0,
	.min_minor = ADF_DEV_PROCESSES_BASE_MINOR,
	.max_minor = ADF_DEV_PROCESSES_MAX_MINOR,
	.name = ADF_DEV_PROCESSES_NAME,
	.file_ops = &adf_processes_ops,
};

static LIST_HEAD(processes_list);
static DEFINE_SEMAPHORE(processes_list_sema, ADF_DEV_PROCESSES_SEMAPHORE_CNT);

static void adf_chr_drv_destroy(void)
{
	device_destroy(adf_processes_drv_info.drv_class,
		       MKDEV(adf_processes_drv_info.major, 0));
	cdev_del(&adf_processes_drv_info.drv_cdev);
	class_destroy(adf_processes_drv_info.drv_class);
	unregister_chrdev_region(MKDEV(
				 adf_processes_drv_info.major, 0), 1);
}

static int adf_chr_drv_create(void)
{
	dev_t dev_id;
	struct device *drv_device;

	if (alloc_chrdev_region(&dev_id, 0, 1, ADF_DEV_PROCESSES_NAME)) {
		pr_err("QAT: unable to allocate chrdev region\n");
		return -EFAULT;
	}

	adf_processes_drv_info.drv_class =
		class_create(ADF_DEV_PROCESSES_NAME);
	if (IS_ERR(adf_processes_drv_info.drv_class)) {
		pr_err("QAT: class_create failed for adf_ctl\n");
		goto err_chrdev_unreg;
	}
	adf_processes_drv_info.major = MAJOR(dev_id);
	cdev_init(&adf_processes_drv_info.drv_cdev, &adf_processes_ops);
	if (cdev_add(&adf_processes_drv_info.drv_cdev, dev_id, 1)) {
		pr_err("QAT: cdev add failed\n");
		goto err_class_destr;
	}

	drv_device = device_create(adf_processes_drv_info.drv_class, NULL,
				   MKDEV(adf_processes_drv_info.major, 0),
				   NULL, ADF_DEV_PROCESSES_NAME);
	if (IS_ERR(drv_device)) {
		pr_err("QAT: failed to create device\n");
		goto err_cdev_del;
	}
	return 0;
err_cdev_del:
	cdev_del(&adf_processes_drv_info.drv_cdev);
err_class_destr:
	class_destroy(adf_processes_drv_info.drv_class);
err_chrdev_unreg:
	unregister_chrdev_region(dev_id, 1);
	return -EFAULT;
}

static int adf_processes_open(struct inode *inp, struct file *fp)
{
	int i = 0, devices = 0;
	struct adf_accel_dev *accel_dev = NULL;
	struct adf_processes_priv_data *prv_data = NULL;

	for (i = 0; i < ADF_MAX_DEVICES; i++) {
		accel_dev = adf_devmgr_get_dev_by_id(i);
		if (!accel_dev)
			continue;
		if (!adf_dev_started(accel_dev))
			continue;
		devices++;
	}
	if (!devices) {
		pr_err("QAT: Device not yet ready.\n");
		return -EACCES;
	}
	prv_data = kzalloc(sizeof(*prv_data), GFP_KERNEL);
	if (!prv_data)
		return -ENOMEM;
	INIT_LIST_HEAD(&prv_data->list);
	fp->private_data = prv_data;

	return 0;
}

static ssize_t adf_processes_write(struct file *fp, const char __user *buf,
				   size_t count, loff_t *pos)
{
	struct adf_processes_priv_data *prv_data = NULL;
	struct adf_processes_priv_data *pdata = NULL;
	int dev_num = 0, pr_num = 0, ret = 1;
	struct list_head *lpos = NULL;
	char usr_name[ADF_CFG_MAX_SECTION_LEN_IN_BYTES] = {0};
	char val[ADF_CFG_MAX_VAL_LEN_IN_BYTES] = {0};
	struct adf_accel_dev *accel_dev = NULL;
	bool pr_name_available = 0, user_section_found = 0;
	u32 num_accel_devs = 0;
	unsigned int dev_access_limit = 0;

	if (!fp || !fp->private_data) {
		pr_err("QAT: invalid file descriptor\n");
		return -EBADF;
	}

	prv_data = (struct adf_processes_priv_data *)fp->private_data;
	if (prv_data->read_flag == 1) {
		pr_err("QAT: can only write once\n");
		return -EBADF;
	}
	if (count <= 0 || count >= ADF_CFG_MAX_SECTION_LEN_IN_BYTES) {
		pr_err("QAT: wrong size %d\n", (int)count);
		return -EIO;
	}

	if (copy_from_user(usr_name, buf, count)) {
		pr_err("QAT: can't copy data\n");
		return -EIO;
	}

	/* Lock other processes and try to find out the process name */
	if (down_interruptible(&processes_list_sema)) {
		pr_err("QAT: can't aquire process info lock\n");
		return -EBADF;
	}

	/* Search for a first free name */
	adf_devmgr_get_num_dev(&num_accel_devs);
	num_accel_devs += adf_get_num_dettached_vfs();
	for (dev_num = 0; dev_num < num_accel_devs; dev_num++) {
		accel_dev = adf_devmgr_get_dev_by_id(dev_num);
		if (!accel_dev)
			continue;

		if (!adf_dev_started(accel_dev))
			continue; /* to next device */

		if (adf_cfg_sec_find(accel_dev, usr_name))
			user_section_found = 1;
		else
			continue; /* user section not found */

		if (adf_cfg_get_param_value(accel_dev, usr_name,
					    ADF_LIMIT_DEV_ACCESS, val))
			dev_access_limit = 0;
		else
			if (kstrtouint(val, 10, &dev_access_limit))
				dev_access_limit = 0;

		/* one device can support up to GET_MAX_PROCESSES processes */
		for (pr_num = 0;
		     pr_num < GET_MAX_PROCESSES(accel_dev);
		     pr_num++) {
			/* figure out name */
			if (dev_access_limit) {
				snprintf(prv_data->name,
					 ADF_CFG_MAX_SECTION_LEN_IN_BYTES,
					 "%s_DEV%d"
					 ADF_INTERNAL_USERSPACE_SEC_SUFF "%d",
					 usr_name, dev_num, pr_num);
			} else {
				snprintf(prv_data->name,
					 ADF_CFG_MAX_SECTION_LEN_IN_BYTES, "%s"
					 ADF_INTERNAL_USERSPACE_SEC_SUFF "%d",
					 usr_name, pr_num);
			}
			/* Figure out if section exists in the config table */
			if (!adf_cfg_sec_find(accel_dev, prv_data->name)) {
				/* This section name doesn't exist */
				/* As process_num enumerates from 0, once we get
				 * to one which doesn't exist no further ones
				 * will exist. On to next device
				 */
				break;
			}
			pr_name_available = 1;
			/* Figure out if it's been taken already */
			list_for_each(lpos, &processes_list) {
				pdata = list_entry(lpos, struct
						   adf_processes_priv_data,
						   list);
				if (!strncmp(pdata->name, prv_data->name,
					     ADF_CFG_MAX_SECTION_LEN_IN_BYTES)){
					pr_name_available = 0;
					break;
				}
			}
			if (pr_name_available)
				break;
		}
		if (pr_name_available)
			break;
	}
	/*
	 * If we have a valid name that is not on
	 * the list take it and add to the list
	 */
	if (pr_name_available) {
		list_add(&prv_data->list, &processes_list);
		up(&processes_list_sema);
		prv_data->read_flag = 1;
		return 0;
	}

	/* If user section is not found in any device, log an error */
	if (!user_section_found) {
		pr_err("QAT: could not find %s section in any config files\n",
		       usr_name);
		ret = -EINVAL;
	}

	/* If not then the process needs to wait */
	up(&processes_list_sema);
	memset(prv_data->name, '\0', sizeof(prv_data->name));
	prv_data->read_flag = 0;
	return ret;
}

static ssize_t adf_processes_read(struct file *fp, char __user *buf,
				  size_t count, loff_t *pos)
{
	size_t len = 0;
	struct adf_processes_priv_data *prv_data = NULL;

	if (!fp || !fp->private_data) {
		pr_err("QAT: invalid file descriptor\n");
		return -EBADF;
	}
	prv_data = (struct adf_processes_priv_data *)fp->private_data;

	/*
	 * If there is a name that the process can use then give it
	 * to the proocess.
	 */
	len = min(count, strnlen(prv_data->name, sizeof(prv_data->name)));
	if (prv_data->read_flag) {
		if (copy_to_user(buf, prv_data->name, len)) {
			pr_err("QAT: failed to copy data to user\n");
			return -EIO;
		}
		return 0;
	}

	return -EIO;
}

static int adf_processes_release(struct inode *inp, struct file *fp)
{
	struct adf_processes_priv_data *prv_data = NULL;

	if (!fp || !fp->private_data) {
		pr_err("QAT: invalid file descriptor\n");
		return -EBADF;
	}
	prv_data = (struct adf_processes_priv_data *)fp->private_data;
	if (down_interruptible(&processes_list_sema)) {
		pr_err("QAT: can't aquire process info lock\n");
		return -EBADF;
	}
	list_del(&prv_data->list);
	up(&processes_list_sema);
	kfree(fp->private_data);
	return 0;
}

int adf_processes_dev_register(void)
{
	return adf_chr_drv_create();
}

void adf_processes_dev_unregister(void)
{
	adf_chr_drv_destroy();
}
