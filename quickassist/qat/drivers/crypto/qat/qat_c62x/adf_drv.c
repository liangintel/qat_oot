// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2021 Intel Corporation */
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
#include <linux/io.h>
#include <adf_accel_devices.h>
#include <adf_common_drv.h>
#include <adf_cfg.h>
#ifdef QAT_UIO
#include <adf_fw_counters.h>
#endif
#include "adf_c62x_hw_data.h"

#define ADF_SYSTEM_DEVICE(device_id) \
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, device_id)}

static const struct pci_device_id adf_pci_tbl[] = {
	ADF_SYSTEM_DEVICE(ADF_C62X_PCI_DEVICE_ID),
	{0,}
};
MODULE_DEVICE_TABLE(pci, adf_pci_tbl);

static int adf_probe(struct pci_dev *dev, const struct pci_device_id *ent);
static void adf_remove(struct pci_dev *dev);

static struct pci_driver adf_driver = {
	.id_table = adf_pci_tbl,
	.name = ADF_C62X_DEVICE_NAME,
	.probe = adf_probe,
	.remove = adf_remove,
#ifndef NO_SRIOV_CONFIGURE
	.sriov_configure = adf_sriov_configure,
#endif
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
		case ADF_C62X_PCI_DEVICE_ID:
			adf_clean_hw_data_c62x(accel_dev->hw_device);
			break;
		default:
			break;
		}
		kfree(accel_dev->hw_device);
		accel_dev->hw_device = NULL;
	}
#ifdef QAT_UIO
	adf_fw_counters_remove(accel_dev);
#endif
	adf_cfg_dev_remove(accel_dev);
	debugfs_remove(accel_dev->clock_dbgfile);
	debugfs_remove(accel_dev->debugfs_dir);
}

