/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2022 Intel Corporation */
#ifndef ADF_ACCEL_DEVICES_H_
#define ADF_ACCEL_DEVICES_H_
#ifndef USER_SPACE
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/io.h>
#include <linux/ratelimit.h>
#include <linux/pci.h>
#ifndef QAT_NO_AUX
#include <linux/auxiliary_bus.h>
#endif
#include "adf_cfg_common.h"
#include "adf_rl.h"
#else
#include <stdbool.h>
#endif /* USER_SPACE */

#ifdef QAT_UIO
#define NON_GPL_COMMON
#endif

#define ADF_DH895XCC_DEVICE_NAME "dh895xcc"
#define ADF_DH895XCCVF_DEVICE_NAME "dh895xccvf"
#define ADF_C62X_DEVICE_NAME "c6xx"
#define ADF_C62XVF_DEVICE_NAME "c6xxvf"
#define ADF_C3XXX_DEVICE_NAME "c3xxx"
#define ADF_C3XXXVF_DEVICE_NAME "c3xxxvf"
#define ADF_C4XXX_DEVICE_NAME "c4xxx"
#define ADF_C4XXXVF_DEVICE_NAME "c4xxxvf"
#define ADF_D15XX_DEVICE_NAME "d15xx"
#define ADF_D15XXVF_DEVICE_NAME "d15xxvf"
#define ADF_4XXX_DEVICE_NAME "4xxx"
#define ADF_4XXXVF_DEVICE_NAME "4xxxvf"
#define ADF_420XX_DEVICE_NAME "420xx"
#define ADF_420XXVF_DEVICE_NAME "420xxvf"

#define ADF_DH895XCC_PCI_DEVICE_ID 0x435
#define ADF_DH895XCCIOV_PCI_DEVICE_ID 0x443
#define ADF_C62X_PCI_DEVICE_ID 0x37c8
#define ADF_C62XIOV_PCI_DEVICE_ID 0x37c9
#define ADF_C3XXX_PCI_DEVICE_ID 0x19e2
#define ADF_C3XXXIOV_PCI_DEVICE_ID 0x19e3
#define ADF_C3XXX_PCI_DEVICE_ID2 0x18ee
#define ADF_C3XXXIOV_PCI_DEVICE_ID2 0x18ef
#define ADF_D15XX_PCI_DEVICE_ID 0x6f54
#define ADF_D15XXIOV_PCI_DEVICE_ID 0x6f55
#define ADF_C4XXX_PCI_DEVICE_ID 0x18a0
#define ADF_C4XXXIOV_PCI_DEVICE_ID 0x18a1

static inline bool IS_QAT_GEN3(const unsigned int id)
{
	return (id == ADF_C4XXX_PCI_DEVICE_ID);
}

#define ADF_4XXX_PCI_DEVICE_ID 0x4940
#define ADF_4XXXIOV_PCI_DEVICE_ID 0x4941
#define ADF_401XX_PCI_DEVICE_ID 0x4942
#define ADF_401XXIOV_PCI_DEVICE_ID 0x4943
#define ADF_402XX_PCI_DEVICE_ID 0x4944
#define ADF_402XXIOV_PCI_DEVICE_ID 0x4945
#define ADF_420XX_PCI_DEVICE_ID 0x4946
#define ADF_420XXIOV_PCI_DEVICE_ID 0x4947

static inline bool IS_QAT_GEN4(const unsigned int id)
{
	return (id == ADF_4XXX_PCI_DEVICE_ID ||
		id == ADF_4XXXIOV_PCI_DEVICE_ID ||
		id == ADF_402XX_PCI_DEVICE_ID ||
		id == ADF_402XXIOV_PCI_DEVICE_ID ||
		id == ADF_420XX_PCI_DEVICE_ID ||
		id == ADF_420XXIOV_PCI_DEVICE_ID ||
		id == ADF_401XX_PCI_DEVICE_ID ||
		id == ADF_401XXIOV_PCI_DEVICE_ID);
}

static inline int IS_DSS_FWSK_EXPONENT_LEN(const int id)
{
	return  IS_QAT_GEN4(id);
}

static inline int IS_QAT_GEN3_OR_GEN4(const int id)
{
	return (IS_QAT_GEN3(id) || IS_QAT_GEN4(id));
}

#if defined(CONFIG_PCI_IOV)
#define ADF_VF2PF_SET_SIZE 32
#define ADF_MAX_VF2PF_SET 16
#define ADF_VF2PF_SET_OFFSET(set_nr) ((set_nr) * ADF_VF2PF_SET_SIZE)
#define ADF_VF2PF_VFNR_TO_SET(vf_nr) ((vf_nr) / ADF_VF2PF_SET_SIZE)
#define ADF_VF2PF_VFNR_TO_MASK(vf_nr) \
	({ \
	typeof(vf_nr) vf_nr_ = (vf_nr); \
	BIT((vf_nr_) - ADF_VF2PF_SET_SIZE * ADF_VF2PF_VFNR_TO_SET(vf_nr_)); \
	})
#endif

