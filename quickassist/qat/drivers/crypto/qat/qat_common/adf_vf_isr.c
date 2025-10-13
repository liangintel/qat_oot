// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2021 Intel Corporation */
#include <linux/workqueue.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "adf_cfg.h"
#include "adf_cfg_strings.h"
#include "adf_cfg_common.h"
#include "adf_transport_access_macros.h"
#include "adf_transport_internal.h"
#include "adf_pf2vf_msg.h"

#define ADF_VINTSOU_BUN		BIT(0)
#define ADF_VINTSOU_PF2VF	BIT(1)

static struct workqueue_struct *adf_vf_restart_wq;
static DEFINE_MUTEX(vf_restart_wq_lock);
static struct workqueue_struct *adf_vf_sla_update_wq;
static DEFINE_MUTEX(vf_sla_update_wq_lock);

struct adf_vf_restart_data {
	struct adf_accel_dev *accel_dev;
	struct work_struct vf_restart_work;
	u32 msg_type;
};

struct adf_vf_sla_update_data {
	struct adf_accel_dev *accel_dev;
	struct work_struct vf_sla_update_work;
	u32 msg_type;
	u32 ring;
};

#define ADF_PF2VF_WQ "adf_pf2vf_wq_"
#define ADF_VF_RESP_WQ "adf_vf_resp_wq_"

static void adf_sla_update(struct work_struct *work)
{
	struct adf_vf_sla_update_data *sla_update_data =
		container_of(work, struct adf_vf_sla_update_data,
			     vf_sla_update_work);
	/*
	 * SLA update is not supported.
	 * Free sla_update_data
	 */
	kfree(sla_update_data);
}

static int adf_enable_msi(struct adf_accel_dev *accel_dev)
{
	struct adf_accel_pci *pci_dev_info = &accel_dev->accel_pci_dev;
	int stat = pci_enable_msi(pci_dev_info->pci_dev);

	if (stat) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to enable MSI interrupts\n");
		return stat;
	}

	accel_dev->vf.irq_name = kzalloc(ADF_MAX_MSIX_VECTOR_NAME, GFP_KERNEL);
	if (!accel_dev->vf.irq_name)
		return -ENOMEM;

	return stat;
}

static void adf_disable_msi(struct adf_accel_dev *accel_dev)
{
	struct pci_dev *pdev = accel_to_pci_dev(accel_dev);

	kfree(accel_dev->vf.irq_name);
	pci_disable_msi(pdev);
}

static void adf_dev_stop_async(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	unsigned long timeout =
		msecs_to_jiffies(ADF_ERR_NOTIFY_TIMEOUT);

	/*
	 * avoid repeating reset when encountering the duplicated
	 * restarting message from PF driver.
	 */
	if (test_bit(ADF_STATUS_RESTARTING, &accel_dev->status))
		return;

	set_bit(ADF_STATUS_RESTARTING, &accel_dev->status);

	if (accel_dev->vf.is_err_notified) {
		if (!wait_for_completion_timeout(&accel_dev->vf.err_notified,
						 timeout)) {
			clear_bit(ADF_STATUS_RESTARTING, &accel_dev->status);
			accel_dev->vf.is_err_notified = false;
			dev_err(&GET_DEV(accel_dev),
				"Failed to wait for the error notified complete\n");
			return;
		}
	}
	accel_dev->vf.is_err_notified = false;

	if (adf_dev_restarting_notify_sync(accel_dev)) {
		clear_bit(ADF_STATUS_RESTARTING, &accel_dev->status);
		return;
	}
	adf_lkca_unregister(accel_dev);
	adf_dev_stop(accel_dev);
	adf_dev_shutdown(accel_dev);

	/* Re-enable PF2VF interrupts */
	hw_data->enable_pf2vf_interrupt(accel_dev);
	adf_vf2pf_restarting_complete(accel_dev);
}

