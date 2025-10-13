// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2021 Intel Corporation */
#include <linux/module.h>
#include <linux/slab.h>
#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "adf_transport.h"
#include "adf_transport_access_macros.h"
#include "adf_cfg.h"
#include "adf_cfg_bundle.h"
#include "adf_cfg_strings.h"
#include "qat_crypto.h"
#include "icp_qat_fw.h"

static struct service_hndl qat_crypto;

void qat_crypto_put_instance(struct qat_crypto_instance *inst)
{
	atomic_dec(&inst->refctr);
	adf_dev_put(inst->accel_dev);
}
EXPORT_SYMBOL_GPL(qat_crypto_put_instance);

static int qat_crypto_free_instances(struct adf_accel_dev *accel_dev)
{
	struct qat_crypto_instance *inst = NULL, *tmp = NULL;
	int i;

	list_for_each_entry_safe(inst, tmp, &accel_dev->crypto_list, list) {
		for (i = 0; i < atomic_read(&inst->refctr); i++)
			qat_crypto_put_instance(inst);

		if (inst->sym_tx)
			adf_remove_ring(inst->sym_tx);

		if (inst->sym_rx)
			adf_remove_ring(inst->sym_rx);

		if (inst->pke_tx)
			adf_remove_ring(inst->pke_tx);

		if (inst->pke_rx)
			adf_remove_ring(inst->pke_rx);

		list_del(&inst->list);
		kfree(inst);
	}
	return 0;
}

static bool qat_crypto_check_service(struct adf_accel_dev *accel_dev,
				     u8 service_type)
{
	u32 cap_mask = accel_dev->hw_device->accel_capabilities_mask;

	if ((service_type == ASYM &&
	     (ADF_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC & cap_mask)) ||
	    (service_type == SYM &&
	     (ADF_ACCEL_CAPABILITIES_CRYPTO_SYMMETRIC & cap_mask)))
		return true;
	return false;
}

struct qat_crypto_instance *qat_crypto_get_instance_node(int node,
							 u8 service_type)
{
	struct adf_accel_dev *accel_dev = NULL, *tmp_dev = NULL;
	struct qat_crypto_instance *inst = NULL, *tmp_inst;
	unsigned long best = ~0;

	list_for_each_entry(tmp_dev, adf_devmgr_get_head(), list) {
		unsigned long ctr;

		if ((node == dev_to_node(&GET_DEV(tmp_dev)) ||
		     dev_to_node(&GET_DEV(tmp_dev)) < 0) &&
		    adf_dev_started(tmp_dev) &&
		    !list_empty(&tmp_dev->crypto_list)) {
			if (qat_crypto_check_service(tmp_dev, service_type)) {
				ctr = atomic_read(&tmp_dev->ref_count);
				if (best > ctr) {
					accel_dev = tmp_dev;
					best = ctr;
				}
			}
		}
	}

	if (!accel_dev) {
		pr_info("QAT: Could not find a device on node %d\n", node);
		/* Get any started device */
		list_for_each_entry(tmp_dev, adf_devmgr_get_head(), list) {
			if (adf_dev_started(tmp_dev) &&
			    !list_empty(&tmp_dev->crypto_list)) {
				if (qat_crypto_check_service(tmp_dev,
							     service_type)) {
					accel_dev = tmp_dev;
					break;
				}
			}
		}
	}

	if (!accel_dev)
		return NULL;

	best = ~0;
	list_for_each_entry(tmp_inst, &accel_dev->crypto_list, list) {
		if ((service_type == ASYM && tmp_inst->pke_tx) ||
		    (service_type == SYM && tmp_inst->sym_tx)) {
			unsigned long ctr;

			ctr = atomic_read(&tmp_inst->refctr);
			if (best > ctr) {
				inst = tmp_inst;
				best = ctr;
			}
		}
	}
	if (inst) {
		if (adf_dev_get(accel_dev)) {
			dev_err(&GET_DEV(accel_dev), "Could not increment dev refctr\n");
			return NULL;
		}
		atomic_inc(&inst->refctr);
	}
	return inst;
}
EXPORT_SYMBOL_GPL(qat_crypto_get_instance_node);

