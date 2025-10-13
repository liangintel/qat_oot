// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2018 - 2022 Intel Corporation */
#include <linux/atomic.h>
#include <adf_accel_devices.h>
#include <adf_common_drv.h>
#include <adf_pf2vf_msg.h>
#include <adf_dev_err.h>
#include <adf_cfg.h>
#include <adf_transport_access_macros_gen4.h>
#include <adf_gen4_hw_csr_data.h>
#include "adf_gen4_hw_data.h"
#include "adf_gen4_ras.h"
#include "adf_heartbeat.h"
#include "adf_4xxx_hw_data.h"
#include "adf_4xxx_ras.h"
#include "adf_4xxx_reset.h"
#include "adf_gen4_pm.h"
#include "adf_gen4_tl.h"
#include "icp_qat_hw.h"
#include "adf_adi.h"
#include "adf_gen4_adi_hal.h"
#include "adf_gen4_timer.h"
#include "adf_pasid.h"
#include "adf_rl_v2.h"
#include "adf_gen4_kpt.h"

#define MAX_CLUSTER 4

/* Accel unit information */
static const struct adf_accel_unit adf_4xxx_au_a_ae[] = {
	{0x1,  0x1,   0xF,       0x1B,       4, ADF_ACCEL_SERVICE_NULL},
	{0x2,  0x1,   0xF0,      0x6C0,      4, ADF_ACCEL_SERVICE_NULL},
	{0x4,  0x1,   0x100,     0xF000,     1, ADF_ACCEL_ADMIN},
};

/* Masks representing ME thread-service mappings.
 * Thread 7 carries out Admin work and is thus
 * left out.
 */
static u8 default_active_thd_mask = 0x7F;
static u8 dc_me_active_thd_mask = 0x03;

static u32 thrd_to_arb_map_gen[ADF_4XXX_MAX_ACCELENGINES] = {0};

#define ADF_4XXX_DEFAULT_RING_TO_SRV_MAP \
	(ASYM | SYM << ADF_CFG_SERV_RING_PAIR_1_SHIFT | \
	ASYM << ADF_CFG_SERV_RING_PAIR_2_SHIFT | \
	SYM << ADF_CFG_SERV_RING_PAIR_3_SHIFT)

#define ADF_4XXX_ASYM_SYM ADF_4XXX_DEFAULT_RING_TO_SRV_MAP

#define ADF_4XXX_DC \
	(COMP | COMP << ADF_CFG_SERV_RING_PAIR_1_SHIFT | \
	COMP << ADF_CFG_SERV_RING_PAIR_2_SHIFT | \
	COMP << ADF_CFG_SERV_RING_PAIR_3_SHIFT)

#define ADF_4XXX_SYM \
	(SYM | SYM << ADF_CFG_SERV_RING_PAIR_1_SHIFT | \
	SYM << ADF_CFG_SERV_RING_PAIR_2_SHIFT | \
	SYM << ADF_CFG_SERV_RING_PAIR_3_SHIFT)

#define ADF_4XXX_ASYM \
	(ASYM | ASYM << ADF_CFG_SERV_RING_PAIR_1_SHIFT | \
	ASYM << ADF_CFG_SERV_RING_PAIR_2_SHIFT | \
	ASYM << ADF_CFG_SERV_RING_PAIR_3_SHIFT)

#define ADF_4XXX_ASYM_DC \
	(ASYM | ASYM << ADF_CFG_SERV_RING_PAIR_1_SHIFT | \
	COMP << ADF_CFG_SERV_RING_PAIR_2_SHIFT | \
	COMP << ADF_CFG_SERV_RING_PAIR_3_SHIFT)

#define ADF_4XXX_SYM_DC \
	(SYM | SYM << ADF_CFG_SERV_RING_PAIR_1_SHIFT | \
	COMP << ADF_CFG_SERV_RING_PAIR_2_SHIFT | \
	COMP << ADF_CFG_SERV_RING_PAIR_3_SHIFT)

#define ADF_4XXX_NA \
	(NA | NA << ADF_CFG_SERV_RING_PAIR_1_SHIFT | \
	NA << ADF_CFG_SERV_RING_PAIR_2_SHIFT | \
	NA << ADF_CFG_SERV_RING_PAIR_3_SHIFT)

struct adf_enabled_services {
	const char svcs_enabled[ADF_CFG_MAX_VAL_LEN_IN_BYTES];
	u16 rng_to_svc_msk;
};

static struct adf_enabled_services adf_4xxx_svcs[] = {
	{"dc", ADF_4XXX_DC},
	{"sym", ADF_4XXX_SYM},
	{"asym", ADF_4XXX_ASYM},
	{"dc;asym", ADF_4XXX_ASYM_DC},
	{"asym;dc", ADF_4XXX_ASYM_DC},
	{"sym;dc", ADF_4XXX_SYM_DC},
	{"dc;sym", ADF_4XXX_SYM_DC},
	{"asym;sym", ADF_4XXX_ASYM_SYM},
	{"sym;asym", ADF_4XXX_ASYM_SYM},
};

static struct adf_hw_device_class adf_4xxx_class = {
	.name = ADF_4XXX_DEVICE_NAME,
	.type = DEV_4XXX,
	.instances = 0
};

static u32 get_accel_mask(struct adf_accel_dev *accel_dev)
{
	return ADF_4XXX_ACCELERATORS_MASK;
}

static u32 get_ae_mask(struct adf_accel_dev *accel_dev)
{
	struct pci_dev *pdev = accel_dev->accel_pci_dev.pci_dev;
	u32 me_disable, me_mask = ADF_4XXX_ACCELENGINES_MASK;
	unsigned long me_disable_mask;
	int i;

	pci_read_config_dword(pdev, ADF_GEN4_FUSECTL4_OFFSET, &me_disable);
	me_disable_mask = me_disable;
	me_mask &= ~me_disable;
	for_each_set_bit(i, &me_disable_mask,
			 ADF_4XXX_MAX_ACCELENGINES - 1) {
		if (!(i & 0x3))
			me_mask &= ~(0xf << i);
	}

	return me_mask;
}

