/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2020 - 2024 Intel Corporation */

#include "adf_accel_devices.h"
#include "icp_qat_fw_init_admin.h"
#include "adf_common_drv.h"
#include "adf_rl_v2.h"
#include "adf_rl.h"

u32 rl_pci_to_vf_num(struct adf_user_sla *sla)
{
	return ((sla->pci_addr.dev << RL_PCI_DEV_OFFSET) + sla->pci_addr.func -
		RL_VF_OFFSET);
}


static struct rl_node_info *
adf_rl_get_root_node_locked(struct adf_accel_dev *accel_dev,
			    enum adf_svc_type svc_type)
{
	struct rl_node_count *rl_count;
	int i;

	if (svc_type >= ADF_SVC_NONE) {
		dev_err(&GET_DEV(accel_dev),
			"Rate Limiting: Invalid service type\n");
		return NULL;
	}

	if (!accel_dev->rl_v2) {
		dev_err(&GET_DEV(accel_dev), "Rate Limiting: rl_v2 is NULL\n");
		return NULL;
	}

	rl_count = &accel_dev->rl_v2->node_count;

	for (i = 0; i < rl_count->root_count; i++) {
		if (accel_dev->rl_v2->root_info[i].svc_type == svc_type)
			return &accel_dev->rl_v2->root_info[i];
	}

	return NULL;
}

int rl_v2_set_node_id(struct adf_accel_dev *accel_dev, struct adf_user_sla *sla)
{
	struct rl_node_info *root_node;
	u32 static_node_id = 0, total_vfs = 0;

	if (RL_VALIDATE_PCI_FUNK(sla))
		return -EINVAL;

	mutex_lock(&accel_dev->rl_lock);
	root_node = adf_rl_get_root_node_locked(accel_dev, sla->svc_type);
	if (!root_node) {
		mutex_unlock(&accel_dev->rl_lock);
		return -ENOENT;
	}
	mutex_unlock(&accel_dev->rl_lock);

	total_vfs = pci_sriov_get_totalvfs(accel_to_pci_dev(accel_dev));
	static_node_id = (RL_TREEID_TO_ROOT(root_node->node_id) * total_vfs) +
			 rl_pci_to_vf_num(sla);

	/* Node ID corresponds to the node in the static config */
	sla->node_id = RL_NODEID_TO_TREEID(static_node_id, ADF_NODE_LEAF);

	/*
	 * We only have 1 cluster per service (and root node), so we can
	 * deduce the parent using the root node.
	 */
	sla->parent_node_id =
		RL_NODEID_TO_TREEID(RL_TREEID_TO_ROOT(root_node->node_id),
				    ADF_NODE_CLUSTER);

	return 0;
}

static bool rl_check_id_range(struct adf_accel_dev *accel_dev, u32 id,
			      enum adf_user_node_type node_type)
{
	switch (node_type) {
	case ADF_NODE_LEAF:
		if (id < RL_NODEID_TO_TREEID(0, node_type) ||
		    id > RL_NODEID_TO_TREEID(RL_MAX_LEAF - 1, node_type)) {
			dev_err(&GET_DEV(accel_dev),
				"Rate Limiting: Leaf id out of range\n");
			return false;
		}
		break;
	case ADF_NODE_CLUSTER:
		if (id < RL_NODEID_TO_TREEID(0, node_type) ||
		    id > RL_NODEID_TO_TREEID(RL_MAX_CLUSTER - 1, node_type)) {
			dev_err(&GET_DEV(accel_dev),
				"Rate Limiting: Cluster id out of range\n");
			return false;
		}
		break;
	case ADF_NODE_ROOT:
		if (id < RL_NODEID_TO_TREEID(0, node_type) ||
		    id > RL_NODEID_TO_TREEID(RL_MAX_ROOT - 1, node_type)) {
			dev_err(&GET_DEV(accel_dev),
				"Rate Limiting: Root id out of range\n");
			return false;
		}
		break;
	default:
		dev_err(&GET_DEV(accel_dev),
			"Rate limiting: Not a valid node type\n");
		return false;
	}
	return true;
}

static bool rl_set_ring_for_svc(struct adf_accel_dev *accel_dev,
				struct adf_user_sla *sla, u32 bank, u32 *ring)
{
	u8 serv_type =
		GET_SRV_TYPE(accel_dev->hw_device->ring_to_svc_map, bank);

	/* Check the bank supports the specified service */
	if (rl_cfg_svc_to_adf_svc(serv_type) == sla->svc_type) {
		*ring = RL_RING_NUM(rl_pci_to_vf_num(sla), bank);
		return true;
	}
	return false;
}

static int rl_hw_unconfigure_tree(struct adf_accel_dev *accel_dev,
				  struct rl_node_info *node,
				  struct adf_user_sla *sla)
{
	u32 ring = 0, i = 0;

	switch (node->nodetype) {
	case ADF_NODE_CLUSTER:
		rl_link_node_to_parent(accel_dev, node->node_id, 0U,
				       ADF_NODE_ROOT);
		break;
	case ADF_NODE_LEAF:
		for (i = 0; i < accel_dev->hw_device->num_banks_per_vf; i++) {
			if (rl_set_ring_for_svc(accel_dev, sla, i, &ring))
				rl_link_node_to_parent(accel_dev, ring, 0U,
						       ADF_NODE_LEAF);
		}
		rl_link_node_to_parent(accel_dev, sla->node_id, 0U,
				       ADF_NODE_CLUSTER);
		break;
	case ADF_NODE_ROOT:
		break;
	default:
		dev_err(&GET_DEV(accel_dev),
			"Rate Limiting: Not a valid node type\n");
		return -EFAULT;
	}
	return 0;
}

/* Rl_v1 cannot create root/cluster/leaf nodes,
 * and cannot configure SLA's for root or cluster nodes.
 * Therefore we must generate the root/cluster/leaf nodes,
 * and create the SLA's for our root and cluster.
 */
