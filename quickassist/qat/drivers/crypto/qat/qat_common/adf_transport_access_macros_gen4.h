/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2019 - 2021 Intel Corporation */
#ifndef ADF_TRANSPORT_ACCESS_MACROS_GEN4_H
#define ADF_TRANSPORT_ACCESS_MACROS_GEN4_H

#include "adf_accel_devices.h"
#include "adf_transport_access_macros.h"

#define ADF_RINGS_PER_INT_SRCSEL_GEN4 2
#define ADF_BANK_INT_SRC_SEL_MASK_GEN4 0x44UL
#define ADF_BANK_INT_FLAG_CLEAR_MASK_GEN4 0x3
#define ADF_RING_BUNDLE_SIZE_GEN4 0x2000
#define ADF_RING_CSR_ADDR_OFFSET_GEN4 0x100000
#define ADF_RING_CSR_RING_CONFIG_GEN4 0x1000
#define ADF_RING_CSR_RING_LBASE_GEN4 0x1040
#define ADF_RING_CSR_RING_UBASE_GEN4 0x1080
#define ADF_RING_CSR_EXP_STAT_GEN4 0x188
#define ADF_RING_CSR_EXP_INT_EN_GEN4 0x18C
#define ADF_UQ_WINDOW_SIZE_GEN4 0x2000
#define ADF_UQ_OFFSET_UNPRIV_GEN4 0

#define BUILD_RING_BASE_ADDR_GEN4(addr, size) \
	((((addr) >> 6) & (0xFFFFFFFFFFFFFFFFULL << (size))) << 6)
