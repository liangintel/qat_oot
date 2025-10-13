// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2021 Intel Corporation */
#include <linux/uio_driver.h>
#include <linux/sched.h>
#include <linux/kobject.h>
#include <linux/semaphore.h>
#include <linux/uaccess.h>

#include "adf_common_drv.h"
#include "adf_uio_control.h"
#include "adf_transport_access_macros.h"
#include "adf_uio_cleanup.h"
#include "adf_uio.h"
#include "adf_cfg.h"
#include "adf_cfg_user.h"
#include "adf_cfg_device.h"
#include "qdm.h"
#include "adf_transport_internal.h"
#include "adf_svm.h"
#include "adf_uq.h"

#define ADF_UIO_NAME "UIO_%s_%02d_BUNDLE_%02d"
#define ADF_UIO_UQ_NAME "UIO_%s_%02d_SHARED_BUNDLE_%02d"
#define ADF_UIO_DEV_NAME "/dev/uio%i"
#define ADF_UIO_MAP_NAME "ADF_%s_ETR_BUNDLE_%02d"

#define ADF_UIO_GET_NAME(accel_dev) (GET_HW_DATA(accel_dev)->dev_class->name)
#define ADF_UIO_GET_TYPE(accel_dev) (GET_HW_DATA(accel_dev)->dev_class->type)
#define ADF_UIO_GET_BAR(accel_dev)  (GET_HW_DATA(accel_dev)->get_etr_bar_id(\
				     GET_HW_DATA(accel_dev)))

static struct service_hndl adf_uio_hndl;

static inline int adf_uio_get_minor(struct uio_info *info)
{
	struct uio_device *uio_dev = info->uio_dev;

	return uio_dev->minor;
}

/*
 * Structure defining the QAT UIO device private information
 */
struct qat_uio_pci_dev {
	u8 nb_bundles;
};

static inline
void adf_uio_init_bundle_ctrl(struct adf_uio_control_bundle *bundle)
{
	struct uio_info *info = &bundle->uio_info;
	int minor = adf_uio_get_minor(info);
	struct qat_uio_bundle_dev *priv = info->priv;

	snprintf(bundle->name, sizeof(bundle->name), ADF_UIO_DEV_NAME,
		 minor);
	bundle->hardware_bundle_number = priv->hardware_bundle_number;
	bundle->device_minor = minor;
	INIT_LIST_HEAD(&bundle->list);
	priv->bundle = bundle;
	mutex_init(&bundle->lock);
	mutex_init(&bundle->list_lock);
	bundle->csr_addr = info->mem[0].internal_addr;
}

static inline void adf_uio_init_accel_ctrl(struct adf_uio_control_accel *accel,
					   struct adf_accel_dev *accel_dev,
					   unsigned int nb_bundles)
{
	int i;

	accel->nb_bundles = nb_bundles;

	for (i = 0; i < nb_bundles; i++)
		adf_uio_init_bundle_ctrl(accel->bundle[i]);

	accel->first_minor = accel->bundle[0]->device_minor;
	accel->last_minor = accel->bundle[nb_bundles - 1]->device_minor;
}

static u32
adf_map_bank_nr_to_usr_bundle_nr(struct adf_accel_dev *accel_dev,
				 u32 bank_nr)
{
	u32 i = 0;
	int usr_bundle_nr = -1;

	for (i = 0; i <= bank_nr; i++) {
		if (accel_dev->transport->banks[i].type == USER)
			usr_bundle_nr++;
	}
	if (usr_bundle_nr == -1)
		return INVALID_BUNDLE_INDEX;
	return (u32)usr_bundle_nr;
}

static struct adf_uio_control_bundle *adf_ctl_ioctl_bundle(
		struct adf_user_reserve_ring reserve)
{
	struct adf_accel_dev *accel_dev;
	struct adf_uio_control_accel *accel;
	struct adf_uio_control_bundle *bundle;
	u32 num_rings_per_bank = 0;
	u32 usr_bundle_nr = 0;

	accel_dev = adf_devmgr_get_dev_by_id(reserve.accel_id);
	if (!accel_dev) {
		pr_err("QAT: Failed to get accel_dev\n");
		return NULL;
	}
	num_rings_per_bank = accel_dev->hw_device->num_rings_per_bank;

