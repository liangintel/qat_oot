// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2018 - 2024 Intel Corporation */
#include <linux/delay.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "adf_gen4_pm.h"
#include "adf_cfg_strings.h"
#include "icp_qat_fw_init_admin.h"
#include "adf_gen4_hw_data.h"
#include "adf_cfg.h"

static int get_cfg_pm_setting(struct adf_accel_dev *accel_dev, int *val_ptr,
			      const char *key_str)
{
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	char val[ADF_CFG_MAX_VAL_LEN_IN_BYTES];

	strscpy(key, key_str, sizeof(key));
	if (!adf_cfg_get_param_value(accel_dev, ADF_GENERAL_SEC, key, val))
		if (kstrtoint(val, 0, val_ptr))
			return -EFAULT;

	return 0;
}

static int config_qat_pm(struct adf_accel_dev *accel_dev, u32 idle_delay)
{
	struct icp_qat_fw_init_admin_req req;
	struct icp_qat_fw_init_admin_resp resp;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u32 ae_mask = hw_data->admin_ae_mask;

	if (!accel_dev->admin) {
		dev_err(&GET_DEV(accel_dev), "adf_admin is not available\n");
		return -EFAULT;
	}

	memset(&req, 0, sizeof(struct icp_qat_fw_init_admin_req));

	req.cmd_id = ICP_QAT_FW_PM_STATE_CONFIG;
	req.idle_filter = idle_delay;

	if (adf_send_admin(accel_dev, &req, &resp, ae_mask)) {
		dev_err(&GET_DEV(accel_dev), "Failed to configure pm\n");
		return -EFAULT;
	}

	return 0;
}

#ifdef CONFIG_DEBUG_FS
static DEFINE_MUTEX(pm_status_read_lock);

static int query_qat_pm_info(struct adf_accel_dev *accel_dev,
			     void *pm_info_buf)
{
	struct icp_qat_fw_init_admin_req req;
	struct icp_qat_fw_init_admin_resp resp;
	dma_addr_t phy_pm_state_addr;
	void *virt_pm_state_addr;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u32 ae_mask = hw_data->admin_ae_mask;

	/* Query pm info via init/admin cmd */
	if (!accel_dev->admin) {
		dev_err(&GET_DEV(accel_dev), "adf_admin is not available\n");
		return -EFAULT;
	}

	virt_pm_state_addr = dma_alloc_coherent(&GET_DEV(accel_dev),
						PAGE_SIZE,
						&phy_pm_state_addr,
						GFP_KERNEL);
	if (!virt_pm_state_addr) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to allocate DMA buffer\n");
		return -ENOMEM;
	}

	memset(&req, 0, sizeof(struct icp_qat_fw_init_admin_req));
	memset(virt_pm_state_addr, 0, PAGE_SIZE);

	req.cmd_id = ICP_QAT_FW_PM_INFO;
	req.init_cfg_sz = sizeof(struct icp_qat_fw_init_admin_pm_info);
	req.init_cfg_ptr = phy_pm_state_addr;

	if (adf_send_admin(accel_dev, &req, &resp, ae_mask)) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to query pm info from qat\n");
		dma_free_coherent(&GET_DEV(accel_dev),
				  PAGE_SIZE,
				  virt_pm_state_addr,
				  phy_pm_state_addr);
		return -EFAULT;
	}
	memcpy(pm_info_buf, virt_pm_state_addr,
	       sizeof(struct icp_qat_fw_init_admin_pm_info));
	dma_free_coherent(&GET_DEV(accel_dev), PAGE_SIZE,
			  virt_pm_state_addr, phy_pm_state_addr);

	return 0;
}

