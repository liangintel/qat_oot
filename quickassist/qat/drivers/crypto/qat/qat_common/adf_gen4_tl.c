// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2021 Intel Corporation */

#include "adf_accel_devices.h"
#include "icp_qat_fw_init_admin.h"
#include "adf_common_drv.h"
#include "adf_gen4_tl.h"
#include <linux/sysfs.h>


static int adf_gen4_alloc_mem_tl(struct adf_accel_dev *accel_dev)
{
	struct adf_telemetry *tl = NULL;
	u32 tl_data_size = sizeof(struct adf_tl_data_regs);
	struct device *dev = &GET_DEV(accel_dev);

	tl = kzalloc(sizeof(*tl), GFP_KERNEL);
	if (!tl)
		goto free_tl_mem;

	tl->virt_addr =
		dma_alloc_coherent(dev,
				   PAGE_ALIGN(tl_data_size),
				   &tl->phy_addr,
				   GFP_KERNEL);
	if (!tl->virt_addr)
		goto free_tl_mem;
	memset(tl->virt_addr, 0, PAGE_ALIGN(tl_data_size));

	tl->rp_num_indexes[TL_RP_0_DATA_INDEX] = TL_RP_0_DEFAULT_NUM;
	tl->rp_num_indexes[TL_RP_1_DATA_INDEX] = TL_RP_1_DEFAULT_NUM;
	tl->rp_num_indexes[TL_RP_2_DATA_INDEX] = TL_RP_2_DEFAULT_NUM;
	tl->rp_num_indexes[TL_RP_3_DATA_INDEX] = TL_RP_3_DEFAULT_NUM;

	accel_dev->telemetry = tl;

	return 0;

free_tl_mem:
	dev_err(dev, "Failed to allocate memory for telemetry.\n");
	kfree(tl);

	return -ENOMEM;
}

static void adf_gen4_free_mem_tl(struct adf_accel_dev *accel_dev)
{
	u32 tl_data_size = sizeof(struct adf_tl_data_regs);
	struct device *dev = &GET_DEV(accel_dev);

	if (accel_dev->telemetry) {
		dma_free_coherent(dev, PAGE_ALIGN(tl_data_size),
				  accel_dev->telemetry->virt_addr,
				  accel_dev->telemetry->phy_addr);

		kfree(accel_dev->telemetry);
		accel_dev->telemetry = NULL;
	}
}

static int adf_gen4_validate_slice_cnt(u8 *slice_cnt_data, u32 slice_type_cnt)
{
	u32 counter = 0;
	int ret = 0;

	for (counter = 0; counter < slice_type_cnt; counter++) {
		if (slice_cnt_data[counter] > HW_MAX_NUM_OF_SLICES) {
			ret = -EFAULT;
			break;
		}
	}

	return ret;
}

static int adf_gen4_stop_tl(struct adf_accel_dev *accel_dev)
{
	struct icp_qat_fw_init_admin_req req = { 0 };
	struct icp_qat_fw_init_admin_resp rsp = { 0 };
	u32 ae_mask = accel_dev->hw_device->admin_ae_mask;

	if (!accel_dev->telemetry)
		return -ENOMEM;

	accel_dev->telemetry->state = TL_OFF;

	if (!accel_dev->admin)
		return -EFAULT;

	req.cmd_id = ICP_QAT_FW_TL_STOP;
	if (adf_send_admin(accel_dev, &req, &rsp, ae_mask))
		return -EFAULT;

	if (accel_dev->hw_device->switch_drv_active)
		accel_dev->hw_device->switch_drv_active(accel_dev);

	return 0;
}

