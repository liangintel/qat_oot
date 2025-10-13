// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2019 - 2021 Intel Corporation */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/vfio.h>
#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "adf_vdcm.h"
#include "icp_qat_hw.h"

#define VQAT_NUM_CAPS 3

static u64 vqat_hdr_defconfig[] = {
	0x001000000da58086ULL, /* 0x00 */
	0x008000000b400002ULL, /* 0x08 */
	0x000000000000000cULL, /* 0x10, Prefetchable 64-bit mmio bar 0 */
	0x000000000000000cULL, /* 0x18, Prefetchable 64-bit mmio bar 2 */
	0x000000000000000cULL, /* 0x20, Prefetchable 64-bit mmio bar 4 */
	0x0000808600000000ULL, /* 0x28, Subsystem Vendor ID is 0x8086 */
	0x0000005000000000ULL, /* 0x30, The first capability starts at 0x50 */
	0x0000000000000000ULL, /* 0x38 */
	0x0000000000000000ULL, /* 0x40 */
	0x0000000000000000ULL, /* 0x48 */
};

static u64 vqat_pcie_defconfig[] = {
	0x1000806100020010ULL, /* 0x00: PCIe capability */
	0x0000001100000000ULL, /* 0x08 */
	0x0000000000000000ULL, /* 0x10 */
	0x0000000000000000ULL, /* 0x18 */
	0x0000001200000000ULL, /* 0x20 */
	0x0000000000000000ULL, /* 0x28 */
	0x0000000000000000ULL, /* 0x30 */
	0x0000000000000000ULL, /* 0x38 */
};

static u64 vqat_msi_defconfig[] = {
	0x0000000000000005ULL, /* 0x00: MSI capability */
	0x0000000000000000ULL, /* 0x08 */
	0x0000000000000000ULL, /* 0x10 */
	0x0000000000000000ULL, /* 0x18 */
};

#define ADF_VQAT_MSIX_TABLE_INFO \
	((ADF_VQAT_MSIX_TABLE_OFS & PCI_MSIX_TABLE_OFFSET) + \
	((ADF_VQAT_PMISC_BAR << 1) & PCI_MSIX_TABLE_BIR))
#define ADF_VQAT_MSIX_PBA_INFO \
	((ADF_VQAT_MSIX_PBA_OFS & PCI_MSIX_PBA_OFFSET) + \
	(((ADF_VQAT_PMISC_BAR << 1)) & PCI_MSIX_PBA_BIR))

static u64 vqat_msix_defconfig[] = {
	/* 0x00: MSI-X capability */
	0x0000000000010011ULL + ((u64)ADF_VQAT_MSIX_TABLE_INFO << 32),
	/* 0x08 */
	0x0000000000000000ULL + ADF_VQAT_MSIX_PBA_INFO,
};

static int
vqat_do_config_rw(struct adf_vdcm_vqat *vqat,
		  struct adf_vcfg_container *vcfg,
		  struct adf_vqat_pci_cap_meta *cap,
		  u32 offset,
		  u8 *data,
		  u32 len,
		  bool is_write)
{
	u32 abs_offset;
	u32 processed = 0;
	u32 pos;
	u32 delta;
	u32 orig_val = 0;
	u32 ro_val;
	u32 woc_val;
	u32 *reg_addr;
	int ret = 0;
	struct adf_vcfg_vreg_meta *vreg;

	if (!cap  || !cap->vreg_arr)
		return -EINVAL;

	vreg = &cap->vreg_arr[offset >> 2];
	abs_offset = cap->offset + offset;

	if (abs_offset + len > vcfg->size)
		return -EINVAL;

	pr_debug("%s: %s access to offset 0x%x(0x%x) of CAP_ID(%d)\n",
		 __func__,
		 is_write ? "write" : "read",
		 offset,
		 abs_offset,
		 cap->cap_id);

	while (processed < len) {
		pos = abs_offset + processed;

		/* split memory access to 4 bytes aligned */
		delta = min(len - processed,
			    4 - (pos % 4));
		reg_addr = (u32 *)(vcfg->regmap + (pos & ~0x3U));
		orig_val = *reg_addr;

		pr_debug("%s: pos=%d, delta=%d\n",
			 __func__,
			 pos,
			 delta);

		if (is_write) {
			memcpy(vcfg->regmap + pos, data + processed, delta);

			if (vreg->attr_desc && vreg->attr_desc->ro_mask) {
				/* Handle read-only bits */
				ro_val = orig_val & vreg->attr_desc->ro_mask;
				*reg_addr = ro_val |
					    (*reg_addr &
					     ~vreg->attr_desc->ro_mask);
			}

			if (vreg->access_hdl) {
				pr_debug("%s: vcfg handler triggered @0x%x\n",
					 __func__, pos);

				/* pass register address as 4 bytes aligned */
				ret += vreg->access_hdl(vqat,
							is_write,
							offset + processed,
							data + processed,
							delta,
							(void *)reg_addr);
			} else {
				ret += delta;
			}

			if (vreg->attr_desc && vreg->attr_desc->woc_mask) {
				/* Handle write-1-to-clear bits */
				woc_val = orig_val & ~(*reg_addr) &
					  vreg->attr_desc->woc_mask;
				*reg_addr = woc_val |
					    (*reg_addr &
					    ~vreg->attr_desc->woc_mask);
			}
		} else {
			memcpy(data + processed, vcfg->regmap + pos, delta);
			ret += delta;
		}

		processed += delta;
	}

	return ret;
}

