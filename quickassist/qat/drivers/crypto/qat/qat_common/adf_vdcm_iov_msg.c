// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2019 - 2020 Intel Corporation */

#include <linux/device.h>
#include <linux/uuid.h>
#include <linux/mdev.h>
#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "adf_pf2vf_msg.h"
#include "adf_vdcm_iov.h"
#include "adf_vdcm.h"

/* IOV message with data */
int adf_sw_iov_putmsg(struct adf_iov_transport *transport, u16 msg_type,
		      u8 *msg_data, u16 len)
{
	struct adf_iov_msg msg;

	msg.type = msg_type;
	msg.data = msg_data;
	msg.len = len;

	if (adf_iov_trans_put_msg(transport, &msg) < 0)
		return -EFAULT;

	return 0;
}

/* IOV message without data */
int adf_sw_iov_notify(struct adf_iov_transport *transport, u16 msg_type)
{
	struct adf_iov_msg msg;

	msg.type = msg_type;
	msg.data = NULL;
	msg.len = 0;

	if (adf_iov_trans_put_msg(transport, &msg) < 0)
		return -EFAULT;

	return 0;
}

/* IOV message initiated by vQAT */
int adf_vqat2vdcm_init(struct adf_accel_dev *accel_dev)
{
	u16 msg_type = ADF_VF2PF_MSGTYPE_INIT;
	struct adf_iov_transport *transport = accel_dev->vf.iov_transport;

	if (adf_sw_iov_notify(transport, msg_type)) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to send Init event to VDCM\n");
		return -EFAULT;
	}
	set_bit(ADF_STATUS_PF_RUNNING, &accel_dev->status);
	return 0;
}
EXPORT_SYMBOL_GPL(adf_vqat2vdcm_init);

int adf_vqat_compat_version_checker(struct adf_accel_dev *accel_dev,
				    u8 pf_compat_ver)
{
	/* If VDCM is newer than vQAT driver, the compatibility
	 * is determined by VDCM, we should not be here,
	 * so VDCM should be older than vQAT driver and reports
	 * UNKNOWN at this point. Add new checker instead of version
	 * checker if a new vQAT driver comes out.
	 **/
	return ADF_PF2VF_VF_COMPATIBLE;
}

void adf_vqat2vdcm_shutdown(struct adf_accel_dev *accel_dev)
{
	u16 msg_type = ADF_VF2PF_MSGTYPE_SHUTDOWN;
	struct adf_iov_transport *transport = accel_dev->vf.iov_transport;

	if (!transport)
		return;

	if (test_bit(ADF_STATUS_PF_RUNNING, &accel_dev->status))
		if (adf_sw_iov_notify(transport, msg_type))
			dev_err(&GET_DEV(accel_dev),
				"Failed to send Shutdown event to VDCM\n");
}
EXPORT_SYMBOL_GPL(adf_vqat2vdcm_shutdown);

int adf_vqat2vdcm_req_version(struct adf_accel_dev *accel_dev)
{
	struct adf_iov_transport *transport = accel_dev->vf.iov_transport;
	unsigned long timeout = msecs_to_jiffies(ADF_IOV_MSG_RESP_TIMEOUT);
	u16 msg_type = ADF_VF2PF_MSGTYPE_COMPAT_VER_REQ;
	struct adf_sw_iov_compat_version_req req;
	int ret = 0;
	int compat = 0;
	int response_received = 0;
	int retry_count = 0;
	struct pfvf_stats *pfvf_counters = &transport->pfvf_counters;

	req.version = ADF_VQAT_COMPATIBILITY_VERSION;
	do {
		/* Send request from vQAT to VDCM */
		if (retry_count)
			pfvf_counters->retry++;
		if (adf_sw_iov_putmsg(transport, msg_type,
				      (u8 *)&req, sizeof(req)) < 0) {
			dev_err(&GET_DEV(accel_dev),
				"Failed to send Compat Version Request.\n");
			return -EIO;
		}

		/* Wait for response */
		if (!wait_for_completion_timeout
				(&accel_dev->vf.iov_msg_completion, timeout))
			dev_err(&GET_DEV(accel_dev),
				"IOV response message timeout with retry %d\n",
				retry_count);
		else
			response_received = 1;
	} while (!response_received &&
		 ++retry_count < ADF_IOV_MSG_RESP_RETRIES);

	if (!response_received)
		pfvf_counters->rx_timeout++;
	else
		pfvf_counters->rx_rsp++;
	if (!response_received)
		return -EIO;

	if (accel_dev->vf.compatible == ADF_PF2VF_VF_COMPAT_UNKNOWN)
		/* Response from VDCM received, check compatibility */
		compat = adf_iov_compatibility_check(accel_dev,
						     accel_dev->cm,
						     accel_dev->vf.pf_version);
	else
		compat = accel_dev->vf.compatible;

	ret = (compat == ADF_PF2VF_VF_COMPATIBLE) ? 0 : -EFAULT;
	if (ret)
		dev_err(&GET_DEV(accel_dev),
			"VQAT is not compatible with VDCM, due to the reason %d\n",
			compat);

	return ret;
}