static int adf_gen4_start_tl(struct adf_accel_dev *accel_dev)
{
	struct icp_qat_fw_init_admin_req req = { 0 };
	struct icp_qat_fw_init_admin_resp rsp = { 0 };
	u32 ae_mask = accel_dev->hw_device->admin_ae_mask;
	int ret = 0;

	if (!accel_dev->admin)
		return -EFAULT;

	if (!accel_dev->telemetry)
		return -ENOMEM;

	req.cmd_id = ICP_QAT_FW_TL_START;
	req.init_cfg_ptr = (u64)accel_dev->telemetry->phy_addr;
	req.rp_num_index_0 =
		accel_dev->telemetry->rp_num_indexes[TL_RP_0_DATA_INDEX];
	req.rp_num_index_1 =
		accel_dev->telemetry->rp_num_indexes[TL_RP_1_DATA_INDEX];
	req.rp_num_index_2 =
		accel_dev->telemetry->rp_num_indexes[TL_RP_2_DATA_INDEX];
	req.rp_num_index_3 =
		accel_dev->telemetry->rp_num_indexes[TL_RP_3_DATA_INDEX];
	if (adf_send_admin(accel_dev, &req, &rsp, ae_mask))
		return -EFAULT;

	memcpy(&accel_dev->telemetry->slice_cnt, &rsp.slice_count,
		  sizeof(struct adf_tl_slice_cnt));
	ret =
	adf_gen4_validate_slice_cnt((u8 *)(&accel_dev->telemetry->slice_cnt),
				    sizeof(struct adf_tl_slice_cnt));
	if (ret) {
		adf_gen4_stop_tl(accel_dev);
		return -EFAULT;
	}

	if (accel_dev->hw_device->switch_drv_active)
		accel_dev->hw_device->switch_drv_active(accel_dev);

	accel_dev->telemetry->state = TL_ON;

	return 0;
}

static void
adf_gen4_crt_key_val_tl_stat(char *buf, size_t *offset,
			     const char *stat_name, u64 stat_value)
{
	*offset += sysfs_emit_at(buf, *offset, "%s %llu\n", stat_name,
				 stat_value);
}

static void
adf_gen4_get_slice_tl_data(char *buf, size_t *offset, u32 *slice_data,
			   u32 slice_num, const char *slice_name)
{
	char text[MAX_STAT_NAME_BUF_SIZE];
	u32 counter = 0;

	for (counter = 0; counter < slice_num; counter++) {
		scnprintf(text, sizeof(text), "%s%u", slice_name, counter);
		adf_gen4_crt_key_val_tl_stat(buf, offset, text,
					     slice_data[counter]);
	}
}