static void print_pm_status(struct seq_file *sfile)
{
	struct adf_accel_dev *accel_dev = sfile->private;
	struct icp_qat_fw_init_admin_pm_info pm_info;
	struct adf_pm *pm = accel_dev->power_management;
	union adf_fusectl0_reg fusectl0 = { .reg = 0 };
	union adf_pm_fw_init_reg pm_fw_init = { .reg = 0 };
	union adf_pm_status_reg pm_status = { .reg = 0 };
	union adf_pm_main_reg pm_main = { .reg = 0 };
	union adf_pm_thread_reg pm_thread = { .reg = 0 };
	union adf_pm_ssm_enable_reg ssm_pm_enable = { .reg = 0 };
	union adf_pm_count active_sts = { .reg = 0 };
	union adf_pm_count managed_sts = { .reg = 0 };
	union adf_pm_domain_status_reg domain_sts = { .reg = 0 };
	int i = 0;
	u32 val;
	void __iomem *pmisc =
		(&GET_BARS(accel_dev)[ADF_GEN4_PMISC_BAR])->virt_addr;

	memset(&pm_info, 0, sizeof(pm_info));

	/* Query PM info from QAT FW */
	if (query_qat_pm_info(accel_dev, &pm_info))
		return;

	fusectl0.reg = pm_info.fusectl0;
	pm_fw_init.reg = pm_info.pm_fw_init;
	pm_status.reg = pm_info.pm_status;
	pm_main.reg = pm_info.pm_main;
	pm_thread.reg = pm_info.pm_thread;
	ssm_pm_enable.reg = pm_info.ssm_pm_enable;
	active_sts.reg = pm_info.ssm_pm_active_status;
	managed_sts.reg = pm_info.ssm_pm_managed_status;
	domain_sts.reg = pm_info.ssm_pm_domain_status;

	seq_puts(sfile, "----------- PM Fuse info ---------\n");
	/* Fusectl related */
	seq_printf(sfile, "enable_pm: %#x\n", fusectl0.enable_pm);
	seq_printf(sfile, "enable_pm_idle: %#x\n",
		   fusectl0.enable_pm_idle);
	seq_printf(sfile, "enable_deep_pm_idle: %#x\n",
		   fusectl0.enable_deep_pm_idle);
	seq_printf(sfile, "max_pwrreq: %#x\n", pm_info.max_pwrreq);
	seq_printf(sfile, "min_pwrreq: %#x\n", pm_info.min_pwrreq);

	seq_puts(sfile, "------------  PM Info ------------\n");
	/* PM related */
	seq_printf(sfile, "power_level: %s\n",
		   pm_info.pwr_state == PM_SET_MIN ? "min" : "max");
	seq_printf(sfile, "cpm_pm_state: %#x\n",
		   pm_status.qat_pm_state);
	seq_printf(sfile, "pending_wp: %#x\n",
		   pm_status.pending_wp);
	seq_printf(sfile, "current_wp: %#x\n",
		   pm_status.current_wp);
	seq_printf(sfile, "idle_enable: %#x\n", pm_fw_init.idle_enable);
	seq_printf(sfile, "idle_filter: %#x\n",
		   pm_fw_init.idle_filter);
	seq_printf(sfile, "min_pwr_ack: %#x\n", pm_main.min_pwr_ack);
	seq_printf(sfile, "min_pwr_ack_pending: %#x\n",
		   pm_thread.min_pwr_ack_pending);
	seq_printf(sfile, "thr_value: %#x\n", pm_main.thr_value);
	seq_puts(sfile, "pm_mode: STATIC\n");

	/* SSM related */
	seq_puts(sfile, "----------- SSM_PM Info ----------\n");
	seq_printf(sfile, "ssm_pm_enable: %#x\n",
		   ssm_pm_enable.pm_enable);
	seq_printf(sfile, "active_constraint: %#x\n",
		   pm_info.active_constraints);
	seq_printf(sfile, "domain_power_gated: %#x\n",
		   domain_sts.domain_power_gated);
	seq_printf(sfile, "ath_active_count: %#x\n", active_sts.ath);
	seq_printf(sfile, "cph_active_count: %#x\n", active_sts.cph);
	seq_printf(sfile, "pke_active_count: %#x\n", active_sts.pke);
	seq_printf(sfile, "cpr_active_count: %#x\n", active_sts.cpr);
	seq_printf(sfile, "dcpr_active_count: %#x\n", active_sts.dcpr);
	seq_printf(sfile, "ucs_active_count: %#x\n", active_sts.ucs);
	seq_printf(sfile, "xlt_active_count: %#x\n", active_sts.xlt);
	seq_printf(sfile, "wat_active_count: %#x\n", active_sts.wat);
	seq_printf(sfile, "wcp_active_count: %#x\n", active_sts.wcp);

	seq_printf(sfile, "ath_managed_count: %#x\n", managed_sts.ath);
	seq_printf(sfile, "cph_managed_count: %#x\n", managed_sts.cph);
	seq_printf(sfile, "pke_managed_count: %#x\n", managed_sts.pke);
	seq_printf(sfile, "cpr_managed_count: %#x\n", managed_sts.cpr);
	seq_printf(sfile, "dcpr_managed_count: %#x\n", managed_sts.dcpr);
	seq_printf(sfile, "ucs_managed_count: %#x\n", managed_sts.ucs);
	seq_printf(sfile, "xlt_managed_count: %#x\n", managed_sts.xlt);
	seq_printf(sfile, "wat_managed_count: %#x\n", managed_sts.wat);
	seq_printf(sfile, "wcp_managed_count: %#x\n", managed_sts.wcp);

	seq_puts(sfile, "------------- PM Log -------------\n");
	seq_printf(sfile, "host_msg_event_count: %#x\n",
		   pm_info.host_msg_event_count);
	seq_printf(sfile, "sys_pm_event_count: %#x\n",
		   pm_info.sys_pm_event_count);
	seq_printf(sfile, "ssm_event_count: %#x\n",
		   pm_info.local_ssm_event_count);
	seq_printf(sfile, "timer_event_count: %#x\n",
		   pm_info.timer_event_count);
	seq_printf(sfile, "unknown_event_count: %#x\n",
		   pm_info.unknown_event_count);

	for (i = 0; i < ADF_GEN4_PM_EVENT_LOG_NUM; i++)
		seq_printf(sfile, "event%d: %#x\n", i,
			   pm_info.event_log[i]);

	if (pm) {
		seq_printf(sfile, "idle_irq_cnt: %#x\n",
			   pm->idle_irq_counters);
		seq_printf(sfile, "host_ack_counter: %#x\n",
			   pm->host_ack_counter);
		seq_printf(sfile, "host_nack_counter: %#x\n",
			   pm->host_nack_counter);
		seq_printf(sfile, "fw_irq_cnt: %#x\n",
			   pm->fw_irq_counters);
		seq_printf(sfile, "throttle_irq_cnt: %#x\n",
			   pm->throttle_irq_counters);
	}

	seq_puts(sfile, "----------- HW PM CSRs -----------\n");
	seq_printf(sfile, "CPM_PM_FW_INIT: %#x\n", pm_info.pm_fw_init);
	seq_printf(sfile, "CPM_PM_STATUS: %#x\n", pm_info.pm_status);
	seq_printf(sfile, "CPM_PM_MAIN_FW: %#x\n", pm_info.pm_main);
	seq_printf(sfile, "CPM_PM_PWRREQ: %#x\n", pm_info.pm_pwrreq);

	val = ADF_CSR_RD(pmisc, ADF_GEN4_PM_HOST_MSG);
	seq_printf(sfile, "CPM_PM_HOST_MSG: %#x\n", val);

	val = ADF_CSR_RD(pmisc, ADF_GEN4_PM_INTERRUPT);
	seq_printf(sfile, "CPM_PM_INTERRUPT: %#x\n", val);
}

