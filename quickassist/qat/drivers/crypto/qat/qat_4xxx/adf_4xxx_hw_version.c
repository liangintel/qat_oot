// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2018 Intel Corporation */

#include "adf_4xxx_hw_data.h"
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/io.h>
#include <adf_accel_devices.h>
#include <adf_common_drv.h>
#include <adf_cfg.h>

/* PSDID register in config space */
#define ADF_4XXX_PSID_OFFSET 0x2e
/* 17 read required of PSID register */
#define ADF_4XXX_PSID_NUM_READS 17
/* String buffer size */
#define HW_VERSION_BUFFER_SIZE 20
/* Ascii value of 'a' */
#define ASCII_A 0x61

/* PSID index value */
#define PLATFORM_TYPE_INDEX 0
#define VERSION_INDEX 3
#define NUM_ME_CPP0_INDEX 6
#define NUM_ME_CPP1_INDEX 7
#define NUM_SSM_CPP0_INDEX 9
#define NUM_SSM_CPP1_INDEX 10
#define NUM_SSM_CPP0_INDEX 9
#define NUM_SSM_CPP1_INDEX 10
#define NUM_CIPH_INDEX 11
#define NUM_PKE_INDEX 12
#define NUM_CPR_XLT_INDEX 13
#define NUM_AUTH_INDEX 14
#define ARAM_SIZE_INDEX 16

static DEFINE_MUTEX(hw_version_read_lock);

static void adf_print_hw_version_data(struct seq_file *sfile)
{
	struct adf_accel_dev *accel_dev = sfile->private;
	struct pci_dev *pdev = accel_dev->accel_pci_dev.pci_dev;
	u8 psid_read_index = 0;
	u16 reg_value = 0;
	u8 year;
	u8 week;
	u8 rel_num;
	u8 rev;
	char tempBuffer[HW_VERSION_BUFFER_SIZE];

	/* Reset tag in PSID register */
	pci_write_config_word(pdev, ADF_4XXX_PSID_OFFSET, 0);

	do {
		pci_read_config_word(pdev, ADF_4XXX_PSID_OFFSET, &reg_value);
		/* Decode emulation model information */
		switch (psid_read_index) {
		case PLATFORM_TYPE_INDEX:
			seq_printf(sfile, "Platform type: %s\n",
				   ((reg_value & 0x7f) == 0) ? "FPGA" : "VP");
			break;
		case VERSION_INDEX:
			/* Read and decode the release version */

			/* Decode release number and year */
			memset(tempBuffer, '\0', HW_VERSION_BUFFER_SIZE);
			year = reg_value & 0x7f;
			rel_num = (u8)((reg_value >> 8) & 0xff);

			/* Decode week number and revision */
			psid_read_index++;
			pci_read_config_word(pdev, ADF_4XXX_PSID_OFFSET,
					     &reg_value);
			week = (u8)((reg_value >> 8) & 0xff);
			rev = ASCII_A + (reg_value & 0xff);
			snprintf(tempBuffer, sizeof(tempBuffer), "%d %dww%d%c",
				 rel_num, year, week, rev);
			seq_printf(sfile, "Release version: %s\n", tempBuffer);
			break;
		case NUM_ME_CPP0_INDEX:
			seq_printf(sfile, "Number of MEs on CPP0: %d\n",
				   reg_value & 0xff);
			break;
		case NUM_ME_CPP1_INDEX:
			seq_printf(sfile, "Number of MEs on CPP1: %d\n",
				   reg_value & 0xff);
			break;
		case NUM_SSM_CPP0_INDEX:
			seq_printf(sfile, "Number of SSMs on CPP0: %d\n",
				   reg_value & 0xff);
			break;
		case NUM_SSM_CPP1_INDEX:
			seq_printf(sfile, "Number of SSMs on CPP1: %d\n",
				   reg_value & 0xff);
			break;
		case NUM_CIPH_INDEX:
			seq_printf(sfile, "Number of Cipher slices: %d\n",
				   reg_value & 0xff);
			break;
		case NUM_PKE_INDEX:
			seq_printf(sfile, "Number of PKE slices: %d\n",
				   reg_value & 0xff);
			break;
		case NUM_CPR_XLT_INDEX:
			seq_printf(sfile, "Number of CPR, XLT slices: %d\n",
				   reg_value & 0xff);
			break;
		case NUM_AUTH_INDEX:
			seq_printf(sfile, "Number of Auth slices: %d\n",
				   reg_value & 0xff);
			break;
		case ARAM_SIZE_INDEX:
			seq_printf(sfile, "ARAM size: %dKB\n", reg_value);
			seq_printf(sfile, "\n");
		}
		psid_read_index++;
	} while (psid_read_index < ADF_4XXX_PSID_NUM_READS);
}