static ssize_t
adf_gen4_print_dev_tl_data(struct device *dev, char *buf)
{
	struct adf_accel_dev *accel_dev = pci_get_drvdata(to_pci_dev(dev));
	struct adf_telemetry *tl = accel_dev->telemetry;
	struct adf_tl_device_data tl_dev_data;
	struct adf_tl_slice_cnt slice_cnt;
	size_t offset = 0;

	if (!tl)
		return -ENOMEM;

	tl_dev_data = tl->tl_data.tl_device_data;
	slice_cnt = tl->slice_cnt;

	if (tl->state == TL_ON) {
		adf_gen4_crt_key_val_tl_stat(buf, &offset, SNAPSHOT_CNT_MSG,
					     tl->tl_data.tl_msg_cnt);
		adf_gen4_crt_key_val_tl_stat(buf, &offset, PCI_TRANS_CNT_NAME,
					     tl_dev_data.tl_pci_trans_cnt);
		adf_gen4_crt_key_val_tl_stat(buf, &offset, MAX_RD_LAT_NAME,
					     tl_dev_data.tl_max_rd_lat);
		adf_gen4_crt_key_val_tl_stat(buf, &offset, RD_LAT_ACC_NAME,
					     tl_dev_data.tl_rd_lat_acc);
		adf_gen4_crt_key_val_tl_stat(buf, &offset, MAX_LAT_NAME,
					     tl_dev_data.tl_max_lat);
		adf_gen4_crt_key_val_tl_stat(buf, &offset, LAT_ACC_NAME,
					     tl_dev_data.tl_lat_acc);
		adf_gen4_crt_key_val_tl_stat(buf, &offset, BW_IN_NAME,
					     tl_dev_data.tl_bw_in);
		adf_gen4_crt_key_val_tl_stat(buf, &offset, BW_OUT_NAME,
					     tl_dev_data.tl_bw_out);
		adf_gen4_crt_key_val_tl_stat(buf, &offset, PAGE_REQ_LAT_NAME,
					     tl_dev_data.tl_at_page_req_lat_acc
					     );
		adf_gen4_crt_key_val_tl_stat(buf, &offset, AT_TRANS_LAT_NAME,
					     tl_dev_data.tl_at_trans_lat_acc);
		adf_gen4_crt_key_val_tl_stat(buf, &offset,
					     AT_MAX_UTLB_USED_NAME,
					     tl_dev_data.tl_at_max_tlb_used);
		adf_gen4_get_slice_tl_data(buf, &offset,
					   tl_dev_data.tl_sliceutil_cpr,
					   slice_cnt.tl_cpr_slice_cnt,
					   CPR_SLICE_UTIL_NAME);
		adf_gen4_get_slice_tl_data(buf, &offset,
					   tl_dev_data.tl_sliceutil_dcpr,
					   slice_cnt.tl_dcpr_slice_cnt,
					   DCPR_SLICE_UTIL_NAME);
		adf_gen4_get_slice_tl_data(buf, &offset,
					   tl_dev_data.tl_sliceutil_xlt,
					   slice_cnt.tl_xlt_slice_cnt,
					   XLT_SLICE_UTIL_NAME);
		adf_gen4_get_slice_tl_data(buf, &offset,
					   tl_dev_data.tl_sliceutil_cph,
					   slice_cnt.tl_cph_slice_cnt,
					   CPH_SLICE_UTIL_NAME);
		adf_gen4_get_slice_tl_data(buf, &offset,
					   tl_dev_data.tl_sliceutil_ath,
					   slice_cnt.tl_ath_slice_cnt,
					   ATH_SLICE_UTIL_NAME);
		adf_gen4_get_slice_tl_data(buf, &offset,
					   tl_dev_data.tl_sliceutil_ucs,
					   slice_cnt.tl_ucs_slice_cnt,
					   UCS_SLICE_UTIL_NAME);
		adf_gen4_get_slice_tl_data(buf, &offset,
					   tl_dev_data.tl_sliceutil_pke,
					   slice_cnt.tl_pke_slice_cnt,
					   PKE_SLICE_UTIL_NAME);
		adf_gen4_get_slice_tl_data(buf, &offset,
					   tl_dev_data.tl_sliceutil_wat,
					   slice_cnt.tl_wat_slice_cnt,
					   WAT_SLICE_UTIL_NAME);
		adf_gen4_get_slice_tl_data(buf, &offset,
					   tl_dev_data.tl_sliceutil_wcp,
					   slice_cnt.tl_wcp_slice_cnt,
					   WCP_SLICE_UTIL_NAME);
	} else {
		offset = sysfs_emit(buf, "%s\n", TL_NOT_TURNED_ON_MSG);
	}

	return offset;
}

static ssize_t
device_data_show(struct device *dev, struct device_attribute *dev_attr,
		 char *buf)
{
	return adf_gen4_print_dev_tl_data(dev, buf);
}

DEVICE_ATTR_RO(device_data);

