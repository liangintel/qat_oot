/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2019, 2021 Intel Corporation */
#ifndef ADF_TRANSPORT_ACCESS_MACROS_GEN4VF_H
#define ADF_TRANSPORT_ACCESS_MACROS_GEN4VF_H

#include "adf_accel_devices.h"
#include "adf_transport_access_macros.h"
#include "adf_transport_access_macros_gen4.h"

#define ADF_RING_CSR_ADDR_OFFSET_GEN4VF 0x0

#define READ_CSR_RING_HEAD_GEN4VF(csr_base_addr, bank, ring) \
	ADF_CSR_RD((csr_base_addr), \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_RING_HEAD + ((ring) << 2))
#define READ_CSR_RING_TAIL_GEN4VF(csr_base_addr, bank, ring) \
	ADF_CSR_RD((csr_base_addr), \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_RING_TAIL + ((ring) << 2))
#define READ_CSR_STAT_GEN4VF(csr_base_addr, bank) \
	ADF_CSR_RD((csr_base_addr), \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_STAT)
#define READ_CSR_UO_STAT_GEN4VF(csr_base_addr, bank) \
	ADF_CSR_RD((csr_base_addr), \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_UO_STAT)
#define READ_CSR_E_STAT_GEN4VF(csr_base_addr, bank) \
	ADF_CSR_RD((csr_base_addr), \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_E_STAT)
#define READ_CSR_NE_STAT_GEN4VF(csr_base_addr, bank) \
	ADF_CSR_RD((csr_base_addr), \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_NE_STAT)
#define READ_CSR_NF_STAT_GEN4VF(csr_base_addr, bank) \
	ADF_CSR_RD((csr_base_addr), \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_NF_STAT)
#define READ_CSR_F_STAT_GEN4VF(csr_base_addr, bank) \
	ADF_CSR_RD((csr_base_addr), \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_F_STAT)
#define READ_CSR_C_STAT_GEN4VF(csr_base_addr, bank) \
	ADF_CSR_RD((csr_base_addr), \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_C_STAT)
#define READ_CSR_EXP_STAT_GEN4VF(csr_base_addr, bank) \
	ADF_CSR_RD((csr_base_addr), \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_EXP_STAT_GEN4)
#define READ_CSR_EXP_INT_EN_GEN4VF(csr_base_addr, bank) \
	ADF_CSR_RD((csr_base_addr), \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_EXP_INT_EN_GEN4)
#define WRITE_CSR_EXP_INT_EN_GEN4VF(csr_base_addr, bank, value) \
	ADF_CSR_WR((csr_base_addr), \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_EXP_INT_EN_GEN4, value)
#define READ_CSR_RING_CONFIG_GEN4VF(csr_base_addr, bank, ring) \
	ADF_CSR_RD((csr_base_addr), \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_RING_CONFIG_GEN4 + ((ring) << 2))
#define WRITE_CSR_RING_CONFIG_GEN4VF(csr_base_addr, bank, ring, value) \
	ADF_CSR_WR((csr_base_addr), \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_RING_CONFIG_GEN4 + ((ring) << 2), (value))
#define WRITE_CSR_RING_BASE_GEN4VF(csr_base_addr, bank, ring, value)	\
do {									\
	void __iomem *_csr_base_addr = csr_base_addr;			\
	u32 _bank = bank;						\
	u32 _ring = ring;						\
	dma_addr_t _value = value;					\
	u32 l_base = 0, u_base = 0;					\
	l_base = (u32)((_value) & 0xFFFFFFFF);				\
	u_base = (u32)(((_value) & 0xFFFFFFFF00000000ULL) >> 32);	\
	ADF_CSR_WR((_csr_base_addr),	\
		(ADF_RING_BUNDLE_SIZE_GEN4 * (_bank)) +			\
		ADF_RING_CSR_RING_LBASE_GEN4 + ((_ring) << 2), l_base);	\
	ADF_CSR_WR((_csr_base_addr),	\
		(ADF_RING_BUNDLE_SIZE_GEN4 * (_bank)) +			\
		ADF_RING_CSR_RING_UBASE_GEN4 + ((_ring) << 2), u_base);	\
} while (0)

