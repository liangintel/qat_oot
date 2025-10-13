/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2022 Intel Corporation */
#ifndef _ICP_QAT_FW_INIT_ADMIN_H_
#define _ICP_QAT_FW_INIT_ADMIN_H_

#include "icp_qat_fw.h"

#define RL_MAX_RP_IDS 4

enum icp_qat_fw_init_admin_cmd_id {
	ICP_QAT_FW_INIT_ME = 0,
	ICP_QAT_FW_TRNG_ENABLE = 1,
	ICP_QAT_FW_TRNG_DISABLE = 2,
	ICP_QAT_FW_CONSTANTS_CFG = 3,
	ICP_QAT_FW_STATUS_GET = 4,
	ICP_QAT_FW_COUNTERS_GET = 5,
	ICP_QAT_FW_LOOPBACK = 6,
	ICP_QAT_FW_HEARTBEAT_SYNC = 7,
	ICP_QAT_FW_HEARTBEAT_GET = 8,
	ICP_QAT_FW_COMP_CAPABILITY_GET = 9,
	ICP_QAT_FW_CRYPTO_CAPABILITY_GET = 10,
	ICP_QAT_FW_DC_CHAIN_INIT = 11,
	ICP_QAT_FW_HEARTBEAT_TIMER_SET = 13,
	ICP_QAT_FW_RL_SLA_CONFIG = 14,
	ICP_QAT_FW_RL_INIT = 15,
	ICP_QAT_FW_RL_DU_START = 16,
	ICP_QAT_FW_RL_DU_STOP = 17,
	ICP_QAT_FW_TIMER_GET = 19,
	ICP_QAT_FW_CNV_STATS_GET = 20,
	ICP_QAT_FW_PKE_REPLAY_STATS_GET = 21,
	ICP_QAT_FW_PM_STATE_CONFIG = 128,
	ICP_QAT_FW_PM_INFO = 129,
	ICP_QAT_FW_RL_ADD = 134,
	ICP_QAT_FW_RL_UPDATE = 135,
	ICP_QAT_FW_RL_REMOVE = 136,
	ICP_QAT_FW_TL_START = 137,
	ICP_QAT_FW_TL_STOP = 138,
	ICP_QAT_FW_KPT_ENABLE = 144,
	/* Get kpt statistic and public info for debug purpose */
	ICP_QAT_FW_KPT_IPUB_ISIG_GET = 145,
	ICP_QAT_FW_KPT_STATS_GET = 146,
	ICP_QAT_FW_KPT_INFO_GET = 147,
};

enum icp_qat_fw_init_admin_resp_status {
	ICP_QAT_FW_INIT_RESP_STATUS_SUCCESS = 0,
#ifdef QAT_UIO
	ICP_QAT_FW_INIT_RESP_STATUS_FAIL = 1,
	ICP_QAT_FW_INIT_RESP_STATUS_UNSUPPORTED = 4
#else
	ICP_QAT_FW_INIT_RESP_STATUS_FAIL
#endif
};

enum icp_qat_fw_cnv_error_type {
	CNV_ERR_TYPE_NO_ERROR = 0,
	CNV_ERR_TYPE_CHECKSUM_ERROR,
	CNV_ERR_TYPE_DECOMP_PRODUCED_LENGTH_ERROR,
	CNV_ERR_TYPE_DECOMPRESSION_ERROR,
	CNV_ERR_TYPE_TRANSLATION_ERROR,
	CNV_ERR_TYPE_DECOMP_CONSUMED_LENGTH_ERROR,
	CNV_ERR_TYPE_UNKNOWN_ERROR
};

#define CNV_ERROR_TYPE_GET(latest_error)	\
	({__typeof__(latest_error) _lerror = latest_error;	\
	(_lerror >> 12)	> CNV_ERR_TYPE_UNKNOWN_ERROR	\
	? CNV_ERR_TYPE_UNKNOWN_ERROR	\
	: (enum icp_qat_fw_cnv_error_type)(_lerror >> 12); })
