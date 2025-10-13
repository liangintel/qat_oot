// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2021 Intel Corporation */
#include <linux/version.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/bitops.h>
#include <linux/pci.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/crypto.h>
#include <linux/device.h>
#include <linux/mman.h>
#if (KERNEL_VERSION(6, 4, 0) <= LINUX_VERSION_CODE)
#include <crypto/algapi.h>
#endif

#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "adf_cfg.h"
#include "adf_cfg_common.h"
#include "adf_cfg_user.h"
#ifdef QAT_UIO
#include "adf_uio.h"
#include "qdm.h"
#include "icp_qat_hw.h"
#include "adf_sla.h"
#include "adf_ctl_rl.h"
#endif
#include "adf_heartbeat.h"
#ifdef CONFIG_CRYPTO_DEV_QAT_VDCM
#include "adf_vdcm.h"
#endif
#include "adf_svm.h"

/* Max number of PCI buses in system */
#define MAX_PCI_BUS 256
/* Maximum number of instances multiplied by 5 */
#define COPY_KEY_LOOP_LIMIT 2560

#define DEVICE_NAME "qat_adf_ctl"

static DEFINE_MUTEX(adf_ctl_lock);
static long adf_ctl_ioctl(struct file *fp, unsigned int cmd, unsigned long arg);
static int adf_ctl_mmap_misc_counter(struct file *fp,
				     struct vm_area_struct *vma);


static const struct file_operations adf_ctl_ops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = adf_ctl_ioctl,
	.compat_ioctl = adf_ctl_ioctl,
	.mmap = adf_ctl_mmap_misc_counter,
};

void *misc_counter;
EXPORT_SYMBOL_GPL(misc_counter);

struct adf_ctl_drv_info {
	unsigned int major;
	struct cdev drv_cdev;
	struct class *drv_class;
};

static struct adf_ctl_drv_info adf_ctl_drv;

static void adf_chr_drv_destroy(void)
{
	device_destroy(adf_ctl_drv.drv_class, MKDEV(adf_ctl_drv.major, 0));
	cdev_del(&adf_ctl_drv.drv_cdev);
	class_destroy(adf_ctl_drv.drv_class);
	unregister_chrdev_region(MKDEV(adf_ctl_drv.major, 0), 1);
}

static int adf_chr_drv_create(void)
{
	dev_t dev_id;
	struct device *drv_device;

	if (alloc_chrdev_region(&dev_id, 0, 1, DEVICE_NAME)) {
		pr_err("QAT: unable to allocate chrdev region\n");
		return -EFAULT;
	}

	adf_ctl_drv.drv_class = class_create(DEVICE_NAME);
	if (IS_ERR(adf_ctl_drv.drv_class)) {
		pr_err("QAT: class_create failed for adf_ctl\n");
		goto err_chrdev_unreg;
	}
	adf_ctl_drv.major = MAJOR(dev_id);
	cdev_init(&adf_ctl_drv.drv_cdev, &adf_ctl_ops);
	if (cdev_add(&adf_ctl_drv.drv_cdev, dev_id, 1)) {
		pr_err("QAT: cdev add failed\n");
		goto err_class_destr;
	}

	drv_device = device_create(adf_ctl_drv.drv_class, NULL,
				   MKDEV(adf_ctl_drv.major, 0),
				   NULL, DEVICE_NAME);
	if (IS_ERR(drv_device)) {
		pr_err("QAT: failed to create device\n");
		goto err_cdev_del;
	}
	return 0;
err_cdev_del:
	cdev_del(&adf_ctl_drv.drv_cdev);
err_class_destr:
	class_destroy(adf_ctl_drv.drv_class);
err_chrdev_unreg:
	unregister_chrdev_region(dev_id, 1);
	return -EFAULT;
}

