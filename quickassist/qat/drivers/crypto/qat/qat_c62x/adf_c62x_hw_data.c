// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2021 Intel Corporation */
#include <adf_accel_devices.h>
#include <adf_common_drv.h>
#include <adf_pf2vf_msg.h>
#include <adf_dev_err.h>
#include <adf_gen2_hw_csr_data.h>
#include "adf_gen2_hw_data.h"
#include "adf_cfg.h"
#include "adf_c62x_hw_data.h"
#include "adf_heartbeat.h"
#include "icp_qat_hw.h"
#include "adf_cfg_strings.h"
#ifdef QAT_UIO
#ifdef QAT_KPT
#include "adf_mei_kpt.h"

#define ADF_C6X_MAILBOX1_BASE_OFFSET 0x20974
#define ADF_C6X_MAILBOX1_STRIDE 0x1000
#endif
#endif

/* Worker thread to service arbiter mappings */
static u32 thrd_to_arb_map[ADF_C62X_MAX_ACCELENGINES] = {
	0x12222AAA, 0x11222AAA, 0x12222AAA, 0x11222AAA, 0x12222AAA,
	0x11222AAA, 0x12222AAA, 0x11222AAA, 0x12222AAA, 0x11222AAA
};

static u32 thrd_to_arb_map_gen[ADF_C62X_MAX_ACCELENGINES] = {0};

static struct adf_hw_device_class c62x_class = {
	.name = ADF_C62X_DEVICE_NAME,
	.type = DEV_C62X,
	.instances = 0
};

static u32 get_accel_mask(struct adf_accel_dev *accel_dev)
{
	struct pci_dev *pdev = accel_dev->accel_pci_dev.pci_dev;
	u32 fuse;
	u32 straps;

	pci_read_config_dword(pdev, ADF_DEVICE_FUSECTL_OFFSET,
			      &fuse);
	pci_read_config_dword(pdev, ADF_C62X_SOFTSTRAP_CSR_OFFSET,
			      &straps);

	return (~(fuse | straps)) >> ADF_C62X_ACCELERATORS_REG_OFFSET &
		ADF_C62X_ACCELERATORS_MASK;
}

static u32 get_ae_mask(struct adf_accel_dev *accel_dev)
{
	struct pci_dev *pdev = accel_dev->accel_pci_dev.pci_dev;
	u32 fuse;
	u32 me_straps;
	u32 me_disable;
	u32 ssms_disabled;

	pci_read_config_dword(pdev, ADF_DEVICE_FUSECTL_OFFSET,
			      &fuse);
	pci_read_config_dword(pdev, ADF_C62X_SOFTSTRAP_CSR_OFFSET,
			      &me_straps);

	/* If SSMs are disabled, then disable the corresponding MEs */
	ssms_disabled = (~get_accel_mask(accel_dev)) &
		ADF_C62X_ACCELERATORS_MASK;
	me_disable = 0x3;
	while (ssms_disabled) {
		if (ssms_disabled & 1)
			me_straps |= me_disable;
		ssms_disabled >>= 1;
		me_disable <<= 2;
	}

	return (~(fuse | me_straps)) & ADF_C62X_ACCELENGINES_MASK;
}

static u32 get_num_accels(struct adf_hw_device_data *self)
{
	u32 i, ctr = 0;

	if (!self || !self->accel_mask)
		return 0;

	for (i = 0; i < ADF_C62X_MAX_ACCELERATORS; i++) {
		if (self->accel_mask & (1 << i))
			ctr++;
	}
	return ctr;
}

static u32 get_num_aes(struct adf_hw_device_data *self)
{
	u32 i, ctr = 0;

	if (!self || !self->ae_mask)
		return 0;

	for (i = 0; i < ADF_C62X_MAX_ACCELENGINES; i++) {
		if (self->ae_mask & (1 << i))
			ctr++;
	}
	return ctr;
}

static u32 get_misc_bar_id(struct adf_hw_device_data *self)
{
	return ADF_C62X_PMISC_BAR;
}

static u32 get_etr_bar_id(struct adf_hw_device_data *self)
{
	return ADF_C62X_ETR_BAR;
}

static u32 get_sram_bar_id(struct adf_hw_device_data *self)
{
	return ADF_C62X_SRAM_BAR;
}

static enum dev_sku_info get_sku(struct adf_hw_device_data *self)
{
	int aes = get_num_aes(self);

	if (aes == 8)
		return DEV_SKU_2;
	else if (aes == 10)
		return DEV_SKU_4;

