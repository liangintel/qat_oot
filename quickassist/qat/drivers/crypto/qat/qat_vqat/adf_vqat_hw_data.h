/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2021 Intel Corporation */
#ifndef ADF_VQAT_HW_DATA_H_
#define ADF_VQAT_HW_DATA_H_

#include "adf_vdcm.h"

#define ADF_VQAT_DEVICE_NAME "vqat-adi"

#define ADF_VQAT_ACCELERATORS_MASK 0x1
#define ADF_VQAT_ACCELENGINES_MASK 0x1
#define ADF_VQAT_MAX_ACCELERATORS 1
#define ADF_VQAT_MAX_ACCELENGINES 1
#define ADF_VQAT_NUM_RINGS_PER_BANK 2
#define ADF_VQAT_RX_RINGS_OFFSET 1
#define ADF_VQAT_TX_RINGS_MASK 0x1
#define ADF_VQAT_ETR_MAX_BANKS 1
#define ADF_VQAT_CAP_HDR_SIZE 8

#ifdef QAT_UIO
#define ADF_VQAT_DEF_ASYM_MASK 0x1
#endif

/* Interrupt Coalesce Timer Defaults */
#define ADF_VQAT_ACCEL_DEF_COALESCE_TIMER 1000
#define ADF_VQAT_COALESCING_MIN_TIME 0x1FF
#define ADF_VQAT_COALESCING_MAX_TIME 0xFFFF
#define ADF_VQAT_COALESCING_DEF_TIME 0x1FF

struct adf_vqat_data {
	void *cap_data;
	u16 cap_size;
};

void adf_init_hw_data_vqat(struct adf_hw_device_data *hw_data);
void adf_clean_hw_data_vqat(struct adf_hw_device_data *hw_data);
int adf_vqat_get_ring_to_svc_map(struct adf_accel_dev *accel_dev, u16 *map);
int adf_vqat_get_cap(struct adf_accel_dev *accel_dev);
void adf_vqat_cfg_get_accel_algo_cap(struct adf_accel_dev *accel_dev);
#endif