#ifdef CONFIG_CRYPTO_DEV_QAT_VDCM
/* IOV message initiated by VDCM */
void adf_vdcm2vqat_restarting(struct adf_iov_vx_agent *iov_agent,
			      struct adf_vdcm_vqat *vqat)
{
	struct adf_iov_transport *transport = &iov_agent->transport;
	u16 msg_type = ADF_PF2VF_MSGTYPE_RESTARTING;

	if (adf_sw_iov_notify(transport, msg_type))
		dev_err(&GET_DEV(vqat->parent),
			"Failed to send restarting message to iov%d\n",
			transport->id);

	/* Interrupt vQAT for the IOV message */
	adf_vdcm_notify_vqat_iov(vqat, 0);
}

void adf_vdcm2vqat_fatal_error(struct adf_iov_vx_agent *iov_agent,
			       struct adf_vdcm_vqat *vqat)
{
	struct adf_iov_transport *transport = &iov_agent->transport;
	u16 msg_type = ADF_PF2VF_MSGTYPE_FATAL_ERROR;

	if (adf_sw_iov_notify(transport, msg_type))
		dev_err(&GET_DEV(vqat->parent),
			"Failed to send fatal error message to iov%d\n",
			transport->id);

	/* Interrupt vQAT for the IOV message */
	adf_vdcm_notify_vqat_iov(vqat, 0);
}

/* The checker based on version number only */
int adf_vdcm_compat_version_checker(struct adf_accel_dev *accel_dev,
				    u8 vf_compat_ver)
{
	return abs((int)vf_compat_ver - ADF_VDCM_COMPATIBILITY_VERSION)
		   <= ADF_PFVF_COMPATIBILITY_MIN_REQ ?
		   ADF_PF2VF_VF_COMPATIBLE : ADF_PF2VF_VF_COMPAT_UNKNOWN;
}

/* IOV message handler for VDCM */
void adf_vdcm_iov_handle_vqat_msg(struct adf_iov_vx_agent *iov_agent,
				  struct adf_vdcm_vqat *vqat)
{
	struct adf_iov_transport *transport = &iov_agent->transport;
	struct adf_accel_dev *accel_dev = vqat->parent;
	u16 resp_type = 0;
	u8 *resp_data = NULL;
	u16 resp_len;
	struct adf_iov_msg msg;
	u8 msg_data[ADF_SW_IOV_MAX_MSGLEN];
	struct adf_sw_iov_compat_version_req *req = NULL;
	struct adf_sw_iov_compat_version_resp resp;
	struct adf_accel_compat_manager *cm;

	msg.len = ADF_SW_IOV_MAX_MSGLEN;
	msg.data = msg_data;

	if (adf_iov_trans_get_msg(transport, &msg) < 0) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to get iov message from iov%d\n",
			transport->id);
		goto out;
	}

	switch (msg.type) {
	case ADF_VF2PF_MSGTYPE_COMPAT_VER_REQ:
	{
		req = (struct adf_sw_iov_compat_version_req *)msg.data;
		iov_agent->compat_ver = req->version;
		resp_type = ADF_PF2VF_MSGTYPE_VERSION_RESP;
		resp.version = ADF_VDCM_COMPATIBILITY_VERSION;

		/* check compatibility */
		dev_dbg(&GET_DEV(accel_dev),
			"Compatibility Version Request from VQAT(%s) vers=%u\n",
			dev_name(mdev_dev(vqat->mdev)),
			req->version);

		if (req->version != ADF_VDCM_COMPATIBILITY_VERSION) {
			cm = adf_vdcm_get_cm(accel_dev->vdcm);
			resp.compatible =
				adf_iov_compatibility_check(accel_dev,
							    cm,
							    (u8)req->version);
		} else {
			resp.compatible = ADF_PF2VF_VF_COMPATIBLE;
		}

		if (resp.compatible == ADF_PF2VF_VF_INCOMPATIBLE)
			dev_err(&GET_DEV(accel_dev),
				"VQAT(%s) and VDCM are incompatible.\n",
				dev_name(mdev_dev(vqat->mdev)));

		resp_data = (u8 *)&resp;
		resp_len = sizeof(resp);
		break;
	}
	case ADF_VF2PF_MSGTYPE_INIT:
		iov_agent->init = true;
		break;
	case ADF_VF2PF_MSGTYPE_SHUTDOWN:
		iov_agent->init = false;
		break;
	default:
		dev_err(&GET_DEV(accel_dev),
			"Unknown message from iov%d\n",
			transport->id);
		break;
	}

	adf_iov_trans_ack_msg(transport, &msg);
	if (resp_type) {
		if (adf_sw_iov_putmsg(transport, resp_type,
				      resp_data, resp_len)) {
			dev_err(&GET_DEV(accel_dev),
				"Failed to send response to iov%d\n",
				transport->id);
			goto out;
		}
		adf_vdcm_notify_vqat_iov(vqat, 0);
	}

out:
	adf_iov_trans_finish_rx(transport, &msg);
}
#endif /* CONFIG_CRYPTO_DEV_QAT_VDCM */
