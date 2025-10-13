// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2018 - 2020 Intel Corporation */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <adf_accel_devices.h>
#include <adf_common_drv.h>
#include <adf_cfg.h>
#include "adf_4xxxvf_hw_data.h"

#define ADF_SYSTEM_DEVICE(device_id) \
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, device_id)}

static const struct pci_device_id adf_pci_tbl[] = {
	ADF_SYSTEM_DEVICE(ADF_4XXXIOV_PCI_DEVICE_ID),
	ADF_SYSTEM_DEVICE(ADF_401XXIOV_PCI_DEVICE_ID),
	ADF_SYSTEM_DEVICE(ADF_402XXIOV_PCI_DEVICE_ID),
	{0,}
};
MODULE_DEVICE_TABLE(pci, adf_pci_tbl);

static unsigned int adf_disconnect_status[ADF_MAX_DEVICES];

static int adf_probe(struct pci_dev *dev, const struct pci_device_id *ent);
static void adf_remove(struct pci_dev *dev);

static struct pci_driver adf_driver = {
	.id_table = adf_pci_tbl,
	.name = ADF_4XXXVF_DEVICE_NAME,
	.probe = adf_probe,
	.remove = adf_remove,
};

static void adf_cleanup_pci_dev(struct adf_accel_dev *accel_dev)
{
	pci_release_regions(accel_dev->accel_pci_dev.pci_dev);
	pci_disable_device(accel_dev->accel_pci_dev.pci_dev);
}

static void adf_cleanup_accel(struct adf_accel_dev *accel_dev)
{
	struct adf_accel_pci *accel_pci_dev = &accel_dev->accel_pci_dev;
	int i;

	for (i = 0; i < ADF_PCI_MAX_BARS; i++) {
		struct adf_bar *bar = &accel_pci_dev->pci_bars[i];

		if (bar->virt_addr)
			pci_iounmap(accel_pci_dev->pci_dev, bar->virt_addr);
	}

	if (accel_dev->hw_device) {
		switch (accel_pci_dev->pci_dev->device) {
		case ADF_4XXXIOV_PCI_DEVICE_ID:
		case ADF_401XXIOV_PCI_DEVICE_ID:
		case ADF_402XXIOV_PCI_DEVICE_ID:
			adf_clean_hw_data_4xxxiov(accel_dev->hw_device);
			break;
		default:
			break;
		}
		kfree(accel_dev->hw_device);
		accel_dev->hw_device = NULL;
	}
	debugfs_remove_recursive(accel_dev->pfvf_dbgdir);
	adf_cfg_dev_remove(accel_dev);
	debugfs_remove(accel_dev->debugfs_dir);
}

static void adf_cleanup_vf(struct adf_accel_dev *accel_dev,
			   struct adf_accel_dev *pf)
{
	struct adf_hw_device_data *hw_data;
	mutex_lock(&accel_dev->lock);

	hw_data = accel_dev->hw_device;
	clear_bit(ADF_STATUS_RESTARTING, &accel_dev->status);
	adf_devmgr_rm_dev(accel_dev, pf);

	if (hw_data->disable_vf2pf_comms)
		hw_data->disable_vf2pf_comms(accel_dev);
	if (test_bit(ADF_STATUS_IRQ_ALLOCATED, &accel_dev->status)) {
		hw_data->free_irq(accel_dev);
		clear_bit(ADF_STATUS_IRQ_ALLOCATED, &accel_dev->status);
	}
	/* Delete configuration and etr data here */
	adf_cfg_del_all(accel_dev);
#ifdef QAT_UIO
	adf_cfg_device_clear_all(accel_dev);
#endif
	adf_cleanup_etr_data(accel_dev);
	mutex_unlock(&accel_dev->lock);
}