#define ADF_ADMINMSGUR_OFFSET (0x3A000 + 0x574)
#define ADF_ADMINMSGLR_OFFSET (0x3A000 + 0x578)
#define ADF_MAILBOX_BASE_OFFSET 0x20970
#define ADF_MAILBOX_STRIDE 0x1000
#define ADF_ADMINMSG_LEN 32
#define ADF_DEVICE_FUSECTL_OFFSET 0x40
#define ADF_DEVICE_LEGFUSE_OFFSET 0x4C
#define ADF_DEVICE_FUSECTL_MASK 0x80000000
#define ADF_PCI_MAX_BARS 3
#define ADF_DEVICE_NAME_LENGTH 32
#define ADF_ETR_MAX_RINGS_PER_BANK 16
#define ADF_MAX_MSIX_VECTOR_NAME 32
#define ADF_DEVICE_NAME_PREFIX "qat_"
#define ADF_CFG_NUM_SERVICES 4
#define ADF_CONST_TABLE_SIZE 1024
#define ADF_STOP_RETRY 100
#define ADF_RESTARTING_RETRY 300
#define ADF_VF_SHUTDOWN_RETRY 100
#define ADF_PF_WAIT_RESTARTING_COMPLETE_DELAY 100
#define ADF_SRV_TYPE_BIT_LEN 3
#define ADF_SRV_TYPE_MASK 0x7
#if defined(QAT_UIO) || defined(QAT_ESXI)
#define ADF_RINGS_PER_SRV_TYPE 2
#define ADF_THRD_ABILITY_BIT_LEN 4
#define ADF_THRD_ABILITY_MASK 0xf
#endif /* QAT_UIO || QAT_ESXI */
#define AUX_MSIX_SIGNAL_HANDLED_CONTINUE_HANDLE 0x1
#define AUX_MSIX_SIGNAL_HANDLED 0x3
#define ADF_SRV_CHAINING_ENABLED 1

#define ADF_DEFAULT_RING_TO_SRV_MAP \
	(CRYPTO | CRYPTO << ADF_CFG_SERV_RING_PAIR_1_SHIFT | \
	NA << ADF_CFG_SERV_RING_PAIR_2_SHIFT | \
	COMP << ADF_CFG_SERV_RING_PAIR_3_SHIFT)

enum adf_accel_capabilities {
	ADF_ACCEL_CAPABILITIES_NULL = 0,
	ADF_ACCEL_CAPABILITIES_CRYPTO_SYMMETRIC = 1,
	ADF_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC = 2,
	ADF_ACCEL_CAPABILITIES_CIPHER = 4,
	ADF_ACCEL_CAPABILITIES_AUTHENTICATION = 8,
	ADF_ACCEL_CAPABILITIES_COMPRESSION = 32,
	ADF_ACCEL_CAPABILITIES_DEPRECATED = 64,
	ADF_ACCEL_CAPABILITIES_RANDOM_NUMBER = 128,
	ADF_ACCEL_CAPABILITIES_AUX = 0x80000000
};

#ifndef USER_SPACE
struct adf_bar {
	resource_size_t base_addr;
	void __iomem *virt_addr;
	resource_size_t size;
} __packed;

struct adf_irq {
	bool enabled;
	char name[ADF_MAX_MSIX_VECTOR_NAME];
} __packed;

struct adf_accel_msix {
	struct msix_entry *entries;
	struct adf_irq *irqs;
	u32 num_entries;
} __packed;

struct adf_accel_pci {
	struct pci_dev *pci_dev;
	struct adf_accel_msix msix_entries;
	struct adf_bar pci_bars[ADF_PCI_MAX_BARS];
	u8 revid;
	u8 sku;
} __packed;
#endif /* USER_SPACE */

enum dev_state {
	DEV_DOWN = 0,
	DEV_UP
};

enum dev_sku_info {
	DEV_SKU_1 = 0,
	DEV_SKU_1_CY,
	DEV_SKU_2,
	DEV_SKU_2_CY,
	DEV_SKU_3,
	DEV_SKU_3_CY,
	DEV_SKU_4,
	DEV_SKU_VF,
	DEV_SKU_UNKNOWN,
};

enum rpreset_status {
	RPRESET_SUCCESS = 0,
	RPRESET_NOT_SUPPORTED,
	RPRESET_INVAL_BANK,
	RPRESET_TIMEOUT,
};

enum adf_ring_mode {
	ADF_WQ_MODE = 0,
	ADF_UQ_MODE,
};

enum ras_errors {
	ADF_RAS_CORR = 0,
	ADF_RAS_UNCORR,
	ADF_RAS_FATAL,
	ADF_RAS_ERRORS,
};

static inline const char *get_sku_info(enum dev_sku_info info)
{
	switch (info) {
	case DEV_SKU_1:
		return "SKU1";
	case DEV_SKU_1_CY:
		return "SKU1CY";
	case DEV_SKU_2:
		return "SKU2";
	case DEV_SKU_2_CY:
		return "SKU2CY";
	case DEV_SKU_3:
		return "SKU3";
	case DEV_SKU_3_CY:
		return "SKU3CY";
	case DEV_SKU_4:
		return "SKU4";
	case DEV_SKU_VF:
		return "SKUVF";
	case DEV_SKU_UNKNOWN:
	default:
		break;
	}
	return "Unknown SKU";
}

#ifndef USER_SPACE
/* ADF_ACCEL_CRYPTO includes SYM & ASYM in CPM1X; */
/* while in CPM20, ADF_ACCEL_CRYPTO only includes SYM */
enum adf_accel_unit_services {
	ADF_ACCEL_SERVICE_NULL = 0,
	ADF_ACCEL_INLINE_CRYPTO =  1,
	ADF_ACCEL_CRYPTO =  2,
	ADF_ACCEL_COMPRESSION = 4,
	ADF_ACCEL_ASYM =  8,
	ADF_ACCEL_ADMIN = 16,
	ADF_ACCEL_AUX = 32,
};

struct adf_ae_info {
	u32 num_asym_thd;
	u32 num_sym_thd;
	u32 num_dc_thd;
} __packed;

struct adf_accel_unit {
	u8 au_mask;
	u32 accel_mask;
	u32 ae_mask;
	u32 comp_ae_mask;
	u32 num_ae;
	enum adf_accel_unit_services services;
} __packed;

