/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2021 Intel Corporation */
#ifndef ADF_CFG_STRINGS_H_
#define ADF_CFG_STRINGS_H_

#define ADF_GENERAL_SEC "GENERAL"
#define ADF_KERNEL_SEC "KERNEL"
#define ADF_ACCEL_SEC "Accelerator"
#define ADF_SIOV_SEC "SIOV"
#define ADF_NUM_CY "NumberCyInstances"
#define ADF_NUM_DC "NumberDcInstances"
#define ADF_NUM_ADI "NumberAdis"
#define ADF_UQ_ENABLED "UQEnabled"
#define ADF_RING_SYM_SIZE "NumConcurrentSymRequests"
#define ADF_RING_ASYM_SIZE "NumConcurrentAsymRequests"
#define ADF_RING_DC_SIZE "NumConcurrentRequests"
#define ADF_RING_ASYM_TX "RingAsymTx"
#define ADF_RING_SYM_TX "RingSymTx"
#define ADF_RING_ASYM_RX "RingAsymRx"
#define ADF_RING_SYM_RX "RingSymRx"
#define ADF_RING_DC_TX "RingTx"
#define ADF_RING_DC_RX "RingRx"
#define ADF_ETRMGR_BANK "Bank"
#define ADF_RING_BANK_NUM "BankNumber"
#define ADF_RING_BANK_NUM_ASYM "BankNumberAsym"
#define ADF_RING_BANK_NUM_SYM "BankNumberSym"
#define ADF_CY "Cy"
#define ADF_DC "Dc"
#define ADF_CY_ASYM_BANK_NUM_FORMAT \
	ADF_CY "%d" ADF_RING_BANK_NUM_ASYM
#define ADF_CY_SYM_BANK_NUM_FORMAT \
	ADF_CY "%d" ADF_RING_BANK_NUM_SYM
#define ADF_CY_BANK_NUM_FORMAT \
	ADF_CY "%d" ADF_RING_BANK_NUM
#define ADF_ADI "Adi"
#define ADF_ADI_TYPE_FORMAT \
	ADF_ADI "%dType"
#define ADF_ADI_RING_TX_FORMAT \
	ADF_ADI "%dRingTx"
#define ADF_ADI_RING_RX_FORMAT \
	ADF_ADI "%dRingRx"
#define ADF_ETRMGR_COALESCING_ENABLED "InterruptCoalescingEnabled"
#define ADF_ETRMGR_COALESCING_ENABLED_FORMAT \
	ADF_ETRMGR_BANK "%d" ADF_ETRMGR_COALESCING_ENABLED
#define ADF_ETRMGR_COALESCE_TIMER "InterruptCoalescingTimerNs"
#define ADF_ETRMGR_COALESCE_TIMER_FORMAT \
	ADF_ETRMGR_BANK "%d" ADF_ETRMGR_COALESCE_TIMER
#define ADF_ETRMGR_COALESCING_MSG_ENABLED "InterruptCoalescingNumResponses"
#define ADF_ETRMGR_COALESCING_MSG_ENABLED_FORMAT \
	ADF_ETRMGR_BANK "%d" ADF_ETRMGR_COALESCING_MSG_ENABLED
#define ADF_ETRMGR_CORE_AFFINITY "CoreAffinity"
#define ADF_ETRMGR_CORE_AFFINITY_FORMAT \
	ADF_ETRMGR_BANK "%d" ADF_ETRMGR_CORE_AFFINITY
