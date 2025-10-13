/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2019 Intel Corporation */
#ifndef ADF_GEN2_HW_CSR_DATA_H_
#define ADF_GEN2_HW_CSR_DATA_H_

#define ADF_GEN2_SMIAPF0_MASK_OFFSET (0x3A000 + 0x28)
#define ADF_GEN2_SMIAPF1_MASK_OFFSET (0x3A000 + 0x30)

#define ADF_GEN2_SMIA0_MASK 0xFFFF
#define ADF_GEN2_SMIA1_MASK 0x1

#define ADF_GEN2_PF2VF_OFFSET(i)	(0x3A000 + 0x280 + ((i) * 0x04))
#define ADF_GEN2_VINTMSK_OFFSET(i)	(0x3A000 + 0x200 + ((i) * 0x04))

#define ADF_GEN2_ERRSOU3 (0x3A000 + 0x0C)
#define ADF_GEN2_ERRMSK3 (0x3A000 + 0x1C)

#define ADF_GEN2_ERRSOU3_VF2PF(errsou3) (((errsou3) & 0x01FFFE00) >> 9)
#define ADF_GEN2_ERRMSK3_VF2PF(vf_mask) (((vf_mask) & 0xFFFF) << 9)

#define ADF_GEN2_SHINTMASKSSM(i) ((i) * 0x4000 + 0x1018)

#define ADF_GEN2_ENABLE_SLICE_HANG 0x000000

struct adf_hw_csr_info;
void gen2_init_hw_csr_info(struct adf_hw_csr_info *csr_info);

#endif /* ADF_GEN2_HW_CSR_DATA_H_ */
