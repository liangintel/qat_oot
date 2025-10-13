/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2019 Intel Corporation */
#ifndef ADF_GEN4VF_HW_CSR_DATA_H_
#define ADF_GEN4VF_HW_CSR_DATA_H_

#define ADF_GEN4VF_VINTMSK_OFFSET	0x4
#define ADF_GEN4VF_VINTMSKPF2VM_OFFSET	0x1004

struct adf_hw_csr_info;
void gen4vf_init_hw_csr_info(struct adf_hw_csr_info *csr_info);

#endif /* ADF_GEN4VF_HW_CSR_DATA_H_ */