enum adf_bundle_type {
	FREE = 0,
	KERNEL,
	USER,
	ADI
};

/* num_cy_au includes SYM & ASYM in CPM1X; */
/* while in CPM20, num_cy_au only includes SYM */
struct adf_accel_unit_info {
	u32 inline_ingress_msk;
	u32 inline_egress_msk;
	u32 sym_ae_msk;
	u32 asym_ae_msk;
	u32 dc_ae_msk;
	u32 aux_ae_mask;
	u8 num_cy_au;
	u8 num_dc_au;
	u8 num_asym_au;
	u8 num_aux_au;
	u8 num_inline_au;
	struct adf_accel_unit *au;
	const struct adf_ae_info *ae_info;
} __packed;

struct adf_hw_aram_info {
	/* Inline Egress mask. "1" = AE is working with egress traffic */
	u32 inline_direction_egress_mask;
	/* Inline congestion managmenet profiles set in config file */
	u32 inline_congest_mngt_profile;
	/* Initialise CY AE mask, "1" = AE is used for CY operations */
	u32 cy_ae_mask;
	/* Initialise DC AE mask, "1" = AE is used for DC operations */
	u32 dc_ae_mask;
	/* Number of long words used to define the ARAM regions */
	u32 num_aram_lw_entries;
	/* ARAM region definitions */
	u32 mmp_region_size;
	u32 mmp_region_offset;
	u32 skm_region_size;
	u32 skm_region_offset;
	/*
	 * Defines size and offset of compression intermediate buffers stored
	 * in ARAM (device's on-chip memory).
	 */
	u32 inter_buff_aram_region_size;
	u32 inter_buff_aram_region_offset;
	u32 sadb_region_size;
	u32 sadb_region_offset;
} __packed;

struct adf_hw_device_class {
	const char *name;
	const enum adf_device_type type;
	u32 instances;
} __packed;

struct arb_info {
	u32 arbiter_offset;
	u32 wrk_thd_2_srv_arb_map;
	u32 wrk_cfg_offset;
} __packed;

struct admin_info {
	u32 admin_msg_ur;
	u32 admin_msg_lr;
	u32 mailbox_offset;
} __packed;


struct adf_cfg_device_data;
struct adf_accel_dev;
struct adf_etr_data;
struct adf_etr_ring_data;

struct adf_hw_csr_ops {
	u32 (*build_ring_config)(u32 size);
	u64 (*build_resp_ring_config)(u32 size,
				      u32 watermark_nf,
				      u32 watermark_ne);
	u64 (*build_ring_base_addr)(dma_addr_t addr,
				    u32 size);
	u32 (*read_csr_ring_head)(void __iomem *csr_base_addr,
				  u32 bank,
				  u32 ring);
	void (*write_csr_ring_head)(void __iomem *csr_base_addr,
				    u32 bank,
				    u32 ring,
				    u32 value);
	u32 (*read_csr_ring_tail)(void __iomem *csr_base_addr,
				  u32 bank,
				  u32 ring);
	void (*write_csr_ring_tail)(void __iomem *csr_base_addr,
				    u32 bank,
				    u32 ring,
				    u32 value);
	u32 (*read_csr_stat)(void __iomem *csr_base_addr,
			     u32 bank);
	u32 (*read_csr_uo_stat)(void __iomem *csr_base_addr,
				u32 bank);
	u32 (*read_csr_e_stat)(void __iomem *csr_base_addr,
			       u32 bank);
	u32 (*read_csr_ne_stat)(void __iomem *csr_base_addr,
				u32 bank);
	u32 (*read_csr_nf_stat)(void __iomem *csr_base_addr,
				u32 bank);
	u32 (*read_csr_f_stat)(void __iomem *csr_base_addr,
			       u32 bank);
	u32 (*read_csr_c_stat)(void __iomem *csr_base_addr,
			       u32 bank);
	u32 (*read_csr_exp_stat)(void __iomem *csr_base_addr,
				 u32 bank);
	u32 (*read_csr_exp_int_en)(void __iomem *csr_base_addr,
				   u32 bank);
	void (*write_csr_exp_int_en)(void __iomem *csr_base_addr,
				     u32 bank,
				     u32 value);
	u32 (*read_csr_ring_config)(void __iomem *csr_base_addr,
				    u32 bank,
				    u32 ring);
	void (*write_csr_ring_config)(void __iomem *csr_base_addr,
				      u32 bank,
				      u32 ring,
				      u32 value);
	dma_addr_t (*read_csr_ring_base)(void __iomem *csr_base_addr,
					 u32 bank,
					 u32 ring);
	void (*write_csr_ring_base)(void __iomem *csr_base_addr,
				    u32 bank,
				    u32 ring,
				    dma_addr_t addr);
	u32 (*read_csr_int_en)(void __iomem *csr_base_addr,
			       u32 bank);
	void (*write_csr_int_en)(void __iomem *csr_base_addr,
				 u32 bank,
				 u32 value);
	u32 (*read_csr_int_flag)(void __iomem *csr_base_addr,
				 u32 bank);
	void (*write_csr_int_flag)(void __iomem *csr_base_addr,
				   u32 bank,
				   u32 value);
	u32 (*read_csr_int_srcsel)(void __iomem *csr_base_addr,
				   u32 bank,
				   u32 idx);
	void (*write_csr_int_srcsel)(void __iomem *csr_base_addr,
				     u32 bank,
				     u32 idx,
				     u32 value);
	u32 (*read_csr_int_col_en)(void __iomem *csr_base_addr,
				   u32 bank);
	void (*write_csr_int_col_en)(void __iomem *csr_base_addr,
				     u32 bank,
				     u32 value);
	u32 (*read_csr_int_col_ctl)(void __iomem *csr_base_addr,
				    u32 bank);
	void (*write_csr_int_col_ctl)(void __iomem *csr_base_addr,
				      u32 bank,
				      u32 value);
	u32 (*read_csr_int_flag_and_col)(void __iomem *csr_base_addr,
					 u32 bank);
	void (*write_csr_int_flag_and_col)(void __iomem *csr_base_addr,
					   u32 bank,
					   u32 value);
	u32 (*read_csr_ring_srv_arb_en)(void __iomem *csr_base_addr, u32 bank);
	void (*write_csr_ring_srv_arb_en)(void __iomem *csr_base_addr,
					  u32 bank, u32 value);
	u32 (*get_src_sel_mask)(void);
	u32 (*get_int_col_ctl_enable_mask)(void);
	u32 (*get_bank_irq_mask)(u32 irq_mask);
	void (*bank_pasid_enable)(void __iomem *csr_base_addr,
				  u32 bank_number,
				  bool at, bool adi, bool priv,
				  int pasid);
	void (*bank_pasid_disable)(void __iomem *csr_base_addr,
				   u32 bank_number,
				   bool at, bool adi, bool priv);
	int (*set_uq_mode)(void __iomem *csr_base_addr,
			   u32 bank_number,
			   u8 mode);
	void (*enable_misc_interrupts)(void __iomem *csr_base_addr);
	void (*disable_misc_interrupts)(void __iomem *csr_base_addr);
	void (*enable_bundle_interrupts)(void __iomem *csr_base_addr,
					 struct adf_accel_dev *accel_dev);
	void (*disable_bundle_interrupts)(void __iomem *csr_base_addr);
	void (*clear_pf2vf_msg_register)(void __iomem *csr_base_addr,
					 u32 index);
	u32 (*read_vf2pf_isr_sou)(void __iomem *csr_base_addr);
	u32 (*read_vf2pf_isr_mask)(void __iomem *csr_base_addr);
	void (*write_vf2pf_isr_mask)(void __iomem *csr_base_addr, u32 val);
	void (*enable_slice_hang_interrupt)(void __iomem *csr_base_addr,
					    u16 accel_mask);
	void (*mask_rp_interrupts)(void __iomem *csr_base_addr);
	void (*mask_pfvf_interrupts)(void __iomem *csr_base_addr);
	void (*disable_pm_idle_interrupt)(void __iomem *csr_base_addr);
	void (*clear_pm_sts)(void __iomem *csr_base_addr);
	void (*deactive_pm_drive)(void __iomem *csr_base_addr);
	void (*active_pm_drive)(void __iomem *csr_base_addr);
} __packed;

