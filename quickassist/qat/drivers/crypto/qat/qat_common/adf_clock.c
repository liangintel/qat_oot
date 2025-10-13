// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2021 Intel Corporation */
#include <linux/delay.h>
#include <linux/debugfs.h>

#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "adf_dev_err.h"

#define MEASURE_CLOCK_RETRIES 10
#define MEASURE_CLOCK_DELAY 10000
#define ME_CLK_DIVIDER 16
#define MEASURE_CLOCK_DELTA_THRESHOLD 100

#define CLK_DBGFS_FILE "frequency"

#ifdef CONFIG_DEBUG_FS
static int clock_debugfs_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t clock_debugfs_read(struct file *file, char __user *user_buf,
				  size_t count, loff_t *ppos)
{
	struct adf_accel_dev *accel_dev = file->private_data;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u32 speed;

	char buf[16] = {0};
	int len = 0;

	speed = hw_data->clock_frequency;

	len = scnprintf(buf, sizeof(buf), "%u\n", speed);
	if (len < 0)
		return -EFAULT;
	return simple_read_from_buffer(user_buf, count, ppos, buf, len + 1);
}

static const struct file_operations clock_fops = {
	.open = clock_debugfs_open,
	.read = clock_debugfs_read,
};
#endif

int adf_clock_debugfs_add(struct adf_accel_dev *accel_dev)
{
	accel_dev->clock_dbgfile = debugfs_create_file(CLK_DBGFS_FILE, 0400,
						       accel_dev->debugfs_dir,
						       accel_dev,
						       &clock_fops);
	if (!accel_dev->clock_dbgfile) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to create frequency debugfs entry\n");
		return -EFAULT;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(adf_clock_debugfs_add);

static inline s64 timespec_to_us(const struct timespec64 *ts)
{
	return ((s64)ts->tv_sec * USEC_PER_SEC +
		 (ts->tv_nsec + NSEC_PER_USEC / 2) / NSEC_PER_USEC);
}

static inline u64 timespec_to_ms(const struct timespec64 *ts)
{
	return (uint64_t)(ts->tv_sec * MSEC_PER_SEC)
		+ (ts->tv_nsec / NSEC_PER_MSEC);
}

/**
 * measure_clock() -- Measure the CPM clock frequency
 * @accel_dev: Pointer to acceleration device.
 * @frequency: Pointer to returned frequency in Hz.
 *
 * Return: 0 on success, error code otherwise.
 */
static int measure_clock(struct adf_accel_dev *accel_dev,
			 u32 *frequency)
{
	struct timespec64 ts1;
	struct timespec64 ts2;
	struct timespec64 ts3;
	struct timespec64 ts4;
	u64 delta_us = 0;
	u64 timestamp1 = 0;
	u64 timestamp2 = 0;
	u64 temp = 0;
	int tries = 0;

	if (!accel_dev || !frequency)
		return -EIO;

	do {
		ktime_get_real_ts64(&ts1);
		if (adf_get_fw_timestamp(accel_dev, &timestamp1)) {
			dev_err(&GET_DEV(accel_dev),
				"Failed to get fw timestamp\n");
			return -EIO;
		}
		ktime_get_real_ts64(&ts2);
		delta_us = timespec_to_us(&ts2) - timespec_to_us(&ts1);
	} while (delta_us > MEASURE_CLOCK_DELTA_THRESHOLD &&
		 ++tries < MEASURE_CLOCK_RETRIES);
	if (tries >= MEASURE_CLOCK_RETRIES) {
		dev_err(&GET_DEV(accel_dev), "Excessive clock measure delay\n");
		return -EIO;
	}

	usleep_range(MEASURE_CLOCK_DELAY, MEASURE_CLOCK_DELAY * 2);

	tries = 0;
	do {
		ktime_get_real_ts64(&ts3);
		if (adf_get_fw_timestamp(accel_dev, &timestamp2)) {
			dev_err(&GET_DEV(accel_dev),
				"Failed to get fw timestamp\n");
			return -EIO;
		}
		ktime_get_real_ts64(&ts4);
		delta_us = timespec_to_us(&ts4) - timespec_to_us(&ts3);
	} while (delta_us > MEASURE_CLOCK_DELTA_THRESHOLD &&
		 ++tries < MEASURE_CLOCK_RETRIES);
	if (tries >= MEASURE_CLOCK_RETRIES) {
		dev_err(&GET_DEV(accel_dev), "Excessive clock measure delay\n");
		return -EIO;
	}

	delta_us = timespec_to_us(&ts3) - timespec_to_us(&ts1);

	/* Don't pretend that this gives better than 100KHz resolution */
	temp = (timestamp2 - timestamp1) * ME_CLK_DIVIDER * 10 + (delta_us / 2);
	do_div(temp, delta_us);
	*frequency = temp * 100000;

	return 0;
}

/**
 * adf_dev_measure_clock() -- Measure the CPM clock frequency
 * @accel_dev: Pointer to acceleration device.
 * @frequency: Pointer to returned frequency in Hz.
 * @min: Minimum expected frequency
 * @max: Maximum expected frequency
 *
 * Return: 0 on success, error code otherwise.
 */
int adf_dev_measure_clock(struct adf_accel_dev *accel_dev,
			  u32 *frequency, u32 min, u32 max)
{
	int ret = 0;
	u32 freq = 0;

	ret = measure_clock(accel_dev, &freq);
	if (ret)
		return ret;

	if (freq < min) {
		dev_warn(&GET_DEV(accel_dev),
			 "Slow clock %d Hz measured, assuming %d\n",
			 freq, min);
		freq = min;
	} else if (freq > max) {
		dev_warn(&GET_DEV(accel_dev),
			 "Fast clock %d Hz measured, assuming %d\n",
			 freq, max);
		freq = max;
	}
	*frequency = freq;
	return 0;
}
EXPORT_SYMBOL_GPL(adf_dev_measure_clock);

u64 adf_clock_get_current_time(void)
{
	struct timespec64 ts;

	ktime_get_real_ts64(&ts);
	return timespec_to_ms(&ts);
}