static int rl_init_v1_tree(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	int ret = 0;
	u32 i = 0, vf_index = 0;
	struct adf_user_sla sla = { { 0 } };
	struct adf_user_node cluster_node = { { 0 } };
	struct adf_user_node leaf_node = { { 0 } };
	struct rl_node_info *rl_root = NULL;
	struct rl_node_count *rl_count = &accel_dev->rl_v2->node_count;

	/* Create nodes/SLA's */
	for (i = 0; i < rl_count->root_count; i++) {
		/* Root SLA's */
		rl_root = &accel_dev->rl_v2->root_info[i];

		/* Create cluster nodes */
		cluster_node.nodetype = ADF_NODE_CLUSTER;
		cluster_node.svc_type = rl_root->svc_type;
		ret = rl_v2_create_user_node(accel_dev, &cluster_node);
		if (ret)
			return ret;

		/* Create leaf nodes */
		for (vf_index = 0;
		     vf_index <
		     pci_sriov_get_totalvfs(accel_to_pci_dev(accel_dev));
		     vf_index++) {
			leaf_node.nodetype = ADF_NODE_LEAF;
			leaf_node.svc_type = rl_root->svc_type;
			ret = rl_v2_create_user_node(accel_dev, &leaf_node);
			if (ret)
				return ret;
		}

		memset(&sla, 0, sizeof(sla));
		sla.node_id = rl_root->node_id;
		sla.nodetype = ADF_NODE_ROOT;
		sla.svc_type = rl_root->svc_type;
		sla.cir = (rl_root->svc_type == ADF_SVC_ASYM) ?
				  hw_data->rl_data.slice_reference :
				  hw_data->rl_data.max_throughput[sla.svc_type];
		sla.pir = sla.cir;

		/* Create root SLA's */
		ret = rl_v2_create_sla(accel_dev, &sla);
		if (ret)
			return ret;

		sla.node_id = cluster_node.node_id;
		sla.nodetype = cluster_node.nodetype;

		/* Create cluster SLA's */
		ret = rl_v2_create_sla(accel_dev, &sla);
		if (ret)
			return ret;
	}

	return 0;
}

/* Free memory of RL root nodes structures */
static void rl_free_root_structs(struct adf_accel_dev *accel_dev)
{
	if (accel_dev->rl_v2) {
		kfree(accel_dev->rl_v2->cluster_idr);
		kfree(accel_dev->rl_v2->leaf_idr);
		kfree(accel_dev->rl_v2);
		accel_dev->rl_v2 = NULL;
	}
}

/* Initialise memory for RL root nodes structures */
static int rl_alloc_root_structs(struct adf_accel_dev *accel_dev)
{
	struct adf_rl_v2 *rl_v2 = NULL;

	if (!accel_dev->rl_v2) {
		rl_v2 = kzalloc(sizeof(*rl_v2), GFP_KERNEL);
		if (!rl_v2)
			return -ENOMEM;

		rl_v2->cluster_idr =
			kzalloc(sizeof(*rl_v2->cluster_idr), GFP_KERNEL);
		if (!rl_v2->cluster_idr) {
			kfree(rl_v2);
			return -ENOMEM;
		}

		rl_v2->leaf_idr =
			kzalloc(sizeof(*rl_v2->leaf_idr), GFP_KERNEL);
		if (!rl_v2->leaf_idr) {
			kfree(rl_v2->cluster_idr);
			kfree(rl_v2);
			return -ENOMEM;
		}

		accel_dev->rl_v2 = rl_v2;

		return 0;
	}

	return -EFAULT;
}

/* Initialise the root structure default values */
static int rl_init_root(struct adf_accel_dev *accel_dev)
{
	struct rl_node_count *rl_count = &accel_dev->rl_v2->node_count;
	struct rl_node_info *root_node = NULL;
	u32 i, root_idx = 0;

	rl_count->root_count = 0;
	for (i = 0; i < ADF_SVC_NONE; i++) {
		u32 node_id = root_idx + 1;

		/*
		 * XXX: Avoid using node with id == 0 as it may lead to
		 * issues with rate limiting service assigned to this node.
		 * Also we need to check if such node_id is could be used.
		 */
		if (node_id >= RL_MAX_ROOT) {
			dev_err(&GET_DEV(accel_dev),
				"Rate Limiting: Root node id is too high\n");
			return -ENOENT;
		}

		/* Check root node avaialability */
		if (root_idx >= ARRAY_SIZE(accel_dev->rl_v2->root_info)) {
			dev_err(&GET_DEV(accel_dev),
				"Rate Limiting: Not enough root nodes for all enabled services\n");
			return -ENOENT;
		}

		if (!rl_is_svc_enabled(accel_dev, i))
			continue;

		root_node = &accel_dev->rl_v2->root_info[root_idx];
		root_node->nodetype = ADF_NODE_ROOT;
		root_node->node_id =
			RL_NODEID_TO_TREEID(node_id, ADF_NODE_ROOT);
		root_node->sla_added = false;
		root_node->svc_type = i;
		root_idx++;
		rl_count->root_count++;
	}
	return 0;
}

static void rl_caps_get_cir_pir(struct adf_accel_dev *accel_dev,
				struct rl_node_info *rl_root,
				struct adf_user_service *service)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct rl_node_info *cluster = NULL;
	u32 node_id = 0;

	service->max_svc_rate_in_slau = 0;
	service->avail_svc_rate_in_slau = 0;

	if (accel_dev->rl_v2->cluster_idr) {
		idr_for_each_entry(accel_dev->rl_v2->cluster_idr, cluster, node_id)
		{
			if (cluster->svc_type == rl_root->svc_type) {
				if (rl_root->svc_type == ADF_SVC_ASYM)
					service->max_svc_rate_in_slau =
						hw_data->rl_data.slice_reference;
				else
					service->max_svc_rate_in_slau =
					    hw_data->rl_data.max_throughput[cluster->svc_type];
				service->avail_svc_rate_in_slau +=
					cluster->rem_cir;
			}
		}
	}
}

/*
 * rl_v2_get_caps - return the capabilities of SLA
 *
 * Function receives the user input as argument and gets the capability
 * information which is supported on the specific device
 */
void rl_v2_get_caps(struct adf_accel_dev *accel_dev,
		    struct adf_user_sla_caps *sla_caps)
{
	u32 i = 0;
	struct rl_node_info *rl_root = NULL;
	struct rl_node_count *rl_count = NULL;
	struct pci_dev *pdev = accel_to_pci_dev(accel_dev);
	struct adf_pci_address pci_addr = { 0 };

	mutex_lock(&accel_dev->rl_lock);

	rl_count = &accel_dev->rl_v2->node_count;

	memcpy(&pci_addr, &sla_caps->pf_addr, sizeof(struct adf_pci_address));
	memset(sla_caps, 0, sizeof(struct adf_user_sla_caps));

	for (i = 0; i < ARRAY_SIZE(sla_caps->services); i++) {
		if (i >= rl_count->root_count) {
			sla_caps->services[i].svc_type = ADF_SVC_NONE;
			continue;
		}

		rl_root = &accel_dev->rl_v2->root_info[i];

		sla_caps->services[i].svc_type = rl_root->svc_type;
		sla_caps->num_services++;
		rl_caps_get_cir_pir(accel_dev, rl_root, &sla_caps->services[i]);
	}

	sla_caps->max_slas = sla_caps->num_services * pci_num_vf(pdev);
	sla_caps->used_slas = rl_count->sla_count_leaf;
	sla_caps->avail_slas = sla_caps->max_slas - sla_caps->used_slas;

	memcpy(&sla_caps->pf_addr, &pci_addr, sizeof(struct adf_pci_address));

	mutex_unlock(&accel_dev->rl_lock);
}