struct adf_hw_csr_info {
	struct adf_hw_csr_ops csr_ops;
	u32 csr_addr_offset;
	u32 ring_bundle_size;
	u32 uq_size;
	u32 bank_int_flag_clear_mask;
	u32 num_rings_per_int_srcsel;
	u32 arb_enable_mask;
} __packed;


struct adf_aux_ops {
	int (*add_aux_dev)(struct adf_accel_dev *accel_dev);
	int (*del_aux_dev)(struct adf_accel_dev *accel_dev);
	int (*enable_aux_dev)(struct adf_accel_dev *accel_dev);
	int (*aux_set_msix_isr_ae)(struct auxiliary_device *aux_dev);
	int (*get_aux_fw_name)(struct adf_accel_dev *accel_dev);
};

struct adf_dev_err_mask {
	u32 cppagentcmdpar_mask;
	u32 parerr_ath_cph_mask;
	u32 parerr_cpr_xlt_mask;
	u32 parerr_dcpr_ucs_mask;
	u32 parerr_pke_mask;
	u32 parerr_wat_wcp_mask;
	u32 ssmfeatren_mask;
};

struct adf_hw_device_data {
	struct adf_hw_device_class *dev_class;
	struct adf_dev_err_mask dev_err_mask;
	uint32_t (*get_accel_mask)(struct adf_accel_dev *accel_dev);
	uint32_t (*get_ae_mask)(struct adf_accel_dev *accel_dev);
	uint32_t (*get_sram_bar_id)(struct adf_hw_device_data *self);
	uint32_t (*get_misc_bar_id)(struct adf_hw_device_data *self);
	uint32_t (*get_etr_bar_id)(struct adf_hw_device_data *self);
	uint32_t (*get_uq_bar_id)(struct adf_hw_device_data *self);
	uint32_t (*get_num_aes)(struct adf_hw_device_data *self);
	uint32_t (*get_num_accels)(struct adf_hw_device_data *self);
	void (*notify_and_wait_ethernet)(struct adf_accel_dev *accel_dev);
	bool (*get_eth_doorbell_msg)(struct adf_accel_dev *accel_dev);
	uint32_t (*get_pf2vf_offset)(uint32_t i);
	uint32_t (*get_vf2pf_offset)(uint32_t i);
	void (*get_arb_info)(struct arb_info *arb_csrs_info);
	void (*get_admin_info)(struct admin_info *admin_csrs_info);
	void (*get_errsou_offset)(u32 *errsou3, u32 *errsou5);
	uint32_t (*get_num_accel_units)(struct adf_hw_device_data *self);
	int (*init_accel_units)(struct adf_accel_dev *accel_dev);
	void (*exit_accel_units)(struct adf_accel_dev *accel_dev);
	uint32_t (*get_clock_speed)(struct adf_hw_device_data *self);
	enum dev_sku_info (*get_sku)(struct adf_hw_device_data *self);
	bool (*check_prod_sku)(struct adf_accel_dev *accel_dev);
	bool (*check_base_sku)(struct adf_accel_dev *accel_dev);
	u32 heartbeat_ctr_num;
#if defined(CONFIG_PCI_IOV)
	void (*mask_misc_irq)(struct adf_accel_dev *accel_dev,
			      const bool mask_irq);
	void (*process_and_get_vf2pf_int)(void __iomem *pmisc_bar_addr,
					  u32 vf_int_mask[ADF_MAX_VF2PF_SET]);
	void (*enable_vf2pf_interrupts)(void __iomem *pmisc_bar_addr,
					u32 vf_mask_sets, u8 vf2pf_set);
	void (*disable_vf2pf_interrupts)(void __iomem *pmisc_bar_addr,
					 u32 vf_mask_sets, u8 vf2pf_set);
	int (*check_arbitrary_numvfs)(struct adf_accel_dev *accel_dev,
				      const int numvfs);
#endif
	int (*alloc_irq)(struct adf_accel_dev *accel_dev);
	void (*free_irq)(struct adf_accel_dev *accel_dev);
	void (*enable_error_correction)(struct adf_accel_dev *accel_dev);
	int (*check_uncorrectable_error)(struct adf_accel_dev *accel_dev);
	void (*print_err_registers)(struct adf_accel_dev *accel_dev);
	void (*disable_error_interrupts)(struct adf_accel_dev *accel_dev);
	int (*init_ras)(struct adf_accel_dev *accel_dev);
	void (*exit_ras)(struct adf_accel_dev *accel_dev);
	void (*disable_arb)(struct adf_accel_dev *accel_dev);
	void (*update_ras_errors)(struct adf_accel_dev *accel_dev, int error);
	bool (*ras_interrupts)(struct adf_accel_dev *accel_dev,
			       bool *reset_required);
	int (*init_admin_comms)(struct adf_accel_dev *accel_dev);
	void (*exit_admin_comms)(struct adf_accel_dev *accel_dev);
	int (*init_chaining)(struct adf_accel_dev *accel_dev);
	int (*send_admin_init)(struct adf_accel_dev *accel_dev);
	int (*get_heartbeat_status)(struct adf_accel_dev *accel_dev);
	uint32_t (*get_ae_clock)(struct adf_hw_device_data *self);
	uint32_t (*get_hb_clock)(struct adf_hw_device_data *self);
	int (*init_pm)(struct adf_accel_dev *accel_dev);
	void (*switch_drv_active)(struct adf_accel_dev *accel_dev);
	void (*exit_pm)(struct adf_accel_dev *accel_dev);
	int (*update_qat_pm_state)(struct adf_accel_dev *accel_dev);
	bool (*check_pm_interrupts)(struct adf_accel_dev *accel_dev);
	int (*int_timer_init)(struct adf_accel_dev *accel_dev);
	void (*int_timer_exit)(struct adf_accel_dev *accel_dev);
#ifdef QAT_HB_FAIL_SIM
	int (*adf_disable_ae_wrk_thds)(struct adf_accel_dev *accel_dev,
				       u32 ae, u32 thr);
	int (*adf_set_max_hb_timer)(struct adf_accel_dev *accel_dev);
#endif
#ifdef QAT_UIO
#ifdef QAT_KPT
	int (*enable_kpt)(struct adf_accel_dev *accel_dev);
#endif
	void (*set_asym_rings_mask)(struct adf_accel_dev *accel_dev);
#endif
	u32 (*get_slices_for_svc)(struct adf_accel_dev *accel_dev,
				  enum adf_svc_type svc);
#ifdef NON_GPL_COMMON
	void (*get_accel_algo_cap)(struct adf_accel_dev *accel_dev);
#endif
	uint32_t (*get_accel_cap)(struct adf_accel_dev *accel_dev);
	int (*init_arb)(struct adf_accel_dev *accel_dev);
	void (*exit_arb)(struct adf_accel_dev *accel_dev);
	void (*get_arb_mapping)(struct adf_accel_dev *accel_dev,
				const uint32_t **cfg);
	void (*disable_iov)(struct adf_accel_dev *accel_dev);
	void (*configure_iov_threads)(struct adf_accel_dev *accel_dev,
				      bool enable);
	void (*enable_ints)(struct adf_accel_dev *accel_dev);
	bool (*check_slice_hang)(struct adf_accel_dev *accel_dev);
	int (*set_ssm_wdtimer)(struct adf_accel_dev *accel_dev);
	void (*set_msix_rttable)(struct adf_accel_dev *accel_dev);
	int (*enable_vf2pf_comms)(struct adf_accel_dev *accel_dev);
	int (*disable_vf2pf_comms)(struct adf_accel_dev *accel_dev);
	void (*enable_pf2vf_interrupt)(struct adf_accel_dev *accel_dev);
	void (*disable_pf2vf_interrupt)(struct adf_accel_dev *accel_dev);
	int (*interrupt_active_pf2vf)(struct adf_accel_dev *accel_dev);
	int (*get_int_active_bundles)(struct adf_accel_dev *accel_dev);
	void (*reset_device)(struct adf_accel_dev *accel_dev);
	void (*reset_hw_units)(struct adf_accel_dev *accel_dev);
	int (*measure_clock)(struct adf_accel_dev *accel_dev);
	void (*restore_device)(struct adf_accel_dev *accel_dev);
	uint32_t (*get_obj_cfg_ae_mask)(struct adf_accel_dev *accel_dev,
					enum adf_accel_unit_services services);
	enum adf_accel_unit_services (*get_service_type)
				(struct adf_accel_dev *accel_dev, s32 obj_num);
	int (*init_res_part)(struct adf_accel_dev *accel_dev);
	void (*exit_res_part)(struct adf_accel_dev *accel_dev);
	int (*add_pke_stats)(struct adf_accel_dev *accel_dev);
	void (*remove_pke_stats)(struct adf_accel_dev *accel_dev);
	int (*add_misc_error)(struct adf_accel_dev *accel_dev);
	int (*count_ras_event)(struct adf_accel_dev *accel_dev,
			       u32 *ras_event, char *aeidstr);
	void (*remove_misc_error)(struct adf_accel_dev *accel_dev);
	void (*init_hw_csr_info)(struct adf_hw_csr_info *csr_info);
	int (*configure_accel_units)(struct adf_accel_dev *accel_dev);
	int (*ring_pair_reset)(struct adf_accel_dev *accel_dev,
			       u32 bank_number);
	int (*ring_pair_drain)(struct adf_accel_dev *accel_dev,
			       u32 bank_number, int timeout_ms);
	void (*config_ring_irq)(struct adf_accel_dev *accel_dev,
				u32 bank_number, u16 ring_mask);
	int (*get_ring_to_svc_map)(struct adf_accel_dev *accel_dev,
				   u16 *ring_to_svc_map);
	int (*check_supported_services)(struct adf_accel_dev *accel_dev);
	int (*init_kpt)(struct adf_accel_dev *accel_dev);
	int (*config_kpt)(struct adf_accel_dev *accel_dev);
	int (*init_rl_v2)(struct adf_accel_dev *accel_dev);
	void (*exit_rl_v2)(struct adf_accel_dev *accel_dev);
	void (*get_vf_rl_info)(struct adf_accel_dev *accel_dev);
	void (*pf_sla_val_provider)(struct adf_accel_dev *accel_dev,
				    u32 ring_num, u32 *value, u32 msg_type);
	int (*telemetry_init)(struct adf_accel_dev *accel_dev);
	int (*telemetry_exit)(struct adf_accel_dev *accel_dev);
	void (*telemetry_calc_data)(struct adf_accel_dev *accel_dev);
	uint32_t (*get_objs_num)(struct adf_accel_dev *accel_dev);
	const char* (*get_obj_name)(struct adf_accel_dev *accel_dev,
				    enum adf_accel_unit_services services);
	void (*pre_reset)(struct adf_accel_dev *accel_dev);
	void (*post_reset)(struct adf_accel_dev *accel_dev);
	struct adf_hw_csr_info csr_info;
	int (*check_ae_exist)(unsigned char ae, unsigned int relmask,
			      unsigned char cppmask);
	void (*get_ring_svc_map_data)(int ring_pair_index, u16 ring_to_svc_map,
				      u8 *serv_type, int *ring_index,
				      int *num_rings_per_srv, int bundle_num);
	int (*init_adis)(struct adf_accel_dev *accel_dev);
	void (*exit_adis)(struct adf_accel_dev *accel_dev);
	int (*config_bank_pasid)(struct adf_accel_dev *accel_dev,
				 u32 bank_number,
				 bool enable,
				 bool at, bool adi, bool priv,
				 int pasid);
#if defined(QAT_UIO) || defined(QAT_ESXI)
	int (*get_fw_image_type)(struct adf_accel_dev *accel_dev,
				 enum adf_cfg_fw_image_type *fw_image_type);
	int (*update_accel_cap_mask)(struct adf_accel_dev *accel_dev);
#endif /* QAT_UIO || QAT_ESXI */
	int (*get_uq_base_addr)(struct adf_accel_dev *accel_dev,
				void **uq_base_addr,
				u32 bank_number);
	u32 (*get_num_svc_aes)(struct adf_accel_dev *accel_dev,
			       enum adf_svc_type svc);
	const char *fw_name;
	const char *fw_mmp_name;
	const char *fw_aux_obj;
	const char *fw_aux_admin_obj;
	bool reset_ack;
	u32 fuses;
	u32 accel_capabilities_mask;
	u32 instance_id;
	u16 accel_mask;
	u32 aerucm_mask;
	u32 ae_mask;
	u32 admin_ae_mask;
	u32 service_mask;
	u32 service_to_load_mask;
	u16 tx_rings_mask;
	u8 tx_rx_gap;
	u16 num_banks;
	u8 num_rings_per_bank;
	u16 num_banks_per_vf;
	u8 num_accel;
	u8 num_logical_accel;
	u8 num_engines;
	u8 min_iov_compat_ver;
	u16 ring_to_svc_map;
	u32 extended_dc_capabilities;
	u32 asym_ae_active_thd_mask;
	/* flag to check if mmp fw will be loaded  by default */
	bool load_mmp_always;
	bool get_ring_to_svc_done;
	/* flag to check if aux service enable */
	bool qat_aux_enable;
	/* flag to check if sriov_numvfs isn't zero */
	bool is_sriov_numvfs_set;
	/* flag to check FW integrity self-test */
	bool fw_integr_selftest;
	int (*get_storage_enabled)(struct adf_accel_dev *accel_dev,
				   uint32_t *storage_enabled);
	u8 query_storage_cap;
	u32 clock_frequency;
#ifdef QAT_UIO
	int (*config_device)(struct adf_accel_dev *accel_dev);
	u16 asym_rings_mask;
#endif
#ifdef NON_GPL_COMMON
	u32 cipher_capabilities_mask;
	u32 hash_capabilities_mask;
	u32 asym_capabilities_mask;
#endif
#ifdef QAT_UIO
#ifdef QAT_KPT
	u32 kpt_hw_capabilities;
	u32 kpt_achandle;
#endif
#endif
	int pfvf_type_shift;
	u32 pfvf_type_mask;
	int pfvf_data_shift;
	u32 pfvf_data_mask;
	u8 *kpt_issue_cert;
	u32 kpt_issue_cert_len;
	struct adf_adi_ops *adi_ops;
	int (*get_capabilities_ex)(struct adf_accel_dev *accel_dev);
	void *priv_data;
	struct adf_aux_ops *aux_ops;
	u32 default_coalesce_timer;
	u32 coalescing_min_time;
	u32 coalescing_max_time;
	u32 coalescing_def_time;
	struct adf_rl_hw_data rl_data;
} __packed;

