// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2024 Intel Corporation */

#include "adf_accel_devices.h"
#include "adf_rl.h"
#include "icp_qat_fw_init_admin.h"
#include "adf_common_drv.h"

#define RL_CSR_WIDTH (4U)
void rl_link_node_to_parent(struct adf_accel_dev *accel_dev, u32 node_id,
			    u32 parent_id, enum adf_user_node_type parent_type)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct adf_bar *pmisc =
		&GET_BARS(accel_dev)[hw_data->get_misc_bar_id(hw_data)];
	void __iomem *pmisc_addr = pmisc->virt_addr;
	bool write_is_required = false;
	u32 base_offset;

	switch (parent_type) {
	case ADF_NODE_ROOT:
		base_offset = hw_data->rl_data.cluster_to_service_offset;
		write_is_required = true;
		break;
	case ADF_NODE_CLUSTER:
		base_offset = hw_data->rl_data.leaf_to_cluster_offset;
		write_is_required = true;
		break;
	case ADF_NODE_LEAF:
		/*
		 * v2 API is using ring index as node_id, but conversion is
		 * still possible.
		 */
		base_offset = hw_data->rl_data.ring_to_leaf_offset;
		write_is_required = true;
		break;
	}

	if (write_is_required)
		ADF_CSR_WR(pmisc_addr,
			   base_offset +
				   RL_CSR_WIDTH * RL_TREEID_TO_NODEID(node_id),
			   RL_TREEID_TO_NODEID(parent_id));
}

enum adf_cfg_service_type rl_adf_svc_to_cfg_svc(enum adf_svc_type adf_svc)
{
	switch (adf_svc) {
	case ADF_SVC_ASYM:
		return ASYM;
	case ADF_SVC_SYM:
		return SYM;
	case ADF_SVC_DC:
		return COMP;
	default:
		return NA;
	}
	return NA;
}

enum adf_svc_type rl_cfg_svc_to_adf_svc(enum adf_cfg_service_type svc)
{
	switch (svc) {
	case ASYM:
		return ADF_SVC_ASYM;
	case SYM:
		return ADF_SVC_SYM;
	case COMP:
		return ADF_SVC_DC;
	default:
		return ADF_SVC_NONE;
	}

	return ADF_SVC_NONE;
}

bool rl_is_svc_enabled(struct adf_accel_dev *accel_dev,
		       enum adf_svc_type adf_svc)
{
	u8 serv_type = NA;
	u32 bank = 0;

	/*
	 * Ring mapping is same across bundles/VFs. Only need to check
	 * single bundle (have num_banks_per_vf RPs) for supported service.
	 */
	for (bank = 0; bank < accel_dev->hw_device->num_banks_per_vf; bank++) {
		serv_type = GET_SRV_TYPE(accel_dev->hw_device->ring_to_svc_map,
					 bank);

		if (rl_cfg_svc_to_adf_svc(serv_type) == adf_svc)
			return true;
	}
	return false;
}

int rl_send_admin_init(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct rl_slice_cnt *slices = &hw_data->rl_data.slice_cnt;
	struct icp_qat_fw_init_admin_resp rsp = { 0 };
	struct icp_qat_fw_init_admin_req req = { 0 };
	u32 ae_mask = hw_data->admin_ae_mask;

	req.cmd_id = ICP_QAT_FW_RL_INIT;

	if (adf_send_admin(accel_dev, &req, &rsp, ae_mask)) {
		dev_err(&GET_DEV(accel_dev), "Rate Limiting: Not supported\n");
		return -EFAULT;
	}

	/* Use FW response to populate slice information for device */
	slices->cpr_slice_cnt = rsp.slice_count.cpr_slice_cnt;
	slices->xlt_slice_cnt = rsp.slice_count.xlt_slice_cnt;
	slices->dcpr_slice_cnt = rsp.slice_count.dcpr_slice_cnt;
	slices->pke_slice_cnt = rsp.slice_count.pke_slice_cnt;
	slices->wat_slice_cnt = rsp.slice_count.wat_slice_cnt;
	slices->wcp_slice_cnt = rsp.slice_count.wcp_slice_cnt;
	slices->ucs_slice_cnt = rsp.slice_count.ucs_slice_cnt;
	slices->cph_slice_cnt = rsp.slice_count.cph_slice_cnt;
	slices->ath_slice_cnt = rsp.slice_count.ath_slice_cnt;

	return 0;
}

int rl_send_admin_delete_sla(struct adf_accel_dev *accel_dev, u16 id, u8 type)
{
	u32 ae_mask = accel_dev->hw_device->admin_ae_mask;
	struct icp_qat_fw_init_admin_resp rsp = { 0 };
	struct icp_qat_fw_init_admin_req req = { 0 };

	req.cmd_id = ICP_QAT_FW_RL_REMOVE;
	req.node_id = RL_TREEID_TO_NODEID(id);
	req.node_type = type;

	if (adf_send_admin(accel_dev, &req, &rsp, ae_mask)) {
		dev_err(&GET_DEV(accel_dev),
			"Rate Limiting: Delete SLA failed\n");
		return -EFAULT;
	}

	return 0;
}

int rl_validate_hw_data(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u32 i;

	if (!hw_data->get_slices_for_svc) {
		dev_err(&GET_DEV(accel_dev),
			"Rate Limiting: Not supported - unable to get information about slices for service\n");
		return -EINVAL;
	}

	if (!hw_data->get_num_svc_aes) {
		dev_err(&GET_DEV(accel_dev),
			"Rate Limiting: Not supported - unable to get information about AEs for service\n");
		return -EINVAL;
	}

	if (RL_VALIDATE_NON_ZERO(hw_data->rl_data.scan_interval) ||
	    RL_VALIDATE_NON_ZERO(hw_data->rl_data.slice_reference) ||
	    RL_VALIDATE_NON_ZERO(hw_data->rl_data.pcie_scale_divisor) ||
	    RL_VALIDATE_NON_ZERO(hw_data->rl_data.pcie_scale_multiplier)) {
		dev_err(&GET_DEV(accel_dev),
			"Rate Limiting: Zero divide input error\n");
		return -EINVAL;
	}

	for (i = 0; i < ADF_SVC_NONE; i++) {
		if (!rl_is_svc_enabled(accel_dev, i))
			continue;
		if (RL_VALIDATE_NON_ZERO(hw_data->rl_data.max_throughput[i])) {
			dev_err(&GET_DEV(accel_dev),
				"Rate Limiting: Zero divide input error\n");
			return -EINVAL;
		}
	}

	return 0;
}
