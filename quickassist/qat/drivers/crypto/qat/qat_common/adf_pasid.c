// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2020 Intel Corporation */

#include <linux/pci.h>
#include <linux/version.h>
#include "adf_transport_access_macros_gen4.h"
#include "adf_transport_internal.h"
#include "adf_common_drv.h"
#include "adf_svm.h"
#include "adf_pasid.h"
#if (KERNEL_VERSION(5, 4, 0) <= LINUX_VERSION_CODE)
#include <linux/mmu_notifier.h>
#if (KERNEL_VERSION(5, 7, 0) <= LINUX_VERSION_CODE)
#include <linux/iommu.h>
#endif

struct adf_pasid_hw_instance {
	struct adf_accel_dev *accel_dev;
	struct adf_etr_bank_data *bank;
	struct list_head list;
	struct mm_struct *mm;
#if (KERNEL_VERSION(5, 7, 0) <= LINUX_VERSION_CODE)
	/* kernel uses per device iommu_sva handle to unbind device */
	struct iommu_sva *handle;
#endif
};

struct adf_pasid_context {
	int pid;
	int pasid;
	int ref_cnt;
	/* list of hw_instance (dev:bank) sharing the PASID/PID */
	struct list_head instance_list;
	/* list of hash table */
	struct list_head hash_list;
	/* Mutex to sync pasid_context data */
	struct mutex lock;
	/* mmu notifier to get notification when mm is being destroyed */
	struct mmu_notifier mmu_notifier;
	/* list of mm */
	struct list_head mm_list;
	cleanup_svm_orphan_fn cleanup_orphan;
	void *cleanup_orphan_priv;
};

struct adf_mm_info {
	struct mm_struct *mm;
	struct list_head list;
	int users;
};

#define PASID_HASH_SIZE 64

/* Hash table for fast pid->pasid_context lookup */
static struct list_head pasid_hash_table[PASID_HASH_SIZE] = {0};
static DEFINE_MUTEX(pasid_hash_table_lock);

static int adf_pasid_unbind_all_by_pid(int pid);

static inline int adf_pasid_hash_get_key(int pid)
{
	return ((pid % PASID_HASH_SIZE + PASID_HASH_SIZE) % PASID_HASH_SIZE);
}

static inline void adf_pasid_hash_add(struct adf_pasid_context *node)
{
	int key = adf_pasid_hash_get_key(node->pid);

	list_add_tail(&node->hash_list, &pasid_hash_table[key]);
}

static inline void adf_pasid_hash_del(int pid)
{
	struct list_head *list;

	list_for_each(list, &pasid_hash_table[adf_pasid_hash_get_key(pid)]) {
		struct adf_pasid_context *ptr =
			list_entry(list,
				   struct adf_pasid_context, hash_list);

		if (ptr->pid == pid) {
			list_del(&ptr->hash_list);
			return;
		}
	}
}

static inline struct adf_pasid_context *adf_pasid_pid_to_context(int pid)
{
	struct list_head *list;

	list_for_each(list, &pasid_hash_table[adf_pasid_hash_get_key(pid)]) {
		struct adf_pasid_context *ptr =
			list_entry(list,
				   struct adf_pasid_context, hash_list);

		if (ptr->pid == pid)
			return ptr;
	}

	return NULL;
}

void adf_pasid_init(void)
{
	int i;

	mutex_lock(&pasid_hash_table_lock);
	for (i = 0; i < ARRAY_SIZE(pasid_hash_table); i++)
		INIT_LIST_HEAD(&pasid_hash_table[i]);
	mutex_unlock(&pasid_hash_table_lock);
}

void adf_pasid_destroy(void)
{
	int i;
	struct adf_pasid_context *ptr;
	struct list_head *list, *tmp;

	mutex_lock(&pasid_hash_table_lock);
	for (i = 0; i < ARRAY_SIZE(pasid_hash_table); i++) {
		if (!pasid_hash_table[i].next)
			continue;

		list_for_each_prev_safe(list, tmp, &pasid_hash_table[i]) {
			ptr = list_entry(list,
					 struct adf_pasid_context,
					 hash_list);

			pr_err("QAT: Zombie PASID context by PID(%d)\n",
			       ptr->pid);

			adf_pasid_unbind_all_by_pid(ptr->pid);
		}
	}
	mutex_unlock(&pasid_hash_table_lock);
}