static void *pm_status_start(struct seq_file *sfile, loff_t *pos)
{
	mutex_lock(&pm_status_read_lock);
	if (*pos == 0)
		return SEQ_START_TOKEN;
	else
		return NULL;
}

static void pm_status_stop(struct seq_file *sfile, void *v)
{
	mutex_unlock(&pm_status_read_lock);
}

static void *pm_status_next(struct seq_file *sfile, void *v, loff_t *pos)
{
	(*pos)++;
	return NULL;
}

static int pm_status_show(struct seq_file *sfile, void *v)
{
	if (v == SEQ_START_TOKEN) {
		/* Display PM status */
		print_pm_status(sfile);
	}

	return 0;
}

static const struct seq_operations pm_status_sops = {
	.start = pm_status_start,
	.next = pm_status_next,
	.stop = pm_status_stop,
	.show = pm_status_show
};

static int pm_debugfs_open(struct inode *inode, struct file *file)
{
	struct seq_file *seq_f = NULL;
	int ret = seq_open(file, &pm_status_sops);

	if (!ret) {
		seq_f = file->private_data;
		seq_f->private = inode->i_private;
	}

	return ret;
}

static const struct file_operations pm_debugfs_fops = {
	.owner = THIS_MODULE,
	.open = pm_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release
};
#endif

