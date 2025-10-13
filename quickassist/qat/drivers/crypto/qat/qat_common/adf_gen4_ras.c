// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2022 Intel Corporation */
#include "adf_common_drv.h"
#include "adf_dev_err.h"
#include "adf_gen4_hw_data.h"
#include "adf_gen4_ras.h"

struct reg_info {
	size_t offs;
	char *name;
};

static struct reg_info adf_gen4_err_regs[] = {
	{ADF_GEN4_ERRSOU0, "ERRSOU0"},
	{ADF_GEN4_ERRMSK0, "ERRMSK0"},
	{ADF_GEN4_ERRSOU1, "ERRSOU1"},
	{ADF_GEN4_ERRMSK1, "ERRMSK1"},
	{ADF_GEN4_ERRSOU2, "ERRSOU2"},
	{ADF_GEN4_ERRMSK2, "ERRMSK2"},
	{ADF_GEN4_ERRSOU3, "ERRSOU3"},
	{ADF_GEN4_ERRMSK3, "ERRMSK3"},

	{ADF_GEN4_HIAECORERRLOG_CPP0, "HIAECORERRLOG_CPP0"},
	{ADF_GEN4_HIAECORERRLOGENABLE_CPP0, "HIAECORERRLOGENABLE_CPP0"},

	{ADF_GEN4_HIAEUNCERRLOG_CPP0, "HIAEUNCERRLOG_CPP"},
	{ADF_GEN4_HIAEUNCERRLOGENABLE_CPP0, "HIAEUNCERRLOGENABLE_CPP"},
	{ADF_GEN4_HICPPAGENTCMDPARERRLOG, "HICPPAGENTCMDPARERRLOG"},
	{ADF_GEN4_HICPPAGENTCMDPARERRLOGENABLE,
	 "HICPPAGENTCMDPARERRLOGENABLE"},
	{ADF_GEN4_RIMEM_PARERR_STS, "RIMEM_PARERR_STS"},
	{ADF_GEN4_RI_MEM_PAR_ERR_EN0, "RI_MEM_PAR_ERR_EN0"},
	{ADF_GEN4_TI_CI_PAR_STS, "TI_CI_PAR_STS"},
	{ADF_GEN4_TI_CI_PAR_ERR_MASK, "TI_CI_PAR_ERR_MASK"},
	{ADF_GEN4_TI_CD_PAR_STS, "TI_CD_PAR_STS"},
	{ADF_GEN4_TI_CD_PAR_ERR_MASK, "TI_CD_PAR_ERR_MASK"},
	{ADF_GEN4_TI_PULL0FUB_PAR_STS, "TI_PULL0FUB_PAR_STS"},
	{ADF_GEN4_TI_PULL0FUB_PAR_ERR_MASK, "TI_PULL0FUB_PAR_ERR_MASK"},
	{ADF_GEN4_TI_PUSHFUB_PAR_STS, "TI_PUSHFUB_PAR_STS"},
	{ADF_GEN4_TI_PUSHFUB_PAR_ERR_MASK, "TI_PUSHFUB_PAR_ERR_MASK"},
	{ADF_GEN4_TI_TRNSB_PAR_STS, "TI_TRNSB_PAR_STS"},
	{ADF_GEN4_TI_TRNSB_PAR_ERR_MASK, "TI_TRNSB_PAR_ERR_MASK"},
	{ADF_GEN4_RIMISCSTS, "RIMISCSTS"},
	{ADF_GEN4_RIMISCCTL, "RIMISCCTL"},

	{ADF_GEN4_IAINTSTATSSM, "IAINTSTATSSM"},
	{ADF_GEN4_INTMASKSSM, "INTMASKSSM"},
	{ADF_GEN4_UERRSSMSH, "UERRSSMSH"},
	{ADF_GEN4_UERRSSMSHAD, "UERRSSMSHAD"},
	{ADF_GEN4_CERRSSMSH, "CERRSSMSH"},
	{ADF_GEN4_CERRSSMSHAD, "CERRSSMSHAD"},
	{ADF_GEN4_SSMFEATREN, "SSMFEATREN"},
	{ADF_GEN4_PPERR, "PPERR"},
	{ADF_GEN4_PPERRID, "PPERRID"},
	{ADF_GEN4_SSMCPPERR, "SSMCPPERR"},
	{ADF_GEN4_SER_ERR_SSMSH, "SER_ERR_SSMSH"},
	{ADF_GEN4_SER_EN_SSMSH, "SER_EN_SSMSH"},

	{ADF_GEN4_SSMSOFTERRORPARITY_SRC, "SSMSOFTERRORPARITY_SRC"},
	{ADF_GEN4_SSMSOFTERRORPARITYMASK_SRC, "SSMSOFTERRORPARITYMASK_SRC"},

	{ADF_GEN4_SSMSOFTERRORPARITY_ATH_CPH, "SSMSOFTERRORPARITY_ATH_CPH"},
	{ADF_GEN4_SSMSOFTERRORPARITYMASK_ATH_CPH,
	 "SSMSOFTERRORPARITYMASK_ATH_CPH"},

	{ADF_GEN4_SSMSOFTERRORPARITY_CPR_XLT, "SSMSOFTERRORPARITY_CPR_XLT"},
	{ADF_GEN4_SSMSOFTERRORPARITYMASK_CPR_XLT,
	 "SSMSOFTERRORPARITYMASK_CPR_XLT"},

	{ADF_GEN4_SSMSOFTERRORPARITY_DCPR_UCS, "SSMSOFTERRORPARITY_DCPR_UCS"},
	{ADF_GEN4_SSMSOFTERRORPARITYMASK_DCPR_UCS,
	 "SSMSOFTERRORPARITYMASK_DCPR_UCS"},

	{ADF_GEN4_SSMSOFTERRORPARITY_PKE, "SSMSOFTERRORPARITY_PKE"},
	{ADF_GEN4_SSMSOFTERRORPARITYMASK_PKE, "SSMSOFTERRORPARITYMASK_PKE"},

	{ADF_GEN4_SPPPULLCMDPARERR_ATH_CPH, "SPPPULLCMDPARERR_ATH_CPH"},
	{ADF_GEN4_SPPPULLCMDPARERR_CPR_XLT, "SPPPULLCMDPARERR_CPR_XLT"},
	{ADF_GEN4_SPPPULLCMDPARERR_DCPR_UCS, "SPPPULLCMDPARERR_DCPR_UCS"},
	{ADF_GEN4_SPPPULLCMDPARERR_PKE, "SPPPULLCMDPARERR_PKE"},

	{ADF_GEN4_SPPPUSHCMDPARERR_ATH_CPH, "SPPPUSHCMDPARERR_ATH_CPH"},
	{ADF_GEN4_SPPPUSHCMDPARERR_CPR_XLT, "SPPPUSHCMDPARERR_CPR_XLT"},
	{ADF_GEN4_SPPPUSHCMDPARERR_DCPR_UCS, "SPPPUSHCMDPARERR_DCPR_UCS"},
	{ADF_GEN4_SPPPUSHCMDPARERR_PKE, "SPPPUSHCMDPARERR_PKE"},

	{ADF_GEN4_SPPPULLDATAPARERR_ATH_CPH, "SPPPULLDATAPARERR_ATH_CPH"},
	{ADF_GEN4_SPPPULLDATAPARERR_CPR_XLT, "SPPPULLDATAPARERR_CPR_XLT"},
	{ADF_GEN4_SPPPULLDATAPARERR_DCPR_UCS, "SPPPULLDATAPARERR_DCPR_UCS"},
	{ADF_GEN4_SPPPULLDATAPARERR_PKE, "SPPPULLDATAPARERR_PKE"},

	{ADF_GEN4_SPPPUSHDATAPARERR_ATH_CPH, "SPPPUSHDATAPARERR_ATH_CPH"},
	{ADF_GEN4_SPPPUSHDATAPARERR_CPR_XLT, "SPPPUSHDATAPARERR_CPR_XLT"},
	{ADF_GEN4_SPPPUSHDATAPARERR_DCPR_UCS, "SPPPUSHDATAPARERR_DCPR_UCS"},
	{ADF_GEN4_SPPPUSHDATAPARERR_PKE, "SPPPUSHDATAPARERR_PKE"},

	{ADF_GEN4_SPPPARERRMSK_ATH_CPH, "SPPPARERRMSK_ATH_CPH"},
	{ADF_GEN4_SPPPARERRMSK_CPR_XLT, "SPPPARERRMSK_CPR_XLT"},
	{ADF_GEN4_SPPPARERRMSK_DCPR_UCS, "SPPPARERRMSK_DCPR_UCS"},
	{ADF_GEN4_SPPPARERRMSK_PKE, "SPPPARERRMSK_PKE"},

	{ADF_GEN4_CPP_CFC_ERR_STATUS, "CPP_CFC_ERR_STATUS"},
	{ADF_GEN4_CPP_CFC_ERR_CTRL, "CPP_CFC_ERR_CTRL"},
	{ADF_GEN4_CPP_CFC_ERR_STATUS_CLR, "CPP_CFC_ERR_STATUS_CLR"},
	{ADF_GEN4_CPP_CFC_ERR_PPID_LO, "CPP_CFC_ERR_PPID_LO"},
	{ADF_GEN4_CPP_CFC_ERR_PPID_HI, "CPP_CFC_ERR_PPID_HI"},