static int adf_ctl_alloc_resources(struct adf_user_cfg_ctl_data **ctl_data,
				   unsigned long arg)
{
	struct adf_user_cfg_ctl_data *cfg_data;

	cfg_data = kzalloc(sizeof(*cfg_data), GFP_KERNEL);
	if (!cfg_data)
		return -ENOMEM;

	/* Initialize device id to NO DEVICE as 0 is a valid device id */
	cfg_data->device_id = ADF_CFG_NO_DEVICE;

	if (copy_from_user(cfg_data, (void __user *)arg, sizeof(*cfg_data))) {
		pr_err("QAT: failed to copy from user cfg_data.\n");
		kfree(cfg_data);
		return -EIO;
	}

	*ctl_data = cfg_data;
	return 0;
}

static int adf_add_key_value_data(struct adf_accel_dev *accel_dev,
				  const char *section,
				  const struct adf_user_cfg_key_val *key_val)
{
	if (key_val->type == ADF_HEX) {
		long *ptr = (long *)key_val->val;
		long val = *ptr;

		if (adf_cfg_add_key_value_param(accel_dev, section,
						key_val->key, (void *)val,
						key_val->type)) {
			dev_err(&GET_DEV(accel_dev),
				"failed to add hex keyvalue.\n");
			return -EFAULT;
		}
	} else {
		if (adf_cfg_add_key_value_param(accel_dev, section,
						key_val->key, key_val->val,
						key_val->type)) {
			dev_err(&GET_DEV(accel_dev),
				"failed to add keyvalue.\n");
			return -EFAULT;
		}
	}
	return 0;
}

static int adf_copy_key_value_data(struct adf_accel_dev *accel_dev,
				   struct adf_user_cfg_ctl_data *ctl_data)
{
	u32 inner_loop_ct = 0;
	u32 outer_loop_ct = 0;
	struct adf_user_cfg_key_val key_val;
	struct adf_user_cfg_key_val *params_head;
	struct adf_user_cfg_section section, *section_head;

	section_head = ctl_data->config_section;

	while (section_head && outer_loop_ct < COPY_KEY_LOOP_LIMIT) {
		if (copy_from_user(&section, (void __user *)section_head,
				   sizeof(*section_head))) {
			dev_err(&GET_DEV(accel_dev),
				"failed to copy section info\n");
			goto out_err;
		}

		if (adf_cfg_section_add(accel_dev, &section.name[0])) {
			dev_err(&GET_DEV(accel_dev),
				"failed to add section.\n");
			goto out_err;
		}

		params_head = section.params;

		while (params_head && inner_loop_ct < COPY_KEY_LOOP_LIMIT) {
			if (copy_from_user(&key_val, (void __user *)params_head,
					   sizeof(key_val))) {
				dev_err(&GET_DEV(accel_dev),
					"Failed to copy keyvalue.\n");
				goto out_err;
			}
			if (adf_add_key_value_data(accel_dev, &section.name[0],
						   &key_val)) {
				goto out_err;
			}
			params_head = key_val.next;
			inner_loop_ct++;
		}
		section_head = section.next;
		inner_loop_ct = 0;
		outer_loop_ct++;
	}
	return 0;
out_err:
	adf_cfg_del_all(accel_dev);
	return -EFAULT;
}

#ifdef QAT_UIO
static int adf_copy_keyval_to_user(struct adf_accel_dev *accel_dev,
				   struct adf_user_cfg_ctl_data *ctl_data)
{
	struct adf_user_cfg_key_val key_val;
	struct adf_user_cfg_section section;
	char val[ADF_CFG_MAX_VAL_LEN_IN_BYTES] = {0};
	char *user_ptr;

	if (copy_from_user(&section, (void __user *)ctl_data->config_section,
			   sizeof(struct adf_user_cfg_section))) {
		dev_err(&GET_DEV(accel_dev), "failed to copy section info\n");
		return -EFAULT;
	}

	if (copy_from_user(&key_val, (void __user *)section.params,
			   sizeof(struct adf_user_cfg_key_val))) {
		dev_err(&GET_DEV(accel_dev), "failed to copy key val\n");
		return -EFAULT;
	}