static int pm_debugfs_add(struct adf_pm *pm, struct adf_accel_dev *accel_dev)
{
	/* Create pm debug file */
	pm->debugfs_pm_status =
		debugfs_create_file("pm_status",
				    0400,
				    accel_dev->debugfs_dir,
				    accel_dev,
				    &pm_debugfs_fops);
	if (!pm->debugfs_pm_status) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to create pm_status debugfs entry.\n");
		return -EFAULT;
	}

	return 0;
}

static void pm_debugfs_del(struct adf_pm *pm)
{
	debugfs_remove(pm->debugfs_pm_status);
	pm->debugfs_pm_status = NULL;
}

static int check_pm_idle_support(struct adf_accel_dev *accel_dev)
{
	int pm_idle = PM_IDLE_UNSUPPORT;
	int ret = 0;

	/*
	 * Get pm_idle value from configuration file. In case of translation
	 * error, return unsupported.
	 */
	ret = get_cfg_pm_setting(accel_dev, &pm_idle, ADF_PM_IDLE_SUPPORT);
	if (ret)
		return PM_IDLE_UNSUPPORT;

	/*
	 * Only exact value of pm_idle identified as supported is translated
	 * as PM_IDLE_SUPPORT, any other value will be translated
	 * as unsupported.
	 */
	if (pm_idle == PM_IDLE_SUPPORT)
		return PM_IDLE_SUPPORT;
	else
		return PM_IDLE_UNSUPPORT;
}

static int init_dev_pm_struct(struct adf_accel_dev *accel_dev)
{
	struct adf_pm *pm = NULL;

	pm = kzalloc(sizeof(*pm), GFP_KERNEL);
	if (!pm)
		return -ENOMEM;
	memset(pm, 0, sizeof(struct adf_pm));

	pm->pm_irq_wq = alloc_workqueue("%s_%d",
					WQ_MEM_RECLAIM,
					1,
					"qat_pm_irq_wq",
					accel_dev->accel_id);
	if (!pm->pm_irq_wq) {
		kfree(pm);
		return -ENOMEM;
	}

	if (pm_debugfs_add(pm, accel_dev)) {
		destroy_workqueue(pm->pm_irq_wq);
		kfree(pm);
		return -EFAULT;
	}

	pm->idle_support = check_pm_idle_support(accel_dev);
	accel_dev->power_management = pm;

	return 0;
}

static void exit_dev_pm_struct(struct adf_accel_dev *accel_dev)
{
	struct adf_pm *pm = accel_dev->power_management;

	if (pm) {
		if (pm->pm_irq_wq)
			destroy_workqueue(pm->pm_irq_wq);
		pm->pm_irq_wq = NULL;
		pm_debugfs_del(pm);
		kfree(pm);
		accel_dev->power_management = NULL;
	}
}

static bool send_pm_host_msg(struct adf_accel_dev *accel_dev)
{
	u32 msg = 0;
	int times = 0;

	struct adf_pm *pm = accel_dev->power_management;
	void __iomem *pmisc =
		(&GET_BARS(accel_dev)[ADF_GEN4_PMISC_BAR])->virt_addr;
	int pm_idle = pm->idle_support;

	if ((ADF_CSR_RD(pmisc, ADF_GEN4_PM_HOST_MSG)
				& ADF_GEN4_PM_MSG_PENDING))
		return false;

	/* Send HOST_MSG */
	if (pm_idle == PM_IDLE_SUPPORT) {
		msg = PM_SET_MIN << ADF_GEN4_PM_MSG_PAYLOAD_BIT_OFFSET;
		accel_dev->power_management->host_ack_counter++;
	} else {
		msg = PM_NO_CHANGE << ADF_GEN4_PM_MSG_PAYLOAD_BIT_OFFSET;
		accel_dev->power_management->host_nack_counter++;
	}
	msg |= ADF_GEN4_PM_MSG_PENDING;
	ADF_CSR_WR(pmisc, ADF_GEN4_PM_HOST_MSG, msg);

	/* Wait for HOST_MSG completed */
	for (times = 0; times < ADF_GEN4_RD_LOOP_ITERATIONS; times++) {
		if (!(ADF_CSR_RD(pmisc, ADF_GEN4_PM_HOST_MSG)
					& ADF_GEN4_PM_MSG_PENDING))
			return true;
		msleep(ADF_GEN4_SLEEP_TIME_IN_MS);
	}

	return false;
}

