/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2018 - 2021 Intel Corporation */
#ifndef ADF_4XXX_HW_DATA_H_
#define ADF_4XXX_HW_DATA_H_

#include <adf_accel_devices.h>

#define DEFAULT_4XXX_ASYM_AE_MASK 0x03
#define DEFAULT_401XX_ASYM_AE_MASK 0x3F

/* PCIe configuration space */
#define ADF_4XXX_RX_RINGS_OFFSET 1
#define ADF_4XXX_TX_RINGS_MASK 0x1

#define ADF_4XXX_MAX_ACCELERATORS 1
#define ADF_4XXX_MAX_ACCELENGINES 9

/* 2 Accel units dedicated to services and */
/* 1 Accel unit dedicated to Admin AE */
#define ADF_4XXX_MAX_ACCELUNITS   3

#define ADF_4XXX_ACCELERATORS_MASK (0x1)
#define ADF_4XXX_ACCELENGINES_MASK (0x1FF)
#define ADF_4XXX_ADMIN_AE_MASK (0x100)

#define ADF_4XXX_HICPPAGENTCMDPARERRLOG_MASK (0x1F)
#define ADF_4XXX_PARITYERRORMASK_ATH_CPH_MASK (0xF000F)
#define ADF_4XXX_PARITYERRORMASK_CPR_XLT_MASK (0x10001)
#define ADF_4XXX_PARITYERRORMASK_DCPR_UCS_MASK (0x30007)
#define ADF_4XXX_PARITYERRORMASK_PKE_MASK (0x3F)

/*
 * SSMFEATREN bit mask
 * BIT(4) - enables parity detection on CPP
 * BIT(12) - enables the logging of push/pull data errors
 *	     in pperr register
 * BIT(16) - BIT(23) - enable parity detection on SPPs
 */
#define ADF_4XXX_SSMFEATREN_MASK \
	(BIT(4) | BIT(12) | BIT(16) | BIT(17) | BIT(18) | \
	 BIT(19) | BIT(20) | BIT(21) | BIT(22) | BIT(23))

#define ADF_4XXX_ETR_MAX_BANKS 64
/*MSIX interrupt*/
#define ADF_4XXX_SMIAPF_RP_X0_MASK_OFFSET (0x41A040)
#define ADF_4XXX_SMIAPF_RP_X1_MASK_OFFSET (0x41A044)
#define ADF_4XXX_SMIAPF_MASK_OFFSET (0x41A084)

/* Bank and ring configuration */
#define ADF_4XXX_NUM_RINGS_PER_BANK 2
#define ADF_4XXX_NUM_SERVICES_PER_BANK 1
#define ADF_4XXX_NUM_BANKS_PER_VF 4
/* Error detection and correction */
#define ADF_4XXX_AE_CTX_ENABLES(i) (0x600818 + ((i) * 0x1000))
#define ADF_4XXX_AE_MISC_CONTROL(i) (0x600960 + ((i) * 0x1000))
#define ADF_4XXX_ENABLE_AE_ECC_ERR BIT(28)
#define ADF_4XXX_ENABLE_AE_ECC_PARITY_CORR (BIT(24) | BIT(12))
#define ADF_4XXX_ERRSSMSH_EN BIT(3)
#define ADF_4XXX_PF2VM_OFFSET(i)	(0x40B010 + ((i) * 0x20))
#define ADF_4XXX_VM2PF_OFFSET(i)	(0x40B014 + ((i) * 0x20))
#define ADF_4XXX_VINTMSK_OFFSET(i)	(0x40B004 + ((i) * 0x20))

/* Slice power down register */
#define ADF_4XXX_SLICEPWRDOWN(i) (0x2C((i) * 0x800))

/* Return interrupt accelerator source mask */
#define ADF_4XXX_IRQ_SRC_MASK(accel) (1 << (accel))

/* VF2PF interrupt source register */
#define ADF_4XXX_VM2PF_SOU (0x41A180)
/* VF2PF interrupt mask register */
#define ADF_4XXX_VM2PF_MSK (0x41A1C0)

#define ADF_4XXX_FCU_STATUS (0x641004)

#define ADF_4XXX_SLPAGEFWDERR (0x19C)

/* Buffer manager error status */
#define ADF_4XXX_BFMGRERROR (0x3E0)

/* Firmware error conditions in Resource Manager and Buffer Manager */
#define ADF_4XXX_FW_ERR_STATUS (0x440)

