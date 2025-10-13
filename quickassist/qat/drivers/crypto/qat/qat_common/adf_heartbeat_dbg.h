/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2021 Intel Corporation */
#ifndef ADF_HEARTBEAT_DBG_H_
#define ADF_HEARTBEAT_DBG_H_

struct adf_accel_dev;

int adf_heartbeat_dbg_add(struct adf_accel_dev *accel_dev);
void adf_heartbeat_dbg_del(struct adf_accel_dev *accel_dev);

#endif /* ADF_HEARTBEAT_DBG_H_ */
