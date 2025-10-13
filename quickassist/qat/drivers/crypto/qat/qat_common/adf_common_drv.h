/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2021 Intel Corporation */
#ifndef ADF_DRV_H
#define ADF_DRV_H

#ifndef USER_SPACE
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/device.h>
#include "adf_accel_devices.h"
#include "icp_qat_fw_loader_handle.h"
#include "icp_qat_hal.h"
#endif
#define SET_BIT(byte, bit) ((byte) |= ((uint32_t)1 << (bit)))
#define CLEAR_BIT(byte, bit) ((byte) &= ~((uint32_t)1 << (bit)))

#ifdef QAT_UIO
struct adf_uio_control_bundle;
#endif

#define ADF_MAJOR_VERSION	1
#define ADF_MINOR_VERSION	2
#define ADF_BUILD_VERSION	30
#define ADF_DRV_VERSION		__stringify(ADF_MAJOR_VERSION) "." \
				__stringify(ADF_MINOR_VERSION) "." \
				__stringify(ADF_BUILD_VERSION)

#define ADF_STATUS_RESTARTING 0
#define ADF_STATUS_STARTING 1
#define ADF_STATUS_CONFIGURED 2
#define ADF_STATUS_STARTED 3
#define ADF_STATUS_AE_INITIALISED 4
#define ADF_STATUS_AE_UCODE_LOADED 5
#define ADF_STATUS_AE_STARTED 6
#define ADF_STATUS_PF_RUNNING 7
#define ADF_STATUS_IRQ_ALLOCATED 8
#ifdef QAT_UIO
#define ADF_VF_PKG_ID_NONE 0xFFFF
#define ADF_STATUS_SRIOV_RESTARTING 9
#endif

#define ADF_PCIE_FLR_ATTEMPT 10

#define PCI_EXP_AERUCS 0x104

/* PMISC BAR upper and lower offsets in PCIe config space */
#define ADF_PMISC_L_OFFSET 0x18
#define ADF_PMISC_U_OFFSET 0x1c

enum adf_dev_reset_mode {
	ADF_DEV_RESET_ASYNC = 0,
	ADF_DEV_RESET_SYNC
};

enum adf_event {
	ADF_EVENT_INIT = 0,
	ADF_EVENT_START,
	ADF_EVENT_STOP,
	ADF_EVENT_SHUTDOWN,
	ADF_EVENT_RESTARTING,
	ADF_EVENT_RESTARTED,
	ADF_EVENT_ERROR,
	ADF_EVENT_SLA_UPDATE,
	ADF_EVENT_NOP,
};

#ifndef USER_SPACE
#define ADF_DEV_STATUS_ARRAY_SIZE BITS_TO_LONGS((ADF_MAX_DEVICES))
struct service_hndl {
	int (*event_hld)(struct adf_accel_dev *accel_dev,
			 enum adf_event event);
	unsigned long init_status[ADF_DEV_STATUS_ARRAY_SIZE];
	unsigned long start_status[ADF_DEV_STATUS_ARRAY_SIZE];
	char *name;
	struct list_head list;
};

struct vf_id_map {
	int domain_nr;
	u32 bdf;
	u32 id;
	u32 fake_id;
	bool attached;
	struct list_head list;
};

static inline int get_current_node(void)
{
	int ret;

	preempt_disable();
	ret = topology_physical_package_id(smp_processor_id());
	preempt_enable();
	return ret;
}

int adf_service_register(struct service_hndl *service);
int adf_service_unregister(struct service_hndl *service);

int adf_dev_init(struct adf_accel_dev *accel_dev);
int adf_dev_start(struct adf_accel_dev *accel_dev);
void adf_dev_stop(struct adf_accel_dev *accel_dev);
void adf_dev_shutdown(struct adf_accel_dev *accel_dev);
int adf_dev_autoreset(struct adf_accel_dev *accel_dev);
int adf_dev_reset(struct adf_accel_dev *accel_dev,
		  enum adf_dev_reset_mode mode);
int adf_dev_aer_schedule_reset(struct adf_accel_dev *accel_dev,
			       enum adf_dev_reset_mode mode);
void adf_dev_ref_fixup(struct adf_accel_dev *accel_dev);
void adf_error_event(struct adf_accel_dev *accel_dev);
void adf_error_notifier(uintptr_t arg);
void adf_error_notifier_wq(struct work_struct *data);
int adf_init_fatal_error_wq(void);
void adf_exit_fatal_error_wq(void);
int adf_iov_putmsg(struct adf_accel_dev *accel_dev, u32 msg_type, u32 msg_data,
		   u8 vf_nr);