	{ADF_GEN4_SLICEHANGSTATUS_ATH_CPH, "SLICEHANGSTATUS_ATH_CPH"},
	{ADF_GEN4_SLICEHANGSTATUS_CPR_XLT, "SLICEHANGSTATUS_CPR_XLT"},
	{ADF_GEN4_SLICEHANGSTATUS_DCPR_UCS, "SLICEHANGSTATUS_DCPR_UCS"},
	{ADF_GEN4_SLICEHANGSTATUS_PKE, "SLICEHANGSTATUS_PKE"},

	{ADF_GEN4_SHINTMASKSSM_ATH_CPH, "SHINTMASKSSM_ATH_CPH"},
	{ADF_GEN4_SHINTMASKSSM_CPR_XLT, "SHINTMASKSSM_CPR_XLT"},
	{ADF_GEN4_SHINTMASKSSM_DCPR_UCS, "SHINTMASKSSM_DCPR_UCS"},
	{ADF_GEN4_SHINTMASKSSM_PKE, "SHINTMASKSSM_PKE"},

	{ADF_GEN4_TIMISCSTS, "TIMISCSTS"},
	{ADF_GEN4_TIMISCCTL, "TIMISCCTL"},

	{ADF_GEN4_RICPPINTSTS, "RICPPINTSTS"},
	{ADF_GEN4_RICPPINTCTL, "RICPPINTCTL"},

	{ADF_GEN4_RIERRPUSHID, "RIERRPUSHID"},
	{ADF_GEN4_RIERRPULLID, "RIERRPULLID"},

	{ADF_GEN4_TICPPINTSTS, "TICPPINTSTS"},
	{ADF_GEN4_TICPPINTCTL, "TICPPINTCTL"},

	{ADF_GEN4_TIERRPUSHID, "TIERRPUSHID"},
	{ADF_GEN4_TIERRPULLID, "TIERRPULLID"},

	{ADF_GEN4_REG_ARAMCERRUERR_EN, "REG_ARAMCERRUERR_EN"},
	{ADF_GEN4_REG_ARAMCERR, "REG_ARAMCERR"},
	{ADF_GEN4_REG_ARAMCERRAD, "REG_ARAMCERRAD"},
	{ADF_GEN4_REG_ARAMUERR, "REG_ARAMUERR"},
	{ADF_GEN4_REG_ARAMUERRAD, "REG_ARAMUERRAD"},
	{ADF_GEN4_REG_ERRPPID_LO, "REG_ERRPPID_LO"},
	{ADF_GEN4_REG_ERRPPID_HI, "REG_ERRPPID_HI"},
	{ADF_GEN4_REG_CPPMEMTGTERR, "REG_CPPMEMTGTERR"},
};

static struct reg_info adf_gen4_special_err_regs[] = {
	{ADF_GEN4_SPPPULLCMDPARERR_WAT_WCP, "SPPPULLCMDPARERR_WAT_WCP"},
	{ADF_GEN4_SPPPULLDATAPARERR_WAT_WCP, "SPPPULLDATAPARERR_WAT_WCP"},
	{ADF_GEN4_SPPPUSHCMDPARERR_WAT_WCP, "SPPPUSHCMDPARERR_WAT_WCP"},
	{ADF_GEN4_SPPPUSHDATAPARERR_WAT_WCP, "SPPPUSHDATAPARERR_WAT_WCP"},
	{ADF_GEN4_SPPPARERRMSK_WAT_WCP, "SPPPARERRMSK_WAT_WCP"},
	{ADF_GEN4_SSMSOFTERRORPARITY_WAT_WCP, "SSMSOFTERRORPARITY_WAT_WCP"},
	{ADF_GEN4_SSMSOFTERRORPARITYMASK_WAT_WCP,
	 "SSMSOFTERRORPARITYMASK_WAT_WCP"},
	{ADF_GEN4_SLICEHANGSTATUS_WAT_WCP, "SLICEHANGSTATUS_WAT_WCP"},
	{ADF_GEN4_SHINTMASKSSM_WAT_WCP, "SHINTMASKSSM_WAT_WCP"},
};

static void enable_errsou_reporting(void __iomem *csr)
{
	/* Enable correctable error reporting in ERRSOU0 */
	ADF_CSR_WR(csr, ADF_GEN4_ERRMSK0, 0);

	/* Enable uncorrectable error reporting in ERRSOU1 */
	ADF_CSR_WR(csr, ADF_GEN4_ERRMSK1, 0);

	/* Enable uncorrectable error reporting in ERRSOU2
	 * but disable PM interrupt and CFC attention interrupt by default
	 */
	ADF_CSR_WR(csr, ADF_GEN4_ERRMSK2,
		   (ADF_GEN4_ERRSOU2_PM_INT_BIT |
		    ADF_GEN4_ERRSOU2_CPP_CFC_ATT_INT_BITMASK));

	/* Enable uncorrectable error reporting in ERRSOU3
	 * but disable RLT error interrupt and VFLR notify interrupt by default
	 */
	ADF_CSR_WR(csr, ADF_GEN4_ERRMSK3,
		   (ADF_GEN4_ERRSOU3_RLTERROR_BIT |
		    ADF_GEN4_ERRSOU3_VFLRNOTIFY_BIT));
}

static void disable_errsou_reporting(void __iomem *csr)
{
	/* Disable correctable error reporting in ERRSOU0 */
	ADF_CSR_WR(csr, ADF_GEN4_ERRMSK0,
		   ADF_GEN4_ERRSOU0_BIT);

	/* Disable uncorrectable error reporting in ERRSOU1 */
	ADF_CSR_WR(csr, ADF_GEN4_ERRMSK1,
		   ADF_GEN4_ERRSOU1_BITMASK);

	/* Disable uncorrectable error reporting in ERRSOU2 */
	adf_csr_fetch_and_or(csr, ADF_GEN4_ERRMSK2,
			     ADF_GEN4_ERRSOU2_DIS_BITMASK);

	/* Disable uncorrectable error reporting in ERRSOU3 */
	ADF_CSR_WR(csr, ADF_GEN4_ERRMSK3,
		   ADF_GEN4_ERRSOU3_BITMASK);
}

static void enable_ae_error_reporting(void __iomem *csr,
				      struct adf_accel_dev *accel_dev)
{
	u32 ae_mask = GET_HW_DATA(accel_dev)->get_ae_mask(accel_dev);

	/* Enable Acceleration Engine correctable error reporting */
	ADF_CSR_WR(csr, ADF_GEN4_HIAECORERRLOGENABLE_CPP0, ae_mask);

	/* Enable Acceleration Engine uncorrectable error reporting */
	ADF_CSR_WR(csr, ADF_GEN4_HIAEUNCERRLOGENABLE_CPP0, ae_mask);
}

static void disable_ae_error_reporting(void __iomem *csr)
{
	/* Disable Acceleration Engine correctable error reporting */
	ADF_CSR_WR(csr, ADF_GEN4_HIAECORERRLOGENABLE_CPP0, 0);

	/* Disable Acceleration Engine uncorrectable error reporting */
	ADF_CSR_WR(csr, ADF_GEN4_HIAEUNCERRLOGENABLE_CPP0, 0);
}

static void enable_cpp_error_reporting(void __iomem *csr,
				       struct adf_accel_dev *accel_dev)
{
	struct adf_dev_err_mask *err_mask = &accel_dev->hw_device->dev_err_mask;

	/* Enable HI CPP Agents Command Parity Error Reporting */
	ADF_CSR_WR(csr,
		   ADF_GEN4_HICPPAGENTCMDPARERRLOGENABLE,
		   err_mask->cppagentcmdpar_mask);

	ADF_CSR_WR(csr, ADF_GEN4_CPP_CFC_ERR_CTRL,
		   ADF_GEN4_CPP_CFC_ERR_CTRL_BITMASK);
}

static void disable_cpp_error_reporting(void __iomem *csr)
{
	/* Disable HI CPP Agents Command Parity Error Reporting */
	ADF_CSR_WR(csr, ADF_GEN4_HICPPAGENTCMDPARERRLOGENABLE, 0);

	ADF_CSR_WR(csr, ADF_GEN4_CPP_CFC_ERR_CTRL,
		   ADF_GEN4_CPP_CFC_ERR_CTRL_DIS_BITMASK);
}

static void enable_ti_ri_error_reporting(void __iomem *csr)
{
	u32 reg;

	/* Enable RI Memory error reporting */
	ADF_CSR_WR(csr, ADF_GEN4_RI_MEM_PAR_ERR_EN0,
		   ADF_GEN4_RIMEM_PARERR_STS_FATAL_BITMASK |
		   ADF_GEN4_RIMEM_PARERR_STS_UNCERR_BITMASK);

	/* Enable IOSF Primary Command Parity error Reporting */
	ADF_CSR_WR(csr, ADF_GEN4_RIMISCCTL,
		   ADF_GEN4_RIMISCSTS_BIT);

	/* Enable TI Internal Memory Parity Error reporting */
	ADF_CSR_WR(csr, ADF_GEN4_TI_CI_PAR_ERR_MASK, 0);
	ADF_CSR_WR(csr, ADF_GEN4_TI_PULL0FUB_PAR_ERR_MASK, 0);
	ADF_CSR_WR(csr, ADF_GEN4_TI_PUSHFUB_PAR_ERR_MASK, 0);
	ADF_CSR_WR(csr, ADF_GEN4_TI_CD_PAR_ERR_MASK, 0);
	ADF_CSR_WR(csr, ADF_GEN4_TI_TRNSB_PAR_ERR_MASK, 0);

	/* Enable error handling in RI, TI CPP interface control registers */
	ADF_CSR_WR(csr, ADF_GEN4_RICPPINTCTL,
		   ADF_GEN4_RICPPINTCTL_BITMASK);

	ADF_CSR_WR(csr, ADF_GEN4_TICPPINTCTL,
		   ADF_GEN4_TICPPINTCTL_BITMASK);

	/* Enable error detection and reporting in TIMISCSTS
	 * with bits 1, 2 and 30 value preserved
	 */
	reg = ADF_CSR_RD(csr, ADF_GEN4_TIMISCCTL);
	reg &= ADF_GEN4_TIMSCCTL_RELAY_BITMASK;
	reg |= ADF_GEN4_TIMISCCTL_BIT;
	ADF_CSR_WR(csr, ADF_GEN4_TIMISCCTL, reg);
}

