// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2021 Intel Corporation */
#include <linux/workqueue.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/iommu.h>
#include <linux/spinlock.h>
#include "adf_common_drv.h"
#include "adf_cfg.h"
#include "adf_pf2vf_msg.h"
#ifdef QAT_UIO
#include "icp_qat_hw.h"
#endif
#include "qdm.h"

#define AE2FUNCTION_MAP_A_OFFSET	(0x3A400 + 0x190)
#define AE2FUNCTION_MAP_B_OFFSET	(0x3A400 + 0x310)
#define AE2FUNCTION_MAP_REG_SIZE	4
#define AE2FUNCTION_MAP_VALID		BIT(7)

#define READ_CSR_AE2FUNCTION_MAP_A(pmisc_bar_addr, index)		\
	ADF_CSR_RD(pmisc_bar_addr, AE2FUNCTION_MAP_A_OFFSET +		\
		   AE2FUNCTION_MAP_REG_SIZE * (index))

#define WRITE_CSR_AE2FUNCTION_MAP_A(pmisc_bar_addr, index, value)	\
	ADF_CSR_WR(pmisc_bar_addr, AE2FUNCTION_MAP_A_OFFSET +		\
		   AE2FUNCTION_MAP_REG_SIZE * (index), value)

#define READ_CSR_AE2FUNCTION_MAP_B(pmisc_bar_addr, index)		\
	ADF_CSR_RD(pmisc_bar_addr, AE2FUNCTION_MAP_B_OFFSET +		\
		   AE2FUNCTION_MAP_REG_SIZE * (index))

#define WRITE_CSR_AE2FUNCTION_MAP_B(pmisc_bar_addr, index, value)	\
	ADF_CSR_WR(pmisc_bar_addr, AE2FUNCTION_MAP_B_OFFSET +		\
		   AE2FUNCTION_MAP_REG_SIZE * (index), value)

static struct workqueue_struct *pf2vf_resp_wq;
static int pf2vf_resp_wq_reference;
static DEFINE_MUTEX(pf2vf_resp_wq_ref_lock);

struct adf_pf2vf_resp {
	struct work_struct pf2vf_resp_work;
	struct adf_accel_vf_info *vf_info;
};

static int adf_check_arbitrary_numvfs(struct adf_accel_dev *accel_dev,
				      const int numvfs)
{
	int totalvfs = pci_sriov_get_totalvfs(accel_to_pci_dev(accel_dev));

	/*
	 * Due to the hardware design, when SR-IOV and the ring arbiter
	 * are enabled all the VFs supported in hardware must be enabled in
	 * order for all the hardware resources (i.e. bundles) to be usable.
	 * When SR-IOV is enabled, each of the VFs will own one bundle.
	 */
	return numvfs ? totalvfs : numvfs;
}

void adf_configure_iov_threads(struct adf_accel_dev *accel_dev, bool enable)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct adf_bar *pmisc =
			&GET_BARS(accel_dev)[hw_data->get_misc_bar_id(hw_data)];
	void __iomem *pmisc_addr = pmisc->virt_addr;
	u32 i, reg;

	/* Set/Unset Valid bits in AE Thread to PCIe Function Mapping */
	for (i = 0; i < ADF_NUM_THREADS_PER_AE * hw_data->num_engines; i++) {
		reg = READ_CSR_AE2FUNCTION_MAP_A(pmisc_addr, i);
		if (enable)
			reg |= AE2FUNCTION_MAP_VALID;
		else
			reg &= ~AE2FUNCTION_MAP_VALID;
		WRITE_CSR_AE2FUNCTION_MAP_A(pmisc_addr, i, reg);
	}

	for (i = 0; i < hw_data->num_engines; i++) {
		reg = READ_CSR_AE2FUNCTION_MAP_B(pmisc_addr, i);
		if (enable)
			reg |= AE2FUNCTION_MAP_VALID;
		else
			reg &= ~AE2FUNCTION_MAP_VALID;
		WRITE_CSR_AE2FUNCTION_MAP_B(pmisc_addr, i, reg);
	}
}
EXPORT_SYMBOL_GPL(adf_configure_iov_threads);

static void adf_iov_send_resp(struct work_struct *work)
{
	struct adf_pf2vf_resp *pf2vf_resp =
		container_of(work, struct adf_pf2vf_resp, pf2vf_resp_work);

	adf_vf2pf_req_hndl(pf2vf_resp->vf_info);
	kfree(pf2vf_resp);
}