static void adf_dev_start_async(struct adf_accel_dev *accel_dev)
{
	int ret;

	if (adf_dev_started(accel_dev)) {
		dev_err(&GET_DEV(accel_dev),
			"Restarted message should not be sent to VF\n");
		return;
	}

	ret = adf_dev_init(accel_dev);
	if (ret)
		goto out_err_dev_shutdown;

	ret = adf_dev_start(accel_dev);
	if (ret)
		goto out_err_dev_stop;

	ret = adf_lkca_register(accel_dev);
	if (ret)
		goto out_err_dev_stop;
	adf_dev_restarted_notify(accel_dev);
	clear_bit(ADF_STATUS_RESTARTING, &accel_dev->status);
	return;

out_err_dev_stop:
	clear_bit(ADF_STATUS_RESTARTING, &accel_dev->status);
	adf_dev_stop(accel_dev);
out_err_dev_shutdown:
	adf_dev_shutdown(accel_dev);
}

static void adf_dev_restart_async(struct work_struct *work)
{
	struct adf_vf_restart_data *restart_data =
		container_of(work, struct adf_vf_restart_data, vf_restart_work);
	struct adf_accel_dev *accel_dev = restart_data->accel_dev;
	u32 msg_type = restart_data->msg_type;

	switch (msg_type) {
	case ADF_PF2VF_MSGTYPE_RESTARTING:
		adf_dev_stop_async(accel_dev);
		break;
	case ADF_PF2VF_MSGTYPE_RESTARTED:
		adf_dev_start_async(accel_dev);
		break;
	default:
		dev_err(&GET_DEV(accel_dev),
			"Unknown restart message(%d)\n", msg_type);
	}
	kfree(restart_data);
}