	accel = accel_dev->accel;
	if (!accel) {
		pr_err("QAT: Failed to get accel\n");
		return NULL;
	}

	if (reserve.bank_nr >= GET_MAX_BANKS(accel_dev)) {
		pr_err("QAT: Invalid bank bunber %d\n", reserve.bank_nr);
		return NULL;
	}
	if (reserve.ring_mask & ~((1 << num_rings_per_bank) - 1)) {
		pr_err("QAT: Invalid ring mask %0X\n", reserve.ring_mask);
		return NULL;
	}

	usr_bundle_nr = adf_map_bank_nr_to_usr_bundle_nr(accel_dev,
							 reserve.bank_nr);
	if (usr_bundle_nr == INVALID_BUNDLE_INDEX)
		return NULL;

	bundle = accel->bundle[usr_bundle_nr];

	return bundle;
}

int adf_ctl_ioctl_reserve_ring(unsigned long arg)
{
	struct adf_user_reserve_ring reserve;
	struct adf_uio_control_bundle *bundle;
	struct adf_uio_instance_rings *instance_rings = NULL;
	int pid_entry_found;

	if (copy_from_user(&reserve, (void __user *)arg,
			   sizeof(struct adf_user_reserve_ring))) {
		pr_err("QAT: failed to copy from user.\n");
		return -EFAULT;
	}

	bundle = adf_ctl_ioctl_bundle(reserve);
	if (!bundle) {
		pr_err("QAT: Failed to get bundle\n");
		return -EINVAL;
	}

	if (bundle->rings_used & reserve.ring_mask) {
		pr_err("QAT: Bundle %d, rings 0x%04X already reserved\n",
		       reserve.bank_nr, reserve.ring_mask);
		return -EINVAL;
	}

	/* Find the list entry for this process */
	pid_entry_found = 0;
	mutex_lock(&bundle->list_lock);
	list_for_each_entry(instance_rings, &bundle->list, list) {
		if (instance_rings->user_pid == current->tgid) {
			pid_entry_found = 1;
			break;
		}
	}
	mutex_unlock(&bundle->list_lock);

	if (!pid_entry_found) {
		instance_rings = kzalloc(sizeof(*instance_rings), GFP_KERNEL);
		if (!instance_rings)
			return -ENOMEM;
		instance_rings->user_pid = current->tgid;
		instance_rings->ring_mask = 0;
		mutex_lock(&bundle->list_lock);
		list_add_tail(&instance_rings->list, &bundle->list);
		mutex_unlock(&bundle->list_lock);
	}

	instance_rings->ring_mask |= reserve.ring_mask;
	mutex_lock(&bundle->lock);
	bundle->rings_used |= reserve.ring_mask;
	mutex_unlock(&bundle->lock);

	return 0;
}

int adf_ctl_ioctl_release_ring(unsigned long arg)
{
	struct adf_user_reserve_ring reserve;
	struct adf_uio_control_bundle *bundle;
	struct adf_uio_instance_rings *instance_rings = NULL;
	int pid_entry_found;

	if (copy_from_user(&reserve, (void __user *)arg,
			   sizeof(struct adf_user_reserve_ring))) {
		pr_err("QAT: failed to copy from user.\n");
		return -EFAULT;
	}

	bundle = adf_ctl_ioctl_bundle(reserve);
	if (!bundle) {
		pr_err("QAT: Failed to get bundle\n");
		return -EINVAL;
	}

	/* Find the list entry for this process */
	pid_entry_found = 0;
	mutex_lock(&bundle->list_lock);
	list_for_each_entry(instance_rings, &bundle->list, list) {
		if (instance_rings->user_pid == current->tgid) {
			pid_entry_found = 1;
			break;
		}
	}
	mutex_unlock(&bundle->list_lock);

	if (!pid_entry_found) {
		pr_err("QAT: No ring reservation found for PID %d\n",
		       current->tgid);
		return -EINVAL;
	}

	if ((instance_rings->ring_mask & reserve.ring_mask) !=
			reserve.ring_mask) {
		pr_err("QAT: Attempt to release rings not reserved by this process\n");
		return -EINVAL;
	}

	instance_rings->ring_mask &= ~reserve.ring_mask;
	mutex_lock(&bundle->lock);
	bundle->rings_used &= ~reserve.ring_mask;
	mutex_unlock(&bundle->lock);
	if (!instance_rings->ring_mask) {
		mutex_lock(&bundle->list_lock);
		list_del(&instance_rings->list);
		mutex_unlock(&bundle->list_lock);
		kfree(instance_rings);
	}

	return 0;
}