static ssize_t
adf_gen4_print_rp_stat(struct device *dev, char *buf, u8 rp_num_index)
{
	struct adf_accel_dev *accel_dev = pci_get_drvdata(to_pci_dev(dev));
	struct adf_telemetry *tl = accel_dev->telemetry;
	struct adf_tl_ring_pair_data tl_rp_data;
	size_t offset = 0;

	if (!tl)
		return -ENOMEM;

	tl_rp_data = tl->tl_data.tl_ring_pairs_data[rp_num_index];

	if (tl->state == TL_ON) {
		adf_gen4_crt_key_val_tl_stat(buf, &offset, SNAPSHOT_CNT_MSG,
					     tl->tl_data.tl_msg_cnt);
		adf_gen4_crt_key_val_tl_stat(buf, &offset, RP_NUM_INDEX,
					     tl->rp_num_indexes[rp_num_index]);
		adf_gen4_crt_key_val_tl_stat(buf, &offset, PCI_TRANS_CNT_NAME,
					     tl_rp_data.tl_pci_trans_cnt
					     );
		adf_gen4_crt_key_val_tl_stat(buf, &offset, LAT_ACC_NAME,
					     tl_rp_data.tl_lat_acc);
		adf_gen4_crt_key_val_tl_stat(buf, &offset, BW_IN_NAME,
					     tl_rp_data.tl_bw_in);
		adf_gen4_crt_key_val_tl_stat(buf, &offset, BW_OUT_NAME,
					     tl_rp_data.tl_bw_out);
		adf_gen4_crt_key_val_tl_stat(buf, &offset,
					     AT_GLOB_DTLB_HIT_NAME,
					     tl_rp_data.tl_at_glob_devtlb_hit);
		adf_gen4_crt_key_val_tl_stat(buf, &offset,
					     AT_GLOB_DTLB_MISS_NAME,
					     tl_rp_data.tl_at_glob_devtlb_miss);
		adf_gen4_crt_key_val_tl_stat(buf, &offset,
					     AT_PAYLD_DTLB_HIT_NAME,
					     tl_rp_data.tl_at_payld_devtlb_hit);
		adf_gen4_crt_key_val_tl_stat(buf, &offset,
					     AT_PAYLD_DTLB_MISS_NAME,
					     tl_rp_data.tl_at_payld_devtlb_miss
					     );
	} else {
		offset = sysfs_emit(buf, "%s\n", TL_NOT_TURNED_ON_MSG);
	}

	return offset;
}

static ssize_t
adf_gen4_change_rp_index(struct device *dev, const char *buf, size_t count,
			 u8 rp_num_index)
{
	struct adf_accel_dev *accel_dev = pci_get_drvdata(to_pci_dev(dev));
	struct adf_telemetry *tl = accel_dev->telemetry;
	u32 new_rp_num = 0;
	u32 counter = 0;

	if (!tl)
		return -ENOMEM;

	if (kstrtouint(buf, AUTOMATIC_BASE_DETECT, &new_rp_num))
		return -EINVAL;

	if (new_rp_num > TL_RP_MAX_NUM) {
		dev_warn(dev, TL_MAX_RP_INDEX_WARN);
		return -EINVAL;
	}

	for (counter = 0; counter < HW_MAX_TL_RP_NUM; counter++) {
		if (tl->rp_num_indexes[counter] == new_rp_num) {
			dev_warn(dev, "%s in file rp_%c_data",
				 TL_RP_DUP_INDEX_WARN,
				 TL_PR_ALPHA_INDEX(counter));
			return -EINVAL;
		}
	}

	adf_gen4_stop_tl(accel_dev);
	tl->rp_num_indexes[rp_num_index] = new_rp_num;
	adf_gen4_start_tl(accel_dev);

	return count;
}

static ssize_t rp_A_data_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	return adf_gen4_print_rp_stat(dev, buf, TL_RP_0_DATA_INDEX);
}

static ssize_t rp_A_data_store(struct device *dev,
			       struct device_attribute *dev_attr,
			       const char *buf, size_t count)
{
	return adf_gen4_change_rp_index(dev, buf, count, TL_RP_0_DATA_INDEX);
}
static DEVICE_ATTR_RW(rp_A_data);

static ssize_t rp_B_data_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	return adf_gen4_print_rp_stat(dev, buf, TL_RP_1_DATA_INDEX);
}

static ssize_t rp_B_data_store(struct device *dev,
			       struct device_attribute *dev_attr,
			       const char *buf, size_t count)
{
	return adf_gen4_change_rp_index(dev, buf, count, TL_RP_1_DATA_INDEX);
}
static DEVICE_ATTR_RW(rp_B_data);

static ssize_t rp_C_data_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	return adf_gen4_print_rp_stat(dev, buf, TL_RP_2_DATA_INDEX);
}

static ssize_t rp_C_data_store(struct device *dev,
			       struct device_attribute *dev_attr,
			       const char *buf, size_t count)
{
	return adf_gen4_change_rp_index(dev, buf, count, TL_RP_2_DATA_INDEX);
}
static DEVICE_ATTR_RW(rp_C_data);