void adf_pf2vf_bh_handler(void *data)
{
	struct adf_accel_dev *accel_dev = data;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct adf_bar *pmisc =
			&GET_BARS(accel_dev)[hw_data->get_misc_bar_id(hw_data)];
	void __iomem *pmisc_bar_addr = pmisc->virt_addr;
	u32 msg, pf_msg;
	u16 msg_type;
	u32 msg_data;
	bool is_notification = false;
	int local_csr_shift = 0, remote_csr_shift = 0;
	u32 local_csr_offset = hw_data->get_vf2pf_offset(0);
	u32 remote_csr_offset = hw_data->get_pf2vf_offset(0);
	int type_shift = hw_data->pfvf_type_shift;
	u32 type_mask = hw_data->pfvf_type_mask;
	int data_shift = hw_data->pfvf_data_shift;
	u32 data_mask = hw_data->pfvf_data_mask;
	u32 ring;

	if (local_csr_offset == remote_csr_offset)
		local_csr_shift = ADF_PFVF_VF_MSG_SHIFT;

	/* Read the message from PF */
	msg = ADF_CSR_RD(pmisc_bar_addr, remote_csr_offset);
	pf_msg = msg >> remote_csr_shift;
	if (!(pf_msg & ADF_PFVF_INT)) {
		dev_err(&GET_DEV(accel_dev),
			"Spurious PF2VF interrupt. msg %X. Ignored\n", msg);
		accel_dev->vf.pfvf_counters.spurious++;
		goto out;
	}
	accel_dev->vf.pfvf_counters.rx++;
	msg_type = pf_msg >> type_shift & type_mask;
	msg_data = pf_msg >> data_shift & data_mask;

	if (!(pf_msg & ADF_PFVF_MSGORIGIN_SYSTEM)) {
		dev_err(&GET_DEV(accel_dev),
			"Ignore non-system PF2VF message(0x%x)\n", msg);
		/*
		 * To ack, clear the INT bit.
		 * Because this must be a legacy message, the far side
		 * must clear the in-use pattern.
		 */
		msg &= ~(ADF_PFVF_INT << remote_csr_shift);
		ADF_CSR_WR(pmisc_bar_addr, remote_csr_offset, msg);
		goto out;
	}

	switch (msg_type) {
	case ADF_PF2VF_MSGTYPE_RESTARTING: {
		struct adf_vf_restart_data *restart_data;

		is_notification = true;
		dev_dbg(&GET_DEV(accel_dev),
			"Restarting msg received from PF 0x%x on vf_%d\n", msg,
			accel_dev->accel_id);

		if (!adf_dev_started(accel_dev))
			goto out;

		clear_bit(ADF_STATUS_PF_RUNNING, &accel_dev->status);

		restart_data = kzalloc(sizeof(*restart_data), GFP_ATOMIC);
		if (!restart_data)
			goto out;

		restart_data->accel_dev = accel_dev;
		restart_data->msg_type = ADF_PF2VF_MSGTYPE_RESTARTING;
		INIT_WORK(&restart_data->vf_restart_work,
			  adf_dev_restart_async);
		queue_work(adf_vf_restart_wq,
			   &restart_data->vf_restart_work);
		break;
	}
	case ADF_PF2VF_MSGTYPE_RESTARTED: {
		struct adf_vf_restart_data *restart_data;

		is_notification = true;

		dev_dbg(&GET_DEV(accel_dev),
			"Restarted msg received from PF 0x%x\n", msg);

		if (!adf_devmgr_in_reset(accel_dev))
			goto out;

		restart_data = kzalloc(sizeof(*restart_data), GFP_ATOMIC);
		if (!restart_data)
			goto out;

		restart_data->accel_dev = accel_dev;
		restart_data->msg_type = ADF_PF2VF_MSGTYPE_RESTARTED;
		INIT_WORK(&restart_data->vf_restart_work,
			  adf_dev_restart_async);
		queue_work(adf_vf_restart_wq,
			   &restart_data->vf_restart_work);
		break;
	}
	case ADF_PF2VF_MSGTYPE_VERSION_RESP:
		dev_dbg(&GET_DEV(accel_dev),
			"Version resp received from PF 0x%x\n", msg);
		is_notification = false;
		accel_dev->vf.pf_version =
			(msg_data >> ADF_PF2VF_VERSION_RESP_VERS_SHIFT &
			 ADF_PF2VF_VERSION_RESP_VERS_MASK);
		accel_dev->vf.compatible =
			(msg_data >> ADF_PF2VF_VERSION_RESP_RESULT_SHIFT &
			 ADF_PF2VF_VERSION_RESP_RESULT_MASK);
		complete(&accel_dev->vf.iov_msg_completion);
		break;
	case ADF_PF2VF_MSGTYPE_BLOCK_RESP:
		is_notification = false;
		accel_dev->vf.pf2vf_block_byte =
			(msg_data >> ADF_PF2VF_BLOCK_RESP_DATA_SHIFT &
			 ADF_PF2VF_BLOCK_RESP_DATA_MASK);
		accel_dev->vf.pf2vf_block_resp_type =
			(msg_data >> ADF_PF2VF_BLOCK_RESP_TYPE_SHIFT &
			 ADF_PF2VF_BLOCK_RESP_TYPE_MASK);
		complete(&accel_dev->vf.iov_msg_completion);
		break;
	case ADF_PF2VF_MSGTYPE_CIR_RESP:
	case ADF_PF2VF_MSGTYPE_PIR_RESP: {
		struct adf_vf_sla_update_data *sla_update_data;
		ring = msg_data & ADF_PFVF_RL_MSG_MASK;
		msg_data = msg_data >> ADF_PFVF_RL_MSGDATA_SHIFT;

		if (ring > GET_MAX_BANKS(accel_dev))
			break;

		switch (msg_type) {
		case ADF_PF2VF_MSGTYPE_CIR_RESP:
			accel_dev->vf.vf_rl[ring].cir = msg_data;
			break;
		case ADF_PF2VF_MSGTYPE_PIR_RESP:
			accel_dev->vf.vf_rl[ring].pir = msg_data;
		}

		sla_update_data = kzalloc(sizeof(*sla_update_data),
					  GFP_ATOMIC);
		if (!sla_update_data)
			goto out;

		sla_update_data->accel_dev = accel_dev;
		sla_update_data->ring = ring;
		INIT_WORK(&sla_update_data->vf_sla_update_work,
			  adf_sla_update);
		queue_work(adf_vf_sla_update_wq,
			   &sla_update_data->vf_sla_update_work);

		complete(&accel_dev->vf.iov_msg_completion);
		break;
	}
	case ADF_PF2VF_MSGTYPE_FATAL_ERROR:
		dev_err(&GET_DEV(accel_dev),
			"Fatal error received from PF 0x%x\n", pf_msg);
		is_notification = true;
		accel_dev->vf.is_err_notified = true;

		if (adf_notify_fatal_error(accel_dev))
			dev_err(&GET_DEV(accel_dev),
				"Couldn't notify fatal error\n");
		break;
	case ADF_PF2VF_MSGTYPE_RP_RESET_RESP:
		is_notification = false;
		accel_dev->vf.rpreset_sts = msg_data;
		if (accel_dev->vf.rpreset_sts == RPRESET_SUCCESS)
			dev_dbg(&GET_DEV(accel_dev),
				"rpreset resp(success) resp from PF 0x%x\n",
				msg);
		else if (accel_dev->vf.rpreset_sts == RPRESET_NOT_SUPPORTED)
			dev_dbg(&GET_DEV(accel_dev),
				"rpreset resp(not supported) from PF 0x%x\n",
				msg);
		else if (accel_dev->vf.rpreset_sts == RPRESET_INVAL_BANK)
			dev_dbg(&GET_DEV(accel_dev),
				"rpreset resp(invalid bank) from PF 0x%x\n",
				msg);
		else
			dev_dbg(&GET_DEV(accel_dev),
				"rpreset resp(timeout) from PF 0x%x\n", msg);
		complete(&accel_dev->vf.iov_msg_completion);
		break;
	default:
		dev_err(&GET_DEV(accel_dev),
			"Unknown PF2VF message(0x%x)\n", pf_msg);
	}

	/* To ack, clear the PF2VFINT bit */
	msg &= ~(ADF_PFVF_INT << remote_csr_shift);

	/*
	 * Clear the in-use pattern if using a single CSR
	 * for both directions.
	 */
	if (local_csr_offset == remote_csr_offset) {
		/*
		 * Clear the in-use pattern if the sender won't do it.
		 * Because the compatibility version must be the first message
		 * exchanged between the VF and PF, the pf.version must be
		 * set at this time.
		 * The in-use pattern is not cleared for notifications so that
		 * it can be used for collision detection.
		 */
		if (accel_dev->vf.pf_version >=
				ADF_PFVF_COMPATIBILITY_FAST_ACK &&
				!is_notification)
			msg &= ~(ADF_PFVF_IN_USE_MASK << local_csr_shift);
	}
	ADF_CSR_WR(pmisc_bar_addr, remote_csr_offset, msg);

out:
	/* Re-enable PF2VF interrupts */
	hw_data->enable_pf2vf_interrupt(accel_dev);
}

