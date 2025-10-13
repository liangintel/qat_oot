/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2019 - 2021 Intel Corporation */

#ifndef ADF_VDCM_H
#define ADF_VDCM_H

#include <linux/pci.h>
#include "adf_accel_devices.h"
#include "adf_vdcm_iov.h"
#include "adf_vdcm_caps.h"

#define PCI_CAP_ID_BASIC 0

/* Definition of read-only mask */
#define PCI_VENDOR_DEVICE_RO_MASK	0xffffffff
#define PCI_COMMAND_STATUS_RO_MASK	0x06fffab8

/* Definition of write-1-to-clear mask mask */
#define PCI_COMMAND_STATUS_WOC_MASK	0xf9000000

/* Definition of virtual QAT device ID*/
#define ADF_VQAT_PCI_DEVICE_ID 0x0da5

/* Definition of virtual QAT subsystem ID*/
#define ADF_VQAT_SYM_PCI_SUBSYSTEM_ID 0x00
#define ADF_VQAT_ASYM_PCI_SUBSYSTEM_ID 0x01
#define ADF_VQAT_DC_PCI_SUBSYSTEM_ID 0x02

/* Definition of bars*/
#define ADF_VQAT_ETR_BAR	0
#define ADF_VQAT_PMISC_BAR	1
#define ADF_VQAT_EXT_BAR	2
#define ADF_VQAT_MAX_BAR	3
#define ADF_VQAT_ETR_BAR_SIZE	0x2000
#define ADF_VQAT_PMISC_BAR_SIZE	0x20000
#define ADF_VQAT_EXT_BAR_SIZE	0x1000

/* Layout of ETR bar slow path */
#define ADF_VQAT_R0_CONFIG	0x1000
#define ADF_VQAT_R1_CONFIG	0x1004
#define ADF_VQAT_R0_LBASE	0x1040
#define ADF_VQAT_R1_LBASE	0x1044
#define ADF_VQAT_R0_UBASE	0x1080
#define ADF_VQAT_R1_UBASE	0x1084
#define ADF_VQAT_RPRESETCTL	0x1100
#define ADF_VQAT_RPRESETSTS	0x1104

/* Layout of ETR bar fast path */
#define ADF_VQAT_R0_HEAD	0x0C0
#define ADF_VQAT_R1_HEAD	0x0C4
#define ADF_VQAT_R0_TAIL	0x100
#define ADF_VQAT_R1_TAIL	0x104

/* Layout of MISC bar slow path */
#define ADF_VQAT_VINTSOU		0x0
#define ADF_VQAT_VINTMSK		0x4
#define ADF_VQAT_MSGQ_CFG		0x20
#define ADF_VQAT_MSGQ_TX_NOTIFIER	0x30
#define ADF_VQAT_MSGQ_RX_NOTIFIER	0x130
#define ADF_VQAT_MSGQ_NOTIFIER_MASK	0xffff

/* Bit fields of VQAT registers */
#define ADF_VQAT_VINT_PF2VM_OFFSET	0

enum vqat_type {
	/* VF based vQAT must be added between
	 * QAT_VQAT_TYPE_VF_MIN and QAT_VQAT_TYPE_VF_MAX,
	 * and ADI based vQAT must be added between
	 * QAT_VQAT_ADI_RP_MIN and QAT_VQAT_ADI_RP_MAX
	 */
	QAT_VQAT_TYPE_VF_MIN,
	QAT_VQAT_TYPE_VF = QAT_VQAT_TYPE_VF_MIN,
	QAT_VQAT_TYPE_VF_MAX,
	QAT_VQAT_ADI_RP_MIN = QAT_VQAT_TYPE_VF_MAX,
	QAT_VQAT_ADI_RP_SYM = QAT_VQAT_ADI_RP_MIN,
	QAT_VQAT_ADI_RP_ASYM,
	QAT_VQAT_ADI_RP_DC,
	QAT_VQAT_ADI_RP_MAX,
	QAT_VQAT_TYPES_MAX = QAT_VQAT_ADI_RP_MAX,
};

/*
 * Definition of register MsgQueueCfg
 * bit<0:5>: Number of queuess vQAT provides
 * bit<6:15>: Offset of queue0 in PAGE_SIZE
 * bit<16:19>: Queue size in power of 2
 * bit<20:31>: Reserved
 */