	return DEV_SKU_UNKNOWN;
}

#if defined(CONFIG_PCI_IOV)
static void process_and_get_vf2pf_int(void __iomem *pmisc_addr,
				      u32 vf_int_mask_sets[ADF_MAX_VF2PF_SET])
{
	int i;
	u32 errsou3, errmsk3;
	u32 sources, disabled, non_vf2pf_errmsk3;

	/* Get the interrupt sources triggered by VFs */
	errsou3 = ADF_CSR_RD(pmisc_addr, ADF_ERRSOU3);
	sources = ADF_C62X_ERR_REG_VF2PF(errsou3);

	/* Get the already disabled interrupts */
	errmsk3 = ADF_CSR_RD(pmisc_addr, ADF_ERRMSK3);
	non_vf2pf_errmsk3 = errmsk3 & ADF_C62X_ERRMSK3_NON_VF2PF;
	disabled = ADF_C62X_ERR_REG_VF2PF(errmsk3);

	/*
	 * To avoid adding duplicate entries to work queue, clear
	 * source interrupt bits that are already masked in ERRMSK register.
	 */
	vf_int_mask_sets[0] = sources & ~disabled;

	/* All unused vf2pf mask registers should be zeroed */
	for (i = 1; i < ADF_MAX_VF2PF_SET; i++)
		vf_int_mask_sets[i] = 0;

	/*
	 * Due to HW limitations, when disabling the interrupts, we can't
	 * just disable the requested sources, as this would lead to missed
	 * interrupts if sources change just before writing to ERRMSK3.
	 * To resolve this, disable all interrupts and re-enable only the
	 * sources that are not currently being serviced and the sources that
	 * were not already disabled. Re-enabling will trigger a new interrupt
	 * for the sources that have changed in the meantime, if any.
	 */
	errmsk3 |= ADF_C62X_ERRMSK3_VF2PF(ADF_VF2PF_INT_MASK);
	ADF_CSR_WR(pmisc_addr, ADF_ERRMSK3, errmsk3);

	errmsk3 =
		non_vf2pf_errmsk3 | ADF_C62X_ERRMSK3_VF2PF(sources | disabled);
	ADF_CSR_WR(pmisc_addr, ADF_ERRMSK3, errmsk3);
}

static void enable_vf2pf_interrupts(void __iomem *pmisc_addr,
				    u32 vf_mask, u8 vf2pf_set)
{
	if (vf2pf_set)
		return;

	/* Enable VF2PF Messaging Ints - VFs 1 through 16 per vf_mask[15:0] */
	if (vf_mask & 0xFFFF)
		adf_csr_fetch_and_and(pmisc_addr,
				      ADF_ERRMSK3,
				      ~ADF_C62X_ERRMSK3_VF2PF(vf_mask));
}

static void disable_vf2pf_interrupts(void __iomem *pmisc_addr,
				     u32 vf_mask, u8 vf2pf_set)
{
	if (vf2pf_set)
		return;

	/* Disable VF2PF interrupts for VFs 1 through 16 per vf_mask[15:0] */
	if (vf_mask & 0xFFFF)
		adf_csr_fetch_and_or(pmisc_addr,
				     ADF_ERRMSK3,
				     ADF_C62X_ERRMSK3_VF2PF(vf_mask));
}

static int check_arbitrary_numvfs(struct adf_accel_dev *accel_dev,
				  const int numvfs)
{
	int totalvfs = pci_sriov_get_totalvfs(accel_to_pci_dev(accel_dev));

	return numvfs > totalvfs ? totalvfs : numvfs;
}
#endif /* CONFIG_PCI_IOV */

static void adf_get_arbiter_mapping(struct adf_accel_dev *accel_dev,
				    u32 const **arb_map_config)
{
	int i;
	struct adf_hw_device_data *hw_device = accel_dev->hw_device;

	for_each_set_bit(i, (unsigned long *)&hw_device->ae_mask, ADF_C62X_MAX_ACCELENGINES)
		thrd_to_arb_map_gen[i] = thrd_to_arb_map[i];
#if defined(QAT_UIO) || defined(QAT_ESXI)
	adf_cfg_gen_dispatch_arbiter(accel_dev,
				     thrd_to_arb_map,
				     thrd_to_arb_map_gen,
				     ADF_C62X_MAX_ACCELENGINES);
	*arb_map_config = thrd_to_arb_map_gen;
#else
	*arb_map_config = thrd_to_arb_map_gen;
#endif
}