static int rl_populate_sla(struct adf_accel_dev *accel_dev,
			   struct rl_node_info *rl_root,
			   struct adf_user_slas *slas, int sla_num)
{
	struct rl_node_info *leaf = NULL;
	u32 node_id = 0;

	/* For phase 1 we only return leaf SLA's */
	if (accel_dev->rl_v2->leaf_idr) {
		idr_for_each_entry(accel_dev->rl_v2->leaf_idr, leaf, node_id) {
			if (leaf->sla_added &&
			    leaf->svc_type == rl_root->svc_type) {
				memcpy(&slas->slas[sla_num], &leaf->sla,
				       sizeof(struct adf_user_sla));
				sla_num++;
			}
		}
	}

	return sla_num;
}

static void rl_populate_sla_buffer(struct adf_accel_dev *accel_dev,
				   struct adf_user_slas *slas)
{
	int sla_num = 0, i = 0;
	struct rl_node_info *rl_root = NULL;
	struct rl_node_count *rl_count =
		&accel_dev->rl_v2->node_count;

	for (i = 0; i < rl_count->root_count; i++) {
		rl_root = &accel_dev->rl_v2->root_info[i];
		if (rl_root->sla_added) {
			sla_num = rl_populate_sla(accel_dev, rl_root, slas,
						  sla_num);
		}
	}
}

/*
 *  rl_v2_get_slas - return the list of user SLA's
 */
void rl_v2_get_user_slas(struct adf_accel_dev *accel_dev,
			 struct adf_user_slas *slas)
{
	struct rl_node_count *rl_count = NULL;

	mutex_lock(&accel_dev->rl_lock);

	rl_count = &accel_dev->rl_v2->node_count;

	rl_populate_sla_buffer(accel_dev, slas);
	slas->used_slas = rl_count->sla_count_leaf;

	mutex_unlock(&accel_dev->rl_lock);
}

int rl_v2_create_user_node(struct adf_accel_dev *accel_dev,
			   struct adf_user_node *node)
{
	int id = 0;
	struct rl_node_info *new_node = NULL;
	struct rl_node_count *count = NULL;

	new_node = kzalloc(sizeof(*new_node), GFP_KERNEL);
	if (!new_node) {
		dev_err(&GET_DEV(accel_dev),
			"Rate Limiting: Node allocation failed\n");
		return -ENOMEM;
	}

	mutex_lock(&accel_dev->rl_lock);

	count = &accel_dev->rl_v2->node_count;

	switch (node->nodetype) {
	case ADF_NODE_CLUSTER:
		/* XXX: skip usage of node id 0 for clusters */
		id = idr_alloc(accel_dev->rl_v2->cluster_idr, new_node, 1,
			       RL_MAX_CLUSTER, GFP_KERNEL);
		if (id == -ENOSPC)
			goto error;
		new_node->node_id = RL_NODEID_TO_TREEID(id, node->nodetype);
		count->cluster_count += 1;
		break;
	case ADF_NODE_LEAF:
		/*
		 * XXX: skip usage of node id 0 for leafs - and subsequently
		 * all ids from 0 to 15 to avoid changes in the static node
		 * id assignment logic.
		 */
		id = idr_alloc(accel_dev->rl_v2->leaf_idr, new_node, RL_MAX_CLUSTER,
			       RL_MAX_LEAF, GFP_KERNEL);
		if (id == -ENOSPC)
			goto error;
		new_node->node_id = RL_NODEID_TO_TREEID(id, node->nodetype);
		count->leaf_count += 1;
		break;
	default:
		dev_err(&GET_DEV(accel_dev),
			"Rate Limiting: Unsupported node type\n");
		kfree(new_node);
		mutex_unlock(&accel_dev->rl_lock);
		return -EFAULT;
	}

	new_node->svc_type = node->svc_type;
	new_node->nodetype = node->nodetype;

	/* Return the sla_id to the user */
	node->node_id = new_node->node_id;

	mutex_unlock(&accel_dev->rl_lock);

	return 0;

error:
	kfree(new_node);
	dev_err(&GET_DEV(accel_dev), "Rate Limiting: No free idr\n");
	mutex_unlock(&accel_dev->rl_lock);

	return -ENOSPC;
}

static int rl_is_sla_added_for_children(struct idr *idr, u32 parent_node_id)
{
	u32 node_id = 0;
	struct rl_node_info *node = NULL;

	idr_for_each_entry(idr, node, node_id) {
		if (node->sla_added &&
		    node->sla.parent_node_id == parent_node_id) {
			return -EFAULT;
		}
	}

	return 0;
}

static int rl_increase_rem_sla(struct adf_accel_dev *accel_dev,
			       struct rl_node_info *node)
{
	struct rl_node_info *parent = NULL;

	if (!node->parent)
		return -EFAULT;

	parent = node->parent;

	switch (RL_GET_NODE_TYPE(node->parent->node_id)) {
	case ADF_NODE_CLUSTER:
	case ADF_NODE_ROOT:
		parent->rem_cir += node->sla.cir;
		break;
	default:
		dev_err(&GET_DEV(accel_dev), "Rate Limiting: Invalid node type\n");
		return -EFAULT;
	}

	return 0;
}

static int rl_remove_sla_from_node(struct adf_accel_dev *accel_dev,
				   struct idr *idr, struct adf_user_sla *sla)
{
	u32 id;
	struct rl_node_info *find = NULL;
	struct rl_node_count *rl_count =
		&accel_dev->rl_v2->node_count;

	id = RL_TREEID_TO_NODEID(sla->sla_id);
	find = idr_find(idr, id);
	if (!find) {
		dev_err(&GET_DEV(accel_dev), "Rate Limiting: Node not found\n");
		return -EFAULT;
	}

	if (!find->sla_added) {
		dev_err(&GET_DEV(accel_dev),
			"Rate Limiting: No SLA added to the node\n");
		return -EFAULT;
	}

	if (find->nodetype == ADF_NODE_CLUSTER &&
	    rl_is_sla_added_for_children(accel_dev->rl_v2->leaf_idr,
					 find->node_id)) {
		dev_err(&GET_DEV(accel_dev),
			"Rate Limiting: Can't remove SLA, delete SLA for leaf first\n");
		return -EPERM;
	}

	if (rl_send_admin_delete_sla(accel_dev, find->sla.sla_id,
				     find->sla.nodetype))
		return -EFAULT;

	if (rl_increase_rem_sla(accel_dev, find))
		return -EFAULT;

	if (rl_hw_unconfigure_tree(accel_dev, find, &find->sla))
		return -EFAULT;

	find->sla_added = false;

	memset(&find->sla, 0, sizeof(struct adf_user_sla));

	if (RL_GET_NODE_TYPE(find->node_id) == ADF_NODE_LEAF) {
		if (rl_count->sla_count_leaf > 0)
			rl_count->sla_count_leaf -= 1;
	} else {
		if (rl_count->sla_count_cluster > 0)
			rl_count->sla_count_cluster -= 1;
	}

