// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2023 Intel Corporation */
#include <linux/version.h>
#include "adf_common_drv.h"
#include "adf_uq.h"

#if (KERNEL_VERSION(5, 7, 0) <= LINUX_VERSION_CODE)
static int adf_uq_do_set_mode(struct adf_accel_dev *accel_dev,
			      u32 bank_number,
			      u8 mode)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct adf_bar *etr_bar = NULL;
	void __iomem *csr = NULL;
	struct adf_hw_csr_ops *csr_ops = NULL;

	if (bank_number >= hw_data->num_banks)
		return -EFAULT;

	etr_bar = &GET_BARS(accel_dev)[hw_data->get_etr_bar_id(hw_data)];
	csr = etr_bar->virt_addr;
	csr_ops = &hw_data->csr_info.csr_ops;

	if (!csr_ops->set_uq_mode)
		return -EFAULT;

	return csr_ops->set_uq_mode(csr, bank_number, mode);
}

int adf_uq_set_mode(struct adf_accel_dev *accel_dev,
		    u32 bank_number,
		    u8 mode)
{
	struct adf_accel_dev *pf = NULL;
	int vf_id = 0;
	struct adf_accel_pci *accel_pci_dev = &accel_dev->accel_pci_dev;
	struct pci_dev *pdev = accel_pci_dev->pci_dev;
	u32 bank_number_pf = 0;

	if (bank_number >= accel_dev->hw_device->num_banks)
		return -EINVAL;

	if (!accel_dev->is_vf) {
		pf = accel_dev;
		bank_number_pf = bank_number;
		goto set_uq_mode;
	}

	/* For VF in the guest, UQ is not supported */
	pf = adf_devmgr_pci_to_accel_dev(pdev->physfn);
	if (!pf)
		return -EFAULT;

	/* For VF in the host, set ringmode register directly via PF */
	vf_id = adf_get_vf_id(accel_dev, pf);

	if (vf_id < 0 || vf_id >= pci_num_vf(accel_to_pci_dev(pf))) {
		dev_err(&GET_DEV(accel_dev),
			"Invalid VF ID: %d\n", vf_id + 1);
		return -EFAULT;
	}

	bank_number_pf =
		pf->hw_device->num_banks_per_vf * vf_id + bank_number;
set_uq_mode:
	return adf_uq_do_set_mode(pf,
				  bank_number_pf,
				  mode);
}
#else
int adf_uq_set_mode(struct adf_accel_dev *accel_dev,
		    u32 bank_number,
		    u8 mode)
{
	return 0;
}

#endif
