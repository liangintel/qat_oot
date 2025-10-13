/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2017, 2019 - 2020, 2022 Intel Corporation */
#ifndef ADF_SLA_USER_H_
#define ADF_SLA_USER_H_

#include "adf_cfg_common.h"

#define ADF_MAX_SLA 64

/* Rate limiting 2 node type */
enum adf_user_node_type {
	ADF_NODE_ROOT = 0x0,
	ADF_NODE_CLUSTER = 0x1,
	ADF_NODE_LEAF = 0x2
};

/*
 *
 * @ingroup sla
 *
 * struct adf_user_service - For a given service, specifies the max
 * rate the device can sustain and the actual available rate, that is,
 * not yet allocated.
 *
 * @svc_type:                service type
 * @max_svc_rate_in_slau:    maximum rate defined in sla units
 * @avail_svc_rate_in_slau:  available rate defined in sla units
 */
struct adf_user_service {
	enum adf_svc_type svc_type;
	u32 max_svc_rate_in_slau;
	u32 avail_svc_rate_in_slau;
};

/*
 *
 * @ingroup sla
 *
 * struct adf_user_sla_caps - For a given device, specifies the maximum
 * number of SLAs, the number of SLAs still available and the number of SLAs
 * already allocated. Also, for each service, it provides details about
 * the rate still available.
 *
 * @pf_addr:        BDF address of physical function for this device
 * @max_slas:       maximum number of SLAs supported on this device
 * @avail_slas:     number of SLAs still available
 * @used_slas:      number of SLAs already allocated
 *
 * @services:       for each service type, provides details about the rate still
 *                  available
 */
struct adf_user_sla_caps {
	struct adf_pci_address pf_addr;
	u16 max_slas;
	u16 avail_slas;
	u16 used_slas;
	struct adf_user_service services[ADF_MAX_SERVICES];
	/* RL_V2 specific */
	u32 num_services;
	u32 max_root;
	u32 max_cluster;
	u32 max_leaf;
	/* End RL_V2 specific */
};

/*
 *
 * @ingroup sla
 *
 * struct adf_user_sla - parameters required to request an SLA
 *
 * @pci_addr:       For IOCTL_SLA_CREATE this will be the BDF address of the
 *                  virtual function. For IOCTL_SLA_UPDATE/IOCTL_SLA_DELETE this
 *                  will be the BDF address of the physical function to which
 *                  the VF belongs to
 * @sla_id:         For IOCTL_SLA_CREATE this is an output parameter. Kernel
 *                  will populate this with the sla_id which is device specific.
 *                  User has to keep track of both pf_addr and sla_id to later
 *                  update/delete the sla.
 *                  For IOCTL_SLA_DELETE/IOCTL_SLA_UPDATE this is an input
 *                  parameter that paired with pci_addr set to the PF BDF, will
 *                  uniquely identify the SLA system wide
 * @svc_type:       service type to request SLA for
 * @rate_in_slau:   rate requested in sla units. Must be lower or equal
 *                  to adf_user_sla_caps.services[svc_type].
 *                  avail_svc_rate_in_slau
 * @nodetype:       Which type of SLA to create root/cluster/leaf
 * @node_id:        Specifies which node a new SLA is attatched to.
 * @parent_node_id: For leaf sla, specifies which cluster node to attach to.
 * @cir:            C rate
 * @pir:            Peak rate
 */
struct adf_user_sla {
	struct adf_pci_address pci_addr;
	u16 sla_id;
	enum adf_svc_type svc_type;
	u32 rate_in_slau;
	/* RL_V2 specific */
	enum adf_user_node_type nodetype;
	u32 node_id;
	u32 parent_node_id;
	u32 cir;
	u32 pir;
	/* End RL_V2 specific */
};

/*
 *
 * @ingroup sla
 *
 * struct adf_user_slas - to be used with IOCTL_SLA_GET_LIST to retrieve the
 * list of allocated SLAs.
 *
 * @pf_addr:    BDF address of physical function for this device
 * @slas:       array of allocated SLAs.
 * @used_slas:  actual number of SLA allocated. Entries in slas from 0 to
 *              used_slas are valid.
 */
struct adf_user_slas {
	struct adf_pci_address pf_addr;
	struct adf_user_sla slas[ADF_MAX_SLA];
	u16 used_slas;
};

/*
 * @ingroup sla
 *
 * struct adf_user_node - structure to track a node with PF
 *
 * @pf_addr:  BDF address of physical function for this device.
 * @nodetype: Node type, root, cluster, leaf.
 * @svc_type: Type of service you want node to be associated with.
 * @node_id:  This will contain the node_id for the new node, once created.
 */
struct adf_user_node {
	struct adf_pci_address pf_addr;
	enum adf_user_node_type nodetype;
	enum adf_svc_type svc_type;
	u32 node_id;
};

#endif /* ADF_SLA_USER_H_ */