static int qat_asym_configure(struct adf_accel_dev *accel_dev, int inst,
			      unsigned long asym_tx_ring)
{
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	unsigned long val;
	unsigned long tx_rx_offset = accel_dev->hw_device->tx_rx_gap;

	snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_ASYM_SIZE, inst);
	val = ADF_DEFAULT_ASYM_RING_SIZE;
	if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
					key, (void *)&val, ADF_DEC))
		goto err;

	val = asym_tx_ring;
	snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_ASYM_TX, inst);
	if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
					key, (void *)&val, ADF_DEC))
		goto err;

	val += tx_rx_offset;
	snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_ASYM_RX, inst);
	if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
					key, (void *)&val, ADF_DEC))
		goto err;

	return 0;
err:
	dev_err(&GET_DEV(accel_dev), "Failed to configure asym service\n");
	return -EINVAL;
}

static int qat_sym_configure(struct adf_accel_dev *accel_dev, int inst,
			     unsigned long sym_tx_ring)
{
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	unsigned long val;
	unsigned long tx_rx_offset = accel_dev->hw_device->tx_rx_gap;

	snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_SYM_SIZE, inst);
	val = ADF_DEFAULT_SYM_RING_SIZE;
	if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
					key, (void *)&val, ADF_DEC))
		goto err;

	val = sym_tx_ring;
	snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_SYM_TX, inst);
	if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
					key, (void *)&val, ADF_DEC))
		goto err;

	val += tx_rx_offset;
	snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_SYM_RX, inst);
	if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
					key, (void *)&val, ADF_DEC))
		goto err;

	return 0;
err:
dev_err(&GET_DEV(accel_dev), "Failed to configure sym service\n");
	return -EINVAL;
}

/**
 * qat_crypto_vf_dev_config()
 *     create dev config required to create crypto inst.
 *
 * @accel_dev: Pointer to acceleration device.
 *
 * Function creates device configuration required to create
 * asym, sym or, crypto instances
 *
 * Return: 0 on success, error code otherwise.
 */