	user_ptr = ((char *)section.params) + ADF_CFG_MAX_KEY_LEN_IN_BYTES;

	if (adf_cfg_get_param_value(accel_dev, &section.name[0],
				    &key_val.key[0], val)) {
		dev_dbg(&GET_DEV(accel_dev),
			"failed to get %s value from config!\n", key_val.key);
		return -EFAULT;
	}

	if (copy_to_user((void __user *)user_ptr, val,
			 ADF_CFG_MAX_VAL_LEN_IN_BYTES)) {
		dev_err(&GET_DEV(accel_dev),
			"failed to copy keyvalue to user!\n");
		return -EFAULT;
	}

	return 0;
}

#endif
static int adf_ctl_ioctl_dev_config(unsigned long arg)
{
	int ret;
	struct adf_user_cfg_ctl_data *ctl_data;
	struct adf_accel_dev *accel_dev;
#ifdef QAT_UIO
	struct adf_hw_device_data *hw_data;
#endif

	ret = adf_ctl_alloc_resources(&ctl_data, arg);
	if (ret)
		return ret;

	accel_dev = adf_devmgr_get_dev_by_id(ctl_data->device_id);
	if (!accel_dev) {
		ret = -EFAULT;
		goto out;
	}

#ifdef QAT_UIO
	hw_data = accel_dev->hw_device;
	if (!hw_data) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to init device - hw_data not set\n");
		ret = -EFAULT;
		goto out;
	}

#endif
	if (adf_dev_started(accel_dev)) {
		ret = -EFAULT;
		goto out;
	}

	if (adf_copy_key_value_data(accel_dev, ctl_data)) {
		ret = -EFAULT;
		goto out;
	}

	/* Delete the original saved config before saving the new
	 * config from userspace
	 */
	adf_cfg_depot_del_all(accel_dev);

	if (adf_cfg_depot_save_all(accel_dev)) {
		ret = -ENOMEM;
		goto out;
	}
#ifdef QAT_UIO

	if (hw_data->config_device && hw_data->config_device(accel_dev)) {
		ret = -EFAULT;
		goto out;
	}

#endif
	set_bit(ADF_STATUS_CONFIGURED, &accel_dev->status);
out:
	kfree(ctl_data);
	return ret;
}

static int adf_ctl_is_device_in_use(int id)
{
	struct adf_accel_dev *dev = NULL;

	list_for_each_entry(dev, adf_devmgr_get_head(), list) {
		if (id == dev->accel_id || id == ADF_CFG_ALL_DEVICES) {
			if (adf_devmgr_in_reset(dev) || adf_dev_in_use(dev)) {
				dev_info(&GET_DEV(dev),
					 "device qat_dev%d is busy\n",
					 dev->accel_id);
				return -EBUSY;
			}
		}
	}
	return 0;
}

static void adf_ctl_stop_devices(uint32_t id)
{
	struct adf_accel_dev *accel_dev = NULL;

	list_for_each_entry(accel_dev, adf_devmgr_get_head(), list) {
		if (id == accel_dev->accel_id || id == ADF_CFG_ALL_DEVICES) {
			if (!adf_dev_started(accel_dev))
				continue;

			/* First stop all VFs */
			if (!accel_dev->is_vf)
				continue;

			adf_lkca_unregister(accel_dev);
			adf_dev_stop(accel_dev);
			adf_dev_shutdown(accel_dev);
		}
	}

	list_for_each_entry(accel_dev, adf_devmgr_get_head(), list) {
		if (id == accel_dev->accel_id || id == ADF_CFG_ALL_DEVICES) {
			if (!adf_dev_started(accel_dev))
				continue;

			adf_lkca_unregister(accel_dev);
			adf_dev_stop(accel_dev);
			adf_dev_shutdown(accel_dev);
		}
	}
}

