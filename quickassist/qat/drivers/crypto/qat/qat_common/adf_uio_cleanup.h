/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2021 Intel Corporation */

#ifndef ADF_UIO_CLEANUP_H
#define ADF_UIO_CLEANUP_H

void adf_uio_do_cleanup_orphan(struct uio_info *info,
			       struct adf_uio_control_accel *accel,
			       u32 pid, u8 *comm);
#endif
