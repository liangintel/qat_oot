/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2021 Intel Corporation */
#ifndef ADF_CFG_DEV_DBG_H_
#define ADF_CFG_DEV_DBG_H_

struct adf_accel_dev;

int adf_cfg_dev_dbg_add(struct adf_accel_dev *accel_dev);
void adf_cfg_dev_dbg_remove(struct adf_accel_dev *accel_dev);

#endif /* ADF_CFG_DEV_DBG_H_ */