#ifdef QAT_UIO
static int adf_ctl_reset_devices(uint32_t id)
{
	struct adf_accel_dev *accel_dev = NULL;
	int ret = 0;

	list_for_each_entry(accel_dev, adf_devmgr_get_head(), list) {
		if (id == accel_dev->accel_id ||
		    (id == ADF_CFG_ALL_DEVICES && !accel_dev->is_vf)) {
			if (adf_dev_reset(accel_dev, ADF_DEV_RESET_ASYNC)) {
				dev_info(&GET_DEV(accel_dev),
					 "Failed to reset qat_dev%d\n",
					 accel_dev->accel_id);
				ret = -EFAULT;
			}
		}
	}
	return ret;
}
#endif

static int adf_ctl_ioctl_dev_stop(unsigned long arg)
{
	int ret;
	struct adf_user_cfg_ctl_data *ctl_data;

	ret = adf_ctl_alloc_resources(&ctl_data, arg);
	if (ret)
		return ret;

	if (adf_devmgr_verify_id(&ctl_data->device_id)) {
		pr_err("QAT: Device %d not found\n", ctl_data->device_id);
		ret = -ENODEV;
		goto out;
	}

	ret = adf_ctl_is_device_in_use(ctl_data->device_id);
	if (ret)
		goto out;

	if (ctl_data->device_id == ADF_CFG_ALL_DEVICES)
		pr_info("QAT: Stopping all acceleration devices.\n");
	else
		pr_info("QAT: Stopping acceleration device qat_dev%d.\n",
			ctl_data->device_id);

	adf_ctl_stop_devices(ctl_data->device_id);

out:
	kfree(ctl_data);
	return ret;
}

static int adf_ctl_ioctl_dev_start(unsigned long arg)
{
	int ret;
	struct adf_user_cfg_ctl_data *ctl_data;
	struct adf_accel_dev *accel_dev;

	ret = adf_ctl_alloc_resources(&ctl_data, arg);
	if (ret)
		return ret;

	accel_dev = adf_devmgr_get_dev_by_id(ctl_data->device_id);
	if (!accel_dev) {
		ret = -ENODEV;
		goto out;
	}

	if (!adf_dev_started(accel_dev)) {
		dev_info(&GET_DEV(accel_dev),
			 "Starting acceleration device qat_dev%d.\n",
			 ctl_data->device_id);
		ret = adf_dev_init(accel_dev);
		if (!ret)
			ret = adf_dev_start(accel_dev);
	} else {
		dev_info(&GET_DEV(accel_dev),
			 "Acceleration device qat_dev%d already started.\n",
			 ctl_data->device_id);
		goto out;
	}

	if (!ret)
		ret = adf_lkca_register(accel_dev);

	if (ret) {
		dev_err(&GET_DEV(accel_dev), "Failed to start qat_dev%d\n",
			ctl_data->device_id);
		adf_dev_stop(accel_dev);
		adf_dev_shutdown(accel_dev);
#ifdef QAT_UIO
	} else if (!accel_dev->is_vf) {
		ret = adf_cfg_setup_irq(accel_dev);
		if (ret) {
			dev_err(&GET_DEV(accel_dev),
				"Failed to setup irq for qat_dev%d\n",
				ctl_data->device_id);
			adf_lkca_unregister(accel_dev);
			adf_dev_stop(accel_dev);
			adf_dev_shutdown(accel_dev);
		}
#endif
	}
	if (adf_devmgr_in_reset(accel_dev)) {
		clear_bit(ADF_STATUS_RESTARTING, &accel_dev->status);
		adf_dev_restarted_notify(accel_dev);
	}
out:
	kfree(ctl_data);
	return ret;
}

static int adf_ctl_ioctl_get_num_devices(unsigned long arg)
{
	u32 num_devices = 0;

	adf_devmgr_get_num_dev(&num_devices);
	if (copy_to_user((void __user *)arg, &num_devices, sizeof(num_devices)))
		return -EFAULT;

	return 0;
}

#ifdef QAT_UIO
/*
 * adf_ctl_ioctl_dev_reset
 *
 * Function to reset acceleration device
 */