static u32 get_num_accels(struct adf_hw_device_data *self)
{
	return ADF_4XXX_MAX_ACCELERATORS;
}

static u32 get_num_aes(struct adf_hw_device_data *self)
{
	u32 i, ctr = 0;

	if (!self || !self->ae_mask)
		return 0;

	for (i = 0; i < ADF_4XXX_MAX_ACCELENGINES; i++) {
		if (self->ae_mask & (1 << i))
			ctr++;
	}
	return ctr;
}

static u32 get_misc_bar_id(struct adf_hw_device_data *self)
{
	return ADF_GEN4_PMISC_BAR;
}

static u32 get_etr_bar_id(struct adf_hw_device_data *self)
{
	return ADF_GEN4_ETR_BAR;
}

static u32 get_uq_bar_id(struct adf_hw_device_data *self)
{
	return ADF_GEN4_UQ_BAR;
}

static u32 get_sram_bar_id(struct adf_hw_device_data *self)
{
	return ADF_GEN4_SRAM_BAR;
}

/**
 * adf_4xxx_check_slice_hang() - Check slice hang status
 * @accel_dev: Structure holding accelerator data
 *
 * Return: true if a slice hange interrupt is serviced..
 */
static bool adf_4xxx_check_slice_hang(struct adf_accel_dev *accel_dev)
{
	return false;
}

static u32 get_clock_speed(struct adf_hw_device_data *self)
{
	/* CPP clock is half high-speed clock */
	return self->clock_frequency / 2;

}

static enum dev_sku_info get_sku(struct adf_hw_device_data *self)
{
	return DEV_SKU_1;
}

#if defined(CONFIG_PCI_IOV)
static void mask_misc_irq(struct adf_accel_dev *accel_dev, const bool mask_irq)
{
	void __iomem *addr;

	addr = (&GET_BARS(accel_dev)[ADF_GEN4_PMISC_BAR])->virt_addr;

	ADF_CSR_WR(addr, ADF_4XXX_SMIAPF_MASK_OFFSET, mask_irq);
}

static void process_and_get_vf2pf_int(void __iomem *pmiscbar_addr,
				      u32 vf_mask_sets[ADF_MAX_VF2PF_SET])
{
	int i;
	u32 sou, mask;

	sou = ADF_CSR_RD(pmiscbar_addr, ADF_4XXX_VM2PF_SOU);
	mask = ADF_CSR_RD(pmiscbar_addr, ADF_4XXX_VM2PF_MSK);
	vf_mask_sets[0] = sou & ~mask;

	for (i = 1; i < ADF_MAX_VF2PF_SET; i++)
		vf_mask_sets[i] = 0;

	/*
	 * Due to HW limitations, when disabling the interrupts, we can't
	 * just disable the requested sources, as this would lead to missed
	 * interrupts if sources change just before writing to ERRMSK3.
	 * To resolve this, disable all interrupts and re-enable only the
	 * sources that are not currently being serviced and the sources that
	 * were not already disabled. Re-enabling will trigger a new interrupt
	 * for the sources that have changed in the meantime, if any.
	 */
	ADF_CSR_WR(pmiscbar_addr, ADF_4XXX_VM2PF_MSK, ADF_VF2PF_INT_MASK);
	ADF_CSR_WR(pmiscbar_addr, ADF_4XXX_VM2PF_MSK, mask | sou);
}

static void enable_vm2pf_interrupts(void __iomem *pmiscbar_addr,
				    u32 vf_mask, u8 i)
{
	if (i)
		return;

	adf_csr_fetch_and_and(pmiscbar_addr,
			      ADF_4XXX_VM2PF_MSK,
			      ~vf_mask);
}

static void disable_vm2pf_interrupts(void __iomem *pmiscbar_addr,
				     u32 vf_mask, u8 i)
{
	if (i)
		return;

	adf_csr_fetch_and_or(pmiscbar_addr,
			     ADF_4XXX_VM2PF_MSK,
			     vf_mask);
}

static int check_arbitrary_numvfs(struct adf_accel_dev *accel_dev,
				  const int numvfs)
{
	int totalvfs = pci_sriov_get_totalvfs(accel_to_pci_dev(accel_dev));

	return numvfs > totalvfs ? totalvfs : numvfs;
}
#endif

static struct adf_accel_unit *get_au_by_ae(struct adf_accel_dev *accel_dev,
					   int ae_num)
{
	int i = 0;
	struct adf_accel_unit *accel_unit =  accel_dev->au_info->au;

	if (!accel_unit)
		return NULL;

	for (i = 0; i < ADF_4XXX_MAX_ACCELUNITS; i++)
		if (accel_unit[i].ae_mask & BIT(ae_num))
			return &accel_unit[i];

	return NULL;
}

static bool check_accel_unit_service(enum adf_accel_unit_services au_srv,
				     enum adf_cfg_service_type ring_srv)
{
	if ((au_srv & ADF_ACCEL_SERVICE_NULL) && ring_srv == NA)
		return true;
	if ((au_srv & ADF_ACCEL_COMPRESSION) && ring_srv == COMP)
		return true;
	if ((au_srv & ADF_ACCEL_ASYM) && ring_srv == ASYM)
		return true;
	if ((au_srv & ADF_ACCEL_CRYPTO) && ring_srv == SYM)
		return true;

	return false;
}

static void adf_4xxx_cfg_gen_dispatch_arbiter(struct adf_accel_dev *accel_dev,
					   u32 *thrd_to_arb_map_gen)
{
	struct adf_accel_unit *au = NULL;
	int engine = 0;
	int thread = 0;
	int service;
	u16 service_type;
	u32 service_mask;
	unsigned long thd_srv_mask = default_active_thd_mask;
	u16 ena_srv_mask = accel_dev->hw_device->ring_to_svc_map;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;

	for (engine = 0;
		engine < ADF_4XXX_MAX_ACCELENGINES - 1;
		engine++) {
		thrd_to_arb_map_gen[engine] = 0;
		service_mask = 0;
		au = get_au_by_ae(accel_dev, engine);
		if (!au)
			continue;

		for (service = 0; service < ADF_CFG_MAX_SERVICES;
		     service++) {
			service_type =
				GET_SRV_TYPE(ena_srv_mask, service);
			if (check_accel_unit_service(au->services,
						     service_type))
				service_mask |= BIT(service);
		}

		if (au->services == ADF_ACCEL_COMPRESSION)
			thd_srv_mask = dc_me_active_thd_mask;
		else if (au->services == ADF_ACCEL_ASYM)
			thd_srv_mask = hw_data->asym_ae_active_thd_mask;
		else
			thd_srv_mask = default_active_thd_mask;

		for_each_set_bit(thread, &thd_srv_mask, 8) {
			thrd_to_arb_map_gen[engine] |=
				(service_mask << (ADF_CFG_MAX_SERVICES * thread));
		}

	}
}