	return 0;
}

static int rl_remove_root_sla_node(struct adf_accel_dev *accel_dev,
				   struct adf_user_sla *sla)
{
	struct rl_node_info *rl_root =
		&accel_dev->rl_v2
			 ->root_info[RL_TREEID_TO_ROOT(sla->sla_id)];

	/* Check for clusters SLA's */
	if (rl_is_sla_added_for_children(accel_dev->rl_v2->cluster_idr,
					 rl_root->node_id)) {
		dev_err(&GET_DEV(accel_dev), "Cluster still has an SLA\n");
		return -EPERM;
	}

	if (rl_root->sla_added)
		if (rl_send_admin_delete_sla(accel_dev, rl_root->sla.sla_id,
					     rl_root->sla.nodetype))
			return -EFAULT;

	rl_root->sla_added = 0;
	rl_root->sla = (const struct adf_user_sla){ { 0 } };

	return 0;
}

int rl_v2_delete_sla(struct adf_accel_dev *accel_dev, struct adf_user_sla *sla)
{
	int ret = 0;
	enum adf_user_node_type type = RL_GET_NODE_TYPE(sla->sla_id);

	if (!rl_check_id_range(accel_dev, sla->sla_id, type))
		return -EFAULT;

	mutex_lock(&accel_dev->rl_lock);

	switch (type) {
	case ADF_NODE_LEAF:
		ret = rl_remove_sla_from_node(accel_dev,
					      accel_dev->rl_v2->leaf_idr, sla);
		break;
	case ADF_NODE_CLUSTER:
		ret = rl_remove_sla_from_node(accel_dev,
					      accel_dev->rl_v2->cluster_idr,
					      sla);
		break;
	case ADF_NODE_ROOT:
		ret = rl_remove_root_sla_node(accel_dev, sla);
		break;
	}

	mutex_unlock(&accel_dev->rl_lock);

	return ret;
}

static void rl_free_node(struct adf_accel_dev *accel_dev, struct idr *idr_node,
			 struct rl_node_info *node, int type)
{
	struct rl_node_count *count = &accel_dev->rl_v2->node_count;

	if (node->sla_added)
		rl_remove_sla_from_node(accel_dev, idr_node, &node->sla);

	switch (type) {
	case ADF_NODE_CLUSTER:
		if (count->cluster_count == 0) {
			dev_warn(
				&GET_DEV(accel_dev),
				"Rate Limiting: Cluster node count is zero cannot remove node\n");
			return;
		}
		count->cluster_count -= 1;
		break;
	case ADF_NODE_LEAF:
		if (count->leaf_count == 0) {
			dev_warn(
				&GET_DEV(accel_dev),
				"Rate Limiting: Leaf node count is zero cannot remove node\n");
			return;
		}
		count->leaf_count -= 1;
		break;
	default:
		dev_warn(&GET_DEV(accel_dev),
			 "Rate Limiting: Cannot remove node type\n");
		break;
	}

	if (!idr_remove(idr_node, RL_TREEID_TO_NODEID(node->node_id)))
		dev_warn(&GET_DEV(accel_dev),
			 "Rate Limiting: Node IDR does not exist\n");

	kfree(node);
}

static int rl_delete_node(struct adf_accel_dev *accel_dev,
			  struct idr *idr_node, u32 node_id,
			  enum adf_user_node_type type)
{
	u32 leaf_id = 0;
	u32 id = RL_TREEID_TO_NODEID(node_id);
	struct rl_node_info *leaf = NULL;
	struct rl_node_info *find = NULL;

	find = idr_find(idr_node, id);
	if (!find) {
		dev_warn(&GET_DEV(accel_dev),
			 "Rate Limiting: Node not found\n");
		return -EFAULT;
	}

	if (type == ADF_NODE_CLUSTER) {
		idr_for_each_entry(accel_dev->rl_v2->leaf_idr, leaf, leaf_id) {
			if (leaf->sla_added &&
			    leaf->sla.parent_node_id == find->node_id) {
				rl_free_node(accel_dev,
					     accel_dev->rl_v2->leaf_idr, leaf,
					     ADF_NODE_LEAF);
			}
		}
	}

	rl_free_node(accel_dev, idr_node, find, type);

	return 0;
}

int rl_v2_delete_user_node(struct adf_accel_dev *accel_dev,
			   struct adf_user_node *node)
{
	int ret = 0;
	enum adf_user_node_type type = RL_GET_NODE_TYPE(node->node_id);

	mutex_lock(&accel_dev->rl_lock);
	switch (type) {
	case ADF_NODE_LEAF:
		ret = rl_delete_node(accel_dev, accel_dev->rl_v2->leaf_idr,
				     node->node_id, type);
		break;
	case ADF_NODE_CLUSTER:
		ret = rl_delete_node(accel_dev, accel_dev->rl_v2->cluster_idr,
				     node->node_id, type);
		break;
	default:
		dev_err(&GET_DEV(accel_dev),
			"Rate Liming: Not a supported node type\n");
		ret = -EINVAL;
	}
	mutex_unlock(&accel_dev->rl_lock);

	return ret;
}
static bool rl_enough_sla_budget(struct rl_node_info *parent,
				 struct adf_user_sla *sla)
{
	/* Check if less than sla of parent */
	if (parent->rem_cir < sla->cir)
		return false;

	/* Check if less than sla of parent */
	if (parent->max_pir < sla->pir)
		return false;

	return true;
}

static bool rl_enough_root_sla_budget(struct adf_accel_dev *accel_dev,
				      struct rl_node_info *root,
				      struct adf_user_sla *sla)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;

	switch (root->svc_type) {
	case ADF_SVC_ASYM:
		if (sla->pir > hw_data->rl_data.slice_reference ||
		    sla->cir > hw_data->rl_data.slice_reference)
			return false;
		break;
	case ADF_SVC_NONE:
		return false;
	default:
		if (sla->pir > hw_data->rl_data.max_throughput[root->svc_type] ||
		    sla->cir > hw_data->rl_data.max_throughput[root->svc_type])
			return false;
	}

	return true;
}

static void rl_update_req_buffer(struct adf_accel_dev *accel_dev,
				 struct icp_qat_fw_init_admin_req *req,
				 struct adf_user_sla *sla,
				 struct rl_rings_info *fw_rings_info)
{
	req->init_cfg_sz = RL_SLA_CONFIG_SIZE;
	req->node_id = RL_TREEID_TO_NODEID(sla->node_id);
	req->node_type = sla->nodetype;
	req->rp_count = fw_rings_info->num_rings;
	req->svc_type = sla->svc_type;
}