static void adf_dev_mm_release(struct mmu_notifier *mn,
			       struct mm_struct *mm)
{
	struct adf_pasid_context *pasid_ctx;
	struct adf_pasid_hw_instance *hw_inst;
	struct adf_accel_dev *accel_dev;
	struct list_head *list;
	int i;

	/* Find all hw instances that were using mm that is being destroyed */
	for (i = 0; i < PASID_HASH_SIZE; i++) {
		list_for_each(list, &pasid_hash_table[i]) {
			pasid_ctx =
				list_entry(list,
					   struct adf_pasid_context,
					   hash_list);
			if (current->tgid != pasid_ctx->pid) {
				continue;
			}

			list_for_each_entry(hw_inst,
					    &pasid_ctx->instance_list, list) {
				accel_dev = hw_inst->accel_dev;
				dev_dbg(&GET_DEV(accel_dev),
					"Detected orphan ring from %d process",
					pasid_ctx->pid);

				pasid_ctx->cleanup_orphan
					(pasid_ctx->cleanup_orphan_priv,
					pasid_ctx->pid);
			}
		}
	}
}

static const struct mmu_notifier_ops adf_dev_mmu_notifier_ops = {
	.release = adf_dev_mm_release,
};

#if (KERNEL_VERSION(5, 7, 0) <= LINUX_VERSION_CODE)
static int adf_dev_attach_pasid(struct adf_accel_dev *accel_dev,
				struct adf_pasid_context *pasid_ctx,
				struct adf_pasid_hw_instance *hw_inst)
{
	struct adf_accel_pci *accel_pci_dev = &accel_dev->accel_pci_dev;
	struct pci_dev *pdev = accel_pci_dev->pci_dev;
	struct iommu_sva *pasid_handle;
	struct adf_mm_info *mm_info;
	int ret, have_notifier = 0;

	if (!hw_inst || !pasid_ctx)
		return -EINVAL;

	pasid_handle = iommu_sva_bind_device(&pdev->dev, current->mm);
	if (IS_ERR(pasid_handle)) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to bind the current process to a PASID!\n");
		return -EINVAL;
	}

	pasid_ctx->pasid = iommu_sva_get_pasid(pasid_handle);
	if (pasid_ctx->pasid == IOMMU_PASID_INVALID) {
		dev_err(&GET_DEV(accel_dev),
			"Invalid PASID!\n");

		iommu_sva_unbind_device(pasid_handle);
		return -EFAULT;
	}

	/* Check if we already registered notifier for given mm,
	 * if so increase users count to know when notification
	 * can be safely unregistered
	 */
	list_for_each_entry(mm_info, &pasid_ctx->mm_list, list) {
		if (mm_info && mm_info->mm == current->mm) {
			have_notifier = 1;
			mm_info->users++;
			break;
		}
	}

	/* Register mmu notification for given mm, will be used
	 * to detect mm release and cleanup orphan rings
	 */
	if (have_notifier == 0) {
		mm_info = kzalloc(sizeof(*mm_info), GFP_KERNEL);
		if (!mm_info) {
			iommu_sva_unbind_device(pasid_handle);
			return -ENOMEM;
		}
		mm_info->users = 1;
		mm_info->mm = current->mm;
		ret = mmu_notifier_register(&pasid_ctx->mmu_notifier,
					    current->mm);
		if (ret) {
			kfree(mm_info);
			iommu_sva_unbind_device(pasid_handle);
			return -EFAULT;
		}
		INIT_LIST_HEAD(&mm_info->list);
		list_add_tail(&mm_info->list, &pasid_ctx->mm_list);
	}

	/* Store mapping between hw_inst and mm, to be able to
	 * detect later which hw inst was using mm being released
	 */
	hw_inst->mm = current->mm;
	hw_inst->handle = pasid_handle;
	dev_dbg(&GET_DEV(accel_dev),
		"Attach device to PASID %d\n", pasid_ctx->pasid);

	return 0;
}

static void adf_dev_detach_pasid(struct adf_accel_dev *accel_dev,
				 struct adf_pasid_context *pasid_ctx,
				 struct adf_pasid_hw_instance *hw_inst)
{
	struct adf_mm_info *mm_info;

	if (!hw_inst || !hw_inst->handle)
		return;

	/* Decrease mm user count, if it drops to 0,
	 * unregister mmu notification for given mm
	 */
	list_for_each_entry(mm_info, &pasid_ctx->mm_list, list) {
		if (mm_info && mm_info->mm == hw_inst->mm) {
			mm_info->users--;
			if (mm_info->users == 0) {
				mmu_notifier_unregister(
					&pasid_ctx->mmu_notifier,
					mm_info->mm);
				list_del(&mm_info->list);
				kfree(mm_info);
				break;
			}
		}
	}

