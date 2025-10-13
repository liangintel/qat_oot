// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2021 Intel Corporation */
#include <linux/device.h>
#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "adf_pf2vf_msg.h"
#include "adf_cfg.h"

#define ADF_VF2PF_RING_TO_SVC_VERSION 1
#define ADF_VF2PF_RING_TO_SVC_LENGTH  2

#ifdef QAT_UIO
static void adf_vf_update_hw_capability(struct adf_accel_dev *accel_dev)
{
	u16 ring_to_svc_map = accel_dev->hw_device->ring_to_svc_map;
	int ring_pair_index = 0;
	bool asym = 0, sym = 0, cy = 0;
	u8 serv_type = 0;

	for (ring_pair_index = 0;
		ring_pair_index < ADF_CFG_NUM_SERVICES;
			ring_pair_index++) {
		serv_type =
			GET_SRV_TYPE(ring_to_svc_map,
				     ring_pair_index);
		switch (serv_type) {
		case ASYM:
			asym = true;
			break;
		case SYM:
			sym = true;
			break;
		case CRYPTO:
			cy = true;
			break;
		default:
			break;
		}
	}
	/* If extended DC capabilities, then we also need SYM capabilities */
	if (accel_dev->chaining_enabled)
		sym = true;
	if (!asym && !cy)
		accel_dev->hw_device->accel_capabilities_mask &=
			~ADF_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC;
	if (!sym && !cy)
		accel_dev->hw_device->accel_capabilities_mask &=
			~ADF_ACCEL_CAPABILITIES_CRYPTO_SYMMETRIC;
}

#endif
static int adf_pf_ring_to_svc_msg_provider(struct adf_accel_dev *accel_dev,
					   u8 **buffer, u8 *length,
					   u8 *block_version, u8 compatibility,
					   u8 byte_num, u32 vf_nr)
{
	static u8 data[ADF_VF2PF_RING_TO_SVC_LENGTH] = {0};
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u16 ring_to_svc_map = hw_data->ring_to_svc_map;
	u16 byte = 0;

	for (byte = 0; byte < ADF_VF2PF_RING_TO_SVC_LENGTH; byte++) {
		data[byte] =
			(ring_to_svc_map >> (byte * ADF_PFVF_DATA_SHIFT))
				& ADF_PFVF_DATA_MASK;
	}

	*length = ADF_VF2PF_RING_TO_SVC_LENGTH;
	*block_version = ADF_VF2PF_RING_TO_SVC_VERSION;
	*buffer = data;

	return 0;
}

int adf_pf_vf_ring_to_svc_init(struct adf_accel_dev *accel_dev)
{
	u8 data[ADF_VF2PF_RING_TO_SVC_LENGTH] = {0};
	u8 len = ADF_VF2PF_RING_TO_SVC_LENGTH;
	u8 version = ADF_VF2PF_RING_TO_SVC_VERSION;
	u16 ring_to_svc_map  = 0;
	u16 byte = 0;

	if (!accel_dev->is_vf) {
		/* on the pf */
		if (!adf_iov_is_block_provider_registered(
		    ADF_VF2PF_BLOCK_MSG_GET_RING_TO_SVC_REQ))
			adf_iov_block_provider_register(
			ADF_VF2PF_BLOCK_MSG_GET_RING_TO_SVC_REQ,
			adf_pf_ring_to_svc_msg_provider);
	} else  if (accel_dev->vf.pf_version >=
		    ADF_PFVF_COMPATIBILITY_RING_TO_SVC_MAP) {
		/* on the vf */
		if (adf_iov_block_get
				(accel_dev,
				 ADF_VF2PF_BLOCK_MSG_GET_RING_TO_SVC_REQ,
				 &version, data, &len)) {
			dev_err(&GET_DEV(accel_dev),
				"QAT: Failed adf_iov_block_get\n");
			return -EFAULT;
		}
		for (byte = 0; byte < ADF_VF2PF_RING_TO_SVC_LENGTH; byte++) {
			ring_to_svc_map |=
				data[byte] << (byte * ADF_PFVF_DATA_SHIFT);
		}
		accel_dev->hw_device->ring_to_svc_map = ring_to_svc_map;
		accel_dev->hw_device->get_ring_to_svc_done = true;
#ifdef QAT_UIO
		adf_vf_update_hw_capability(accel_dev);
#endif
	}

	return 0;
}