static void adf_pf2vf_bh_handler_wq(struct work_struct *data)
{
	struct adf_accel_dev *accel_dev =
		container_of(data, struct adf_accel_dev, vf.pf2vf_bh_wq);
	void *arg = (void *)accel_dev;

	adf_pf2vf_bh_handler(arg);
}

static int adf_setup_pf2vf_bh(struct adf_accel_dev *accel_dev)
{
	char wq_name[ADF_CFG_MAX_VAL_LEN_IN_BYTES] = {0};

	snprintf(wq_name, sizeof(wq_name), "%s%d",
		 ADF_PF2VF_WQ, accel_dev->accel_id);

	accel_dev->vf.pf2vf_wq = alloc_workqueue
		(wq_name, WQ_HIGHPRI | WQ_MEM_RECLAIM, 1);
	if (!accel_dev->vf.pf2vf_wq)
		return -ENOMEM;

	INIT_WORK(&accel_dev->vf.pf2vf_bh_wq, adf_pf2vf_bh_handler_wq);
	mutex_init(&accel_dev->vf.vf2pf_lock);
	return 0;
}

static void adf_cleanup_pf2vf_bh(struct adf_accel_dev *accel_dev)
{
	if (accel_dev->vf.pf2vf_wq) {
		destroy_workqueue(accel_dev->vf.pf2vf_wq);
		accel_dev->vf.pf2vf_wq = NULL;
	}

	mutex_destroy(&accel_dev->vf.vf2pf_lock);
}