int adf_iov_notify(struct adf_accel_dev *accel_dev, u32 msg_type, u32 msg_data,
		   u8 vf_nr);
int adf_pf2vf_block_get_byte(struct adf_accel_dev *accel_dev, u8 msg_type,
			     u8 byte_num, u8 *data, u8 compatibility,
			     u32 vf_nr);
int adf_pf2vf_block_get_crc(struct adf_accel_dev *accel_dev, u8 msg_type,
			    u8 byte_num, u8 *data, u8 compatibility, u32 vf_nr);
void adf_pf2vf_notify_restarting(struct adf_accel_dev *accel_dev);
void adf_vf2pf_wait_for_restarting_complete(struct adf_accel_dev *accel_dev);
int adf_notify_fatal_error(struct adf_accel_dev *accel_dev);
void adf_pf2vf_notify_fatal_error(struct adf_accel_dev *accel_dev);
typedef int (*adf_iov_block_provider)(struct adf_accel_dev *accel_dev,
				      u8 **buffer, u8 *length,
				      u8 *block_version, u8 compatibility,
				      u8 byte_num, u32 vf_nr);
int adf_iov_block_provider_register(u8 block_type,
				    const adf_iov_block_provider provider);
u8 adf_iov_is_block_provider_registered(u8 block_type);
adf_iov_block_provider adf_iov_get_block_provider(u8 msg_type);
int adf_iov_block_provider_unregister(u8 block_type,
				      const adf_iov_block_provider provider);
int adf_iov_block_get(struct adf_accel_dev *accel_dev, u8 block_type,
		      u8 *block_version, u8 *buffer, u8 *length);
u8 adf_pfvf_crc(u8 start_crc, u8 *buf, u8 len);
int adf_iov_register_compat_checker(struct adf_accel_dev *accel_dev,
				    struct adf_accel_compat_manager *cm,
				    const adf_iov_compat_checker_t cc);
int adf_iov_unregister_compat_checker(struct adf_accel_dev *accel_dev,
				      struct adf_accel_compat_manager *cm,
				      const adf_iov_compat_checker_t cc);
int adf_pf_enable_vf2pf_comms(struct adf_accel_dev *accel_dev);
int adf_pf_disable_vf2pf_comms(struct adf_accel_dev *accel_dev);
int adf_iov_init_compat_manager(struct adf_accel_dev *accel_dev,
				struct adf_accel_compat_manager **cm);
int adf_iov_shutdown_compat_manager(struct adf_accel_dev *accel_dev,
				    struct adf_accel_compat_manager **cm);
int adf_enable_vf2pf_comms(struct adf_accel_dev *accel_dev);
int adf_disable_vf2pf_comms(struct adf_accel_dev *accel_dev);
int adf_iov_compatibility_check(struct adf_accel_dev *accel_dev,
				struct adf_accel_compat_manager *cm,
				u8 compat_ver);
void adf_vf2pf_req_hndl(struct adf_accel_vf_info *vf_info);
int adf_pfvf_debugfs_add(struct adf_accel_dev *accel_dev);
void adf_devmgr_update_class_index(struct adf_hw_device_data *hw_data);
unsigned int adf_devmgr_get_id(struct pci_dev *pdev);
void adf_clean_vf_map(bool vf);
int adf_pf_vf_ring_to_svc_init(struct adf_accel_dev *accel_dev);

int adf_pf_vf_capabilities_init(struct adf_accel_dev *accel_dev);
int adf_cfg_get_services_enabled(struct adf_accel_dev *accel_dev,
				 u16 *def_serv_mask);
int adf_processes_dev_register(void);
void adf_processes_dev_unregister(void);

int adf_devmgr_add_dev(struct adf_accel_dev *accel_dev,
		       struct adf_accel_dev *pf);
void adf_devmgr_rm_dev(struct adf_accel_dev *accel_dev,
		       struct adf_accel_dev *pf);