/* helper enum for performing CSR operations */
enum operation {
	AND,
	OR,
};

/* 32-bit CSR write macro */
#define ADF_CSR_WR(csr_base, csr_offset, val) \
	__raw_writel(val, (((u8 *)(csr_base)) + (csr_offset)))
/* 64-bit CSR write macro */
#define ADF_CSR_WR64(csr_base, csr_offset, val) \
	__raw_writeq(val, (((u8 *)(csr_base)) + (csr_offset)))

/* 32-bit CSR read macro */
#define ADF_CSR_RD(csr_base, csr_offset) \
	__raw_readl(((u8 *)(csr_base)) + (csr_offset))

/* 64-bit CSR read macro */
#define ADF_CSR_RD64(csr_base, csr_offset) \
	__raw_readq(((u8 *)(csr_base)) + (csr_offset))

#define GET_DEV(accel_dev) ((accel_dev)->accel_pci_dev.pci_dev->dev)
#define GET_BARS(accel_dev) ((accel_dev)->accel_pci_dev.pci_bars)
#define GET_HW_DATA(accel_dev) ((accel_dev)->hw_device)
#define GET_MAX_BANKS(accel_dev) (GET_HW_DATA(accel_dev)->num_banks)
#define GET_NUM_RINGS_PER_BANK(accel_dev) \
	(GET_HW_DATA(accel_dev)->num_rings_per_bank)