	iommu_sva_unbind_device(hw_inst->handle);
	hw_inst->handle = NULL;
	dev_dbg(&GET_DEV(accel_dev),
		"Detach device from PASID %d\n", pasid_ctx->pasid);
}
#else
static int adf_dev_attach_pasid(struct adf_accel_dev *accel_dev,
				struct adf_pasid_context *pasid_ctx,
				struct adf_pasid_hw_instance *hw_inst)
{
	struct adf_accel_pci *accel_pci_dev = &accel_dev->accel_pci_dev;
	struct pci_dev *pdev = accel_pci_dev->pci_dev;
	struct adf_mm_info *mm_info;
	int ret, have_notifier = 0;

	if (!pasid_ctx)
		return -EINVAL;

	if (intel_svm_bind_mm(&pdev->dev, &pasid_ctx->pasid, 0, NULL)) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to bind the current process to a PASID!\n");
		return -EINVAL;
	}

	/* Check if we already registered notifier for given mm,
	 * if so increase users count to know when notification
	 * can be safely unregistered
	 */
	list_for_each_entry(mm_info, &pasid_ctx->mm_list, list) {
		if (mm_info && mm_info->mm == current->mm) {
			have_notifier = 1;
			mm_info->users++;
			break;
		}
	}

	/* Register mmu notification for given mm, will be used
	 * to detect mm release and cleanup orphan rings
	 */
	if (have_notifier == 0) {
		mm_info = kzalloc(sizeof(*mm_info), GFP_KERNEL);
		if (!mm_info)
			return -ENOMEM;

		mm_info->users = 1;
		mm_info->mm = current->mm;
		ret = mmu_notifier_register(&pasid_ctx->mmu_notifier,
					    current->mm);
		if (ret) {
			kfree(mm_info);
			return -EFAULT;
		}
		INIT_LIST_HEAD(&mm_info->list);
		list_add_tail(&mm_info->list, &pasid_ctx->mm_list);
	}
	/* Store mapping between hw_inst and mm, to be able to
	 * detect later which hw inst was using mm being released
	 */
	hw_inst->mm = current->mm;

	dev_dbg(&GET_DEV(accel_dev),
		"Attach device to PASID %d\n", pasid_ctx->pasid);

	return 0;
}

static void adf_dev_detach_pasid(struct adf_accel_dev *accel_dev,
				 struct adf_pasid_context *pasid_ctx,
				 struct adf_pasid_hw_instance *hw_inst)
{
	struct adf_accel_pci *accel_pci_dev = &accel_dev->accel_pci_dev;
	struct pci_dev *pdev = accel_pci_dev->pci_dev;
	struct adf_mm_info *mm_info;

	if (!pasid_ctx)
		return;

	/* Decrease mm user count, if it drops to 0,
	 * unregister mmu notification for given mm
	 */
	list_for_each_entry(mm_info, &pasid_ctx->mm_list, list) {
		if (mm_info && mm_info->mm == hw_inst->mm) {
			mm_info->users--;
			if (mm_info->users == 0) {
				mmu_notifier_unregister(
					&pasid_ctx->mmu_notifier,
					mm_info->mm);
				list_del(&mm_info->list);
				kfree(mm_info);
				break;
			}
		}
	}

	if (intel_svm_unbind_mm(&pdev->dev, pasid_ctx->pasid))
		dev_err(&GET_DEV(accel_dev),
			"Failed to detach device from PASID: 0x%x!\n",
			pasid_ctx->pasid);

	dev_dbg(&GET_DEV(accel_dev),
		"Detach device from PASID %d\n", pasid_ctx->pasid);
}
#endif

static struct adf_pasid_hw_instance
*adf_pasid_find_instance_from_context(struct adf_pasid_context *context,
				      u32 accel_id,
				      u32 bank_nr)
{
	struct list_head *list;

	if (context) {
		list_for_each(list, &context->instance_list) {
			struct adf_pasid_hw_instance *ptr =
				list_entry(list,
					   struct adf_pasid_hw_instance,
					   list);
			if (ptr->accel_dev->accel_id == accel_id &&
			    ptr->bank->bank_number == bank_nr)
				return ptr;
		}
	}

	return NULL;
}