static void adf_get_arbiter_mapping(struct adf_accel_dev *accel_dev,
				    u32 const **arb_map_config)
{
	adf_4xxx_cfg_gen_dispatch_arbiter(accel_dev, thrd_to_arb_map_gen);
	*arb_map_config = thrd_to_arb_map_gen;
}

static u32 get_pf2vm_offset(u32 i)
{
	return ADF_4XXX_PF2VM_OFFSET(i);
}

static u32 get_vm2pf_offset(u32 i)
{
	return ADF_4XXX_VM2PF_OFFSET(i);
}


static void get_arb_info(struct arb_info *arb_csrs_info)
{
	arb_csrs_info->arbiter_offset = ADF_4XXX_ARB_OFFSET;
	arb_csrs_info->wrk_thd_2_srv_arb_map =
					ADF_4XXX_ARB_WRK_2_SER_MAP_OFFSET;
}

static void get_admin_info(struct admin_info *admin_csrs_info)
{
	admin_csrs_info->mailbox_offset = ADF_4XXX_MAILBOX_BASE_OFFSET;
	admin_csrs_info->admin_msg_ur = ADF_4XXX_ADMINMSGUR_OFFSET;
	admin_csrs_info->admin_msg_lr = ADF_4XXX_ADMINMSGLR_OFFSET;
}

static void adf_enable_error_correction(struct adf_accel_dev *accel_dev)
{
	struct adf_bar *misc_bar = &GET_BARS(accel_dev)[ADF_GEN4_PMISC_BAR];
	void __iomem *csr = misc_bar->virt_addr;

	/* Mask VFLRNOTIFY interrupt */
	adf_csr_fetch_and_or(csr, ADF_GEN4_ERRMSK3,
			     ADF_GEN4_VFLRNOTIFY);

	/* Parity Check Enable */
	adf_gen4_enable_iosf_parity(accel_dev);
}

static void adf_enable_ints(struct adf_accel_dev *accel_dev)
{
	void __iomem *addr;

	addr = (&GET_BARS(accel_dev)[ADF_GEN4_PMISC_BAR])->virt_addr;

	/* Enable bundle interrupts */
	ADF_CSR_WR(addr, ADF_4XXX_SMIAPF_RP_X0_MASK_OFFSET, 0);
	ADF_CSR_WR(addr, ADF_4XXX_SMIAPF_RP_X1_MASK_OFFSET, 0);

	/*Enable misc interrupts*/
	ADF_CSR_WR(addr, ADF_4XXX_SMIAPF_MASK_OFFSET, 0);
}

static u32 adf_4xxx_get_hw_cap(struct adf_accel_dev *accel_dev)
{
	struct pci_dev *pdev = accel_dev->accel_pci_dev.pci_dev;
	u32 fusectl1;
	u32 capabilities_sym, capabilities_sym_cipher, capabilities_sym_auth,
		capabilities_asym, capabilities_dc, capabilities_other;

	capabilities_other = ICP_ACCEL_CAPABILITIES_RL;

	/* Read accelerator capabilities mask */
	pci_read_config_dword(pdev, ADF_GEN4_FUSECTL1_OFFSET,
			      &fusectl1);
	dev_info(&GET_DEV(accel_dev), "Fuses %x\n", fusectl1);

	capabilities_sym_cipher = ICP_ACCEL_CAPABILITIES_HKDF |
				  ICP_ACCEL_CAPABILITIES_SM4 |
				  ICP_ACCEL_CAPABILITIES_CHACHA_POLY |
				  ICP_ACCEL_CAPABILITIES_AESGCM_SPC |
				  ICP_ACCEL_CAPABILITIES_AES_V2;
	capabilities_sym_auth = ICP_ACCEL_CAPABILITIES_SM3 |
				ICP_ACCEL_CAPABILITIES_SHA3 |
				ICP_ACCEL_CAPABILITIES_SHA3_EXT;

	/* A set bit in fusectl1 means the feature is OFF in this SKU */
	if (fusectl1 & ICP_ACCEL_4XXX_MASK_CIPHER_SLICE) {
		capabilities_sym_cipher &= ~ICP_ACCEL_CAPABILITIES_HKDF;
		capabilities_sym_cipher &= ~ICP_ACCEL_CAPABILITIES_SM4;
	}

	if (fusectl1 & ICP_ACCEL_4XXX_MASK_UCS_SLICE) {
		capabilities_sym_cipher &= ~ICP_ACCEL_CAPABILITIES_CHACHA_POLY;
		capabilities_sym_cipher &= ~ICP_ACCEL_CAPABILITIES_AESGCM_SPC;
		capabilities_sym_cipher &= ~ICP_ACCEL_CAPABILITIES_AES_V2;
	}

	if (fusectl1 & ICP_ACCEL_4XXX_MASK_AUTH_SLICE) {
		capabilities_sym_auth &= ~ICP_ACCEL_CAPABILITIES_SM3;
		capabilities_sym_auth &= ~ICP_ACCEL_CAPABILITIES_SHA3;
		capabilities_sym_auth &= ~ICP_ACCEL_CAPABILITIES_SHA3_EXT;
	}

	if (fusectl1 & ICP_ACCEL_4XXX_MASK_SMX_SLICE) {
		capabilities_sym_cipher &= ~ICP_ACCEL_CAPABILITIES_SM4;
		capabilities_sym_auth &= ~ICP_ACCEL_CAPABILITIES_SM3;
	}

	if (capabilities_sym_cipher)
		capabilities_sym_cipher |= ICP_ACCEL_CAPABILITIES_CIPHER;

	if (capabilities_sym_auth)
		capabilities_sym_auth |= ICP_ACCEL_CAPABILITIES_AUTHENTICATION;

	capabilities_sym = capabilities_sym_cipher | capabilities_sym_auth;

	if (capabilities_sym)
		capabilities_sym |= ICP_ACCEL_CAPABILITIES_CRYPTO_SYMMETRIC;

	capabilities_asym = ICP_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC |
			  ICP_ACCEL_CAPABILITIES_SM2 |
			  ICP_ACCEL_CAPABILITIES_ECEDMONT;

	if (fusectl1 & ICP_ACCEL_4XXX_MASK_PKE_SLICE) {
		capabilities_asym &= ~ICP_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC;
		capabilities_asym &= ~ICP_ACCEL_CAPABILITIES_SM2;
		capabilities_asym &= ~ICP_ACCEL_CAPABILITIES_ECEDMONT;
	}

	capabilities_dc = ICP_ACCEL_CAPABILITIES_COMPRESSION |
			  ICP_ACCEL_CAPABILITIES_LZ4_COMPRESSION |
			  ICP_ACCEL_CAPABILITIES_LZ4S_COMPRESSION |
			  ICP_ACCEL_CAPABILITIES_CNV_INTEGRITY64;

	if (fusectl1 & ICP_ACCEL_4XXX_MASK_COMPRESS_SLICE) {
		capabilities_dc &= ~ICP_ACCEL_CAPABILITIES_COMPRESSION;
		capabilities_dc &= ~ICP_ACCEL_CAPABILITIES_LZ4_COMPRESSION;
		capabilities_dc &= ~ICP_ACCEL_CAPABILITIES_LZ4S_COMPRESSION;
		capabilities_dc &= ~ICP_ACCEL_CAPABILITIES_CNV_INTEGRITY64;
	}

	return capabilities_sym | capabilities_dc | capabilities_asym |
	       capabilities_other;
}