int adf_vdcm_vcfg_rw(struct adf_vdcm_vqat *vqat,
		     u32 offset,
		     void *data,
		     u32 size,
		     bool is_write)
{
	int i;
	struct adf_vqat_pci_cap_meta *cap;
	struct adf_vcfg_container *vcfg;

	if (!vqat || !vqat->vcfg || !vqat->vcfg->caps ||
	    !vqat->vcfg->last_pci_cap || !size)
		return -EINVAL;

	vcfg = vqat->vcfg;

	/* find capability */
	for (i = 0; i < vcfg->num_caps; i++) {
		cap = &vcfg->caps[i];
		if (offset >= cap->offset &&
		    offset < (cap->offset + cap->size)) {
			return vqat_do_config_rw(vqat,
						 vcfg,
						 cap,
						 offset - cap->offset,
						 data,
						 size,
						 is_write);
		}
	}

	if (offset >= vcfg->last_pci_cap->offset + vcfg->last_pci_cap->size &&
	    offset < PCI_CFG_SPACE_SIZE) {
		pr_debug("%s: %s access to offset 0x%x is between last pci capability and first pcie extended capability\n",
			 __func__,
			is_write ? "write" : "read",
			offset);
		if (!is_write)
			memset(data, 0, size);
		return size;
	}

	pr_err("%s: vqat config @offset 0x%x does not exist\n",
	       __func__, offset);

	return -ENOENT;
}

struct adf_vcfg_container *adf_vdcm_vcfg_create(u32 num_caps)
{
	struct adf_vcfg_container *vcfg;

	if (!num_caps)
		return NULL;

	vcfg = kzalloc(sizeof(*vcfg), GFP_KERNEL);

	if (unlikely(!vcfg))
		return NULL;

	vcfg->caps = kcalloc(num_caps, sizeof(*vcfg->caps), GFP_KERNEL);

	if (unlikely(!vcfg->caps)) {
		kfree(vcfg);
		return NULL;
	}

	vcfg->num_caps = num_caps;
	return vcfg;
}

int adf_vdcm_vcfg_add_capability(struct adf_vcfg_container *vcfg,
				 u32 index,
				 u32 size,
				 u16 cap_id,
				 bool ext_cap,
				 void *reg_map)
{
	struct adf_vqat_pci_cap_meta *cap;
	u8 *reg = reg_map;

	if (!vcfg || !reg_map || !size)
		return -EINVAL;

	if (index >= vcfg->num_caps)
		return -EFAULT;

	cap = &vcfg->caps[index];

	/* Check if capability already exists */
	if (cap->size)
		return -EEXIST;

	/* Check if CAP_ID matches default config */
	if (cap_id != PCI_CAP_ID_BASIC) {
		if (!ext_cap && *reg != (u8)cap_id) {
			pr_err("%s: incorrect capability(%d), expect %d\n",
			       __func__,
			       reg[0],
			       cap_id);

			return -EFAULT;
		}

		if (ext_cap && *(u16 *)reg != cap_id) {
			pr_err("%s: incorrect capability(%d), expect %d\n",
			       __func__,
			       reg[0],
			       cap_id);

			return -EFAULT;
		}
	}

	/* Every register in CFG has a dedicated register handle */
	cap->vreg_arr = kcalloc(DIV_ROUND_UP(size, 4),
				sizeof(*cap->vreg_arr),
				GFP_KERNEL);

	if (unlikely(!cap->vreg_arr))
		return -ENOMEM;

	cap->num_regs = size / 4;
	cap->size = size;
	cap->default_regmap = reg_map;
	cap->cap_id = cap_id;
	cap->is_ext_cap = ext_cap;
	if (!cap->is_ext_cap) {
		vcfg->size += size;
		vcfg->last_pci_cap = cap;
	} else {
		vcfg->ext_size += size;
		vcfg->size = PCI_CFG_SPACE_SIZE + vcfg->ext_size;
	}

	return 0;
}

int adf_vdcm_set_vreg_attr(struct adf_vcfg_container *vcfg,
			   u16 cap_id, u32 offset,
			   struct adf_vcfg_attr_desc *attr)
{
	struct adf_vqat_pci_cap_meta *cap = NULL;
	struct adf_vcfg_vreg_meta *vreg = NULL;
	struct adf_vcfg_attr_desc *attr_desc_meta = NULL;
	int i;

	if (!vcfg || !vcfg->num_caps  || !attr)
		return -EINVAL;

	for (i = 0; i < vcfg->num_caps; i++) {
		if (vcfg->caps[i].cap_id == cap_id) {
			cap = &vcfg->caps[i];
			break;
		}
	}

	if (!cap)
		return -EINVAL;

	/* Every 4 bytes in cfg space share a attribute descriptor */
	vreg = &cap->vreg_arr[offset >> 2];

	if (vreg->attr_desc)
		return -EINVAL;

	attr_desc_meta = kzalloc(sizeof(*attr_desc_meta), GFP_KERNEL);

	if (unlikely(!attr_desc_meta))
		return -ENOMEM;

	memcpy(attr_desc_meta, attr, sizeof(*attr_desc_meta));

	vreg->attr_desc = attr_desc_meta;

	return 0;
}

