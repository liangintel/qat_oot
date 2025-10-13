/* SPDX-License-Identifier: GPL-2.0-only*/
/* Copyright(c) 2022 Intel Corporation */
#ifndef _QAT_COMPAT_AUX_H_
#define _QAT_COMPAT_AUX_H_

#ifndef QAT_NO_AUX
#if (KERNEL_VERSION(5, 11, 0) > LINUX_VERSION_CODE)
#define QAT_NO_AUX
struct auxiliary_device;
struct adf_accel_dev;
int adf_add_aux_dev(struct adf_accel_dev *accel_dev);
int adf_del_aux_dev(struct adf_accel_dev *accel_dev);
int adf_enable_aux_dev(struct adf_accel_dev *accel_dev);
int adf_add_vf_aux_dev(struct adf_accel_dev *accel_dev);
int adf_del_vf_aux_dev(struct adf_accel_dev *accel_dev);
#endif /* 5.11.0 */
#endif /* QAT_NO_AUX */
#endif

