/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2021 - 2022 Intel Corporation */
#include <linux/uaccess.h>
#include "adf_common_drv.h"
#include "adf_sla.h"
#include "adf_ctl_rl.h"
#include "adf_rl_v2.h"

/*
 * get_accel_check_support - check if RL V2 is supported
 *
 * Function receives addel_dev and pci_address
 *
 * Return: 0 on success, error code otherwise.
 */
static int get_accel_check_support(struct adf_accel_dev **accel_dev_ret,
				   struct adf_pci_address *pci_addr)
{
	struct adf_accel_dev *accel_dev = NULL;

	accel_dev = adf_devmgr_get_dev_by_pci_domain_bus(pci_addr);
	if (!accel_dev) {
		pr_err("QAT: Device dbdf:%.4x:%.2x:%.2x.%x not found\n",
		       pci_addr->domain_nr, pci_addr->bus, pci_addr->dev,
		       pci_addr->func);
		return -ENODEV;
	}

	if (!accel_dev->rl_v2) {
		adf_dev_put(accel_dev);
		return -ENOTSUPP;
	}

	*accel_dev_ret = accel_dev;

	return 0;
}

/*
 * adf_ctl_sla_get_caps_rl_v2 - get returned capabilities
 *
 * Function receives the user input as argument
 *
 * Return: 0 on success, error code otherwise.
 */
static int adf_ctl_sla_get_caps_rl_v2(struct adf_user_sla_caps *sla_caps)
{
	int ret = 0;
	struct adf_accel_dev *accel_dev = NULL;

	ret = get_accel_check_support(&accel_dev, &sla_caps->pf_addr);
	if (ret == 0) {
		rl_v2_get_caps(accel_dev, sla_caps);
		adf_dev_put(accel_dev);
	}

	return ret;
}

/*
 * adf_ctl_sla_get_list_rl_v2 - get returned slas
 *
 * Function receives the user input as argument
 *
 * Return: 0 on success, error code otherwise.
 */
static int adf_ctl_sla_get_list_rl_v2(struct adf_user_slas *slas)
{
	int ret = 0;
	struct adf_accel_dev *accel_dev = NULL;

	ret = get_accel_check_support(&accel_dev, &slas->pf_addr);
	if (ret == 0) {
		/* Check user passed a physical function */
		if (!(slas->pf_addr.dev == 0 && slas->pf_addr.func == 0)) {
			dev_err(&GET_DEV(accel_dev),
				"Rate Limiting: For list must use PF address\n");
			adf_dev_put(accel_dev);
			return -EINVAL;
		}
		rl_v2_get_user_slas(accel_dev, slas);
		adf_dev_put(accel_dev);
	}

	return ret;
}

/*
 * adf_ctl_ioctl_sla_create_rl_v2 - IOCTL to get create an SLA
 *
 * Function receives user input as argument
 *
 * Return: 0 on success, error code otherwise.
 */
int adf_ctl_ioctl_sla_create_rl_v2(unsigned long arg, bool compat)
{
	int ret = 0;
	struct adf_user_sla sla = { 0 };
	struct adf_accel_dev *accel_dev = NULL;

	ret = copy_from_user(&sla, (void __user *)arg,
			     sizeof(struct adf_user_sla));
	if (ret) {
		pr_err("QAT: Failed to copy node info from user\n");
		return ret;
	}

	ret = get_accel_check_support(&accel_dev, &sla.pci_addr);
	if (ret == 0) {
		/* Check user passed a virtual function */
		if (sla.pci_addr.dev == 0 && sla.pci_addr.func == 0) {
			dev_err(&GET_DEV(accel_dev),
				"Rate Limiting: For create must use VF address\n");
			adf_dev_put(accel_dev);
			return -EINVAL;
		}

		if (!rl_is_svc_enabled(accel_dev, sla.svc_type)) {
			dev_err(&GET_DEV(accel_dev),
				"Rate Limiting: Service type not supported\n");
			adf_dev_put(accel_dev);
			return -EFAULT;
		}

		/* We enforce that for this API we can only config leaf SLAs */
		sla.nodetype = ADF_NODE_LEAF;
		if (rl_v2_set_node_id(accel_dev, &sla)) {
			dev_err(&GET_DEV(accel_dev),
				"Rate Limiting: User input out of range\n");
			adf_dev_put(accel_dev);
			return -EINVAL;
		}

		if (compat) {
			sla.cir = sla.rate_in_slau;
			sla.pir = sla.rate_in_slau;
		}

		ret = rl_v2_create_sla(accel_dev, &sla);
		if (!ret) {
			ret = copy_to_user((void __user *)arg, &sla,
					   sizeof(struct adf_user_sla));
			if (ret)
				dev_err(&GET_DEV(accel_dev),
					"Rate Limiting: Failed to copy sla to user space\n");
		}
		adf_dev_put(accel_dev);
	}

	return ret;
}