struct list_head *adf_devmgr_get_head(void);
int adf_devmgr_get_real_id(uint32_t fake_id);
struct adf_accel_dev *adf_devmgr_get_dev_by_id(uint32_t id);
struct adf_accel_dev *adf_devmgr_get_first(void);
struct adf_accel_dev *adf_devmgr_pci_to_accel_dev(struct pci_dev *pci_dev);
int adf_get_num_dettached_vfs(void);
int adf_get_vf_id(struct adf_accel_dev *vf, struct adf_accel_dev *pf);
int adf_devmgr_verify_id(uint32_t *id);
void adf_devmgr_get_num_dev(uint32_t *num);
int adf_devmgr_in_reset(struct adf_accel_dev *accel_dev);
int adf_dev_started(struct adf_accel_dev *accel_dev);
void adf_devmgr_update_drv_rm(u16 dev_id);
int adf_dev_restarting_notify(struct adf_accel_dev *accel_dev);
int adf_dev_restarted_notify(struct adf_accel_dev *accel_dev);
int adf_dev_restarting_notify_sync(struct adf_accel_dev *accel_dev);
int adf_ae_init(struct adf_accel_dev *accel_dev);
int adf_ae_shutdown(struct adf_accel_dev *accel_dev);
int adf_ae_fw_load(struct adf_accel_dev *accel_dev);
void adf_ae_fw_release(struct adf_accel_dev *accel_dev);
int adf_ae_start(struct adf_accel_dev *accel_dev);
int adf_ae_stop(struct adf_accel_dev *accel_dev);

int adf_aer_store_ppaerucm_reg(struct pci_dev *pdev,
			       struct adf_hw_device_data *hw_data);
int adf_enable_aer(struct adf_accel_dev *accel_dev, struct pci_driver *adf);
void adf_disable_aer(struct adf_accel_dev *accel_dev);
void adf_reset_sbr(struct adf_accel_dev *accel_dev);
void adf_reset_flr(struct adf_accel_dev *accel_dev);
void adf_pf2vf_bh_handler(void *data);
void adf_dev_pre_reset(struct adf_accel_dev *accel_dev);
void adf_dev_post_reset(struct adf_accel_dev *accel_dev);
void adf_dev_restore(struct adf_accel_dev *accel_dev);
int adf_init_aer(void);
void adf_exit_aer(void);
int adf_init_admin_comms(struct adf_accel_dev *accel_dev);
void adf_exit_admin_comms(struct adf_accel_dev *accel_dev);
struct icp_qat_fw_init_admin_req;
struct icp_qat_fw_init_admin_resp;
int adf_send_admin(struct adf_accel_dev *accel_dev,
		   struct icp_qat_fw_init_admin_req *req,
		   struct icp_qat_fw_init_admin_resp *resp,
		   u32 ae_mask);
int adf_send_admin_init(struct adf_accel_dev *accel_dev);
int adf_get_fw_pke_stats(struct adf_accel_dev *accel_dev,
			 u64 *suc_counter,
			 u64 *unsuc_counter);
int adf_get_fw_timestamp(struct adf_accel_dev *accel_dev, u64 *timestamp);
int adf_dev_measure_clock(struct adf_accel_dev *accel_dev, u32 *frequency,
			  u32 min, u32 max);
int adf_clock_debugfs_add(struct adf_accel_dev *accel_dev);
u64 adf_clock_get_current_time(void);
int adf_init_arb(struct adf_accel_dev *accel_dev);
void adf_exit_arb(struct adf_accel_dev *accel_dev);
void adf_disable_arb(struct adf_accel_dev *accel_dev);
void adf_update_ring_arb(struct adf_etr_ring_data *ring);
int adf_set_ssm_wdtimer(struct adf_accel_dev *accel_dev);
#ifdef QAT_HB_FAIL_SIM
void adf_write_csr_arb_wrk_2_ser_map(void *csr_addr, u32 csr_offset,
				     u32 wrk_to_ser_map_offset,
				     size_t index, u32 value);
#endif
void adf_enable_ring_arb(struct adf_accel_dev *accel_dev, void *csr_addr,
			 u32 bank, unsigned int mask);
void adf_disable_ring_arb(struct adf_accel_dev *accel_dev, void *csr_addr,
			  u32 bank, unsigned int mask);
int adf_rate_limiting_init(struct adf_accel_dev *accel_dev);
void adf_rate_limiting_exit(struct adf_accel_dev *accel_dev);
struct adf_accel_dev *
adf_devmgr_get_dev_by_bdf(struct adf_pci_address *pci_addr);
struct adf_accel_dev *
adf_devmgr_get_dev_by_pci_domain_bus(struct adf_pci_address *pci_addr);
#ifdef QAT_UIO
void adf_update_uio_ring_arb(struct adf_uio_control_bundle *bundle);
#ifdef QAT_KPT
void adf_update_kpt_wrk_arb(struct adf_accel_dev *accel_dev);
int adf_dev_enable_kpt(struct adf_accel_dev *accel_dev);
#endif
int adf_config_device(struct adf_accel_dev *accel_dev);
#endif

int adf_put_admin_msg_sync(struct adf_accel_dev *accel_dev, u32 ae,
			   void *in, void *out);