/*
 * Special handling of registers, like RC WC,
 * read only check are implemented through
 * register handlers.
 */
int
adf_vdcm_register_vreg_handle(struct adf_vcfg_container *vcfg,
			      u16 cap_id,
			      u32 offset,
			      int (*access)(struct adf_vdcm_vqat*,
					    bool, u32, void *, u32, void *))
{
	struct adf_vqat_pci_cap_meta *cap = NULL;
	struct adf_vcfg_vreg_meta *vreg = NULL;
	int i;

	if (!vcfg || !vcfg->num_caps || !access)
		return -EINVAL;

	for (i = 0; i < vcfg->num_caps; i++) {
		if (vcfg->caps[i].cap_id == cap_id) {
			cap = &vcfg->caps[i];
			break;
		}
	}

	if (!cap)
		return -EINVAL;

	/* Every 4 bytes in cfg space share a handler */
	vreg = &cap->vreg_arr[offset >> 2];
	vreg->access_hdl = access;

	return 0;
}

int adf_vdcm_vcfg_populate(struct adf_vcfg_container *vcfg)
{
	struct adf_vqat_pci_cap_meta *cap;
	int i;
	u32 offset = 0;
	u8 *data;
	u16 next_cap_ext;

	if (!vcfg || !vcfg->caps || !vcfg->last_pci_cap ||
	    !vcfg->num_caps || !vcfg->size)
		return -EINVAL;

	if (!vcfg->regmap) {
		vcfg->regmap = kmalloc(vcfg->size, GFP_KERNEL);
		if (unlikely(!vcfg->regmap))
			return -ENOMEM;
	}

	data = (u8 *)vcfg->regmap;

	for (i = 0; i < vcfg->num_caps; i++) {
		cap = &vcfg->caps[i];
		if (!cap->size) {
			kfree(vcfg->regmap);
			vcfg->regmap = NULL;
			pr_err("Size of cap(%d) is zero\n", cap->cap_id);
			return -EFAULT;
		}

		memcpy(data, cap->default_regmap, cap->size);
		cap->offset = offset;

		if (cap == vcfg->last_pci_cap) {
			/*
			 * If it is the last PCI cap, set the next cap offset
			 * to 0, because initial offset of PCIe extended cap
			 * is 0x100.
			 */
			data[1] = 0;
			offset = PCI_CFG_SPACE_SIZE;
			data = (u8 *)vcfg->regmap + PCI_CFG_SPACE_SIZE;
			continue;
		}

		if (i != 0 && i != (vcfg->num_caps - 1)) {
			if (cap->is_ext_cap) {
				next_cap_ext = *((u16 *)data + 1);
				next_cap_ext |=
					(((offset + cap->size) << 4) & 0xffff);

				*((u16 *)data + 1) = next_cap_ext;
			} else {
				*(data + 1) = offset + cap->size;
			}
		}

		data += cap->size;
		offset += cap->size;
	}

	return 0;
}

void adf_vdcm_vcfg_destroy(struct adf_vcfg_container **pvcfg)
{
	struct adf_vcfg_container *vcfg = *pvcfg;
	struct adf_vqat_pci_cap_meta *cap;
	struct adf_vcfg_vreg_meta *vreg;
	int i;
	int j;

	if (!vcfg)
		return;

	if (!vcfg->caps)
		goto clean_up_vcfg;

	for (i = 0; i < vcfg->num_caps; i++) {
		cap = &vcfg->caps[i];
		if (cap->vreg_arr) {
			for (j = 0; j < cap->num_regs; j++) {
				vreg = &cap->vreg_arr[j];
				kfree(vreg->attr_desc);
			}

			kfree(cap->vreg_arr);
		}
	}

	kfree(vcfg->caps);

clean_up_vcfg:
	kfree(vcfg->regmap);
	kfree(vcfg);
	*pvcfg = NULL;
}

int adf_vdcm_vqat_reset_config(struct adf_vdcm_vqat *vqat)
{
	struct adf_vcfg_container *vcfg;

	if (!vqat || !vqat->vcfg)
		return -EINVAL;

	vcfg = vqat->vcfg;
	if (adf_vdcm_vcfg_populate(vcfg))
		return -EFAULT;

	*(u16 *)(vcfg->regmap + PCI_SUBSYSTEM_ID) = vcfg->subsystem_id;

	/* TODO: We should keep sticky bits and RO bits */
	return 0;
}

struct adf_vcfg_container *adf_vdcm_vcfg_init(u16 dev_id, u16 subsystem_id,
					      bool is_vf)
{
	struct adf_vcfg_container *vcfg;
	u32 index = 0;

	vcfg = adf_vdcm_vcfg_create(VQAT_NUM_CAPS);

	if (!vcfg)
		return NULL;

	if (adf_vdcm_vcfg_add_capability(vcfg,
					 index++,
					 sizeof(vqat_hdr_defconfig),
					 PCI_CAP_ID_BASIC,
					 false,
					 vqat_hdr_defconfig))
		goto err_add_capability;

	if (adf_vdcm_vcfg_add_capability(vcfg,
					 index++,
					 sizeof(vqat_pcie_defconfig),
					 PCI_CAP_ID_EXP,
					 false,
					 vqat_pcie_defconfig))
		goto err_add_capability;

	if (is_vf) {
		if (adf_vdcm_vcfg_add_capability(vcfg,
						 index++,
						 sizeof(vqat_msi_defconfig),
						 PCI_CAP_ID_MSI,
						 false,
						 vqat_msi_defconfig))
			goto err_add_capability;
	} else {
		if (adf_vdcm_vcfg_add_capability(vcfg,
						 index++,
						 sizeof(vqat_msix_defconfig),
						 PCI_CAP_ID_MSIX,
						 false,
						 vqat_msix_defconfig))
			goto err_add_capability;
	}

	if (adf_vdcm_vcfg_populate(vcfg))
		goto err_add_capability;

	*(u16 *)(vcfg->regmap + PCI_DEVICE_ID) = dev_id;
	*(u16 *)(vcfg->regmap + PCI_SUBSYSTEM_ID) = subsystem_id;
	vcfg->dev_id = dev_id;
	vcfg->subsystem_id = subsystem_id;

	return vcfg;

err_add_capability:
	adf_vdcm_vcfg_destroy(&vcfg);
	return vcfg;
}