static u32 get_hb_clock(struct adf_hw_device_data *self)
{
	/*
	 * 4XXX uses KPT counter for HB
	 */
	return ADF_GEN4_KPT_COUNTER_FREQ;
}

static u32 get_ae_clock(struct adf_hw_device_data *self)
{
	/*
	 * Clock update interval is <16> ticks for qat_4xxx.
	 */
	return self->clock_frequency / 16;
}

static int adf_4xxx_configure_accel_units(struct adf_accel_dev *accel_dev)
{
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES] = {0};
	char val_str[ADF_CFG_MAX_VAL_LEN_IN_BYTES] = {0};

	snprintf(key, sizeof(key), ADF_SERVICES_ENABLED);

	/* If DC Chaining already enabled then don't re-configure */
	if (accel_dev->chaining_enabled)
		goto err;

	if (adf_cfg_section_add(accel_dev, ADF_GENERAL_SEC))
		goto err;

	snprintf(val_str, sizeof(val_str), ADF_CFG_ASYM
		    ADF_SERVICES_SEPARATOR ADF_CFG_SYM);

	if (adf_cfg_add_key_value_param(accel_dev, ADF_GENERAL_SEC,
					key, (void *)val_str, ADF_STR))
		goto err;

	return 0;
err:
	dev_err(&GET_DEV(accel_dev), "Failed to configure accel units\n");
	return -EINVAL;
}

#ifdef QAT_UIO
static void adf_set_asym_rings_mask(struct adf_accel_dev *accel_dev)
{
	accel_dev->hw_device->asym_rings_mask = ADF_4XXX_DEF_ASYM_MASK;
}
#endif