static u32 get_pf2vf_offset(u32 i)
{
	return ADF_C62X_PF2VF_OFFSET(i);
}


static void get_arb_info(struct arb_info *arb_csrs_info)
{
	arb_csrs_info->arbiter_offset = ADF_C62X_ARB_OFFSET;
	arb_csrs_info->wrk_thd_2_srv_arb_map =
			ADF_C62X_ARB_WRK_2_SER_MAP_OFFSET;
	arb_csrs_info->wrk_cfg_offset = ADF_C62X_ARB_WQCFG_OFFSET;
}

static void get_admin_info(struct admin_info *admin_csrs_info)
{
	admin_csrs_info->mailbox_offset = ADF_C62X_MAILBOX_BASE_OFFSET;
	admin_csrs_info->admin_msg_ur = ADF_C62X_ADMINMSGUR_OFFSET;
	admin_csrs_info->admin_msg_lr = ADF_C62X_ADMINMSGLR_OFFSET;
}

static void get_errsou_offset(u32 *errsou3, u32 *errsou5)
{
	*errsou3 = ADF_C62X_ERRSOU3;
	*errsou5 = ADF_C62X_ERRSOU5;
}

static u32 get_clock_speed(struct adf_hw_device_data *self)
{
	/* CPP clock is half high-speed clock */
	return self->clock_frequency / 2;
}

static void adf_enable_error_interrupts(void __iomem *csr)
{
	ADF_CSR_WR(csr, ADF_ERRMSK0, ADF_C62X_ERRMSK0_CERR); /* ME0-ME3  */
	ADF_CSR_WR(csr, ADF_ERRMSK1, ADF_C62X_ERRMSK1_CERR); /* ME4-ME7  */
	ADF_CSR_WR(csr, ADF_ERRMSK4, ADF_C62X_ERRMSK4_CERR); /* ME8-ME9  */
	ADF_CSR_WR(csr, ADF_ERRMSK5, ADF_C62X_ERRMSK5_CERR); /* SSM2-SSM4 */

	/* Reset everything except VFtoPF1_16 */
	adf_csr_fetch_and_and(csr, ADF_ERRMSK3, ADF_C62X_VF2PF1_16);
	/* Disable Secure RAM correctable error interrupt */
	adf_csr_fetch_and_or(csr, ADF_ERRMSK3, ADF_C62X_ERRMSK3_CERR);

	/* RI CPP bus interface error detection and reporting. */
	ADF_CSR_WR(csr, ADF_C62X_RICPPINTCTL, ADF_C62X_RICPP_EN);

	/* TI CPP bus interface error detection and reporting. */
	ADF_CSR_WR(csr, ADF_C62X_TICPPINTCTL, ADF_C62X_TICPP_EN);

	/* Enable CFC Error interrupts and logging */
	ADF_CSR_WR(csr, ADF_C62X_CPP_CFC_ERR_CTRL, ADF_C62X_CPP_CFC_UE);

	/* Enable SecureRAM to fix and log Correctable errors */
	ADF_CSR_WR(csr, ADF_C62X_SECRAMCERR, ADF_C62X_SECRAM_CERR);

	/* Enable SecureRAM Uncorrectable error interrupts and logging */
	ADF_CSR_WR(csr, ADF_SECRAMUERR, ADF_C62X_SECRAM_UERR);

	/* Enable Push/Pull Misc Uncorrectable error interrupts and logging */
	ADF_CSR_WR(csr, ADF_CPPMEMTGTERR, ADF_C62X_TGT_UERR);
}

static void adf_disable_error_interrupts(struct adf_accel_dev *accel_dev)
{
	struct adf_bar *misc_bar = &GET_BARS(accel_dev)[ADF_C62X_PMISC_BAR];
	void __iomem *csr = misc_bar->virt_addr;

	/* ME0-ME3 */
	ADF_CSR_WR(csr, ADF_ERRMSK0, ADF_C62X_ERRMSK0_UERR |
		   ADF_C62X_ERRMSK0_CERR);
	/* ME4-ME7 */
	ADF_CSR_WR(csr, ADF_ERRMSK1, ADF_C62X_ERRMSK1_UERR |
		   ADF_C62X_ERRMSK1_CERR);
	/* Secure RAM, CPP Push Pull, RI, TI, SSM0-SSM1, CFC */
	ADF_CSR_WR(csr, ADF_ERRMSK3, ADF_C62X_ERRMSK3_UERR |
		   ADF_C62X_ERRMSK3_CERR);
	/* ME8-ME9 */
	ADF_CSR_WR(csr, ADF_ERRMSK4, ADF_C62X_ERRMSK4_UERR |
		   ADF_C62X_ERRMSK4_CERR);
	/* SSM2-SSM4 */
	ADF_CSR_WR(csr, ADF_ERRMSK5, ADF_C62X_ERRMSK5_UERR |
		   ADF_C62X_ERRMSK5_CERR);
}