int adf_vdcm_vqat_bar_init(struct adf_vqat_bar *vbar, char *name,
			   phys_addr_t phys_addr,
			   void *virt_addr, size_t size, bool readable,
			   bool writable, u8 type, u16 total_sub_mmaps)
{
	memset(vbar, 0, sizeof(struct adf_vqat_bar));
	/* vbar->name is already cleared above, no need to clear it again */
	strncpy(vbar->name, name, ADF_VQAT_BAR_NAME_SIZE - 1);
	if (readable)
		vbar->attr |= VFIO_REGION_INFO_FLAG_READ;
	if (writable)
		vbar->attr |= VFIO_REGION_INFO_FLAG_WRITE;
	if (phys_addr) {
		if (!total_sub_mmaps && type == ADF_VQAT_BAR_MIX) {
			pr_err("%s: Invalid bar configuration\n", __func__);
			return -EINVAL;
		}
		vbar->attr |= VFIO_REGION_INFO_FLAG_MMAP;
	}
	vbar->base_addr = phys_addr;
	vbar->virt_addr = virt_addr;
	vbar->size = size;
	vbar->type = type;
	vbar->total_sub_mmap_areas = total_sub_mmaps;
	if (total_sub_mmaps) {
		vbar->sub_mmap_areas =
			kcalloc(total_sub_mmaps,
				sizeof(struct adf_vqat_sub_mmap_area),
				GFP_KERNEL);
		if (!vbar->sub_mmap_areas)
			return -ENOMEM;
	}

	return 0;
}

void adf_vdcm_vqat_bar_cleanup(struct adf_vqat_bar *vbar)
{
	kfree(vbar->sub_mmap_areas);
	memset(vbar, 0, sizeof(struct adf_vqat_bar));
}

static struct adf_vqat_sub_mmap_area *
adf_vdcm_vqat_bar_lookup_sub_mmap_area(struct adf_vqat_bar *vbar,
				       u64 bar_ofs, u64 size)
{
	int i;
	struct adf_vqat_sub_mmap_area *area;

	for (i = 0; i < vbar->num_sub_mmap_area; i++) {
		area = &vbar->sub_mmap_areas[i];
		if (bar_ofs >= area->bar_ofs &&
		    (bar_ofs + size <= (area->bar_ofs + area->size)))
			return area;
	}

	return NULL;
}

int adf_vdcm_vqat_bar_add_sub_mmap_area(struct adf_vqat_bar *vbar,
					phys_addr_t pbase, void *vbase,
					u32 bar_ofs, u32 size, bool is_io)
{
	int i = vbar->num_sub_mmap_area;

	if (i == vbar->total_sub_mmap_areas) {
		pr_err("No enough entries for sub areas\n");
		return -ENOMEM;
	}
	if (!PAGE_ALIGNED(bar_ofs) || !PAGE_ALIGNED(size)) {
		pr_info("Invalid offset 0x%x or size 0x%x in bar %s\n",
			bar_ofs, size, vbar->name);
		return -EINVAL;
	}
	if (adf_vdcm_vqat_bar_lookup_sub_mmap_area(vbar, bar_ofs, size)) {
		pr_info("Overlapped sub area (ofs:%u, size %u) in bar %s\n",
			bar_ofs, size, vbar->name);
		return -EINVAL;
	}
	vbar->attr |= (VFIO_REGION_INFO_FLAG_MMAP |
			VFIO_REGION_INFO_FLAG_CAPS);
	vbar->sub_mmap_areas[i].pbase = pbase;
	vbar->sub_mmap_areas[i].size = size;
	vbar->sub_mmap_areas[i].bar_ofs = bar_ofs;
	vbar->sub_mmap_areas[i].is_io = is_io;
	vbar->num_sub_mmap_area++;

	return 0;
}

phys_addr_t adf_vdcm_vqat_lookup_mmap_space(struct adf_vqat_bar *vbar,
					    u64 bar_ofs, u64 size,
					    bool *is_io)
{
	if (vbar->num_sub_mmap_area > 0) {
		struct adf_vqat_sub_mmap_area *area;

		area = adf_vdcm_vqat_bar_lookup_sub_mmap_area(vbar,
							      bar_ofs, size);
		if (!area) {
			pr_err("Unable to find mapped phys in bar %s\n",
			       vbar->name);
			return 0;
		}
		*is_io = area->is_io;
		return (area->pbase + bar_ofs - area->bar_ofs);
	}
	if (vbar->type == ADF_VQAT_BAR_MMIO)
		*is_io = true;
	else
		*is_io = false;
	return (vbar->base_addr + bar_ofs);
}