static int get_ring_to_svc_map(struct adf_accel_dev *accel_dev,
			       u16 *ring_to_svc_map)
{
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	char val[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	u32 i = 0;

	*ring_to_svc_map = 0;
	/* Get the services enabled by user */
	snprintf(key, sizeof(key), ADF_SERVICES_ENABLED);
	if (adf_cfg_get_param_value(accel_dev, ADF_GENERAL_SEC, key, val))
		return -EFAULT;

	for (i = 0; i < ARRAY_SIZE(adf_4xxx_svcs); i++) {
		if (!strncmp(val, adf_4xxx_svcs[i].svcs_enabled,
				ADF_CFG_MAX_KEY_LEN_IN_BYTES)) {
			*ring_to_svc_map = adf_4xxx_svcs[i].rng_to_svc_msk;
			return 0;
		}
	}

	dev_err(&GET_DEV(accel_dev), "Invalid services enabled: %s\n", val);
	return -EFAULT;
}

static u32 get_num_accel_units(struct adf_hw_device_data *self)
{
	return ADF_4XXX_MAX_ACCELUNITS;
}

static void get_accel_unit(struct adf_hw_device_data *self,
			   struct adf_accel_unit **accel_unit)
{
	memcpy(*accel_unit, adf_4xxx_au_a_ae, sizeof(adf_4xxx_au_a_ae));
}

static void adf_exit_accel_unit_services(struct adf_accel_dev *accel_dev)
{
	if (accel_dev->au_info) {
		kfree(accel_dev->au_info->au);
		accel_dev->au_info->au = NULL;
		kfree(accel_dev->au_info);
		accel_dev->au_info = NULL;
	}
}

static int get_accel_unit_config(struct adf_accel_dev *accel_dev,
				 u8 *num_sym_au,
				 u8 *num_dc_au,
				 u8 *num_asym_au)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	char val[ADF_CFG_MAX_VAL_LEN_IN_BYTES];
	u32 num_au = hw_data->get_num_accel_units(hw_data);
	/* One AU will be allocated by default if a service enabled */
	u32 alloc_au = 1;
	/* There's always one AU that is used for Admin AE */
	u32 service_mask = ADF_ACCEL_ADMIN;
	char *token, *cur_str;
	u32 disabled_caps = 0;
	u32 required_capability = 0;

	/* Get the services enabled by user */
	snprintf(key, sizeof(key), ADF_SERVICES_ENABLED);
	if (adf_cfg_get_param_value(accel_dev, ADF_GENERAL_SEC, key, val))
		return -EFAULT;
	cur_str = val;
	token = strsep(&cur_str, ADF_SERVICES_SEPARATOR);
	while (token) {
		if (!strncmp(token, ADF_CFG_SYM, strlen(ADF_CFG_SYM))) {
			service_mask |= ADF_ACCEL_CRYPTO;
			required_capability |=
				ICP_ACCEL_CAPABILITIES_CRYPTO_SYMMETRIC;
		}

		if (!strncmp(token, ADF_CFG_ASYM, strlen(ADF_CFG_ASYM))) {
			service_mask |= ADF_ACCEL_ASYM;
			required_capability |=
				ICP_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC;
		}

		if (!strncmp(token, ADF_SERVICE_DC, strlen(ADF_SERVICE_DC))) {
			service_mask |= ADF_ACCEL_COMPRESSION;
			required_capability |=
				ICP_ACCEL_CAPABILITIES_COMPRESSION;
			if (accel_dev->chaining_enabled) {
				service_mask |= ADF_ACCEL_CRYPTO;
				required_capability |=
					ICP_ACCEL_CAPABILITIES_CRYPTO_SYMMETRIC;
			}
		}

		token = strsep(&cur_str, ADF_SERVICES_SEPARATOR);
	}

	/* Ensure the user won't enable more services than it can support */
	if (hweight32(service_mask) > num_au) {
		dev_err(&GET_DEV(accel_dev), "Can't enable more services than ");
		dev_err(&GET_DEV(accel_dev), "%d!\n", num_au);
		return -EFAULT;
	} else if (hweight32(service_mask) == 2) {
		/* Due to limitation, besides AU for Admin AE
		 * only 2 more AUs can be allocated
		 */
		alloc_au = 2;
	}

	if (service_mask & ADF_ACCEL_CRYPTO)
		*num_sym_au = alloc_au;
	if (service_mask & ADF_ACCEL_ASYM)
		*num_asym_au = alloc_au;
	if (service_mask & ADF_ACCEL_COMPRESSION)
		*num_dc_au = alloc_au;

	/*update capability*/
	if (!*num_sym_au || !(service_mask & ADF_ACCEL_CRYPTO)) {
		disabled_caps = ICP_ACCEL_CAPABILITIES_CRYPTO_SYMMETRIC
			| ICP_ACCEL_CAPABILITIES_CIPHER
			| ICP_ACCEL_CAPABILITIES_AUTHENTICATION
			| ICP_ACCEL_CAPABILITIES_SHA3_EXT
			| ICP_ACCEL_CAPABILITIES_SM3
			| ICP_ACCEL_CAPABILITIES_SM4
			| ICP_ACCEL_CAPABILITIES_CHACHA_POLY
			| ICP_ACCEL_CAPABILITIES_AESGCM_SPC
			| ICP_ACCEL_CAPABILITIES_AES_V2;
	}
	if (!*num_asym_au || !(service_mask & ADF_ACCEL_ASYM))
		disabled_caps |= ICP_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC;

	if (!*num_dc_au || !(service_mask & ADF_ACCEL_COMPRESSION)) {
		disabled_caps |= ICP_ACCEL_CAPABILITIES_COMPRESSION
		| ICP_ACCEL_CAPABILITIES_LZ4_COMPRESSION
		| ICP_ACCEL_CAPABILITIES_LZ4S_COMPRESSION
		| ICP_ACCEL_CAPABILITIES_CNV_INTEGRITY64;
		accel_dev->hw_device->extended_dc_capabilities = 0;
	}
	accel_dev->hw_device->accel_capabilities_mask =
		adf_4xxx_get_hw_cap(accel_dev) & ~disabled_caps;

	/* Ensure the user doesn't enable services that are not supported by
	 * accelerator.
	 */
	if (adf_gen4_check_svc_to_hw_capabilities(accel_dev,
						  required_capability)) {
		dev_err(&GET_DEV(accel_dev),
			"HW does not support the configured services!\n");
		return -EFAULT;
	}

	hw_data->service_mask = service_mask;
	hw_data->service_to_load_mask = service_mask;

	return 0;
}

static int adf_4xxx_check_supported_services(struct adf_accel_dev *accel_dev)
{
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	char val[ADF_CFG_MAX_VAL_LEN_IN_BYTES];
	char *services_str = NULL;

	/* Check if DC Chaining selected */
	if (accel_dev->chaining_enabled) {
		/* Get the services enabled by user */
		snprintf(key, sizeof(key), ADF_SERVICES_ENABLED);
		if (adf_cfg_get_param_value(accel_dev, ADF_GENERAL_SEC, key, val)) {
			dev_err(&GET_DEV(accel_dev), "Can't get %s\n",
				ADF_SERVICES_ENABLED);
			return -EFAULT;
		}
		services_str = val;

		/* For 4xxx device, only DC service allowed for DC Chaining mode */
		if (strncmp(services_str,
			     ADF_SERVICE_DC,
			     ADF_CFG_MAX_VAL_LEN_IN_BYTES))
			return -EFAULT;
	}
	return 0;
}