static int adf_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct adf_accel_dev *accel_dev;
	struct adf_accel_dev *pf;
	struct adf_accel_pci *accel_pci_dev;
	struct adf_hw_device_data *hw_data;
	char name[ADF_DEVICE_NAME_LENGTH];
	unsigned int i, bar_nr;
	unsigned long bar_mask;
	int ret = 0;

	switch (ent->device) {
	case ADF_4XXXIOV_PCI_DEVICE_ID:
	case ADF_401XXIOV_PCI_DEVICE_ID:
	case ADF_402XXIOV_PCI_DEVICE_ID:
		break;
	default:
		dev_err(&pdev->dev, "Invalid device 0x%x.\n", ent->device);
		return -ENODEV;
	}

	accel_dev = kzalloc_node(sizeof(*accel_dev), GFP_KERNEL,
				 dev_to_node(&pdev->dev));
	if (!accel_dev)
		return -ENOMEM;

	accel_dev->accel_id = adf_devmgr_get_id(pdev);
	if (accel_dev->accel_id > ADF_MAX_DEVICES) {
		ret = -EFAULT;
		goto out_err;
	}

	mutex_init(&accel_dev->lock);
	accel_dev->is_vf = true;
	accel_pci_dev = &accel_dev->accel_pci_dev;
	accel_pci_dev->pci_dev = pdev;

	INIT_LIST_HEAD(&accel_dev->crypto_list);

	accel_dev->owner = THIS_MODULE;
	/* Allocate and configure device configuration structure */
	hw_data = kzalloc_node(sizeof(*hw_data), GFP_KERNEL,
			       dev_to_node(&pdev->dev));
	if (!hw_data) {
		ret = -ENOMEM;
		goto out_err;
	}
	accel_dev->hw_device = hw_data;
	adf_init_hw_data_4xxxiov(accel_dev->hw_device);
	pci_read_config_byte(pdev, PCI_REVISION_ID, &accel_pci_dev->revid);

	/* Get Accelerators and Accelerators Engines masks */
	hw_data->accel_mask = hw_data->get_accel_mask(accel_dev);
	hw_data->ae_mask = hw_data->get_ae_mask(accel_dev);
	hw_data->admin_ae_mask = hw_data->ae_mask;
	accel_pci_dev->sku = hw_data->get_sku(hw_data);

	/* Create dev top level debugfs entry */
	snprintf(name, sizeof(name), "%s%s_%04x:%02x:%02d.%02d",
		 ADF_DEVICE_NAME_PREFIX, hw_data->dev_class->name,
		 pci_domain_nr(pdev->bus),
		 pdev->bus->number, PCI_SLOT(pdev->devfn),
		 PCI_FUNC(pdev->devfn));

	accel_dev->debugfs_dir = debugfs_create_dir(name, NULL);
	if (!accel_dev->debugfs_dir) {
		dev_err(&pdev->dev, "Could not create debugfs dir %s\n", name);
		ret = -EINVAL;
		goto out_err;
	}
	adf_pfvf_debugfs_add(accel_dev);

	/* Create device configuration table */
	ret = adf_cfg_dev_add(accel_dev);
	if (ret)
		goto out_err;

	/* enable PCI device */
	if (pci_enable_device(pdev)) {
		ret = -EFAULT;
		goto out_err;
	}

	/* Set dma identifier */
	if (dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64))) {
		dev_err(&pdev->dev, "No usable DMA configuration.\n");
		ret = -EIO;
		goto out_err_disable;
	}

	if (pci_request_regions(pdev, ADF_4XXXVF_DEVICE_NAME)) {
		ret = -EFAULT;
		goto out_err_disable;
	}
#ifdef QAT_UIO

	hw_data->accel_capabilities_mask = adf_4xxxvf_get_hw_cap(accel_dev);
#endif

	/* Find and map all the device's BARS */
	i = 0;
	bar_mask = pci_select_bars(pdev, IORESOURCE_MEM);
	for_each_set_bit(bar_nr, &bar_mask, ADF_PCI_MAX_BARS * 2) {
		struct adf_bar *bar = &accel_pci_dev->pci_bars[i++];

		bar->base_addr = pci_resource_start(pdev, bar_nr);
		if (!bar->base_addr)
			break;
		bar->size = pci_resource_len(pdev, bar_nr);
		bar->virt_addr = pci_iomap(accel_pci_dev->pci_dev, bar_nr, 0);
		if (!bar->virt_addr) {
			dev_err(&pdev->dev, "Failed to map BAR %d\n", bar_nr);
			ret = -EFAULT;
			goto out_err_free_reg;
		}
	}
	pci_set_master(pdev);

	if (pci_save_state(pdev)) {
		dev_err(&pdev->dev, "Failed to save pci state\n");
		ret = -ENOMEM;
		goto out_err_free_reg;
	}

	/* Completion for VF2PF request/response message exchange */
	init_completion(&accel_dev->vf.iov_msg_completion);
	mutex_init(&accel_dev->vf.rpreset_lock);
	/* Completion for error notified to userspace */
	init_completion(&accel_dev->vf.err_notified);
	accel_dev->vf.is_err_notified = false;

	ret = adf_cfg_depot_restore_all(accel_dev);
	if (ret)
		goto out_err_free_reg;
