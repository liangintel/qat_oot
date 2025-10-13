// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2021 Intel Corporation */
#include <linux/delay.h>
#include <linux/debugfs.h>

#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "adf_dev_err.h"

#define ADF_PFVF_DEBUG_NAME_SZ 16

static void *adf_pfvf_start(struct seq_file *sfile, loff_t *pos)
{
	if (*pos >= NUM_PFVF_COUNTERS)
		return NULL;

	return pos;
}

static void *adf_pfvf_next(struct seq_file *sfile, void *v, loff_t *pos)
{
	(*pos)++;

	if (*pos >= NUM_PFVF_COUNTERS)
		return NULL;

	return pos;
}

static int adf_pfvf_show(struct seq_file *sfile, void *v)
{
	struct pfvf_stats *pfvf_counters = sfile->private;
	unsigned int value = 0;
	char *string = "unknown";
	loff_t field = *(loff_t *)(v);

	switch (field) {
	case 0:
		string = "Messages written to CSR";
		value = pfvf_counters->tx;
		break;
	case 1:
		string = "Messages read from CSR";
		value = pfvf_counters->rx;
		break;
	case 2:
		string = "Spurious Interrupt";
		value = pfvf_counters->spurious;
		break;
	case 3:
		string = "Block messages sent";
		value = pfvf_counters->blk_tx;
		break;
	case 4:
		string = "Block messages received";
		value = pfvf_counters->blk_rx;
		break;
	case 5:
		string = "Blocks received with CRC errors";
		value = pfvf_counters->crc_err;
		break;
	case 6:
		string = "CSR in use";
		value = pfvf_counters->busy;
		break;
	case 7:
		string = "No acknowledgment";
		value = pfvf_counters->no_ack;
		break;
	case 8:
		string = "Collisions";
		value = pfvf_counters->collision;
		break;
	case 9:
		string = "Put msg timeout";
		value = pfvf_counters->tx_timeout;
		break;
	case 10:
		string = "No response received";
		value = pfvf_counters->rx_timeout;
		break;
	case 11:
		string = "Responses received";
		value = pfvf_counters->rx_rsp;
		break;
	case 12:
		string = "Messages re-transmitted";
		value = pfvf_counters->retry;
		break;
	case 13:
		string = "Put event timeout";
		value = pfvf_counters->event_timeout;
		break;
	}
	if (value)
		seq_printf(sfile, "%s %u\n", string, value);

	return 0;
}

static void adf_pfvf_stop(struct seq_file *sfile, void *v)
{
}

static const struct seq_operations adf_pfvf_sops = {
	.start = adf_pfvf_start,
	.next = adf_pfvf_next,
	.stop = adf_pfvf_stop,
	.show = adf_pfvf_show
};

static int pfvf_debugfs_open(struct inode *inode, struct file *file)
{
	int ret = seq_open(file, &adf_pfvf_sops);

	if (!ret) {
		struct seq_file *seq_f = file->private_data;

		seq_f->private = inode->i_private;
	}
	return ret;
}

static const struct file_operations pfvf_fops = {
	.open = pfvf_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release
};

int adf_pfvf_debugfs_add(struct adf_accel_dev *accel_dev)
{
	struct pci_dev *pdev = accel_to_pci_dev(accel_dev);
	int numvfs;
	char filename[ADF_PFVF_DEBUG_NAME_SZ] = {0};
	u8 vf;
	struct adf_accel_vf_info *vf_info;

	accel_dev->pfvf_dbgdir = debugfs_create_dir("pfvf",
						    accel_dev->debugfs_dir);
	if (!accel_dev->pfvf_dbgdir) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to create pf/vf debugfs directory\n");
		return -EFAULT;
	}

	if (accel_dev->is_vf) {
		accel_dev->vf.pfvf_counters.stats_file =
			debugfs_create_file("pf", 0400,
					    accel_dev->pfvf_dbgdir,
					    &accel_dev->vf.pfvf_counters,
					    &pfvf_fops);
	} else {
		numvfs = pci_num_vf(pdev);
		for (vf = 0, vf_info = accel_dev->pf.vf_info; vf < numvfs;
		     vf++, vf_info++) {
			snprintf(filename, sizeof(filename), "vf%u", vf);
			vf_info->pfvf_counters.stats_file =
				debugfs_create_file(filename, 0400,
						    accel_dev->pfvf_dbgdir,
						    &vf_info->pfvf_counters,
						    &pfvf_fops);
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(adf_pfvf_debugfs_add);
