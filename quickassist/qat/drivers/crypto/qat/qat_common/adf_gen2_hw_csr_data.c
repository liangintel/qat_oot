// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2019, 2021 Intel Corporation */

#include "adf_accel_devices.h"
#include "adf_transport_access_macros.h"
#include "adf_gen2_hw_csr_data.h"

static u32 build_ring_config(u32 size)
{
	return BUILD_RING_CONFIG(size);
}

static u64 build_resp_ring_config(u32 size,
				  u32 watermark_nf,
				  u32 watermark_ne)
{
	return BUILD_RESP_RING_CONFIG(size, watermark_nf, watermark_ne);
}

static u64 build_ring_base_addr(dma_addr_t addr, u32 size)
{
	return BUILD_RING_BASE_ADDR(addr, size);
}

static u32 read_csr_ring_head(void __iomem *csr_base_addr,
			      u32 bank,
			      u32 ring)
{
	return READ_CSR_RING_HEAD(csr_base_addr, bank, ring);
}

static void write_csr_ring_head(void __iomem *csr_base_addr,
				u32 bank,
				u32 ring,
				u32 value)
{
	WRITE_CSR_RING_HEAD(csr_base_addr, bank, ring, value);
}

static u32 read_csr_ring_tail(void __iomem *csr_base_addr,
			      u32 bank,
			      u32 ring)
{
	return READ_CSR_RING_TAIL(csr_base_addr, bank, ring);
}

static void write_csr_ring_tail(void __iomem *csr_base_addr,
				u32 bank,
				u32 ring,
				u32 value)
{
	WRITE_CSR_RING_TAIL(csr_base_addr, bank, ring, value);
}

static u32 read_csr_stat(void __iomem *csr_base_addr,
			 u32 bank)
{
	return READ_CSR_STAT(csr_base_addr, bank);
}

static u32 read_csr_uo_stat(void __iomem *csr_base_addr,
			    u32 bank)
{
	return READ_CSR_UO_STAT(csr_base_addr, bank);
}

static u32 read_csr_e_stat(void __iomem *csr_base_addr,
			   u32 bank)
{
	return READ_CSR_E_STAT(csr_base_addr, bank);
}

static u32 read_csr_ne_stat(void __iomem *csr_base_addr,
			    u32 bank)
{
	return READ_CSR_NE_STAT(csr_base_addr, bank);
}

static u32 read_csr_nf_stat(void __iomem *csr_base_addr,
			    u32 bank)
{
	return READ_CSR_NF_STAT(csr_base_addr, bank);
}

static u32 read_csr_f_stat(void __iomem *csr_base_addr,
			   u32 bank)
{
	return READ_CSR_F_STAT(csr_base_addr, bank);
}

static u32 read_csr_c_stat(void __iomem *csr_base_addr,
			   u32 bank)
{
	return READ_CSR_C_STAT(csr_base_addr, bank);
}

static u32 read_csr_ring_config(void __iomem *csr_base_addr,
				u32 bank,
				u32 ring)
{
	return READ_CSR_RING_CONFIG(csr_base_addr, bank, ring);
}

static void write_csr_ring_config(void __iomem *csr_base_addr,
				  u32 bank,
				  u32 ring,
				  u32 value)
{
	WRITE_CSR_RING_CONFIG(csr_base_addr, bank, ring, value);
}

static dma_addr_t read_csr_ring_base(void __iomem *csr_base_addr,
				     u32 bank,
				     u32 ring)
{
	return READ_CSR_RING_BASE(csr_base_addr, bank, ring);
}

static void write_csr_ring_base(void __iomem *csr_base_addr,
				u32 bank,
				u32 ring,
				dma_addr_t addr)
{
	WRITE_CSR_RING_BASE(csr_base_addr, bank, ring, addr);
}

static u32 read_csr_int_en(void __iomem *csr_base_addr,
			   u32 bank)
{
	return READ_CSR_INT_EN(csr_base_addr, bank);
}

static void write_csr_int_en(void __iomem *csr_base_addr,
			     u32 bank,
			     u32 value)
{
	WRITE_CSR_INT_EN(csr_base_addr, bank, value);
}

static u32 read_csr_int_flag(void __iomem *csr_base_addr,
			     u32 bank)
{
	return READ_CSR_INT_FLAG(csr_base_addr, bank);
}

static void write_csr_int_flag(void __iomem *csr_base_addr,
			       u32 bank,
			       u32 value)
{
	WRITE_CSR_INT_FLAG(csr_base_addr, bank, value);
}

