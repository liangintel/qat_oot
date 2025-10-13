/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2021 Intel Corporation */
#ifndef ADF_CFG_SECTION_H_
#define ADF_CFG_SECTION_H_

#include <linux/list.h>
#include <linux/rwsem.h>
#include <linux/debugfs.h>
#include "adf_accel_devices.h"
#include "adf_cfg.h"
#include "adf_cfg_common.h"
#include "adf_cfg_strings.h"
#include "adf_cfg_instance.h"
#include "adf_cfg_device.h"

int adf_cfg_process_section(struct adf_accel_dev *accel_dev,
			    const char *section_name,
			    int dev);

int adf_cfg_process_filter_by(struct adf_accel_dev *accel_dev,
			      const char *section_name);

int adf_cfg_cleanup_section(struct adf_accel_dev *accel_dev,
			    const char *section_name,
			    int dev);
#endif