static u32 rl_calculate_slice_tokens(struct adf_accel_dev *accel_dev,
				     enum adf_svc_type svc_type, u32 ir)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u64 avail_slice_cycles = 0;
	u64 allocated_tokens = 0;

	if (!ir)
		return 0;

	/* Numbers of slice cycles over RL_SCANS_PER_SEC */
	avail_slice_cycles = hw_data->clock_frequency;
	avail_slice_cycles *= hw_data->get_slices_for_svc(accel_dev, svc_type);
	avail_slice_cycles /= hw_data->rl_data.scan_interval;

	switch (svc_type) {
	case ADF_SVC_ASYM:
		/* Percent of available tokens allocated */
		allocated_tokens = avail_slice_cycles * ir /
				   hw_data->rl_data.slice_reference;
		break;
	case ADF_SVC_NONE:
		break;
	default:
		allocated_tokens = avail_slice_cycles * ir /
				   hw_data->rl_data.max_throughput[svc_type];
	}

	return allocated_tokens;
}

static u32 rl_calculate_ae_cycles(struct adf_accel_dev *accel_dev,
				  enum adf_svc_type svc_type, u32 ir)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u64 allocated_ae_cycles = 0;
	u64 avail_ae_cycles = 0;

	avail_ae_cycles = hw_data->clock_frequency;
	avail_ae_cycles *= hw_data->get_num_svc_aes(accel_dev, svc_type);
	avail_ae_cycles /= hw_data->rl_data.scan_interval;

	if (!ir)
		return 0;

	switch (svc_type) {
	case ADF_SVC_ASYM:
		/* Scale for asym */
		ir *= hw_data->rl_data.max_throughput[svc_type];
		ir /= hw_data->rl_data.slice_reference;
		allocated_ae_cycles =
			((ir * avail_ae_cycles) /
			 hw_data->rl_data.max_throughput[svc_type]);
		break;
	case ADF_SVC_NONE:
		break;
	default:
		allocated_ae_cycles =
			((ir * avail_ae_cycles) /
			 hw_data->rl_data.max_throughput[svc_type]);
	}

	return allocated_ae_cycles;
}

static u32 rl_calculate_pci_bw(struct adf_accel_dev *accel_dev,
			       enum adf_svc_type svc_type, u32 ir, bool bw_out)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u64 sla_to_bytes = 0;
	u64 allocated_bw = 0;
	u64 sla_scaled = 0;

	if (!ir)
		return 0;

	sla_to_bytes = ir;

	switch (svc_type) {
	case ADF_SVC_ASYM:
		/* Scale for asym */
		sla_to_bytes *= hw_data->rl_data.max_throughput[svc_type];
		sla_to_bytes /= hw_data->rl_data.slice_reference;
		sla_to_bytes *= RL_ASYM_TOKEN_SIZE;
		break;
	case ADF_SVC_NONE:
		break;
	case ADF_SVC_DC:
		sla_to_bytes *= RL_CONVERT_TO_BYTES;
		/*
		 * As higher throughput for decompression
		 * we multiple by (slice count minus correction)
		 */
		if (bw_out)
			sla_to_bytes *= hw_data->get_slices_for_svc(accel_dev,
								    svc_type) -
					hw_data->rl_data.dc_correction;
		break;
	default:
		sla_to_bytes *= RL_CONVERT_TO_BYTES;
	}

	sla_scaled = sla_to_bytes * hw_data->rl_data.pcie_scale_multiplier;
	sla_scaled /= hw_data->rl_data.pcie_scale_divisor;
	allocated_bw = sla_scaled / RL_TOKEN_PCIE_SIZE;
	allocated_bw /= hw_data->rl_data.scan_interval;

	return allocated_bw;
}

static int rl_update_sla_buffer(
	struct adf_accel_dev *accel_dev,
	struct icp_qat_fw_init_admin_sla_config_params *fw_config_params,
	struct adf_user_sla *sla, struct rl_rings_info *fw_rings_info)
{
	u32 i = 0;

	fw_config_params->pcie_in_cir =
		rl_calculate_pci_bw(accel_dev, sla->svc_type, sla->cir, false);
	fw_config_params->pcie_out_cir =
		rl_calculate_pci_bw(accel_dev, sla->svc_type, sla->cir, true);
	fw_config_params->pcie_in_pir =
		rl_calculate_pci_bw(accel_dev, sla->svc_type, sla->pir, false);
	fw_config_params->pcie_out_pir =
		rl_calculate_pci_bw(accel_dev, sla->svc_type, sla->pir, true);

	fw_config_params->slice_util_cir =
		rl_calculate_slice_tokens(accel_dev, sla->svc_type, sla->cir);
	fw_config_params->slice_util_pir =
		rl_calculate_slice_tokens(accel_dev, sla->svc_type, sla->pir);

	fw_config_params->ae_util_cir =
		rl_calculate_ae_cycles(accel_dev, sla->svc_type, sla->cir);
	fw_config_params->ae_util_pir =
		rl_calculate_ae_cycles(accel_dev, sla->svc_type, sla->pir);

	if (fw_config_params->pcie_in_cir == 0 ||
	    fw_config_params->slice_util_cir == 0 ||
	    fw_config_params->ae_util_cir == 0) {
		dev_err(&GET_DEV(accel_dev),
			"Rate Limiting: SLA value too low\n");
		return -EINVAL;
	}

	if (sla->nodetype == ADF_NODE_LEAF) {
		for (i = 0; i < fw_rings_info->num_rings; i++)
			fw_config_params->rp_ids[i] = fw_rings_info->rp_ids[i];
	}

	return 0;
}

static int rl_send_add_update_sla(struct adf_accel_dev *accel_dev,
				  struct adf_user_sla *sla,
				  enum icp_qat_fw_init_admin_cmd_id cmd)
{
	int ret = 0;
	u32 i = 0, ring = 0;
	struct rl_rings_info fw_rings_info = { 0 };
	struct icp_qat_fw_init_admin_resp rsp = { 0 };
	struct icp_qat_fw_init_admin_req req = { 0 };
	u32 ae_mask = accel_dev->hw_device->admin_ae_mask;
	struct icp_qat_fw_init_admin_sla_config_params *fw_config_params = NULL;
	dma_addr_t dma;
	struct pci_dev *pdev = accel_to_pci_dev(accel_dev);

	if (sla->nodetype == ADF_NODE_LEAF) {
		for (i = 0; i < accel_dev->hw_device->num_banks_per_vf; i++) {
			if (rl_set_ring_for_svc(accel_dev, sla, i, &ring)) {
				fw_rings_info.rp_ids[fw_rings_info.num_rings] =
					ring;
				fw_rings_info.num_rings++;
			}
		}
	}

	req.cmd_id = cmd;

	fw_config_params = dma_alloc_coherent(
		&pdev->dev,
		sizeof(struct icp_qat_fw_init_admin_sla_config_params), &dma,
		GFP_KERNEL);
	if (!fw_config_params)
		return -ENOMEM;