static u32 read_csr_int_srcsel(void __iomem *csr_base_addr,
			       u32 bank,
			       u32 idx)
{
	return READ_CSR_INT_SRCSEL(csr_base_addr, bank, idx);
}

static void write_csr_int_srcsel(void __iomem *csr_base_addr,
				 u32 bank,
				 u32 idx,
				 u32 value)
{
	WRITE_CSR_INT_SRCSEL(csr_base_addr, bank, idx, value);
}

static u32 read_csr_int_col_en(void __iomem *csr_base_addr,
			       u32 bank)
{
	return READ_CSR_INT_COL_EN(csr_base_addr, bank);
}

static void write_csr_int_col_en(void __iomem *csr_base_addr,
				 u32 bank,
				 u32 value)
{
	WRITE_CSR_INT_COL_EN(csr_base_addr, bank, value);
}

static u32 read_csr_int_col_ctl(void __iomem *csr_base_addr,
				u32 bank)
{
	return READ_CSR_INT_COL_CTL(csr_base_addr, bank);
}

static void write_csr_int_col_ctl(void __iomem *csr_base_addr,
				  u32 bank,
				  u32 value)
{
	WRITE_CSR_INT_COL_CTL(csr_base_addr, bank, value);
}

static u32 read_csr_int_flag_and_col(void __iomem *csr_base_addr,
				     u32 bank)
{
	return READ_CSR_INT_FLAG_AND_COL(csr_base_addr, bank);
}

static void write_csr_int_flag_and_col(void __iomem *csr_base_addr,
				       u32 bank,
				       u32 value)
{
	WRITE_CSR_INT_FLAG_AND_COL(csr_base_addr, bank, value);
}

static u32 read_csr_ring_srv_arb_en(void __iomem *csr_base_addr, u32 bank)
{
	return READ_CSR_RING_SRV_ARB_EN(csr_base_addr, bank);
}

static void write_csr_ring_srv_arb_en(void __iomem *csr_base_addr,
				      u32 bank, u32 value)
{
	WRITE_CSR_RING_SRV_ARB_EN(csr_base_addr, bank, value);
}

static u32 get_src_sel_mask(void)
{
	return ADF_BANK_INT_SRC_SEL_MASK;
}

static u32 get_int_col_ctl_enable_mask(void)
{
	return ADF_RING_CSR_INT_COL_CTL_ENABLE;
}

static u32 get_bank_irq_mask(u32 irq_mask)
{
	return irq_mask;
}

static void enable_misc_interrupts(void __iomem *csr_base_addr)
{
	ADF_CSR_WR(csr_base_addr, ADF_GEN2_SMIAPF1_MASK_OFFSET,
		   ADF_GEN2_SMIA1_MASK);
}

static void disable_misc_interrupts(void __iomem *csr_base_addr)
{
	ADF_CSR_WR(csr_base_addr, ADF_GEN2_SMIAPF1_MASK_OFFSET, 0);
}

static void enable_bundle_interrupts(void __iomem *csr_base_addr,
				     struct adf_accel_dev *accel_dev)
{
	ADF_CSR_WR(csr_base_addr, ADF_GEN2_SMIAPF0_MASK_OFFSET,
		   ADF_GEN2_SMIA0_MASK);
}

static void disable_bundle_interrupts(void __iomem *csr_base_addr)
{
	ADF_CSR_WR(csr_base_addr, ADF_GEN2_SMIAPF0_MASK_OFFSET, 0);
}

static void clear_pf2vf_msg_register(void __iomem *csr_base_addr, u32 index)
{
	ADF_CSR_WR(csr_base_addr,
		   ADF_GEN2_PF2VF_OFFSET((u64)index), 0x00000000);
}

static u32 read_vf2pf_isr_sou(void __iomem *csr_base_addr)
{
	u32 val = ADF_CSR_RD(csr_base_addr, ADF_GEN2_ERRSOU3);

	return ADF_GEN2_ERRSOU3_VF2PF(val);
}

static u32 read_vf2pf_isr_mask(void __iomem *csr_base_addr)
{
	u32 val = ADF_CSR_RD(csr_base_addr, ADF_GEN2_ERRMSK3);

	return ADF_GEN2_ERRSOU3_VF2PF(val);
}

static void write_vf2pf_isr_mask(void __iomem *csr_base_addr, u32 val)
{
	u32 errmsk3 = read_vf2pf_isr_mask(csr_base_addr);

	errmsk3 |= (val << 9);
	ADF_CSR_WR(csr_base_addr, ADF_GEN2_ERRMSK3, errmsk3);
}