static void disable_ti_ri_error_reporting(void __iomem *csr)
{
	u32 reg;

	/* Disable RI Memory error reporting */
	ADF_CSR_WR(csr, ADF_GEN4_RI_MEM_PAR_ERR_EN0, 0);

	/* Disable IOSF Primary Command Parity error Reporting */
	ADF_CSR_WR(csr, ADF_GEN4_RIMISCCTL, 0);

	/* Disable TI Internal Memory Parity Error reporting */
	ADF_CSR_WR(csr, ADF_GEN4_TI_CI_PAR_ERR_MASK,
		   ADF_GEN4_TI_CI_PAR_STS_BITMASK);
	ADF_CSR_WR(csr, ADF_GEN4_TI_PULL0FUB_PAR_ERR_MASK,
		   ADF_GEN4_TI_PULL0FUB_PAR_STS_BITMASK);
	ADF_CSR_WR(csr, ADF_GEN4_TI_PUSHFUB_PAR_ERR_MASK,
		   ADF_GEN4_TI_PUSHFUB_PAR_STS_BITMASK);
	ADF_CSR_WR(csr, ADF_GEN4_TI_CD_PAR_ERR_MASK,
		   ADF_GEN4_TI_CD_PAR_STS_BITMASK);
	ADF_CSR_WR(csr, ADF_GEN4_TI_TRNSB_PAR_ERR_MASK,
		   ADF_GEN4_TI_TRNSB_PAR_STS_BITMASK);

	/* Disable error handling in RI, TI CPP interface control registers */
	ADF_CSR_WR(csr, ADF_GEN4_RICPPINTCTL, 0);

	ADF_CSR_WR(csr, ADF_GEN4_TICPPINTCTL, 0);

	/* Disable error detection and reporting in TIMISCSTS
	 * with bits 1, 2 and 30 value preserved
	 */
	reg = ADF_CSR_RD(csr, ADF_GEN4_TIMISCCTL);
	reg &= ADF_GEN4_TIMSCCTL_RELAY_BITMASK;
	ADF_CSR_WR(csr, ADF_GEN4_TIMISCCTL, reg);
}

static void enable_rf_error_reporting(void __iomem *csr,
				      struct adf_accel_dev *accel_dev)
{
	struct adf_dev_err_mask *err_mask =
		&accel_dev->hw_device->dev_err_mask;

	if (GET_HW_DATA(accel_dev)->qat_aux_enable)
		return;

	/* Enable RF parity error in Shared RAM */
	ADF_CSR_WR(csr, ADF_GEN4_SSMSOFTERRORPARITYMASK_SRC, 0);
	ADF_CSR_WR(csr, ADF_GEN4_SSMSOFTERRORPARITYMASK_ATH_CPH, 0);
	ADF_CSR_WR(csr, ADF_GEN4_SSMSOFTERRORPARITYMASK_CPR_XLT, 0);
	ADF_CSR_WR(csr, ADF_GEN4_SSMSOFTERRORPARITYMASK_DCPR_UCS, 0);
	ADF_CSR_WR(csr, ADF_GEN4_SSMSOFTERRORPARITYMASK_PKE, 0);

	if (err_mask->parerr_wat_wcp_mask)
		ADF_CSR_WR(csr, ADF_GEN4_SSMSOFTERRORPARITYMASK_WAT_WCP, 0);
}

static void disable_rf_error_reporting(void __iomem *csr,
				       struct adf_accel_dev *accel_dev)
{
	struct adf_dev_err_mask *err_mask =
		&accel_dev->hw_device->dev_err_mask;

	if (GET_HW_DATA(accel_dev)->qat_aux_enable)
		return;

	/* Disable RF Parity Error reporting in Shared RAM */
	ADF_CSR_WR(csr, ADF_GEN4_SSMSOFTERRORPARITYMASK_SRC,
		   ADF_GEN4_SSMSOFTERRORPARITY_SRC_BIT);

	ADF_CSR_WR(csr, ADF_GEN4_SSMSOFTERRORPARITYMASK_ATH_CPH,
		   err_mask->parerr_ath_cph_mask);

	ADF_CSR_WR(csr, ADF_GEN4_SSMSOFTERRORPARITYMASK_CPR_XLT,
		   err_mask->parerr_cpr_xlt_mask);

	ADF_CSR_WR(csr, ADF_GEN4_SSMSOFTERRORPARITYMASK_DCPR_UCS,
		   err_mask->parerr_dcpr_ucs_mask);

	ADF_CSR_WR(csr, ADF_GEN4_SSMSOFTERRORPARITYMASK_PKE,
		   err_mask->parerr_pke_mask);

	if (err_mask->parerr_wat_wcp_mask)
		ADF_CSR_WR(csr, ADF_GEN4_SSMSOFTERRORPARITYMASK_WAT_WCP,
			   err_mask->parerr_wat_wcp_mask);
}

static void enable_ssm_error_reporting(void __iomem *csr,
				       struct adf_accel_dev *accel_dev)
{
	struct adf_dev_err_mask *err_mask =
		&accel_dev->hw_device->dev_err_mask;

	if (GET_HW_DATA(accel_dev)->qat_aux_enable)
		return;

	/* Enable SSM interrupts */
	ADF_CSR_WR(csr, ADF_GEN4_INTMASKSSM, 0);

	/* Enable shared memory error detection & correction */
	adf_csr_fetch_and_or(csr, ADF_GEN4_SSMFEATREN,
			     err_mask->ssmfeatren_mask);

	/* Enable SER detection in SER_err_ssmsh register */
	ADF_CSR_WR(csr, ADF_GEN4_SER_EN_SSMSH,
		   ADF_GEN4_SER_EN_SSMSH_BITMASK);

	/* Enable SSM soft parity error */
	ADF_CSR_WR(csr, ADF_GEN4_SPPPARERRMSK_ATH_CPH, 0);
	ADF_CSR_WR(csr, ADF_GEN4_SPPPARERRMSK_CPR_XLT, 0);
	ADF_CSR_WR(csr, ADF_GEN4_SPPPARERRMSK_DCPR_UCS, 0);
	ADF_CSR_WR(csr, ADF_GEN4_SPPPARERRMSK_PKE, 0);

	if (err_mask->parerr_wat_wcp_mask)
		ADF_CSR_WR(csr, ADF_GEN4_SPPPARERRMSK_WAT_WCP, 0);

	/* Enable slice hang interrupt reporting */
	ADF_CSR_WR(csr, ADF_GEN4_SHINTMASKSSM_ATH_CPH, 0);
	ADF_CSR_WR(csr, ADF_GEN4_SHINTMASKSSM_CPR_XLT, 0);
	ADF_CSR_WR(csr, ADF_GEN4_SHINTMASKSSM_DCPR_UCS, 0);
	ADF_CSR_WR(csr, ADF_GEN4_SHINTMASKSSM_PKE, 0);

	if (err_mask->parerr_wat_wcp_mask)
		ADF_CSR_WR(csr, ADF_GEN4_SHINTMASKSSM_WAT_WCP, 0);
}

static void disable_ssm_error_reporting(void __iomem *csr,
					struct adf_accel_dev *accel_dev)
{
	struct adf_dev_err_mask *err_mask =
		&accel_dev->hw_device->dev_err_mask;

	if (GET_HW_DATA(accel_dev)->qat_aux_enable)
		return;

	/* Disable SSM interrupts */
	ADF_CSR_WR(csr, ADF_GEN4_INTMASKSSM,
		   ADF_GEN4_INTMASKSSM_BITMASK);

	/* Disable shared memory error detection & correction */
	adf_csr_fetch_and_and(csr, ADF_GEN4_SSMFEATREN,
			      ADF_GEN4_SSMFEATREN_DIS_BITMASK);

	/* Disable SER detection in SER_err_ssmsh register */
	ADF_CSR_WR(csr, ADF_GEN4_SER_EN_SSMSH, 0);

	/* Disable SSM soft parity error */
	ADF_CSR_WR(csr, ADF_GEN4_SPPPARERRMSK_ATH_CPH,
		   err_mask->parerr_ath_cph_mask);

	ADF_CSR_WR(csr, ADF_GEN4_SPPPARERRMSK_CPR_XLT,
		   err_mask->parerr_cpr_xlt_mask);

	ADF_CSR_WR(csr, ADF_GEN4_SPPPARERRMSK_DCPR_UCS,
		   err_mask->parerr_dcpr_ucs_mask);