int adf_ctl_ioctl_enable_ring(unsigned long arg)
{
	struct adf_user_reserve_ring reserve;
	struct adf_uio_control_bundle *bundle;
	struct adf_accel_dev *accel_dev;
	struct adf_hw_device_data *hw_data;

	if (copy_from_user(&reserve, (void __user *)arg,
			   sizeof(struct adf_user_reserve_ring))) {
		pr_err("QAT: failed to copy from user.\n");
		return -EFAULT;
	}

	bundle = adf_ctl_ioctl_bundle(reserve);
	if (!bundle) {
		pr_err("QAT: Failed to get bundle\n");
		return -EINVAL;
	}

	accel_dev = bundle->uio_priv.accel->accel_dev;
	hw_data = accel_dev->hw_device;

	/*
	 * Ensure intcolctl and intflagsrcsel to be in initialized
	 * state if ring_pair_reset is introduced.
	 */
	if (hw_data->config_ring_irq)
		hw_data->config_ring_irq(accel_dev, reserve.bank_nr,
					 reserve.ring_mask);
	mutex_lock(&bundle->lock);
	bundle->rings_enabled |= reserve.ring_mask;
	adf_update_uio_ring_arb(bundle);
	mutex_unlock(&bundle->lock);

	return 0;
}

int adf_ctl_ioctl_disable_ring(unsigned long arg)
{
	struct adf_user_reserve_ring reserve;
	struct adf_uio_control_bundle *bundle;

	if (copy_from_user(&reserve, (void __user *)arg,
			   sizeof(struct adf_user_reserve_ring))) {
		pr_err("QAT: failed to copy from user.\n");
		return -EFAULT;
	}

	bundle = adf_ctl_ioctl_bundle(reserve);
	if (!bundle) {
		pr_err("QAT: Failed to get bundle\n");
		return -EINVAL;
	}

	mutex_lock(&bundle->lock);
	bundle->rings_enabled &= ~reserve.ring_mask;
	adf_update_uio_ring_arb(bundle);
	mutex_unlock(&bundle->lock);

	return 0;
}

static void adf_uio_cleanup_svm_orphan_from_pid(void *priv, u32 pid)
{
	struct uio_info *info = (struct uio_info *)priv;
	struct qat_uio_bundle_dev *uio_priv = info->priv;
	struct adf_uio_control_accel *accel = uio_priv->accel;

	adf_uio_do_cleanup_orphan(info, accel, pid, NULL);
}

static int adf_uio_open(struct uio_info *info, struct inode *inode)
{
	struct qat_uio_bundle_dev *priv = info->priv;
	struct adf_accel_dev *accel_dev = priv->accel->accel_dev;
	u32 bundle_nr = priv->hardware_bundle_number;

	adf_dev_get(accel_dev);

	if (!accel_dev->svm_enabled)
		return 0;

	if (ADF_UQ_GET_Q_MODE(accel_dev) == ADF_UQ_MODE)
		adf_uq_set_mode(accel_dev, bundle_nr, ADF_UQ_MODE_POLLING);
	else
		adf_uq_set_mode(accel_dev, bundle_nr, ADF_UQ_MODE_DISABLE);

	return adf_svm_bind_bank_with_pid(accel_dev,
					  bundle_nr,
					  current->tgid,
					  adf_uio_cleanup_svm_orphan_from_pid,
					  (void *)info);
}

static int adf_uio_release(struct uio_info *info, struct inode *inode)
{
	struct qat_uio_bundle_dev *priv = info->priv;
	struct adf_accel_dev *accel_dev = priv->accel->accel_dev;
	u32 bundle_nr = priv->hardware_bundle_number;

	if (!accel_dev->svm_enabled)
		return 0;

	return adf_svm_unbind_bank_with_pid(accel_dev,
					    current->tgid,
					    bundle_nr);
}

