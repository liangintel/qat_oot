// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2019 - 2020 Intel Corporation */
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
#include <qat_crypto.h>
#include <adf_transport_access_macros.h>
#include "adf_vqat_hw_data.h"

#define ADF_SYSTEM_DEVICE(device_id) \
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, device_id)}

static const struct pci_device_id adf_pci_tbl[] = {
	ADF_SYSTEM_DEVICE(ADF_VQAT_PCI_DEVICE_ID),
	{0,}
};
MODULE_DEVICE_TABLE(pci, adf_pci_tbl);

static int adf_probe(struct pci_dev *dev, const struct pci_device_id *ent);
static void adf_remove(struct pci_dev *dev);

static struct pci_driver adf_driver = {
	.id_table = adf_pci_tbl,
	.name = ADF_VQAT_DEVICE_NAME,
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
		case ADF_VQAT_PCI_DEVICE_ID:
			adf_clean_hw_data_vqat(accel_dev->hw_device);
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

/* TODO: Duplicated code inside it, exisiting 1.x/2.0 drivers should have a
 * common routine to add configuration, but it's not there.
 * Use vqat specific one as a temporary implementation.
 */
static int adf_vqat_config(struct adf_accel_dev *accel_dev)
{
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	char val_str[ADF_CFG_MAX_VAL_LEN_IN_BYTES];
	unsigned long val;
	int banks = GET_MAX_BANKS(accel_dev);
	int instances =  banks;
	u16 serv_type = 0;
	int i = 0;
	int bank = 0;
	unsigned long tx_rx_offset = accel_dev->hw_device->tx_rx_gap;

	if (adf_cfg_section_add(accel_dev, ADF_GENERAL_SEC))
		goto err;
	if (adf_cfg_section_add(accel_dev, ADF_KERNEL_SEC))
		goto err;
	if (adf_cfg_section_add(accel_dev, "Accelerator0"))
		goto err;

	switch (accel_to_pci_dev(accel_dev)->subsystem_device) {
	case ADF_VQAT_SYM_PCI_SUBSYSTEM_ID:
		serv_type = SYM;
		break;
	case ADF_VQAT_ASYM_PCI_SUBSYSTEM_ID:
		serv_type = ASYM;
		break;
	case ADF_VQAT_DC_PCI_SUBSYSTEM_ID:
		return 0;
	default:
		goto err;
	}

	while (i < instances) {
		if (bank == banks)
			break;

		snprintf(key, sizeof(key),
			 ADF_CY "%d" ADF_ETRMGR_CORE_AFFINITY, i);
		if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						key, (void *)&val, ADF_DEC))
			goto err;

