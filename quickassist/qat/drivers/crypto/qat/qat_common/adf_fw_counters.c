// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2021 Intel Corporation */

#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/seq_file.h>
#include "adf_accel_devices.h"
#include "adf_fw_counters.h"
#include "adf_common_drv.h"
#include "icp_qat_fw_init_admin.h"

#define ADF_RAS_EVENT_STR "RAS events"
#define ADF_FW_REQ_STR "Firmware Requests"
#define ADF_FW_RESP_STR "Firmware Responses"

/*
 * Max space count for fw counter format
 */
#define ADF_MAX_SPACE_COUNT 18

static char *last_string = ADF_FW_RESP_STR;
static DEFINE_MUTEX(qat_fw_counters_read_lock);

static void adf_fw_counters_section_del_all(struct list_head *head);
static void adf_fw_counters_del_all(struct adf_accel_dev *accel_dev);
static int adf_fw_counters_add_key_value_param(struct adf_accel_dev *accel_dev,
					       const char *section_name,
					       const char *key,
					       const void *val);
static int adf_fw_counters_section_add(struct adf_accel_dev *accel_dev,
				       const char *name);

static void *qat_fw_counters_data_start(struct seq_file *sfile, loff_t *pos)
{
	struct adf_fw_counters_data *fw_counters_data = sfile->private;

	seq_printf(sfile,
		   "+------------------------------------------------+\n");
	seq_printf(sfile,
		   "| FW Statistics for Qat Device                   |\n");
	seq_printf(sfile,
		   "+------------------------------------------------+\n");
	mutex_lock(&qat_fw_counters_read_lock);
	return seq_list_start(&fw_counters_data->ae_sec_list, *pos);
}

static int qat_fw_counters_data_show(struct seq_file *sfile, void *v)
{
	struct list_head *list;
	struct adf_fw_counters_section *sec =
			list_entry(v, struct adf_fw_counters_section, list);

	list_for_each(list, &sec->param_head) {
		struct adf_fw_counters_val *ptr =
			list_entry(list, struct adf_fw_counters_val, list);
			seq_printf(sfile, "| %*s[%s]:%20s |\n",
				   -(ADF_MAX_SPACE_COUNT), ptr->key,
				   sec->name, ptr->val);
		if (!strcmp(ptr->key, last_string))
			seq_printf(sfile,
				   "+------------------------------------------------+\n");
	}
	return 0;
}

static void *qat_fw_counters_data_next(struct seq_file *sfile,
				       void *v, loff_t *pos)
{
	struct adf_fw_counters_data *fw_counters_data = sfile->private;

	return seq_list_next(v, &fw_counters_data->ae_sec_list, pos);
}

static void qat_fw_counters_data_stop(struct seq_file *sfile, void *v)
{
	mutex_unlock(&qat_fw_counters_read_lock);
}

static const struct seq_operations qat_fw_counters_data_sops = {
	.start = qat_fw_counters_data_start,
	.next = qat_fw_counters_data_next,
	.stop = qat_fw_counters_data_stop,
	.show = qat_fw_counters_data_show
};