static int adf_pasid_do_bind(struct adf_accel_dev *accel_dev,
			     u32 bank_nr, int pid,
			     cleanup_svm_orphan_fn cleanup_orphan,
			     void *cleanup_priv)
{
	int ret = -EFAULT;
	struct adf_pasid_context *pasid_context;
	struct adf_etr_bank_data *bank;
	struct adf_pasid_hw_instance *hw_inst;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;

	if (bank_nr >= hw_data->num_banks)
		return -EINVAL;

	if (!hw_data->config_bank_pasid) {
		dev_err(&GET_DEV(accel_dev),
			"NULL ops for config_bank_pasid\n");

		return -EFAULT;
	}

	/* Locate PASID context of the process */
	pasid_context = adf_pasid_pid_to_context(pid);

	/*
	 * Allocate PASID context during the first call
	 * by the process
	 */
	if (!pasid_context) {
		pasid_context = kzalloc(sizeof(*pasid_context), GFP_KERNEL);
		if (!pasid_context)
			return -ENOMEM;

		mutex_init(&pasid_context->lock);
		INIT_LIST_HEAD(&pasid_context->hash_list);
		INIT_LIST_HEAD(&pasid_context->instance_list);
		INIT_LIST_HEAD(&pasid_context->mm_list);
		pasid_context->pid = pid;
		pasid_context->mmu_notifier.ops = &adf_dev_mmu_notifier_ops;
		pasid_context->cleanup_orphan = cleanup_orphan;
		pasid_context->cleanup_orphan_priv = cleanup_priv;
		adf_pasid_hash_add(pasid_context);
	}

	dev_dbg(&GET_DEV(accel_dev),
		"Enable PASID for Bank: %d\n", bank_nr);

	/* Make sure banks are not used by another PASID context */
	mutex_lock(&pasid_context->lock);
	bank = &accel_dev->transport->banks[bank_nr];
	if (unlikely(bank->pasid_context)) {
		dev_err(&GET_DEV(accel_dev),
			"Bank %d already in SVM mode used by PID(%d)\n",
			bank_nr,
			((struct adf_pasid_context *)
				bank->pasid_context)->pid);

		ret = -EEXIST;
		mutex_unlock(&pasid_context->lock);
		goto err_release_context;
	}

	bank->pasid_context = pasid_context;

	/* Make sure there is no duplicate instance in the context */
	if (adf_pasid_find_instance_from_context(pasid_context,
						 accel_dev->accel_id,
						 bank_nr)) {
		dev_err(&GET_DEV(accel_dev),
			"Duplicated instance found in process %d\n",
			pasid_context->pid);

		mutex_unlock(&pasid_context->lock);
		ret = -EEXIST;
		goto err_release_bank;
	}

	/* allocates HW instance */
	hw_inst = kzalloc(sizeof(*hw_inst), GFP_KERNEL);
	if (!hw_inst) {
		mutex_unlock(&pasid_context->lock);
		ret = -ENOMEM;
		goto err_release_bank;
	}

	hw_inst->accel_dev = accel_dev;
	hw_inst->bank = bank;

	/*
	 * IOMMU driver allows same device to bind with the
	 * process address space multiple times using an internal
	 * reference count per the device. Thus it is safe to
	 * attach the device to the PASID repeatingly for different
	 * ring pairs.
	 */
	if (adf_dev_attach_pasid(accel_dev, pasid_context, hw_inst)) {
		mutex_unlock(&pasid_context->lock);
		dev_err(&GET_DEV(accel_dev),
			"Cannot bind device to existing PASID(%d)\n",
			pasid_context->pasid);
		goto err_release_inst;
	}

	list_add_tail(&hw_inst->list, &pasid_context->instance_list);
	pasid_context->ref_cnt++;

	dev_dbg(&GET_DEV(accel_dev),
		"PASID(%d).ref_cnt == %d\n",
		pasid_context->pasid,
		pasid_context->ref_cnt);

	if (hw_data->config_bank_pasid(accel_dev,
				       bank_nr,
				       true,
				       accel_dev->at_enabled,
				       false,
				       false,
				       pasid_context->pasid)) {
		mutex_unlock(&pasid_context->lock);
		ret = -EFAULT;
		goto err_dec_refcnt;
	}

	mutex_unlock(&pasid_context->lock);
	return 0;
err_dec_refcnt:
	pasid_context->ref_cnt--;
	list_del(&hw_inst->list);
	adf_dev_detach_pasid(accel_dev, pasid_context, hw_inst);
err_release_inst:
	kfree(hw_inst);
err_release_bank:
	bank->pasid_context = NULL;
err_release_context:
	if (pasid_context && !pasid_context->ref_cnt) {
		adf_pasid_hash_del(pasid_context->pid);
		mutex_destroy(&pasid_context->lock);
		kfree(pasid_context);
	}

	return ret;
}

