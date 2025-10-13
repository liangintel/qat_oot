/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2021 Intel Corporation */

#ifndef ADF_GEN4_TL_H
#define ADF_GEN4_TL_H

/* States of device telemetry */
#define TL_OFF		0
#define TL_ON		1
#define TL_OFF_MSG	"off"
#define TL_ON_MSG	"on"

/* Maximum size of the buffer for slice name */
#define MAX_STAT_NAME_BUF_SIZE	50

/* Calculation constants */
#define BITS_IN_BYTE		8
#define BITS_IN_MBITS		1000000
#define TL_BW_HW_UNIT_TO_BYTES	64
#define CYCLES_TO_NS_MULTIPLIER	2

/* Maximum number of supported slices type. */
#define HW_MAX_NUM_OF_SLICES	24

/* Maximum number of supported ring pairs */
#define HW_MAX_TL_RP_NUM	4
#define TL_RP_0_DATA_INDEX	0
#define TL_RP_1_DATA_INDEX	1
#define TL_RP_2_DATA_INDEX	2
#define TL_RP_3_DATA_INDEX	3
#define TL_RP_0_DEFAULT_NUM	0
#define TL_RP_1_DEFAULT_NUM	1
#define TL_RP_2_DEFAULT_NUM	2
#define TL_RP_3_DEFAULT_NUM	3
#define TL_RP_MAX_NUM		63
/* Parameters of kstrtouint, which converts a string to an unsigned int */
#define AUTOMATIC_BASE_DETECT	0

/* List of telemetry statistics names */
#define SNAPSHOT_CNT_MSG	"sample_cnt"
#define RP_NUM_INDEX		"rp_num"

#define PCI_TRANS_CNT_NAME	"pci_trans_cnt"
#define MAX_RD_LAT_NAME		"max_rd_lat"
#define RD_LAT_ACC_NAME		"rd_lat_acc_avg"
#define MAX_LAT_NAME		"max_lat"
#define LAT_ACC_NAME		"lat_acc_avg"
#define BW_IN_NAME		"bw_in"
#define BW_OUT_NAME		"bw_out"
#define PAGE_REQ_LAT_NAME	"at_page_req_lat_acc_avg"
#define AT_TRANS_LAT_NAME	"at_trans_lat_acc_avg"
#define AT_MAX_UTLB_USED_NAME	"at_max_tlb_used"
#define AT_GLOB_DTLB_HIT_NAME	"at_glob_devtlb_hit"
#define AT_GLOB_DTLB_MISS_NAME	"at_glob_devtlb_miss"
#define AT_PAYLD_DTLB_HIT_NAME	"tl_at_payld_devtlb_hit"
#define AT_PAYLD_DTLB_MISS_NAME	"tl_at_payld_devtlb_miss"

#define ATH_SLICE_UTIL_NAME	"util_ath"
#define CPH_SLICE_UTIL_NAME	"util_cph"
#define CPR_SLICE_UTIL_NAME	"util_cpr"
#define XLT_SLICE_UTIL_NAME	"util_xlt"
#define DCPR_SLICE_UTIL_NAME	"util_dcpr"
#define PKE_SLICE_UTIL_NAME	"util_pke"
#define UCS_SLICE_UTIL_NAME	"util_ucs"
#define WAT_SLICE_UTIL_NAME	"util_wat"
#define WCP_SLICE_UTIL_NAME	"util_wcp"

/* List of telemetry messages */
#define TL_NOT_TURNED_ON_MSG	"Telemetry is turned off!"

/* List of telemetry error and warning messages */
#define UNKNOWN_TL_STAT_WARN	"Telemetry statistic not available!"
#define TL_STOPPED_WARN		"Telemetry already stopped!"
#define TL_STARTED_WARN		"Telemetry already started!"
#define TL_RP_DUP_INDEX_WARN	"RingPair index already selected!"
#define TL_MAX_RP_INDEX_WARN	"Selected RingPair index exceeded maximum!"

/* telemetry RP alphabetical index from numercal index*/
#define TL_PR_ALPHA_INDEX(index) ((index) + 'A')


/*
 * Below structures are used for mapping data received from FW.
 * Reserved longwords are inserted to keep 64 bit alignment.
 */
struct adf_tl_slice_data_regs {
	u32 reg_tm_sliceexeccount;
	u32 reg_tm_sliceutil;
};

