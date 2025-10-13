/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2018, 2021 Intel Corporation */

#ifndef ADF_RAS_H
#define ADF_RAS_H

struct adf_accel_dev;

int adf_init_ras_4xxx(struct adf_accel_dev *accel_dev);
void adf_exit_ras_4xxx(struct adf_accel_dev *accel_dev);

#endif /* ADF_RAS_H */