void adf_vdcm_vqat_msgq_reset(struct adf_vdcm_vqat_msgq *msgq)
{
	memset(msgq->vbase, 0, PAGE_SIZE);
	msgq->tx_notifier = 0;
	msgq->rx_notifier = 0;
}

int adf_vdcm_vqat_msgq_init(struct adf_vdcm_vqat_msgq *msgq)
{
	struct page *msgq_page;

	memset(msgq, 0, sizeof(struct adf_vdcm_vqat_msgq));
	msgq_page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!msgq_page) {
		pr_err("Failed to get free page for message queue\n");
		return -ENOMEM;
	}
	SetPageReserved(msgq_page);
	msgq->pbase = page_to_phys(msgq_page);
	msgq->vbase = page_address(msgq_page);
	msgq->tx_notifier = 0;
	msgq->rx_notifier = 0;

	return 0;
}

void adf_vdcm_vqat_msgq_cleanup(struct adf_vdcm_vqat_msgq *msgq)
{
	if (msgq->vbase) {
		ClearPageReserved(virt_to_page((void *)msgq->vbase));
		free_page((unsigned long)msgq->vbase);
		msgq->vbase = 0;
	}
}

u16 adf_vqat_populate_cap(struct adf_vqat_cap *cap,
			  struct adf_vqat_enabled_cap *enabled_cap,
			  struct adf_accel_dev *parent,
			  enum vqat_type type)
{
	cap->id = enabled_cap->id;
	enabled_cap->get_value(parent, type, (u64 *)&cap->data);
	cap->len = adf_vqat_cap_header_size() + enabled_cap->len;

	return cap->len;
}

struct adf_vdcm_obj_mgr_item {
	struct list_head list;
	/* The object which is referenced by client */
	void *obj;
	/* To store the result of the cb_at_first */
	s64 res;
	refcount_t ref_count;
};

struct adf_vdcm_obj_mgr {
	struct list_head header;
	/* The lock for objects list */
	struct mutex objs_lock;
	/* callback when the object has the first reference */
	int (*cb_at_first)(void *obj, void *data, s64 *p_res);
	/* callback when the object doesn't have reference any more */
	int (*cb_at_last)(void *obj, void *data, s64 res);
	void *cb_data;
};

struct adf_vdcm_obj_mgr *
adf_vdcm_obj_mgr_new(int (*cb_at_first)(void *obj, void *data, s64 *p_res),
		     int (*cb_at_last)(void *obj, void *data, s64 res),
		     void *cb_data)
{
	struct adf_vdcm_obj_mgr *obj_mgr;

	obj_mgr = kzalloc(sizeof(*obj_mgr), GFP_KERNEL);
	if (!obj_mgr)
		return NULL;
	INIT_LIST_HEAD(&obj_mgr->header);
	mutex_init(&obj_mgr->objs_lock);

	obj_mgr->cb_at_first = cb_at_first;
	obj_mgr->cb_at_last = cb_at_last;
	obj_mgr->cb_data = cb_data;

	return obj_mgr;
}

void adf_vdcm_obj_mgr_destroy(struct adf_vdcm_obj_mgr *obj_mgr)
{
	struct adf_vdcm_obj_mgr_item *p, *q;

	mutex_lock(&obj_mgr->objs_lock);
	list_for_each_entry_safe(p, q, &obj_mgr->header, list) {
		if (refcount_read(&p->ref_count))
			pr_warn("adf_vdcm_obj still has reference!\n");
		kfree(p);
	}
	mutex_unlock(&obj_mgr->objs_lock);
	mutex_destroy(&obj_mgr->objs_lock);
	kfree(obj_mgr);
}

static struct adf_vdcm_obj_mgr_item *
adf_vdcm_obj_mgr_lookup_obj(struct adf_vdcm_obj_mgr *obj_mgr, void *obj)
{
	struct adf_vdcm_obj_mgr_item *p, *r = NULL;

	list_for_each_entry(p, &obj_mgr->header, list) {
		if (p->obj == obj) {
			r = p;
			break;
		}
	}

	return r;
}

int adf_vdcm_obj_mgr_ref_obj(struct adf_vdcm_obj_mgr *obj_mgr,
			     void *obj, s64 *p_res)
{
	struct adf_vdcm_obj_mgr_item *p;
	int ret;

	mutex_lock(&obj_mgr->objs_lock);
	p = adf_vdcm_obj_mgr_lookup_obj(obj_mgr, obj);
	if (p) {
		refcount_inc(&p->ref_count);
		goto out;
	}
	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p) {
		mutex_unlock(&obj_mgr->objs_lock);
		return -ENOMEM;
	}
	if (obj_mgr->cb_at_first) {
		ret = obj_mgr->cb_at_first(obj, obj_mgr->cb_data, &p->res);
		if (ret < 0) {
			kfree(p);
			mutex_unlock(&obj_mgr->objs_lock);
			return ret;
		}
	} else {
		p->res = 0;
	}
	p->obj = obj;
	list_add(&p->list, &obj_mgr->header);
	refcount_set(&p->ref_count, 1);
out:
	mutex_unlock(&obj_mgr->objs_lock);
	if (p_res)
		*p_res = p->res;
	return refcount_read(&p->ref_count);
}