int adf_pasid_bind_bank_with_pid(struct adf_accel_dev *accel_dev,
				 u32 bank_nr, int pid,
				 cleanup_svm_orphan_fn cleanup_orphan,
				 void *cleanup_priv)
{
	int ret = 0;

	if (!accel_dev || !accel_dev->hw_device)
		return -EINVAL;

	if (!accel_dev->svm_enabled)
		return -EFAULT;

	mutex_lock(&pasid_hash_table_lock);
	ret = adf_pasid_do_bind(accel_dev, bank_nr, pid, cleanup_orphan,
				cleanup_priv);
	mutex_unlock(&pasid_hash_table_lock);

	return ret;
}

static void adf_pasid_do_unbind(struct adf_pasid_context *pasid_context,
				struct adf_pasid_hw_instance *hw_inst)
{
	struct adf_accel_dev *accel_dev = hw_inst->accel_dev;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;

	/* Discharge resource of HW instance */
	mutex_lock(&pasid_context->lock);
	hw_inst->bank->pasid_context = NULL;

	if (hw_data->config_bank_pasid)
		hw_data->config_bank_pasid(accel_dev,
					   hw_inst->bank->bank_number,
					   false,
					   false,
					   false,
					   false,
					   0);

	adf_dev_detach_pasid(accel_dev, pasid_context, hw_inst);
	list_del(&hw_inst->list);
	kfree(hw_inst);
	pasid_context->ref_cnt--;
	mutex_unlock(&pasid_context->lock);
}

int adf_pasid_unbind_bank_with_pid(struct adf_accel_dev *accel_dev,
				   int pid,
				   u32 bank_nr)
{
	struct adf_pasid_context *pasid_context;
	struct adf_pasid_hw_instance *hw_inst = NULL;

	if (!accel_dev)
		return -EINVAL;

	mutex_lock(&pasid_hash_table_lock);
	pasid_context = adf_pasid_pid_to_context(pid);
	if (!pasid_context) {
		dev_err(&GET_DEV(accel_dev),
			"Cannot find PASID context for process %d\n", pid);
		mutex_unlock(&pasid_hash_table_lock);

		return -ENOENT;
	}

	if (!pasid_context->ref_cnt) {
		dev_err(&GET_DEV(accel_dev),
			"Instance pool is empty for process %d\n", pid);
		mutex_unlock(&pasid_hash_table_lock);

		return -ENOENT;
	}

	hw_inst = adf_pasid_find_instance_from_context(pasid_context,
						       accel_dev->accel_id,
						       bank_nr);

	if (!hw_inst) {
		dev_err(&GET_DEV(accel_dev),
			"Cannot find HW instance for process %d\n", pid);
		mutex_unlock(&pasid_hash_table_lock);

		return -ENOENT;
	}

	adf_pasid_do_unbind(pasid_context, hw_inst);

	dev_dbg(&GET_DEV(accel_dev),
		"PASID(%d).ref_cnt == %d\n",
		pasid_context->pasid,
		pasid_context->ref_cnt);

	if (!pasid_context->ref_cnt) {
		adf_pasid_hash_del(pasid_context->pid);
		mutex_destroy(&pasid_context->lock);
		kfree(pasid_context);
	}

	mutex_unlock(&pasid_hash_table_lock);

	return 0;
}

static int adf_pasid_unbind_all_by_pid(int pid)
{
	int ret = 0;
	struct adf_pasid_context *pasid_context = NULL;
	struct list_head *list_ptr, *tmp;

	pasid_context = adf_pasid_pid_to_context(pid);
	if (!pasid_context)
		return -EFAULT;

	/* Release all HW instance under PASID context */
	list_for_each_prev_safe(list_ptr, tmp, &pasid_context->instance_list) {
		struct adf_pasid_hw_instance *hw_inst =
			list_entry(list_ptr,
				   struct adf_pasid_hw_instance, list);

		adf_pasid_do_unbind(pasid_context, hw_inst);
	}

	if (pasid_context->ref_cnt) {
		pr_err("QAT: Warning, ref_cnt not 0 after unbind_all %d\n",
		       pid);
		ret = -EFAULT;
	}

	adf_pasid_hash_del(pasid_context->pid);
	mutex_destroy(&pasid_context->lock);
	kfree(pasid_context);

	return ret;
}