static ssize_t rp_D_data_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	return adf_gen4_print_rp_stat(dev, buf, TL_RP_3_DATA_INDEX);
}

static ssize_t rp_D_data_store(struct device *dev,
			       struct device_attribute *dev_attr,
			       const char *buf, size_t count)
{
	return adf_gen4_change_rp_index(dev, buf, count, TL_RP_3_DATA_INDEX);
}
static DEVICE_ATTR_RW(rp_D_data);

static ssize_t
control_store(struct device *dev, struct device_attribute *dev_attr,
	      const char *buf, size_t count)
{
	struct adf_accel_dev *accel_dev = pci_get_drvdata(to_pci_dev(dev));
	struct adf_telemetry *tl = accel_dev->telemetry;
	u32 new_state;

	if (!tl)
		return -ENOMEM;

	if (kstrtouint(buf, AUTOMATIC_BASE_DETECT, &new_state))
		return -EINVAL;

	if (new_state == TL_OFF) {
		if (tl->state == TL_ON) {
			if (adf_gen4_stop_tl(accel_dev))
				return -EFAULT;
			tl->state = new_state;
		} else {
			dev_warn(dev, TL_STOPPED_WARN);
		}
	} else if (new_state == TL_ON) {
		if (tl->state == TL_OFF) {
			if (adf_gen4_start_tl(accel_dev))
				return -EFAULT;
			tl->state = new_state;
		} else {
			dev_warn(dev, TL_STARTED_WARN);
		}
	} else {
		return -EINVAL;
	}

	return count;
}

static ssize_t
control_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct adf_accel_dev *accel_dev = pci_get_drvdata(to_pci_dev(dev));
	size_t offset = 0;

	if (!accel_dev->telemetry)
		return -ENOMEM;

	if (accel_dev->telemetry->state == TL_ON)
		offset = sysfs_emit(buf, "%s\n", TL_ON_MSG);
	else
		offset = sysfs_emit(buf, "%s\n", TL_OFF_MSG);

	return offset;
}
DEVICE_ATTR_RW(control);

static struct attribute *accel_tl_attrs[] = {
	&dev_attr_device_data.attr,
	&dev_attr_rp_A_data.attr,
	&dev_attr_rp_B_data.attr,
	&dev_attr_rp_C_data.attr,
	&dev_attr_rp_D_data.attr,
	&dev_attr_control.attr,
	NULL,
};

static const struct attribute_group tl_sysfs_group = {
	.name = "telemetry",
	.attrs = accel_tl_attrs,
};

int adf_gen4_tl_sysfs_create(struct adf_accel_dev *accel_dev)
{
	struct device *dev = &GET_DEV(accel_dev);

	return devm_device_add_group(dev, &tl_sysfs_group);
}
EXPORT_SYMBOL_GPL(adf_gen4_tl_sysfs_create);

static u32 adf_gen4_calc_bw(u64 raw_bw)
{
	/* Changing raw data from HW to bytes per second  */
	raw_bw = raw_bw * TL_BW_HW_UNIT_TO_BYTES;
	/* Changing bytes per second to bits per second */
	raw_bw = raw_bw * BITS_IN_BYTE;
	/* Changing bits per second to Mbps */
	return div_u64(raw_bw, BITS_IN_MBITS);
}

static u64 adf_gen4_calc_avg(u64 dividend, u32 divisor)
{
	if (divisor == 0)
		return 0;
	else
		return div_u64(dividend, divisor);
}

static u64 adf_gen4_conv_cycles_to_ns(u64 cycles_num)
{
	return cycles_num * CYCLES_TO_NS_MULTIPLIER;
}

static u64 adf_gen4_calc_avg_ns(u64 dividend, u32 divisor)
{
	u64 quotient = adf_gen4_calc_avg(dividend, divisor);

	return adf_gen4_conv_cycles_to_ns(quotient);
}

static void
adf_gen4_calc_slices_tl_data(u32 *sliceutils, u32 slice_num,
			     struct adf_tl_slice_data_regs *slice_data_regs)
{
	u32 counter;

