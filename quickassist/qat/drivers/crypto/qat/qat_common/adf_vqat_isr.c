// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2019 - 2021 Intel Corporation */

#include <linux/workqueue.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "adf_cfg.h"
#include "adf_cfg_strings.h"
#include "adf_cfg_common.h"
#include "adf_transport_access_macros.h"
#include "adf_transport_internal.h"
#include "adf_vdcm_iov.h"

static struct workqueue_struct *adf_vf_stop_wq;
static DEFINE_MUTEX(vf_stop_wq_lock);

struct adf_vf_stop_data {
	struct adf_accel_dev *accel_dev;
	struct work_struct vf_stop_work;
};

#define ADF_VQAT_PF2VF_WQ "adf_vqat_pf2vf_wq_"
#define ADF_VQAT_VF_RESP_WQ "adf_vqat_vf_resp_wq_"

static int adf_isr_alloc_msix_entry_table(struct adf_accel_dev *accel_dev)
{
	struct adf_irq *irqs;
	struct msix_entry *entries;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	/* The total interrupt num of VQAT should be one MISC irq plus
	 * multiple bundle interrupts.
	 */
	u32 msix_num_entries = 1 + hw_data->num_banks;

	entries = kzalloc_node(msix_num_entries * sizeof(*entries),
			       GFP_KERNEL, dev_to_node(&GET_DEV(accel_dev)));
	if (!entries)
		return -ENOMEM;

	irqs = kzalloc_node(msix_num_entries * sizeof(*irqs),
			    GFP_KERNEL, dev_to_node(&GET_DEV(accel_dev)));
	if (!irqs) {
		kfree(entries);
		return -ENOMEM;
	}
	accel_dev->accel_pci_dev.msix_entries.num_entries = msix_num_entries;
	accel_dev->accel_pci_dev.msix_entries.entries = entries;
	accel_dev->accel_pci_dev.msix_entries.irqs = irqs;
	return 0;
}

static void adf_isr_free_msix_entry_table(struct adf_accel_dev *accel_dev)
{
	kfree(accel_dev->accel_pci_dev.msix_entries.entries);
	kfree(accel_dev->accel_pci_dev.msix_entries.irqs);
}

static int adf_enable_msix(struct adf_accel_dev *accel_dev)
{
	struct adf_accel_pci *pci_dev_info = &accel_dev->accel_pci_dev;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	/* The total interrupt num of VQAT should be one MISC irq plus
	 * multiple bundle interrupts.
	 */
	u32 msix_num_entries = 1 + hw_data->num_banks;
	int i;

	for (i = 0; i < msix_num_entries; i++)
		pci_dev_info->msix_entries.entries[i].entry = i;

	if (pci_enable_msix_exact(pci_dev_info->pci_dev,
				  pci_dev_info->msix_entries.entries,
				  msix_num_entries)) {
		dev_err(&GET_DEV(accel_dev), "Failed to enable MSI-X IRQ(s)\n");
		return -EFAULT;
	}

	return 0;
}

static void adf_disable_msix(struct adf_accel_pci *pci_dev_info)
{
	pci_disable_msix(pci_dev_info->pci_dev);
}

static void adf_dev_stop_async(struct work_struct *work)
{
	struct adf_vf_stop_data *stop_data =
		container_of(work, struct adf_vf_stop_data, vf_stop_work);
	struct adf_accel_dev *accel_dev = stop_data->accel_dev;
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

	adf_dev_stop(accel_dev);
	adf_dev_shutdown(accel_dev);

	/* Re-enable PF2VF interrupts */
	hw_data->enable_pf2vf_interrupt(accel_dev);
	kfree(stop_data);
}