static inline void adf_iov_vdcm_get_msgq_info(u32 msgq_cfg, u8 *num,
					      u32 *start, u16 *size)
{
	*num = msgq_cfg & GENMASK(5, 0);
	*start = (msgq_cfg & GENMASK(15, 6)) >> 6 << PAGE_SHIFT;
	*size = BIT((msgq_cfg & GENMASK(19, 16)) >> 16);
}

static inline u32 adf_iov_vdcm_build_msgqcfg(u8 num, u32 start, u16 size)
{
	if (size && num)
		return (num | (start >> PAGE_SHIFT << 6) |
			((fls(size) - 1) << 16));
	else
		return 0;
}

#ifdef CONFIG_CRYPTO_DEV_QAT_VDCM
#include <linux/version.h>
#include <linux/irqbypass.h>
#define CONFIG_SIOV_MSIX
#define ADF_VQAT_MSGQ_SIZE		256
#define ADF_VQAT_BAR_NAME_SIZE		8
#define ADF_VQAT_IRQ_NAME_SIZE		16
#define QAT_VQAT_TYPE_NAME_MAX_LEN	32
#define QAT_VQAT_TYPE_VF_NAME		"vqat_vf"
#define QAT_VQAT_TYPE_ADI_SYM_NAME	"vqat_sym"
#define QAT_VQAT_TYPE_ADI_ASYM_NAME	"vqat_asym"
#define QAT_VQAT_TYPE_ADI_DC_NAME	"vqat_dc"

/* Layout of message queue in MISC bar!
 * Seen by VDCM only. vQAT driver gets these
 * info from msgq_cfg.
 */
#define ADF_VQAT_MSGQ_BAR_OFS	0x10000
#define ADF_VQAT_MSGQ_MEMSIZE	PAGE_SIZE
#define ADF_VQAT_MSGQ_NUMBER	1
#define ADF_VQAT_MSGQ_SIZE	256

union pcie_reg {
	u8 mem[4];
	u32 val;
};

struct adf_vcfg_attr_desc {
	u32 ro_mask; /* Read-only mask */
	u32 woc_mask; /* Write-1-to-clear mask */
};

struct adf_vcfg_vreg_meta {
	struct adf_vcfg_attr_desc *attr_desc;

	int (*access_hdl)(struct adf_vdcm_vqat *vqat,
			  bool is_write,
			  unsigned int offset,
			  void *data,
			  unsigned int len,
			  void *reg_addr);
};

struct adf_vqat_pci_cap_meta {
	u16 cap_id;
	u32 offset;
	u32 size;
	u32 num_regs;
	bool is_ext_cap;
	void *default_regmap;
	struct adf_vcfg_vreg_meta *vreg_arr;
};

struct adf_vcfg_container {
	u32 size;
	u32 ext_size;
	u8 *regmap;
	u32 num_caps;
	u16 dev_id;
	u16 subsystem_id;
	struct adf_vqat_pci_cap_meta *caps;
	struct adf_vqat_pci_cap_meta *last_pci_cap;
};

enum adf_vqat_irq {
	ADF_VQAT_MISC_IRQ,
	ADF_VQAT_RING_IRQ,
	ADF_VQAT_IRQ_MAX,
};

enum adf_vqat_irq_op {
	ADF_VQAT_IRQ_DISABLE,
	ADF_VQAT_IRQ_ENABLE,
};

/* Layout of msix table and pba in MISC bar!
 * Seen by VDCM only. vQAT driver gets these
 * info from msix capability
 */
#define ADF_VQAT_MSIX_TABLE_OFS		0x8000
#define ADF_VQAT_MSIX_TABLE_SIZE	\
	(ADF_VQAT_IRQ_MAX * PCI_MSIX_ENTRY_SIZE)
#define ADF_VQAT_MSIX_PBA_OFS		0x8800
#define ADF_VQAT_MSIX_PBA_SIZE_BITS	round_up(ADF_VQAT_IRQ_MAX, 64)
#define ADF_VQAT_MSIX_PBA_SIZE (ADF_VQAT_MSIX_PBA_SIZE_BITS / BITS_PER_BYTE)

enum adf_vqat_class_op_func {
	ADF_VDCM_GET_NUM_AVAIL_INSTS,
	ADF_VDCM_NOTIFY_PARENT_REGISTER,
	ADF_VDCM_NOTIFY_PARENT_UNREGISTER,
};

struct adf_vdcm_obj_mgr;
struct adf_vqat_class {
	enum vqat_type type;
	struct adf_vdcm_obj_mgr *cap_mgr;
	struct adf_vdcm_vqat_ops *ops;
	struct attribute_group *ag;
	void *class_data;
	/* The lock for vqat class */
	struct mutex class_lock;
};