#define ADF_ACCEL_STR "Accelerator%d"
#define ADF_INLINE_SEC "INLINE"
#define ADF_NUM_CY_ACCEL_UNITS "NumCyAccelUnits"
#define ADF_NUM_DC_ACCEL_UNITS "NumDcAccelUnits"
#define ADF_NUM_INLINE_ACCEL_UNITS "NumInlineAccelUnits"
#define ADF_NUM_ASYM_ACCEL_UNITS "NumAsymAccelUnits"
#define ADF_NUM_SYM_ACCEL_UNITS "NumSymAccelUnits"
#define ADF_INLINE_INGRESS "InlineIngress"
#define ADF_INLINE_EGRESS "InlineEgress"
#define ADF_INLINE_CONGEST_MNGT_PROFILE "InlineCongestionManagmentProfile"
#define ADF_INLINE_IPSEC_ALGO_GROUP "InlineIPsecAlgoGroup"
#define ADF_SERVICE_CY "cy"
#define ADF_SERVICE_SYM "sym"
#define ADF_SERVICE_DC "dc"
#define ADF_SERVICE_AUX "aux"
#define ADF_CFG_CY "cy"
#define ADF_CFG_DC "dc"
#define ADF_CFG_ASYM "asym"
#define ADF_CFG_SYM "sym"
#define ADF_CFG_AUX "aux"
#define ADF_SERVICE_INLINE "inline"
#define ADF_SERVICES_ENABLED "ServicesEnabled"
#define ADF_SERVICES_SEPARATOR ";"
#define ADF_AUX_FW "AuxFw"
#define AUX_SERVICE_OBJ "AuxServiceObj"
#define AUX_ADMIN_OBJ "AuxAdminObj"

#define ADF_DEV_SSM_WDT_BULK "CySymAndDcWatchDogTimer"
#define ADF_DEV_SSM_WDT_PKE  "CyAsymWatchDogTimer"
#define ADF_DH895XCC_AE_FW_NAME "icp_qat_ae.uof"
#define ADF_CXXX_AE_FW_NAME "icp_qat_ae.suof"
#define ADF_HEARTBEAT_TIMER  "HeartbeatTimer"
#define ADF_MMP_VER_KEY "Firmware_MmpVer"
#define ADF_UOF_VER_KEY "Firmware_UofVer"
#define ADF_HW_REV_ID_KEY "HW_RevId"
#define ADF_STORAGE_FIRMWARE_ENABLED "StorageEnabled"
#define ADF_AUTO_RESET_ON_ERROR "AutoResetOnError"
#define ADF_SERVICES_PROFILE "ServicesProfile"
#define ADF_SERVICES_DEFAULT "DEFAULT"
#define ADF_SERVICES_CRYPTO "CRYPTO"
#define ADF_SERVICES_COMPRESSION "COMPRESSION"
#define ADF_SERVICES_CUSTOM1 "CUSTOM1"
#define ADF_DEV_KPT_ENABLE "KptEnabled"
#define ADF_KPT_MAX_SWK_COUNT_PER_FN "KptMaxSWKPerFn"
#define ADF_KPT_MAX_SWK_COUNT_PER_PASID "KptMaxSWKPerPASID"
#define ADF_KPT_MAX_SWK_TTL "KptMaxSWKLifetime"
#define ADF_KPT_SWK_SHARED "KptSWKShared"
#define ADF_PM_IDLE_SUPPORT "PmIdleSupport"
#define ADF_RL_FIRMWARE_ENABLED "RateLimitingEnabled"
#define ADF_SERVICE_CHAINING_ENABLED "ServiceChainingEnabled"
#define ADF_CXXX_AE_FW_NAME_COMPRESSION "compression.suof"
#define ADF_CXXX_AE_FW_NAME_CRYPTO "crypto.suof"
#define ADF_CXXX_AE_FW_NAME_CUSTOM1 "custom1.suof"
#if defined(QAT_UIO) || defined(QAT_ESXI)
#define ADF_DEV_MAX_BANKS "Device_Max_Banks"
#define ADF_DEV_CAPABILITIES_MASK "Device_Capabilities_Mask"
#define ADF_CIPHER_CAPABILITIES_MASK "Cipher_Capabilities_Mask"
#define ADF_HASH_CAPABILITIES_MASK "Hash_Capabilities_Mask"
#define ADF_ASYM_CAPABILITIES_MASK "Asym_Capabilities_Mask"
#define ADF_DEV_NODE_ID "Device_NodeId"
#define ADF_DEV_PKG_ID "Device_PkgId"
#define ADF_INTERNAL_USERSPACE_SEC_SUFF     "_INT_"
#define ADF_LIMIT_DEV_ACCESS "LimitDevAccess"
#define ADF_DEV_MAX_RINGS_PER_BANK "Device_Max_Rings_Per_Bank"
#define ADF_NUM_PROCESSES "NumProcesses"
#define ADF_SVM_ENABLE "SVMEnabled"
#define ADF_AT_ENABLE "ATEnabled"
#define ADF_DH895XCC_AE_FW_NAME_STORAGE "icp_qat_ae_storage.uof"
#define ADF_CXXX_AE_FW_NAME_STORAGE "icp_qat_ae_storage.suof"
#define ADF_CXXX_AE_FW_NAME_KPT "icp_qat_ae_kpt.suof"
#define ADF_DC_EXTENDED_FEATURES "Device_DcExtendedFeatures"
#define ADF_PKE_DISABLED "PkeServiceDisabled"
#define ADF_INTER_BUF_SIZE "DcIntermediateBufferSizeInKB"
#define ADF_POLL_MODE "IsPolled"
#define ADF_CY_RING_SYM_SIZE (ADF_CY ADF_RING_SYM_SIZE)
#define ADF_CY_RING_ASYM_SIZE (ADF_CY ADF_RING_ASYM_SIZE)
#define ADF_DC_RING_SIZE (ADF_DC ADF_RING_DC_SIZE)
#define ADF_ASYM "Asym"
#define ADF_SYM "Sym"
#define ADF_CY_CORE_AFFINITY_FORMAT \
	ADF_CY "%d" ADF_ETRMGR_CORE_AFFINITY