	ADF_CSR_WR(csr, ADF_GEN4_SPPPARERRMSK_PKE,
		   err_mask->parerr_pke_mask);

	if (err_mask->parerr_wat_wcp_mask)
		ADF_CSR_WR(csr, ADF_GEN4_SPPPARERRMSK_WAT_WCP,
			   err_mask->parerr_wat_wcp_mask);

	/* Disable slice hang interrupt reporting */
	ADF_CSR_WR(csr, ADF_GEN4_SHINTMASKSSM_ATH_CPH,
		   err_mask->parerr_ath_cph_mask);

	ADF_CSR_WR(csr, ADF_GEN4_SHINTMASKSSM_CPR_XLT,
		   err_mask->parerr_cpr_xlt_mask);

	ADF_CSR_WR(csr, ADF_GEN4_SHINTMASKSSM_DCPR_UCS,
		   err_mask->parerr_dcpr_ucs_mask);

	ADF_CSR_WR(csr, ADF_GEN4_SHINTMASKSSM_PKE,
		   err_mask->parerr_pke_mask);

	if (err_mask->parerr_wat_wcp_mask)
		ADF_CSR_WR(csr, ADF_GEN4_SHINTMASKSSM_WAT_WCP,
			   err_mask->parerr_wat_wcp_mask);
}

static void enable_aram_error_reporting(void __iomem *csr)
{
	ADF_CSR_WR(csr, ADF_GEN4_REG_ARAMCERRUERR_EN,
		   ADF_GEN4_REG_ARAMCERRUERR_EN_BITMASK);

	ADF_CSR_WR(csr, ADF_GEN4_REG_ARAMCERR,
		   ADF_GEN4_REG_ARAMCERR_EN_BITMASK);

	ADF_CSR_WR(csr, ADF_GEN4_REG_ARAMUERR,
		   ADF_GEN4_REG_ARAMUERR_EN_BITMASK);

	ADF_CSR_WR(csr, ADF_GEN4_REG_CPPMEMTGTERR,
		   ADF_GEN4_REG_CPPMEMTGTERR_EN_BITMASK);
}

static void disable_aram_error_reporting(void __iomem *csr)
{
	ADF_CSR_WR(csr, ADF_GEN4_REG_ARAMCERRUERR_EN, 0);

	ADF_CSR_WR(csr, ADF_GEN4_REG_ARAMCERR, 0);

	ADF_CSR_WR(csr, ADF_GEN4_REG_ARAMUERR, 0);

	ADF_CSR_WR(csr, ADF_GEN4_REG_CPPMEMTGTERR, 0);
}

void adf_gen4_enable_ras(struct adf_accel_dev *accel_dev)
{
	void __iomem *csr = adf_get_pmisc_base(accel_dev);
	void __iomem *aram_csr = adf_get_param_base(accel_dev);

	enable_errsou_reporting(csr);
	enable_ae_error_reporting(csr, accel_dev);
	enable_cpp_error_reporting(csr, accel_dev);
	enable_ti_ri_error_reporting(csr);
	enable_rf_error_reporting(csr, accel_dev);
	enable_ssm_error_reporting(csr, accel_dev);
	enable_aram_error_reporting(aram_csr);
}
EXPORT_SYMBOL_GPL(adf_gen4_enable_ras);

void adf_gen4_disable_ras(struct adf_accel_dev *accel_dev)
{
	void __iomem *csr = adf_get_pmisc_base(accel_dev);
	void __iomem *aram_csr = adf_get_param_base(accel_dev);

	disable_errsou_reporting(csr);
	disable_ae_error_reporting(csr);
	disable_cpp_error_reporting(csr);
	disable_ti_ri_error_reporting(csr);
	disable_rf_error_reporting(csr, accel_dev);
	disable_ssm_error_reporting(csr, accel_dev);
	disable_aram_error_reporting(aram_csr);
}
EXPORT_SYMBOL_GPL(adf_gen4_disable_ras);

static void adf_gen4_process_errsou0(struct adf_accel_dev *accel_dev,
				     void __iomem *csr)
{
	u32 aecorrerr =
		ADF_CSR_RD(csr, ADF_GEN4_HIAECORERRLOG_CPP0);

	aecorrerr &= GET_HW_DATA(accel_dev)->get_ae_mask(accel_dev);

	dev_warn(&GET_DEV(accel_dev),
		 "Correctable error detected in AE: 0x%x\n",
		 aecorrerr);

	atomic_inc(&accel_dev->ras_counters[ADF_RAS_CORR]);

	/* Clear interrupt from ERRSOU0 */
	ADF_CSR_WR(csr, ADF_GEN4_HIAECORERRLOG_CPP0, aecorrerr);
}

static void adf_handle_cpp_aeunc(struct adf_accel_dev *accel_dev,
				 void __iomem *csr)
{
	u32 aeuncorerr = ADF_CSR_RD(csr, ADF_GEN4_HIAEUNCERRLOG_CPP0);

	aeuncorerr &= GET_HW_DATA(accel_dev)->get_ae_mask(accel_dev);

	dev_err(&GET_DEV(accel_dev),
		"Uncorrectable error detected in AE: 0x%x\n",
		aeuncorerr);

	atomic_inc(&accel_dev->ras_counters[ADF_RAS_UNCORR]);

	ADF_CSR_WR(csr, ADF_GEN4_HIAEUNCERRLOG_CPP0, aeuncorerr);
}

static void adf_handle_cppcmdparerr(struct adf_accel_dev *accel_dev,
				    void __iomem *csr,
				    bool *reset_required)
{
	struct adf_dev_err_mask *err_mask = &accel_dev->hw_device->dev_err_mask;

	u32 cmdparerr = ADF_CSR_RD(csr, ADF_GEN4_HICPPAGENTCMDPARERRLOG);

	cmdparerr &= err_mask->cppagentcmdpar_mask;

	dev_err(&GET_DEV(accel_dev),
		"HI CPP agent command parity error, reset required: 0x%x\n",
		cmdparerr);

	atomic_inc(&accel_dev->ras_counters[ADF_RAS_FATAL]);

	ADF_CSR_WR(csr, ADF_GEN4_HICPPAGENTCMDPARERRLOG, cmdparerr);

	*reset_required = true;
}

static void adf_handle_ri_mem_par_err(struct adf_accel_dev *accel_dev,
				      void __iomem *csr,
				      bool *reset_required)
{
	u32 rimem_parerr_sts = ADF_CSR_RD(csr, ADF_GEN4_RIMEM_PARERR_STS);

	rimem_parerr_sts &= ADF_GEN4_RIMEM_PARERR_STS_UNCERR_BITMASK |
			    ADF_GEN4_RIMEM_PARERR_STS_FATAL_BITMASK;

	dev_err(&GET_DEV(accel_dev),
		"RI Memory Parity Error: 0x%x\n", rimem_parerr_sts);

	if (rimem_parerr_sts & ADF_GEN4_RIMEM_PARERR_STS_UNCERR_BITMASK)
		atomic_inc(&accel_dev->ras_counters[ADF_RAS_UNCORR]);

	if (rimem_parerr_sts & ADF_GEN4_RIMEM_PARERR_STS_FATAL_BITMASK) {
		atomic_inc(&accel_dev->ras_counters[ADF_RAS_FATAL]);

		*reset_required = true;
	}

	ADF_CSR_WR(csr, ADF_GEN4_RIMEM_PARERR_STS, rimem_parerr_sts);
}

static void adf_handle_ti_ci_par_sts(struct adf_accel_dev *accel_dev,
				     void __iomem *csr)
{
	u32 ti_ci_par_sts = ADF_CSR_RD(csr, ADF_GEN4_TI_CI_PAR_STS);

	ti_ci_par_sts &= ADF_GEN4_TI_CI_PAR_STS_BITMASK;

	dev_err(&GET_DEV(accel_dev),
		"TI Memory Parity Error: 0x%x\n", ti_ci_par_sts);

	atomic_inc(&accel_dev->ras_counters[ADF_RAS_UNCORR]);

	ADF_CSR_WR(csr, ADF_GEN4_TI_CI_PAR_STS, ti_ci_par_sts);
}

static void adf_handle_ti_pullfub_par_sts(struct adf_accel_dev *accel_dev,
					  void __iomem *csr)
{
	u32 ti_pullfub_par_sts = ADF_CSR_RD(csr, ADF_GEN4_TI_PULL0FUB_PAR_STS);

	ti_pullfub_par_sts &= ADF_GEN4_TI_PULL0FUB_PAR_STS_BITMASK;

	dev_err(&GET_DEV(accel_dev),
		"TI Pull Parity Error: 0x%x\n", ti_pullfub_par_sts);

	atomic_inc(&accel_dev->ras_counters[ADF_RAS_UNCORR]);

	ADF_CSR_WR(csr, ADF_GEN4_TI_PULL0FUB_PAR_STS, ti_pullfub_par_sts);
}

static void adf_handle_ti_pushfub_par_sts(struct adf_accel_dev *accel_dev,
					  void __iomem *csr)
{
	u32 ti_pushfub_par_sts = ADF_CSR_RD(csr, ADF_GEN4_TI_PUSHFUB_PAR_STS);

	ti_pushfub_par_sts &= ADF_GEN4_TI_PUSHFUB_PAR_STS_BITMASK;

	dev_err(&GET_DEV(accel_dev),
		"TI Push Parity Error: 0x%x\n", ti_pushfub_par_sts);

	atomic_inc(&accel_dev->ras_counters[ADF_RAS_UNCORR]);

	ADF_CSR_WR(csr, ADF_GEN4_TI_PUSHFUB_PAR_STS, ti_pushfub_par_sts);
}