static inline u64 read_base_gen4vf(void __iomem *csr_base_addr,
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

#define READ_CSR_RING_BASE_GEN4VF(csr_base_addr, bank, ring) \
	read_base_gen4vf((csr_base_addr), \
			(bank), (ring))

#define WRITE_CSR_RING_HEAD_GEN4VF(csr_base_addr, bank, ring, value) \
	ADF_CSR_WR((csr_base_addr), \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_RING_HEAD + ((ring) << 2), (value))
#define WRITE_CSR_RING_TAIL_GEN4VF(csr_base_addr, bank, ring, value) \
	ADF_CSR_WR((csr_base_addr), \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_RING_TAIL + ((ring) << 2), (value))
#define READ_CSR_INT_EN_GEN4VF(csr_base_addr, bank) \
	ADF_CSR_RD((csr_base_addr), \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_INT_FLAG_EN)
#define WRITE_CSR_INT_EN_GEN4VF(csr_base_addr, bank, value) \
	ADF_CSR_WR((csr_base_addr), \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_INT_FLAG_EN, (value))
#define READ_CSR_INT_FLAG_GEN4VF(csr_base_addr, bank) \
	ADF_CSR_RD((csr_base_addr), \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_INT_FLAG)
#define WRITE_CSR_INT_FLAG_GEN4VF(csr_base_addr, bank, value) \
	ADF_CSR_WR((csr_base_addr), \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_INT_FLAG, (value))
#define READ_CSR_INT_SRCSEL_GEN4VF(csr_base_addr, bank, idx) \
	ADF_CSR_RD((csr_base_addr), \
	(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
	(ADF_RING_CSR_INT_SRCSEL + ((idx) * ADF_RING_CSR_NEXT_INT_SRCSEL)))
#define WRITE_CSR_INT_SRCSEL_GEN4VF(csr_base_addr, bank, idx, value) \
	ADF_CSR_WR((csr_base_addr), \
	(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
	(ADF_RING_CSR_INT_SRCSEL + ((idx) * ADF_RING_CSR_NEXT_INT_SRCSEL)), \
	(value))
#define READ_CSR_INT_COL_EN_GEN4VF(csr_base_addr, bank) \
	ADF_CSR_RD((csr_base_addr), \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_INT_COL_EN)
#define WRITE_CSR_INT_COL_EN_GEN4VF(csr_base_addr, bank, value) \
	ADF_CSR_WR((csr_base_addr), \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_INT_COL_EN, (value))
#define READ_CSR_INT_COL_CTL_GEN4VF(csr_base_addr, bank) \
	ADF_CSR_RD((csr_base_addr), \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_INT_COL_CTL)
#define WRITE_CSR_INT_COL_CTL_GEN4VF(csr_base_addr, bank, value) \
	ADF_CSR_WR((csr_base_addr), \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_INT_COL_CTL, (value))
#define READ_CSR_INT_FLAG_AND_COL_GEN4VF(csr_base_addr, bank) \
	ADF_CSR_RD((csr_base_addr), \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_INT_FLAG_AND_COL)
#define WRITE_CSR_INT_FLAG_AND_COL_GEN4VF(csr_base_addr, bank, value) \
	ADF_CSR_WR((csr_base_addr), \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_INT_FLAG_AND_COL, (value))
#define READ_CSR_RING_SRV_ARB_EN_GEN4VF(csr_base_addr, bank) \
	ADF_CSR_RD((csr_base_addr), \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_RING_SRV_ARB_EN)
#define WRITE_CSR_RING_SRV_ARB_EN_GEN4VF(csr_base_addr, bank, value) \
	ADF_CSR_WR((csr_base_addr), \
		(ADF_RING_BUNDLE_SIZE_GEN4 * (bank)) + \
		ADF_RING_CSR_RING_SRV_ARB_EN, (value))
#endif