	memset(fw_config_params, 0,
		  sizeof(struct icp_qat_fw_init_admin_sla_config_params));

	req.init_cfg_ptr = dma;

	rl_update_req_buffer(accel_dev, &req, sla, &fw_rings_info);

	if (rl_update_sla_buffer(accel_dev, fw_config_params, sla,
				 &fw_rings_info)) {
		dma_free_coherent(
			&GET_DEV(accel_dev),
			sizeof(struct icp_qat_fw_init_admin_sla_config_params),
			fw_config_params, dma);
		return -EINVAL;
	}

	if (adf_send_admin(accel_dev, &req, &rsp, ae_mask)) {
		dev_err(&GET_DEV(accel_dev),
			"Rate Limiting: Add/Update SLA failed\n");
		ret = -EFAULT;
	}

	dma_free_coherent(
		&GET_DEV(accel_dev),
		sizeof(struct icp_qat_fw_init_admin_sla_config_params),
		fw_config_params, dma);

	return ret;
}

static int rl_update_add_sla(struct adf_accel_dev *accel_dev,
			     struct rl_node_info *new_node,
			     struct adf_user_sla *sla)
{
	struct rl_node_count *rl_count =
		&accel_dev->rl_v2->node_count;

	/* If no SLA */
	if (!new_node->sla_added) {
		if (rl_send_add_update_sla(accel_dev, sla, ICP_QAT_FW_RL_ADD))
			return -EFAULT;

		switch (new_node->nodetype) {
		case ADF_NODE_LEAF:
			rl_count->sla_count_leaf += 1;
			break;
		case ADF_NODE_CLUSTER:
			rl_count->sla_count_cluster += 1;
			break;
		case ADF_NODE_ROOT:
			rl_count->sla_count_root += 1;
			break;
		}
	} else {
		if (rl_send_add_update_sla(accel_dev, sla, ICP_QAT_FW_RL_UPDATE))
			return -EFAULT;
	}

	/* Update the SLA */
	new_node->sla_added = true;
	memcpy(&new_node->sla, sla, sizeof(struct adf_user_sla));

	return 0;
}

static int rl_hw_configure_tree(struct adf_accel_dev *accel_dev,
				struct rl_node_info *new_node,
				struct adf_user_sla *sla)
{
	int ret = 0;
	u32 ring = 0, i = 0;

	switch (new_node->nodetype) {
	case ADF_NODE_CLUSTER: {
		struct rl_node_info *rl_root = NULL;

		/* Cluster node is directly connected to Service/Root node */
		rl_root = adf_rl_get_root_node_locked(accel_dev, sla->svc_type);
		if (!rl_root)
			return -ENOENT;
		rl_link_node_to_parent(accel_dev, new_node->node_id,
				       rl_root->node_id, ADF_NODE_ROOT);
		break;
	}
	case ADF_NODE_LEAF:
		for (i = 0; i < accel_dev->hw_device->num_banks_per_vf; i++) {
			if (rl_set_ring_for_svc(accel_dev, sla, i, &ring))
				rl_link_node_to_parent(accel_dev, ring,
						       new_node->node_id,
						       ADF_NODE_LEAF);
		}
		/* Cluster Leaf belongs to */
		rl_link_node_to_parent(accel_dev, sla->node_id,
				       sla->parent_node_id, ADF_NODE_CLUSTER);
		break;
	case ADF_NODE_ROOT:
		break;
	default:
		dev_err(&GET_DEV(accel_dev),
			"Rate Limiting: Not a valid node type\n");
		return -EFAULT;
	}

	/* Update the sla_id to be the same as the node_id */
	sla->sla_id = sla->node_id;

	ret = rl_update_add_sla(accel_dev, new_node, sla);

	if (ret) {
		rl_hw_unconfigure_tree(accel_dev, new_node, sla);
		sla->sla_id = 0;
	}

	return ret;
}

static void rl_check_pir_to_cir(struct adf_accel_dev *accel_dev,
				struct adf_user_sla *sla)
{
	/* User can't specify PIR less than CIR */
	if (sla->pir < sla->cir) {
		sla->pir = sla->cir;
		dev_warn(&GET_DEV(accel_dev),
			 "Rate Limiting: PIR must be >= CIR, setting PIR to CIR automatically\n");
	}
}

/*
 * Adds node in the RL tree after configuring hw registers,
 * and checking SLA budget. "parent" is the node to which
 * new_node will be attached as a child.
 */
static int rl_add_node_intree(struct adf_accel_dev *accel_dev,
			      struct rl_node_info *new_node,
			      struct adf_user_sla *sla)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u32 max_valid =
		RL_VALIDATE_RET_MAX(sla->svc_type,
		                    hw_data->rl_data.slice_reference,
				    hw_data->rl_data.max_throughput[sla->svc_type]);
	struct rl_node_info *parent = NULL;


	if (RL_VALIDATE_IR(sla->cir, max_valid) ||
	    RL_VALIDATE_IR(sla->pir, max_valid)) {
		dev_err(&GET_DEV(accel_dev),
			"Rate Limiting: User input out of range\n");
		return -EINVAL;
	}

	switch (new_node->nodetype) {
	case ADF_NODE_CLUSTER:
		parent = adf_rl_get_root_node_locked(accel_dev, sla->svc_type);
		if (!parent)
			return -ENOENT;

		if (!rl_check_id_range(accel_dev, parent->node_id,
				       ADF_NODE_ROOT)) {
			return -EINVAL;
		}

		break;
	case ADF_NODE_LEAF:
		if (!rl_check_id_range(accel_dev, sla->parent_node_id,
				       ADF_NODE_CLUSTER)) {
			return -EINVAL;
		}
		parent = idr_find(accel_dev->rl_v2->cluster_idr,
				  RL_TREEID_TO_NODEID(sla->parent_node_id));
		break;
	default:
		dev_err(&GET_DEV(accel_dev),
			"Rate Limiting: Unsupported node\n");
		return -EINVAL;
	}

	if (!parent) {
		dev_err(&GET_DEV(accel_dev),
			"Rate Limiting: No parent, check NodeID and/or Service type\n");
		return -EFAULT;
	}

	/* Parent's SLA is first to be set */
	if (!parent->sla_added) {
		dev_err(&GET_DEV(accel_dev),
			"Rate Limiting: SLA for parent not added, won't proceed\n");
		return -EPERM;
	}

	/* Get SLA feasibility, sum of sla of children is less parent's */
	if (!rl_enough_sla_budget(parent, sla)) {
		dev_err(&GET_DEV(accel_dev),
			"Rate Limiting: No SLA budget\n");
		return -EPERM;
	}

	rl_check_pir_to_cir(accel_dev, sla);

	if (rl_hw_configure_tree(accel_dev, new_node, sla)) {
		dev_err(&GET_DEV(accel_dev),
			"Rate Limiting: Could not configure, won't add in tree\n");
		return -EFAULT;
	}

	/* Add to tree */
	new_node->parent = parent;
	new_node->rem_cir = sla->cir;
	new_node->max_pir = sla->pir;

	/* Compute remaining sla after spending */
	parent->rem_cir -= sla->cir;

	return 0;
}

