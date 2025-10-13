/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2020 - 2024 Intel Corporation */

#ifndef ADF_RL_V2_H_
#define ADF_RL_V2_H_

#include "adf_sla_user.h"

#define RL_VF_OFFSET 0x1
#define RL_PCI_DEV_OFFSET 0x3

#define RL_INPUT_FUNC_MAX 0xFF
#define RL_VALIDATE_PCI_FUNK(sla) ((sla)->pci_addr.func > RL_INPUT_FUNC_MAX)

/* Returns the node type -> root, cluster, leaf */
#define RL_GET_NODE_TYPE(user_id) (((user_id) >> 6) - 1)

#define RL_RING_NUM(vf_func_num, vf_bank) (((vf_func_num) << (2)) + (vf_bank))

struct adf_accel_dev;
/* Internal context for each node in rate limiting tree */
struct rl_node_info {
	enum adf_user_node_type nodetype;
	u32 node_id; /* Unique ID */
	u32 rem_cir;
	u32 max_pir;
	bool sla_added;
	enum adf_svc_type svc_type;
	struct adf_user_sla sla; /* Copy of the user SLA */
	struct rl_node_info *parent;
};

/* Internal structure for tracking number of leafs/clusters */
struct rl_node_count {
	/* Keep track of nodes in-use */
	u32 root_count;
	u32 cluster_count;
	u32 leaf_count;
	/* Keep track of assigned SLA's */
	u32 sla_count_root;
	u32 sla_count_cluster;
	u32 sla_count_leaf;
};

/*
 * Internal structure for rl_node_info + rl_node_count
 */
struct adf_rl_v2 {
	struct rl_node_info root_info[RL_MAX_ROOT];
	struct rl_node_count node_count;
	struct idr *cluster_idr;
	struct idr *leaf_idr;
};

int adf_rl_v2_init(struct adf_accel_dev *accel_dev);
void adf_rl_v2_exit(struct adf_accel_dev *accel_dev);
void rl_v2_get_caps(struct adf_accel_dev *accel_dev,
		    struct adf_user_sla_caps *sla_caps);
void rl_v2_get_user_slas(struct adf_accel_dev *accel_dev,
			 struct adf_user_slas *slas);
int rl_v2_create_user_node(struct adf_accel_dev *accel_dev,
			   struct adf_user_node *node);
int rl_v2_delete_user_node(struct adf_accel_dev *accel_dev,
			   struct adf_user_node *node);
int rl_v2_create_sla(struct adf_accel_dev *accel_dev, struct adf_user_sla *sla);
int rl_v2_update_sla(struct adf_accel_dev *accel_dev, struct adf_user_sla *sla);
int rl_v2_delete_sla(struct adf_accel_dev *accel_dev, struct adf_user_sla *sla);
u32 rl_pci_to_vf_num(struct adf_user_sla *sla);
int rl_v2_set_node_id(struct adf_accel_dev *accel_dev,
		      struct adf_user_sla *sla);

#endif /* ADF_RL_V2_H_ */