static int adf_uio_remap_bar(struct adf_accel_dev *accel_dev,
			     struct uio_info *info,
			     u8 bundle, u8 bank_offset)
{
	struct adf_bar bar =
		accel_dev->accel_pci_dev.pci_bars[ADF_UIO_GET_BAR(accel_dev)];
	struct adf_hw_csr_info *csr_info =
		&accel_dev->hw_device->csr_info;
	char bar_name[ADF_DEVICE_NAME_LENGTH];
	unsigned int ring_bundle_size, offset;
	void *uq_base_addr = NULL;

	ring_bundle_size = csr_info->ring_bundle_size;
	offset = bank_offset * ring_bundle_size;

	snprintf(bar_name, sizeof(bar_name), ADF_UIO_MAP_NAME,
		 ADF_UIO_GET_NAME(accel_dev), bundle);
	info->mem[0].name = kstrndup(bar_name, sizeof(bar_name), GFP_KERNEL);
	/* Starting from CPM 2.0 HW there is an additional offset
	 * for bundle CSRs
	 */

	info->mem[0].internal_addr = bar.virt_addr + offset;
	info->mem[0].memtype = UIO_MEM_PHYS;
	if (ADF_UQ_GET_Q_MODE(accel_dev) == ADF_UQ_MODE) {
		if (!accel_dev->hw_device->get_uq_base_addr ||
		    accel_dev->hw_device->get_uq_base_addr(accel_dev,
							   &uq_base_addr,
							   bank_offset))
			return -EFAULT;

		info->mem[0].addr = (phys_addr_t)uq_base_addr;
		info->mem[0].size = accel_dev->hw_device->csr_info.uq_size;
	} else {
		info->mem[0].addr =
			bar.base_addr + offset + csr_info->csr_addr_offset;
		info->mem[0].size = ring_bundle_size;
	}

	return 0;
}

void adf_uio_mmap_close_fixup(struct adf_accel_dev *accel_dev)
{
	struct adf_uio_control_accel *accel;
	int i, nb_bundles = accel_dev->uiodev->nb_bundles;

	mutex_lock(&uio_lock);
	accel = accel_dev->accel;

	for (i = 0; i < nb_bundles; i++) {
		if (!accel->bundle[i]->vma)
			continue;
		adf_uio_bundle_unref(accel->bundle[i]);
		adf_uio_accel_unref(accel);
		adf_dev_put(accel_dev);
	}
	mutex_unlock(&uio_lock);
}

/*   adf memory map operatoin   */
/*   in the close operation, we do the ring clean up if needed  */
static void adf_uio_mmap_close(struct vm_area_struct *vma)
{
	struct uio_info *info = vma->vm_private_data;
	struct qat_uio_bundle_dev *priv;

	if (!info)
		return;

	mutex_lock(&uio_lock);
	priv = info->priv;

	if (!priv->bundle->vma) {
		mutex_unlock(&uio_lock);
		return;
	}
	/* Ensure that an uncontrolled device removal did not occur */
	if (priv->accel && priv->accel->accel_dev) {
		adf_uio_do_cleanup_orphan(info, priv->accel,
					  current->tgid,
					  current->comm);
		adf_dev_put(priv->accel->accel_dev);
	}

	/* Decrease a reference counter for the accel kobj. */
	adf_uio_accel_unref(priv->accel);
	/* Decrease a reference counter for the bundle kobj. */
	adf_uio_bundle_unref(priv->bundle);

	priv->bundle->vma = NULL;
	mutex_unlock(&uio_lock);
}

static const struct vm_operations_struct adf_uio_mmap_operation = {
	.close = adf_uio_mmap_close,
#ifdef CONFIG_HAVE_IOREMAP_PROT
	.access = generic_access_phys,
#endif
};

static int find_mem_index(struct vm_area_struct *vma)
{
	struct uio_info *info = vma->vm_private_data;

	if (!info)
		return -1;

	if (vma->vm_pgoff < MAX_UIO_MAPS) {
		if (!info->mem[vma->vm_pgoff].size)
			return -1;
		return (int)vma->vm_pgoff;
	}

	return -EINVAL;
}