#define ADF_4XXX_IASTATSSM_UERRSSMSH_MASK  BIT(0)
#define ADF_4XXX_IASTATSSM_CERRSSMSH_MASK  BIT(1)
#define ADF_4XXX_IASTATSSM_UERRSSMMMP0_MASK  BIT(2)
#define ADF_4XXX_IASTATSSM_CERRSSMMMP0_MASK  BIT(3)
#define ADF_4XXX_IASTATSSM_UERRSSMMMP1_MASK  BIT(4)
#define ADF_4XXX_IASTATSSM_CERRSSMMMP1_MASK  BIT(5)
#define ADF_4XXX_IASTATSSM_UERRSSMMMP2_MASK  BIT(6)
#define ADF_4XXX_IASTATSSM_CERRSSMMMP2_MASK  BIT(7)
#define ADF_4XXX_IASTATSSM_UERRSSMMMP3_MASK  BIT(8)
#define ADF_4XXX_IASTATSSM_CERRSSMMMP3_MASK  BIT(9)
#define ADF_4XXX_IASTATSSM_UERRSSMMMP4_MASK  BIT(10)
#define ADF_4XXX_IASTATSSM_CERRSSMMMP4_MASK  BIT(11)
#define ADF_4XXX_IASTATSSM_PPERR_MASK    BIT(12)
#define ADF_4XXX_IASTATSSM_SPPPAR_ERR_MASK  BIT(14)
#define ADF_4XXX_IASTATSSM_CPPPAR_ERR_MASK  BIT(15)
#define ADF_4XXX_IASTATSSM_RFPAR_ERR_MASK  BIT(16)

#define ADF_4XXX_PPERR_INTS_CLEAR_MASK BIT(0)

/* Resource manager error status */
#define ADF_4XXX_RSRCMGRERROR (0x448)

/* Misc interrupts and errors. */
#define ADF_4XXX_SINTPF (0x41A080)

#define ADF_4XXX_QM_PAR_STS (0x500660)
#define ADF_4XXX_RL_PAR_STS (0x500654)

/* AT exception status register */
#define ADF_4XXX_AT_EXCEP_STS (0x50502C)

/* Rate Limiter Error Log Register */
#define ADF_4XXX_RLT_ERRLOG (0x508814)

/* Error registers for MMP. */
#define ADF_4XXX_MAX_MMP (5)

#define ADF_4XXX_MMP_BASE(i)       ((i) * 0x1000 % 0x3800)
#define ADF_4XXX_CERRSSMMMP(i, n)  ((i) * 0x4000 + \
		     ADF_4XXX_MMP_BASE(n) + 0x380)
#define ADF_4XXX_UERRSSMMMP(i, n)  ((i) * 0x4000 + \
		     ADF_4XXX_MMP_BASE(n) + 0x388)
#define ADF_4XXX_UERRSSMMMPAD(i, n)    ((i) * 0x4000 + \
		     ADF_4XXX_MMP_BASE(n) + 0x38C)
#define ADF_4XXX_INTMASKSSM(i)     ((i) * 0x4000 + 0x0)

#define ADF_4XXX_UERRSSMMMP_INTS_CLEAR_MASK ((BIT(16) || BIT(0)))
#define ADF_4XXX_CERRSSMMMP_INTS_CLEAR_MASK BIT(0)

/* ARAM region sizes in bytes */
#define ADF_4XXX_DEF_ASYM_MASK 0x1

/* Arbiter configuration */
#define ADF_4XXX_ARB_OFFSET			(0x0)
#define ADF_4XXX_ARB_WRK_2_SER_MAP_OFFSET      (0x400)

/* Admin Interface Reg Offset */
#define ADF_4XXX_ADMINMSGUR_OFFSET         (0x500574)
#define ADF_4XXX_ADMINMSGLR_OFFSET         (0x500578)
#define ADF_4XXX_MAILBOX_BASE_OFFSET       (0x600970)

/*RP PASID register*/
#define ADF_4XXX_PASID_BASE (0x104000)

#define ADF_4XXX_PRIV_ENABLE_PIDVECTABLE BIT(31)
#define ADF_4XXX_PASID_ENABLE_PIDVECTABLE BIT(30)
#define ADF_4XXX_AT_ENABLE_PIDVECTABLE BIT(29)
#define ADF_4XXX_AI_ENABLE_PIDVECTABLE BIT(28)
#define ADF_4XXX_PRIV_ENABLE_PLD BIT(27)
#define ADF_4XXX_PASID_ENABLE_PLD BIT(26)
#define ADF_4XXX_AT_ENABLE_PLD BIT(25)
#define ADF_4XXX_AI_ENABLE_PLD BIT(24)
#define ADF_4XXX_PRIV_ENABLE_RING BIT(23)
#define ADF_4XXX_PASID_ENABLE_RING BIT(22)
#define ADF_4XXX_AT_ENABLE_RING BIT(21)
#define ADF_4XXX_AI_ENABLE_RING BIT(20)
#define ADF_4XXX_PASID_MSK (0xFFFFF)