/*
 * Looks up the node in the respective IDR.
 * Performs basic checks on node type and sla params given by user before
 * proceeding to add to the tree. Checks if sla is already added and determines
 * whether to do update or add sla for the first time.
 */
static int rl_add_sla_2_node(struct adf_accel_dev *accel_dev, struct idr *idr,
			     struct adf_user_sla *sla)
{
	u32 id;
	struct rl_node_info *find;

	id = RL_TREEID_TO_NODEID(sla->node_id);

	find = idr_find(idr, id);
	if (!find) {
		dev_err(&GET_DEV(accel_dev),
			"Rate Limiting: Node not found, please add a Node first\n");
		return -EFAULT;
	}

	if (find->sla_added) {
		dev_err(&GET_DEV(accel_dev),
			"Rate limiting: Node already has an SLA associated with it\n");
		return -EFAULT;
	}

	if (find->svc_type != sla->svc_type) {
		dev_err(&GET_DEV(accel_dev),
			"Rate limiting: Service type of SLA and node doesn't match\n");
		return -EFAULT;
	}

	if (find->nodetype != sla->nodetype) {
		dev_err(&GET_DEV(accel_dev),
			"Rate Limiting: Node type of SLA and node do not match\n");
		return -EFAULT;
	}

	return rl_add_node_intree(accel_dev, find, sla);
}

static bool rl_parent_afford_child_sla(struct rl_node_info *parent,
				       struct rl_node_info *node,
				       struct adf_user_sla *sla)
{
	u32 new_rem_cir = parent->rem_cir + node->sla.cir;

	if (new_rem_cir < sla->cir || parent->max_pir < sla->pir)
		return false;

	return true;
}

static bool rl_node_afford_sla_update(struct rl_node_info *node,
				      struct adf_user_sla *sla)
{
	u32 afford_update_cir = node->sla.cir - node->rem_cir;

	if (afford_update_cir <= sla->cir)
		return true;

	return false;
}

/*
 * Determines if sla can be updated. We get the parent node, subtract the
 * current sla of the child node and then call/re-use "rl_check_sla_budget"
 * by passing the new sla which has new cirs/pirs. If this passes, then we can
 * afford the new sla and "rl_update_add_sla" is called that sends the admin
 * command. If that's a success, then spent sla of parent
 */
static int rl_update_sla(struct adf_accel_dev *accel_dev,
			 struct rl_node_info *node, struct adf_user_sla *sla)
{
	struct rl_node_info *parent = node->parent;

	switch (node->nodetype) {
	case ADF_NODE_CLUSTER:
		if (rl_parent_afford_child_sla(parent, node, sla)) {
			if (rl_node_afford_sla_update(node, sla)) {
				if (rl_update_add_sla(accel_dev, node, sla))
					return -EFAULT;
				return 0;
			}
		}
		break;
	case ADF_NODE_LEAF:
		if (rl_parent_afford_child_sla(parent, node, sla)) {
			if (rl_update_add_sla(accel_dev, node, sla))
				return -EFAULT;
			return 0;
		}
		break;
	case ADF_NODE_ROOT:
		if (rl_node_afford_sla_update(node, sla)) {
			if (rl_update_add_sla(accel_dev, node, sla))
				return -EFAULT;
			return 0;
		}
		break;
	default:
		break;
	}

	dev_err(&GET_DEV(accel_dev),
		"Rate Limiting: Update SLA failed, cannot afford\n");

	return -EFAULT;
}

/*
 * Update path has two possibilities:
 * Change sla pir/cir values or migrate the
 * node to a different parent.
 */
static int rl_update_if_sla_added(struct adf_accel_dev *accel_dev,
				  struct rl_node_info *node,
				  struct adf_user_sla *sla)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u32 max_valid =
		RL_VALIDATE_RET_MAX(sla->svc_type,
				    hw_data->rl_data.slice_reference,
				    hw_data->rl_data.max_throughput[sla->svc_type]);
	u32 old_cir = 0;


	if (RL_VALIDATE_IR(sla->cir, max_valid) ||
	    RL_VALIDATE_IR(sla->pir, max_valid)) {
		dev_err(&GET_DEV(accel_dev),
			"Rate Limiting: User input out of range\n");
		return -EINVAL;
	}

	if (sla->nodetype == ADF_NODE_ROOT ||
	    sla->parent_node_id == node->parent->node_id) {
		old_cir = node->sla.cir;

		if (node->sla_added) {
			rl_check_pir_to_cir(accel_dev, sla);
			if (rl_update_sla(accel_dev, node, sla)) {
				dev_err(&GET_DEV(accel_dev),
					"Can't update SLA\n");
				return -EPERM;
			}
			/* Update parent for leaf/cluster */
			if (node->parent) {
				node->parent->rem_cir += (old_cir - sla->cir);
				node->rem_cir += (node->sla.cir - old_cir);
				node->max_pir = sla->pir;
			} else {
				node->rem_cir = sla->cir;
				node->max_pir = sla->pir;
			}

			return 0;
		}

		dev_err(&GET_DEV(accel_dev), "Can't update SLA\n");
		return -EPERM;
	}
	return 0;
}

/*
 * Updates an SLA  using the sla->sla_id
 */