static int adf_ctl_ioctl_dev_reset(unsigned long arg)
{
	int ret;
	struct adf_user_cfg_ctl_data *ctl_data = NULL;

	ret = adf_ctl_alloc_resources(&ctl_data, arg);
	if (ret)
		return ret;

	/* Verify the device id */
	if (adf_devmgr_verify_id(&ctl_data->device_id)) {
		pr_err("QAT: Device %d not found\n", ctl_data->device_id);
		ret = -ENODEV;
		goto out;
	}

	if (ctl_data->device_id == ADF_CFG_ALL_DEVICES)
		pr_info("QAT: Scheduling reset of all acceleration devices.\n");
	else
		pr_info("QAT: Scheduling reset of device icp_dev%d.\n",
			(uint32_t)ctl_data->device_id);

	ret = adf_ctl_reset_devices(ctl_data->device_id);
	if (ret) {
		pr_err("QAT: Failed to reset device %d.\n",
		       (uint32_t)ctl_data->device_id);
		ret = -ENODEV;
		goto out;
	}

out:
	kfree(ctl_data);
	return ret;
}

#endif

/*
 * adf_get_dev_node_id
 *
 * Function determines to what physical die the pci dev is connected to.
 */
static int adf_get_dev_node_id(struct pci_dev *pdev)
{
	int node_id = dev_to_node(&pdev->dev);

	if (node_id < 0) {
		unsigned int bus_per_cpu = 0;
		struct cpuinfo_x86 *c = &cpu_data(num_online_cpus() - 1);

		node_id = 0;

		/* if there is only one physical processor don't need
		 * to do any further calculations
		 */
		if (get_phys_proc_id(c) == 0) {
			node_id = 0;
		} else {
			bus_per_cpu = MAX_PCI_BUS / (get_phys_proc_id(c) + 1);
			if (bus_per_cpu != 0)
				node_id = pdev->bus->number / bus_per_cpu;
		}
	}

	return node_id;
}

static int adf_ctl_ioctl_get_status(unsigned long arg)
{
	struct adf_hw_device_data *hw_data;
	struct adf_dev_status_info dev_info;
	struct adf_accel_dev *accel_dev;

	if (copy_from_user(&dev_info, (void __user *)arg,
			   sizeof(struct adf_dev_status_info))) {
		pr_err("QAT: failed to copy from user.\n");
		return -EFAULT;
	}

	accel_dev = adf_devmgr_get_dev_by_id(dev_info.accel_id);
	if (!accel_dev)
		return -ENODEV;

	hw_data = accel_dev->hw_device;
	dev_info.state = adf_dev_started(accel_dev) ? DEV_UP : DEV_DOWN;
	dev_info.num_ae = hw_data->get_num_aes(hw_data);
	dev_info.num_accel = hw_data->get_num_accels(hw_data);
	dev_info.num_logical_accel = hw_data->num_logical_accel;
	dev_info.banks_per_accel = hw_data->num_banks
					/ hw_data->num_logical_accel;
	dev_info.rings_per_bank = hw_data->num_rings_per_bank;
	strscpy(dev_info.name, hw_data->dev_class->name, sizeof(dev_info.name));
	dev_info.instance_id = hw_data->instance_id;
	dev_info.node_id     = adf_get_dev_node_id(accel_to_pci_dev(accel_dev));
#ifdef QAT_UIO
#ifdef QAT_KPT
	dev_info.kpt_achandle = hw_data->kpt_achandle;
#endif
#endif
	dev_info.type = hw_data->dev_class->type;
	dev_info.domain = pci_domain_nr(accel_to_pci_dev(accel_dev)->bus);
	dev_info.bus = accel_to_pci_dev(accel_dev)->bus->number;
	dev_info.dev = PCI_SLOT(accel_to_pci_dev(accel_dev)->devfn);
	dev_info.fun = PCI_FUNC(accel_to_pci_dev(accel_dev)->devfn);

	if (copy_to_user((void __user *)arg, &dev_info,
			 sizeof(struct adf_dev_status_info))) {
		dev_err(&GET_DEV(accel_dev), "failed to copy status.\n");
		return -EFAULT;
	}
	return 0;
}