void adf_vf2pf_handler(struct adf_accel_vf_info *vf_info)
{
	struct adf_pf2vf_resp *pf2vf_resp;

	pf2vf_resp = kzalloc(sizeof(*pf2vf_resp), GFP_ATOMIC);
	if (!pf2vf_resp)
		return;

	pf2vf_resp->vf_info = vf_info;
	INIT_WORK(&pf2vf_resp->pf2vf_resp_work, adf_iov_send_resp);
	queue_work(pf2vf_resp_wq, &pf2vf_resp->pf2vf_resp_work);
}

static int adf_enable_sriov(struct adf_accel_dev *accel_dev, const int numvfs)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct adf_bar *pmisc =
			&GET_BARS(accel_dev)[hw_data->get_misc_bar_id(hw_data)];
	void __iomem *pmisc_addr = pmisc->virt_addr;
	struct adf_accel_vf_info *vf_info;
	int i = 0;
	int ret = 0;
	unsigned long flags;

	/* init workqueue in PF */
	ret = adf_init_pf_wq();
	if (ret) {
		return ret;
	}

	for (i = 0, vf_info = accel_dev->pf.vf_info; i < numvfs;
	     i++, vf_info++) {
		/* This ptr will be populated when VFs will be created */
		vf_info->accel_dev = accel_dev;
		vf_info->vf_nr = i;
		vf_info->compat_ver = 0;

		mutex_init(&vf_info->pf2vf_lock);
		ratelimit_state_init(&vf_info->vf2pf_ratelimit,
				     ADF_IOV_RATELIMIT_INTERVAL,
				     ADF_IOV_RATELIMIT_BURST);
	}

	/* Set Valid bits in AE Thread to PCIe Function Mapping */
	if (hw_data->configure_iov_threads)
		hw_data->configure_iov_threads(accel_dev, true);

	/* Enable VF to PF interrupts for all VFs */
	spin_lock_irqsave(&accel_dev->vf2pf_csr_lock, flags);
	for (i = 0; i < ADF_MAX_VF2PF_SET; i++)
		hw_data->enable_vf2pf_interrupts(pmisc_addr, 0xFFFFFFFF, i);
	spin_unlock_irqrestore(&accel_dev->vf2pf_csr_lock, flags);

	adf_pfvf_debugfs_add(accel_dev);

	return pci_enable_sriov(accel_to_pci_dev(accel_dev), numvfs);
}

/**
 * adf_disable_sriov() - Disable SRIOV for the device
 * @accel_dev:  Pointer to accel device.
 *
 * Function disables SRIOV for the accel device.
 *
 * Return: 0 on success, error code otherwise.
 */
void adf_disable_sriov(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct adf_bar *pmisc =
			&GET_BARS(accel_dev)[hw_data->get_misc_bar_id(hw_data)];
	void __iomem *pmisc_addr = pmisc->virt_addr;
	int numvfs = pci_num_vf(accel_to_pci_dev(accel_dev));
	struct adf_accel_vf_info *vf;
	int i;

	if (!accel_dev->pf.vf_info)
		return;

	adf_pf2vf_notify_restarting(accel_dev);

	/*
	 * We need to wait for the Restarting completion message from VF's if
	 * a Hardware Hang Occurs so as to all VFs shutdown prior to PF
	 * disabling SRIOV.
	 */
	adf_vf2pf_wait_for_restarting_complete(accel_dev);

	pci_disable_sriov(accel_to_pci_dev(accel_dev));
	debugfs_remove_recursive(accel_dev->pfvf_dbgdir);
	accel_dev->pfvf_dbgdir = NULL;

	/* Disable VF to PF interrupts */
	spin_lock(&accel_dev->vf2pf_csr_lock);
	for (i = 0; i < ADF_MAX_VF2PF_SET; i++)
		hw_data->disable_vf2pf_interrupts(pmisc_addr, 0xFFFFFFFF, i);
	spin_unlock(&accel_dev->vf2pf_csr_lock);

	/* Clear Valid bits in AE Thread to PCIe Function Mapping */
	if (hw_data->configure_iov_threads)
		hw_data->configure_iov_threads(accel_dev, false);

	for (i = 0, vf = accel_dev->pf.vf_info; i < numvfs; i++, vf++) {
		mutex_destroy(&vf->pf2vf_lock);
	}

	/* destroy workqueue in PF */
	adf_exit_pf_wq();

	kfree(accel_dev->pf.vf_info);
	accel_dev->pf.vf_info = NULL;
}
EXPORT_SYMBOL_GPL(adf_disable_sriov);

