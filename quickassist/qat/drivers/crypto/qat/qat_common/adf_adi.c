// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2019 - 2021 Intel Corporation */

#include "adf_accel_devices.h"
#include "adf_cfg.h"
#include "adf_cfg_strings.h"
#include "adf_transport.h"
#include "adf_transport_internal.h"
#include "adf_adi.h"
#include "icp_qat_fw.h"

#define DEFAULT_ADI_RING_MSG_NUM 256

static inline void adf_init_adi_ops(struct adf_accel_dev *accel_dev,
				    struct adf_adi_ep *adi)
{
	adi->adi_ops = accel_dev->hw_device->adi_ops;
}

static void adf_destroy_adi(struct adf_accel_dev *accel_dev,
			    struct adf_adi_ep *adi)
{
	dev_dbg(&GET_DEV(accel_dev), "Destroy ADI#%d on bank %d.\n",
		adi->adi_idx, adi->bank_idx);

	if (adi->adi_ops && adi->adi_ops->destroy)
		adi->adi_ops->destroy(adi);

	if (adi->status == ADI_STATUS_ACTIVE)
		adf_adi_free(adi);

	adi->status = ADI_STATUS_INVALID;
	mutex_destroy(&adi->lock);
}

static inline int adf_adi_type_str_to_int(const char *type)
{
	if (!type)
		return ADI_TYPE_INVALID;

	if (!strncmp(type, "asym", strlen("asym")))
		return ADI_TYPE_ASYM;
	else if (!strncmp(type, "sym", strlen("sym")))
		return ADI_TYPE_SYM;
	else if (!strncmp(type, "dc", strlen("dc")))
		return ADI_TYPE_COMP;
	else
		return ADI_TYPE_INVALID;
}

static int adf_create_adi(struct adf_accel_dev *accel_dev,
			  struct adf_adi_ep *adi,
			  s32 adi_idx)
{
	unsigned int bank_num = 0;
	unsigned int tx_idx = 0;
	unsigned int rx_idx = 0;
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES] = {0};
	char val[ADF_CFG_MAX_VAL_LEN_IN_BYTES] = {0};

	if (!accel_dev || !adi)
		return -EINVAL;

	if (adi_idx < 0 ||
	    adi_idx >= accel_dev->adi_info->adi_num)
		return -EINVAL;

	/* Get info from cfg for associated bank and tx/rx etr */
	snprintf(key, sizeof(key),
		 ADF_ADI "%d" ADF_RING_BANK_NUM, adi_idx);
	if (adf_cfg_get_param_value(accel_dev, ADF_SIOV_SEC,
				    key, val))
		return -EINVAL;

	if (kstrtouint(val, 10, &bank_num))
		return -EINVAL;

	snprintf(key, sizeof(key),
		 ADF_ADI_RING_TX_FORMAT, adi_idx);
	if (adf_cfg_get_param_value(accel_dev, ADF_SIOV_SEC,
				    key, val))
		return -EINVAL;

	if (kstrtouint(val, 10, &tx_idx))
		return -EINVAL;

	snprintf(key, sizeof(key),
		 ADF_ADI_RING_RX_FORMAT, adi_idx);
	if (adf_cfg_get_param_value(accel_dev, ADF_SIOV_SEC,
				    key, val))
		return -EINVAL;

	if (kstrtouint(val, 10, &rx_idx))
		return -EINVAL;

	snprintf(key, sizeof(key),
		 ADF_ADI_TYPE_FORMAT, adi_idx);
	if (adf_cfg_get_param_value(accel_dev, ADF_SIOV_SEC,
				    key, val))
		return -EINVAL;

	adi->adi_idx = adi_idx;
	adi->bank_idx = bank_num;
	adi->tx_idx = tx_idx;
	adi->rx_idx = rx_idx;
	adi->parent = accel_dev;
	snprintf(adi->name, sizeof(adi->name),
		 ADF_ADI "%d", adi_idx);

	adi->type = adf_adi_type_str_to_int(val);
	adf_init_adi_ops(accel_dev, adi);
	if (adi->adi_ops->init)
		if (adi->adi_ops->init(adi)) {
			dev_err(&GET_DEV(accel_dev), "Failed to init ADI HW on bank %d.\n",
				(int)bank_num);
			return -EFAULT;
		}

	mutex_init(&adi->lock);
	adi->status = ADI_STATUS_IDLE;
	adi->reset_complete = true;

	dev_dbg(&GET_DEV(accel_dev),
		"ADI#%d (%s) created from Bank %d, tx = %d, rx = %d, stype = %d.\n",
		adi_idx, val, (int)bank_num, (int)tx_idx,
		(int)rx_idx, adi->type);

	return 0;
}