static int adf_check_uncorrectable_error(struct adf_accel_dev *accel_dev)
{
	struct adf_bar *misc_bar = &GET_BARS(accel_dev)[ADF_C62X_PMISC_BAR];
	void __iomem *csr = misc_bar->virt_addr;

	u32 errsou0 = ADF_CSR_RD(csr, ADF_ERRSOU0) & ADF_C62X_ERRMSK0_UERR;
	u32 errsou1 = ADF_CSR_RD(csr, ADF_ERRSOU1) & ADF_C62X_ERRMSK1_UERR;
	u32 errsou3 = ADF_CSR_RD(csr, ADF_ERRSOU3) & ADF_C62X_ERRMSK3_UERR;
	u32 errsou4 = ADF_CSR_RD(csr, ADF_ERRSOU4) & ADF_C62X_ERRMSK4_UERR;
	u32 errsou5 = ADF_CSR_RD(csr, ADF_ERRSOU5) & ADF_C62X_ERRMSK5_UERR;

	return (errsou0 | errsou1 | errsou3 | errsou4 | errsou5);
}

static void adf_enable_mmp_error_correction(void __iomem *csr,
					    struct adf_hw_device_data *hw_data)
{
	unsigned int dev, mmp;

	/* Enable MMP Logging */
	for_each_set_bit(dev, (unsigned long *)&hw_data->accel_mask, ADF_C62X_MAX_ACCELERATORS) {
		/* Set power-up */
		adf_csr_fetch_and_and(csr,
				      ADF_C62X_SLICEPWRDOWN(dev),
				      ~ADF_C62X_MMP_PWR_UP_MSK);

		if (hw_data->accel_capabilities_mask &
		    ADF_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC) {
			for (mmp = 0; mmp < ADF_MAX_MMP; ++mmp) {
				/*
				 * The device supports PKE,
				 * so enable error reporting from MMP memory
				 */
				adf_csr_fetch_and_or(csr,
						     ADF_UERRSSMMMP(dev, mmp),
						     ADF_C62X_UERRSSMMMP_EN);
				/*
				 * The device supports PKE,
				 * so enable error correction from MMP memory
				 */
				adf_csr_fetch_and_or(csr,
						     ADF_CERRSSMMMP(dev, mmp),
						     ADF_C62X_CERRSSMMMP_EN);
			}
		} else {
			for (mmp = 0; mmp < ADF_MAX_MMP; ++mmp) {
				/*
				 * The device doesn't support PKE,
				 * so disable error reporting from MMP memory
				 */
				adf_csr_fetch_and_and(csr,
						      ADF_UERRSSMMMP(dev, mmp),
						      ~ADF_C62X_UERRSSMMMP_EN);
				/*
				 * The device doesn't support PKE,
				 * so disable error correction from MMP memory
				 */
				adf_csr_fetch_and_and(csr,
						      ADF_CERRSSMMMP(dev, mmp),
						      ~ADF_C62X_CERRSSMMMP_EN);
			}
		}

		/* Restore power-down value */
		adf_csr_fetch_and_or(csr,
				     ADF_C62X_SLICEPWRDOWN(dev),
				     ADF_C62X_MMP_PWR_UP_MSK);

		/* Disabling correctable error interrupts. */
		ADF_CSR_WR(csr,
			   ADF_C62X_INTMASKSSM(dev),
			   ADF_C62X_INTMASKSSM_UERR);
	}
}