#ifdef QAT_UIO

	ret = hw_data->config_device(accel_dev);
	if (ret)
		goto out_err_free_reg;
#endif

	ret = adf_dev_init(accel_dev);
	if (ret)
		goto out_err_dev_shutdown;

	ret = adf_dev_start(accel_dev);
	if (ret)
		goto out_err_dev_stop;

	/* Add accel device to accel table.
	 * This should be called before adf_devmgr_rm_dev() is called.
	 */
	pf = adf_devmgr_pci_to_accel_dev(pdev->physfn);
	ret = adf_devmgr_add_dev(accel_dev, pf);
	if (ret) {
		dev_err(&pdev->dev, "Failed to add new accelerator device.\n");
		goto out_err_dev_stop;
	}

	ret = adf_lkca_register(accel_dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register Linux Kernel Crypto API.\n");
		goto out_err_dev_mgr;
	}

	if (adf_disconnect_status[accel_dev->accel_id]) {
		adf_dev_restarted_notify(accel_dev);
		adf_disconnect_status[accel_dev->accel_id] = 0;
	}

	return ret;

out_err_dev_mgr:
	adf_devmgr_rm_dev(accel_dev, pf);
out_err_dev_stop:
	adf_dev_stop(accel_dev);
out_err_dev_shutdown:
	adf_dev_shutdown(accel_dev);
out_err_free_reg:
	pci_release_regions(accel_pci_dev->pci_dev);
out_err_disable:
	pci_disable_device(accel_pci_dev->pci_dev);
out_err:
	adf_cleanup_accel(accel_dev);
	kfree(accel_dev);
	return ret;
}

static void adf_remove(struct pci_dev *pdev)
{
	struct adf_accel_dev *accel_dev = adf_devmgr_pci_to_accel_dev(pdev);
	struct adf_accel_dev *pf = adf_devmgr_pci_to_accel_dev(pdev->physfn);
	struct adf_hw_device_data *hw_data;

	if (!accel_dev) {
		pr_err("QAT: Driver removal failed\n");
		return;
	}

	/*
	 * Don't return here to have a consistent behavior with
	 * other device drivers.
	 */
	if (!accel_dev->is_drv_rm) {
		dev_info(&GET_DEV(accel_dev), "Device removal\n");
		adf_disconnect_status[accel_dev->accel_id] = 1;
		adf_error_notifier((uintptr_t)accel_dev);

		if (adf_dev_restarting_notify_sync(accel_dev)) {
			adf_dev_ref_fixup(accel_dev);
			adf_disconnect_status[accel_dev->accel_id] = 0;
		}
	} else {
		adf_cfg_depot_del_all(accel_dev);
	}

	hw_data = accel_dev->hw_device;
	hw_data->disable_pf2vf_interrupt(accel_dev);
	adf_flush_vf_wq();
	if (test_bit(ADF_STATUS_RESTARTING, &accel_dev->status)) {
		adf_cleanup_vf(accel_dev, pf);
	} else {
		adf_lkca_unregister(accel_dev);
		adf_devmgr_rm_dev(accel_dev, pf);
		adf_dev_stop(accel_dev);
		adf_dev_shutdown(accel_dev);
	}
	adf_cleanup_accel(accel_dev);
	adf_cleanup_pci_dev(accel_dev);
	mutex_destroy(&accel_dev->lock);
	kfree(accel_dev);
}

static int __init adfdrv_init(void)
{
	request_module("intel_qat");

	if (pci_register_driver(&adf_driver)) {
		pr_err("QAT: Driver initialization failed\n");
		return -EFAULT;
	}
	return 0;
}

static void __exit adfdrv_release(void)
{
	adf_devmgr_update_drv_rm(adf_driver.id_table->device);
	pci_unregister_driver(&adf_driver);
	adf_clean_vf_map(true);
}

module_init(adfdrv_init);
module_exit(adfdrv_release);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Intel");
MODULE_DESCRIPTION("Intel(R) QuickAssist Technology");
MODULE_VERSION(ADF_DRV_VERSION);