int adf_init_adis(struct adf_accel_dev *accel_dev)
{
	int i = 0;
	int adi_num = 0;
	struct adf_adi_info *adi_info = NULL;
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES] = {0};
	char val[ADF_CFG_MAX_VAL_LEN_IN_BYTES] = {0};

	if (accel_dev->pf.vf_info)
		return 0;

	if (!accel_dev->cfg)
		return 0;

	strscpy(key, ADF_NUM_ADI, ADF_CFG_MAX_VAL_LEN_IN_BYTES);
	if (adf_cfg_get_param_value(accel_dev, ADF_SIOV_SEC, key, val) ||
	    kstrtouint(val, 0, &adi_num) || 0 >= adi_num)
		return 0;

	if (accel_dev->adi_info)
		return -EEXIST;

	accel_dev->adi_info = (struct adf_adi_info *)
		kzalloc(sizeof(*accel_dev->adi_info), GFP_KERNEL);

	if (!accel_dev->adi_info)
		return -ENOMEM;

	adi_info = accel_dev->adi_info;
	adi_info->adi_num = adi_num;

	adi_info->adis = (struct adf_adi_ep *)
		kcalloc(adi_num, sizeof(*adi_info->adis),
			GFP_KERNEL);

	if (!adi_info->adis)
		goto failed;

	for (i = 0; i < adi_num; i++)
		if (adf_create_adi(accel_dev,
				   &adi_info->adis[i],
				   i)) {
			dev_err(&GET_DEV(accel_dev),
				"Failed to create ADI#%d\n",
				i);
			goto failed;
		}
	return 0;
failed:
	adf_exit_adis(accel_dev);
	dev_err(&GET_DEV(accel_dev), "Failed to do ADI init\n");
	return -EFAULT;
}
EXPORT_SYMBOL_GPL(adf_init_adis);

void adf_exit_adis(struct adf_accel_dev *accel_dev)
{
	int i, adi_num = 0;

	if (!accel_dev || !accel_dev->adi_info)
		return;

	adi_num = accel_dev->adi_info->adi_num;
	if (accel_dev->adi_info && accel_dev->adi_info->adis) {
		for (i = 0; i < adi_num; i++)
			adf_destroy_adi(accel_dev,
					&accel_dev->adi_info->adis[i]);

		kfree(accel_dev->adi_info->adis);
		accel_dev->adi_info->adis = NULL;
	}

	kfree(accel_dev->adi_info);
	accel_dev->adi_info = NULL;
}
EXPORT_SYMBOL_GPL(adf_exit_adis);

struct adf_adi_ep *adf_adi_alloc(struct adf_accel_dev *accel_dev,
				 enum adi_service_type type)
{
	int i = 0;
	struct adf_adi_ep *new_adi = NULL;
	struct adf_adi_info *adi_info = NULL;

	if (type != ADI_TYPE_COMP &&
	    type != ADI_TYPE_SYM &&
	    type != ADI_TYPE_ASYM)
		return NULL;

	if (!accel_dev || !accel_dev->adi_info ||
	    !accel_dev->adi_info->adis)
		return NULL;

	adi_info = accel_dev->adi_info;

	for (i = 0; i < accel_dev->adi_info->adi_num; i++) {
		mutex_lock(&adi_info->adis[i].lock);
		if (adi_info->adis[i].type == type &&
		    adi_info->adis[i].status == ADI_STATUS_IDLE) {
			new_adi = &adi_info->adis[i];
			new_adi->status = ADI_STATUS_ACTIVE;
			if (new_adi->adi_ops && new_adi->adi_ops->enable)
				new_adi->adi_ops->enable(new_adi);

			mutex_unlock(&adi_info->adis[i].lock);
			break;
		}
		mutex_unlock(&adi_info->adis[i].lock);
	}

	return new_adi;
}

int adf_adi_free(struct adf_adi_ep *adi)
{
	if (!adi)
		return -EINVAL;

	mutex_lock(&adi->lock);
	if (adi->status != ADI_STATUS_ACTIVE) {
		mutex_unlock(&adi->lock);
		return -EFAULT;
	}

	if (adi->adi_ops && adi->adi_ops->disable)
		adi->adi_ops->disable(adi);

	adi->status = ADI_STATUS_IDLE;
	mutex_unlock(&adi->lock);
	return 0;
}

int adf_get_num_avail_adis(struct adf_accel_dev *accel_dev,
			   enum adi_service_type type)
{
	int num_adi = 0;
	int i = 0;
	int ret = 0;
	struct adf_adi_ep *adi = NULL;

	if (!accel_dev->adi_info ||
	    !accel_dev->adi_info->adis)
		return 0;

	num_adi = accel_dev->adi_info->adi_num;

	for (i = 0; i < num_adi; i++) {
		adi = &accel_dev->adi_info->adis[i];
		if (adi->type == type &&
		    adi->status == ADI_STATUS_IDLE)
			ret++;
	}

	return ret;
}

/*
 * Return number of all ADI if type is ADI_TYPE_INVALID(0),
 * Otherwise return number of ADI with specific type.
 */
int adf_get_num_max_adis(struct adf_accel_dev *accel_dev,
			 enum adi_service_type type)
{
	int num_adi = 0;
	int i = 0;
	int ret = 0;
	struct adf_adi_ep *adi = NULL;

	if (!accel_dev->adi_info ||
	    !accel_dev->adi_info->adis)
		return 0;

	num_adi = accel_dev->adi_info->adi_num;

	if (type == ADI_TYPE_INVALID) {
		ret = num_adi;
	} else {
		for (i = 0; i < num_adi; i++) {
			adi = &accel_dev->adi_info->adis[i];
			if (adi->type == type)
				ret++;
		}
	}

	return ret;
}