static inline
enum vqat_type adf_vqat_class_type(struct adf_vqat_class *dclass)
{
	return dclass->type;
}

static inline
struct adf_vdcm_vqat_ops *adf_vqat_class_ops(struct adf_vqat_class *dclass)
{
	return dclass->ops;
}

struct adf_vqat_sub_mmap_area {
	phys_addr_t pbase;
	/* Maximum size of vQAT bar is 4GB. */
	u32 bar_ofs;
	u32 size;
	u8 is_io:1;
};

#define ADF_VQAT_BAR_MMIO	0
#define ADF_VQAT_BAR_MEM	1
#define ADF_VQAT_BAR_MIX	2
struct adf_vqat_bar {
	char name[ADF_VQAT_BAR_NAME_SIZE];
	resource_size_t size;
	phys_addr_t base_addr;
	void *virt_addr;
	u32 attr;
	u8 type;
	u8 total_sub_mmap_areas;
	u8 num_sub_mmap_area;
	/* For alignment */
	u8 reserved;
	struct adf_vqat_sub_mmap_area *sub_mmap_areas;
};

struct adf_vqat_irq_ctx {
	struct eventfd_ctx *trigger;
	struct irq_bypass_producer *producer;
	void (*set_irq)(struct adf_vqat_irq_ctx *ctx,
			enum adf_vqat_irq_op irq_op);
	void *data;
	char name[ADF_VQAT_IRQ_NAME_SIZE];
};

static inline
int adf_vqat_irq_ctx_init(struct adf_vqat_irq_ctx *ctx,
			  char *name,
			  void (*set_irq)(struct adf_vqat_irq_ctx *ctx,
					  enum adf_vqat_irq_op irq_op),
			  void *data)
{
	ctx->producer = NULL;
	ctx->trigger = NULL;
	ctx->set_irq = set_irq;
	ctx->data = data;
	if (name)
		strscpy(ctx->name, name, ADF_VQAT_IRQ_NAME_SIZE);
	else
		ctx->name[0] = 0;

	return 0;
}

static inline
int adf_vqat_irq_ctx_set_irq_info(struct adf_vqat_irq_ctx *ctx,
				  u32 irq, u8 is_hirq)
{
	if (is_hirq) {
		if (ctx->producer)
			return -EBUSY;
		ctx->producer = kzalloc(sizeof(*ctx->producer), GFP_KERNEL);
		if (!ctx->producer)
			return -ENOMEM;
		ctx->producer->irq = irq;
	}

	return 0;
}

static inline void adf_vqat_irq_ctx_cleanup(struct adf_vqat_irq_ctx *ctx)
{
	kfree(ctx->producer);
	ctx->producer = NULL;
	ctx->set_irq = NULL;
	ctx->name[0] = 0;
}

static inline
void adf_vqat_irq_ctx_set_trigger(struct adf_vqat_irq_ctx *ctx,
				  struct eventfd_ctx *trigger)
{
	ctx->trigger = trigger;
}

static inline
struct eventfd_ctx *adf_vqat_irq_ctx_trigger(struct adf_vqat_irq_ctx *ctx)
{
	return ctx->trigger;
}

static inline
char *adf_vqat_irq_ctx_name(struct adf_vqat_irq_ctx *ctx)
{
	return ctx->name;
}

static inline
int adf_vqat_irq_ctx_irq_no(struct adf_vqat_irq_ctx *ctx)
{
	if (ctx->producer)
		return ctx->producer->irq;
	return -EINVAL;
}

struct adf_vdcm_vqat_msix_info {
	u8 entries[ADF_VQAT_IRQ_MAX][PCI_MSIX_ENTRY_SIZE];
	u64 pba[ADF_VQAT_MSIX_PBA_SIZE_BITS / 64];
};

struct adf_vdcm_vqat_msgq {
	phys_addr_t pbase;
	void *vbase;
	u32 tx_notifier;
	u32 rx_notifier;
};

struct debug_info {
	struct dentry *dev_dbgdir;
	struct dentry *status_dbgdir;
	struct dentry *ctrl_dbgdir;
	struct dentry *etr_dbg;
	struct dentry *misc_dbg;
};