static void adf_handle_ti_cd_par_sts(struct adf_accel_dev *accel_dev,
				     void __iomem *csr)
{
	u32 ti_cd_par_sts = ADF_CSR_RD(csr, ADF_GEN4_TI_CD_PAR_STS);

	ti_cd_par_sts &= ADF_GEN4_TI_CD_PAR_STS_BITMASK;

	dev_err(&GET_DEV(accel_dev),
		"TI CD Parity Error: 0x%x\n", ti_cd_par_sts);

	atomic_inc(&accel_dev->ras_counters[ADF_RAS_UNCORR]);

	ADF_CSR_WR(csr, ADF_GEN4_TI_CD_PAR_STS, ti_cd_par_sts);
}

static void adf_handle_ti_trnsb_par_sts(struct adf_accel_dev *accel_dev,
					void __iomem *csr)
{
	u32 ti_trnsb_par_sts = ADF_CSR_RD(csr, ADF_GEN4_TI_TRNSB_PAR_STS);

	ti_trnsb_par_sts &= ADF_GEN4_TI_TRNSB_PAR_STS_BITMASK;

	dev_err(&GET_DEV(accel_dev),
		"TI TRNSB Parity Error: 0x%x\n", ti_trnsb_par_sts);

	atomic_inc(&accel_dev->ras_counters[ADF_RAS_UNCORR]);

	ADF_CSR_WR(csr, ADF_GEN4_TI_TRNSB_PAR_STS, ti_trnsb_par_sts);
}

static void adf_handle_iosfp_cmd_parerr(struct adf_accel_dev *accel_dev,
					void __iomem *csr,
					bool *reset_required)
{
	u32 rimiscsts = ADF_CSR_RD(csr, ADF_GEN4_RIMISCSTS);

	rimiscsts &= ADF_GEN4_RIMISCSTS_BIT;

	dev_err(&GET_DEV(accel_dev),
		"Command Parity error detected on IOSFP: 0x%x\n",
		rimiscsts);

	atomic_inc(&accel_dev->ras_counters[ADF_RAS_FATAL]);

	ADF_CSR_WR(csr, ADF_GEN4_RIMISCSTS, rimiscsts);

	*reset_required = true;
}

static void adf_gen4_process_errsou1(struct adf_accel_dev *accel_dev,
				     void __iomem *csr, u32 errsou,
				     bool *reset_required)
{
	if (errsou & ADF_GEN4_ERRSOU1_HIAEUNCERRLOG_CPP0_BIT)
		adf_handle_cpp_aeunc(accel_dev, csr);

	if (errsou & ADF_GEN4_ERRSOU1_HICPPAGENTCMDPARERRLOG_BIT)
		adf_handle_cppcmdparerr(accel_dev, csr, reset_required);

	if (errsou & ADF_GEN4_ERRSOU1_RIMEM_PARERR_STS_BIT)
		adf_handle_ri_mem_par_err(accel_dev, csr, reset_required);

	if (errsou & ADF_GEN4_ERRSOU1_TIMEM_PARERR_STS_BIT) {
		adf_handle_ti_ci_par_sts(accel_dev, csr);
		adf_handle_ti_pullfub_par_sts(accel_dev, csr);
		adf_handle_ti_pushfub_par_sts(accel_dev, csr);
		adf_handle_ti_cd_par_sts(accel_dev, csr);
		adf_handle_ti_trnsb_par_sts(accel_dev, csr);
	}

	if (errsou & ADF_GEN4_ERRSOU1_RIMISCSTS_BIT)
		adf_handle_iosfp_cmd_parerr(accel_dev, csr, reset_required);
}

static void adf_handle_uerrssmsh(struct adf_accel_dev *accel_dev,
				 void __iomem *csr)
{
	u32 reg = ADF_CSR_RD(csr, ADF_GEN4_UERRSSMSH);

	reg &= ADF_GEN4_UERRSSMSH_BITMASK;

	dev_err(&GET_DEV(accel_dev),
		"Uncorrectable error on ssm shared memory: 0x%x\n",
		reg);

	atomic_inc(&accel_dev->ras_counters[ADF_RAS_UNCORR]);

	ADF_CSR_WR(csr, ADF_GEN4_UERRSSMSH, reg);
}

static void adf_handle_cerrssmsh(struct adf_accel_dev *accel_dev,
				 void __iomem *csr)
{
	u32 reg = ADF_CSR_RD(csr, ADF_GEN4_CERRSSMSH);

	reg &= ADF_GEN4_CERRSSMSH_ERROR_BIT;

	dev_warn(&GET_DEV(accel_dev),
		 "Correctable error on ssm shared memory: 0x%x\n",
		 reg);

	atomic_inc(&accel_dev->ras_counters[ADF_RAS_CORR]);

	ADF_CSR_WR(csr, ADF_GEN4_CERRSSMSH, reg);
}

static void adf_handle_pperr_err(struct adf_accel_dev *accel_dev,
				 void __iomem *csr)
{
	u32 reg = ADF_CSR_RD(csr, ADF_GEN4_PPERR);

	reg &= ADF_GEN4_PPERR_BITMASK;

	dev_err(&GET_DEV(accel_dev),
		"Uncorrectable error CPP transaction on memory target: 0x%x\n",
		reg);

	atomic_inc(&accel_dev->ras_counters[ADF_RAS_UNCORR]);

	ADF_CSR_WR(csr, ADF_GEN4_PPERR, reg);
}

static void adf_poll_slicehang_csr(struct adf_accel_dev *accel_dev,
				   void __iomem *csr, u32 slice_hang_offset,
				   char *slice_name)
{
	u32 slice_hang_reg = ADF_CSR_RD(csr, slice_hang_offset);

	if (!slice_hang_reg)
		return;

	dev_err(&GET_DEV(accel_dev),
		"Slice %s hang error encountered\n", slice_name);

	atomic_inc(&accel_dev->ras_counters[ADF_RAS_UNCORR]);
}

static void adf_gen4_handle_slice_hang_error(struct adf_accel_dev *accel_dev,
					     void __iomem *csr)
{
	adf_poll_slicehang_csr(accel_dev, csr,
			       ADF_GEN4_SLICEHANGSTATUS_ATH_CPH, "ath_cph");
	adf_poll_slicehang_csr(accel_dev, csr,
			       ADF_GEN4_SLICEHANGSTATUS_CPR_XLT, "cpr_xlt");
	adf_poll_slicehang_csr(accel_dev, csr,
			       ADF_GEN4_SLICEHANGSTATUS_DCPR_UCS, "dcpr_ucs");
	adf_poll_slicehang_csr(accel_dev, csr,
			       ADF_GEN4_SLICEHANGSTATUS_PKE, "pke");

	if (GET_HW_DATA(accel_dev)->dev_err_mask.parerr_wat_wcp_mask)
		adf_poll_slicehang_csr(accel_dev, csr,
				       ADF_GEN4_SLICEHANGSTATUS_WAT_WCP,
				       "wat_wcp");
}

static void adf_handle_spp_pullcmd_err(struct adf_accel_dev *accel_dev,
				       void __iomem *csr,
				       bool *reset_required)
{
	u32 reg;
	struct adf_dev_err_mask *err_mask =
		&accel_dev->hw_device->dev_err_mask;

	reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPULLCMDPARERR_ATH_CPH);
	reg &= err_mask->parerr_ath_cph_mask;

	if (reg) {
		dev_err(&GET_DEV(accel_dev),
			"SPP pull command fatal error ATH_CPH: 0x%x\n",
			reg);

		atomic_inc(&accel_dev->ras_counters[ADF_RAS_FATAL]);

		ADF_CSR_WR(csr, ADF_GEN4_SPPPULLCMDPARERR_ATH_CPH, reg);

		*reset_required = true;
	}

	reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPULLCMDPARERR_CPR_XLT);
	reg &= err_mask->parerr_cpr_xlt_mask;

	if (reg) {
		dev_err(&GET_DEV(accel_dev),
			"SPP pull command fatal error CPR_XLT: 0x%x\n",
			reg);

		atomic_inc(&accel_dev->ras_counters[ADF_RAS_FATAL]);

		ADF_CSR_WR(csr, ADF_GEN4_SPPPULLCMDPARERR_CPR_XLT, reg);

		*reset_required = true;
	}

	reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPULLCMDPARERR_DCPR_UCS);
	reg &= err_mask->parerr_dcpr_ucs_mask;

	if (reg) {
		dev_err(&GET_DEV(accel_dev),
			"SPP pull command fatal error DCPR_UCS: 0x%x\n",
			reg);

		atomic_inc(&accel_dev->ras_counters[ADF_RAS_FATAL]);

		ADF_CSR_WR(csr, ADF_GEN4_SPPPULLCMDPARERR_DCPR_UCS, reg);

		*reset_required = true;
	}

	reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPULLCMDPARERR_PKE);
	reg &= err_mask->parerr_pke_mask;

	if (reg) {
		dev_err(&GET_DEV(accel_dev),
			"SPP pull command fatal error PKE: 0x%x\n",
			reg);

		atomic_inc(&accel_dev->ras_counters[ADF_RAS_FATAL]);

		ADF_CSR_WR(csr, ADF_GEN4_SPPPULLCMDPARERR_PKE, reg);

		*reset_required = true;
	}

	if (err_mask->parerr_wat_wcp_mask) {
		reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPULLCMDPARERR_WAT_WCP);
		reg &= err_mask->parerr_wat_wcp_mask;

		if (reg) {
			dev_err(&GET_DEV(accel_dev),
				"SPP pull command fatal error WAT_WCP: 0x%x\n",
				reg);

			atomic_inc(&accel_dev->ras_counters
							[ADF_RAS_FATAL]);

			ADF_CSR_WR(csr, ADF_GEN4_SPPPULLCMDPARERR_WAT_WCP,
				   reg);

			*reset_required = true;
		}
	}
}