static void enable_slice_hang_interrupt(void __iomem *csr_base_addr, u16 accel_mask)
{
#ifdef ALLOW_SLICE_HANG_INTERRUPT
	/* Enable slice hang interrupt */
	u32 i;
	unsigned int mask;

	for (i = 0, mask = accel_mask; mask; i++, mask >>= 1) {
		if (!(mask & 1))
			continue;

		ADF_CSR_WR(csr_base_addr, ADF_GEN2_SHINTMASKSSM(i),
			   ADF_GEN2_ENABLE_SLICE_HANG);
	}
#endif
}

void gen2_init_hw_csr_info(struct adf_hw_csr_info *csr_info)
{
	struct adf_hw_csr_ops *csr_ops = &csr_info->csr_ops;

	csr_info->csr_addr_offset = ADF_RING_CSR_ADDR_OFFSET;
	csr_info->ring_bundle_size = ADF_RING_BUNDLE_SIZE;
	csr_info->bank_int_flag_clear_mask = ADF_BANK_INT_FLAG_CLEAR_MASK;
	csr_info->num_rings_per_int_srcsel = ADF_RINGS_PER_INT_SRCSEL;
	csr_info->arb_enable_mask = 0xFF;

	csr_ops->build_ring_config = build_ring_config;
	csr_ops->build_resp_ring_config = build_resp_ring_config;
	csr_ops->build_ring_base_addr = build_ring_base_addr;
	csr_ops->read_csr_ring_head = read_csr_ring_head;
	csr_ops->write_csr_ring_head = write_csr_ring_head;
	csr_ops->read_csr_ring_tail = read_csr_ring_tail;
	csr_ops->write_csr_ring_tail = write_csr_ring_tail;
	csr_ops->read_csr_stat = read_csr_stat;
	csr_ops->read_csr_uo_stat = read_csr_uo_stat;
	csr_ops->read_csr_e_stat = read_csr_e_stat;
	csr_ops->read_csr_ne_stat = read_csr_ne_stat;
	csr_ops->read_csr_nf_stat = read_csr_nf_stat;
	csr_ops->read_csr_f_stat = read_csr_f_stat;
	csr_ops->read_csr_c_stat = read_csr_c_stat;
	csr_ops->read_csr_ring_config = read_csr_ring_config;
	csr_ops->write_csr_ring_config = write_csr_ring_config;
	csr_ops->read_csr_ring_base = read_csr_ring_base;
	csr_ops->write_csr_ring_base = write_csr_ring_base;
	csr_ops->read_csr_int_en = read_csr_int_en;
	csr_ops->write_csr_int_en = write_csr_int_en;
	csr_ops->read_csr_int_flag = read_csr_int_flag;
	csr_ops->write_csr_int_flag = write_csr_int_flag;
	csr_ops->read_csr_int_srcsel = read_csr_int_srcsel;
	csr_ops->write_csr_int_srcsel = write_csr_int_srcsel;
	csr_ops->read_csr_int_col_en = read_csr_int_col_en;
	csr_ops->write_csr_int_col_en = write_csr_int_col_en;
	csr_ops->read_csr_int_col_ctl = read_csr_int_col_ctl;
	csr_ops->write_csr_int_col_ctl = write_csr_int_col_ctl;
	csr_ops->read_csr_int_flag_and_col = read_csr_int_flag_and_col;
	csr_ops->write_csr_int_flag_and_col = write_csr_int_flag_and_col;
	csr_ops->read_csr_ring_srv_arb_en = read_csr_ring_srv_arb_en;
	csr_ops->write_csr_ring_srv_arb_en = write_csr_ring_srv_arb_en;
	csr_ops->get_src_sel_mask = get_src_sel_mask;
	csr_ops->get_int_col_ctl_enable_mask = get_int_col_ctl_enable_mask;
	csr_ops->get_bank_irq_mask = get_bank_irq_mask;
	csr_ops->enable_misc_interrupts = enable_misc_interrupts;
	csr_ops->disable_misc_interrupts = disable_misc_interrupts;
	csr_ops->enable_bundle_interrupts = enable_bundle_interrupts;
	csr_ops->disable_bundle_interrupts = disable_bundle_interrupts;
	csr_ops->clear_pf2vf_msg_register = clear_pf2vf_msg_register;
	csr_ops->read_vf2pf_isr_sou = read_vf2pf_isr_sou;
	csr_ops->read_vf2pf_isr_mask = read_vf2pf_isr_mask;
	csr_ops->write_vf2pf_isr_mask = write_vf2pf_isr_mask;
	csr_ops->enable_slice_hang_interrupt = enable_slice_hang_interrupt;
}
EXPORT_SYMBOL_GPL(gen2_init_hw_csr_info);
