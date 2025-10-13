/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2021 Intel Corporation */
#ifndef ADF_HEARTBEAT_H_
#define ADF_HEARTBEAT_H_

struct adf_accel_dev;
enum adf_device_heartbeat_status;

struct adf_heartbeat {
	unsigned int hb_sent_counter;
	unsigned int hb_failed_counter;
	unsigned int hb_timer;
	u64 last_hb_check_time;
	struct dentry *heartbeat;
	struct dentry *heartbeat_sent;
	struct dentry *heartbeat_failed;
#ifdef QAT_HB_FAIL_SIM
	struct dentry *heartbeat_sim_fail;
#endif
};

int adf_heartbeat_init(struct adf_accel_dev *accel_dev);
void adf_heartbeat_clean(struct adf_accel_dev *accel_dev);
int adf_get_hb_timer(struct adf_accel_dev *accel_dev, unsigned int *value);
int adf_get_heartbeat_status(struct adf_accel_dev *accel_dev);
int adf_heartbeat_status(struct adf_accel_dev *accel_dev,
			 enum adf_device_heartbeat_status *hb_status);
#ifdef QAT_HB_FAIL_SIM
int adf_disable_arbiter(struct adf_accel_dev *accel_dev,
			u32 ae,
			u32 thr);
int adf_heartbeat_simulate_failure(struct adf_accel_dev *accel_dev);
#endif

#endif /* ADF_HEARTBEAT_H_ */