#define GET_MAX_ACCELENGINES(accel_dev) (GET_HW_DATA(accel_dev)->num_engines)
#define accel_to_pci_dev(accel_ptr) ((accel_ptr)->accel_pci_dev.pci_dev)
#define GET_SRV_TYPE(ena_srv_mask, srv) \
	(((ena_srv_mask) >> (ADF_SRV_TYPE_BIT_LEN * (srv))) & ADF_SRV_TYPE_MASK)
#define ADF_NUM_THREADS_PER_AE (8)
#define ADF_AE_ADMIN_THREAD (7)
#define ADF_NUM_PKE_STRAND (2)
#define ADF_AE_STRAND0_THREAD (8)
#define ADF_AE_STRAND1_THREAD (9)
#ifdef QAT_UIO
#define GET_MAX_PROCESSES(accel_dev) \
	({ \
	typeof(accel_dev) dev = (accel_dev); \
	(GET_MAX_BANKS(dev) * (GET_NUM_RINGS_PER_BANK(dev) / 2)); \
	})
#define SET_ASYM_MASK(asym_mask, srv) \
	({ \
	typeof(srv) srv_ = (srv); \
	(asym_mask) |= \
	((1 << (srv_) * ADF_RINGS_PER_SRV_TYPE) | \
	 (1 << ((srv_) * ADF_RINGS_PER_SRV_TYPE + 1))); \
	})