static int qat_fw_counters_data_open(struct inode *inode, struct file *file)
{
	struct adf_accel_dev *accel_dev;
	struct adf_hw_device_data *hw_device;
	struct icp_qat_fw_init_admin_req req;
	struct icp_qat_fw_init_admin_resp resp;
	unsigned long ae_mask;
	struct seq_file *seq_f;
	char *aeidstr = NULL;
	unsigned int i = 0;
	int ret = seq_open(file, &qat_fw_counters_data_sops);

	if (ret)
		return ret;

	seq_f = file->private_data;
	accel_dev = inode->i_private;
	if (!accel_dev) {
		ret = -EFAULT;
		goto out_err;
	}

	if (!adf_dev_started(accel_dev)) {
		dev_err(&GET_DEV(accel_dev),
			"Qat Device not started\n");
		ret = -EFAULT;
		goto out_err;
	}

	hw_device = accel_dev->hw_device;
	if (!hw_device) {
		ret = -EFAULT;
		goto out_err;
	}

	adf_fw_counters_del_all(accel_dev);
	memset(&req, 0, sizeof(struct icp_qat_fw_init_admin_req));
	req.cmd_id = ICP_QAT_FW_COUNTERS_GET;
	ae_mask = hw_device->ae_mask;

	for_each_set_bit(i, &ae_mask, GET_MAX_ACCELENGINES(accel_dev)) {
		memset(&resp, 0, sizeof(struct icp_qat_fw_init_admin_resp));
		if (adf_put_admin_msg_sync(accel_dev, i, &req, &resp) ||
		    resp.status) {
			resp.req_rec_count = ADF_FW_COUNTERS_NO_RESPONSE;
			resp.resp_sent_count = ADF_FW_COUNTERS_NO_RESPONSE;
			resp.ras_event_count = ADF_FW_COUNTERS_NO_RESPONSE;
		}

		aeidstr = kasprintf(GFP_ATOMIC, "AE %2d", i);
		if (!aeidstr) {
			ret = -ENOMEM;
			goto out_err;
		}

		if (adf_fw_counters_section_add(accel_dev, aeidstr)) {
			ret = -ENOMEM;
			goto fail_clean;
		}

		if (adf_fw_counters_add_key_value_param(
		    accel_dev,
		    aeidstr,
		    ADF_FW_REQ_STR,
		    (void *)&resp.req_rec_count)) {
			adf_fw_counters_del_all(accel_dev);
			ret = -ENOMEM;
			goto fail_clean;
		}

		if (adf_fw_counters_add_key_value_param(
		    accel_dev,
		    aeidstr,
		    ADF_FW_RESP_STR,
		    (void *)&resp.resp_sent_count)) {
			adf_fw_counters_del_all(accel_dev);
			ret = -ENOMEM;
			goto fail_clean;
		}

		if (hw_device->count_ras_event &&
		    hw_device->count_ras_event(
		    accel_dev,
		    &resp.ras_event_count,
		    aeidstr)) {
			adf_fw_counters_del_all(accel_dev);
			ret = -ENOMEM;
			goto fail_clean;
		}
	}
	seq_f->private = accel_dev->fw_counters_data;
	return ret;

fail_clean:
	kfree(aeidstr);
out_err:
	seq_release(inode, file);

	return ret;
}

int adf_fw_count_ras_event(struct adf_accel_dev *accel_dev,
			   u32 *ras_event, char *aeidstr)
{
	unsigned long count = 0;

	if (!accel_dev || !ras_event || !aeidstr)
		return -EINVAL;

	count = (*ras_event == ADF_FW_COUNTERS_NO_RESPONSE ?
		ADF_FW_COUNTERS_NO_RESPONSE : (unsigned long)*ras_event);

	return adf_fw_counters_add_key_value_param(
		  accel_dev,
		  aeidstr,
		  ADF_RAS_EVENT_STR,
		  (void *)&count);
}
EXPORT_SYMBOL_GPL(adf_fw_count_ras_event);

static const struct file_operations qat_fw_counters_fops = {
	.open = qat_fw_counters_data_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release
};

/**
 * adf_fw_counters_add() - Create an acceleration device FW counters table.
 * @accel_dev:  Pointer to acceleration device.
 *
 * Function creates a FW counters statistics table for the given
 * acceleration device.
 * The table stores device specific values of FW Requests sent to the FW and
 * FW Responses received from the FW.
 * To be used by QAT device specific drivers.
 *
 * Return: 0 on success, error code otherwise.
 */
int adf_fw_counters_add(struct adf_accel_dev *accel_dev)
{
	struct adf_fw_counters_data *fw_counters_data;

	fw_counters_data = kzalloc(sizeof(*fw_counters_data), GFP_KERNEL);
	if (!fw_counters_data)
		return -ENOMEM;

	INIT_LIST_HEAD(&fw_counters_data->ae_sec_list);

	init_rwsem(&fw_counters_data->lock);
	accel_dev->fw_counters_data = fw_counters_data;

	/* accel_dev->debugfs_dir should always be non-NULL here */
	fw_counters_data->debug = debugfs_create_file("fw_counters", 0400,
						      accel_dev->debugfs_dir,
						      accel_dev,
						      &qat_fw_counters_fops);
	if (!fw_counters_data->debug) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to create qat fw_counters debugfs entry.\n");
		kfree(fw_counters_data);
		accel_dev->fw_counters_data = NULL;
		return -EFAULT;
	}

	if (accel_dev->hw_device->count_ras_event)
		last_string = ADF_RAS_EVENT_STR;

	return 0;
}
EXPORT_SYMBOL_GPL(adf_fw_counters_add);

static void adf_fw_counters_del_all(struct adf_accel_dev *accel_dev)
{
	struct adf_fw_counters_data *fw_counters_data =
					accel_dev->fw_counters_data;

	down_write(&fw_counters_data->lock);
	adf_fw_counters_section_del_all(&fw_counters_data->ae_sec_list);
	up_write(&fw_counters_data->lock);
}

static void adf_fw_counters_keyval_add(struct adf_fw_counters_val *new,
				       struct adf_fw_counters_section *sec)
{
	list_add_tail(&new->list, &sec->param_head);
}