static int adf_uio_mmap(struct uio_info *info, struct vm_area_struct *vma)
{
	int mi;
	struct uio_mem *mem;
	struct qat_uio_bundle_dev *priv = info->priv;

	if (vma->vm_start > vma->vm_end)
		return -EINVAL;

	vma->vm_private_data = info;
	mi = find_mem_index(vma);
	if (mi < 0)
		return -EINVAL;

	/*  only support PHYS type here  */
	if (info->mem[mi].memtype != UIO_MEM_PHYS)
		return -EINVAL;

	if ((vma->vm_end - vma->vm_start) > info->mem[mi].size) {
		pr_err("QAT: requested size out of range.\n");
		return -EINVAL;
	}

	/* Increment a reference counter for the accel object. */
	adf_uio_accel_ref(priv->accel);
	/* Increment a reference counter for the bundle object. */
	adf_uio_bundle_ref(priv->bundle);
	priv->bundle->vma = vma;
	mem = info->mem + mi;
	vma->vm_ops = &adf_uio_mmap_operation;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	return remap_pfn_range(vma,
			       vma->vm_start,
			       mem->addr >> PAGE_SHIFT,
			       vma->vm_end - vma->vm_start,
			       vma->vm_page_prot);
}

static irqreturn_t adf_uio_isr_bundle(int irq, struct uio_info *info)
{
	struct qat_uio_bundle_dev *priv = info->priv;
	struct adf_accel_dev *accel_dev = priv->accel->accel_dev;
	struct adf_etr_data *etr_data = accel_dev->transport;
	struct adf_etr_bank_data *bank =
		&etr_data->banks[priv->hardware_bundle_number];
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct adf_hw_csr_ops *csr_ops = &hw_data->csr_info.csr_ops;
	int int_active_bundles = 0;

	/* For vf, all bundls share the same MSI irq, but only the active */
	/* bundle's event will be handled */
	if (accel_dev->is_vf) {
		if (hw_data->get_int_active_bundles)
			int_active_bundles =
				hw_data->get_int_active_bundles(accel_dev);

		/* Interrupt happened on this bundle */
		if (int_active_bundles & (1 << bank->bank_number)) {
			csr_ops->write_csr_int_flag_and_col(bank->csr_addr,
							    bank->bank_number,
							    0);
		}

		/* Wake up the process to re-enable the shared MSI. This takes
		 * place even so it did not happen on this bundle.
		 */
		return IRQ_HANDLED;
	}
	csr_ops->write_csr_int_flag_and_col(bank->csr_addr,
					    bank->bank_number,
					    0);

	return IRQ_HANDLED;
}

static int adf_uio_init_bundle_dev(struct adf_accel_dev *accel_dev,
				   u8 bundle, u32 hw_bundle_number)
{
	char name[ADF_DEVICE_NAME_LENGTH];
	struct uio_info *info =
		&accel_dev->accel->bundle[bundle]->uio_info;
	struct qat_uio_bundle_dev *priv =
		&accel_dev->accel->bundle[bundle]->uio_priv;
	struct adf_accel_pci *pci_dev_info = &accel_dev->accel_pci_dev;
	struct adf_etr_data *etr_data = accel_dev->transport;
	struct adf_etr_bank_data *bank = &etr_data->banks[hw_bundle_number];
	struct adf_hw_csr_ops *csr_ops =
			&accel_dev->hw_device->csr_info.csr_ops;
	unsigned int irq_flags = 0;
	u32 enable_int_col_mask = csr_ops->get_int_col_ctl_enable_mask();

	priv->accel = accel_dev->accel;
	priv->hardware_bundle_number = hw_bundle_number;

	if (adf_uio_remap_bar(accel_dev, info, bundle,
			      priv->hardware_bundle_number))
		return -ENOMEM;

	snprintf(name, sizeof(name),
		 (ADF_UQ_GET_Q_MODE(accel_dev) == ADF_UQ_MODE) ?
		 ADF_UIO_UQ_NAME : ADF_UIO_NAME,
		 ADF_UIO_GET_NAME(accel_dev), accel_dev->accel_id, bundle);
	info->name = kstrndup(name, sizeof(name), GFP_KERNEL);
	info->version = ADF_DRV_VERSION;
	info->priv = priv;
	info->open = adf_uio_open;
	info->release = adf_uio_release;
	info->handler = adf_uio_isr_bundle;
	info->mmap = adf_uio_mmap;

	/* Use MSIX vector for PF and the proper IRQ for VF */
	if (!accel_dev->is_vf) {
		struct msix_entry *msixe = pci_dev_info->msix_entries.entries;
		u32 msix_num_entries = pci_dev_info->msix_entries.num_entries;

		if (hw_bundle_number > msix_num_entries) {
			dev_err(&GET_DEV(accel_dev),
				"Requested entry out of range\n");
			return -EINVAL;
		}

		info->irq = msixe[hw_bundle_number].vector;
	} else {
		/* This is the case for VQAT, and we need to add 1 to
		 * hw_bundle_number because the first msix entry for
		 * VQAT is MISC interrupt.
		 */
		if (!accel_dev->pf.vf_info) {
			struct msix_entry *msixe = pci_dev_info->msix_entries.entries;
			u32 msix_num_entries = pci_dev_info->msix_entries.num_entries;

			if (hw_bundle_number + 1 > msix_num_entries) {
				dev_err(&GET_DEV(accel_dev),
					"Requested entry out of range\n");
				return -EINVAL;
			}

			info->irq = msixe[hw_bundle_number + 1].vector;
		} else {
			struct pci_dev *pdev = accel_to_pci_dev(accel_dev);

			info->irq = pdev->irq;
		}

		/* In VF we are sharing the interrupt */
		irq_flags = IRQF_SHARED;
	}
	irq_flags |= IRQF_ONESHOT;

	info->irq_flags = irq_flags;

	/* There is no need to set a hint for IRQs affinity cause the CPU
	 * affinity will be set from user space in adf_ctl
	 */

	/* Disable interrupts for this bundle but set the coalescence timer so
	 * that interrupts can be enabled on demand when creating a trans handle
	 */
	csr_ops->write_csr_int_col_en(bank->csr_addr, hw_bundle_number,
					 0);
	csr_ops->write_csr_int_col_ctl(bank->csr_addr, hw_bundle_number,
				       bank->irq_coalesc_timer |
					       enable_int_col_mask);

	if (uio_register_device(&accel_to_pci_dev(accel_dev)->dev, info))
		return -ENODEV;
	return 0;
}