static int adf_dev_prepare_restart(struct adf_accel_dev *accel_dev)
{
#ifdef QAT_UIO
	int ret;
	struct adf_cfg_section sec = { {0} };
	struct adf_cfg_section inline_sec = { {0} };

	ret = adf_cfg_save_section(accel_dev, ADF_GENERAL_SEC, &sec);
	if (ret)
		return ret;
	if ((GET_HW_DATA(accel_dev)->accel_capabilities_mask)
	    & ICP_ACCEL_CAPABILITIES_INLINE) {
		ret = adf_cfg_save_section(accel_dev,
					   ADF_INLINE_SEC,
					   &inline_sec);
		if (ret) {
			adf_cfg_keyval_del_all(&sec.param_head);
			return ret;
		}
	}
#endif
	adf_lkca_unregister(accel_dev);
	adf_dev_stop(accel_dev);
	adf_dev_shutdown(accel_dev);
#ifdef QAT_UIO
	if ((GET_HW_DATA(accel_dev)->accel_capabilities_mask)
	    & ICP_ACCEL_CAPABILITIES_INLINE) {
		ret = adf_cfg_restore_section(accel_dev, &inline_sec);
		adf_cfg_keyval_del_all(&inline_sec.param_head);
		if (ret) {
			adf_cfg_keyval_del_all(&sec.param_head);
			return ret;
		}
	}
	ret = adf_cfg_restore_section(accel_dev, &sec);
	adf_cfg_keyval_del_all(&sec.param_head);
	if (ret)
		return ret;
	set_bit(ADF_STATUS_SRIOV_RESTARTING, &accel_dev->status);
#endif

	return 0;
}

static int adf_dev_reinit(struct adf_accel_dev *accel_dev)
{
	int ret;

	ret = adf_dev_init(accel_dev);
	if (ret) {
		dev_err(&GET_DEV(accel_dev), "Failed to init qat_dev%d\n",
			accel_dev->accel_id);
		adf_dev_shutdown(accel_dev);
		return ret;
	}

	ret = adf_dev_start(accel_dev);
	if (ret) {
		dev_err(&GET_DEV(accel_dev), "Failed to start qat_dev%d\n",
			accel_dev->accel_id);
		adf_dev_stop(accel_dev);
		adf_dev_shutdown(accel_dev);
		return ret;
	}

	ret = adf_lkca_register(accel_dev);
	if (ret) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to register Linux Kernel Crypto API for qat_dev%d\n",
			accel_dev->accel_id);
		adf_dev_stop(accel_dev);
		adf_dev_shutdown(accel_dev);
		return ret;
	}
#ifdef QAT_UIO
	clear_bit(ADF_STATUS_SRIOV_RESTARTING, &accel_dev->status);
#endif

	return 0;
}

static int adf_config_cy(struct adf_accel_dev *accel_dev)
{
	const unsigned long val = 0;

	if (adf_cfg_section_add(accel_dev, ADF_KERNEL_SEC))
		return -EFAULT;

	if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
					ADF_NUM_CY, (void *)&val, ADF_DEC))
		return -EFAULT;

	set_bit(ADF_STATUS_CONFIGURED, &accel_dev->status);

	return 0;
}

static int adf_sriov_enable(struct adf_accel_dev *accel_dev, const int numvfs)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	int ret = -EFAULT;

	if (!qdm_iommu_present())
		dev_warn(&GET_DEV(accel_dev),
			 "IOMMU should be enabled for SR-IOV to work correctly\n");

	if (accel_dev->pf.vf_info) {
		dev_info(&GET_DEV(accel_dev),
			 "Already enabled for this device\n");
		return -EINVAL;
	}

	/* The device is down, hence configure the
	 * accel units before enabling the device
	 */
	if (!adf_dev_started(accel_dev) && !adf_devmgr_in_reset(accel_dev))
		if (GET_HW_DATA(accel_dev)->configure_accel_units &&
		    GET_HW_DATA(accel_dev)->configure_accel_units(accel_dev))
			goto err_del_cfg;

	if (adf_dev_started(accel_dev)) {
		if (adf_devmgr_in_reset(accel_dev) ||
		    adf_dev_in_use(accel_dev)) {
			dev_err(&GET_DEV(accel_dev), "Device busy\n");
			return -EBUSY;
		}
		ret = adf_dev_prepare_restart(accel_dev);
		if (ret)
			return ret;
	}

	ret = adf_config_cy(accel_dev);
	if (ret)
		goto err_del_cfg;

	/* Allocate memory for VF info structs */
	ret = -ENOMEM;
	accel_dev->pf.vf_info = kcalloc(numvfs,
					sizeof(struct adf_accel_vf_info),
					GFP_KERNEL);
	if (!accel_dev->pf.vf_info)
		goto err_del_cfg;

	ret = adf_dev_reinit(accel_dev);
	if (ret)
		goto err_free_vf;

	ret = adf_enable_sriov(accel_dev, numvfs);
	if (ret)
		goto err_free_vf;

	if (hw_data->qat_aux_enable) {
		if (hw_data->aux_ops->add_aux_dev(accel_dev)) {
			dev_err(&GET_DEV(accel_dev),
				"Failed to initialize auxiliary device\n");
			adf_disable_sriov(accel_dev);
			return -EFAULT;
		}
	}

	return numvfs;