		val = bank;
		snprintf(key, sizeof(key), ADF_SERVICES_ENABLED);
		switch (serv_type) {
		case ASYM:
			snprintf(val_str, sizeof(val_str), ADF_CFG_ASYM);
			if (adf_cfg_add_key_value_param(accel_dev,
							ADF_GENERAL_SEC,
							key, (void *)val_str,
							ADF_STR))
				goto err;

			snprintf(key, sizeof(key),
				 ADF_CY "%d" ADF_RING_BANK_NUM_ASYM, i);
			if (adf_cfg_add_key_value_param(accel_dev,
							ADF_KERNEL_SEC,
							key, (void *)&val,
							ADF_DEC))
				goto err;
			snprintf(key, sizeof(key),
				 ADF_CY "%d" ADF_RING_ASYM_SIZE, i);
			val = ADF_DEFAULT_ASYM_RING_SIZE;
			if (adf_cfg_add_key_value_param(accel_dev,
							ADF_KERNEL_SEC,
							key, (void *)&val,
							ADF_DEC))
				goto err;

			val = 0;
			snprintf(key, sizeof(key),
				 ADF_CY "%d" ADF_RING_ASYM_TX, i);
			if (adf_cfg_add_key_value_param(accel_dev,
							ADF_KERNEL_SEC, key,
							(void *)&val, ADF_DEC))
				goto err;

			val = tx_rx_offset;
			snprintf(key, sizeof(key),
				 ADF_CY "%d" ADF_RING_ASYM_RX, i);
			if (adf_cfg_add_key_value_param(accel_dev,
							ADF_KERNEL_SEC, key,
							(void *)&val, ADF_DEC))
				goto err;

			val = accel_dev->hw_device->coalescing_def_time;
			snprintf(key, sizeof(key),
				 ADF_ETRMGR_COALESCE_TIMER_FORMAT, bank);
			if (adf_cfg_add_key_value_param(accel_dev,
							"Accelerator0", key,
							(void *)&val, ADF_DEC))
				goto err;

			break;

		case SYM:
			snprintf(val_str, sizeof(val_str), ADF_CFG_SYM);
			if (adf_cfg_add_key_value_param(accel_dev,
							ADF_GENERAL_SEC,
							key, (void *)val_str,
							ADF_STR))
				goto err;

			snprintf(key, sizeof(key),
				 ADF_CY "%d" ADF_RING_BANK_NUM_SYM, i);
			if (adf_cfg_add_key_value_param(accel_dev,
							ADF_KERNEL_SEC,
							key, (void *)&val,
							ADF_DEC))
				goto err;
			val = ADF_DEFAULT_SYM_RING_SIZE;
			snprintf(key, sizeof(key),
				 ADF_CY "%d" ADF_RING_SYM_SIZE, i);
			if (adf_cfg_add_key_value_param(accel_dev,
							ADF_KERNEL_SEC, key,
							(void *)&val, ADF_DEC))
				goto err;
			val = 0;
			snprintf(key, sizeof(key),
				 ADF_CY "%d" ADF_RING_SYM_TX, i);
			if (adf_cfg_add_key_value_param(accel_dev,
							ADF_KERNEL_SEC, key,
							(void *)&val, ADF_DEC))
				goto err;

			val += tx_rx_offset;
			snprintf(key, sizeof(key),
				 ADF_CY "%d" ADF_RING_SYM_RX, i);
			if (adf_cfg_add_key_value_param(accel_dev,
							ADF_KERNEL_SEC, key,
							(void *)&val, ADF_DEC))
				goto err;

			val = accel_dev->hw_device->coalescing_def_time;
			snprintf(key, sizeof(key),
				 ADF_ETRMGR_COALESCE_TIMER_FORMAT, bank);
			if (adf_cfg_add_key_value_param(accel_dev,
							"Accelerator0", key,
							(void *)&val, ADF_DEC))
				goto err;
			break;
		default:
			break;
		}
		i++;
		bank++;
	}

	val = i;
	if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
					ADF_NUM_CY, (void *)&val, ADF_DEC))
		goto err;

	set_bit(ADF_STATUS_CONFIGURED, &accel_dev->status);
	return 0;
err:
	dev_err(&GET_DEV(accel_dev), "Failed to start QAT accel dev\n");
	return -EINVAL;
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
	case ADF_VQAT_PCI_DEVICE_ID:
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
	adf_init_hw_data_vqat(accel_dev->hw_device);
	pci_read_config_byte(pdev, PCI_REVISION_ID, &accel_pci_dev->revid);

	/* Get Accelerators and Accelerators Engines masks */
	hw_data->accel_mask = hw_data->get_accel_mask(accel_dev);
	hw_data->ae_mask = hw_data->get_ae_mask(accel_dev);
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

	if (pci_request_regions(pdev, ADF_VQAT_DEVICE_NAME)) {
		ret = -EFAULT;
		goto out_err_disable;
	}

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

	/* Completion for VDCM/VQAT request/response message exchange */
	init_completion(&accel_dev->vf.iov_msg_completion);
	/* Completion for error notified to userspace */
	init_completion(&accel_dev->vf.err_notified);
	accel_dev->vf.is_err_notified = false;

	if (adf_vqat_get_cap(accel_dev))
		goto out_err_free_reg;

#ifdef QAT_UIO
	adf_vqat_cfg_get_accel_algo_cap(accel_dev);
#endif

	if (adf_vqat_config(accel_dev))
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

	if (!accel_dev) {
		pr_err("QAT: Driver removal failed\n");
		return;
	}
	adf_lkca_unregister(accel_dev);
	adf_devmgr_rm_dev(accel_dev, pf);
	adf_dev_stop(accel_dev);
	adf_dev_shutdown(accel_dev);
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
	pr_info("vQAT driver is loaded!");

	return 0;
}

static void __exit adfdrv_release(void)
{
	pci_unregister_driver(&adf_driver);
	/* TODO: 1.x common driver has an issue here in case multi generations
	 * of VF exist in the same system. Passing device id to
	 * adf_clean_vf_map could be an potential solution. Revisit this later!
	 */
	adf_clean_vf_map(true);
	pr_info("vQAT driver is removed!");
}

module_init(adfdrv_init);
module_exit(adfdrv_release);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Intel");
MODULE_DESCRIPTION("Intel(R) QuickAssist Technology");
MODULE_VERSION(ADF_DRV_VERSION);