static void adf_handle_spp_pulldata_err(struct adf_accel_dev *accel_dev,
					void __iomem *csr)
{
	u32 reg;
	struct adf_dev_err_mask *err_mask =
		&accel_dev->hw_device->dev_err_mask;

	reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPULLDATAPARERR_ATH_CPH);
	reg &= err_mask->parerr_ath_cph_mask;

	if (reg) {
		dev_err(&GET_DEV(accel_dev),
			"SPP pull data err ATH_CPH: 0x%x\n",
			reg);

		atomic_inc(&accel_dev->ras_counters[ADF_RAS_UNCORR]);

		ADF_CSR_WR(csr, ADF_GEN4_SPPPULLDATAPARERR_ATH_CPH, reg);
	}

	reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPULLDATAPARERR_CPR_XLT);
	reg &= err_mask->parerr_cpr_xlt_mask;

	if (reg) {
		dev_err(&GET_DEV(accel_dev),
			"SPP pull data err CPR_XLT: 0x%x\n",
			reg);

		atomic_inc(&accel_dev->ras_counters[ADF_RAS_UNCORR]);

		ADF_CSR_WR(csr, ADF_GEN4_SPPPULLDATAPARERR_CPR_XLT, reg);
	}

	reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPULLDATAPARERR_DCPR_UCS);
	reg &= err_mask->parerr_dcpr_ucs_mask;

	if (reg) {
		dev_err(&GET_DEV(accel_dev),
			"SPP pull data err DCPR_UCS: 0x%x\n",
			reg);

		atomic_inc(&accel_dev->ras_counters[ADF_RAS_UNCORR]);

		ADF_CSR_WR(csr, ADF_GEN4_SPPPULLDATAPARERR_DCPR_UCS, reg);
	}

	reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPULLDATAPARERR_PKE);
	reg &= err_mask->parerr_pke_mask;

	if (reg) {
		dev_err(&GET_DEV(accel_dev),
			"SPP pull data err PKE: 0x%x\n",
			reg);

		atomic_inc(&accel_dev->ras_counters[ADF_RAS_UNCORR]);

		ADF_CSR_WR(csr, ADF_GEN4_SPPPULLDATAPARERR_PKE, reg);
	}

	if (err_mask->parerr_wat_wcp_mask) {
		reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPULLDATAPARERR_WAT_WCP);
		reg &= err_mask->parerr_wat_wcp_mask;

		if (reg) {
			dev_err(&GET_DEV(accel_dev),
				"SPP pull data err WAT_WCP: 0x%x\n",
				reg);

			atomic_inc(&accel_dev->ras_counters
							[ADF_RAS_UNCORR]);

			ADF_CSR_WR(csr, ADF_GEN4_SPPPULLDATAPARERR_WAT_WCP,
				   reg);
		}
	}
}

static void adf_handle_spp_pushcmd_err(struct adf_accel_dev *accel_dev,
				       void __iomem *csr,
				       bool *reset_required)
{
	u32 reg;
	struct adf_dev_err_mask *err_mask =
		&accel_dev->hw_device->dev_err_mask;

	reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPUSHCMDPARERR_ATH_CPH);
	reg &= err_mask->parerr_ath_cph_mask;

	if (reg) {
		dev_err(&GET_DEV(accel_dev),
			"SPP push command fatal error ATH_CPH: 0x%x\n",
			reg);

		atomic_inc(&accel_dev->ras_counters[ADF_RAS_FATAL]);

		ADF_CSR_WR(csr, ADF_GEN4_SPPPUSHCMDPARERR_ATH_CPH, reg);

		*reset_required = true;
	}

	reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPUSHCMDPARERR_CPR_XLT);
	reg &= err_mask->parerr_cpr_xlt_mask;

	if (reg) {
		dev_err(&GET_DEV(accel_dev),
			"SPP push command fatal error CPR_XLT: 0x%x\n",
			reg);

		atomic_inc(&accel_dev->ras_counters[ADF_RAS_FATAL]);

		ADF_CSR_WR(csr, ADF_GEN4_SPPPUSHCMDPARERR_CPR_XLT, reg);

		*reset_required = true;
	}

	reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPUSHCMDPARERR_DCPR_UCS);
	reg &= err_mask->parerr_dcpr_ucs_mask;

	if (reg) {
		dev_err(&GET_DEV(accel_dev),
			"SPP push command fatal error DCPR_UCS: 0x%x\n",
			reg);

		atomic_inc(&accel_dev->ras_counters[ADF_RAS_FATAL]);

		ADF_CSR_WR(csr, ADF_GEN4_SPPPUSHCMDPARERR_DCPR_UCS, reg);

		*reset_required = true;
	}

	reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPUSHCMDPARERR_PKE);
	reg &= err_mask->parerr_pke_mask;

	if (reg) {
		dev_err(&GET_DEV(accel_dev),
			"SPP push command fatal error PKE: 0x%x\n",
			reg);

		atomic_inc(&accel_dev->ras_counters[ADF_RAS_FATAL]);

		ADF_CSR_WR(csr, ADF_GEN4_SPPPUSHCMDPARERR_PKE, reg);

		*reset_required = true;
	}

	if (err_mask->parerr_wat_wcp_mask) {
		reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPUSHCMDPARERR_WAT_WCP);
		reg &= err_mask->parerr_wat_wcp_mask;

		if (reg) {
			dev_err(&GET_DEV(accel_dev),
				"SPP push command fatal error WAT_WCP: 0x%x\n",
				reg);

			atomic_inc(&accel_dev->ras_counters
							[ADF_RAS_FATAL]);

			ADF_CSR_WR(csr, ADF_GEN4_SPPPUSHCMDPARERR_WAT_WCP,
				   reg);

			*reset_required = true;
		}
	}
}

static void adf_handle_spp_pushdata_err(struct adf_accel_dev *accel_dev,
					void __iomem *csr)
{
	u32 reg;
	struct adf_dev_err_mask *err_mask =
		&accel_dev->hw_device->dev_err_mask;

	reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPUSHDATAPARERR_ATH_CPH);
	reg &= err_mask->parerr_ath_cph_mask;

	if (reg) {
		dev_err(&GET_DEV(accel_dev),
			"SPP push data err ATH_CPH: 0x%x\n",
			reg);

		atomic_inc(&accel_dev->ras_counters[ADF_RAS_UNCORR]);

		ADF_CSR_WR(csr, ADF_GEN4_SPPPUSHDATAPARERR_ATH_CPH, reg);
	}

	reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPUSHDATAPARERR_CPR_XLT);
	reg &= err_mask->parerr_cpr_xlt_mask;

	if (reg) {
		dev_err(&GET_DEV(accel_dev),
			"SPP push data err CPR_XLT: 0x%x\n",
			reg);

		atomic_inc(&accel_dev->ras_counters[ADF_RAS_UNCORR]);

		ADF_CSR_WR(csr, ADF_GEN4_SPPPUSHDATAPARERR_CPR_XLT, reg);
	}

	reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPUSHDATAPARERR_DCPR_UCS);
	reg &= err_mask->parerr_dcpr_ucs_mask;

	if (reg) {
		dev_err(&GET_DEV(accel_dev),
			"SPP push data err DCPR_UCS: 0x%x\n",
			reg);

		atomic_inc(&accel_dev->ras_counters[ADF_RAS_UNCORR]);

		ADF_CSR_WR(csr, ADF_GEN4_SPPPUSHDATAPARERR_DCPR_UCS, reg);
	}

	reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPUSHDATAPARERR_PKE);
	reg &= err_mask->parerr_pke_mask;

	if (reg) {
		dev_err(&GET_DEV(accel_dev),
			"SPP push data err PKE: 0x%x\n",
			reg);

		atomic_inc(&accel_dev->ras_counters[ADF_RAS_UNCORR]);

		ADF_CSR_WR(csr, ADF_GEN4_SPPPUSHDATAPARERR_PKE, reg);
	}

	if (err_mask->parerr_wat_wcp_mask) {
		reg = ADF_CSR_RD(csr, ADF_GEN4_SPPPUSHDATAPARERR_WAT_WCP);
		reg &= err_mask->parerr_wat_wcp_mask;

		if (reg) {
			dev_err(&GET_DEV(accel_dev),
				"SPP push data err WAT_WCP: 0x%x\n",
				reg);

			atomic_inc(&accel_dev->ras_counters
							[ADF_RAS_UNCORR]);

			ADF_CSR_WR(csr, ADF_GEN4_SPPPUSHDATAPARERR_WAT_WCP,
				   reg);
		}
	}
}