int adf_vdcm_obj_mgr_unref_obj(struct adf_vdcm_obj_mgr *obj_mgr,
			       void *obj, s64 *p_res)
{
	struct adf_vdcm_obj_mgr_item *p;
	s64 res = 0;
	int refcount;

	mutex_lock(&obj_mgr->objs_lock);
	p = adf_vdcm_obj_mgr_lookup_obj(obj_mgr, obj);
	if (!p) {
		mutex_unlock(&obj_mgr->objs_lock);
		return -EINVAL;
	}

	if (!refcount_dec_and_test(&p->ref_count)) {
		refcount = refcount_read(&p->ref_count);
		goto out;
	}
	if (obj_mgr->cb_at_last)
		res = obj_mgr->cb_at_last(obj, obj_mgr->cb_data, p->res);
	list_del(&p->list);
	refcount = 0;
	kfree(p);
out:
	mutex_unlock(&obj_mgr->objs_lock);
	if (p_res)
		*p_res = res;
	return refcount;
}

int adf_vdcm_obj_mgr_is_empty(struct adf_vdcm_obj_mgr *obj_mgr)
{
	int empty;

	mutex_lock(&obj_mgr->objs_lock);
	empty = list_empty(&obj_mgr->header);
	mutex_unlock(&obj_mgr->objs_lock);

	return empty;
}

static inline int adf_vqat_cap_svc_map(struct adf_accel_dev *parent,
				       enum vqat_type type, u64 *map)
{
	switch (type) {
	case QAT_VQAT_ADI_RP_SYM:
		*map = SYM << ADF_CFG_SERV_RING_PAIR_0_SHIFT;
		break;
	case QAT_VQAT_ADI_RP_ASYM:
		*map = ASYM << ADF_CFG_SERV_RING_PAIR_0_SHIFT;
		break;
	case QAT_VQAT_ADI_RP_DC:
		*map = COMP << ADF_CFG_SERV_RING_PAIR_0_SHIFT;
		break;
	default:
		pr_err("Unknown device type %d\n", type);
		return -EFAULT;
	}

	return 0;
}

static u32 adf_vqat_cap_svc_mask_sym(struct adf_accel_dev *parent)
{
	u32 p_mask;

	p_mask = ICP_ACCEL_CAPABILITIES_CRYPTO_SYMMETRIC |
		 ICP_ACCEL_CAPABILITIES_CIPHER |
		 ICP_ACCEL_CAPABILITIES_AUTHENTICATION |
		 ICP_ACCEL_CAPABILITIES_ZUC |
		 ICP_ACCEL_CAPABILITIES_SHA3 |
		 ICP_ACCEL_CAPABILITIES_SHA3_EXT |
		 ICP_ACCEL_CAPABILITIES_AESGCM_SPC |
		 ICP_ACCEL_CAPABILITIES_CHACHA_POLY |
		 ICP_ACCEL_CAPABILITIES_SM3 |
		 ICP_ACCEL_CAPABILITIES_SM4 |
		 ICP_ACCEL_CAPABILITIES_AES_V2;

	return parent->hw_device->accel_capabilities_mask & p_mask;
}

static u32 adf_vqat_cap_svc_mask_asym(struct adf_accel_dev *parent)
{
	u32 p_mask;

	p_mask = ICP_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC |
		 ICP_ACCEL_CAPABILITIES_SM2;

	return parent->hw_device->accel_capabilities_mask & p_mask;
}

static u32 adf_vqat_cap_svc_mask_dc(struct adf_accel_dev *parent)
{
	u32 p_mask;

	p_mask = ICP_ACCEL_CAPABILITIES_COMPRESSION |
		 ICP_ACCEL_CAPABILITIES_LZ4_COMPRESSION |
		 ICP_ACCEL_CAPABILITIES_LZ4S_COMPRESSION |
		 ICP_ACCEL_CAPABILITIES_CNV_INTEGRITY |
		 ICP_ACCEL_CAPABILITIES_CNV_INTEGRITY64;

	return parent->hw_device->accel_capabilities_mask & p_mask;
}

static u32 adf_vqat_cap_svc_mask_common(struct adf_accel_dev *parent)
{
	u32 p_mask;

	p_mask = ICP_ACCEL_CAPABILITIES_INLINE |
		 ICP_ACCEL_CAPABILITIES_KPT |
		 ICP_ACCEL_CAPABILITIES_KPT2;

	return parent->hw_device->accel_capabilities_mask & p_mask;
}

static int adf_vqat_cap_svc_mask(struct adf_accel_dev *parent,
				 enum vqat_type type,
				 u64 *mask)
{
	u32 mask32 = 0;

	switch (type) {
	case QAT_VQAT_ADI_RP_SYM:
		mask32 |= adf_vqat_cap_svc_mask_sym(parent);
		break;
	case QAT_VQAT_ADI_RP_ASYM:
		mask32 |= adf_vqat_cap_svc_mask_asym(parent);
		break;
	case QAT_VQAT_ADI_RP_DC:
		mask32 |= adf_vqat_cap_svc_mask_dc(parent);
		if (parent->chaining_enabled)
			mask32 |= adf_vqat_cap_svc_mask_sym(parent);
		break;
	default:
		pr_err("Unknown device type %d\n", type);
		return -EFAULT;
	}

	mask32 |= adf_vqat_cap_svc_mask_common(parent);
	*mask = mask32;

	return 0;
}

