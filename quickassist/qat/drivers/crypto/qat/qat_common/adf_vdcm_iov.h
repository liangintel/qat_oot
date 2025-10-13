/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2019 - 2020 Intel Corporation */
#ifndef ADF_VDCM_IOV_H
#define ADF_VDCM_IOV_H

#include "adf_pf2vf_msg.h"

#define ADF_VDCM_COMPATIBILITY_VERSION	1
#define ADF_VQAT_COMPATIBILITY_VERSION	ADF_VDCM_COMPATIBILITY_VERSION
#define ADF_SW_IOV_MAX_MSGLEN		256

struct adf_iov_msg {
	u16 type;
	u16 len;
	u8 *data;
};

struct adf_sw_iov_compat_version_req {
	u16 version;
};

struct adf_sw_iov_compat_version_resp {
	u16 version;
	u8 compatible;
};

/*
 * The sw based iov transport class for vdev & vdcm
 */
struct adf_iov_transport {
	struct pfvf_stats pfvf_counters;
	u32 id;
	void *tx_queue;
	void *rx_queue;
	void *rx_notifier;
	void *tx_notifier;
	u16 queue_len;
};

/*
 * The iov agent for vdev in vdcm side
 */
struct adf_iov_vx_agent {
	struct adf_iov_transport transport;
	struct ratelimit_state vf2pf_ratelimit;
	int init;
	u8 compat_ver;
};

int adf_iov_trans_put_msg(struct adf_iov_transport *transport,
			  struct adf_iov_msg *msg);
int adf_iov_trans_get_msg(struct adf_iov_transport *transport,
			  struct adf_iov_msg *msg);
void adf_iov_trans_ack_msg(struct adf_iov_transport *transport,
			   struct adf_iov_msg *msg);
void adf_iov_trans_finish_rx(struct adf_iov_transport *transport,
			     struct adf_iov_msg *msg);

struct adf_vdcm_vqat;
int adf_sw_iov_notify(struct adf_iov_transport *transport, u16 msg_type);
int adf_sw_iov_putmsg(struct adf_iov_transport *transport, u16 msg_type,
		      u8 *msg_data, u16 len);
int adf_vqat2vdcm_init(struct adf_accel_dev *accel_dev);
void adf_vqat2vdcm_shutdown(struct adf_accel_dev *accel_dev);
int adf_vqat2vdcm_req_version(struct adf_accel_dev *accel_dev);
void adf_vdcm2vqat_restarting(struct adf_iov_vx_agent *iov_agent,
			      struct adf_vdcm_vqat *vqat);
void adf_vdcm2vqat_fatal_error(struct adf_iov_vx_agent *iov_agent,
			       struct adf_vdcm_vqat *vqat);
void adf_vdcm_iov_handle_vqat_msg(struct adf_iov_vx_agent *iov_agent,
				  struct adf_vdcm_vqat *vqat);
void adf_cleanup_vdcm_iov_agent(struct adf_iov_vx_agent *agent);
int adf_init_vdcm_iov_agent(struct adf_iov_vx_agent *agent,
			    struct adf_vdcm_vqat *vqat, u32 id);
struct adf_iov_transport *
adf_create_vqat_iov_transport(struct adf_accel_dev *accel_dev, u32 id);
void adf_destroy_vqat_iov_transport(struct adf_iov_transport *transport);
int adf_vdcm_compat_version_checker(struct adf_accel_dev *accel_dev,
				    u8 vf_compat_ver);
int adf_vqat_compat_version_checker(struct adf_accel_dev *accel_dev,
				    u8 pf_compat_ver);

#endif /* ADF_VDCM_IOV_H */