#endif

static inline void adf_csr_fetch_and_and(void __iomem *csr,
					 size_t offs, unsigned long mask)
{
	unsigned int val = ADF_CSR_RD(csr, offs);

	val &= mask;
	ADF_CSR_WR(csr, offs, val);
}

static inline void adf_csr_fetch_and_or(void __iomem *csr,
					size_t offs, unsigned long mask)
{
	unsigned int val = ADF_CSR_RD(csr, offs);

	val |= mask;
	ADF_CSR_WR(csr, offs, val);
}

static inline void
adf_csr_fetch_and_update(enum operation op, void __iomem *csr,
			 size_t offs, unsigned long mask)
{
	switch (op) {
	case AND:
		adf_csr_fetch_and_and(csr, offs, mask);
		break;
	case OR:
		adf_csr_fetch_and_or(csr, offs, mask);
		break;
	}
}

struct pfvf_stats {
	struct dentry *stats_file;
	/* Messages put in CSR */
	unsigned int tx;
	/* Messages read from CSR */
	unsigned int rx;
	/* Interrupt fired but int bit was clear */
	unsigned int spurious;
	/* Block messages sent */
	unsigned int blk_tx;
	/* Block messages received */
	unsigned int blk_rx;
	/* Blocks received with CRC errors */
	unsigned int crc_err;
	/* CSR in use by other side */
	unsigned int busy;
	/* Receiver did not acknowledge */
	unsigned int no_ack;
	/* Collision detected */
	unsigned int collision;
	/* Couldn't send a response */
	unsigned int tx_timeout;
	/* Didn't receive a response */
	unsigned int rx_timeout;
	/* Responses received */
	unsigned int rx_rsp;
	/* Messages re-transmitted */
	unsigned int retry;
	/* Event put timeout */
	unsigned int event_timeout;
};

#define NUM_PFVF_COUNTERS 14

struct adf_admin_comms {
	dma_addr_t phy_addr;
	dma_addr_t const_tbl_addr;
	dma_addr_t aram_map_phys_addr;
	dma_addr_t phy_hb_addr;
	void *virt_addr;
	void *virt_hb_addr;
	void *dma_tbl_addr;
	void __iomem *mailbox_addr;
	struct mutex lock;	/* protects adf_admin_comms struct */
};