static void adf_enable_error_correction(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_device = accel_dev->hw_device;
	struct adf_bar *misc_bar = &GET_BARS(accel_dev)[ADF_C62X_PMISC_BAR];
	void __iomem *csr = misc_bar->virt_addr;
	unsigned int i;
	unsigned int mask;

	/* Enable Accel Engine error detection & correction */
	for (i = 0, mask = hw_device->ae_mask; mask; i++, mask >>= 1) {
		if (!(mask & 1))
			continue;
		adf_csr_fetch_and_or(csr, ADF_C62X_AE_CTX_ENABLES(i),
				     ADF_C62X_ENABLE_AE_ECC_ERR);
		adf_csr_fetch_and_or(csr, ADF_C62X_AE_MISC_CONTROL(i),
				     ADF_C62X_ENABLE_AE_ECC_PARITY_CORR);
	}

	/* Enable shared memory error detection & correction */
	for (i = 0, mask = hw_device->accel_mask; mask; i++, mask >>= 1) {
		if (!(mask & 1))
			continue;
		adf_csr_fetch_and_or(csr, ADF_UERRSSMSH(i),
				     ADF_C62X_ERRSSMSH_EN);
		adf_csr_fetch_and_or(csr, ADF_CERRSSMSH(i),
				     ADF_C62X_ERRSSMSH_EN);
		adf_csr_fetch_and_or(csr, ADF_PPERR(i),
				     ADF_C62X_PPERR_EN);
	}

	adf_enable_error_interrupts(csr);
	adf_enable_mmp_error_correction(csr, hw_device);
}

static void adf_enable_ints(struct adf_accel_dev *accel_dev)
{
	void __iomem *addr;
#ifdef ALLOW_SLICE_HANG_INTERRUPT
	struct adf_hw_device_data *hw_device = accel_dev->hw_device;
	u32 i;
	unsigned int mask;
#endif

	addr = (&GET_BARS(accel_dev)[ADF_C62X_PMISC_BAR])->virt_addr;

	/* Enable bundle and misc interrupts */
	ADF_CSR_WR(addr, ADF_C62X_SMIAPF0_MASK_OFFSET,
		   ADF_C62X_SMIA0_MASK);
	ADF_CSR_WR(addr, ADF_C62X_SMIAPF1_MASK_OFFSET,
		   ADF_C62X_SMIA1_MASK);
#ifdef ALLOW_SLICE_HANG_INTERRUPT
	/* Enable slice hang interrupt */
	for (i = 0, mask = hw_device->accel_mask; mask; i++, mask >>= 1) {
		if (!(mask & 1))
			continue;
		ADF_CSR_WR(addr, ADF_SHINTMASKSSM(i),
			   ADF_ENABLE_SLICE_HANG);
	}
#endif
}

static u32 get_ae_clock(struct adf_hw_device_data *self)
{
	/*
	 * Clock update interval is <16> ticks for c62x.
	 */
	return self->clock_frequency / 16;
}

static int get_storage_enabled(struct adf_accel_dev *accel_dev,
			       uint32_t *storage_enabled)
{
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	char val[ADF_CFG_MAX_VAL_LEN_IN_BYTES];

	strscpy(key, ADF_STORAGE_FIRMWARE_ENABLED, sizeof(key));
	if (!adf_cfg_get_param_value(accel_dev, ADF_GENERAL_SEC, key, val)) {
		if (kstrtouint(val, 0, storage_enabled))
			return -EFAULT;
	}
	return 0;
}

static int measure_clock(struct adf_accel_dev *accel_dev)
{
	u32 frequency;
	int ret = 0;

	ret = adf_dev_measure_clock(accel_dev, &frequency,
				    ADF_C62X_MIN_AE_FREQ,
				    ADF_C62X_MAX_AE_FREQ);
	if (ret)
		return ret;

	accel_dev->hw_device->clock_frequency = frequency;
	return 0;
}