int adf_dev_get(struct adf_accel_dev *accel_dev);
void adf_dev_put(struct adf_accel_dev *accel_dev);
int adf_dev_in_use(struct adf_accel_dev *accel_dev);
int adf_init_etr_data(struct adf_accel_dev *accel_dev);
void adf_cleanup_etr_data(struct adf_accel_dev *accel_dev);
int qat_crypto_register(void);
int qat_crypto_unregister(void);
int qat_crypto_dev_config(struct adf_accel_dev *accel_dev);
int qat_crypto_vf_dev_config(struct adf_accel_dev *accel_dev);
struct qat_crypto_instance *qat_crypto_get_instance_node(int node,
							 u8 service_type);
void qat_crypto_put_instance(struct qat_crypto_instance *inst);
void qat_alg_callback(void *resp);
void qat_alg_asym_callback(void *resp);
int qat_algs_register(void);
void qat_algs_unregister(void);
int qat_asym_algs_register(void);
void qat_asym_algs_unregister(void);
int adf_lkca_register(struct adf_accel_dev *accel_dev);
void adf_lkca_unregister(struct adf_accel_dev *accel_dev);

int adf_isr_resource_alloc(struct adf_accel_dev *accel_dev);
void adf_isr_resource_free(struct adf_accel_dev *accel_dev);
int adf_vf_isr_resource_alloc(struct adf_accel_dev *accel_dev);
void adf_vf_isr_resource_free(struct adf_accel_dev *accel_dev);

int qat_hal_init(struct adf_accel_dev *accel_dev);
void qat_hal_deinit(struct icp_qat_fw_loader_handle *handle);
int qat_hal_start(struct icp_qat_fw_loader_handle *handle);
void qat_hal_stop(struct icp_qat_fw_loader_handle *handle, unsigned char ae,
		  unsigned int ctx_mask);
void qat_hal_reset(struct icp_qat_fw_loader_handle *handle);
int qat_hal_clr_reset(struct icp_qat_fw_loader_handle *handle);
void qat_hal_set_live_ctx(struct icp_qat_fw_loader_handle *handle,
			  unsigned char ae, unsigned int ctx_mask);
int qat_hal_check_ae_active(struct icp_qat_fw_loader_handle *handle,
			    unsigned int ae);
int qat_hal_set_ae_lm_mode(struct icp_qat_fw_loader_handle *handle,
			   unsigned char ae, enum icp_qat_uof_regtype lm_type,
			   unsigned char mode);
void qat_hal_set_ae_tindex_mode(struct icp_qat_fw_loader_handle *handle,
				unsigned char ae,
				unsigned char mode);
void qat_hal_set_ae_scs_mode(struct icp_qat_fw_loader_handle *handle,
			     unsigned char ae,
			     unsigned char mode);
int qat_hal_set_ae_ctx_mode(struct icp_qat_fw_loader_handle *handle,
			    unsigned char ae, unsigned char mode);
int qat_hal_set_ae_nn_mode(struct icp_qat_fw_loader_handle *handle,
			   unsigned char ae, unsigned char mode);
void qat_hal_set_pc(struct icp_qat_fw_loader_handle *handle,
		    unsigned char ae, unsigned int ctx_mask, unsigned int upc);
void qat_hal_wr_uwords(struct icp_qat_fw_loader_handle *handle,
		       unsigned char ae, unsigned int uaddr,
		       unsigned int words_num, u64 *uword);
int qat_hal_wr_coalesce_uwords(struct icp_qat_fw_loader_handle *handle,
			       unsigned char ae,
			       unsigned int uaddr,
			       unsigned int words_num,
			       u64 *uword);

void qat_hal_wr_umem(struct icp_qat_fw_loader_handle *handle, unsigned char ae,
		     unsigned int uword_addr, unsigned int words_num,
		     unsigned int *data);
int qat_hal_get_ins_num(void);
int qat_hal_batch_wr_lm(struct icp_qat_fw_loader_handle *handle,
			unsigned char ae,
			struct icp_qat_uof_batch_init *lm_init_header);
int qat_hal_init_gpr(struct icp_qat_fw_loader_handle *handle,
		     unsigned char ae, unsigned long ctx_mask,
		     enum icp_qat_uof_regtype reg_type,
		     unsigned short reg_num, unsigned int regdata);
int qat_hal_init_wr_xfer(struct icp_qat_fw_loader_handle *handle,
			 unsigned char ae, unsigned long ctx_mask,
			 enum icp_qat_uof_regtype reg_type,
			 unsigned short reg_num, unsigned int regdata);
int qat_hal_init_rd_xfer(struct icp_qat_fw_loader_handle *handle,
			 unsigned char ae, unsigned long ctx_mask,
			 enum icp_qat_uof_regtype reg_type,
			 unsigned short reg_num, unsigned int regdata);