struct adf_vdcm_ctx_blk;
struct adf_vdcm_vqat {
	struct list_head list;
	struct adf_accel_dev *parent;
	struct mdev_device *mdev;
	struct adf_vdcm_vqat_ops *ops;
	struct adf_vcfg_container *vcfg;
	int irqs;
	struct adf_vqat_irq_ctx *irq_ctx;
	struct eventfd_ctx *req_trigger;
	/* The lock for vqat device */
	struct mutex vdev_lock;
	struct list_head prop_list;
	struct adf_vqat_class *dclass;
	struct debug_info debug;
	void *group;
	void *hw_priv;
	struct adf_vqat_bar bar[ADF_VQAT_MAX_BAR];
	struct adf_vdcm_vqat_msix_info msix_info;

	struct adf_iov_vx_agent iov_agent;
	struct adf_vdcm_vqat_msgq iov_msgq;
	struct adf_vdcm_vqat_cap vcap;
	/* The misc interrupt registers stuff */
	u32 msgqcfg;
	u32 vintsrc;
	u32 vintmsk;
};

struct adf_vdcm_vqat_ops {
	int (*class_handler)(struct adf_vqat_class *dclass,
			     struct adf_accel_dev *parent,
			     enum adf_vqat_class_op_func func,
			     void *func_data);
	struct adf_vdcm_vqat *(*create)(struct adf_accel_dev *parent,
					struct mdev_device *mdev,
					struct adf_vqat_class *dclass);
	void (*destroy)(struct adf_accel_dev *parent,
			struct adf_vdcm_vqat *vqat);
	int (*prepare_cap)(struct adf_vqat_enabled_caps *enabled,
			   struct adf_accel_dev *parent,
			   enum vqat_type type);
	int (*populate_cap)(struct adf_vdcm_vqat_cap *vcap,
			    struct adf_accel_dev *parent,
			    struct adf_vqat_enabled_caps *enabled,
			    enum vqat_type type);
	int (*open)(struct adf_vdcm_vqat *vqat);
	void (*release)(struct adf_vdcm_vqat *vqat);
	int (*cfg_read)(struct adf_vdcm_vqat *vqat, unsigned int pos,
			void *buf, unsigned int count);
	int (*cfg_write)(struct adf_vdcm_vqat *vqat, unsigned int pos,
			 void *buf, unsigned int count);
	int (*mmio_read)(struct adf_vdcm_vqat *vqat, int bar, u64 pos,
			 void *buf, unsigned int count);
	int (*mmio_write)(struct adf_vdcm_vqat *vqat, int bar, u64 pos,
			  void *buf, unsigned int count);
	int (*reset)(struct adf_vdcm_vqat *vqat);
};

int adf_vdcm_init(void);
int adf_vdcm_cleanup(void);
int adf_vdcm_vqat_reset_config(struct adf_vdcm_vqat *vqat);
int adf_vdcm_vqat_bar_init(struct adf_vqat_bar *vbar, char *name,
			   phys_addr_t phys_addr, void *virt_addr,
			   size_t size, bool readable, bool writable,
			   u8 type, u16 total_sub_mmaps);
void adf_vdcm_vqat_bar_cleanup(struct adf_vqat_bar *vbar);
int adf_vdcm_vqat_bar_add_sub_mmap_area(struct adf_vqat_bar *vbar,
					phys_addr_t pb, void *vb, u32 bar_ofs,
					u32 size, bool is_io);
void adf_vdcm_vqat_msgq_reset(struct adf_vdcm_vqat_msgq *msgq);
int adf_vdcm_vqat_msgq_init(struct adf_vdcm_vqat_msgq *msgq);
void adf_vdcm_vqat_msgq_cleanup(struct adf_vdcm_vqat_msgq *msgq);
struct adf_vdcm_ctx_blk *
adf_vdcm_register_vqat_parent(struct adf_accel_dev *accel_dev,
			      int vqat_types_num,
			      enum vqat_type types[]);
void adf_vdcm_unregister_vqat_parent(struct adf_vdcm_ctx_blk *vdcm,
				     struct adf_accel_dev *accel_dev);
int adf_vdcm_register_vqat_class(struct adf_vqat_class *dclass);
void adf_vdcm_unregister_vqat_class(struct adf_vqat_class *dclass);
phys_addr_t adf_vdcm_vqat_lookup_mmap_space(struct adf_vqat_bar *vbar,
					    u64 bar_ofs, u64 size,
					    bool *is_io);