/*
 * adf_ctl_ioctl_sla_update_rl_v2 - IOCTL to update an SLA
 *
 * Function receives user input as argument
 *
 * Return: 0 on success, error code otherwise.
 */
int adf_ctl_ioctl_sla_update_rl_v2(unsigned long arg, bool compat)
{
	int ret = 0;
	struct adf_user_sla sla = { 0 };
	struct adf_accel_dev *accel_dev = NULL;

	ret = copy_from_user(&sla, (void __user *)arg,
			     sizeof(struct adf_user_sla));
	if (ret) {
		pr_err("QAT: Failed to copy node info from user.\n");
		return ret;
	}

	ret = get_accel_check_support(&accel_dev, &sla.pci_addr);
	if (ret == 0) {
		if (compat) {
			sla.cir = sla.rate_in_slau;
			sla.pir = sla.rate_in_slau;
		}
		ret = rl_v2_update_sla(accel_dev, &sla);
		adf_dev_put(accel_dev);
	}

	return ret;
}

/*
 * adf_ctl_ioctl_sla_delete_rl_v2 - IOCTL to delete an SLA
 *
 * Function receives user input as argument
 *
 * Return: 0 on success, error code otherwise.
 */
static int adf_ctl_ioctl_sla_delete_rl_v2(unsigned long arg)
{
	int ret = 0;
	struct adf_user_sla sla = { 0 };
	struct adf_accel_dev *accel_dev = NULL;

	ret = copy_from_user(&sla, (void __user *)arg,
			     sizeof(struct adf_user_sla));
	if (ret) {
		pr_err("QAT: Failed to copy node info from user.\n");
		return ret;
	}

	ret = get_accel_check_support(&accel_dev, &sla.pci_addr);
	if (ret == 0) {
		ret = rl_v2_delete_sla(accel_dev, &sla);
		adf_dev_put(accel_dev);
	}

	return ret;
}

/*
 * adf_ctl_ioctl_sla_create - IOCTL to create the SLA
 *
 * Function receives the user input as argument and creates the SLA
 *
 * Return: 0 on success, error code otherwise.
 */
int adf_ctl_ioctl_sla_create(unsigned long arg)
{
	int ret = 0;
	struct adf_user_sla sla;
	void *sla_id_ptr = NULL;

	/* Check for RL_V2 support */
	ret = adf_ctl_ioctl_sla_create_rl_v2(arg, true);
	if (ret == -ENOTSUPP) {
		/* RL_V1 */
		if (copy_from_user(&sla, (void __user *)arg, sizeof(sla))) {
			pr_err("QAT: Failed to copy sla create info from user.\n");
			return -EFAULT;
		}

		/* We will always return -1 as RL_V1 is currently un-supported */
		ret = adf_sla_create(&sla);
		if (ret)
			return ret;

		sla_id_ptr = &((struct adf_user_sla *)arg)->sla_id;
		return put_user(sla.sla_id, (u16 __user *)sla_id_ptr);
	}

	return ret;
}