static irqreturn_t adf_isr(int irq, void *privdata)
{
	struct adf_accel_dev *accel_dev = privdata;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct adf_hw_csr_ops *csr_ops = &hw_data->csr_info.csr_ops;
	int handled = 0;
	int int_active_bundles = 0;
	int i = 0;

	/* Check for PF2VF interrupt */
	if (hw_data->interrupt_active_pf2vf(accel_dev)) {
		/* Disable PF to VF interrupt */
		hw_data->disable_pf2vf_interrupt(accel_dev);

		/* Schedule work to handle interrupt BH */
		queue_work(accel_dev->vf.pf2vf_wq, &accel_dev->vf.pf2vf_bh_wq);
		handled = 1;
	}

#ifdef QAT_UIO
	/* We only need to handle the interrupt in case this is a kernel bundle.
	 * If it is a user bundle, the UIO resp handler will handle the IRQ
	 */
	if (!accel_dev->num_ker_bundles)
		return handled ? IRQ_HANDLED : IRQ_NONE;
#endif

	if (hw_data->get_int_active_bundles)
		int_active_bundles =
			hw_data->get_int_active_bundles(accel_dev);

	for (i = 0; i < GET_MAX_BANKS(accel_dev); i++) {
		if (int_active_bundles & BIT(i)) {
			struct adf_etr_data *etr_data = accel_dev->transport;
			struct adf_etr_bank_data *bank = &etr_data->banks[i];

			/* Disable Flag and Coalesce Ring Interrupts */
			csr_ops->write_csr_int_flag_and_col(bank->csr_addr,
							    bank->bank_number,
							    0);
			queue_work(accel_dev->vf.resp_wq, &bank->resp_handler_wq);
			handled = 1;
		}
	}

	if (handled)
		return IRQ_HANDLED;
	return IRQ_NONE;
}

static int adf_request_msi_irq(struct adf_accel_dev *accel_dev)
{
	struct pci_dev *pdev = accel_to_pci_dev(accel_dev);
	unsigned int cpu;
	int ret;
#ifdef QAT_UIO
	unsigned int irq_flags = 0, i = 0;
	struct adf_etr_data *etr_data = accel_dev->transport;
	unsigned long num_ker_bundles = 0;

	for (i = 0; i < accel_dev->hw_device->num_banks; i++) {
		if (etr_data->banks[i].type == KERNEL)
			num_ker_bundles++;
	}

	accel_dev->num_ker_bundles = num_ker_bundles;
#endif
	snprintf(accel_dev->vf.irq_name, ADF_MAX_MSIX_VECTOR_NAME,
		 "qat_%02x:%02d.%02d", pdev->bus->number, PCI_SLOT(pdev->devfn),
		 PCI_FUNC(pdev->devfn));
#ifdef QAT_UIO
	/* We need to share the interrupt with the UIO device in case this is
	 * a user bundle
	 */
	if (!num_ker_bundles)
		irq_flags = IRQF_SHARED | IRQF_ONESHOT;
	ret = request_irq(pdev->irq, adf_isr, irq_flags, accel_dev->vf.irq_name,
			  (void *)accel_dev);
#else
	ret = request_irq(pdev->irq, adf_isr, 0, accel_dev->vf.irq_name,
			  (void *)accel_dev);
#endif
	if (ret) {
		dev_err(&GET_DEV(accel_dev), "failed to enable irq for %s\n",
			accel_dev->vf.irq_name);
		return ret;
	}
	cpu = accel_dev->accel_id % num_online_cpus();
	irq_set_affinity_hint(pdev->irq, get_cpu_mask(cpu));
	accel_dev->vf.irq_enabled = true;

	return ret;
}

static int adf_setup_bh(struct adf_accel_dev *accel_dev)
{
	int i = 0;
	struct adf_etr_data *priv_data = accel_dev->transport;

	char wq_name[ADF_CFG_MAX_VAL_LEN_IN_BYTES] = {0};

	snprintf(wq_name, sizeof(wq_name), "%s%d",
		 ADF_VF_RESP_WQ, accel_dev->accel_id);

	accel_dev->vf.resp_wq = alloc_workqueue
		(ADF_VF_RESP_WQ, WQ_HIGHPRI | WQ_MEM_RECLAIM, 1);
	if (!accel_dev->vf.resp_wq)
		return -ENOMEM;

	for (i = 0; i < GET_MAX_BANKS(accel_dev); i++) {
		INIT_WORK
			(&priv_data->banks[i].resp_handler_wq,
			adf_response_handler_wq);
	}

	return 0;
}

static void adf_cleanup_bh(struct adf_accel_dev *accel_dev)
{
	if (accel_dev->vf.resp_wq) {
		destroy_workqueue(accel_dev->vf.resp_wq);
		accel_dev->vf.resp_wq = NULL;
	}
}