static void adf_vqat_iov_handle_vdcm_msg(struct adf_accel_dev *accel_dev)
{
	struct adf_iov_transport *transport = accel_dev->vf.iov_transport;
	struct adf_iov_msg msg;
	u8 msg_data[ADF_SW_IOV_MAX_MSGLEN];

	msg.len = sizeof(msg_data);
	msg.data = msg_data;

	if (adf_iov_trans_get_msg(transport, &msg) < 0) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to get iov message from VDCM\n");
		goto out;
	}

	switch (msg.type) {
	case ADF_PF2VF_MSGTYPE_RESTARTING: {
		struct adf_vf_stop_data *stop_data;

		dev_dbg(&GET_DEV(accel_dev),
			"Restarting msg received from VDCM\n");

		clear_bit(ADF_STATUS_PF_RUNNING, &accel_dev->status);
		stop_data = kzalloc(sizeof(*stop_data), GFP_ATOMIC);
		if (!stop_data)
			goto out;

		stop_data->accel_dev = accel_dev;
		INIT_WORK(&stop_data->vf_stop_work, adf_dev_stop_async);
		queue_work(adf_vf_stop_wq, &stop_data->vf_stop_work);
		break;
	}
	case ADF_PF2VF_MSGTYPE_VERSION_RESP:
	{
		struct adf_sw_iov_compat_version_resp *resp;

		dev_dbg(&GET_DEV(accel_dev),
			"Version resp received from VDCM\n");
		resp = (struct adf_sw_iov_compat_version_resp *)msg.data;
		accel_dev->vf.pf_version = resp->version;
		accel_dev->vf.compatible = resp->compatible;
		complete(&accel_dev->vf.iov_msg_completion);
		break;
	}
	case ADF_PF2VF_MSGTYPE_FATAL_ERROR:
		dev_info(&GET_DEV(accel_dev),
			 "Fatal error received from VDCM\n");

		accel_dev->vf.is_err_notified = true;
		if (adf_notify_fatal_error(accel_dev))
			dev_err(&GET_DEV(accel_dev),
				"Couldn't notify fatal error\n");
		break;
	default:
		dev_err(&GET_DEV(accel_dev),
			"Unknown PF2VF message\n");
	}

	adf_iov_trans_ack_msg(transport, &msg);
out:
	adf_iov_trans_finish_rx(transport, &msg);
}

static void adf_vqat_pf2vf_bh_handler(void *data)
{
	struct adf_accel_dev *accel_dev = data;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;

	adf_vqat_iov_handle_vdcm_msg(accel_dev);
	hw_data->enable_pf2vf_interrupt(accel_dev);
}

static void adf_vqat_pf2vf_bh_handler_wq(struct work_struct *data)
{
	struct adf_accel_dev *accel_dev =
		container_of(data, struct adf_accel_dev, vf.pf2vf_bh_wq);
	void *arg = (void *)accel_dev;

	adf_vqat_pf2vf_bh_handler(arg);
}

static int adf_setup_pf2vf_bh(struct adf_accel_dev *accel_dev)
{
	struct adf_iov_transport *iov_transport;
	char wq_name[ADF_CFG_MAX_VAL_LEN_IN_BYTES] = {0};

	iov_transport = adf_create_vqat_iov_transport(accel_dev,
						      accel_dev->accel_id);
	if (!iov_transport) {
		dev_err(&GET_DEV(accel_dev), "failed to get iov_transport\n");
		return -EINVAL;
	}

	accel_dev->vf.iov_transport = iov_transport;

	snprintf(wq_name, sizeof(wq_name), "%s%d",
		 ADF_VQAT_PF2VF_WQ, accel_dev->accel_id);

	accel_dev->vf.pf2vf_wq = alloc_workqueue
		(wq_name, WQ_HIGHPRI | WQ_MEM_RECLAIM, 1);
	if (!accel_dev->vf.pf2vf_wq)
		return -ENOMEM;

	INIT_WORK(&accel_dev->vf.pf2vf_bh_wq, adf_vqat_pf2vf_bh_handler_wq);
	return 0;
}

static void adf_cleanup_pf2vf_bh(struct adf_accel_dev *accel_dev)
{
	if (accel_dev->vf.pf2vf_wq) {
		destroy_workqueue(accel_dev->vf.pf2vf_wq);
		accel_dev->vf.pf2vf_wq = NULL;
	}

	adf_destroy_vqat_iov_transport(accel_dev->vf.iov_transport);
	accel_dev->vf.iov_transport = NULL;
}

static irqreturn_t adf_isr_vdcm2vqat(int irq, void *privdata)
{
	struct adf_accel_dev *accel_dev = privdata;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;

	/* Disable PF to VF interrupt */
	hw_data->disable_pf2vf_interrupt(accel_dev);

	/* Schedule WQ to handle interrupt BH */
	queue_work(accel_dev->vf.pf2vf_wq, &accel_dev->vf.pf2vf_bh_wq);

	return IRQ_HANDLED;
}

static irqreturn_t adf_isr_bundle(int irq, void *privdata)
{
	struct adf_accel_dev *accel_dev = privdata;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct adf_hw_csr_ops *csr_ops = &hw_data->csr_info.csr_ops;
	int handled = 0;
	int int_active_bundles = 0;
	int i = 0;

#ifdef QAT_UIO
	/* We only need to handle the interrupt in case this is a kernel bundle.
	 * If it is a user bundle, the UIO resp handler will handle the IRQ
	 */
	if (!accel_dev->num_ker_bundles)
		return IRQ_NONE;
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
			queue_work(accel_dev->vf.pf2vf_wq, &accel_dev->vf.pf2vf_bh_wq);
			handled = 1;
		}
	}

	if (handled)
		return IRQ_HANDLED;
	return IRQ_NONE;
}