struct icp_qat_fw_loader_handle;
struct adf_fw_loader_data {
	struct icp_qat_fw_loader_handle *fw_loader;
	const struct firmware *uof_fw;
	const struct firmware *mmp_fw;
};

struct adf_accel_vf_info {
	struct adf_accel_dev *accel_dev;
	struct mutex pf2vf_lock; /* protect CSR access for PF2VF messages */
	struct ratelimit_state vf2pf_ratelimit;
	u32 vf_nr;
	bool init;
	bool restarting;
	u8 compat_ver;
	struct pfvf_stats pfvf_counters;
};

/* Struct to store rate limiting info for each VF */
struct adf_vf_rp_rl_info {
	u32 cir;
	u32 pir;
};

struct adf_fw_versions {
	u8 fw_version_major;
	u8 fw_version_minor;
	u8 fw_version_patch;
	u8 mmp_version_major;
	u8 mmp_version_minor;
	u8 mmp_version_patch;
};

#define ADF_COMPAT_CHECKER_MAX 8
typedef int (*adf_iov_compat_checker_t)(struct adf_accel_dev *accel_dev,
					u8 vf_compat_ver);
struct adf_accel_compat_manager {
	u8 num_chker;
	adf_iov_compat_checker_t iov_compat_checkers[ADF_COMPAT_CHECKER_MAX];
};

struct adf_pm {
	struct workqueue_struct *pm_irq_wq;
	struct dentry *debugfs_pm_status;
	u32 idle_irq_counters;
	u32 host_ack_counter;
	u32 host_nack_counter;
	u32 throttle_irq_counters;
	u32 fw_irq_counters;
	int idle_support;
};

struct adf_int_timer {
	struct adf_accel_dev *accel_dev;
	struct workqueue_struct *timer_irq_wq;
	struct timer_list timer;
	u32 timeout_val;
	u32 int_cnt;
	atomic_t timer_bh_state;
};


#ifdef QAT_UIO
struct adf_heartbeat;
struct adf_ver;
struct adf_uio_control_accel;
struct qat_uio_pci_dev;
#endif
struct adf_adi_info;
struct adf_rl_v2;
struct adf_rl_v3;
struct adf_accel_dev {
	struct adf_hw_aram_info *aram_info;
	struct adf_accel_unit_info *au_info;
	struct adf_etr_data *transport;
	struct adf_hw_device_data *hw_device;
	struct adf_cfg_device_data *cfg;
	struct adf_fw_loader_data *fw_loader;
	struct adf_admin_comms *admin;
	struct work_struct error_event_wq;
#ifdef QAT_UIO
	struct adf_uio_control_accel *accel;
	struct qat_uio_pci_dev *uiodev;
	unsigned int num_ker_bundles;
#endif
	struct adf_heartbeat *heartbeat;
	struct adf_telemetry *telemetry;
	struct adf_ver *pver;
	struct adf_pm *power_management;
	struct adf_int_timer *int_timer;
	unsigned int autoreset_on_error;
	struct adf_rl_v2 *rl_v2;
#ifdef QAT_UIO
	struct adf_fw_counters_data *fw_counters_data;
	struct dentry *debugfs_inline_dir;
#endif
	struct dentry *debugfs_ae_config;
	struct list_head crypto_list;
	atomic_t *ras_counters;
	unsigned long status;
	atomic_t ref_count;
	struct dentry *debugfs_dir;
	struct dentry *cnvnr_dbgfile;
	struct dentry *pfvf_dbgdir;
	struct dentry *clock_dbgfile;
	struct dentry *pke_replay_dbgfile;
	struct dentry *misc_error_dbgfile;
	struct list_head list;
	struct module *owner;
	struct adf_accel_pci accel_pci_dev;
	struct adf_accel_compat_manager *cm;
	u8 compat_ver;
	struct adf_fw_versions fw_versions;
	union {
		struct {
			/* vf_info is non-zero when SR-IOV is init'ed */
			struct adf_accel_vf_info *vf_info;
			struct workqueue_struct *resp_wq;
		} pf;
		struct {
			bool irq_enabled;
			bool is_err_notified;
			char *irq_name;
			struct workqueue_struct *pf2vf_wq;
			struct workqueue_struct *resp_wq;
			struct work_struct pf2vf_bh_wq;
			struct mutex vf2pf_lock; /* protect CSR access */
			struct completion iov_msg_completion;
			u8 compatible;
			u8 pf_version;
			struct completion err_notified;
			u8 pf2vf_block_byte;
			u8 pf2vf_block_resp_type;
			enum rpreset_status rpreset_sts;
			struct mutex rpreset_lock; /* protect rpreset_sts */
			struct pfvf_stats pfvf_counters;
			struct adf_iov_transport *iov_transport;
			struct adf_vf_rp_rl_info *vf_rl;
		} vf;
	};
	bool is_vf;
	bool is_drv_rm;
	u32 accel_id;
	spinlock_t vf2pf_csr_lock; /* protects VF2PF CSR access */
#ifdef QAT_UIO
#ifdef QAT_KPT
	u32 detect_kpt;
#endif
#endif
#ifdef CONFIG_CRYPTO_DEV_QAT_VDCM
	struct adf_vdcm_ctx_blk *vdcm;
#endif
	struct adf_adi_info *adi_info;
	bool svm_enabled;
	bool at_enabled;
	struct auxiliary_device *aux_dev;
	struct mutex lock; /* protect accel_dev during start/stop e.t.c */
	bool chaining_enabled;
	enum adf_ring_mode ring_mode;
	struct adf_rl_v3 *rl_v3;
	/* Protects RL across allocation/deallocation/set/delete/query */
	struct mutex rl_lock;
};
#endif
#endif