static int adf_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct adf_accel_dev *accel_dev;
	struct adf_accel_pci *accel_pci_dev;
	struct adf_hw_device_data *hw_data;
	char name[ADF_DEVICE_NAME_LENGTH];
	unsigned int i, bar_nr;
	unsigned long bar_mask;
	int ret = 0;

	switch (ent->device) {
	case ADF_C62X_PCI_DEVICE_ID:
		break;
	default:
		dev_err(&pdev->dev, "Invalid device 0x%x.\n", ent->device);
		return -ENODEV;
	}

	if (num_possible_nodes() > 1 && dev_to_node(&pdev->dev) < 0) {
		/* If the accelerator is connected to a node with no memory
		 * there is no point in using the accelerator since the remote
		 * memory transaction will be very slow.
		 */
		dev_err(&pdev->dev, "Invalid NUMA configuration.\n");
		return -EINVAL;
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
	INIT_LIST_HEAD(&accel_dev->crypto_list);
	accel_pci_dev = &accel_dev->accel_pci_dev;
	accel_pci_dev->pci_dev = pdev;

	accel_dev->owner = THIS_MODULE;
	/* Allocate and configure device configuration structure */
	hw_data = kzalloc_node(sizeof(*hw_data), GFP_KERNEL,
			       dev_to_node(&pdev->dev));
	if (!hw_data) {
		ret = -ENOMEM;
		goto out_err;
	}

	accel_dev->hw_device = hw_data;
	adf_init_hw_data_c62x(accel_dev->hw_device);
	pci_read_config_byte(pdev, PCI_REVISION_ID, &accel_pci_dev->revid);
	pci_read_config_dword(pdev, ADF_DEVICE_FUSECTL_OFFSET,
			      &hw_data->fuses);
	if (accel_pci_dev->revid == 0x00) {
		dev_err(&pdev->dev, "A0 stepping is not supported.\n");
		ret = -ENODEV;
		goto out_err;
	}

	/* Get PPAERUCM values and store */
	ret = adf_aer_store_ppaerucm_reg(pdev, hw_data);
	if (ret)
		goto out_err;

	/* Get Accelerators and Accelerators Engines masks */
	hw_data->accel_mask = hw_data->get_accel_mask(accel_dev);
	hw_data->ae_mask = hw_data->get_ae_mask(accel_dev);
	hw_data->admin_ae_mask = hw_data->ae_mask;
	accel_pci_dev->sku = hw_data->get_sku(hw_data);
	/* If the device has no acceleration engines then ignore it. */
	if (!hw_data->accel_mask || (~hw_data->ae_mask & 0x01)) {
		dev_err(&pdev->dev, "No acceleration units found");
		ret = -EFAULT;
		goto out_err;
	}

	/* Create dev top level debugfs entry */
	snprintf(name, sizeof(name), "%s%s_%04x:%02x:%02d.%d",
		 ADF_DEVICE_NAME_PREFIX, hw_data->dev_class->name,
		 pci_domain_nr(accel_to_pci_dev(accel_dev)->bus),
		 pdev->bus->number, PCI_SLOT(pdev->devfn),
		 PCI_FUNC(pdev->devfn));

	accel_dev->debugfs_dir = debugfs_create_dir(name, NULL);
	if (!accel_dev->debugfs_dir) {
		dev_err(&pdev->dev, "Could not create debugfs dir %s\n", name);
		ret = -EINVAL;
		goto out_err;
	}
	adf_clock_debugfs_add(accel_dev);

	/* Create device configuration table */
	ret = adf_cfg_dev_add(accel_dev);
	if (ret)
		goto out_err;

#ifdef QAT_UIO
	/* Create FW counters table */
	ret = adf_fw_counters_add(accel_dev);
	if (ret)
		goto out_err;
#endif

	/* enable PCI device */
	if (pci_enable_device(pdev)) {
		ret = -EFAULT;
		goto out_err;
	}

	/* Set dma identifier */
	if (dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(48))) {
		dev_err(&pdev->dev, "No usable DMA configuration\n");
		ret = -EIO;
		goto out_err_disable;
	}

	if (pci_request_regions(pdev, ADF_C62X_DEVICE_NAME)) {
		ret = -EFAULT;
		goto out_err_disable;
	}

	if (hw_data->get_accel_cap) {
		hw_data->accel_capabilities_mask =
			hw_data->get_accel_cap(accel_dev);
	}

	/* Find and map all the device's BARS */
	i = (hw_data->fuses & ADF_DEVICE_FUSECTL_MASK) ? 1 : 0;
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

	if (adf_enable_aer(accel_dev, &adf_driver)) {
		dev_err(&pdev->dev, "Failed to enable aer\n");
		ret = -EFAULT;
		goto out_err_free_reg;
	}

	if (pci_save_state(pdev)) {
		dev_err(&pdev->dev, "Failed to save pci state\n");
		ret = -ENOMEM;
		goto out_err_free_reg;
	}

	ret = qat_crypto_dev_config(accel_dev);
	if (ret)
		goto out_err_free_reg;

	ret = adf_dev_init(accel_dev);
	if (ret)
		goto out_err_dev_shutdown;

	ret = adf_dev_start(accel_dev);
	if (ret)
		goto out_err_dev_stop;

	/* Add accel device to accel table.
	 * This should be called before adf_devmgr_rm_dev() is called.
	 */
	ret = adf_devmgr_add_dev(accel_dev, NULL);
	if (ret) {
		dev_err(&pdev->dev, "Failed to add new accelerator device.\n");
		goto out_err_dev_stop;
	}

	ret = adf_lkca_register(accel_dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register Linux Kernel Crypto API.\n");
		goto out_err_dev_mgr;
	}

	return ret;

out_err_dev_mgr:
	adf_devmgr_rm_dev(accel_dev, NULL);
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

	if (!accel_dev) {
		pr_err("QAT: Driver removal failed\n");
		return;
	}
	adf_lkca_unregister(accel_dev);
	adf_devmgr_rm_dev(accel_dev, NULL);
	adf_dev_stop(accel_dev);
	adf_dev_shutdown(accel_dev);
	adf_disable_aer(accel_dev);
	adf_cleanup_accel(accel_dev);
	adf_cleanup_pci_dev(accel_dev);
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
	pci_unregister_driver(&adf_driver);
}

module_init(adfdrv_init);
module_exit(adfdrv_release);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Intel");
MODULE_FIRMWARE(ADF_C62X_FW);
MODULE_FIRMWARE(ADF_C62X_MMP);
MODULE_DESCRIPTION("Intel(R) QuickAssist Technology");
MODULE_VERSION(ADF_DRV_VERSION);