#define CNV_ERROR_LENGTH_DELTA_GET(latest_error)	\
	({__typeof__(latest_error) _lerror = latest_error;	\
	((s16)((_lerror & 0x0FFF)	\
	| (_lerror & 0x0800 ? 0xF000 : 0))); })
#define CNV_ERROR_DECOMP_STATUS_GET(latest_error) ((s8)(latest_error & 0xFF))

#define ICP_QAT_FW_INIT_AE_AT_ENABLE_FLAG 0x01

struct icp_qat_fw_init_admin_req {
	u16 init_cfg_sz;
	u8 resrvd1;
	u8 cmd_id;
	u32 resrvd2;
	u64 opaque_data;
	u64 init_cfg_ptr;

	union {
		/* ICP_QAT_FW_INIT_ME */
		struct {
			u16 ibuf_size_in_kb;
			u8 fw_flags;
			u8 resrvd5;
		};
		/* ICP_QAT_FW_HEARTBEAT_TIMER_SET */
		struct {
			u32 heartbeat_ticks;
		};
		/* ICP_QAT_INIT_PM 2.0 */
		struct {
			u32 idle_filter;
		};
		/* ICP_QAT_FW_TL_START 2.0 */
		struct {
			u8 rp_num_index_0;
			u8 rp_num_index_1;
			u8 rp_num_index_2;
			u8 rp_num_index_3;
		};
#ifdef QAT_UIO
		/* ICP_QAT_FW_RL_SLA_CONFIG 1.x */
		struct {
			u32 credit_per_sla;
			u8 service_id;
			u8 vf_id;
		};
		/* ICP_QAT_FW_RL_INIT 1.x */
		struct {
			u32 rl_period;
			u8 config;
			u8 resrvd3;
			u8 num_me;
			u8 resrvd4;
			u8 pke_svc_arb_map;
			u8 bulk_crypto_svc_arb_map;
			u8 compression_svc_arb_map;
		};
		/* ICP_QAT_FW_RL_DU_STOP 1.x */
		struct {
			u64 cfg_ptr;
		};
#endif /* QAT_UIO */
		/* ICP_QAT_FW_ADD_SLA 2.0 */
		/* ICP_QAT_FW_UPDATE_SLA 2.0 */
		/* ICP_QAT_FW_REMOVE_SLA 2.0 */
		struct {
			u16 node_id;
			u8 node_type;
			u8 svc_type;
			u8 rl_reserved[3];
			u8 rp_count;
		};
	};
} __packed;

struct icp_qat_fw_init_admin_resp {
	u8 flags;
	u8 resrvd1;
	u8 status;
	u8 cmd_id;
	union {
		u32 resrvd2;
		u32 ras_event_count;
		/* ICP_QAT_FW_STATUS_GET */
		struct {
			u16 version_minor_num;
			u16 version_major_num;
		};
		/* ICP_QAT_FW_COMP_CAPABILITY_GET */
		u32 extended_features;
		/* ICP_QAT_FW_CNV_STATS_GET */
		struct {
			u16 error_count;
			u16 latest_error;
		};
	};
	u64 opaque_data;
	union {
		u32 resrvd3[ICP_QAT_FW_NUM_LONGWORDS_4];
		struct {
			u32 version_patch_num;
			u8 context_id;
			u8 ae_id;
			u16 resrvd4;
			u64 resrvd5;
		};
		struct {
			u64 req_rec_count;
			u64 resp_sent_count;
		};
		struct {
			u16    compression_algos;
			u16    checksum_algos;
			u32    deflate_capabilities;
			u32    resrvd6;
			u32    deprecated;
		};
		struct {
			u32    cipher_algos;
			u32    hash_algos;
			u16    keygen_algos;
			u16    other;
			u16    public_key_algos;
			u16    prime_algos;
		};
		struct {
			u64 timestamp;
			u64 resrvd7;
		};
		struct { /* ICP_QAT_FW_PKE_REPLAY_STATS_GET */
			u32 successful_count;
			u32 unsuccessful_count;
			u64 resrvd8;
		};
		struct { /* ICP_QAT_FW_TL_START and ICP_QAT_FW_RL_INIT */
			u8 cpr_slice_cnt;
			u8 xlt_slice_cnt;
			u8 dcpr_slice_cnt;
			u8 pke_slice_cnt;
			u8 wat_slice_cnt;
			u8 wcp_slice_cnt;
			u8 ucs_slice_cnt;
			u8 cph_slice_cnt;
			u8 ath_slice_cnt;
			u8 reservd1;
			u16 reservd2;
			u32 reservd3;
		} slice_count;
	};
} __packed;

