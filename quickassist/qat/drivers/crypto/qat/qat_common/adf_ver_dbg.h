/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2021 Intel Corporation */
#ifndef ADF_VER_DBG_H_
#define ADF_VER_DBG_H_

struct adf_accel_dev;

struct adf_ver {
	struct dentry *hw_version;
	struct dentry *fw_version;
	struct dentry *mmp_version;
	struct dentry *ver_dir;
};

int adf_ver_dbg_add(struct adf_accel_dev *accel_dev);
void adf_ver_dbg_del(struct adf_accel_dev *accel_dev);

#endif /* ADF_VER_DBG_H_ */