static u32 c62x_get_hw_cap(struct adf_accel_dev *accel_dev)
{
	struct pci_dev *pdev = accel_dev->accel_pci_dev.pci_dev;
	u32 legfuses;
	u32 capabilities;
	u32 straps;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u32 fuses = hw_data->fuses;

	/* Read accelerator capabilities mask */
	pci_read_config_dword(pdev, ADF_DEVICE_LEGFUSE_OFFSET,
			      &legfuses);
	capabilities =
		ICP_ACCEL_CAPABILITIES_CRYPTO_SYMMETRIC +
		ICP_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC +
		ICP_ACCEL_CAPABILITIES_CIPHER +
		ICP_ACCEL_CAPABILITIES_AUTHENTICATION +
		ICP_ACCEL_CAPABILITIES_COMPRESSION +
		ICP_ACCEL_CAPABILITIES_ZUC +
		ICP_ACCEL_CAPABILITIES_SHA3 +
		ICP_ACCEL_CAPABILITIES_ECEDMONT +
		ICP_ACCEL_CAPABILITIES_EXT_ALGCHAIN;
	if (legfuses & ICP_ACCEL_MASK_CIPHER_SLICE) {
		capabilities &= ~(ICP_ACCEL_CAPABILITIES_CRYPTO_SYMMETRIC |
				  ICP_ACCEL_CAPABILITIES_CIPHER |
				  ICP_ACCEL_CAPABILITIES_EXT_ALGCHAIN);
	}
	if (legfuses & ICP_ACCEL_MASK_AUTH_SLICE)
		capabilities &= ~ICP_ACCEL_CAPABILITIES_AUTHENTICATION;
	if (legfuses & ICP_ACCEL_MASK_PKE_SLICE)
		capabilities &= ~ICP_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC;
	if (legfuses & ICP_ACCEL_MASK_COMPRESS_SLICE)
		capabilities &= ~ICP_ACCEL_CAPABILITIES_COMPRESSION;
	if (legfuses & ICP_ACCEL_MASK_EIA3_SLICE)
		capabilities &= ~ICP_ACCEL_CAPABILITIES_ZUC;

	pci_read_config_dword(pdev, ADF_C62X_SOFTSTRAP_CSR_OFFSET,
			      &straps);
	if ((straps | fuses) & ADF_C62X_POWERGATE_PKE)
		capabilities &= ~(ICP_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC |
				  ICP_ACCEL_CAPABILITIES_ECEDMONT);
	if ((straps | fuses) & ADF_C62X_POWERGATE_CY)
		capabilities &= ~ICP_ACCEL_CAPABILITIES_COMPRESSION;

	return capabilities;
}

#ifdef QAT_KPT
static void init_mailbox1(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct adf_bar *pmisc =
		&GET_BARS(accel_dev)[hw_data->get_misc_bar_id(hw_data)];
	void __iomem *csr = pmisc->virt_addr;
	u32 mailbox1_offset = 0;
	int i = 0;

	for (i = 0; i < hw_data->get_num_accels(hw_data); i++) {
		mailbox1_offset = ADF_C6X_MAILBOX1_BASE_OFFSET +
				  i * ADF_C6X_MAILBOX1_STRIDE;
		ADF_CSR_WR(csr, mailbox1_offset, 0);
	}
}

static void set_kpt_hw_capability(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct adf_bar *pmisc =
		&GET_BARS(accel_dev)[hw_data->get_misc_bar_id(hw_data)];
	u32 ac_value;
	void __iomem *csr = pmisc->virt_addr;

	ac_value = ADF_CSR_RD(csr, ADF_C6X_MAILBOX1_BASE_OFFSET);
	if (ac_value) {
		hw_data->kpt_hw_capabilities = 1;
		hw_data->kpt_achandle = ac_value;
	}
}

static int update_hw_capability(struct adf_accel_dev *accel_dev,
				u32 kpt_enabled)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;

	if (kpt_enabled) {
		if (!hw_data->kpt_hw_capabilities) {
			dev_err(&GET_DEV(accel_dev),
				"Device has no KPT capabilitiy\n");
			return -EFAULT;
		}
		adf_update_kpt_wrk_arb(accel_dev);
		hw_data->accel_capabilities_mask |=
			ICP_ACCEL_CAPABILITIES_KPT;
		hw_data->accel_capabilities_mask &=
			~(ICP_ACCEL_CAPABILITIES_COMPRESSION);
	} else {
		hw_data->accel_capabilities_mask &=
			~ICP_ACCEL_CAPABILITIES_KPT;
		hw_data->accel_capabilities_mask |=
			(ICP_ACCEL_CAPABILITIES_COMPRESSION);
	}
	return 0;
}

static int enable_kpt(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u32 kpt_enabled = 0;
	int ret = 0;
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	char val[ADF_CFG_MAX_VAL_LEN_IN_BYTES];
	u64 kpt_val = 0;

	snprintf(key, sizeof(key), ADF_DEV_KPT_ENABLE);
	if (!accel_dev->detect_kpt) {
		/* initialize mailbox1 to 0 */
		init_mailbox1(accel_dev);
		/* send discovery KPT hardware capability command */
		ret = adf_mei_send_discovery_kpt();
		if (ret)
			return -EFAULT;
		set_kpt_hw_capability(accel_dev);

		if (hw_data->kpt_hw_capabilities)
			kpt_val = 1;
		if (adf_cfg_section_add(accel_dev, ADF_GENERAL_SEC))
			return -EINVAL;
		if (adf_cfg_add_key_value_param(accel_dev, ADF_GENERAL_SEC,
						key, (void *)&kpt_val, ADF_DEC))
			return -EINVAL;

		accel_dev->detect_kpt = 1;
	}
	/*
	 * Enable KPT capability and disable compression capability
	 * if KPT is enabled.
	 */
	if (adf_cfg_get_param_value(accel_dev, ADF_GENERAL_SEC, key, val))
		return -EFAULT;
	if (kstrtouint(val, ADF_CFG_BASE_DEC, &kpt_enabled))
		return -EFAULT;

	return update_hw_capability(accel_dev, kpt_enabled);
}
#endif

