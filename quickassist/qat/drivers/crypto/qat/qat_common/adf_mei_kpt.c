// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2017 - 2018 Intel Corporation */
#include "adf_mei_kpt.h"

#ifdef QAT_KPT_CAP_DISCOVERY
struct mei_cl_device_id adf_mei_kpt_tbl[] = {
	{ .uuid = ADF_MEI_KPT_UUID, .version = MEI_CL_VERSION_ANY },
	/* required last entry */
	{}
};

static int adf_mei_kpt_msg_send(struct adf_mei_kpt *kpt)
{
	struct adf_mei_kpt_request req;
	const size_t req_len = sizeof(req);
	int ret;

	memset(&req, 0, req_len);
	req.cmd = MEI_KPT_REINIT_KPT;
	ret = mei_cldev_send(kpt->cldev, (u8 *)&req, req_len);
	if (ret < 0)
		return ret;

	return 0;
}

static int adf_mei_kpt_discovery(struct adf_mei_kpt *kpt)
{
	int ret;

	init_completion(&kpt->response);
	ret = adf_mei_kpt_msg_send(kpt);
	if (ret)
		return ret;
	ret = wait_for_completion_timeout(&kpt->response,
					  MEI_KPT_DISCOVERY_TIMEOUT);

	return ret;
}

static void adf_mei_kpt_event_rx(struct mei_cl_device *cldev)
{
	struct adf_mei_kpt *kpt = mei_cldev_get_drvdata(cldev);
	struct adf_mei_kpt_response res;
	const size_t res_len = sizeof(res);
	int ret;

	ret = mei_cldev_recv(kpt->cldev, (u8 *)&res, res_len);
	if (ret < 0) {
		dev_err(&cldev->dev, "Failure in recv %d\n", ret);
		return;
	}
	/* Empty response can be sent on stop */
	if (ret == 0)
		return;

	if (res.cmd != MEI_KPT_REINIT_KPT_RSP) {
		dev_warn(&cldev->dev, "Unsupported command %d\n",
			 res.cmd);
		return;
	}

	if (res.status) {
		dev_warn(&cldev->dev, "Failed to do kpt discovery %d\n",
			 res.status);
		return;
	}

	if (!completion_done(&kpt->response))
		complete(&kpt->response);
}

static void adf_mei_kpt_event(struct mei_cl_device *cldev, u32 events,
			      void *context)
{
	if (events & BIT(MEI_CL_EVENT_RX))
		adf_mei_kpt_event_rx(cldev);
}

static int adf_mei_kpt_probe(struct mei_cl_device *cldev,
			     const struct mei_cl_device_id *id)
{
	struct adf_mei_kpt *kpt;
	int ret;

	kpt = kzalloc(sizeof(*kpt), GFP_KERNEL);
	if (!kpt)
		return -ENOMEM;

	kpt->cldev = cldev;
	mei_cldev_set_drvdata(cldev, kpt);
	ret = mei_cldev_enable(cldev);
	if (ret < 0) {
		dev_err(&cldev->dev, "Could not enable cl device\n");
		goto err_out;
	}
	mei_cldev_register_event_cb(kpt->cldev,
				    BIT(MEI_CL_EVENT_RX) |
				    BIT(MEI_CL_EVENT_NOTIF),
				    adf_mei_kpt_event, 0);

	/* on legacy devices notification is not supported
	 * this doesn't fail the registration for RX event
	 */
	if (ret && ret != -EOPNOTSUPP) {
		dev_err(&cldev->dev, "Could not register event ret=%d\n", ret);
		goto err_disable;
	}
	ret = adf_mei_kpt_discovery(kpt);
	if (!ret)
		goto err_disable;

	return 0;

err_disable:
	mei_cldev_disable(cldev);
err_out:
	kfree(kpt);
	return ret;
}

static int adf_mei_kpt_remove(struct mei_cl_device *cldev)
{
	struct adf_mei_kpt *kpt = mei_cldev_get_drvdata(cldev);

	mei_cldev_disable(cldev);
	kfree(kpt);
	return 0;
}

struct mei_cl_driver mei_kpt_driver = {
	.id_table = adf_mei_kpt_tbl,
	.name = KBUILD_MODNAME,
	.probe = adf_mei_kpt_probe,
	.remove = adf_mei_kpt_remove,
};

int adf_mei_send_discovery_kpt(void)
{
	int ret;

	ret = mei_cldev_driver_register(&mei_kpt_driver);
	if (ret) {
		pr_err(KBUILD_MODNAME "Failed to send discovery kpt message\n");
		return ret;
	}
	mei_cldev_driver_unregister(&mei_kpt_driver);
	return 0;
}
#else
int adf_mei_send_discovery_kpt(void)
{
	return 0;
}
#endif
EXPORT_SYMBOL_GPL(adf_mei_send_discovery_kpt);