void adf_gen4_pm_bh_handler(struct work_struct *work)
{
	struct adf_gen4_pm_irq *pm_irq =
		container_of(work, struct adf_gen4_pm_irq, pm_irq_work);
	u32 pm_int_sts = pm_irq->pm_int_sts;
	struct adf_accel_dev *accel_dev = pm_irq->accel_dev;
	struct adf_pm *pm = accel_dev->power_management;
	void __iomem *pmisc =
		(&GET_BARS(accel_dev)[ADF_GEN4_PMISC_BAR])->virt_addr;

	if (!pm) {
		dev_err(&GET_DEV(accel_dev),
			"Power manager already exit\n");
		goto exit;
	}

	/* PM Idle interrupt */
	if (pm_int_sts & ADF_GEN4_PM_IDLE_STS) {
		pm->idle_irq_counters++;
		/* Issue host message to FW */
		if (!send_pm_host_msg(accel_dev))
			dev_err(&GET_DEV(accel_dev),
				"Failed to send host msg to QAT\n");
	}

	/* PM throttle interrupt */
	if (pm_int_sts & ADF_GEN4_PM_THR_STS)
		pm->throttle_irq_counters++;

	/* PM fw interrupt */
	if (pm_int_sts & ADF_GEN4_PM_FM_INT_STS)
		pm->fw_irq_counters++;

	/* Clear interrupt status */
	ADF_CSR_WR(pmisc, ADF_GEN4_PM_INTERRUPT, pm_int_sts);

	/* Reenable PM interrupt */
	adf_csr_fetch_and_and(pmisc, ADF_GEN4_ERRMSK2,
			      ~ADF_GEN4_PM_INTERRUPT_MASK);

exit:
	kfree(pm_irq);
}

int adf_gen4_init_pm(struct adf_accel_dev *accel_dev)
{
	u32 idle_delay = ADF_GEN4_PM_IDLE_512_US;
	u32 val = 0;
	void __iomem *pmisc =
		(&GET_BARS(accel_dev)[ADF_GEN4_PMISC_BAR])->virt_addr;

	if (config_qat_pm(accel_dev, idle_delay))
		return -EFAULT;

	if (init_dev_pm_struct(accel_dev))
		return -EFAULT;

	/* Enable PM Idle interrupt */
	val = ADF_CSR_RD(pmisc, ADF_GEN4_PM_INTERRUPT);
	val |= ADF_GEN4_PM_IDLE_INT_EN;
	/* Clear interrupt status */
	val |= ADF_GEN4_PM_INT_STS_MASK;
	ADF_CSR_WR(pmisc, ADF_GEN4_PM_INTERRUPT, val);

	/* Enable PM interrupt */
	adf_csr_fetch_and_and(pmisc,
			      ADF_GEN4_ERRMSK2,
			      ~ADF_GEN4_PM_INTERRUPT_MASK);

	return 0;
}
EXPORT_SYMBOL_GPL(adf_gen4_init_pm);

void adf_gen4_exit_pm(struct adf_accel_dev *accel_dev)
{
	u32 val = 0;
	void __iomem *pmisc =
		(&GET_BARS(accel_dev)[ADF_GEN4_PMISC_BAR])->virt_addr;

	/* Disable interrupt */
	adf_csr_fetch_and_or(pmisc, ADF_GEN4_ERRMSK2,
			     ADF_GEN4_PM_INTERRUPT_MASK);

	val &= ~ADF_GEN4_PM_IDLE_INT_EN;
	/* Clear interrupt status */
	val |= ADF_GEN4_PM_INT_STS_MASK;
	ADF_CSR_WR(pmisc, ADF_GEN4_PM_INTERRUPT, val);

	exit_dev_pm_struct(accel_dev);
}
EXPORT_SYMBOL_GPL(adf_gen4_exit_pm);

