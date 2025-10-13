// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2018, 2021 Intel Corporation */

#include <linux/sysfs.h>
#include <linux/pci.h>
#include <linux/bitops.h>
#include <linux/atomic.h>
#include <linux/string.h>

#include "adf_accel_devices.h"
#include "adf_4xxx_hw_data.h"
#include "adf_4xxx_ras.h"
#include "adf_gen4_ras.h"

#include <adf_dev_err.h>

static ssize_t
adf_ras_show(struct device *dev, struct device_attribute *dev_attr, char *buf)
{
	struct adf_accel_dev *accel_dev = pci_get_drvdata(to_pci_dev(dev));
	struct attribute *attr = &dev_attr->attr;
	unsigned long counter;

	if (!strcmp(attr->name, "ras_correctable")) {
		counter = atomic_read(&accel_dev->ras_counters[ADF_RAS_CORR]);
	} else if (!strcmp(attr->name, "ras_uncorrectable")) {
		counter = atomic_read(&accel_dev->ras_counters[ADF_RAS_UNCORR]);
	} else if (!strcmp(attr->name, "ras_fatal")) {
		counter = atomic_read(&accel_dev->ras_counters[ADF_RAS_FATAL]);
	} else {
		dev_err(&GET_DEV(accel_dev), "Unknown attribute %s\n",
			attr->name);
		return -EFAULT;
	}

	return scnprintf(buf, PAGE_SIZE, "%ld\n", counter);
}

DEVICE_ATTR(ras_correctable, S_IRUSR|S_IRGRP|S_IROTH, adf_ras_show, NULL);
DEVICE_ATTR(ras_uncorrectable, S_IRUSR|S_IRGRP|S_IROTH, adf_ras_show, NULL);
DEVICE_ATTR(ras_fatal, S_IRUSR|S_IRGRP|S_IROTH, adf_ras_show, NULL);

static ssize_t
adf_ras_store(struct device *dev, struct device_attribute *dev_attr,
	      const char *buf, size_t count)
{
	struct adf_accel_dev *accel_dev = pci_get_drvdata(to_pci_dev(dev));
	struct attribute *attr = &dev_attr->attr;

	if (!strcmp(attr->name, "ras_reset")) {
		if (buf[0] != '0' || count != 2)
			return -EINVAL;

		atomic_set(&accel_dev->ras_counters[ADF_RAS_CORR], 0);
		atomic_set(&accel_dev->ras_counters[ADF_RAS_UNCORR], 0);
		atomic_set(&accel_dev->ras_counters[ADF_RAS_FATAL], 0);
	} else {
		dev_err(&GET_DEV(accel_dev), "Unknown attribute %s\n",
			attr->name);
		return -EFAULT;
	}

	return count;
}

DEVICE_ATTR(ras_reset, S_IWUSR, NULL, adf_ras_store);

int adf_init_ras_4xxx(struct adf_accel_dev *accel_dev)
{
	int i;

	accel_dev->ras_counters = kcalloc(ADF_RAS_ERRORS,
					  sizeof(*(accel_dev->ras_counters)),
					  GFP_KERNEL);
	if (!accel_dev->ras_counters)
		return -ENOMEM;

	for (i = 0; i < ADF_RAS_ERRORS; ++i)
		atomic_set(&accel_dev->ras_counters[i], 0);
	pci_set_drvdata(accel_to_pci_dev(accel_dev), accel_dev);
	device_create_file(&GET_DEV(accel_dev), &dev_attr_ras_correctable);
	device_create_file(&GET_DEV(accel_dev), &dev_attr_ras_uncorrectable);
	device_create_file(&GET_DEV(accel_dev), &dev_attr_ras_fatal);
	device_create_file(&GET_DEV(accel_dev), &dev_attr_ras_reset);

	adf_gen4_enable_ras(accel_dev);

	return 0;
}

void adf_exit_ras_4xxx(struct adf_accel_dev *accel_dev)
{
	adf_gen4_disable_ras(accel_dev);

	if (accel_dev->ras_counters) {
		device_remove_file(&GET_DEV(accel_dev), &dev_attr_ras_correctable);
		device_remove_file(&GET_DEV(accel_dev), &dev_attr_ras_uncorrectable);
		device_remove_file(&GET_DEV(accel_dev), &dev_attr_ras_fatal);
		device_remove_file(&GET_DEV(accel_dev), &dev_attr_ras_reset);
		kfree(accel_dev->ras_counters);
		accel_dev->ras_counters = NULL;
	}
}