static void adf_handle_spppar_err(struct adf_accel_dev *accel_dev,
				  void __iomem *csr,
				  bool *reset_required)
{
	adf_handle_spp_pullcmd_err(accel_dev, csr, reset_required);
	adf_handle_spp_pulldata_err(accel_dev, csr);
	adf_handle_spp_pushcmd_err(accel_dev, csr, reset_required);
	adf_handle_spp_pushdata_err(accel_dev, csr);
}

static void adf_handle_ssmcpppar_err(struct adf_accel_dev *accel_dev,
				     void __iomem *csr,
				     bool *reset_required)
{
	u32 bit_iterator;
	unsigned long errs_bits;
	u32 reg = ADF_CSR_RD(csr, ADF_GEN4_SSMCPPERR);
	u32 bits_num = sizeof(reg) * BITS_PER_BYTE;

	reg &= ADF_GEN4_SSMCPPERR_FATAL_BITMASK |
	       ADF_GEN4_SSMCPPERR_UNCERR_BITMASK;

	if (reg & ADF_GEN4_SSMCPPERR_FATAL_BITMASK) {
		dev_err(&GET_DEV(accel_dev),
			"Fatal SSM CPP parity error: 0x%x\n", reg);

		errs_bits = reg & ADF_GEN4_SSMCPPERR_FATAL_BITMASK;
		for_each_set_bit(bit_iterator, &errs_bits, bits_num) {
			atomic_inc(&accel_dev->ras_counters[ADF_RAS_FATAL]);
		}
		*reset_required = true;
	}

	if (reg & ADF_GEN4_SSMCPPERR_UNCERR_BITMASK) {
		dev_err(&GET_DEV(accel_dev),
			"non-Fatal SSM CPP parity error: 0x%x\n", reg);

		errs_bits = reg & ADF_GEN4_SSMCPPERR_UNCERR_BITMASK;
		for_each_set_bit(bit_iterator, &errs_bits, bits_num) {
			atomic_inc(&accel_dev->ras_counters[ADF_RAS_UNCORR]);
		}
	}

	ADF_CSR_WR(csr, ADF_GEN4_SSMCPPERR, reg);
}

static void adf_handle_rf_parr_err(struct adf_accel_dev *accel_dev,
				   void __iomem *csr)
{
	atomic_inc(&accel_dev->ras_counters[ADF_RAS_UNCORR]);

	dev_err(&GET_DEV(accel_dev), "Slice ssm soft parity error reported");
}

static void adf_handle_ser_err_ssmsh(struct adf_accel_dev *accel_dev,
				     void __iomem *csr,
				     bool *reset_required)
{
	u32 bit_iterator;
	unsigned long errs_bits;
	u32 reg = ADF_CSR_RD(csr, ADF_GEN4_SER_ERR_SSMSH);
	u32 bits_num = sizeof(reg) * BITS_PER_BYTE;

	reg &= ADF_GEN4_SER_ERR_SSMSH_FATAL_BITMASK |
	       ADF_GEN4_SER_ERR_SSMSH_UNCERR_BITMASK |
	       ADF_GEN4_SER_ERR_SSMSH_CERR_BITMASK;

	if (reg & ADF_GEN4_SER_ERR_SSMSH_FATAL_BITMASK) {
		dev_err(&GET_DEV(accel_dev),
			"Fatal SER_SSMSH_ERR, reset required: 0x%x\n", reg);

		errs_bits = reg & ADF_GEN4_SER_ERR_SSMSH_FATAL_BITMASK;
		for_each_set_bit(bit_iterator, &errs_bits, bits_num) {
			atomic_inc(&accel_dev->ras_counters[ADF_RAS_FATAL]);
		}
		*reset_required = true;
	}

	if (reg & ADF_GEN4_SER_ERR_SSMSH_UNCERR_BITMASK) {
		dev_err(&GET_DEV(accel_dev),
			"non-fatal SER_SSMSH_ERR: 0x%x\n", reg);

		errs_bits = reg & ADF_GEN4_SER_ERR_SSMSH_UNCERR_BITMASK;
		for_each_set_bit(bit_iterator, &errs_bits, bits_num) {
			atomic_inc(&accel_dev->ras_counters[ADF_RAS_UNCORR]);
		}
	}

	if (reg & ADF_GEN4_SER_ERR_SSMSH_CERR_BITMASK) {
		dev_warn(&GET_DEV(accel_dev),
			 "Correctable SER_SSMSH_ERR: 0x%x\n", reg);

		errs_bits = reg & ADF_GEN4_SER_ERR_SSMSH_CERR_BITMASK;
		for_each_set_bit(bit_iterator, &errs_bits, bits_num) {
			atomic_inc(&accel_dev->ras_counters[ADF_RAS_CORR]);
		}
	}

	ADF_CSR_WR(csr, ADF_GEN4_SER_ERR_SSMSH, reg);
}

static void adf_handle_iaintstatssm(struct adf_accel_dev *accel_dev,
				    void __iomem *csr,
				    bool *reset_required)
{
	u32 iastatssm = ADF_CSR_RD(csr, ADF_GEN4_IAINTSTATSSM);

	iastatssm &= ADF_GEN4_IAINTSTATSSM_BITMASK;

	if (!iastatssm)
		return;

	if (iastatssm & ADF_GEN4_IAINTSTATSSM_UERRSSMSH_BIT)
		adf_handle_uerrssmsh(accel_dev, csr);

	if (iastatssm & ADF_GEN4_IAINTSTATSSM_CERRSSMSH_BIT)
		adf_handle_cerrssmsh(accel_dev, csr);

	if (iastatssm & ADF_GEN4_IAINTSTATSSM_PPERR_BIT)
		adf_handle_pperr_err(accel_dev, csr);

	if (iastatssm & ADF_GEN4_IAINTSTATSSM_SLICEHANG_ERR_BIT)
		adf_gen4_handle_slice_hang_error(accel_dev, csr);

	if (iastatssm & ADF_GEN4_IAINTSTATSSM_SPPPARERR_BIT)
		adf_handle_spppar_err(accel_dev, csr, reset_required);

	if (iastatssm & ADF_GEN4_IAINTSTATSSM_SSMCPPERR_BIT)
		adf_handle_ssmcpppar_err(accel_dev, csr, reset_required);

	if (iastatssm & ADF_GEN4_IAINTSTATSSM_SSMSOFTERRORPARITY_BIT)
		adf_handle_rf_parr_err(accel_dev, csr);

	if (iastatssm & (ADF_GEN4_IAINTSTATSSM_SER_ERR_SSMSH_CERR_BIT |
			 ADF_GEN4_IAINTSTATSSM_SER_ERR_SSMSH_UNCERR_BIT))
		adf_handle_ser_err_ssmsh(accel_dev, csr, reset_required);

	ADF_CSR_WR(csr, ADF_GEN4_IAINTSTATSSM, iastatssm);
}

static void adf_handle_cpp_cfc_err(struct adf_accel_dev *accel_dev,
				   void __iomem *csr,
				   bool *reset_required)
{
	u32 reg = ADF_CSR_RD(csr, ADF_GEN4_CPP_CFC_ERR_STATUS);

	if (reg & ADF_GEN4_CPP_CFC_ERR_STATUS_DATAPAR_BIT) {
		dev_err(&GET_DEV(accel_dev),
			"CPP_CFC_ERR: data parity: 0x%x", reg);
		atomic_inc(&accel_dev->ras_counters[ADF_RAS_UNCORR]);
	}

	if (reg & ADF_GEN4_CPP_CFC_ERR_STATUS_CMDPAR_BIT) {
		dev_err(&GET_DEV(accel_dev),
			"CPP_CFC_ERR: command parity, reset required: 0x%x",
			reg);
		atomic_inc(&accel_dev->ras_counters[ADF_RAS_FATAL]);

		*reset_required = true;
	}

	if (reg & ADF_GEN4_CPP_CFC_ERR_STATUS_MERR_BIT) {
		dev_err(&GET_DEV(accel_dev),
			"CPP_CFC_ERR: multiple errors, reset required: 0x%x",
			reg);
		atomic_inc(&accel_dev->ras_counters[ADF_RAS_FATAL]);

		*reset_required = true;
	}

	ADF_CSR_WR(csr, ADF_GEN4_CPP_CFC_ERR_STATUS_CLR,
		   ADF_GEN4_CPP_CFC_ERR_STATUS_CLR_BITMASK);
}

static void adf_gen4_process_errsou2(struct adf_accel_dev *accel_dev,
				     void __iomem *csr, u32 errsou,
				     bool *reset_required)
{
	if (errsou & ADF_GEN4_ERRSOU2_SSM_ERR_BIT)
		adf_handle_iaintstatssm(accel_dev, csr, reset_required);

	if (errsou & ADF_GEN4_ERRSOU2_CPP_CFC_ERR_STATUS_BIT)
		adf_handle_cpp_cfc_err(accel_dev, csr, reset_required);
}

static void adf_handle_timiscsts(struct adf_accel_dev *accel_dev,
				 void __iomem *csr,
				 bool *reset_required)
{
	u32 timiscsts = ADF_CSR_RD(csr, ADF_GEN4_TIMISCSTS);

	dev_err(&GET_DEV(accel_dev),
		"Fatal error in Transmit Interface: 0x%x\n", timiscsts);

	atomic_inc(&accel_dev->ras_counters[ADF_RAS_FATAL]);

	*reset_required = true;
}

