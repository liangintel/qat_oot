// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2019 - 2021 Intel Corporation */
#include "adf_accel_devices.h"
#include "adf_vdcm.h"
#include "adf_vdcm_iov.h"

struct adf_iov_vdcm_msg {
	u16 type;
	u8 data[];
};

int adf_iov_trans_put_msg(struct adf_iov_transport *t,
			  struct adf_iov_msg *msg)
{
	struct adf_iov_vdcm_msg *m =
		(struct adf_iov_vdcm_msg *)(t->tx_queue);
	u32 *tx_notifier = (u32 *)(t->tx_notifier);

	if (*tx_notifier & ADF_VQAT_MSGQ_NOTIFIER_MASK)
		return -EAGAIN;
	if (msg->len > t->queue_len - sizeof(m->type))
		return -EINVAL;

	m->type = msg->type;
	if (msg->len)
		memcpy(m->data, msg->data, msg->len);
	*tx_notifier = msg->len + sizeof(m->type);

	return msg->len;
}

int adf_iov_trans_get_msg(struct adf_iov_transport *t,
			  struct adf_iov_msg *msg)
{
	struct adf_iov_vdcm_msg *m =
		(struct adf_iov_vdcm_msg *)(t->rx_queue);
	u32 len;

	len = *(u32 *)(t->rx_notifier) & ADF_VQAT_MSGQ_NOTIFIER_MASK;
	if (len < sizeof(m->type))
		return -EAGAIN;
	else if (len - sizeof(m->type) > msg->len)
		return -EINVAL;

	msg->type = m->type;
	len -= sizeof(m->type);
	if (len)
		memcpy(msg->data, m->data, len);

	return len;
}

void adf_iov_trans_ack_msg(struct adf_iov_transport *t,
			   struct adf_iov_msg *msg)
{
	*(u32 *)(t->rx_notifier) = 0;
}

void adf_iov_trans_finish_rx(struct adf_iov_transport *t,
			     struct adf_iov_msg *msg)
{
}

static inline void adf_iov_trans_cleanup(struct adf_iov_transport *t)
{
	memset(t, 0, sizeof(*t));
}

static int adf_iov_init_vqat_transport(struct adf_iov_transport *t,
				       struct adf_accel_dev *accel_dev, u32 id)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct adf_bar *pmisc =
		&GET_BARS(accel_dev)[hw_data->get_misc_bar_id(hw_data)];
	void __iomem *pmisc_addr = pmisc->virt_addr;
	u32 msgq_cfg, ofs;
	u8 num;

	msgq_cfg = ADF_CSR_RD(pmisc_addr, ADF_VQAT_MSGQ_CFG);
	adf_iov_vdcm_get_msgq_info(msgq_cfg, &num, &ofs,
				   &t->queue_len);
	t->id = id;
	t->tx_queue = pmisc_addr + ofs;
	t->rx_queue = (u8 *)t->tx_queue + t->queue_len;
	t->tx_notifier = pmisc_addr + ADF_VQAT_MSGQ_TX_NOTIFIER;
	t->rx_notifier = pmisc_addr + ADF_VQAT_MSGQ_RX_NOTIFIER;

	return 0;
}

struct adf_iov_transport *
adf_create_vqat_iov_transport(struct adf_accel_dev *accel_dev, u32 id)
{
	struct adf_iov_transport *transport;

	transport = kzalloc(sizeof(*transport), GFP_KERNEL);
	if (!transport)
		return NULL;

	if (adf_iov_init_vqat_transport(transport, accel_dev, id) < 0) {
		kfree(transport);
		return NULL;
	}

	return transport;
}

void adf_destroy_vqat_iov_transport(struct adf_iov_transport *transport)
{
	if (transport) {
		adf_iov_trans_cleanup(transport);
		kfree(transport);
	}
}

#ifdef CONFIG_CRYPTO_DEV_QAT_VDCM
static int adf_iov_init_vdcm_transport(struct adf_iov_transport *t,
				       struct adf_vdcm_vqat *vqat, u32 id)
{
	t->id = id;
	t->queue_len = ADF_VQAT_MSGQ_SIZE;
	t->rx_queue = vqat->iov_msgq.vbase;
	t->tx_queue = (u8 *)t->rx_queue + t->queue_len;
	t->tx_notifier = &vqat->iov_msgq.rx_notifier;
	t->rx_notifier = &vqat->iov_msgq.tx_notifier;

	return 0;
}

void adf_cleanup_vdcm_iov_agent(struct adf_iov_vx_agent *agent)
{
	adf_iov_trans_cleanup(&agent->transport);
}

int adf_init_vdcm_iov_agent(struct adf_iov_vx_agent *agent,
			    struct adf_vdcm_vqat *vqat, u32 id)
{
	agent->compat_ver = 0;
	agent->init = 0;
	ratelimit_state_init(&agent->vf2pf_ratelimit,
			     ADF_IOV_RATELIMIT_INTERVAL,
			     ADF_IOV_RATELIMIT_BURST);

	return adf_iov_init_vdcm_transport(&agent->transport, vqat, id);
}
#endif

