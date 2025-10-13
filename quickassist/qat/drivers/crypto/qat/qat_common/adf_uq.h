/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2023 Intel Corporation */

#ifndef ADF_UQ_H_
#define ADF_UQ_H_

#define ADF_UQ_MODE_DISABLE		0
#define ADF_UQ_MODE_POLLING		1

#define ADF_UQ_GET_Q_MODE(accel_dev) ((accel_dev)->ring_mode)
#define ADF_UQ_SET_Q_MODE(accel_dev, mode) ((accel_dev)->ring_mode = mode)

int adf_uq_set_mode(struct adf_accel_dev *accel_dev,
		    u32 bank_number,
		    u8 mode);
#endif