static void adf_handle_ricppintsts(struct adf_accel_dev *accel_dev,
				   void __iomem *csr)
{
	u32 ricppintsts = ADF_CSR_RD(csr, ADF_GEN4_RICPPINTSTS);

	ricppintsts &= ADF_GEN4_RICPPINTSTS_BITMASK;

	dev_err(&GET_DEV(accel_dev),
		"RI CPP Uncorrectable Error: 0x%x\n", ricppintsts);

	atomic_inc(&accel_dev->ras_counters[ADF_RAS_UNCORR]);

	ADF_CSR_WR(csr, ADF_GEN4_RICPPINTSTS, ricppintsts);
}

static void adf_handle_ticppintsts(struct adf_accel_dev *accel_dev,
				   void __iomem *csr)
{
	u32 ticppintsts = ADF_CSR_RD(csr, ADF_GEN4_TICPPINTSTS);

	ticppintsts &= ADF_GEN4_TICPPINTSTS_BITMASK;

	dev_err(&GET_DEV(accel_dev),
		"TI CPP Uncorrectable Error: 0x%x\n", ticppintsts);

	atomic_inc(&accel_dev->ras_counters[ADF_RAS_UNCORR]);

	ADF_CSR_WR(csr, ADF_GEN4_TICPPINTSTS, ticppintsts);
}

static void adf_handle_aramcerr(struct adf_accel_dev *accel_dev,
				void __iomem *csr)
{
	u32 aram_cerr = ADF_CSR_RD(csr, ADF_GEN4_REG_ARAMCERR);

	aram_cerr &= ADF_GEN4_REG_ARAMCERR_BIT;

	dev_warn(&GET_DEV(accel_dev),
		 "ARAM correctable error : 0x%x\n", aram_cerr);

	atomic_inc(&accel_dev->ras_counters[ADF_RAS_CORR]);

	aram_cerr |= ADF_GEN4_REG_ARAMCERR_EN_BITMASK;

	ADF_CSR_WR(csr, ADF_GEN4_REG_ARAMCERR, aram_cerr);
}

static void adf_handle_aramuerr(struct adf_accel_dev *accel_dev,
				void __iomem *csr,
				bool *reset_required)
{
	u32 aramuerr = ADF_CSR_RD(csr, ADF_GEN4_REG_ARAMUERR);

	aramuerr &= ADF_GEN4_REG_ARAMUERR_ERROR_BIT |
		    ADF_GEN4_REG_ARAMUERR_MULTI_ERRORS_BIT;

	if (!aramuerr)
		return;

	if (aramuerr & ADF_GEN4_REG_ARAMUERR_MULTI_ERRORS_BIT) {
		dev_err(&GET_DEV(accel_dev),
			"ARAM multiple uncorrectable errors reset required: 0x%x\n",
			aramuerr);

		atomic_inc(&accel_dev->ras_counters[ADF_RAS_FATAL]);

		*reset_required = true;
	} else {
		dev_err(&GET_DEV(accel_dev),
			"ARAM uncorrectable error: 0x%x\n", aramuerr);

		atomic_inc(&accel_dev->ras_counters[ADF_RAS_UNCORR]);
	}

	aramuerr |= ADF_GEN4_REG_ARAMUERR_EN_BITMASK;

	ADF_CSR_WR(csr, ADF_GEN4_REG_ARAMUERR, aramuerr);
}

static void adf_handle_reg_cppmemtgterr(struct adf_accel_dev *accel_dev,
					void __iomem *csr,
					bool *reset_required)
{
	u32 cppmemtgterr = ADF_CSR_RD(csr, ADF_GEN4_REG_CPPMEMTGTERR);

	cppmemtgterr &= ADF_GEN4_REG_CPPMEMTGTERR_BITMASK |
			ADF_GEN4_REG_CPPMEMTGTERR_MULTI_ERRORS_BIT;

	if (!cppmemtgterr)
		return;

	if (cppmemtgterr & ADF_GEN4_REG_CPPMEMTGTERR_MULTI_ERRORS_BIT) {
		dev_err(&GET_DEV(accel_dev),
			"Misc memory target multiple uncorrectable errors reset required: 0x%x\n",
			cppmemtgterr);

		atomic_inc(&accel_dev->ras_counters[ADF_RAS_FATAL]);

		*reset_required = true;
	} else {
		dev_err(&GET_DEV(accel_dev),
			"Misc memory target uncorrectable error: 0x%x\n",
			cppmemtgterr);

		atomic_inc(&accel_dev->ras_counters[ADF_RAS_UNCORR]);
	}

	cppmemtgterr |= ADF_GEN4_REG_CPPMEMTGTERR_EN_BITMASK;

	ADF_CSR_WR(csr, ADF_GEN4_REG_CPPMEMTGTERR, cppmemtgterr);
}

static void adf_handle_atufaultstatus(struct adf_accel_dev *accel_dev,
				      void __iomem *csr)
{
	u32 i;
	u32 max_rp_num = GET_HW_DATA(accel_dev)->num_banks;

	for (i = 0; i < max_rp_num; i++) {
		u32 atufaultstatus = ADF_CSR_RD(csr,
						ADF_GEN4_ATUFAULTSTATUS(i));

		atufaultstatus &= ADF_GEN4_ATUFAULTSTATUS_BIT;

		if (atufaultstatus) {
			dev_err(&GET_DEV(accel_dev),
				"Ring Pair (%u) ATU detected fault: 0x%x\n", i,
				atufaultstatus);

			atomic_inc(&accel_dev->ras_counters[ADF_RAS_UNCORR]);

			ADF_CSR_WR(csr, ADF_GEN4_ATUFAULTSTATUS(i),
				   atufaultstatus);
		}
	}
}

static void adf_gen4_process_errsou3(struct adf_accel_dev *accel_dev,
				     void __iomem *csr,
				     void __iomem *aram_csr,
				     u32 errsou,
				     bool *reset_required)
{
	if (errsou & ADF_GEN4_ERRSOU3_TIMISCSTS_BIT)
		adf_handle_timiscsts(accel_dev, csr, reset_required);

	if (errsou & ADF_GEN4_ERRSOU3_RICPPINTSTS_BITMASK)
		adf_handle_ricppintsts(accel_dev, csr);

	if (errsou & ADF_GEN4_ERRSOU3_TICPPINTSTS_BITMASK)
		adf_handle_ticppintsts(accel_dev, csr);

	if (errsou & ADF_GEN4_ERRSOU3_REG_ARAMCERR_BIT)
		adf_handle_aramcerr(accel_dev, aram_csr);

	if (errsou & ADF_GEN4_ERRSOU3_REG_ARAMUERR_BIT) {
		adf_handle_aramuerr(accel_dev, aram_csr,
				    reset_required);

		adf_handle_reg_cppmemtgterr(accel_dev, aram_csr,
					    reset_required);
	}

	if (errsou & ADF_GEN4_ERRSOU3_ATUFAULTSTATUS_BIT)
		adf_handle_atufaultstatus(accel_dev, csr);
}

bool adf_gen4_ras_interrupts(struct adf_accel_dev *accel_dev,
			     bool *reset_required)
{
	bool handled = false;
	void __iomem *csr = adf_get_pmisc_base(accel_dev);
	void __iomem *aram_csr = adf_get_param_base(accel_dev);
	u32 errsou = ADF_CSR_RD(csr, ADF_GEN4_ERRSOU0);

	*reset_required = false;

	if (errsou & ADF_GEN4_ERRSOU0_BIT) {
		adf_gen4_process_errsou0(accel_dev, csr);
		handled = true;
	}

	errsou = ADF_CSR_RD(csr, ADF_GEN4_ERRSOU1);
	if (errsou & ADF_GEN4_ERRSOU1_BITMASK) {
		adf_gen4_process_errsou1(accel_dev, csr, errsou,
					 reset_required);
		handled = true;
	}

	errsou = ADF_CSR_RD(csr, ADF_GEN4_ERRSOU2);
	if (errsou & ADF_GEN4_ERRSOU2_BITMASK) {
		adf_gen4_process_errsou2(accel_dev, csr, errsou,
					 reset_required);

		handled = true;
	}

	errsou = ADF_CSR_RD(csr, ADF_GEN4_ERRSOU3);
	if (errsou & ADF_GEN4_ERRSOU3_BITMASK) {
		adf_gen4_process_errsou3(accel_dev, csr, aram_csr,
					 errsou,
					 reset_required);

		handled = true;
	}

	return handled;
}
EXPORT_SYMBOL_GPL(adf_gen4_ras_interrupts);

void adf_gen4_print_err_registers(struct adf_accel_dev *accel_dev)
{
	size_t i;
	u32 val;

	void __iomem *csr = adf_get_pmisc_base(accel_dev);

	for (i = 0; i < ARRAY_SIZE(adf_gen4_err_regs); ++i) {
		val = ADF_CSR_RD(csr, adf_gen4_err_regs[i].offs);

		adf_print_reg(accel_dev, adf_gen4_err_regs[i].name, 0, val);
	}

	if (GET_HW_DATA(accel_dev)->dev_err_mask.parerr_wat_wcp_mask) {
		for (i = 0; i < ARRAY_SIZE(adf_gen4_special_err_regs); ++i) {
			val = ADF_CSR_RD(csr,
					 adf_gen4_special_err_regs[i].offs);

			adf_print_reg(accel_dev,
				      adf_gen4_special_err_regs[i].name, 0,
				      val);
		}
	}

	adf_print_flush(accel_dev);
}
EXPORT_SYMBOL_GPL(adf_gen4_print_err_registers);