static void adf_uio_del_bundle_dev(struct adf_accel_dev *accel_dev,
				   u32 nb_bundles)
{
	u32 i;

	for (i = 0; i < nb_bundles; i++) {
		/* Decrease a reference counter for the bundle kobj. */
		adf_uio_sysfs_bundle_delete(accel_dev, i);
	}
}

static void adf_uio_unregiser_dev(struct adf_accel_dev *accel_dev,
				  u32 nb_bundles)
{
	u32 i;

	for (i = 0; i < nb_bundles; i++) {
		struct uio_info *info = &accel_dev->accel->bundle[i]->uio_info;

		irq_set_affinity_hint(info->irq, NULL);
		uio_unregister_device(info);
	}
}

static void adf_uio_clean(struct adf_accel_dev *accel_dev,
			  struct qat_uio_pci_dev *uiodev)
{
	/*
	 * PF belongs to AUX domain by default, so during UIO initialization on
	 * PF, we don't add PF to QAT created domain(QDM). Same reason in UIO
	 * clean, we don't need to detach PF from the QDM.
	 */
	if (uiodev->nb_bundles && accel_dev->is_vf)
		qdm_detach_device(&GET_DEV(accel_dev));
	adf_uio_unregiser_dev(accel_dev, uiodev->nb_bundles);
	adf_uio_del_bundle_dev(accel_dev, uiodev->nb_bundles);
	kfree(uiodev);
	accel_dev->uiodev = NULL;
}

static u32 adf_get_num_user_bundle(struct adf_accel_dev *accel_dev)
{
	struct adf_etr_data *priv_data = accel_dev->transport;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u32 i, nb_bundles = 0;

	for (i = 0; i < hw_data->num_banks; i++) {
		if (priv_data->banks[i].type == USER)
			nb_bundles++;
	}

	return nb_bundles;
}

static u32 adf_get_num_kernel_bundle(struct adf_accel_dev *accel_dev)
{
	struct adf_etr_data *priv_data = accel_dev->transport;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u32 i, nb_bundles = 0;

	for (i = 0; i < hw_data->num_banks; i++) {
		if (priv_data->banks[i].type == KERNEL)
			nb_bundles++;
	}

	return nb_bundles;
}