int qat_hal_init_nn(struct icp_qat_fw_loader_handle *handle,
		    unsigned char ae, unsigned long ctx_mask,
		    unsigned short reg_num, unsigned int regdata);
int qat_hal_wr_lm(struct icp_qat_fw_loader_handle *handle,
		  unsigned char ae, unsigned short lm_addr, unsigned int value);
int qat_uclo_wr_all_uimage(struct icp_qat_fw_loader_handle *handle);
void qat_uclo_del_obj(struct icp_qat_fw_loader_handle *handle);
void qat_uclo_del_mof(struct icp_qat_fw_loader_handle *handle);
int qat_uclo_wr_mimage(struct icp_qat_fw_loader_handle *handle, void *addr_ptr,
		       int mem_size);
int qat_uclo_map_obj(struct icp_qat_fw_loader_handle *handle,
		     void *addr_ptr, u32 mem_size, char *obj_name);
u32 qat_uclo_get_aux_ae_mask(struct adf_fw_loader_data *handle, int max_objs,
			     int num_obj);

void qat_hal_get_scs_neigh_ae(unsigned char ae, unsigned char *ae_neigh);
int qat_uclo_set_cfg_ae_mask(struct icp_qat_fw_loader_handle *handle,
			     unsigned int cfg_ae_mask);
void adf_exit_vqat_wq(void);
void adf_flush_vqat_wq(void);
int adf_enable_vqat_iov(struct adf_accel_dev *accel_dev);
int adf_disable_vqat_iov(struct adf_accel_dev *accel_dev);
int adf_vqat_isr_resource_alloc(struct adf_accel_dev *accel_dev);
void adf_vqat_isr_resource_free(struct adf_accel_dev *accel_dev);
#if defined(CONFIG_PCI_IOV)
void adf_configure_iov_threads(struct adf_accel_dev *accel_dev, bool enable);
int adf_sriov_configure(struct pci_dev *pdev, int numvfs);
void adf_disable_sriov(struct adf_accel_dev *accel_dev);
void adf_vf2pf_handler(struct adf_accel_vf_info *vf_info);

int adf_vf2pf_init(struct adf_accel_dev *accel_dev);
void adf_vf2pf_shutdown(struct adf_accel_dev *accel_dev);
void adf_vf2pf_restarting_complete(struct adf_accel_dev *accel_dev);
int adf_init_pf_wq(void);
void adf_exit_pf_wq(void);
int adf_init_vf_wq(void);
void adf_exit_vf_wq(void);
void adf_flush_vf_wq(void);
void adf_get_vf_rl_info(struct adf_accel_dev *accel_dev);
void adf_pf_sla_value_provider(struct adf_accel_dev *accel,
			       u32 ring_num, u32 *value, u32 msg_type);
void adf_pf2vf_notify_sla_update(struct adf_accel_dev *accel_dev,
				 u16 ring_id, u32 val, int msg_type);

#else
static inline void adf_configure_iov_threads(struct adf_accel_dev *accel_dev,
					     bool enable)
{
}

static inline int adf_sriov_configure(struct pci_dev *pdev, int numvfs)
{
	return 0;
}

static inline void adf_disable_sriov(struct adf_accel_dev *accel_dev)
{
}

static inline void adf_vf2pf_handler(struct adf_accel_vf_info *vf_info)
{
}

static inline int adf_vf2pf_init(struct adf_accel_dev *accel_dev)
{
	return 0;
}

static inline void adf_vf2pf_shutdown(struct adf_accel_dev *accel_dev)
{
}

static inline int adf_init_pf_wq(void)
{
	return 0;
}

static inline void adf_exit_pf_wq(void)
{
}

static inline int adf_init_vf_wq(void)
{
	return 0;
}

static inline void adf_exit_vf_wq(void)
{
}

static inline void adf_flush_vf_wq(void)
{
}

static inline void adf_get_vf_rl_info(struct adf_accel_dev *accel_dev)
{
}

static inline void adf_pf_sla_value_provider(struct adf_accel_dev *accel_dev,
					     u32 ring_num, u32 *value,
					     u32 msg_type)
{
}
#endif

static inline
void __iomem *adf_get_pmisc_base(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct adf_bar *pmisc;

	pmisc = &GET_BARS(accel_dev)[hw_data->get_misc_bar_id(hw_data)];

	return pmisc->virt_addr;
}

static inline
void __iomem *adf_get_param_base(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct adf_bar *param;

	param = &GET_BARS(accel_dev)[hw_data->get_sram_bar_id(hw_data)];

	return param->virt_addr;
}

#endif
#endif