	for (counter = 0; counter < slice_num; counter++)
		sliceutils[counter] = slice_data_regs[counter].reg_tm_sliceutil;
}

static int adf_gen4_calc_rp_tl_data(struct adf_telemetry *tl)
{
	struct adf_tl_data_regs *tl_data_regs = NULL;
	struct adf_tl_ring_pair_data_regs *tl_rp_regs = NULL;
	struct adf_tl_ring_pair_data *tl_rp = NULL;
	u32 counter = 0;

	if (!tl)
		return -EFAULT;

	tl_data_regs = (struct adf_tl_data_regs *)tl->virt_addr;

	for (counter = 0; counter < HW_MAX_TL_RP_NUM; counter++) {
		tl_rp_regs = &tl_data_regs->tl_ring_pairs_data_regs[counter];
		tl_rp = &tl->tl_data.tl_ring_pairs_data[counter];

		tl_rp->tl_pci_trans_cnt = tl_rp_regs->reg_tl_pci_trans_cnt;
		tl_rp->tl_lat_acc =
			adf_gen4_calc_avg_ns(tl_rp_regs->reg_tl_lat_acc,
					     tl_rp_regs->reg_tl_ae_put_cnt);
		tl_rp->tl_bw_in = adf_gen4_calc_bw(tl_rp_regs->reg_tl_bw_in);
		tl_rp->tl_bw_out = adf_gen4_calc_bw(tl_rp_regs->reg_tl_bw_out);
		tl_rp->tl_at_glob_devtlb_hit =
			tl_rp_regs->reg_tl_at_glob_devtlb_hit;
		tl_rp->tl_at_glob_devtlb_miss =
			tl_rp_regs->reg_tl_at_glob_devtlb_miss;
		tl_rp->tl_at_payld_devtlb_hit =
			tl_rp_regs->reg_tl_at_payld_devtlb_hit;
		tl_rp->tl_at_payld_devtlb_miss =
			tl_rp_regs->reg_tl_at_payld_devtlb_miss;
	}

	return 0;
}