/*User Queue*/
#define ADF_4XXX_UQPIDVECTABLELBASE (0x105000)
#define ADF_4XXX_UQPIDVECTABLEUBASE (0x105004)
#define ADF_4XXX_UQPIDVECTABLESIZE (0x105008)

#define ADF_4XXX_UQSWQWTRMRK (0x101044)

/*Reset*/

/*KPT reset*/
#define ADF_4XXX_KPTRP_RESETSTATUS0 (0x500A00)
#define ADF_4XXX_KPTRP_RESETSTATUS1 (0x500A04)

/*Ring Pair reset*/
#define ADF_4XXX_RPRESETCTL (0x106000)

/*qat_4xxx fuse bits are different from old GENs, redefine it*/
enum icp_qat_4xxx_slice_mask {
	ICP_ACCEL_4XXX_MASK_CIPHER_SLICE = 0x01,
	ICP_ACCEL_4XXX_MASK_AUTH_SLICE = 0x02,
	ICP_ACCEL_4XXX_MASK_PKE_SLICE = 0x04,
	ICP_ACCEL_4XXX_MASK_COMPRESS_SLICE = 0x08,
	ICP_ACCEL_4XXX_MASK_UCS_SLICE = 0x10,
	ICP_ACCEL_4XXX_MASK_EIA3_SLICE = 0x20,
	/*SM3&SM4 are indicated by same bit*/
	ICP_ACCEL_4XXX_MASK_SMX_SLICE = 0x80,
};

/* RL constants */
#define ADF_4XXX_RL_SLICE_REF 1000UL
#define ADF_4XXX_RL_MAX_TP_ASYM 173750UL
#define ADF_4XXX_RL_MAX_TP_SYM 95000UL
#define ADF_4XXX_RL_MAX_TP_DC 45000UL
#define ADF_401XX_RL_SLICE_REF 1000UL
#define ADF_401XX_RL_MAX_TP_ASYM 173750UL
#define ADF_401XX_RL_MAX_TP_SYM 95000UL
#define ADF_401XX_RL_MAX_TP_DC 45000UL
#define ADF_4XXX_RL_CNV_SLICE 1
#define ADF_401XX_RL_CNV_SLICE 1

/* Interrupt Coalesce Timer Defaults */
#define ADF_4XXX_ACCEL_DEF_COALESCE_TIMER 1000
#define ADF_4XXX_COALESCING_MIN_TIME 0x1F
#define ADF_4XXX_COALESCING_MAX_TIME 0xFFFF
#define ADF_4XXX_COALESCING_DEF_TIME 0x1F4

/* Firmware Binary */
#define ADF_4XXX_FW "qat_4xxx.bin"
#define ADF_4XXX_MMP "qat_4xxx_mmp.bin"
#define ADF_4XXX_DC_OBJ "qat_4xxx_dc.bin"
#define ADF_4XXX_SYM_OBJ "qat_4xxx_sym.bin"
#define ADF_4XXX_ASYM_OBJ "qat_4xxx_asym.bin"
#define ADF_4XXX_ADMIN_OBJ "qat_4xxx_admin.bin"
#define ADF_402XX_FW "qat_402xx.bin"
#define ADF_402XX_MMP "qat_402xx_mmp.bin"
#define ADF_402XX_DC_OBJ "qat_402xx_dc.bin"
#define ADF_402XX_SYM_OBJ "qat_402xx_sym.bin"
#define ADF_402XX_ASYM_OBJ "qat_402xx_asym.bin"
#define ADF_402XX_ADMIN_OBJ "qat_402xx_admin.bin"

/*Only 3 types of images can be loaded including the admin image*/
#define ADF_4XXX_MAX_OBJ 3

void adf_init_hw_data_4xxx(struct adf_hw_device_data *hw_data, u32 id);
void adf_clean_hw_data_4xxx(struct adf_hw_device_data *hw_data);

int adf_4xxx_qat_crypto_dev_config(struct adf_accel_dev *accel_dev);

#define ADF_4XXX_AE_FREQ          (1000 * 1000000)
#endif