#ifdef QAT_UIO
static int adf_ctl_ioctl_dev_get_value(unsigned long arg)
{
	int ret = 0;
	struct adf_user_cfg_ctl_data *ctl_data;
	struct adf_accel_dev *accel_dev;

	ret = adf_ctl_alloc_resources(&ctl_data, arg);
	if (ret)
		return ret;

	accel_dev = adf_devmgr_get_dev_by_id(ctl_data->device_id);
	if (!accel_dev) {
		pr_err("QAT: Device %d not found\n", ctl_data->device_id);
		ret = -ENODEV;
		goto out;
	}

	ret = adf_copy_keyval_to_user(accel_dev, ctl_data);
	if (ret) {
		ret = -ENODEV;
		goto out;
	}
out:
	kfree(ctl_data);
	return ret;
}

static int adf_ctl_ioctl_get_real_id(unsigned long arg)
{
	u32 fake_id;
	int real_id;

	if (copy_from_user(&fake_id, (void __user *)arg,
			   sizeof(fake_id))) {
		pr_err("QAT: failed to copy from user.\n");
		return -EFAULT;
	}

	real_id = adf_devmgr_get_real_id(fake_id);
	if (real_id < 0)
		return -EFAULT;
	if (copy_to_user((void __user *)arg, &real_id,
			 sizeof(real_id))) {
		pr_err("QAT: failed to copy to user.\n");
		return -EFAULT;
	}
	return 0;
}
#endif

static int adf_ctl_ioctl_heartbeat(unsigned long arg)
{
	int ret = 0;
	struct adf_accel_dev *accel_dev;
	struct adf_dev_heartbeat_status_ctl hb_status;

	if (copy_from_user(&hb_status, (void __user *)arg, sizeof(hb_status))) {
		pr_err("QAT: failed to copy from user hb_status.\n");
		return -EFAULT;
	}

	accel_dev = adf_devmgr_get_dev_by_id(hb_status.device_id);
	if (!accel_dev)
		return -ENODEV;

	if (adf_heartbeat_status(accel_dev, &hb_status.status)) {
		dev_err(&GET_DEV(accel_dev),
			"failed to get heartbeat status\n");
		return -EAGAIN;
	}

	if (copy_to_user((void __user *)arg, &hb_status, sizeof(hb_status))) {
		dev_err(&GET_DEV(accel_dev),
			"failed to copy hb_status to user!\n");
		ret = -EFAULT;
	}
	return ret;
}

#ifdef QAT_HB_FAIL_SIM
static int adf_ctl_ioctl_heartbeat_sim_fail(unsigned long arg)
{
	struct adf_accel_dev *accel_dev =
			adf_devmgr_get_dev_by_id(arg);

	if (!accel_dev)
		return -ENODEV;

	if (adf_heartbeat_simulate_failure(accel_dev))
		return -EFAULT;

	return 0;
}
#endif

#ifdef QAT_ERR_INJECTION_SIM
static int adf_ctl_write_pmisc(unsigned long arg)
{
	struct adf_accel_dev *accel_dev = NULL;
	struct adf_accel_pci *pci_info = NULL;
	struct adf_hw_device_data *hw_data = NULL;
	struct adf_bar *bar = NULL;
	struct adf_pmisc_write_info pmisc = {0};

	if (copy_from_user(&pmisc, (void __user *)arg,
			   sizeof(struct adf_pmisc_write_info))) {
		pr_err("Cannot copy\n");
		return -EFAULT;
	}

	accel_dev = adf_devmgr_get_dev_by_id(pmisc.accel_id);
	if (!accel_dev) {
		pr_err("QAT: Device %d not found\n", pmisc.accel_id);
		return -ENODEV;
	}

	pci_info = &accel_dev->accel_pci_dev;
	hw_data = accel_dev->hw_device;
	bar = &pci_info->pci_bars[hw_data->get_misc_bar_id(hw_data)];
	if (pmisc.offset <= bar->size - sizeof(pmisc.value))
		ADF_CSR_WR(bar->virt_addr, pmisc.offset, pmisc.value);

	return 0;
}
#endif

