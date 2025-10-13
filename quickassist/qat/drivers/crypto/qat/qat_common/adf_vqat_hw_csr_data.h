/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2019 - 2020 Intel Corporation */
#ifndef ADF_VQAT_HW_CSR_DATA_H_
#define ADF_VQAT_HW_CSR_DATA_H_

struct adf_hw_csr_info;
void vqat_init_hw_csr_info(struct adf_hw_csr_info *csr_info);
int vqat_ring_pair_reset(void __iomem *csr, u32 bank_number);

#endif /* ADF_VQAT_HW_CSR_DATA_H_ */