static int adf_request_msix_irq(struct adf_accel_dev *accel_dev,
				unsigned int irq_flags, char *name,
				irq_handler_t handler, u32 irq_num)
{
	struct adf_accel_pci *pci_dev_info = &accel_dev->accel_pci_dev;
	struct msix_entry *msixe = pci_dev_info->msix_entries.entries;
	struct adf_irq *irqs = pci_dev_info->msix_entries.irqs;
	unsigned int cpu;
	int ret = 0;

	ret = request_irq(msixe[irq_num].vector,
			  handler, irq_flags,
			  name, (void *)accel_dev);
	if (ret) {
		dev_err(&GET_DEV(accel_dev),
			"failed to enable irq %d for %s\n",
			msixe[irq_num].vector, name);
		return ret;
	}

	cpu = accel_dev->accel_id % num_online_cpus();
	irq_set_affinity_hint(msixe[irq_num].vector, get_cpu_mask(cpu));
	irqs[irq_num].enabled = true;

	return ret;
}

static int adf_request_irqs(struct adf_accel_dev *accel_dev)
{
	struct pci_dev *pdev = accel_to_pci_dev(accel_dev);
	struct adf_accel_pci *pci_dev_info = &accel_dev->accel_pci_dev;
	struct adf_irq *irqs = pci_dev_info->msix_entries.irqs;
	char *name;
	int ret = 0;
	u32 irq_num = 0;
	unsigned int irq_flags = 0;
#ifdef QAT_UIO
	unsigned int i = 0;
	struct adf_etr_data *etr_data = accel_dev->transport;
	unsigned long num_ker_bundles = 0;

	for (i = 0; i < accel_dev->hw_device->num_banks; i++) {
		if (etr_data->banks[i].type == KERNEL)
			num_ker_bundles++;
	}

	accel_dev->num_ker_bundles = num_ker_bundles;

	/* We need to share the interrupt with the UIO device in case this is
	 * a user bundle
	 */
	if (!num_ker_bundles)
		irq_flags = IRQF_SHARED | IRQF_ONESHOT;
#endif
	name = irqs[irq_num].name;
	snprintf(name, ADF_MAX_MSIX_VECTOR_NAME,
		 "qat_%04x:%02x:%02d.%02d_vdcm2vqat_int",
		 pci_domain_nr(pdev->bus),
		 pdev->bus->number, PCI_SLOT(pdev->devfn),
		 PCI_FUNC(pdev->devfn));

	ret = adf_request_msix_irq(accel_dev, 0, name, adf_isr_vdcm2vqat, irq_num);
	if (ret)
		return ret;

	irq_num++;
	name = irqs[irq_num].name;
	snprintf(name, ADF_MAX_MSIX_VECTOR_NAME,
		 "qat_%04x:%02x:%02d.%02d_bundle_int",
		 pci_domain_nr(pdev->bus),
		 pdev->bus->number, PCI_SLOT(pdev->devfn),
		 PCI_FUNC(pdev->devfn));

	ret = adf_request_msix_irq(accel_dev, irq_flags, name, adf_isr_bundle, irq_num);
	if (ret)
		return ret;

	return ret;
}

static void adf_free_irqs(struct adf_accel_dev *accel_dev)
{
	struct adf_accel_pci *pci_dev_info = &accel_dev->accel_pci_dev;
	struct adf_irq *irqs = pci_dev_info->msix_entries.irqs;
	struct msix_entry *msixe = pci_dev_info->msix_entries.entries;
	u32 i = 0;

	for (i = 0; i < pci_dev_info->msix_entries.num_entries; i++) {
		if (irqs[i].enabled) {
			irq_set_affinity_hint(msixe[i].vector,
					      NULL);
			free_irq(msixe[i].vector, (void *)accel_dev);
			irqs[i].enabled = false;
		}
	}
}