static int adf_gen4_calc_dev_tl_data(struct adf_telemetry *tl)
{
	struct adf_tl_data_regs *tl_data_regs = NULL;
	struct adf_tl_device_data_regs *tl_dev_regs = NULL;
	struct adf_tl_device_data *tl_dev = NULL;
	struct adf_tl_slice_cnt slice_cnt;

	if (!tl)
		return -EFAULT;

	tl_data_regs = (struct adf_tl_data_regs *)tl->virt_addr;
	tl_dev_regs = &tl_data_regs->tl_device_data_regs;
	tl_dev = &tl->tl_data.tl_device_data;
	slice_cnt = tl->slice_cnt;

	tl_dev->tl_pci_trans_cnt = tl_dev_regs->reg_tl_pci_trans_cnt;
	tl_dev->tl_max_rd_lat =
		adf_gen4_conv_cycles_to_ns(tl_dev_regs->reg_tl_max_rd_lat);
	tl_dev->tl_rd_lat_acc =
		adf_gen4_calc_avg_ns(tl_dev_regs->reg_tl_rd_lat_acc,
				     tl_dev_regs->reg_tl_rd_cmpl_cnt);
	tl_dev->tl_max_lat =
		adf_gen4_conv_cycles_to_ns(tl_dev_regs->reg_tl_max_lat);
	tl_dev->tl_lat_acc =
		adf_gen4_calc_avg_ns(tl_dev_regs->reg_tl_lat_acc,
				     tl_dev_regs->reg_tl_ae_put_cnt);
	tl_dev->tl_bw_in = adf_gen4_calc_bw(tl_dev_regs->reg_tl_bw_in);
	tl_dev->tl_bw_out = adf_gen4_calc_bw(tl_dev_regs->reg_tl_bw_out);
	tl_dev->tl_at_page_req_lat_acc =
		adf_gen4_calc_avg_ns(tl_dev_regs->reg_tl_at_page_req_lat_acc,
				     tl_dev_regs->reg_tl_at_page_req_cnt);
	tl_dev->tl_at_trans_lat_acc =
		adf_gen4_calc_avg_ns(tl_dev_regs->reg_tl_at_trans_lat_acc,
				     tl_dev_regs->reg_tl_at_trans_lat_cnt);
	tl_dev->tl_at_max_tlb_used = tl_dev_regs->reg_tl_at_max_tlb_used;

	adf_gen4_calc_slices_tl_data(tl_dev->tl_sliceutil_ath,
				     slice_cnt.tl_ath_slice_cnt,
				     tl_dev_regs->ath_slices);
	adf_gen4_calc_slices_tl_data(tl_dev->tl_sliceutil_cph,
				     slice_cnt.tl_cph_slice_cnt,
				     tl_dev_regs->cph_slices);
	adf_gen4_calc_slices_tl_data(tl_dev->tl_sliceutil_cpr,
				     slice_cnt.tl_cpr_slice_cnt,
				     tl_dev_regs->cpr_slices);
	adf_gen4_calc_slices_tl_data(tl_dev->tl_sliceutil_xlt,
				     slice_cnt.tl_xlt_slice_cnt,
				     tl_dev_regs->xlt_slices);
	adf_gen4_calc_slices_tl_data(tl_dev->tl_sliceutil_dcpr,
				     slice_cnt.tl_dcpr_slice_cnt,
				     tl_dev_regs->dcpr_slices);
	adf_gen4_calc_slices_tl_data(tl_dev->tl_sliceutil_pke,
				     slice_cnt.tl_pke_slice_cnt,
				     tl_dev_regs->pke_slices);
	adf_gen4_calc_slices_tl_data(tl_dev->tl_sliceutil_ucs,
				     slice_cnt.tl_ucs_slice_cnt,
				     tl_dev_regs->ucs_slices);
	adf_gen4_calc_slices_tl_data(tl_dev->tl_sliceutil_wat,
				     slice_cnt.tl_wat_slice_cnt,
				     tl_dev_regs->wat_slices);
	adf_gen4_calc_slices_tl_data(tl_dev->tl_sliceutil_wcp,
				     slice_cnt.tl_wcp_slice_cnt,
				     tl_dev_regs->wcp_slices);

	return 0;
}


int adf_gen4_init_tl(struct adf_accel_dev *accel_dev)
{
	struct device *dev = &GET_DEV(accel_dev);
	int ret = 0;

	if (adf_gen4_alloc_mem_tl(accel_dev)) {
		ret = -ENOMEM;
		dev_err(dev, "Failed to init telemetry!\n");
	}

	return ret;
}
EXPORT_SYMBOL_GPL(adf_gen4_init_tl);

int adf_gen4_exit_tl(struct adf_accel_dev *accel_dev)
{
	struct device *dev = &GET_DEV(accel_dev);
	int ret = 0;

	if (!accel_dev->telemetry)
		return -ENOMEM;

	if (accel_dev->telemetry->state == TL_ON) {
		if (adf_gen4_stop_tl(accel_dev)) {
			dev_err(dev, "Failed to send telemetry init stop\n");
			ret = -EFAULT;
		}
	}

	adf_gen4_free_mem_tl(accel_dev);

	return ret;
}
EXPORT_SYMBOL_GPL(adf_gen4_exit_tl);

void adf_gen4_calc_tl_data(struct adf_accel_dev *accel_dev)
{
	struct adf_telemetry *tl = NULL;
	struct adf_tl_data_regs *tl_data_regs = NULL;

	if (!accel_dev || !accel_dev->telemetry)
		return;

	tl = accel_dev->telemetry;
	if (tl->state == TL_ON) {
		tl_data_regs = (struct adf_tl_data_regs *)tl->virt_addr;

		if (tl->tl_data.tl_msg_cnt !=
						tl_data_regs->reg_tl_msg_cnt) {
			adf_gen4_calc_dev_tl_data(tl);
			adf_gen4_calc_rp_tl_data(tl);
			tl->tl_data.tl_msg_cnt =
					tl_data_regs->reg_tl_msg_cnt;
		}
	}
}
EXPORT_SYMBOL_GPL(adf_gen4_calc_tl_data);