static const char *get_obj_name(struct adf_accel_dev *accel_dev,
				enum adf_accel_unit_services service)
{

#if defined(QAT_UIO) || defined(QAT_ESXI)
	enum adf_cfg_fw_image_type fw_image_type = ADF_FW_IMAGE_DEFAULT;

	struct adf_hw_device_data *hw_data = accel_dev->hw_device;

	if (hw_data->get_fw_image_type(accel_dev, &fw_image_type))
		return NULL;

	switch (fw_image_type) {
	case ADF_FW_IMAGE_DEFAULT:
		return ADF_CXXX_AE_FW_NAME;
	case ADF_FW_IMAGE_CRYPTO:
		return ADF_CXXX_AE_FW_NAME_CRYPTO;
	case ADF_FW_IMAGE_COMPRESSION:
		return ADF_CXXX_AE_FW_NAME_COMPRESSION;
	case ADF_FW_IMAGE_CUSTOM1:
		return ADF_CXXX_AE_FW_NAME_CUSTOM1;
	default:
		dev_err(&GET_DEV(accel_dev),
			"Unsupported ServicesProfile.\n");
		return NULL;
	}
#else /* QAT_UIO || QAT_ESXI */
	return ADF_CXXX_AE_FW_NAME;
#endif
}

static uint32_t get_objs_num(struct adf_accel_dev *accel_dev)
{
	return 1;
}

static uint32_t get_obj_cfg_ae_mask(struct adf_accel_dev *accel_dev,
				    enum adf_accel_unit_services services)
{
	return accel_dev->hw_device->ae_mask;
}