/*
 * adf_ctl_ioctl_sla_update - IOCTL to update the specific SLA
 *
 * Function receives the user input as argument and updates the SLA
 * based on sla id
 *
 * Return: 0 on success, error code otherwise.
 */
int adf_ctl_ioctl_sla_update(unsigned long arg)
{
	int ret = 0;
	struct adf_user_sla sla;

	/* Check for RL_V2 support */
	ret = adf_ctl_ioctl_sla_update_rl_v2(arg, true);
	if (ret == -ENOTSUPP) {
		/* RL_V1 */
		if (copy_from_user(&sla, (void __user *)arg, sizeof(sla))) {
			pr_err("QAT: Failed to copy sla update info from user.\n");
			return -EFAULT;
		}
		return adf_sla_update(&sla);
	}

	return ret;
}

/*
 * adf_ctl_ioctl_sla_delete - IOCTL to delete the specific SLA
 *
 * Function receives the user input as argument and deletes the SLA
 * based on sla id
 *
 * Return: 0 on success, error code otherwise.
 */
int adf_ctl_ioctl_sla_delete(unsigned long arg)
{
	int ret = 0;
	struct adf_user_sla sla;

	/* Check for RL_V2 support */
	ret = adf_ctl_ioctl_sla_delete_rl_v2(arg);
	if (ret == -ENOTSUPP) {
		/* RL_V1 */
		if (copy_from_user(&sla, (void __user *)arg, sizeof(sla))) {
			pr_err("QAT: Failed to copy sla delete info from user.\n");
			return -EFAULT;
		}
		return adf_sla_delete(&sla);
	}

	return ret;
}

/*
 * adf_ctl_ioctl_sla_get_caps - IOCTL to get the capabilities of SLA
 *
 * Function receives the user input as argument and get the capability
 * information which is supported on the specific device
 *
 * Return: 0 on success, error code otherwise.
 */
int adf_ctl_ioctl_sla_get_caps(unsigned long arg)
{
	struct adf_user_sla_caps sla_caps;
	int ret = -EFAULT;

	if (copy_from_user(&sla_caps, (void __user *)arg, sizeof(sla_caps))) {
		pr_err("QAT: Failed to copy sla caps info from user.\n");
		return ret;
	}

	/* Check for RL_V2 support */
	ret = adf_ctl_sla_get_caps_rl_v2(&sla_caps);
	if (ret == -ENOTSUPP)
		/* RL_V1 */
		ret = adf_sla_get_caps(&sla_caps);

	if (ret)
		return ret;

	ret = copy_to_user((void __user *)arg, &sla_caps, sizeof(sla_caps));
	if (ret) {
		pr_err("Failed to copy qat sla capabilities to user.\n");
		return ret;
	}

	return ret;
}

/*
 * adf_ctl_ioctl_sla_get_list - IOCTL to list the SLA created
 *
 * Function receives the user input as argument and lists the SLAs
 * which are created successfully
 *
 * Return: 0 on success, error code otherwise.
 */
int adf_ctl_ioctl_sla_get_list(unsigned long arg)
{
	struct adf_user_slas *slas;
	int ret = -EFAULT;

	slas = kzalloc(sizeof(struct adf_user_slas), GFP_KERNEL);
	if (!slas)
		return -ENOMEM;

	if (copy_from_user(slas, (void __user *)arg, sizeof(struct adf_user_slas))) {
		pr_err("QAT: Failed to copy sla get list info from user.\n");
		goto cleanup;
	}

	/* Check for RL_V2 support */
	ret = adf_ctl_sla_get_list_rl_v2(slas);
	if (ret == -ENOTSUPP)
		/* RL_V1 */
		ret = adf_sla_get_list(slas);

	if (ret)
		goto cleanup;

	/* Copy the information from adf_user_info to user space */
	ret = copy_to_user((void __user *)arg, slas, sizeof(struct adf_user_slas));
	if (ret) {
		pr_err("QAT: Failed to copy slas\n");
	}

cleanup:
	kfree(slas);

	return ret;
}