#define ADF_DC_CORE_AFFINITY_FORMAT \
	ADF_DC "%d" ADF_ETRMGR_CORE_AFFINITY
#define ADF_DC_BANK_NUM_FORMAT \
	ADF_DC "%d" ADF_RING_BANK_NUM
#define ADF_CY_ASYM_TX_FORMAT \
	ADF_CY "%d" ADF_RING_ASYM_TX
#define ADF_CY_SYM_TX_FORMAT \
	ADF_CY "%d" ADF_RING_SYM_TX
#define ADF_CY_ASYM_RX_FORMAT \
	ADF_CY "%d" ADF_RING_ASYM_RX
#define ADF_CY_SYM_RX_FORMAT \
	ADF_CY "%d" ADF_RING_SYM_RX
#define ADF_DC_TX_FORMAT \
	ADF_DC "%d" ADF_RING_DC_TX
#define ADF_DC_RX_FORMAT \
	ADF_DC "%d" ADF_RING_DC_RX
#define ADF_CY_RING_SYM_SIZE_FORMAT \
	ADF_CY "%d" ADF_RING_SYM_SIZE
#define ADF_CY_RING_ASYM_SIZE_FORMAT \
	ADF_CY "%d" ADF_RING_ASYM_SIZE
#define ADF_DC_RING_SIZE_FORMAT \
	ADF_DC "%d" ADF_RING_DC_SIZE
#define ADF_CY_NAME_FORMAT \
	ADF_CY "%dName"
#define ADF_DC_NAME_FORMAT \
	ADF_DC "%dName"
#define ADF_CY_POLL_MODE_FORMAT \
	ADF_CY "%d" ADF_POLL_MODE
#define ADF_DC_POLL_MODE_FORMAT \
	ADF_DC "%d" ADF_POLL_MODE
#define ADF_CONFIG_VERSION "ConfigVersion"
#define ADF_LIMITED_USER_SECTION_NAME_FORMAT "%s_DEV%d_INT_%d"
#define ADF_USER_SECTION_NAME_FORMAT "%s_INT_%d"
#endif
#endif
