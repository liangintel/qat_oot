/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2019, 2021 Intel Corporation */
#ifndef ADF_GEN4_HW_CSR_DATA_H_
#define ADF_GEN4_HW_CSR_DATA_H_
#include <linux/io.h>

#define ADF_GEN4_PM_INTERRUPT (0x50A028)

#define ADF_GEN4_PM_THR_STS      BIT(0)
#define ADF_GEN4_PM_IDLE_STS     BIT(1)
#define ADF_GEN4_PM_FM_INT_STS   BIT(2)

#define ADF_GEN4_PM_IDLE_INT_EN BIT(18)
#define ADF_GEN4_PM_THROTTLE_INT_EN BIT(19)
#define ADF_GEN4_PM_DRV_ACTIVE BIT(20)

#define ADF_GEN4_PM_INT_STS_MASK \
	(ADF_GEN4_PM_THR_STS |   \
	 ADF_GEN4_PM_IDLE_STS |  \
	 ADF_GEN4_PM_FM_INT_STS)

#define ADF_GEN4_SMIAPF_RP_X0_MASK_OFFSET (0x41A040)
#define ADF_GEN4_SMIAPF_RP_X1_MASK_OFFSET (0x41A044)
#define ADF_GEN4_SMIAPF_MASK_OFFSET (0x41A084)

/* VF2PF interrupt source register */
#define ADF_GEN4_VM2PF_SOU (0x41A180)
/* VF2PF interrupt mask register */
#define ADF_GEN4_VM2PF_MSK (0x41A1C0)

#define ADF_GEN4_PF2VM_OFFSET(i)	(0x40B010 + ((i) * 0x20))
#define ADF_GEN4_VM2PF_OFFSET(i)	(0x40B014 + ((i) * 0x20))
#define ADF_GEN4_VINTMSK_OFFSET(i)	(0x40B004 + ((i) * 0x20))
#define ADF_GEN4_VINTMSKPF2VM_OFFSET(i)	(0x40B00C + ((i) * 0x20))

struct adf_hw_csr_info;
void gen4_init_hw_csr_info(struct adf_hw_csr_info *csr_info);
int gen4_ring_pair_reset(void __iomem *csr, u32 bank_number);
int gen4_ring_pair_drain(void __iomem *csr, u32 bank_number, int timeout_ms);

#endif /* ADF_GEN4_HW_CSR_DATA_H_ */