static int adf_vqat_cap_dev_freq(struct adf_accel_dev *parent,
				 enum vqat_type type,
				 u64 *value)
{
	*value = parent->hw_device->clock_frequency;
	return 0;
}

static int adf_vqat_cap_svc_dc_ext(struct adf_accel_dev *parent,
				   enum vqat_type type,
				   u64 *mask)
{
	*mask = parent->hw_device->extended_dc_capabilities;
	return 0;
}

#ifdef NON_GPL_COMMON
static int adf_vqat_cap_svc_sym_hash(struct adf_accel_dev *parent,
				     enum vqat_type type,
				     u64 *mask)
{
	*mask = parent->hw_device->hash_capabilities_mask;
	return 0;
}

static int adf_vqat_cap_svc_sym_cipher(struct adf_accel_dev *parent,
				       enum vqat_type type,
				       u64 *mask)
{
	*mask = parent->hw_device->cipher_capabilities_mask;
	return 0;
}

static int adf_vqat_cap_svc_asym(struct adf_accel_dev *parent,
				 enum vqat_type type,
				 u64 *mask)
{
	*mask = parent->hw_device->asym_capabilities_mask;
	return 0;
}
#endif

static inline
int adf_vqat_cap_svc_kpt_cert_len(struct adf_accel_dev *parent,
				  enum vqat_type type)
{
	return parent->hw_device->kpt_issue_cert_len;
}

static int adf_vqat_cap_svc_kpt_cert(struct adf_accel_dev *parent,
				     enum vqat_type type,
				     u64 *data)
{
	memcpy(data, parent->hw_device->kpt_issue_cert,
	       adf_vqat_cap_svc_kpt_cert_len(parent, type));
	return 0;
}

int adf_vqat_prepare_caps(struct adf_vqat_enabled_caps *enabled_caps,
			  struct adf_accel_dev *parent, enum vqat_type type)
{
	int tmp, size;

	tmp = adf_vqat_enabled_caps_add(enabled_caps,
					ADF_VQAT_CAP_DEV_FREQ_ID,
					ADF_VQAT_CAP_ATTR_INT,
					0,
					adf_vqat_cap_dev_freq);
	if (tmp < 0)
		return -EFAULT;
	size = tmp;

	tmp = adf_vqat_enabled_caps_add(enabled_caps,
					ADF_VQAT_CAP_SVC_MAP_ID,
					ADF_VQAT_CAP_ATTR_INT,
					0,
					adf_vqat_cap_svc_map);
	if (tmp	< 0)
		return -EFAULT;
	size += tmp;

	tmp = adf_vqat_enabled_caps_add(enabled_caps,
					ADF_VQAT_CAP_SVC_MASK_ID,
					ADF_VQAT_CAP_ATTR_INT,
					0,
					adf_vqat_cap_svc_mask);
	if (tmp	< 0)
		return -EFAULT;
	size += tmp;

	/* For now, only DC vQAT has more capabilities defined */
	if (type == QAT_VQAT_ADI_RP_DC) {
		tmp = adf_vqat_enabled_caps_add(enabled_caps,
						ADF_VQAT_CAP_SVC_DC_EXT_ID,
						ADF_VQAT_CAP_ATTR_INT,
						0,
						adf_vqat_cap_svc_dc_ext);
		if (tmp	< 0)
			return -EFAULT;
		size += tmp;
	}
#ifdef NON_GPL_COMMON
	if (type == QAT_VQAT_ADI_RP_SYM || parent->chaining_enabled) {
		tmp = adf_vqat_enabled_caps_add(enabled_caps,
						ADF_VQAT_CAP_SVC_SYM_HASH_ID,
						ADF_VQAT_CAP_ATTR_INT,
						0,
						adf_vqat_cap_svc_sym_hash);
		if (tmp	< 0)
			return -EFAULT;
		size += tmp;

		tmp = adf_vqat_enabled_caps_add(enabled_caps,
						ADF_VQAT_CAP_SVC_SYM_CIPHER_ID,
						ADF_VQAT_CAP_ATTR_INT,
						0,
						adf_vqat_cap_svc_sym_cipher);
		if (tmp	< 0)
			return -EFAULT;
		size += tmp;

	}

	if (type == QAT_VQAT_ADI_RP_ASYM) {
		tmp = adf_vqat_enabled_caps_add(enabled_caps,
						ADF_VQAT_CAP_SVC_ASYM_ID,
						ADF_VQAT_CAP_ATTR_INT,
						0,
						adf_vqat_cap_svc_asym);
		if (tmp	< 0)
			return -EFAULT;
		size += tmp;
	}
#endif

	tmp = adf_vqat_cap_svc_kpt_cert_len(parent, type);
	if (tmp) {
		tmp = adf_vqat_enabled_caps_add(enabled_caps,
						ADF_VQAT_CAP_SVC_KPT_CERT_ID,
						ADF_VQAT_CAP_ATTR_STR,
						tmp,
						adf_vqat_cap_svc_kpt_cert);
		if (tmp	< 0)
			return -EFAULT;
		size += tmp;
	}

	return size;
}