static void adf_fw_counters_keyval_del_all(struct list_head *head)
{
	struct list_head *list_ptr = NULL, *tmp = NULL;

	list_for_each_prev_safe(list_ptr, tmp, head) {
		struct adf_fw_counters_val *ptr =
			list_entry(list_ptr,
				   struct adf_fw_counters_val, list);
		list_del(list_ptr);
		kfree(ptr);
	}
}

static void adf_fw_counters_section_del_all(struct list_head *head)
{
	struct adf_fw_counters_section *ptr;
	struct list_head *list = NULL, *tmp = NULL;

	list_for_each_prev_safe(list, tmp, head) {
		ptr = list_entry(list, struct adf_fw_counters_section, list);
		adf_fw_counters_keyval_del_all(&ptr->param_head);
		list_del(list);
		kfree(ptr);
	}
}

static struct adf_fw_counters_section *adf_fw_counters_sec_find(
					struct adf_accel_dev *accel_dev,
					const char *sec_name)
{
	struct adf_fw_counters_data *fw_counters_data =
					accel_dev->fw_counters_data;
	struct list_head *list = NULL;

	list_for_each(list, &fw_counters_data->ae_sec_list) {
		struct adf_fw_counters_section *ptr =
			list_entry(list,
				   struct adf_fw_counters_section, list);
		if (!strcmp(ptr->name, sec_name))
			return ptr;
	}
	return NULL;
}

static int adf_fw_counters_add_key_value_param(struct adf_accel_dev *accel_dev,
					       const char *section_name,
					       const char *key, const void *val)
{
	struct adf_fw_counters_data *fw_counters_data =
						accel_dev->fw_counters_data;
	struct adf_fw_counters_val *key_val;
	struct adf_fw_counters_section *section = adf_fw_counters_sec_find(
						accel_dev, section_name);
	long tmp = *((long *)val);

	if (!section)
		return -EFAULT;

	key_val = kzalloc(sizeof(*key_val), GFP_KERNEL);
	if (!key_val)
		return -ENOMEM;

	INIT_LIST_HEAD(&key_val->list);

	if (tmp == ADF_FW_COUNTERS_NO_RESPONSE) {
		snprintf(key_val->val, FW_COUNTERS_MAX_VAL_LEN_IN_BYTES,
			 "No Response");
	} else {
		snprintf(key_val->val, FW_COUNTERS_MAX_VAL_LEN_IN_BYTES,
			 "%ld", tmp);
	}

	strscpy(key_val->key, key, sizeof(key_val->key));
	down_write(&fw_counters_data->lock);
	adf_fw_counters_keyval_add(key_val, section);
	up_write(&fw_counters_data->lock);
	return 0;
}

/**
 * adf_fw_counters_section_add() - Add AE section entry to FW counters table.
 * @accel_dev:  Pointer to acceleration device.
 * @name: Name of the section
 *
 * Function adds a section for each AE where FW Requests/Responses and their
 * values will be stored.
 * To be used by QAT device specific drivers.
 *
 * Return: 0 on success, error code otherwise.
 */
static int adf_fw_counters_section_add(struct adf_accel_dev *accel_dev,
				       const char *name)
{
	struct adf_fw_counters_data *fw_counters_data =
					accel_dev->fw_counters_data;
	struct adf_fw_counters_section *sec = adf_fw_counters_sec_find(
							accel_dev, name);

	if (sec)
		return 0;

	sec = kzalloc(sizeof(*sec), GFP_KERNEL);
	if (!sec)
		return -ENOMEM;

	strscpy(sec->name, name, sizeof(sec->name));
	INIT_LIST_HEAD(&sec->param_head);

	down_write(&fw_counters_data->lock);

	list_add_tail(&sec->list, &fw_counters_data->ae_sec_list);
	up_write(&fw_counters_data->lock);
	return 0;
}

/**
 * adf_fw_counters_remove() - Clears acceleration device FW counters table.
 * @accel_dev:  Pointer to acceleration device.
 *
 * Function removes FW counters table from the given acceleration device
 * and frees all allocated memory.
 * To be used by QAT device specific drivers.
 *
 * Return: void
 */
void adf_fw_counters_remove(struct adf_accel_dev *accel_dev)
{
	struct adf_fw_counters_data *fw_counters_data =
					accel_dev->fw_counters_data;

	if (!fw_counters_data)
		return;

	down_write(&fw_counters_data->lock);
	adf_fw_counters_section_del_all(&fw_counters_data->ae_sec_list);
	up_write(&fw_counters_data->lock);
	debugfs_remove(fw_counters_data->debug);
	kfree(fw_counters_data);
	accel_dev->fw_counters_data = NULL;
}
EXPORT_SYMBOL_GPL(adf_fw_counters_remove);