/**
 * adf_vf_isr_resource_free() - Free IRQ for acceleration device
 * @accel_dev:  Pointer to acceleration device.
 *
 * Function frees interrupts for acceleration device virtual function.
 */
void adf_vf_isr_resource_free(struct adf_accel_dev *accel_dev)
{
	struct pci_dev *pdev = accel_to_pci_dev(accel_dev);

	if (accel_dev->vf.irq_enabled) {
		irq_set_affinity_hint(pdev->irq, NULL);
		free_irq(pdev->irq, (void *)accel_dev);
	}

	if (!accel_dev->hw_device->qat_aux_enable) {
		adf_cleanup_bh(accel_dev);
		adf_cleanup_pf2vf_bh(accel_dev);
	}

	adf_disable_msi(accel_dev);
}
EXPORT_SYMBOL_GPL(adf_vf_isr_resource_free);

/**
 * adf_vf_isr_resource_alloc() - Allocate IRQ for acceleration device
 * @accel_dev:  Pointer to acceleration device.
 *
 * Function allocates interrupts for acceleration device virtual function.
 *
 * Return: 0 on success, error code otherwise.
 */
int adf_vf_isr_resource_alloc(struct adf_accel_dev *accel_dev)
{
	if (adf_enable_msi(accel_dev))
		goto err_out;

	if (accel_dev->hw_device->qat_aux_enable)
		return 0;

	if (adf_setup_pf2vf_bh(accel_dev))
		goto err_out;

	if (adf_setup_bh(accel_dev))
		goto err_out;

	if (adf_request_msi_irq(accel_dev))
		goto err_out;

	return 0;
err_out:
	adf_vf_isr_resource_free(accel_dev);
	return -EFAULT;
}
EXPORT_SYMBOL_GPL(adf_vf_isr_resource_alloc);

/**
 * adf_flush_vf_wq() - Flush workqueue for VF
 *
 * Function flushes workqueue 'adf_vf_restart_wq' for VF.
 *
 * Return: void.
 */
void adf_flush_vf_wq(void)
{
	if (adf_vf_restart_wq)
		flush_workqueue(adf_vf_restart_wq);

	if (adf_vf_sla_update_wq)
		flush_workqueue(adf_vf_sla_update_wq);
}
EXPORT_SYMBOL_GPL(adf_flush_vf_wq);

/**
 * adf_init_vf_wq() - Init workqueue for VF
 *
 * Function init workqueue 'adf_vf_restart_wq' for VF.
 *
 * Return: 0 on success, error code otherwise.
 */
int adf_init_vf_wq(void)
{
	int ret = 0;

	mutex_lock(&vf_restart_wq_lock);
	if (!adf_vf_restart_wq)
		adf_vf_restart_wq = alloc_workqueue("adf_vf_restart_wq",
						    WQ_MEM_RECLAIM, 1);

	if (!adf_vf_restart_wq)
		ret = -ENOMEM;

	mutex_unlock(&vf_restart_wq_lock);

	mutex_lock(&vf_sla_update_wq_lock);
	if (!adf_vf_sla_update_wq)
		adf_vf_sla_update_wq = alloc_workqueue("adf_vf_sla_update_wq",
						       WQ_MEM_RECLAIM, 1);

	if (!adf_vf_sla_update_wq)
		ret = -ENOMEM;

	mutex_unlock(&vf_sla_update_wq_lock);

	return ret;
}

/**
 * adf_exit_vf_wq() - Destroy workqueue for VF
 *
 * Function destroy workqueue 'adf_vf_restart_wq' for VF.
 *
 * Return: void.
 */
void adf_exit_vf_wq(void)
{
	mutex_lock(&vf_restart_wq_lock);
	if (adf_vf_restart_wq) {
		destroy_workqueue(adf_vf_restart_wq);
		adf_vf_restart_wq = NULL;
	}
	mutex_unlock(&vf_restart_wq_lock);

	mutex_lock(&vf_sla_update_wq_lock);
	if (adf_vf_sla_update_wq) {
		destroy_workqueue(adf_vf_sla_update_wq);
		adf_vf_sla_update_wq = NULL;
	}
	mutex_unlock(&vf_sla_update_wq_lock);
}