int qat_crypto_vf_dev_config(struct adf_accel_dev *accel_dev)
{
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	unsigned long val;
	int banks = GET_MAX_BANKS(accel_dev);
	int cpus = num_online_cpus();
	int instances = min(cpus, banks);
	int ring_pair_index = 0;
	u8 serv_type = 0;
	int num_rings_per_srv = 0;
	int tx_ring = 0;
	int inst = 0;
	int num_serv_per_vf = 0;
	unsigned long num_cy = 0;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct pci_dev *pdev = accel_to_pci_dev(accel_dev);

	if (hw_data->num_rings_per_bank >= (2 * ADF_CFG_NUM_SERVICES))
		num_serv_per_vf = ADF_CFG_NUM_SERVICES;
	else
		num_serv_per_vf = 1;

	if (adf_cfg_section_add(accel_dev, ADF_KERNEL_SEC))
		goto err;
	if (adf_cfg_section_add(accel_dev, "Accelerator0"))
		goto err;
	for (inst = 0; inst < instances; inst++) {
		val = inst;
		if (!IS_QAT_GEN4(pdev->device)) {
			snprintf(key, sizeof(key),
				 ADF_CY "%d" ADF_RING_BANK_NUM, inst);
			if (adf_cfg_add_key_value_param(accel_dev,
							ADF_KERNEL_SEC,
							key, (void *)&val,
							ADF_DEC))
				goto err;
		}

		snprintf(key, sizeof(key),
			 ADF_CY "%d" ADF_ETRMGR_CORE_AFFINITY, inst);
		if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						key, (void *)&val, ADF_DEC))
			goto err;

		val = hw_data->coalescing_def_time;
		snprintf(key, sizeof(key),
			 ADF_ETRMGR_COALESCE_TIMER_FORMAT, inst);
		if (adf_cfg_add_key_value_param(accel_dev, "Accelerator0",
						key, (void *)&val, ADF_DEC))
			goto err;

		for (ring_pair_index = 0;
			ring_pair_index < num_serv_per_vf;
			ring_pair_index++) {
			adf_get_ring_svc_map_data(hw_data, inst,
						  ring_pair_index, &serv_type,
						  &tx_ring,
						  &num_rings_per_srv);

			switch (serv_type) {
			case ASYM:
				if (IS_QAT_GEN4(pdev->device)) {
					val = inst;
					snprintf(key, sizeof(key),
						 ADF_CY_ASYM_BANK_NUM_FORMAT,
						 inst);
					if (adf_cfg_add_key_value_param
						(accel_dev, ADF_KERNEL_SEC,
						 key, (void *)&val, ADF_DEC))
						goto err;
				}
				if (qat_asym_configure(accel_dev, inst,
						       tx_ring))
					goto err;
				num_cy++;
			break;
			case SYM:
				if (IS_QAT_GEN4(pdev->device)) {
					val = inst;
					snprintf(key, sizeof(key),
						 ADF_CY_SYM_BANK_NUM_FORMAT,
						 inst);
					if (adf_cfg_add_key_value_param
						(accel_dev, ADF_KERNEL_SEC,
						 key, (void *)&val, ADF_DEC))
						goto err;
				}
				if (qat_sym_configure(accel_dev, inst,
						      tx_ring))
					goto err;
				num_cy++;
			break;
			case CRYPTO:
				if (qat_asym_configure(accel_dev, inst,
						       tx_ring))
					goto err;
				num_cy++;
				ring_pair_index++;
				if (ring_pair_index == num_serv_per_vf)
					break;

				tx_ring = ring_pair_index * num_rings_per_srv;
				if (qat_sym_configure(accel_dev, inst,
						      tx_ring))
					goto err;
				num_cy++;
			break;
			default:
				continue;
			}
			break;
		}
	}
	/* Set NumberCyInstances to 0 if there are no CY services */
	if (num_cy == 0)
		val = 0;
	else
		val = inst;

	if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
					ADF_NUM_CY, (void *)&val, ADF_DEC))
		goto err;

	set_bit(ADF_STATUS_CONFIGURED, &accel_dev->status);
	return 0;
err:
	dev_err(&GET_DEV(accel_dev), "Failed to configure QAT accel dev\n");
	return -EINVAL;
}

/**
 * qat_crypto_dev_config() - create dev config required to create crypto inst.
 *
 * @accel_dev: Pointer to acceleration device.
 *
 * Function creates device configuration required to create crypto instances
 *
 * Return: 0 on success, error code otherwise.
 */
int qat_crypto_dev_config(struct adf_accel_dev *accel_dev)
{
	int cpus = num_online_cpus();
	int banks = GET_MAX_BANKS(accel_dev);
	int instances = min(cpus, banks);
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	int i;
	unsigned long val;
	unsigned long tx_rx_offset = accel_dev->hw_device->tx_rx_gap;
	u32 capabilities;

	if (adf_cfg_section_add(accel_dev, ADF_KERNEL_SEC))
		goto err;
	if (adf_cfg_section_add(accel_dev, "Accelerator0"))
		goto err;

	capabilities = GET_HW_DATA(accel_dev)->accel_capabilities_mask;
	if (!((capabilities & ADF_ACCEL_CAPABILITIES_CRYPTO_SYMMETRIC) &&
	      (capabilities & ADF_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC)))
		instances = 0;

	for (i = 0; i < instances; i++) {
		val = i;
		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_BANK_NUM, i);
		if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						key, (void *)&val, ADF_DEC))
			goto err;

		snprintf(key, sizeof(key), ADF_CY "%d" ADF_ETRMGR_CORE_AFFINITY,
			 i);
		if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						key, (void *)&val, ADF_DEC))
			goto err;

		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_ASYM_SIZE, i);
		val = ADF_DEFAULT_ASYM_RING_SIZE;
		if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						key, (void *)&val, ADF_DEC))
			goto err;

		val = ADF_DEFAULT_SYM_RING_SIZE;
		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_SYM_SIZE, i);
		if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						key, (void *)&val, ADF_DEC))
			goto err;

		val = 0;
		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_ASYM_TX, i);
		if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						key, (void *)&val, ADF_DEC))
			goto err;

		val = tx_rx_offset;
		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_ASYM_RX, i);
		if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						key, (void *)&val, ADF_DEC))
			goto err;

		val = tx_rx_offset / ADF_ARB_NUM;
		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_SYM_TX, i);
		if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						key, (void *)&val, ADF_DEC))
			goto err;

		val += tx_rx_offset;
		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_SYM_RX, i);
		if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						key, (void *)&val, ADF_DEC))
			goto err;

		val = accel_dev->hw_device->coalescing_def_time;
		snprintf(key, sizeof(key), ADF_ETRMGR_COALESCE_TIMER_FORMAT, i);
		if (adf_cfg_add_key_value_param(accel_dev, "Accelerator0",
						key, (void *)&val, ADF_DEC))
			goto err;
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
EXPORT_SYMBOL_GPL(qat_crypto_dev_config);