int adf_uio_register(struct adf_accel_dev *accel_dev)
{
	struct qat_uio_pci_dev *uiodev;
	struct pci_dev *pdev = accel_to_pci_dev(accel_dev);
	u32 i, j, nb_bundles = 0;
	u32 hw_bundle_number;

	nb_bundles = adf_get_num_user_bundle(accel_dev);
	accel_dev->accel->num_ker_bundles =
		adf_get_num_kernel_bundle(accel_dev);

	if (nb_bundles) {
		uiodev = kzalloc(sizeof(*uiodev), GFP_KERNEL);
		if (!uiodev)
			return -ENOMEM;

		mutex_lock(&uio_lock);
		uiodev->nb_bundles = nb_bundles;

		for (i = 0; i < nb_bundles; i++) {
			if (adf_uio_sysfs_bundle_create(pdev, i,
							accel_dev->accel))
				goto fail_bundle_create;
		}
		for (j = 0; j < nb_bundles; j++) {
			hw_bundle_number = adf_find_hw_bundle_by_usr(accel_dev,
								     j);
			if (hw_bundle_number == INVALID_BUNDLE_INDEX)
				goto fail_init_bundle;
			if (adf_uio_init_bundle_dev(accel_dev, j,
						    hw_bundle_number))
				goto fail_init_bundle;
		}

		adf_uio_init_accel_ctrl(accel_dev->accel, accel_dev,
					nb_bundles);
		accel_dev->uiodev = uiodev;

		if (accel_dev->is_vf && qdm_attach_device(&GET_DEV(accel_dev)))
			goto fail_unregister;
		mutex_unlock(&uio_lock);
	}
	return 0;

fail_unregister:
	accel_dev->uiodev = NULL;
fail_init_bundle:
	adf_uio_unregiser_dev(accel_dev, j);
fail_bundle_create:
	adf_uio_del_bundle_dev(accel_dev, i);
	kfree(uiodev);
	dev_err(&accel_to_pci_dev(accel_dev)->dev,
		"Failed to register UIO devices\n");
	mutex_unlock(&uio_lock);
	return -ENODEV;
}

void adf_uio_remove(struct adf_accel_dev *accel_dev)
{
	mutex_lock(&uio_lock);
	if (accel_dev->uiodev)
		adf_uio_clean(accel_dev, accel_dev->uiodev);
	mutex_unlock(&uio_lock);
}

static int adf_uio_event_handler(struct adf_accel_dev *accel_dev,
				 enum adf_event event)
{
	int ret = 0;
	struct device *dev = &GET_DEV(accel_dev);
	char *event_str = NULL;
	char *dev_id = NULL;
	char *envp[3];

	switch (event) {
	case ADF_EVENT_INIT:
		return ret;
	case ADF_EVENT_SHUTDOWN:
		return ret;
	case ADF_EVENT_RESTARTING:
		event_str = "qat_event=restarting";
		break;
	case ADF_EVENT_RESTARTED:
		event_str = "qat_event=restarted";
		break;
	case ADF_EVENT_START:
		return ret;
	case ADF_EVENT_STOP:
		return ret;
	case ADF_EVENT_ERROR:
		event_str = "qat_event=error";
		break;
	case ADF_EVENT_SLA_UPDATE:
		return ret;
	default:
		return -EINVAL;
	}

	dev_id = kasprintf(GFP_ATOMIC, "accelid=%d", accel_dev->accel_id);
	if (!dev_id) {
		dev_err(&GET_DEV(accel_dev), "Failed to allocate memory\n");
		return -ENOMEM;
	}

	envp[0] = event_str;
	envp[1] = dev_id;
	envp[2] = NULL;
	ret = kobject_uevent_env(&dev->kobj, KOBJ_CHANGE, envp);
	if (ret) {
		dev_err(&GET_DEV(accel_dev), "Failed to send event %s\n",
			event_str);
		goto end;
	}

end:
	kfree(dev_id);

	return ret;
}

int adf_uio_service_register(void)
{
	memset(&adf_uio_hndl, 0, sizeof(adf_uio_hndl));
	adf_uio_hndl.event_hld = adf_uio_event_handler;
	adf_uio_hndl.name = "adf_event_handler";
	return adf_service_register(&adf_uio_hndl);
}

int adf_uio_service_unregister(void)
{
	return adf_service_unregister(&adf_uio_hndl);
}