static int adf_init_accel_unit_services(struct adf_accel_dev *accel_dev)
{
	u8 num_sym_au = 0, num_dc_au = 0, num_asym_au = 0;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u32 num_au = hw_data->get_num_accel_units(hw_data);
	u32 au_size = num_au * sizeof(struct adf_accel_unit);
	u8 i;

	if (get_accel_unit_config(accel_dev, &num_sym_au, &num_dc_au,
				    &num_asym_au))
		return -EFAULT;

	accel_dev->au_info = kzalloc(sizeof(*accel_dev->au_info), GFP_KERNEL);
	if (!accel_dev->au_info)
		return -ENOMEM;

	accel_dev->au_info->au = kzalloc(au_size, GFP_KERNEL);
	if (!accel_dev->au_info->au) {
		kfree(accel_dev->au_info);
		accel_dev->au_info = NULL;
		return -ENOMEM;
	}

	accel_dev->au_info->num_cy_au = num_sym_au;
	accel_dev->au_info->num_dc_au = num_dc_au;
	accel_dev->au_info->num_asym_au = num_asym_au;

	get_accel_unit(hw_data, &accel_dev->au_info->au);

	/* Enable ASYM accel units */
	for (i = 0; i < num_au && num_asym_au > 0; i++) {
		if (accel_dev->au_info->au[i].services ==
				ADF_ACCEL_SERVICE_NULL) {
			accel_dev->au_info->au[i].services = ADF_ACCEL_ASYM;
			num_asym_au--;
		}
	}
	/* Enable SYM accel units */
	for (i = 0; i < num_au && num_sym_au > 0; i++) {
		if (accel_dev->au_info->au[i].services ==
				ADF_ACCEL_SERVICE_NULL) {
			accel_dev->au_info->au[i].services = ADF_ACCEL_CRYPTO;
			num_sym_au--;
		}
	}
	/* Enable compression accel units */
	for (i = 0; i < num_au && num_dc_au > 0; i++) {
		if (accel_dev->au_info->au[i].services ==
				ADF_ACCEL_SERVICE_NULL) {
			accel_dev->au_info->au[i].services =
				ADF_ACCEL_COMPRESSION;
			num_dc_au--;
		}
	}
	accel_dev->au_info->dc_ae_msk |=
		hw_data->get_obj_cfg_ae_mask(accel_dev, ADF_ACCEL_COMPRESSION);

	return 0;
}

static int adf_init_accel_units(struct adf_accel_dev *accel_dev)
{
	return adf_init_accel_unit_services(accel_dev);
}

static void adf_exit_accel_units(struct adf_accel_dev *accel_dev)
{
	/* reset the AU service */
	adf_exit_accel_unit_services(accel_dev);
}

static const char *get_obj_name_4xxx(struct adf_accel_dev *accel_dev,
				     enum adf_accel_unit_services service)
{
	switch (service) {
	case ADF_ACCEL_ASYM:
		return ADF_4XXX_ASYM_OBJ;
	case ADF_ACCEL_CRYPTO:
		return ADF_4XXX_SYM_OBJ;
	case ADF_ACCEL_COMPRESSION:
		return ADF_4XXX_DC_OBJ;
	case ADF_ACCEL_ADMIN:
		return ADF_4XXX_ADMIN_OBJ;
	default:
		return NULL;
	}
}

static const char *get_obj_name_402xx(struct adf_accel_dev *accel_dev,
				      enum adf_accel_unit_services service)
{
	switch (service) {
	case ADF_ACCEL_ASYM:
		return ADF_402XX_ASYM_OBJ;
	case ADF_ACCEL_CRYPTO:
		return ADF_402XX_SYM_OBJ;
	case ADF_ACCEL_COMPRESSION:
		return ADF_402XX_DC_OBJ;
	case ADF_ACCEL_ADMIN:
		return ADF_402XX_ADMIN_OBJ;
	default:
		return NULL;
	}
}

static uint32_t get_objs_num(struct adf_accel_dev *accel_dev)
{
	return ADF_4XXX_MAX_OBJ;
}

static int
check_ae_exist(unsigned char ae, unsigned int relmask, unsigned char cppmask)
{
	unsigned char ae_per_cluster[MAX_CLUSTER] = {9, 9, 0, 0};
	unsigned int cluster;

	if (cppmask != 0) {
		for (cluster = 0; cluster < MAX_CLUSTER; cluster++) {
			if (ae < ae_per_cluster[cluster])
				break;
			ae -= ae_per_cluster[cluster];
		}
		if ((cppmask & (1 << cluster)) && (relmask & (1 << ae)))
			return 1;
		else
			return 0;
	} else {
		return relmask & (1 << ae);
	}
}

static void get_ring_svc_map_data(int ring_pair_index, u16 ring_to_svc_map,
				  u8 *serv_type, int *ring_index,
				  int *num_rings_per_srv, int bundle_num)
{
	*serv_type = GET_SRV_TYPE(ring_to_svc_map, bundle_num %
			ADF_CFG_NUM_SERVICES);
	*ring_index = 0;
	*num_rings_per_srv = ADF_4XXX_NUM_RINGS_PER_BANK / 2;
}

static void set_dev_err_mask(struct adf_dev_err_mask *dev_err_mask)
{
	dev_err_mask->cppagentcmdpar_mask =
		ADF_4XXX_HICPPAGENTCMDPARERRLOG_MASK;
	dev_err_mask->parerr_ath_cph_mask =
		ADF_4XXX_PARITYERRORMASK_ATH_CPH_MASK;
	dev_err_mask->parerr_cpr_xlt_mask =
		ADF_4XXX_PARITYERRORMASK_CPR_XLT_MASK;
	dev_err_mask->parerr_dcpr_ucs_mask =
		ADF_4XXX_PARITYERRORMASK_DCPR_UCS_MASK;
	dev_err_mask->parerr_pke_mask =
		ADF_4XXX_PARITYERRORMASK_PKE_MASK;
	dev_err_mask->ssmfeatren_mask =
		ADF_4XXX_SSMFEATREN_MASK;
}