static int qat_crypto_create_asym_rings(struct adf_accel_dev *accel_dev,
					struct qat_crypto_instance *inst,
					int instance_num)
{
	int i = instance_num;
	unsigned long bank;
	unsigned long num_msg_asym;
	int msg_size;
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	char val[ADF_CFG_MAX_VAL_LEN_IN_BYTES];

	if (adf_cy_inst_cross_banks(accel_dev)) {
		snprintf(key, sizeof(key), ADF_CY_ASYM_BANK_NUM_FORMAT, i);
		if (adf_cfg_get_param_value(accel_dev, SEC, key, val))
			return 0;
	} else {
		snprintf(key, sizeof(key), ADF_CY_BANK_NUM_FORMAT, i);
		if (adf_cfg_get_param_value(accel_dev, SEC, key, val))
			goto err;
	}
	if (kstrtoul(val, 10, &bank))
		goto err;

	snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_ASYM_SIZE, i);
	if (adf_cfg_get_param_value(accel_dev, SEC, key, val))
		goto err;
	if (kstrtoul(val, 10, &num_msg_asym))
		goto err;
	num_msg_asym = num_msg_asym >> 1;
	msg_size = ICP_QAT_FW_REQ_DEFAULT_SZ >> 1;
	snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_ASYM_TX, i);
	if (adf_create_ring(accel_dev, SEC, bank, num_msg_asym,
			    msg_size, key, NULL, 0, &inst->pke_tx))
		goto err;

	msg_size = ICP_QAT_FW_RESP_DEFAULT_SZ;
	snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_ASYM_RX, i);
	if (adf_create_ring(accel_dev, SEC, bank, num_msg_asym,
			    msg_size, key, qat_alg_asym_callback, 0,
			    &inst->pke_rx))
		goto err;
	return 0;
err:
	dev_err(&GET_DEV(accel_dev), "Failed to create asym rings\n");
	qat_crypto_free_instances(accel_dev);
	return -ENOMEM;
}