static int adf_ctl_mmap_misc_counter(struct file *fp,
				     struct vm_area_struct *vma)
{
	unsigned long mem_size = vma->vm_end - vma->vm_start;

	if (mem_size != PAGE_SIZE) {
		pr_err("QAT: Incorrect mem_size for misc counter.\n");
		return -EINVAL;
	}

	if (misc_counter) {
		if (remap_pfn_range(vma, vma->vm_start,
				    virt_to_phys(misc_counter) >> PAGE_SHIFT,
				    mem_size,
				    vma->vm_page_prot)) {
			pr_err("QAT: Failed to mmap misc counter to user.\n");
			return -EFAULT;
		}
	}

	return 0;
}

static long adf_ctl_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	bool allowed = false;
	int i;
	static const unsigned int unrestricted_cmds[] = {
		IOCTL_GET_NUM_DEVICES,
		IOCTL_STATUS_ACCEL_DEV,
		IOCTL_HEARTBEAT_ACCEL_DEV,
#ifdef QAT_UIO
		IOCTL_GET_CFG_VAL,
		IOCTL_RESERVE_RING,
		IOCTL_RELEASE_RING,
		IOCTL_ENABLE_RING,
		IOCTL_DISABLE_RING,
		IOCTL_GET_DEV_REAL_ID,
#endif
	};

	if (!capable(CAP_SYS_ADMIN)) {
		for (i = 0; i < ARRAY_SIZE(unrestricted_cmds); i++) {
			if (cmd == unrestricted_cmds[i]) {
				allowed = true;
				break;
			}
		}
		if (!allowed)
			return -EACCES;
	}

	if (mutex_lock_interruptible(&adf_ctl_lock))
		return -EFAULT;

	switch (cmd) {
	case IOCTL_CONFIG_SYS_RESOURCE_PARAMETERS:
		ret = adf_ctl_ioctl_dev_config(arg);
		break;

	case IOCTL_STOP_ACCEL_DEV:
		ret = adf_ctl_ioctl_dev_stop(arg);
		break;

	case IOCTL_START_ACCEL_DEV:
		ret = adf_ctl_ioctl_dev_start(arg);
		break;

	case IOCTL_GET_NUM_DEVICES:
		ret = adf_ctl_ioctl_get_num_devices(arg);
		break;

	case IOCTL_STATUS_ACCEL_DEV:
		ret = adf_ctl_ioctl_get_status(arg);
		break;
	case IOCTL_HEARTBEAT_ACCEL_DEV:
		ret = adf_ctl_ioctl_heartbeat(arg);
		break;
#ifdef QAT_HB_FAIL_SIM
	case IOCTL_HEARTBEAT_SIM_FAIL:
		ret = adf_ctl_ioctl_heartbeat_sim_fail(arg);
		break;
#endif
#ifdef QAT_ERR_INJECTION_SIM
	case IOCTL_WRITE_PMISC:
		ret = adf_ctl_write_pmisc(arg);
		break;
#endif
#ifdef QAT_UIO
	case IOCTL_RESET_ACCEL_DEV:
		ret = adf_ctl_ioctl_dev_reset(arg);
		break;
	case IOCTL_GET_CFG_VAL:
		ret = adf_ctl_ioctl_dev_get_value(arg);
		break;
	case IOCTL_RESERVE_RING:
		ret = adf_ctl_ioctl_reserve_ring(arg);
		break;
	case IOCTL_RELEASE_RING:
		ret = adf_ctl_ioctl_release_ring(arg);
		break;
	case IOCTL_ENABLE_RING:
		ret = adf_ctl_ioctl_enable_ring(arg);
		break;
	case IOCTL_DISABLE_RING:
		ret = adf_ctl_ioctl_disable_ring(arg);
		break;
	case IOCTL_SLA_GET_CAPS:
		ret = adf_ctl_ioctl_sla_get_caps(arg);
		break;
	case IOCTL_SLA_CREATE:
		ret = adf_ctl_ioctl_sla_create(arg);
		break;
	case IOCTL_SLA_CREATE_V2:
		ret = adf_ctl_ioctl_sla_create_rl_v2(arg, false);
		break;
	case IOCTL_SLA_UPDATE:
		ret = adf_ctl_ioctl_sla_update(arg);
		break;
	case IOCTL_SLA_UPDATE_V2:
		ret = adf_ctl_ioctl_sla_update_rl_v2(arg, false);
		break;
	case IOCTL_SLA_DELETE:
		ret = adf_ctl_ioctl_sla_delete(arg);
		break;
	case IOCTL_SLA_GET_LIST:
		ret = adf_ctl_ioctl_sla_get_list(arg);
		break;
	case IOCTL_GET_DEV_REAL_ID:
		ret = adf_ctl_ioctl_get_real_id(arg);
		break;
#endif
	default:
		pr_err("QAT: Invalid ioctl\n");
		ret = -EFAULT;
		break;
	}
	mutex_unlock(&adf_ctl_lock);
	return ret;
}