static void *adf_hw_version_start(struct seq_file *sfile, loff_t *pos)
{
	mutex_lock(&hw_version_read_lock);

	if (*pos == 0)
		return SEQ_START_TOKEN;
	else
		return NULL;
}

static int adf_hw_version_show(struct seq_file *sfile, void *v)
{
	if (v == SEQ_START_TOKEN) {
		/* Display hw version data */
		adf_print_hw_version_data(sfile);
	}

	return 0;
}

static void *adf_hw_version_next(struct seq_file *sfile, void *v, loff_t *pos)
{
	return NULL;
}

static void adf_hw_version_stop(struct seq_file *sfile, void *v)
{
	mutex_unlock(&hw_version_read_lock);
}

static const struct seq_operations adf_hw_version_sops = {
	.start = adf_hw_version_start,
	.next = adf_hw_version_next,
	.stop = adf_hw_version_stop,
	.show = adf_hw_version_show
};

static int adf_4xxx_hw_version_open(struct inode *inode, struct file *file)
{
	int ret = seq_open(file, &adf_hw_version_sops);

	if (!ret) {
		struct seq_file *seq_f = file->private_data;

		seq_f->private = inode->i_private;
	}
	return ret;
}

static const struct file_operations adf_4xxx_hw_version_debug_fops = {
	.owner = THIS_MODULE,
	.open = adf_4xxx_hw_version_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release
};

static int adf_4xxx_add_debugfs_hw_version(struct adf_accel_dev *accel_dev)
{
	struct dentry *debugfs_hw_version = NULL;
	struct adf_accel_pci *accel_pci_dev = &accel_dev->accel_pci_dev;
	struct pci_dev *pdev = accel_pci_dev->pci_dev;

	/* Create hw_version debug file */
	debugfs_hw_version =
		debugfs_create_file("hw_version",
				    S_IRUSR,
				    accel_dev->debugfs_dir,
				    accel_dev,
				    &adf_4xxx_hw_version_debug_fops);
	if (!debugfs_hw_version) {
		dev_err(&pdev->dev, "Could not create debug hw version entry.\n");
		return -EFAULT;
	}
	accel_dev->debugfs_hw_version = debugfs_hw_version;

	return 0;
}

int adf_4xxx_init_hw_version(struct adf_accel_dev *accel_dev)
{
	int ret = 0;
	struct adf_accel_pci *accel_pci_dev = &accel_dev->accel_pci_dev;
	struct pci_dev *pdev = accel_pci_dev->pci_dev;

	/* Add a new file in debug file system with h/w version. */
	ret = adf_4xxx_add_debugfs_hw_version(accel_dev);
	if (ret) {
		adf_4xxx_exit_hw_version(accel_dev);
		dev_err(&pdev->dev, "Could not create debugfs "
			"hw version file\n");
		return -EINVAL;
	}

	return 0;
}

void adf_4xxx_exit_hw_version(struct adf_accel_dev *accel_dev)
{
	/* Delete h/w version file */
	if (accel_dev->debugfs_hw_version) {
		debugfs_remove(accel_dev->debugfs_hw_version);
		accel_dev->debugfs_hw_version = NULL;
	}
}