int adf_gen4_set_pm_drv_active(struct adf_accel_dev *accel_dev)
{
	int times = 0;
	int initialized = 0;
	int qat_pm_state = 0;
	void __iomem *pmisc =
		(&GET_BARS(accel_dev)[ADF_GEN4_PMISC_BAR])->virt_addr;

	/* Disable interrupt during PM initialization */
	adf_csr_fetch_and_or(pmisc, ADF_GEN4_ERRMSK2,
			     ADF_GEN4_PM_INTERRUPT_MASK);

	/* Drv active */
	ADF_CSR_WR(pmisc, ADF_GEN4_PM_INTERRUPT, ADF_GEN4_PM_DRV_ACTIVE);

	/* Check if qat PM state is changed to INIT */
	for (times = 0; times < ADF_GEN4_RD_LOOP_ITERATIONS; times++) {
		qat_pm_state = ADF_CSR_RD(pmisc, ADF_GEN4_PM_STATUS);
		qat_pm_state = (qat_pm_state & ADF_GEN4_PM_STATE)
			>> ADF_GEN4_PM_STATE_BIT_OFFSET;
		if (qat_pm_state == ADF_GEN4_PM_INIT_STATE) {
			initialized = 1;
			break;
		}
	}
	if (!initialized) {
		dev_err(&GET_DEV(accel_dev),
			"Time out, failed to set pm drv_active\n");
		return -EFAULT;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(adf_gen4_set_pm_drv_active);

void adf_gen4_switch_drv_active(struct adf_accel_dev *accel_dev)
{
	struct adf_pm *pm = accel_dev->power_management;
	void __iomem *pmisc =
		(&GET_BARS(accel_dev)[ADF_GEN4_PMISC_BAR])->virt_addr;
	int pm_idle = pm->idle_support;

	/*
	 * Create rising edge of PM drv_active bit - write first 0
	 * to ensure having rising edge when writing 1. Rising edge
	 * is needed to properly trigger HW for transition to max pwr_state
	 * and to reset idle timer.
	 */
	if (pm_idle == PM_IDLE_SUPPORT) {
		/* Write 0 to drv_active bit */
		adf_csr_fetch_and_and(pmisc, ADF_GEN4_PM_INTERRUPT,
				      ~ADF_GEN4_PM_DRV_ACTIVE);
		/* Write 1 to drv_active bit */
		adf_csr_fetch_and_or(pmisc, ADF_GEN4_PM_INTERRUPT,
				     ADF_GEN4_PM_DRV_ACTIVE);
	}
}
EXPORT_SYMBOL_GPL(adf_gen4_switch_drv_active);

bool adf_gen4_pm_check_interrupts(struct adf_accel_dev *accel_dev)
{
	u32 errsou = 0;
	u32 errmsk = 0;
	u32 val_r = 0;
	struct adf_gen4_pm_irq *pm_irq = NULL;
	struct adf_pm *pm = accel_dev->power_management;
	void __iomem *pmisc =
		(&GET_BARS(accel_dev)[ADF_GEN4_PMISC_BAR])->virt_addr;

	/* Only handle the interrupt triggered by PM */
	errmsk = ADF_CSR_RD(pmisc, ADF_GEN4_ERRMSK2);
	if (errmsk & ADF_GEN4_PM_INTERRUPT_MASK)
		return false;

	/* Errsou2 */
	errsou = ADF_CSR_RD(pmisc, ADF_GEN4_ERRSOU2);
	if (!(errsou & ADF_GEN4_PM_INTERRUPT_MASK))
		return false;

	/* Disable interrupt */
	adf_csr_fetch_and_or(pmisc, ADF_GEN4_ERRMSK2,
			     ADF_GEN4_PM_INTERRUPT_MASK);

	val_r = ADF_CSR_RD(pmisc, ADF_GEN4_PM_INTERRUPT);

	pm_irq = kzalloc(sizeof(*pm_irq), GFP_ATOMIC);
	if (!pm_irq)
		return false;
	pm_irq->pm_int_sts = val_r;
	pm_irq->accel_dev = accel_dev;
	INIT_WORK(&pm_irq->pm_irq_work, adf_gen4_pm_bh_handler);
	queue_work(pm->pm_irq_wq, &pm_irq->pm_irq_work);

	return true;
}
EXPORT_SYMBOL_GPL(adf_gen4_pm_check_interrupts);
