/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2024 Intel Corporation */

#ifndef ADF_RL_H_
#define ADF_RL_H_

#include "adf_sla_user.h"

#define RL_MAX_ROOT 4
#define RL_MAX_CLUSTER 16
#define RL_MAX_LEAF 64
#define RL_MAX_RPS_PER_LEAF 16
#define RL_TOKEN_GRANULARITY_PCIEIN_BUCKET 0U
#define RL_TOKEN_GRANULARITY_PCIEOUT_BUCKET 0U

#define RL_TOKEN_PCIE_SIZE 64
#define RL_ASYM_TOKEN_SIZE 1024
#define RL_SLA_CONFIG_SIZE 64

/* Convert Mbits to bytes: mul(10^6) then div(8) */
#define RL_CONVERT_TO_BYTES ((u64)125000)

#define RL_VALIDATE_IR(rate, max) ((rate) > (max))
#define RL_VALIDATE_SLAU(rate, max) ((rate) > ((max)))
#define RL_VALIDATE_NON_ZERO(input) ((input) == 0)
#define RL_VALIDATE_RET_MAX(svc, slice_ref, max_tp) \
	(((svc) == ADF_SVC_ASYM) ? slice_ref : max_tp)

/*
 * First 5 bits is for the non unique id
 * 0 - 3  : root
 * 0 - 15 : cluster
 * 0 - 63 : leaves
 * In conjunction with next 2 bits, creates a unique id (bits 0 - 7)
 * 01 -	root
 * 10 -	cluster
 * 11 -	leaf
 */
#define RL_NODEID_TO_TREEID(node_id, node_type)                                \
	((node_id) | (((node_type) + 1) << 6))

/* Returns the node ID from the tree ID */
#define RL_TREEID_TO_NODEID(user_id) ((user_id) & 0x3F)

/* Returns the node ID from the tree ID for root node only */
#define RL_TREEID_TO_ROOT(user_id) ((user_id) & 0x03)

/*
 * internal struct to track rings associated with an SLA
 */
struct rl_rings_info {
	u32 num_rings;
	u16 rp_ids[RL_MAX_RPS_PER_LEAF];
};

struct rl_slice_cnt {
	u8 cpr_slice_cnt;
	u8 xlt_slice_cnt;
	u8 dcpr_slice_cnt;
	u8 pke_slice_cnt;
	u8 wat_slice_cnt;
	u8 wcp_slice_cnt;
	u8 ucs_slice_cnt;
	u8 cph_slice_cnt;
	u8 ath_slice_cnt;
};

struct adf_rl_hw_data {
	u32 pcie_in_bucket_offset;
	u32 pcie_out_bucket_offset;
	u32 ring_to_leaf_offset;
	u32 leaf_to_cluster_offset;
	u32 cluster_to_service_offset;

	u32 pcie_scale_multiplier;
	u32 pcie_scale_divisor;

	u32 scan_interval;
	u32 max_throughput[ADF_SVC_NONE];
	u32 slice_reference;
	u32 dc_correction;
	struct rl_slice_cnt slice_cnt;
};

struct adf_accel_dev;
bool rl_is_svc_enabled(struct adf_accel_dev *accel_dev,
		       enum adf_svc_type adf_svc);
enum adf_cfg_service_type rl_adf_svc_to_cfg_svc(enum adf_svc_type adf_svc);
enum adf_svc_type rl_cfg_svc_to_adf_svc(enum adf_cfg_service_type svc);
int rl_send_admin_init(struct adf_accel_dev *accel_dev);
int rl_send_admin_delete_sla(struct adf_accel_dev *accel_dev, u16 id, u8 type);
void rl_link_node_to_parent(struct adf_accel_dev *accel_dev, u32 node_id,
			    u32 parent_id, enum adf_user_node_type parent_type);
int rl_validate_hw_data(struct adf_accel_dev *accel_dev);

#endif /* ADF_RL_H_ */