#define READ_CSR_RING_HEAD_GEN4(csr_base_addr, bank, ring) \
	ADF_CSR_RD((u8 *)(csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET_GEN4, \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_RING_HEAD + ((ring) << 2))
#define READ_CSR_RING_TAIL_GEN4(csr_base_addr, bank, ring) \
	ADF_CSR_RD((u8 *)(csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET_GEN4, \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_RING_TAIL + ((ring) << 2))
#define READ_CSR_STAT_GEN4(csr_base_addr, bank) \
	ADF_CSR_RD((u8 *)(csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET_GEN4, \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_STAT)
#define READ_CSR_UO_STAT_GEN4(csr_base_addr, bank) \
	ADF_CSR_RD((u8 *)(csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET_GEN4, \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_UO_STAT)
#define READ_CSR_E_STAT_GEN4(csr_base_addr, bank) \
	ADF_CSR_RD((u8 *)(csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET_GEN4, \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_E_STAT)
#define READ_CSR_NE_STAT_GEN4(csr_base_addr, bank) \
	ADF_CSR_RD((u8 *)(csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET_GEN4, \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_NE_STAT)
#define READ_CSR_NF_STAT_GEN4(csr_base_addr, bank) \
	ADF_CSR_RD((u8 *)(csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET_GEN4, \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_NF_STAT)
#define READ_CSR_F_STAT_GEN4(csr_base_addr, bank) \
	ADF_CSR_RD((u8 *)(csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET_GEN4, \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_F_STAT)
#define READ_CSR_C_STAT_GEN4(csr_base_addr, bank) \
	ADF_CSR_RD((u8 *)(csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET_GEN4, \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_C_STAT)
#define READ_CSR_EXP_STAT_GEN4(csr_base_addr, bank) \
	ADF_CSR_RD((u8 *)(csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET_GEN4, \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_EXP_STAT_GEN4)
#define READ_CSR_EXP_INT_EN_GEN4(csr_base_addr, bank) \
	ADF_CSR_RD((u8 *)(csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET_GEN4, \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_EXP_INT_EN_GEN4)
#define WRITE_CSR_EXP_INT_EN_GEN4(csr_base_addr, bank, value) \
	ADF_CSR_WR((u8 *)(csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET_GEN4, \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_EXP_INT_EN_GEN4, value)
#define READ_CSR_RING_CONFIG_GEN4(csr_base_addr, bank, ring) \
	ADF_CSR_RD((u8 *)(csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET_GEN4, \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_RING_CONFIG_GEN4 + ((ring) << 2))
#define WRITE_CSR_RING_CONFIG_GEN4(csr_base_addr, bank, ring, value) \
	ADF_CSR_WR((u8 *)(csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET_GEN4, \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_RING_CONFIG_GEN4 + ((ring) << 2), value)
#define WRITE_CSR_RING_BASE_GEN4(csr_base_addr, bank, ring, value)	\
do {									\
	void __iomem *_csr_base_addr = csr_base_addr;			\
	u32 _bank = bank;						\
	u32 _ring = ring;						\
	dma_addr_t _value = value;					\
	u32 l_base = 0, u_base = 0;					\
	l_base = (u32)((_value) & 0xFFFFFFFF);				\
	u_base = (u32)(((_value) & 0xFFFFFFFF00000000ULL) >> 32);	\
	ADF_CSR_WR((u8 *)(_csr_base_addr) +				\
		ADF_RING_CSR_ADDR_OFFSET_GEN4,				\
		(ADF_RING_BUNDLE_SIZE_GEN4 * (_bank)) +			\
		ADF_RING_CSR_RING_LBASE_GEN4 + ((_ring) << 2), l_base);	\
	ADF_CSR_WR((u8 *)(_csr_base_addr) +				\
		ADF_RING_CSR_ADDR_OFFSET_GEN4,				\
		(ADF_RING_BUNDLE_SIZE_GEN4 * (_bank)) +			\
		ADF_RING_CSR_RING_UBASE_GEN4 + ((_ring) << 2), u_base);	\
} while (0)

static inline u64 read_base_gen4(void __iomem *csr_base_addr,
				 u32 bank,
				 u32 ring)
{
	u32 l_base, u_base;
	u64 addr;

	l_base = ADF_CSR_RD(csr_base_addr, (ADF_RING_BUNDLE_SIZE_GEN4 * bank) +
			    ADF_RING_CSR_RING_LBASE_GEN4 + (ring << 2));
	u_base = ADF_CSR_RD(csr_base_addr, (ADF_RING_BUNDLE_SIZE_GEN4 * bank) +
			    ADF_RING_CSR_RING_UBASE_GEN4 + (ring << 2));

	addr = (u64)l_base & 0x00000000FFFFFFFFULL;
	addr |= (u64)u_base << 32 & 0xFFFFFFFF00000000ULL;

	return addr;
}

#define READ_CSR_RING_BASE_GEN4(csr_base_addr, bank, ring) \
	read_base_gen4((void *)((u8 *)(csr_base_addr) + \
		       ADF_RING_CSR_ADDR_OFFSET_GEN4), (bank), (ring))

#define WRITE_CSR_RING_HEAD_GEN4(csr_base_addr, bank, ring, value) \
	ADF_CSR_WR((u8 *)(csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET_GEN4, \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_RING_HEAD + ((ring) << 2), value)
#define WRITE_CSR_RING_TAIL_GEN4(csr_base_addr, bank, ring, value) \
	ADF_CSR_WR((u8 *)(csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET_GEN4, \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_RING_TAIL + ((ring) << 2), value)
#define READ_CSR_INT_EN_GEN4(csr_base_addr, bank) \
	ADF_CSR_RD((u8 *)(csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET_GEN4, \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_INT_FLAG_EN)
#define WRITE_CSR_INT_EN_GEN4(csr_base_addr, bank, value) \
	ADF_CSR_WR((u8 *)(csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET_GEN4, \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_INT_FLAG_EN, (value))
#define READ_CSR_INT_FLAG_GEN4(csr_base_addr, bank) \
	ADF_CSR_RD((u8 *)(csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET_GEN4, \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_INT_FLAG)
#define WRITE_CSR_INT_FLAG_GEN4(csr_base_addr, bank, value) \
	ADF_CSR_WR((u8 *)(csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET_GEN4, \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_INT_FLAG, (value))
#define READ_CSR_INT_SRCSEL_GEN4(csr_base_addr, bank, idx) \
	ADF_CSR_RD((u8 *)(csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET_GEN4, \
	(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
	(ADF_RING_CSR_INT_SRCSEL + ((idx) * ADF_RING_CSR_NEXT_INT_SRCSEL)))
#define WRITE_CSR_INT_SRCSEL_GEN4(csr_base_addr, bank, idx, value) \
	ADF_CSR_WR((u8 *)(csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET_GEN4, \
	(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
	(ADF_RING_CSR_INT_SRCSEL + ((idx) * ADF_RING_CSR_NEXT_INT_SRCSEL)), \
	(value))
#define READ_CSR_INT_COL_EN_GEN4(csr_base_addr, bank) \
	ADF_CSR_RD((u8 *)(csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET_GEN4, \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_INT_COL_EN)
#define WRITE_CSR_INT_COL_EN_GEN4(csr_base_addr, bank, value) \
	ADF_CSR_WR((u8 *)(csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET_GEN4, \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_INT_COL_EN, (value))
#define READ_CSR_INT_COL_CTL_GEN4(csr_base_addr, bank) \
	ADF_CSR_RD((u8 *)(csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET_GEN4, \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_INT_COL_CTL)
#define WRITE_CSR_INT_COL_CTL_GEN4(csr_base_addr, bank, value) \
	ADF_CSR_WR((u8 *)(csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET_GEN4, \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_INT_COL_CTL, (value))
#define READ_CSR_INT_FLAG_AND_COL_GEN4(csr_base_addr, bank) \
	ADF_CSR_RD((u8 *)(csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET_GEN4, \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_INT_FLAG_AND_COL)
#define WRITE_CSR_INT_FLAG_AND_COL_GEN4(csr_base_addr, bank, value) \
	ADF_CSR_WR((u8 *)(csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET_GEN4, \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_INT_FLAG_AND_COL, (value))

#define READ_CSR_RING_SRV_ARB_EN_GEN4(csr_base_addr, bank) \
	ADF_CSR_RD((u8 *)(csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET_GEN4, \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_RING_SRV_ARB_EN)
#define WRITE_CSR_RING_SRV_ARB_EN_GEN4(csr_base_addr, bank, value) \
	ADF_CSR_WR((u8 *)(csr_base_addr) + ADF_RING_CSR_ADDR_OFFSET_GEN4, \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_RING_SRV_ARB_EN, (value))

#define ADF_WQM_CSR_WQ_BASE(bank) \
	(ADF_RING_CSR_ADDR_OFFSET_GEN4 + ADF_RING_BUNDLE_SIZE_GEN4 * (bank))

#define ADF_WQM_CSR_RPRESETCTL_SHIFT		0
#define ADF_WQM_CSR_RPRESETCTL_DRAIN_SHIFT	2
#define ADF_WQM_CSR_RPRESETCTL_MASK	(BIT(3) - 1)
#define ADF_WQM_CSR_RPRESETCTL(bank)	(0x6000 + ((bank) << 3))
#define ADF_WQM_CSR_RPRESETSTS_SHIFT	0
#define ADF_WQM_CSR_RPRESETSTS_MASK	(BIT(0))
#define ADF_WQM_CSR_RPRESETSTS(bank)	(ADF_WQM_CSR_RPRESETCTL(bank) + 4)
#define	RING_LEVEL_RESET	0x1
#define	PASID_LEVEL_RESET	0x2
#define	RING_LEVEL_ABORT	0x3
#define ADF_GET_RPRESETCTL_VALUE(val) \
		((val) >> ADF_WQM_CSR_RPRESETCTL_SHIFT & \
		ADF_WQM_CSR_RPRESETCTL_MASK)

#define ADF_4XXX_MSIX_RTTABLE_OFFSET(i) (0x409000 + ((i) * 0x04))
#define ADF_WQM_CSR_MSIXLTBL(bank) (0x400000 + ((bank) << 4))
#define ADF_WQM_CSR_MSIXUTBL(bank) (ADF_WQM_CSR_MSIXLTBL(bank) + 0x4)
#define ADF_WQM_CSR_MSIXDATA(bank) (ADF_WQM_CSR_MSIXLTBL(bank) + 0x8)
#define ADF_WQM_CSR_MSIXVECCNTL(bank) (ADF_WQM_CSR_MSIXLTBL(bank) + 0xC)
#define ADF_WQM_CSR_IMSMSIXLTBL(bank) (0x410000 + ((bank) << 4))
#define ADF_WQM_CSR_IMSMSIXUTBL(bank) (ADF_WQM_CSR_IMSMSIXLTBL(bank) + 0x4)
#define ADF_WQM_CSR_IMSMSIXDATA(bank) (ADF_WQM_CSR_IMSMSIXLTBL(bank) + 0x8)
#define ADF_WQM_CSR_IMSMSIXVECCNTL(bank) (ADF_WQM_CSR_IMSMSIXLTBL(bank) + 0xC)

#define ADF_WQM_CSR_RPINTSTS(bank)	(0x200000 + ((bank) << 12))
#define ADF_WQM_CSR_RPINTMSK(bank)	(0x200004 + ((bank) << 12))
#define ADF_WQM_CSR_RPINT_MASK		(BIT(0))
#define ADF_WQM_CSR_RP_IDX_TX		0
#define ADF_WQM_CSR_RP_IDX_RX		1

#define ADF_WQM_CSR_PASIDCTL(bank)	(0x4000 + ((bank) << 4))
#define ADF_PASIDCTL_ADI_ENABLE_RING	BIT(20)
#define ADF_PASIDCTL_AT_ENABLE_RING	BIT(21)
#define ADF_PASIDCTL_PASID_ENABLE_RING	BIT(22)
#define ADF_PASIDCTL_PRIV_ENABLE_RING	BIT(23)
#define ADF_PASIDCTL_ADI_ENABLE_PLD	BIT(24)
#define ADF_PASIDCTL_AT_ENABLE_PLD	BIT(25)
#define ADF_PASIDCTL_PASID_ENABLE_PLD	BIT(26)
#define ADF_PASIDCTL_PRIV_ENABLE_PLD	BIT(27)
#define ADF_PASIDCTL_PASID_SHIFT	0
#define ADF_PASIDCTL_PASID_MASK		(BIT(20) - 1)
#define ADF_GET_PASIDCTL_PASID_VALUE(val) \
	((val) >> ADF_PASIDCTL_PASID_SHIFT & ADF_PASIDCTL_PASID_MASK)


#define ADF_PASIDCTL_MASK_ADIMODE	(ADF_PASIDCTL_ADI_ENABLE_RING |	\
					 ADF_PASIDCTL_PASID_ENABLE_RING | \
					 ADF_PASIDCTL_ADI_ENABLE_PLD | \
					 ADF_PASIDCTL_PASID_ENABLE_PLD)

#define ADF_PASIDCTL_ENABLE_PASID	(ADF_PASIDCTL_PASID_ENABLE_PLD | \
					ADF_PASIDCTL_PASID_ENABLE_RING)

#define ADF_PASIDCTL_ENABLE_ADI		(ADF_PASIDCTL_ADI_ENABLE_RING | \
					ADF_PASIDCTL_ADI_ENABLE_PLD)

#define ADF_PASIDCTL_ENABLE_AT		(ADF_PASIDCTL_AT_ENABLE_PLD | \
					ADF_PASIDCTL_AT_ENABLE_RING)

#define ADF_PASIDCTL_ENABLE_PRIV	(ADF_PASIDCTL_PRIV_ENABLE_PLD | \
					ADF_PASIDCTL_PRIV_ENABLE_RING)
#define ADF_WQM_CSR_RINGMODECTL(bank)	(0x9000 + ((bank) << 2))
#define ADF_RINGMODECTL_ENABLE_UQ	BIT(0)

#endif