int adf_vqat_populate_caps(struct adf_vdcm_vqat_cap *vcap,
			   struct adf_accel_dev *parent,
			   struct adf_vqat_enabled_caps *enabled_caps,
			   enum vqat_type type)
{
	struct adf_vqat_cap *cap;
	int pos;
	int i, number;

	number = adf_vqat_enabled_caps_num(enabled_caps);
	cap = adf_vqat_caps_next_cap(vcap, NULL);
	for (i = 0; i < number; i++) {
		pos = adf_vqat_populate_cap(cap,
					    &enabled_caps->caps[i],
					    parent,
					    type);
		if (pos < 0)
			return -EINVAL;
		cap = adf_vqat_caps_next_cap(vcap, cap);
	}

	vcap->blk->len = (u64)cap - (u64)vcap->blk;
	vcap->blk->number = number;
	pr_debug("cap blk length is %u, total %u caps\n",
		 adf_vqat_caps_blk_size(vcap),
		 number);

	return 0;
}

int adf_vdcm_init_compat_manager(struct adf_accel_dev *accel_dev,
				 struct adf_accel_compat_manager **cm)
{
	int ret;

	ret = adf_iov_init_compat_manager(accel_dev, cm);
	if (ret)
		return ret;

	ret = adf_iov_register_compat_checker(accel_dev,
					      *cm,
					      adf_vdcm_compat_version_checker);
	if (ret)
		adf_iov_shutdown_compat_manager(accel_dev, cm);

	return ret;
}

void adf_vdcm_cleanup_compat_manager(struct adf_accel_dev *accel_dev,
				     struct adf_accel_compat_manager **cm)
{
	adf_iov_unregister_compat_checker(accel_dev,
					  *cm,
					  adf_vdcm_compat_version_checker);
	adf_iov_shutdown_compat_manager(accel_dev, cm);
}

static int adf_vqat_class_cap_new_def(void *obj, void *cb_data, s64 *p_res)
{
	struct adf_vqat_class *dclass = (struct adf_vqat_class *)cb_data;
	enum vqat_type type;
	struct adf_accel_dev *parent = (struct adf_accel_dev *)obj;
	int size;
	struct adf_vdcm_vqat_ops *ops = adf_vqat_class_ops(dclass);
	struct adf_vqat_enabled_caps caps_enabled;
	struct adf_vdcm_vqat_cap *vcap;

	if (!ops->prepare_cap || !ops->populate_cap) {
		pr_err("%s: one of cap function stuff is missing\n",
		       __func__);
		return -EFAULT;
	}
	adf_vqat_enabled_caps_init(&caps_enabled);
	type = adf_vqat_class_type(dclass);
	size = ops->prepare_cap(&caps_enabled, parent, type);
	if (!size) {
		pr_err("%s: failed to prepare vqat capability\n",
		       __func__);
		return -EFAULT;
	}
	vcap = kzalloc(sizeof(*vcap) + sizeof(*vcap->blk) + size, GFP_KERNEL);
	if (!vcap) {
		adf_vqat_enabled_caps_cleanup(&caps_enabled);
		return -ENOMEM;
	}
	adf_vqat_caps_set_blk(vcap, (void *)(vcap + 1));
	ops->populate_cap(vcap, parent, &caps_enabled, type);
	adf_vqat_enabled_caps_cleanup(&caps_enabled);
	*p_res = (s64)vcap;

	return 0;
}

static int adf_vqat_class_cap_destroy_def(void *obj, void *cb_data, s64 res)
{
	kfree((void *)res);

	return 0;
}

int adf_vdcm_alloc_vqat_svc_cap_def(struct adf_vdcm_vqat_cap *vcap,
				    struct adf_accel_dev *parent,
				    struct adf_vqat_class *dclass)
{
	struct adf_vdcm_obj_mgr *mgr;
	int ret;
	struct adf_vdcm_vqat_cap *class_vcap = NULL;

	mutex_lock(&dclass->class_lock);
	mgr = dclass->cap_mgr;
	if (!mgr) {
		mgr = adf_vdcm_obj_mgr_new(adf_vqat_class_cap_new_def,
					   adf_vqat_class_cap_destroy_def,
					   dclass);
		if (!mgr) {
			mutex_unlock(&dclass->class_lock);
			return -ENOMEM;
		}
		dclass->cap_mgr = mgr;
	}
	mutex_unlock(&dclass->class_lock);
	ret = adf_vdcm_obj_mgr_ref_obj(mgr, parent, (s64 *)&class_vcap);
	if (ret < 0 || !class_vcap) {
		pr_err("%s: failed to get vqat capability\n",
		       __func__);
		return -EFAULT;
	}
	adf_vqat_caps_clone_blk(vcap, class_vcap);

	return 0;
}

void adf_vdcm_free_vqat_svc_cap_def(struct adf_vdcm_vqat_cap *vcap,
				    struct adf_accel_dev *parent,
				    struct adf_vqat_class *dclass)
{
	struct adf_vdcm_obj_mgr *mgr;
	int refcount;

	mutex_lock(&dclass->class_lock);
	mgr = dclass->cap_mgr;
	if (mgr) {
		refcount = adf_vdcm_obj_mgr_unref_obj(mgr, parent, NULL);
		if (refcount == 0 && adf_vdcm_obj_mgr_is_empty(mgr)) {
			adf_vdcm_obj_mgr_destroy(mgr);
			dclass->cap_mgr = NULL;
		}
	}
	mutex_unlock(&dclass->class_lock);
}