struct adf_tl_device_data_regs {
	u64 reg_tl_rd_lat_acc;
	u64 reg_tl_lat_acc;
	u64 reg_tl_at_page_req_lat_acc;
	u64 reg_tl_at_trans_lat_acc;
	u64 reg_tl_re_acc;
	u32 reg_tl_pci_trans_cnt;
	u32 reg_tl_max_rd_lat;
	u32 reg_tl_rd_cmpl_cnt;
	u32 reg_tl_max_lat;
	u32 reg_tl_ae_put_cnt;
	u32 reg_tl_bw_in;
	u32 reg_tl_bw_out;
	u32 reg_tl_at_page_req_cnt;
	u32 reg_tl_at_trans_lat_cnt;
	u32 reg_tl_at_max_tlb_used;
	u32 reg_tl_re_cnt;
	u32 reserved;
	struct adf_tl_slice_data_regs ath_slices[HW_MAX_NUM_OF_SLICES];
	struct adf_tl_slice_data_regs cph_slices[HW_MAX_NUM_OF_SLICES];
	struct adf_tl_slice_data_regs cpr_slices[HW_MAX_NUM_OF_SLICES];
	struct adf_tl_slice_data_regs xlt_slices[HW_MAX_NUM_OF_SLICES];
	struct adf_tl_slice_data_regs dcpr_slices[HW_MAX_NUM_OF_SLICES];
	struct adf_tl_slice_data_regs pke_slices[HW_MAX_NUM_OF_SLICES];
	struct adf_tl_slice_data_regs ucs_slices[HW_MAX_NUM_OF_SLICES];
	struct adf_tl_slice_data_regs wat_slices[HW_MAX_NUM_OF_SLICES];
	struct adf_tl_slice_data_regs wcp_slices[HW_MAX_NUM_OF_SLICES];
};

struct adf_tl_ring_pair_data_regs {
	u64 reg_tl_lat_acc;
	u64 reg_tl_re_acc;
	u32 reg_tl_pci_trans_cnt;
	u32 reg_tl_ae_put_cnt;
	u32 reg_tl_bw_in;
	u32 reg_tl_bw_out;
	u32 reg_tl_at_glob_devtlb_hit;
	u32 reg_tl_at_glob_devtlb_miss;
	u32 reg_tl_at_payld_devtlb_hit;
	u32 reg_tl_at_payld_devtlb_miss;
	u32 reg_tl_re_cnt;
	u32 reserved;
};

/* Layout of data received from FW */
struct adf_tl_data_regs {
	struct adf_tl_device_data_regs tl_device_data_regs;
	struct adf_tl_ring_pair_data_regs
		tl_ring_pairs_data_regs[HW_MAX_TL_RP_NUM];
	u32 reg_tl_msg_cnt;
	u32 reserved;
};

/* Below structures are used for storing and calculating telemetry data. */
struct adf_tl_device_data {
	u32 tl_pci_trans_cnt;
	u32 tl_max_rd_lat;
	u64 tl_rd_lat_acc;
	u32 tl_max_lat;
	u64 tl_lat_acc;
	u32 tl_bw_in;
	u32 tl_bw_out;
	u64 tl_at_page_req_lat_acc;
	u64 tl_at_trans_lat_acc;
	u32 tl_at_max_tlb_used;
	u32 tl_sliceutil_ath[HW_MAX_NUM_OF_SLICES];
	u32 tl_sliceutil_cph[HW_MAX_NUM_OF_SLICES];
	u32 tl_sliceutil_cpr[HW_MAX_NUM_OF_SLICES];
	u32 tl_sliceutil_xlt[HW_MAX_NUM_OF_SLICES];
	u32 tl_sliceutil_dcpr[HW_MAX_NUM_OF_SLICES];
	u32 tl_sliceutil_pke[HW_MAX_NUM_OF_SLICES];
	u32 tl_sliceutil_ucs[HW_MAX_NUM_OF_SLICES];
	u32 tl_sliceutil_wat[HW_MAX_NUM_OF_SLICES];
	u32 tl_sliceutil_wcp[HW_MAX_NUM_OF_SLICES];
};

struct adf_tl_ring_pair_data {
	u32 tl_pci_trans_cnt;
	u64 tl_lat_acc;
	u32 tl_ae_ex_acc;
	u32 tl_bw_in;
	u32 tl_bw_out;
	u32 tl_at_glob_devtlb_hit;
	u32 tl_at_glob_devtlb_miss;
	u32 tl_at_payld_devtlb_hit;
	u32 tl_at_payld_devtlb_miss;
};

/* Structure for calculated values */
struct adf_tl_data {
	struct adf_tl_device_data tl_device_data;
	struct adf_tl_ring_pair_data
		tl_ring_pairs_data[HW_MAX_TL_RP_NUM];
	u32 tl_msg_cnt;
};

/* Structure for slice numbering - generic for all 2.x products */
struct adf_tl_slice_cnt {
	u8 tl_cpr_slice_cnt;
	u8 tl_xlt_slice_cnt;
	u8 tl_dcpr_slice_cnt;
	u8 tl_pke_slice_cnt;
	u8 tl_wat_slice_cnt;
	u8 tl_wcp_slice_cnt;
	u8 tl_ucs_slice_cnt;
	u8 tl_cph_slice_cnt;
	u8 tl_ath_slice_cnt;
};

struct adf_telemetry {
	u32 state;
	struct adf_tl_data tl_data;
	struct adf_tl_slice_cnt slice_cnt;
	u32 rp_num_indexes[HW_MAX_TL_RP_NUM];
	dma_addr_t phy_addr;
	void *virt_addr;
};

struct adf_accel_dev;
int adf_gen4_tl_sysfs_create(struct adf_accel_dev *accel_dev);
int adf_gen4_init_tl(struct adf_accel_dev *accel_dev);
int adf_gen4_exit_tl(struct adf_accel_dev *accel_dev);
void adf_gen4_calc_tl_data(struct adf_accel_dev *accel_dev);

#endif