static int __init adf_register_ctl_device_driver(void)
{
	mutex_init(&adf_ctl_lock);

	if (adf_chr_drv_create())
		goto err_chr_dev;

	if (adf_init_aer())
		goto err_aer;

	if (adf_init_fatal_error_wq())
		goto err_event_wq;

	adf_init_svm();

#ifdef CONFIG_CRYPTO_DEV_QAT_VDCM
	if (adf_vdcm_init())
		goto err_adf_vdcm_init;
#endif

	if (qat_crypto_register())
		goto err_crypto_register;

	adf_cfg_depot_init();

#ifdef QAT_UIO
	if (adf_processes_dev_register())
		goto err_processes_dev_register;
	if (qdm_init())
		goto err_qdm_init;
	if (adf_uio_service_register())
		goto err_adf_service_register;

	return 0;

err_adf_service_register:
err_qdm_init:
	adf_processes_dev_unregister();
err_processes_dev_register:
	qat_crypto_unregister();
#else
	return 0;
#endif

err_crypto_register:
#ifdef CONFIG_CRYPTO_DEV_QAT_VDCM
	adf_vdcm_cleanup();
err_adf_vdcm_init:
#endif
	adf_exit_svm();
	adf_exit_fatal_error_wq();
err_event_wq:
	adf_exit_aer();
err_aer:
	adf_chr_drv_destroy();
err_chr_dev:
	mutex_destroy(&adf_ctl_lock);
	return -EFAULT;
}

static void __exit adf_unregister_ctl_device_driver(void)
{
#ifdef QAT_UIO
	adf_uio_service_unregister();
	qdm_exit();
	adf_processes_dev_unregister();
#endif
	adf_exit_vf_wq();
	adf_exit_vqat_wq();
	adf_chr_drv_destroy();
	adf_exit_aer();
#ifdef CONFIG_CRYPTO_DEV_QAT_VDCM
	adf_vdcm_cleanup();
#endif
	adf_exit_svm();
	adf_exit_fatal_error_wq();
	qat_crypto_unregister();
	adf_clean_vf_map(false);
	mutex_destroy(&adf_ctl_lock);
}

module_init(adf_register_ctl_device_driver);
module_exit(adf_unregister_ctl_device_driver);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Intel");
MODULE_DESCRIPTION("Intel(R) QuickAssist Technology");
MODULE_ALIAS_CRYPTO("intel_qat");
MODULE_VERSION(ADF_DRV_VERSION);