static void init_rl_data(struct adf_hw_device_data *hw_data, u32 id)
{
	hw_data->rl_data.pcie_in_bucket_offset =
		ADF_GEN4_RL_TOKEN_PCIEIN_BUCKET;
	hw_data->rl_data.pcie_out_bucket_offset =
		ADF_GEN4_RL_TOKEN_PCIEOUT_BUCKET;
	hw_data->rl_data.ring_to_leaf_offset = ADF_GEN4_RL_RING2LEAF;
	hw_data->rl_data.leaf_to_cluster_offset = ADF_GEN4_RL_LEAF2CLUSTER;
	hw_data->rl_data.cluster_to_service_offset =
		ADF_GEN4_RL_CLUSTER2SERVICE;
	hw_data->rl_data.pcie_scale_multiplier =
		ADF_GEN4_RL_PCIE_SCALE_FACTOR_MUL;
	hw_data->rl_data.pcie_scale_divisor = ADF_GEN4_RL_PCIE_SCALE_FACTOR_DIV;
	hw_data->rl_data.scan_interval = ADF_GEN4_RL_SCANS_PER_SEC;

	switch (id) {
	case ADF_401XX_PCI_DEVICE_ID:
		hw_data->rl_data.max_throughput[ADF_SVC_ASYM] =
			ADF_401XX_RL_MAX_TP_ASYM;
		hw_data->rl_data.max_throughput[ADF_SVC_SYM] =
			ADF_401XX_RL_MAX_TP_SYM;
		hw_data->rl_data.max_throughput[ADF_SVC_DC] =
			ADF_401XX_RL_MAX_TP_DC;
		hw_data->rl_data.slice_reference = ADF_401XX_RL_SLICE_REF;
		hw_data->rl_data.dc_correction = ADF_401XX_RL_CNV_SLICE;
		break;
	default:
		hw_data->rl_data.max_throughput[ADF_SVC_ASYM] =
			ADF_4XXX_RL_MAX_TP_ASYM;
		hw_data->rl_data.max_throughput[ADF_SVC_SYM] =
			ADF_4XXX_RL_MAX_TP_SYM;
		hw_data->rl_data.max_throughput[ADF_SVC_DC] =
			ADF_4XXX_RL_MAX_TP_DC;
		hw_data->rl_data.slice_reference = ADF_4XXX_RL_SLICE_REF;
		hw_data->rl_data.dc_correction = ADF_4XXX_RL_CNV_SLICE;
	}
}