err_free_vf:
	kfree(accel_dev->pf.vf_info);
	accel_dev->pf.vf_info = NULL;
err_del_cfg:
	adf_cfg_del_all(accel_dev);
	return ret;
}

static int adf_sriov_disable(struct adf_accel_dev *accel_dev)
{
	int ret;

	if (!accel_dev->pf.vf_info) {
		dev_info(&GET_DEV(accel_dev),
			 "Already disabled for this device\n");
		return -EINVAL;
	}

	ret = adf_dev_in_use(accel_dev);
	if (ret) {
		dev_err(&GET_DEV(accel_dev),
			"Disable SRIOV failed as device is in use\n");
		return -EBUSY;
	}

	if (adf_dev_started(accel_dev)) {
		if (adf_devmgr_in_reset(accel_dev)) {
			dev_err(&GET_DEV(accel_dev), "Device in reset\n");
			return -EBUSY;
		}

		ret = adf_dev_prepare_restart(accel_dev);
		if (ret)
			return ret;
	} else {
		adf_disable_sriov(accel_dev);
	}

	ret = qat_crypto_dev_config(accel_dev);
	if (ret)
		goto err_del_cfg;

	ret = adf_dev_reinit(accel_dev);
	if (ret)
		goto err_del_cfg;

	return 0;
err_del_cfg:
	adf_cfg_del_all(accel_dev);
	return ret;
}

/**
 * adf_sriov_configure() - Enable/disable SRIOV for the device
 * @pdev:  Pointer to pci device.
 * @numvfs: Number of VFs to configure
 *
 * Function enables or disables SRIOV for the pci device.
 *
 * Return: number of VFs enabled on success, error code otherwise.
 */
int adf_sriov_configure(struct pci_dev *pdev, int numvfs)
{
	struct adf_accel_dev *accel_dev = adf_devmgr_pci_to_accel_dev(pdev);

	if (!accel_dev) {
		dev_err(&pdev->dev, "Failed to find accel_dev\n");
		return -EFAULT;
	}

	if (accel_dev->hw_device->check_arbitrary_numvfs)
		numvfs = accel_dev->hw_device->check_arbitrary_numvfs(accel_dev,
								      numvfs);
	else
		numvfs = adf_check_arbitrary_numvfs(accel_dev, numvfs);

	if (numvfs)
		accel_dev->hw_device->is_sriov_numvfs_set = true;
	else
		accel_dev->hw_device->is_sriov_numvfs_set = false;

	if (numvfs)
		return adf_sriov_enable(accel_dev, numvfs);
	else
		return adf_sriov_disable(accel_dev);
}
EXPORT_SYMBOL_GPL(adf_sriov_configure);

/**
 * adf_init_pf_wq() - Init workqueue for PF
 *
 * Function init workqueue 'qat_pf2vf_resp_wq' for PF.
 *
 * Return: 0 on success, error code otherwise.
 */
int adf_init_pf_wq(void)
{
	/* Workqueue for PF2VF responses */
	mutex_lock(&pf2vf_resp_wq_ref_lock);
	if (!pf2vf_resp_wq)
		pf2vf_resp_wq = alloc_workqueue("qat_pf2vf_resp_wq",
						WQ_MEM_RECLAIM, 1);

	if (!pf2vf_resp_wq) {
		mutex_unlock(&pf2vf_resp_wq_ref_lock);
		return -ENOMEM;
	}

	pf2vf_resp_wq_reference++;
	mutex_unlock(&pf2vf_resp_wq_ref_lock);
	return 0;
}

/**
 * adf_exit_pf_wq() - Destroy workqueue for PF
 *
 * Function destroy workqueue 'qat_pf2vf_resp_wq' for PF.
 *
 * Return: void.
 */
void adf_exit_pf_wq(void)
{
	mutex_lock(&pf2vf_resp_wq_ref_lock);
	if (pf2vf_resp_wq && --pf2vf_resp_wq_reference == 0) {
		destroy_workqueue(pf2vf_resp_wq);
		pf2vf_resp_wq = NULL;
	}
	mutex_unlock(&pf2vf_resp_wq_ref_lock);
}
