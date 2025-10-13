/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2019 - 2021 Intel Corporation */
#ifndef ADF_GEN4_KPT_H_
#define ADF_GEN4_KPT_H_

#define ADF_GEN4_KPT_COUNTER_FREQ (100 * 1000000)

struct adf_accel_dev;
int adf_gen4_init_kpt(struct adf_accel_dev *accel_dev);
int adf_gen4_config_kpt(struct adf_accel_dev *accel_dev);
#endif