static int qat_crypto_create_sym_rings(struct adf_accel_dev *accel_dev,
				       struct qat_crypto_instance *inst,
				       int instance_num)
{
	int i = instance_num;
	unsigned long bank;
	unsigned long num_msg_sym;
	int msg_size;
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	char val[ADF_CFG_MAX_VAL_LEN_IN_BYTES];

	if (adf_cy_inst_cross_banks(accel_dev)) {
		snprintf(key, sizeof(key), ADF_CY_SYM_BANK_NUM_FORMAT, i);
		if (adf_cfg_get_param_value(accel_dev, SEC, key, val))
			return 0;
	} else {
		snprintf(key, sizeof(key), ADF_CY_BANK_NUM_FORMAT, i);
		if (adf_cfg_get_param_value(accel_dev, SEC, key, val))
			goto err;
	}
	if (kstrtoul(val, 10, &bank))
		goto err;

	snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_SYM_SIZE, i);
	if (adf_cfg_get_param_value(accel_dev, SEC, key, val))
		goto err;

	if (kstrtoul(val, 10, &num_msg_sym))
		goto err;
	num_msg_sym = num_msg_sym >> 1;

	msg_size = ICP_QAT_FW_REQ_DEFAULT_SZ;
	snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_SYM_TX, i);
	if (adf_create_ring(accel_dev, SEC, bank, num_msg_sym,
			    msg_size, key, NULL, 0, &inst->sym_tx))
		goto err;

	msg_size = ICP_QAT_FW_RESP_DEFAULT_SZ;
	snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_SYM_RX, i);
	if (adf_create_ring(accel_dev, SEC, bank, num_msg_sym,
			    msg_size, key, qat_alg_callback, 0,
			    &inst->sym_rx))
		goto err;
	return 0;
err:
	dev_err(&GET_DEV(accel_dev), "Failed to create sym rings\n");
	qat_crypto_free_instances(accel_dev);
	return -ENOMEM;
}

static int qat_crypto_create_instances(struct adf_accel_dev *accel_dev)
{
	int i;
	unsigned long num_inst;
	struct qat_crypto_instance *inst;
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	char val[ADF_CFG_MAX_VAL_LEN_IN_BYTES];
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;

	INIT_LIST_HEAD(&accel_dev->crypto_list);
	strscpy(key, ADF_NUM_CY, sizeof(key));
	if (adf_cfg_get_param_value(accel_dev, SEC, key, val))
		return -EFAULT;

	if (kstrtoul(val, 0, &num_inst))
		return -EFAULT;

	for (i = 0; i < num_inst; i++) {
		inst = kzalloc_node(sizeof(*inst), GFP_KERNEL,
				    dev_to_node(&GET_DEV(accel_dev)));
		if (!inst)
			goto err;

		list_add_tail(&inst->list, &accel_dev->crypto_list);
		inst->id = i;
		atomic_set(&inst->refctr, 0);
		inst->accel_dev = accel_dev;

		if (hw_data->accel_capabilities_mask &
		    ADF_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC) {
			if (qat_crypto_create_asym_rings(accel_dev,
							 inst, i))
				goto err;
		}
		if (hw_data->accel_capabilities_mask &
		    ADF_ACCEL_CAPABILITIES_CRYPTO_SYMMETRIC) {
			if (qat_crypto_create_sym_rings(accel_dev,
							inst, i))
				goto err;
		}
	}
	return 0;
err:
	qat_crypto_free_instances(accel_dev);
	return -ENOMEM;
}

static int qat_crypto_init(struct adf_accel_dev *accel_dev)
{
	if (qat_crypto_create_instances(accel_dev))
		return -EFAULT;

	return 0;
}

static int qat_crypto_shutdown(struct adf_accel_dev *accel_dev)
{
	return qat_crypto_free_instances(accel_dev);
}

static int qat_crypto_event_handler(struct adf_accel_dev *accel_dev,
				    enum adf_event event)
{
	int ret;

	switch (event) {
	case ADF_EVENT_INIT:
		ret = qat_crypto_init(accel_dev);
		break;
	case ADF_EVENT_SHUTDOWN:
		ret = qat_crypto_shutdown(accel_dev);
		break;
	case ADF_EVENT_RESTARTING:
	case ADF_EVENT_RESTARTED:
	case ADF_EVENT_START:
	case ADF_EVENT_STOP:
	case ADF_EVENT_ERROR:
	default:
		ret = 0;
	}
	return ret;
}

int qat_crypto_register(void)
{
	memset(&qat_crypto, 0, sizeof(qat_crypto));
	qat_crypto.event_hld = qat_crypto_event_handler;
	qat_crypto.name = "qat_crypto";
	return adf_service_register(&qat_crypto);
}

int qat_crypto_unregister(void)
{
	return adf_service_unregister(&qat_crypto);
}
