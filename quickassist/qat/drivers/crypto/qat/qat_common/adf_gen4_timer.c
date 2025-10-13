// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2020 - 2022 Intel Corporation */

#include "adf_accel_devices.h"
#include "adf_heartbeat.h"
#include "adf_common_drv.h"
#include "icp_qat_fw_init_admin.h"
#include "adf_gen4_timer.h"

#include "adf_dev_err.h"

#define ADF_GEN4_INT_TIMER_VALUE_IN_MS 200
/* Interval within timer interrupt. Value in miliseconds. */

#define ADF_GEN4_MAX_INT_TIMER_VALUE_IN_MS 0xFFFFFFFF
/* MAX Interval within timer interrupt. Value in miliseconds. */

static u64 adf_get_next_timeout(u32 timeout_val)
{
	u64 timeout = msecs_to_jiffies(timeout_val);

	return rounddown(jiffies + timeout, timeout);
}

static void adf_hb_irq_bh_handler(struct work_struct *work)
{
	struct icp_qat_fw_init_admin_req req = {0};
	struct icp_qat_fw_init_admin_resp resp = {0};
	struct adf_hb_timer_data *hb_timer_data =
		container_of(work, struct adf_hb_timer_data, hb_int_timer_work);
	struct adf_accel_dev *accel_dev = hb_timer_data->accel_dev;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	size_t num_au = hw_data->get_num_accel_units(hw_data);
	size_t max_aes = hw_data->num_engines;
	size_t i = 0;
	u32 ae_mask = hw_data->ae_mask;
	u32 admin_ae_mask = hw_data->admin_ae_mask;
	unsigned long service_ae_mask = 0;

	for (i = 0; i < num_au; i++) {
		if (accel_dev->au_info->au[i].services ==
			ADF_ACCEL_SERVICE_NULL)
			ae_mask &= ~accel_dev->au_info->au[i].ae_mask;
	}

	service_ae_mask = ae_mask ^ admin_ae_mask;

	if (!accel_dev->int_timer)
		goto err_int_timer;

	/* Update heartbeat count via init/admin cmd */
	if (!accel_dev->admin) {
		dev_err(&GET_DEV(accel_dev), "adf_admin is not available\n");
		goto end;
	}

	req.cmd_id = ICP_QAT_FW_HEARTBEAT_SYNC;
	req.heartbeat_ticks = hb_timer_data->msg_cnt;

	/* Issue the cmd to admin AE first, if failed, stop the thread */
	if (unlikely(adf_send_admin(accel_dev, &req, &resp, admin_ae_mask) ||
		     resp.status)) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to update HB cnt on admin ME\n");
		goto end;
	}

	/* Issue the cmd to remaining service AEs, stop at the first */
	/* failed AE */
	for_each_set_bit(i, &service_ae_mask, max_aes) {
		if (unlikely(adf_put_admin_msg_sync(accel_dev,
						    i,
						    &req,
						    &resp) || resp.status)) {
			dev_err(&GET_DEV(accel_dev),
				"Failed to update HB cnt on srv ME%zu\n", i);
			goto end;
		}
	}

end:
	atomic_set(&accel_dev->int_timer->timer_bh_state, TIMER_BH_STOPPED);
err_int_timer:
	kfree(hb_timer_data);
}

static void timer_handler(struct timer_list *tl)
{
	struct adf_int_timer *int_timer = from_timer(int_timer, tl, timer);
	struct adf_accel_dev *accel_dev = int_timer->accel_dev;
	struct adf_hb_timer_data *hb_timer_data = NULL;
	u64 timeout_val = adf_get_next_timeout(int_timer->timeout_val);

	/* Update TL */
	if (accel_dev->hw_device->telemetry_calc_data)
		accel_dev->hw_device->telemetry_calc_data(accel_dev);

	/* Schedule a heartbeat work queue to update HB, if the bottom */
	/* half WQ does not return, do not schedule new one, just record */
	/* the int_cnt for next round of usage */
	if (atomic_read(&int_timer->timer_bh_state) == TIMER_BH_STOPPED) {
		hb_timer_data = kzalloc(sizeof(*hb_timer_data), GFP_ATOMIC);
		if (hb_timer_data) {
			hb_timer_data->accel_dev = accel_dev;
			hb_timer_data->msg_cnt = int_timer->int_cnt;
			/* Flag will get ON when timer BH finishes */
			atomic_set(&int_timer->timer_bh_state,
				   TIMER_BH_SCHEDULED);

			INIT_WORK(&hb_timer_data->hb_int_timer_work,
				  adf_hb_irq_bh_handler);
			queue_work(int_timer->timer_irq_wq,
				   &hb_timer_data->hb_int_timer_work);
		} else {
			dev_err(&GET_DEV(accel_dev),
				"Failed to alloc heartbeat timer data\n");
		}
	}

	int_timer->int_cnt++;
	mod_timer(tl, timeout_val);
}

int adf_gen4_int_timer_init(struct adf_accel_dev *accel_dev)
{
	u64 timeout_val = adf_get_next_timeout(ADF_GEN4_INT_TIMER_VALUE_IN_MS);
	struct adf_int_timer *int_timer = NULL;

	if (!accel_dev)
		return 0;

	int_timer = kzalloc(sizeof(*int_timer), GFP_KERNEL);
	if (!int_timer)
		return -ENOMEM;

	int_timer->timer_irq_wq = alloc_workqueue("%s_%d",
						  WQ_MEM_RECLAIM,
						  1,
						  "qat_timer_wq",
						  accel_dev->accel_id);

	if (!int_timer->timer_irq_wq) {
		kfree(int_timer);
		return -ENOMEM;
	}

	int_timer->accel_dev = accel_dev;
	int_timer->timeout_val = ADF_GEN4_INT_TIMER_VALUE_IN_MS;
	int_timer->int_cnt = 0;
	atomic_set(&int_timer->timer_bh_state, TIMER_BH_STOPPED);

	accel_dev->int_timer = int_timer;

	timer_setup(&int_timer->timer, timer_handler, 0);
	mod_timer(&int_timer->timer, timeout_val);

	return 0;
}
EXPORT_SYMBOL_GPL(adf_gen4_int_timer_init);

void adf_gen4_int_timer_exit(struct adf_accel_dev *accel_dev)
{
	if (accel_dev && accel_dev->int_timer) {
		del_timer_sync(&accel_dev->int_timer->timer);
		atomic_set(&accel_dev->int_timer->timer_bh_state,
			   TIMER_BH_NOT_INITIALIZED);

		if (accel_dev->int_timer->timer_irq_wq) {
			flush_workqueue(accel_dev->int_timer->timer_irq_wq);
			destroy_workqueue(accel_dev->int_timer->timer_irq_wq);
		}

		kfree(accel_dev->int_timer);
		accel_dev->int_timer = NULL;
	}
}
EXPORT_SYMBOL_GPL(adf_gen4_int_timer_exit);

#ifdef QAT_HB_FAIL_SIM
int adf_gen4_set_max_hb_timer(struct adf_accel_dev *accel_dev)
{
	u32 timeout_val = ADF_GEN4_MAX_INT_TIMER_VALUE_IN_MS;

	if (!accel_dev->int_timer) {
		dev_err(&GET_DEV(accel_dev), "Device heartbeat timer is not available\n");
		return -EFAULT;
	}

	accel_dev->int_timer->timeout_val = timeout_val;

	return 0;
}
EXPORT_SYMBOL_GPL(adf_gen4_set_max_hb_timer);
#endif
