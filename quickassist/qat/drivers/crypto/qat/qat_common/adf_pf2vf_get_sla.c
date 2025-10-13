// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2024 Intel Corporation */

#include <linux/device.h>
#include "adf_accel_devices.h"
#include "adf_cfg.h"
#include "adf_common_drv.h"
#include "adf_pf2vf_msg.h"
#include "icp_qat_hw.h"

static int send_sla_request(struct adf_accel_dev *accel_dev, u32 msg_type, u32 msg_data)
{
	int response_received = 0;
	int retry_count = 0;
	unsigned long timeout = msecs_to_jiffies(ADF_IOV_MSG_RESP_TIMEOUT);
	struct pfvf_stats *pfvf_counters = NULL;

	pfvf_counters = &accel_dev->vf.pfvf_counters;

	reinit_completion(&accel_dev->vf.iov_msg_completion);
	if (adf_iov_putmsg(accel_dev, msg_type, msg_data, 0)) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to send CIR/PIR request\n");
		return -EIO;
	}

	do {
		if (!wait_for_completion_timeout
				(&accel_dev->vf.iov_msg_completion, timeout))
			dev_err(&GET_DEV(accel_dev),
				"IOV response message timeout\n");
		else
			response_received = 1;
	} while (!response_received &&
		 ++retry_count < ADF_IOV_MSG_RESP_RETRIES);

	if (!response_received)
		pfvf_counters->rx_timeout++;
	else
		pfvf_counters->rx_rsp++;

	if (!response_received) {
		dev_err(&GET_DEV(accel_dev),
			"No response received, stop sending further request\n");
		return -EIO;
	}
	return 0;
}

void adf_get_vf_rl_info(struct adf_accel_dev *accel_dev)
{
	u32 msg_type = 0;
	u32 msg_data = 0;
	int ring_num;
	int banks = GET_MAX_BANKS(accel_dev);
	struct adf_hw_device_data *hw_data = GET_HW_DATA(accel_dev);

	if (!(hw_data->accel_capabilities_mask & ICP_ACCEL_CAPABILITIES_RL) ||
	    accel_dev->vf.pf_version < ADF_PFVF_COMPATIBILITY_GET_SLA)
		return;

	for (ring_num = 0; ring_num < banks; ring_num++) {
		msg_data = ring_num;
		msg_type = ADF_VF2PF_MSGTYPE_CIR_REQ;
		if (send_sla_request(accel_dev, msg_type, msg_data)) {
			dev_err(&GET_DEV(accel_dev),
				"Failed to get CIR from PF\n");
			break;
		}

		msg_type = ADF_VF2PF_MSGTYPE_PIR_REQ;
		if (send_sla_request(accel_dev, msg_type, msg_data)) {
			dev_err(&GET_DEV(accel_dev),
				"Failed to get PIR from PF\n");
			break;
		}
	}
}
EXPORT_SYMBOL_GPL(adf_get_vf_rl_info);

void adf_pf_sla_value_provider(struct adf_accel_dev *accel_dev,
			       u32 ring_num, u32 *val, u32 msg_type)
{
}
EXPORT_SYMBOL_GPL(adf_pf_sla_value_provider);

void adf_pf2vf_notify_sla_update(struct adf_accel_dev *accel_dev, u16 ring_id,
				 u32 val, int msg_type)
{
	u8 vf_num = ring_id / GET_HW_DATA(accel_dev)->num_banks_per_vf;
	int num_vfs = pci_num_vf(accel_to_pci_dev(accel_dev));
	struct adf_accel_vf_info *vf = accel_dev->pf.vf_info;
	u32 msg_data = val << ADF_PFVF_RL_MSGDATA_SHIFT;

	/* Check if ring belongs to any enabled VF */
	if (vf_num >= num_vfs)
		return;
	vf += vf_num;

	msg_data |= ring_id % GET_HW_DATA(accel_dev)->num_banks_per_vf;
	if (vf->init && vf->compat_ver >= ADF_PFVF_COMPATIBILITY_GET_SLA &&
	    adf_iov_putmsg(accel_dev, msg_type, msg_data, vf_num))
		dev_err(&GET_DEV(accel_dev),
			"Failed to notify VF%d of msg_type %d\n", vf_num,
			msg_type);
}
