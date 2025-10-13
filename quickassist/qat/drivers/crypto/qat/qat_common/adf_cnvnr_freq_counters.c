// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2021 Intel Corporation */

#include "adf_cnvnr_freq_counters.h"
#include "adf_common_drv.h"
#include "icp_qat_fw_init_admin.h"
#include <linux/seq_file.h>

#define CNVNR_DBG_FILE "cnv_errors"
#define ADF_CNVNR_ERR_MASK 0xFFF
#define LINE	\
	"+-----------------------------------------------------------------+\n"
#define BANNER	\
	"|             CNV Error Freq Statistics for Qat Device            |\n"
static char *cnvnr_err_str[] = {"No Error      ",
				"Checksum Error",
				"Length Error-P",
				"Decomp Error  ",
				"Xlat Error    ",
				"Length Error-C",
				"Unknown Error "};

static int qat_cnvnr_freq_counters_show(struct seq_file *sfile, void *v)
{
	struct adf_accel_dev *accel_dev;
	struct adf_hw_device_data *hw_device;
	struct icp_qat_fw_init_admin_req req;
	struct icp_qat_fw_init_admin_resp resp;
	unsigned long dc_ae_msk = 0;
	u8 num_aes = 0;
	u8 i = 0;
	u8 error_type = 0;
	s16 latest_error = 0;

	accel_dev = sfile->private;
	hw_device = accel_dev->hw_device;
	if (!hw_device) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to get hw_device.\n");
		return -EFAULT;
	}
	num_aes = hw_device->get_num_aes(hw_device);

	if (accel_dev->au_info)
		dc_ae_msk = accel_dev->au_info->dc_ae_msk;

	seq_printf(sfile, LINE);
	seq_printf(sfile, BANNER);
	seq_printf(sfile, LINE);
	memset(&req, 0, sizeof(struct icp_qat_fw_init_admin_req));
	req.cmd_id = ICP_QAT_FW_CNV_STATS_GET;
	for (i = 0; i < num_aes; i++) {
		if (accel_dev->au_info && !test_bit(i, &dc_ae_msk))
			continue;
		memset(&resp, 0, sizeof(struct icp_qat_fw_init_admin_resp));
		if (adf_put_admin_msg_sync(accel_dev, i, &req, &resp) ||
		    resp.status) {
			return -EFAULT;
		}
		error_type = CNV_ERROR_TYPE_GET(resp.latest_error);
		if (error_type == CNV_ERR_TYPE_DECOMP_PRODUCED_LENGTH_ERROR ||
		    error_type == CNV_ERR_TYPE_DECOMP_CONSUMED_LENGTH_ERROR) {
			latest_error = CNV_ERROR_LENGTH_DELTA_GET(
							resp.latest_error);
		} else if (error_type == CNV_ERR_TYPE_DECOMPRESSION_ERROR ||
			  error_type == CNV_ERR_TYPE_TRANSLATION_ERROR) {
			latest_error = CNV_ERROR_DECOMP_STATUS_GET(
							resp.latest_error);
		} else {
			latest_error = resp.latest_error & ADF_CNVNR_ERR_MASK;
		}
		seq_printf(sfile,
			   "|[AE %2d]: TotalErrors: %5d : LastError: %s [%5d]  |\n",
			   i, resp.error_count,
			   cnvnr_err_str[error_type],
			   latest_error);
		seq_printf(sfile, LINE);
	}
	return 0;
}

static int qat_cnvnr_freq_counters_open(struct inode *inode, struct file *file)
{
	struct adf_accel_dev *accel_dev;

	accel_dev = inode->i_private;
	if (!accel_dev)
		return -EFAULT;

	if (!adf_dev_started(accel_dev)) {
		dev_err(&GET_DEV(accel_dev),
			"Qat Device not started\n");
		return -EFAULT;
	}
	return single_open(file, qat_cnvnr_freq_counters_show, accel_dev);
}

static const struct file_operations qat_cnvnr_ctr_fops = {
	.owner = THIS_MODULE,
	.open = qat_cnvnr_freq_counters_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

/**
 * adf_cnvnr_freq_counters_add() - Create debugfs entry for
 * acceleration device Freq counters.
 * @accel_dev:  Pointer to acceleration device.
 *
 * Return: 0 on success, error code otherwise.
 */
int adf_cnvnr_freq_counters_add(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_device;

	if (!accel_dev)
		return -EFAULT;

	/* Only create counters on pf */
	if (accel_dev->is_vf)
		return 0;

	hw_device = accel_dev->hw_device;
	if (!hw_device) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to get hw_device.\n");
		return -EFAULT;
	}

	/* accel_dev->debugfs_dir should always be non-NULL here */
	accel_dev->cnvnr_dbgfile = debugfs_create_file(CNVNR_DBG_FILE, 0400,
						       accel_dev->debugfs_dir,
						       accel_dev,
						       &qat_cnvnr_ctr_fops);
	if (!accel_dev->cnvnr_dbgfile) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to create qat cnvnr freq counters debugfs entry.\n");
		return -EFAULT;
	}
	return 0;
}

/**
 * adf_cnvnr_freq_counters_remove() - Remove debugfs entry for
 * acceleration device Freq counters.
 * @accel_dev:  Pointer to acceleration device.
 *
 * Return: void
 */
void adf_cnvnr_freq_counters_remove(struct adf_accel_dev *accel_dev)
{
	debugfs_remove(accel_dev->cnvnr_dbgfile);
	accel_dev->cnvnr_dbgfile = NULL;
}