void adf_init_hw_data_c62x(struct adf_hw_device_data *hw_data)
{
	hw_data->dev_class = &c62x_class;
	hw_data->instance_id = c62x_class.instances++;
	hw_data->num_banks = ADF_C62X_ETR_MAX_BANKS;
	hw_data->num_rings_per_bank = ADF_ETR_MAX_RINGS_PER_BANK;
	hw_data->num_accel = ADF_C62X_MAX_ACCELERATORS;
	hw_data->num_logical_accel = 1;
	hw_data->num_engines = ADF_C62X_MAX_ACCELENGINES;
	hw_data->tx_rx_gap = ADF_C62X_RX_RINGS_OFFSET;
	hw_data->tx_rings_mask = ADF_C62X_TX_RINGS_MASK;
	hw_data->alloc_irq = adf_isr_resource_alloc;
	hw_data->free_irq = adf_isr_resource_free;
	hw_data->enable_error_correction = adf_enable_error_correction;
	hw_data->check_uncorrectable_error = adf_check_uncorrectable_error;
	hw_data->print_err_registers = adf_print_err_registers;
	hw_data->disable_error_interrupts = adf_disable_error_interrupts;
	hw_data->get_accel_mask = get_accel_mask;
	hw_data->get_ae_mask = get_ae_mask;
	hw_data->get_num_accels = get_num_accels;
	hw_data->get_num_aes = get_num_aes;
	hw_data->get_sram_bar_id = get_sram_bar_id;
	hw_data->get_etr_bar_id = get_etr_bar_id;
	hw_data->get_misc_bar_id = get_misc_bar_id;
	hw_data->get_pf2vf_offset = get_pf2vf_offset;
	hw_data->get_vf2pf_offset = get_pf2vf_offset;
	hw_data->pfvf_type_shift = ADF_PFVF_1X_MSGTYPE_SHIFT;
	hw_data->pfvf_type_mask = ADF_PFVF_1X_MSGTYPE_MASK;
	hw_data->pfvf_data_shift = ADF_PFVF_1X_MSGDATA_SHIFT;
	hw_data->pfvf_data_mask = ADF_PFVF_1X_MSGDATA_MASK;
	hw_data->get_arb_info = get_arb_info;
	hw_data->get_admin_info = get_admin_info;
	hw_data->get_errsou_offset = get_errsou_offset;
	hw_data->get_clock_speed = get_clock_speed;
	hw_data->get_sku = get_sku;
	hw_data->heartbeat_ctr_num = ADF_NUM_HB_CNT_PER_AE;
#if defined(CONFIG_PCI_IOV)
	hw_data->process_and_get_vf2pf_int = process_and_get_vf2pf_int;
	hw_data->enable_vf2pf_interrupts = enable_vf2pf_interrupts;
	hw_data->disable_vf2pf_interrupts = disable_vf2pf_interrupts;
	hw_data->check_arbitrary_numvfs = check_arbitrary_numvfs;
#endif
	hw_data->fw_name = ADF_C62X_FW;
	hw_data->fw_mmp_name = ADF_C62X_MMP;
	hw_data->init_admin_comms = adf_init_admin_comms;
	hw_data->exit_admin_comms = adf_exit_admin_comms;
	hw_data->configure_iov_threads = adf_configure_iov_threads;
	hw_data->disable_iov = adf_disable_sriov;
	hw_data->send_admin_init = adf_send_admin_init;
	hw_data->init_arb = adf_init_arb;
	hw_data->exit_arb = adf_exit_arb;
	hw_data->disable_arb = adf_disable_arb;
	hw_data->get_arb_mapping = adf_get_arbiter_mapping;
	hw_data->enable_ints = adf_enable_ints;
	hw_data->set_ssm_wdtimer = adf_set_ssm_wdtimer;
	hw_data->check_slice_hang = adf_check_slice_hang;
	hw_data->enable_vf2pf_comms = adf_pf_enable_vf2pf_comms;
	hw_data->disable_vf2pf_comms = adf_pf_disable_vf2pf_comms;
	hw_data->restore_device = adf_dev_restore;
	hw_data->min_iov_compat_ver = ADF_PFVF_COMPATIBILITY_VERSION;
	hw_data->get_heartbeat_status = adf_get_heartbeat_status;
	hw_data->get_ae_clock = get_ae_clock;
#ifdef QAT_HB_FAIL_SIM
	hw_data->adf_disable_ae_wrk_thds = adf_disable_arbiter;
#endif
	hw_data->reset_device = adf_reset_flr;
	hw_data->ring_to_svc_map = ADF_DEFAULT_RING_TO_SRV_MAP;
	hw_data->get_objs_num = get_objs_num;
	hw_data->get_obj_name = get_obj_name;
	hw_data->get_obj_cfg_ae_mask = get_obj_cfg_ae_mask;
	hw_data->clock_frequency = ADF_C62X_AE_FREQ;
	hw_data->measure_clock = measure_clock;
	hw_data->extended_dc_capabilities = 0;
	hw_data->get_storage_enabled = get_storage_enabled;
	hw_data->query_storage_cap = 1;
	hw_data->get_accel_cap = c62x_get_hw_cap;
	hw_data->pre_reset = adf_dev_pre_reset;
	hw_data->post_reset = adf_dev_post_reset;
#ifdef QAT_UIO
#ifdef QAT_KPT
	hw_data->enable_kpt = enable_kpt;
#endif
	hw_data->config_device = adf_config_device;
	hw_data->set_asym_rings_mask = adf_cfg_set_asym_rings_mask;
	hw_data->get_ring_to_svc_map = adf_cfg_get_services_enabled;
	hw_data->get_accel_algo_cap = adf_cfg_get_accel_algo_cap;
#endif
#if defined(QAT_UIO) || defined(QAT_ESXI)
	hw_data->update_accel_cap_mask = update_accel_cap_mask_c62x;
	hw_data->get_fw_image_type = adf_cfg_get_fw_image_type;
#endif /* QAT_UIO || QAT_ESXI */
	gen2_init_hw_csr_info(&hw_data->csr_info);
	hw_data->default_coalesce_timer = ADF_C62X_ACCEL_DEF_COALESCE_TIMER;
	hw_data->coalescing_min_time = ADF_C62X_COALESCING_MIN_TIME;
	hw_data->coalescing_max_time = ADF_C62X_COALESCING_MAX_TIME;
	hw_data->coalescing_def_time = ADF_C62X_COALESCING_DEF_TIME;
}

void adf_clean_hw_data_c62x(struct adf_hw_device_data *hw_data)
{
	hw_data->dev_class->instances--;
}