static int adf_pasid_iov_control(struct adf_accel_dev *accel_dev,
				 bool enable,
				 u32 pasid,
				 u32 bank_number)
{
	dev_dbg(&GET_DEV(accel_dev),
		"Config PASID via IOV is not implemented.\n");

	return -EFAULT;
}

static int adf_pasid_config_pf(struct adf_accel_dev *accel_dev,
			       u32 bank_number,
			       bool enable,
			       bool at, bool adi, bool priv,
			       int pasid)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct adf_bar *etr_bar;
	void __iomem *csr;
	struct adf_hw_csr_ops *csr_ops;

	if (bank_number >= hw_data->num_banks)
		return -EFAULT;

	etr_bar = &GET_BARS(accel_dev)[hw_data->get_etr_bar_id(hw_data)];
	csr = etr_bar->virt_addr;
	csr_ops = &hw_data->csr_info.csr_ops;

	if (!csr_ops->bank_pasid_enable ||
	    !csr_ops->bank_pasid_disable)
		return -EFAULT;

	if (enable)
		csr_ops->bank_pasid_enable(csr,
					   bank_number,
					   at, adi, priv,
					   pasid);
	else
		csr_ops->bank_pasid_disable(csr,
					    bank_number,
					    false, false, false);

	return 0;
}

static int adf_pasid_config_vf(struct adf_accel_dev *accel_dev,
			       u32 bank_number,
			       bool enable,
			       bool at, bool adi, bool priv,
			       int pasid)
{
	struct adf_accel_dev *pf;
	int vf_id;
	struct adf_accel_pci *accel_pci_dev = &accel_dev->accel_pci_dev;
	struct pci_dev *pdev = accel_pci_dev->pci_dev;
	u32 bank_number_pf;

	if (bank_number >= accel_dev->hw_device->num_banks)
		return -EINVAL;

	pf = adf_devmgr_pci_to_accel_dev(pdev->physfn);

	/* For VF in the guest, set PASID via IOV proxy */
	if (!pf)
		return adf_pasid_iov_control(accel_dev,
					     enable,
					     pasid,
					     bank_number);

	/* For VF in the host, set PASID register diretly via PF */
	vf_id = adf_get_vf_id(accel_dev, pf);

	if (vf_id < 0 || vf_id >= pci_num_vf(accel_to_pci_dev(pf))) {
		dev_err(&GET_DEV(accel_dev),
			"Invalid VF ID: %d\n", vf_id + 1);
		return -EFAULT;
	}

	bank_number_pf =
		pf->hw_device->num_banks_per_vf * vf_id + bank_number;

	dev_dbg(&GET_DEV(accel_dev),
		"%s PASID for bank %d of VF#%d through parent PF\n",
		enable ? "Enable" : "Disable",
		bank_number, vf_id + 1);

	return adf_pasid_config_pf(pf,
				       bank_number_pf,
				       enable,
				       at, adi, priv,
				       pasid);
}

int adf_pasid_config_bank(struct adf_accel_dev *accel_dev,
			  u32 bank_number,
			  bool enable,
			  bool at, bool adi, bool priv,
			  int pasid)
{
	if (accel_dev->is_vf)
		return adf_pasid_config_vf(accel_dev,
					   bank_number,
					   enable,
					   at, adi, priv,
					   pasid);
	else
		return adf_pasid_config_pf(accel_dev,
					   bank_number,
					   enable,
					   at, adi, priv,
					   pasid);
}
EXPORT_SYMBOL_GPL(adf_pasid_config_bank);
#else
void adf_pasid_init(void)
{
}

void adf_pasid_destroy(void)
{
}

int adf_pasid_bind_bank_with_pid(struct adf_accel_dev *accel_dev,
				 u32 bank_nr, int pid,
				 cleanup_svm_orphan_fn cleanup_orphan,
				 void *cleanup_priv)
{
	return -EFAULT;
}

int adf_pasid_unbind_bank_with_pid(struct adf_accel_dev *accel_dev,
				   int pid,
				   u32 bank_nr)
{
	return -EFAULT;
}

int adf_pasid_config_bank(struct adf_accel_dev *accel_dev,
			  u32 bank_number,
			  bool enable,
			  bool at, bool adi, bool priv,
			  int pasid)
{
	return -EFAULT;
}
EXPORT_SYMBOL_GPL(adf_pasid_config_bank);
#endif