struct dentry *adf_vdcm_get_debugfs(struct adf_vdcm_ctx_blk *vdcm);
struct adf_vdcm_obj_mgr *
adf_vdcm_obj_mgr_new(int (*cb_at_first)(void *obj, void *data, s64 *res),
		     int (*cb_at_last)(void *obj, void *data, s64 res),
		     void *cb_data);
void adf_vdcm_obj_mgr_destroy(struct adf_vdcm_obj_mgr *obj_mgr);
int adf_vdcm_obj_mgr_ref_obj(struct adf_vdcm_obj_mgr *obj_mgr,
			     void *obj, s64 *p_res);
int adf_vdcm_obj_mgr_unref_obj(struct adf_vdcm_obj_mgr *obj_mgr,
			       void *obj, s64 *p_res);
int adf_vdcm_obj_mgr_is_empty(struct adf_vdcm_obj_mgr *obj_mgr);
int adf_vdcm_init_vqat_vf(void);
void adf_vdcm_cleanup_vqat_vf(void);
int adf_vdcm_init_vqat_adi(void);
void adf_vdcm_cleanup_vqat_adi(void);
void adf_vdcm_set_vqat_msix_vector(struct adf_vdcm_vqat *vqat,
				   enum adf_vqat_irq irq,
				   enum adf_vqat_irq_op irq_op);
void adf_vdcm_notify_vqat(struct adf_vdcm_vqat *vqat,
			  enum adf_vqat_irq irq);
void adf_vdcm_notify_vqat_iov(struct adf_vdcm_vqat *vqat, u32 queue);
int adf_vdcm_add_vqat_dbg(struct adf_accel_dev *accel_dev,
			  struct adf_vdcm_vqat *vqat);
void adf_vdcm_del_vqat_dbg(struct adf_vdcm_vqat *vqat);
struct adf_vcfg_container *adf_vdcm_vcfg_init(u16 dev_id, u16 subsystem_id,
					      bool is_vf);
void adf_vdcm_vcfg_destroy(struct adf_vcfg_container **vcfg);
int adf_vdcm_vcfg_rw(struct adf_vdcm_vqat *vqat,
		     u32 offset,
		     void *data,
		     u32 size,
		     bool is_write);
struct adf_vcfg_container *adf_vdcm_vcfg_create(u32 num_caps);
int adf_vdcm_vcfg_add_capability(struct adf_vcfg_container *vcfg,
				 u32 index,
				 u32 size,
				 u16 cap_id,
				 bool ext_cap,
				 void *reg_map);
int adf_vdcm_vcfg_populate(struct adf_vcfg_container *vcfg);
int adf_vdcm_register_vreg_handle(struct adf_vcfg_container *vcfg,
				  u16 cap_id,
				  u32 offset,
				  int (*access)(struct adf_vdcm_vqat*,
						bool,
						unsigned int,
						void *,
						unsigned int,
						void *));
int adf_vdcm_set_vreg_attr(struct adf_vcfg_container *vcfg,
			   u16 cap_id, u32 offset,
			   struct adf_vcfg_attr_desc *attr);
struct adf_accel_compat_manager *
adf_vdcm_get_cm(struct adf_vdcm_ctx_blk *vdcm);
int adf_vdcm_init_compat_manager(struct adf_accel_dev *accel_dev,
				 struct adf_accel_compat_manager **cm);
void adf_vdcm_cleanup_compat_manager(struct adf_accel_dev *accel_dev,
				     struct adf_accel_compat_manager **cm);
int adf_vqat_prepare_caps(struct adf_vqat_enabled_caps *enabled_caps,
			  struct adf_accel_dev *parent, enum vqat_type type);
int adf_vqat_populate_caps(struct adf_vdcm_vqat_cap *vcap,
			   struct adf_accel_dev *parent,
			   struct adf_vqat_enabled_caps *enabled_caps,
			   enum vqat_type type);
#else
static inline int adf_vdcm_init(void)
{
	return -EINVAL;
}

static inline void adf_vdcm_cleanup(void)
{
}

static inline struct adf_vdcm_ctx_blk *
adf_vdcm_register_vqat_parent(struct adf_accel_dev *accel_dev,
			      int vqat_types_num,
			      enum vqat_type types[])
{
	return NULL;
}

static inline
void adf_vdcm_unregister_vqat_parent(struct adf_vdcm_ctx_blk *vdcm,
				     struct adf_accel_dev *adev)
{
}

#endif /* CONFIG_CRYPTO_DEV_QAT_VDCM */
#endif /*ADF_VDCM_H*/