enum icp_qat_fw_init_admin_init_flag {
	ICP_QAT_FW_INIT_FLAG_PKE_DISABLED = 0
};

struct icp_qat_fw_init_admin_hb_cnt {
	u16 req_heartbeat_cnt;
	u16 resp_heartbeat_cnt;
};

struct icp_qat_fw_init_admin_pm_info {
	u16 max_pwrreq;
	u16 min_pwrreq;
	u16 resvrd1;
	u8 pwr_state;
	u8 resvrd2;
	u32 fusectl0;
	u32 sys_pm_event_count;
	u32 host_msg_event_count;
	u32 unknown_event_count;
	u32 local_ssm_event_count;
	u32 timer_event_count;
	u32 event_log[8];
	u32 pm_fw_init;
	u32 pm_pwrreq;
	u32 pm_status;
	u32 pm_main;
	u32 pm_thread;
	u32 ssm_pm_enable;
	u32 ssm_pm_active_status;
	u32 ssm_pm_managed_status;
	u32 ssm_pm_domain_status;
	u32 active_constraints;
	u32 resvrd3[6];
};

struct icp_qat_fw_init_admin_kpt_config_params {
	u32 swk_count_per_fn;
	u32 swk_count_per_pasid;
	u32 swk_ttl_in_secs;
	u32 swk_shared;
};

struct icp_qat_fw_init_admin_sla_config_params {
	u32 pcie_in_cir;
	u32 pcie_in_pir;
	u32 pcie_out_cir;
	u32 pcie_out_pir;
	u32 slice_util_cir;
	u32 slice_util_pir;
	u32 ae_util_cir;
	u32 ae_util_pir;
	u16 rp_ids[RL_MAX_RP_IDS];
};

#define ICP_QAT_FW_COMN_HEARTBEAT_OK 0
#define ICP_QAT_FW_COMN_HEARTBEAT_BLOCKED 1
#define ICP_QAT_FW_COMN_HEARTBEAT_FLAG_BITPOS 0
#define ICP_QAT_FW_COMN_HEARTBEAT_FLAG_MASK 0x1
#define ICP_QAT_FW_COMN_FWTYPE_LA 0x0
#define ICP_QAT_FW_COMN_FWTYPE_DC 0x1
#define ICP_QAT_FW_COMN_FWTYPE_INLINE 0x2
#define ICP_QAT_FW_COMN_FWTYPE_FLAG_BITPOS 0
#define ICP_QAT_FW_COMN_FWTYPE_FLAG_MASK 0x3
#define ICP_QAT_FW_COMN_STATUS_RESRVD_FLD_MASK 0xFE
#define ICP_QAT_FW_COMN_HEARTBEAT_HDR_FLAG_GET(hdr_t) \
	ICP_QAT_FW_COMN_HEARTBEAT_FLAG_GET(hdr_t.flags)

#define ICP_QAT_FW_COMN_HEARTBEAT_HDR_FLAG_SET(hdr_t, val) \
	ICP_QAT_FW_COMN_HEARTBEAT_FLAG_SET(hdr_t, val)

#define ICP_QAT_FW_COMN_HEARTBEAT_FLAG_GET(flags) \
	QAT_FIELD_GET(flags, \
		 ICP_QAT_FW_COMN_HEARTBEAT_FLAG_BITPOS, \
		 ICP_QAT_FW_COMN_HEARTBEAT_FLAG_MASK)
#endif