static int adf_setup_bh(struct adf_accel_dev *accel_dev)
{
	int i = 0;
	struct adf_etr_data *priv_data = accel_dev->transport;
	char wq_name[ADF_CFG_MAX_VAL_LEN_IN_BYTES] = {0};

	snprintf(wq_name, sizeof(wq_name), "%s%d",
		 ADF_VQAT_VF_RESP_WQ, accel_dev->accel_id);

	accel_dev->vf.resp_wq = alloc_workqueue
		(ADF_VQAT_VF_RESP_WQ, WQ_HIGHPRI | WQ_MEM_RECLAIM, 1);
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
 * adf_vqat_isr_resource_free() - Free IRQ for acceleration device
 * @accel_dev:  Pointer to acceleration device.
 *
 * Function frees interrupts for acceleration device virtual function.
 */
void adf_vqat_isr_resource_free(struct adf_accel_dev *accel_dev)
{
	adf_free_irqs(accel_dev);
	adf_cleanup_bh(accel_dev);
	adf_cleanup_pf2vf_bh(accel_dev);
	adf_disable_msix(&accel_dev->accel_pci_dev);
	adf_isr_free_msix_entry_table(accel_dev);
}
EXPORT_SYMBOL_GPL(adf_vqat_isr_resource_free);

/**
 * adf_vqat_isr_resource_alloc() - Allocate IRQ for acceleration device
 * @accel_dev:  Pointer to acceleration device.
 *
 * Function allocates interrupts for acceleration device virtual function.
 *
 * Return: 0 on success, error code otherwise.
 */
int adf_vqat_isr_resource_alloc(struct adf_accel_dev *accel_dev)
{
	int ret;

	ret = adf_isr_alloc_msix_entry_table(accel_dev);
	if (ret)
		return ret;

	if (adf_enable_msix(accel_dev))
		goto err_out;

	if (adf_setup_pf2vf_bh(accel_dev))
		goto err_out;

	if (adf_setup_bh(accel_dev))
		goto err_out;

	if (adf_request_irqs(accel_dev))
		goto err_out;

	return 0;
err_out:
	adf_vqat_isr_resource_free(accel_dev);
	return -EFAULT;
}
EXPORT_SYMBOL_GPL(adf_vqat_isr_resource_alloc);

/**
 * adf_flush_vqat_wq() - Flush workqueue for VF
 *
 * Function flushes workqueue 'adf_vf_stop_wq' for VF.
 *
 * Return: void.
 */
void adf_flush_vqat_wq(void)
{
	if (adf_vf_stop_wq)
		flush_workqueue(adf_vf_stop_wq);
}
EXPORT_SYMBOL_GPL(adf_flush_vqat_wq);

/**
 * adf_init_vqat_wq() - Init workqueue for VF
 *
 * Function init workqueue 'adf_vf_stop_wq' for VF.
 *
 * Return: 0 on success, error code otherwise.
 */
static int adf_init_vqat_wq(void)
{
	int ret = 0;

	mutex_lock(&vf_stop_wq_lock);
	if (!adf_vf_stop_wq)
		adf_vf_stop_wq = alloc_workqueue("adf_vqat_stop_wq",
						 WQ_MEM_RECLAIM, 1);

	if (!adf_vf_stop_wq)
		ret = -ENOMEM;

	mutex_unlock(&vf_stop_wq_lock);
	return ret;
}

/**
 * adf_exit_vqat_wq() - Destroy workqueue for VF
 *
 * Function destroy workqueue 'adf_vf_stop_wq' for VF.
 *
 * Return: void.
 */
void adf_exit_vqat_wq(void)
{
	if (adf_vf_stop_wq) {
		destroy_workqueue(adf_vf_stop_wq);
		adf_vf_stop_wq = NULL;
	}
}

/**
 * adf_enable_vqat_iov() - Function enables communication from vf to pf
 *
 * @accel_dev: Pointer to acceleration device virtual function.
 *
 * Return: 0 on success, error code otherwise.
 */
int adf_enable_vqat_iov(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	int ret = 0;

	/* init workqueue for VF */
	ret = adf_init_vqat_wq();
	if (ret)
		return ret;

	hw_data->enable_pf2vf_interrupt(accel_dev);

	ret = adf_iov_init_compat_manager(accel_dev, &accel_dev->cm);
	if (ret)
		return ret;

	ret = adf_iov_register_compat_checker(accel_dev,
					      accel_dev->cm,
					      adf_vqat_compat_version_checker);
	if (ret)
		goto err;

	ret = adf_vqat2vdcm_req_version(accel_dev);
	if (ret)
		goto err;

	return 0;
err:
	adf_iov_unregister_compat_checker(accel_dev,
					  accel_dev->cm,
					  adf_vqat_compat_version_checker);
	adf_iov_shutdown_compat_manager(accel_dev, &accel_dev->cm);

	return ret;
}
EXPORT_SYMBOL_GPL(adf_enable_vqat_iov);

/**
 * adf_disable_vqat_iov() - Function disables communication from vf to pf
 *
 * @accel_dev: Pointer to acceleration device virtual function.
 *
 * Return: 0 on success, error code otherwise.
 */
int adf_disable_vqat_iov(struct adf_accel_dev *accel_dev)
{
	if (!accel_dev->cm)
		return 0;

	adf_iov_unregister_compat_checker(accel_dev,
					  accel_dev->cm,
					  adf_vqat_compat_version_checker);
	return adf_iov_shutdown_compat_manager(accel_dev,
					       &accel_dev->cm);
}
EXPORT_SYMBOL_GPL(adf_disable_vqat_iov);