int rl_v2_update_sla(struct adf_accel_dev *accel_dev, struct adf_user_sla *sla)
{
	u32 id = 0;
	int ret = 0;
	struct rl_node_info *find = NULL;
	struct rl_node_info *rl_root = NULL;

	sla->nodetype = RL_GET_NODE_TYPE(sla->sla_id);

	if (!rl_check_id_range(accel_dev, sla->sla_id, sla->nodetype))
		return -EFAULT;

	mutex_lock(&accel_dev->rl_lock);

	switch (sla->nodetype) {
	case ADF_NODE_LEAF:
	case ADF_NODE_CLUSTER:
		id = RL_TREEID_TO_NODEID(sla->sla_id);

		if (sla->nodetype == ADF_NODE_LEAF)
			find = idr_find(accel_dev->rl_v2->leaf_idr, id);
		else if (sla->nodetype == ADF_NODE_CLUSTER)
			find = idr_find(accel_dev->rl_v2->cluster_idr, id);
		if (!find || !find->sla_added) {
			dev_err(&GET_DEV(accel_dev),
				"Rate Limiting: Node/SLA not found, please add a Node/SLA first\n");
			ret = -EFAULT;
			goto free_mutex;
		}

		sla->parent_node_id = find->parent->node_id;
		sla->svc_type = find->sla.svc_type;
		sla->pci_addr.dev = find->sla.pci_addr.dev;
		sla->pci_addr.func = find->sla.pci_addr.func;
		sla->node_id = find->node_id;

		ret = rl_update_if_sla_added(accel_dev, find, sla);

		break;
	case ADF_NODE_ROOT:
		rl_root = adf_rl_get_root_node_locked(accel_dev, sla->svc_type);
		if (!rl_root) {
			dev_err(&GET_DEV(accel_dev),
				"Rate Limiting: Root node is not found\n");
			ret = -EFAULT;
			goto free_mutex;
		}
		if (!rl_root->sla_added) {
			dev_err(&GET_DEV(accel_dev),
				"Rate Limiting: Root SLA not found, please add an SLA first\n");
			ret = -EFAULT;
			goto free_mutex;
		}
		if (!rl_enough_root_sla_budget(accel_dev, rl_root, sla)) {
			dev_err(&GET_DEV(accel_dev),
				"Rate limiting: Out of sla budget\n");
			ret = -EPERM;
			goto free_mutex;
		}
		sla->node_id = sla->sla_id;

		ret = rl_update_if_sla_added(accel_dev, rl_root, sla);

		break;
	default:
		dev_err(&GET_DEV(accel_dev),
			"Rate limiting: Update, not a valid node type\n");
		ret = -EFAULT;
		break;
	}

free_mutex:
	mutex_unlock(&accel_dev->rl_lock);

	return ret;
}

/*
 * Adds an SLA to the specified node (node_id).
 * Without adding the SLA, the node is neither configured
 * at hw level nor is it a part of RL tree maintained
 * by driver
 */
int rl_v2_create_sla(struct adf_accel_dev *accel_dev, struct adf_user_sla *sla)
{
	int ret = 0;
	struct rl_node_info *rl_root = NULL;

	if (RL_VALIDATE_PCI_FUNK(sla)) {
		dev_err(&GET_DEV(accel_dev),
			"Rate Limiting: User input out of range\n");
		return -EINVAL;
	}

	if (sla->svc_type >= ADF_SVC_NONE)
		return -EINVAL;

	mutex_lock(&accel_dev->rl_lock);

	rl_root = adf_rl_get_root_node_locked(accel_dev, sla->svc_type);
	if (!rl_root) {
		mutex_unlock(&accel_dev->rl_lock);
		dev_err(&GET_DEV(accel_dev),
			"Rate limiting: Unsupported service type for create SLA\n");
		return -ENOENT;
	}

	if (!rl_check_id_range(accel_dev, sla->node_id, sla->nodetype)) {
		mutex_unlock(&accel_dev->rl_lock);
		return -EFAULT;
	}

	switch (sla->nodetype) {
	case ADF_NODE_LEAF:
		ret = rl_add_sla_2_node(accel_dev, accel_dev->rl_v2->leaf_idr,
					sla);
		break;
	case ADF_NODE_CLUSTER:
		ret = rl_add_sla_2_node(accel_dev,
					accel_dev->rl_v2->cluster_idr, sla);
		break;
	case ADF_NODE_ROOT:
		if (rl_root->sla_added) {
			mutex_unlock(&accel_dev->rl_lock);
			dev_err(&GET_DEV(accel_dev),
				"Rate limiting: Node already has an SLA associated with it\n");
			return -EFAULT;
		}
		if (!rl_enough_root_sla_budget(accel_dev, rl_root, sla)) {
			mutex_unlock(&accel_dev->rl_lock);
			dev_err(&GET_DEV(accel_dev),
				"Rate limiting: Out of SLA budget\n");
			return -EPERM;
		}

		if (rl_hw_configure_tree(accel_dev, rl_root, sla)) {
			mutex_unlock(&accel_dev->rl_lock);
			dev_err(&GET_DEV(accel_dev),
				"Rate Limiting: Couldn't add SLA for root\n");
			return -EFAULT;
		}

		rl_root->rem_cir = sla->cir;
		rl_root->max_pir = sla->pir;
		break;
	default:
		dev_err(&GET_DEV(accel_dev),
			"Rate limiting: Not a valid node type\n");
		break;
	}

	mutex_unlock(&accel_dev->rl_lock);

	return ret;
}

/* Initialise RL */
int adf_rl_v2_init(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct adf_bar *pmisc =
			&GET_BARS(accel_dev)[hw_data->get_misc_bar_id(hw_data)];
	void __iomem *pmisc_addr = pmisc->virt_addr;

	if (rl_validate_hw_data(accel_dev))
		return -EINVAL;

	if (rl_send_admin_init(accel_dev))
		return -EFAULT;

	if (rl_alloc_root_structs(accel_dev))
		return -ENOMEM;

	/* Initialise the root data */
	if (rl_init_root(accel_dev))
		return -EFAULT;

	/* Initialise idr's */
	idr_init(accel_dev->rl_v2->cluster_idr);
	idr_init(accel_dev->rl_v2->leaf_idr);

	/* Write PCIe_in and PCIe_out token bucket granularity */
	ADF_CSR_WR(pmisc_addr, hw_data->rl_data.pcie_in_bucket_offset,
		   RL_TOKEN_GRANULARITY_PCIEIN_BUCKET);
	ADF_CSR_WR(pmisc_addr, hw_data->rl_data.pcie_out_bucket_offset,
		   RL_TOKEN_GRANULARITY_PCIEOUT_BUCKET);

	/* RL V_1 phase 1 default settings */
	if (rl_init_v1_tree(accel_dev)) {
		adf_rl_v2_exit(accel_dev);
		return -EFAULT;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(adf_rl_v2_init);

/* Remove RL */
void adf_rl_v2_exit(struct adf_accel_dev *accel_dev)
{
	u32 node_id = 0;
	struct rl_node_info *node = NULL;
	struct adf_user_node user_node = { { 0 } };

	if (accel_dev->rl_v2) {
		if (accel_dev->rl_v2->leaf_idr) {
			idr_for_each_entry(accel_dev->rl_v2->leaf_idr, node,
					    node_id) {
				user_node.node_id = node->node_id;
				rl_v2_delete_user_node(accel_dev, &user_node);
			}
		}

		if (accel_dev->rl_v2->cluster_idr) {
			idr_for_each_entry(accel_dev->rl_v2->cluster_idr, node,
					    node_id) {
				user_node.node_id = node->node_id;
				rl_v2_delete_user_node(accel_dev, &user_node);
			}
		}

		if (accel_dev->rl_v2->cluster_idr)
			idr_destroy(accel_dev->rl_v2->cluster_idr);

		if (accel_dev->rl_v2->leaf_idr)
			idr_destroy(accel_dev->rl_v2->leaf_idr);

		rl_free_root_structs(accel_dev);
	}
}
EXPORT_SYMBOL_GPL(adf_rl_v2_exit);