void adf_init_hw_data_4xxx(struct adf_hw_device_data *hw_data, u32 id)
{
	hw_data->dev_class = &adf_4xxx_class;
	hw_data->instance_id = adf_4xxx_class.instances++;
	hw_data->num_banks = ADF_4XXX_ETR_MAX_BANKS;
	hw_data->num_rings_per_bank = ADF_4XXX_NUM_RINGS_PER_BANK;
	hw_data->num_banks_per_vf = ADF_4XXX_NUM_BANKS_PER_VF;
	hw_data->num_accel = ADF_4XXX_MAX_ACCELERATORS;
	hw_data->num_engines = ADF_4XXX_MAX_ACCELENGINES;
	hw_data->check_ae_exist = &check_ae_exist;
	hw_data->num_logical_accel = 1;
	hw_data->tx_rx_gap = ADF_4XXX_RX_RINGS_OFFSET;
	hw_data->tx_rings_mask = ADF_4XXX_TX_RINGS_MASK;
	hw_data->alloc_irq = adf_isr_resource_alloc;
	hw_data->free_irq = adf_isr_resource_free;
	hw_data->enable_error_correction = adf_enable_error_correction;
	hw_data->print_err_registers = adf_gen4_print_err_registers;
	hw_data->ras_interrupts = adf_gen4_ras_interrupts;
	hw_data->init_ras = adf_init_ras_4xxx;
	hw_data->exit_ras = adf_exit_ras_4xxx;
	hw_data->get_accel_mask = get_accel_mask;
	hw_data->get_ae_mask = get_ae_mask;
	hw_data->get_num_accels = get_num_accels;
	hw_data->get_num_aes = get_num_aes;
	hw_data->get_num_svc_aes = adf_gen4_get_num_svc_aes;
	hw_data->get_sram_bar_id = get_sram_bar_id;
	hw_data->get_etr_bar_id = get_etr_bar_id;
	hw_data->get_misc_bar_id = get_misc_bar_id;
	hw_data->get_uq_bar_id = get_uq_bar_id;
	hw_data->get_pf2vf_offset = get_pf2vm_offset;
	hw_data->get_vf2pf_offset = get_vm2pf_offset;
	hw_data->pfvf_type_shift = ADF_PFVF_2X_MSGTYPE_SHIFT;
	hw_data->pfvf_type_mask = ADF_PFVF_2X_MSGTYPE_MASK;
	hw_data->pfvf_data_shift = ADF_PFVF_2X_MSGDATA_SHIFT;
	hw_data->pfvf_data_mask = ADF_PFVF_2X_MSGDATA_MASK;
	hw_data->get_arb_info = get_arb_info;
	hw_data->get_admin_info = get_admin_info;
	hw_data->notify_and_wait_ethernet = NULL;
	hw_data->get_eth_doorbell_msg = NULL;
	hw_data->get_clock_speed = get_clock_speed;
	hw_data->clock_frequency = ADF_4XXX_AE_FREQ;
	hw_data->get_sku = get_sku;
	hw_data->heartbeat_ctr_num = ADF_NUM_HB_CNT_PER_AE;
	set_dev_err_mask(&hw_data->dev_err_mask);
#if defined(CONFIG_PCI_IOV)
	hw_data->mask_misc_irq = mask_misc_irq;
	hw_data->process_and_get_vf2pf_int = process_and_get_vf2pf_int;
	hw_data->enable_vf2pf_interrupts = enable_vm2pf_interrupts;
	hw_data->disable_vf2pf_interrupts = disable_vm2pf_interrupts;
	hw_data->check_arbitrary_numvfs = check_arbitrary_numvfs;
#endif

	switch (id) {
	case ADF_402XX_PCI_DEVICE_ID:
		hw_data->fw_name = ADF_402XX_FW;
		hw_data->fw_mmp_name = ADF_402XX_MMP;
		hw_data->asym_ae_active_thd_mask = DEFAULT_4XXX_ASYM_AE_MASK;
		break;
	case ADF_401XX_PCI_DEVICE_ID:
		hw_data->fw_name = ADF_4XXX_FW;
		hw_data->fw_mmp_name = ADF_4XXX_MMP;
		hw_data->asym_ae_active_thd_mask = DEFAULT_401XX_ASYM_AE_MASK;
		break;

	default:
		hw_data->fw_name = ADF_4XXX_FW;
		hw_data->fw_mmp_name = ADF_4XXX_MMP;
		hw_data->asym_ae_active_thd_mask = DEFAULT_4XXX_ASYM_AE_MASK;
	}
	hw_data->init_admin_comms = adf_init_admin_comms;
	hw_data->exit_admin_comms = adf_exit_admin_comms;
	hw_data->disable_iov = adf_disable_sriov;
	hw_data->send_admin_init = adf_gen4_send_admin_init;
	hw_data->init_arb = adf_init_arb;
	hw_data->exit_arb = adf_exit_arb;
	hw_data->get_arb_mapping = adf_get_arbiter_mapping;
	hw_data->enable_ints = adf_enable_ints;
	hw_data->set_ssm_wdtimer = adf_gen4_set_ssm_wdtimer;
	hw_data->set_msix_rttable = adf_gen4_set_msix_default_rttable;
	hw_data->check_slice_hang = adf_4xxx_check_slice_hang;
	hw_data->enable_vf2pf_comms = adf_pf_enable_vf2pf_comms;
	hw_data->disable_vf2pf_comms = adf_pf_disable_vf2pf_comms;
	hw_data->reset_device = adf_reset_flr;
	hw_data->restore_device = adf_dev_restore;
	hw_data->min_iov_compat_ver = ADF_PFVF_COMPATIBILITY_VERSION;
	hw_data->ring_to_svc_map = ADF_4XXX_DEFAULT_RING_TO_SRV_MAP;
	hw_data->get_ring_svc_map_data = get_ring_svc_map_data;
	hw_data->admin_ae_mask = ADF_4XXX_ADMIN_AE_MASK;
	hw_data->init_accel_units = adf_init_accel_units;
	hw_data->exit_accel_units = adf_exit_accel_units;
	hw_data->get_num_accel_units = get_num_accel_units;
	hw_data->get_objs_num = get_objs_num;
	switch (id) {
	case ADF_402XX_PCI_DEVICE_ID:
		hw_data->get_obj_name = get_obj_name_402xx;
		break;
	default:
		hw_data->get_obj_name = get_obj_name_4xxx;
	}
	hw_data->get_obj_cfg_ae_mask = adf_gen4_get_obj_cfg_ae_mask;
	hw_data->get_service_type = adf_gen4_get_service_type;
	hw_data->init_pm = adf_gen4_init_pm;
	hw_data->switch_drv_active = adf_gen4_switch_drv_active;
	hw_data->exit_pm = adf_gen4_exit_pm;
	hw_data->update_qat_pm_state = adf_gen4_set_pm_drv_active;
	hw_data->check_pm_interrupts = adf_gen4_pm_check_interrupts;
	hw_data->configure_accel_units = adf_4xxx_configure_accel_units;
	hw_data->get_ring_to_svc_map = get_ring_to_svc_map;
	hw_data->query_storage_cap = 0;
	hw_data->init_adis = adf_init_adis;
	hw_data->exit_adis = adf_exit_adis;
	hw_data->init_kpt = adf_gen4_init_kpt;
	hw_data->config_kpt = adf_gen4_config_kpt;
#ifdef QAT_UIO
	hw_data->config_device = adf_config_device;
	hw_data->set_asym_rings_mask = adf_set_asym_rings_mask;
#endif
#if defined(QAT_UIO) || defined(QAT_ESXI)
	hw_data->update_accel_cap_mask = update_accel_cap_mask;
#endif /* QAT_UIO || QAT_ESXI */
#ifdef NON_GPL_COMMON
	hw_data->get_accel_algo_cap = adf_gen4_cfg_get_accel_algo_cap;
#endif
	hw_data->init_rl_v2 = adf_rl_v2_init;
	hw_data->exit_rl_v2 = adf_rl_v2_exit;
	hw_data->config_bank_pasid = adf_pasid_config_bank;
	hw_data->telemetry_init = adf_gen4_init_tl;
	hw_data->telemetry_exit = adf_gen4_exit_tl;
	hw_data->telemetry_calc_data = adf_gen4_calc_tl_data;
	hw_data->ring_pair_reset = adf_gen4_ring_pair_reset;
	hw_data->ring_pair_drain = adf_gen4_ring_pair_drain;
	hw_data->config_ring_irq = adf_gen4_config_ring_irq;
	hw_data->extended_dc_capabilities = 0;
	hw_data->get_hb_clock = get_hb_clock;
	hw_data->get_ae_clock = get_ae_clock;
	hw_data->get_accel_cap = adf_4xxx_get_hw_cap;
	hw_data->get_heartbeat_status = adf_get_heartbeat_status;
	hw_data->int_timer_init = adf_gen4_int_timer_init;
	hw_data->int_timer_exit = adf_gen4_int_timer_exit;
	hw_data->pre_reset = adf_dev_pre_reset;
	hw_data->post_reset = adf_dev_post_reset;
	hw_data->disable_arb = adf_disable_arb;
#ifdef QAT_HB_FAIL_SIM
	hw_data->adf_disable_ae_wrk_thds = adf_disable_arbiter;
	hw_data->adf_set_max_hb_timer = adf_gen4_set_max_hb_timer;
#endif
	/* mmp fw will be loaded  by default */
	hw_data->load_mmp_always = true;
	hw_data->get_uq_base_addr = adf_gen4_get_uq_base_addr;
	gen4_init_hw_csr_info(&hw_data->csr_info);
	gen4_init_adi_ops(&hw_data->adi_ops);
	/* KAT test enabled by default for qat_4xxx */
	hw_data->fw_integr_selftest = true;
	hw_data->init_chaining = adf_gen4_init_chaining;
	hw_data->check_supported_services = adf_4xxx_check_supported_services;
	hw_data->default_coalesce_timer = ADF_4XXX_ACCEL_DEF_COALESCE_TIMER;
	hw_data->coalescing_min_time = ADF_4XXX_COALESCING_MIN_TIME;
	hw_data->coalescing_max_time = ADF_4XXX_COALESCING_MAX_TIME;
	hw_data->coalescing_def_time = ADF_4XXX_COALESCING_DEF_TIME;
	hw_data->get_slices_for_svc = adf_gen4_get_slices_for_svc;
	hw_data->pf_sla_val_provider = adf_pf_sla_value_provider;
	init_rl_data(hw_data, id);
}

void adf_clean_hw_data_4xxx(struct adf_hw_device_data *hw_data)
{
	hw_data->dev_class->instances--;
}
