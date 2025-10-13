/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2019 Intel Corporation */
#ifndef ADF_GEN4_PM_H
#define ADF_GEN4_PM_H

/*Power management register*/
#define ADF_GEN4_PM_FW_INIT (0x50A000)
#define ADF_GEN4_PM_PWRREQ (0x50A008)
#define ADF_GEN4_PM_STATUS (0x50A00C)
#define ADF_GEN4_PM_HOST_MSG (0x50A01C)
#define ADF_GEN4_PM_THREAD (0x50A020)
#define ADF_GEN4_PM_INTERRUPT (0x50A028)
#define ADF_GEN4_PM_PWRREQ_FUSE (0x2D0)

#define ADF_GEN4_PM_IDLE_INT_EN BIT(18)
#define ADF_GEN4_PM_THROTTLE_INT_EN BIT(19)
#define ADF_GEN4_PM_DRV_ACTIVE BIT(20)
#define ADF_GEN4_PM_INT_EN_DEFAULT (ADF_GEN4_PM_IDLE_INT_EN | \
				     ADF_GEN4_PM_THROTTLE_INT_EN)
#define ADF_GEN4_PM_STATE (BIT(20) | BIT(21) | BIT(22))
#define ADF_GEN4_PM_INIT_STATE 0x2
#define ADF_GEN4_PM_STATE_BIT_OFFSET 20

#define ADF_GEN4_PM_THR_STS      BIT(0)
#define ADF_GEN4_PM_IDLE_STS     BIT(1)
#define ADF_GEN4_PM_FM_INT_STS   BIT(2)
#define ADF_GEN4_PM_INT_STS_MASK \
	(ADF_GEN4_PM_THR_STS |   \
	 ADF_GEN4_PM_IDLE_STS |  \
	 ADF_GEN4_PM_FM_INT_STS)

#define ADF_GEN4_PM_MSG_PENDING BIT(0)
#define ADF_GEN4_PM_MSG_PAYLOAD_BIT_OFFSET 1

#define ADF_GEN4_RD_LOOP_ITERATIONS 50
#define ADF_GEN4_SLEEP_TIME_IN_MS 10

#define ADF_GEN4_PM_EVENT_LOG_NUM 8
#define ADF_GEN4_PM_IDLE_512_US		0x6

/* PM CSR fields definition */
union adf_fusectl0_reg {
	struct {
		/* Other fields not used by PM */
		u32 reserved1 : 21;
		u32 enable_pm : 1;
		u32 enable_pm_idle : 1;
		u32 enable_deep_pm_idle : 1;
		u32 reserved2 : 8;
	};
	u32 reg;
};

union adf_pm_fw_init_reg {
	struct {
		u32 reserved1 : 2;
		u32 idle_enable : 1;
		u32 idle_filter : 3;
		u32 reserved2 : 26;
	};
	u32 reg;
};

union adf_pm_status_reg {
	struct {
		u32 thr_stats : 8;
		u32 thr_setting : 3;
		u32 current_wp : 9;
		u32 qat_pm_state : 3;
		u32 pending_wp : 9;
	};
	u32 reg;
};

union adf_pm_main_reg {
	struct {
		u32 timer_enable : 1;
		u32 timer_val : 3;
		u32 thr_value : 3;
		u32 min_pwr_ack : 1;
		u32 reserved1 : 24;
	};
	u32 reg;
};

union adf_pm_thread_reg {
	struct {
		u32 pm_available : 1;
		u32 pm_xfer : 5;
		u32 pm_context : 3;
		u32 pm_signal : 4;
		u32 pm_ras : 1;
		u32 pm_fw_irq : 1;
		u32 pm_fw_irq_msg : 2;
		u32 min_pwr_ack_pending : 1;
		u32 reserved1 : 14;
	};
	u32 reg;
};

union adf_pm_ssm_enable_reg {
	struct {
		u32 pm_enable : 16;
		u32 reserved : 16;
	};
	u32 reg;
};

union adf_pm_count {
	struct {
		u32 cpr : 1;
		u32 xlt : 1;
		u32 dcpr : 2;
		u32 pke : 5;
		u32 wat : 5;
		u32 wcp : 5;
		u32 ucs : 2;
		u32 cph : 4;
		u32 ath : 4;
		u32 reserved1 : 3;
	};
	u32 reg;
};

union adf_pm_domain_status_reg {
	struct {
		u32 domain_power_gated : 16;
		u32 reserved1 : 16;
	};
	u32 reg;
};

enum qat_pm_idle_support {
	PM_IDLE_UNSUPPORT = 0,
	PM_IDLE_SUPPORT
};

enum qat_pm_host_msg {
	PM_NO_CHANGE = 0,
	PM_SET_MIN,
};

struct adf_gen4_pm_irq {
	struct adf_accel_dev *accel_dev;
	struct work_struct pm_irq_work;
	u32 pm_int_sts;
};

struct adf_accel_dev;

void adf_gen4_switch_drv_active(struct adf_accel_dev *accel_dev);
int adf_gen4_init_pm(struct adf_accel_dev *accel_dev);
void adf_gen4_exit_pm(struct adf_accel_dev *accel_dev);
int adf_gen4_set_pm_drv_active(struct adf_accel_dev *accel_dev);
bool adf_gen4_pm_check_interrupts(struct adf_accel_dev *accel_dev);
void adf_gen4_pm_bh_handler(struct work_struct *work);
#endif

