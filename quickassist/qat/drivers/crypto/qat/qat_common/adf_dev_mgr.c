// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2022 Intel Corporation */
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/list_sort.h>
#include "adf_cfg.h"
#include "adf_common_drv.h"

#define ADF_NUM_FUNC_PER_DEV 8
#if KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE && \
KERNEL_VERSION(5, 11, 0) > LINUX_VERSION_CODE && \
!defined(OS_YOCTO_05_10)  || \
KERNEL_VERSION(5, 13, 0) <= LINUX_VERSION_CODE
#define MY_LIST_HEAD const struct list_head
#define MY_ADF_ACCEL_DEV const struct adf_accel_dev
#else
#define MY_LIST_HEAD struct list_head
#define MY_ADF_ACCEL_DEV struct adf_accel_dev
#endif

static LIST_HEAD(accel_table);
static LIST_HEAD(vfs_table);
static DEFINE_MUTEX(table_lock);
static u32 num_devices;
static u8 id_map[ADF_MAX_DEVICES];

static int adf_get_vf_id_pdev(struct pci_dev *pdev)
{
	u16 first_vf_offset;
	int sriov;
	int vf_id = 0;
	u8 vf_slot = PCI_SLOT(pdev->devfn);
	u8 vf_func = PCI_FUNC(pdev->devfn);
	u8 slot, func;

	/* Get the first vf offset */
	sriov = pci_find_ext_capability(pdev->physfn,
					PCI_EXT_CAP_ID_SRIOV);
	pci_read_config_word(pdev->physfn, sriov + PCI_SRIOV_VF_OFFSET,
			     &first_vf_offset);

	slot = PCI_SLOT(pdev->physfn->devfn) +
	       (first_vf_offset / ADF_NUM_FUNC_PER_DEV);
	func = PCI_FUNC(pdev->physfn->devfn) +
	       (first_vf_offset % ADF_NUM_FUNC_PER_DEV);

	while (slot != vf_slot || func != vf_func) {
		vf_id++;
		if (!((func + 1) % ADF_NUM_FUNC_PER_DEV)) {
			func = 0x0;
			slot++;
			continue;
		}
		func++;
	}

	return vf_id;
}

int adf_get_vf_id(struct adf_accel_dev *vf, struct adf_accel_dev *pf)
{
	return adf_get_vf_id_pdev(accel_to_pci_dev(vf));
}

static int adf_get_vf_num(struct adf_accel_dev *vf,
			  struct adf_accel_dev *pf)
{
	return (accel_to_pci_dev(vf)->bus->number << 8) | adf_get_vf_id(vf, pf);
}

static struct vf_id_map *adf_find_vf(int domain_nr, u32 bdf)
{
	struct list_head *itr = NULL;

	list_for_each(itr, &vfs_table) {
		struct vf_id_map *ptr =
			list_entry(itr, struct vf_id_map, list);

		if (ptr->bdf == bdf && domain_nr == ptr->domain_nr)
			return ptr;
	}
	return NULL;
}


/**
 * adf_get_vf_real_id() - Translate fake to real device id
 * @fake: fake device ID to translate
 *
 * The "real" id is assigned to a device when it is initially
 * bound to the driver.
 * The "fake" id is usually the same as the real id, but
 * can change when devices are unbound from the qat driver,
 * perhaps to assign the device to a guest.
 */
static int adf_get_vf_real_id(u32 fake)
{
	struct list_head *itr = NULL;

	list_for_each(itr, &vfs_table) {
		struct vf_id_map *ptr =
			list_entry(itr, struct vf_id_map, list);
		if (ptr->fake_id == fake)
			return ptr->id;
	}
	return -1;
}

/**
 * adf_clean_vf_map() - Cleans VF id mapings
 *
 * Function cleans internal ids for virtual functions.
 * @vf: flag indicating whether mappings is cleaned
 *	for vfs only or for vfs and pfs
 */
void adf_clean_vf_map(bool vf)
{
	struct vf_id_map *map;
	struct list_head *ptr = NULL, *tmp = NULL;

	mutex_lock(&table_lock);
	list_for_each_safe(ptr, tmp, &vfs_table) {
		map = list_entry(ptr, struct vf_id_map, list);
		if (map->bdf != -1) {
			id_map[map->id] = 0;
			num_devices--;
		}

		if (vf && map->bdf == -1)
			continue;

		list_del(ptr);
		kfree(map);
	}
	mutex_unlock(&table_lock);
}
EXPORT_SYMBOL_GPL(adf_clean_vf_map);

typedef int (*list_cmp_func_t)(void *priv,
				MY_LIST_HEAD * first_device_entry,
				MY_LIST_HEAD * second_device_entry);

static int compare_accel_id(void *priv,
			    MY_LIST_HEAD *first_device_entry,
			    MY_LIST_HEAD *second_device_entry)
{
	MY_ADF_ACCEL_DEV *first_accel_dev =
					list_entry(first_device_entry,
						   struct adf_accel_dev, list);
	MY_ADF_ACCEL_DEV *second_accel_dev =
					list_entry(second_device_entry,
						   struct adf_accel_dev, list);

	/* compare accel dev list based on the accel_id and
	 * the return values as below:
	 * 1. Return negative value if first_accel_dev->accel_id less
	 * than second_accel_dev->accel_id.
	 * 2. Return Zero when first_accel_dev->accel_id and
	 * second_accel_dev->accel_id is equal.
	 * 3. Return positive value if first_accel_dev->accel_id
	 * greater than second_accel_dev->accel_id.
	 * This ensures list will be sorted in ascending order
	 * based on accel_dev id.
	 */
	return first_accel_dev->accel_id - second_accel_dev->accel_id;
}

/**
 * adf_update_class_index() - Update internal index
 * @hw_data:  Pointer to internal device data.
 *
 * Function updates internal dev index for devices.
 */
static void adf_update_class_index(struct adf_hw_device_data *hw_data)
{
	struct adf_hw_device_class *class = hw_data->dev_class;
	struct list_head *itr = NULL;
	int i = 0;

	/* Sort the accel_dev list based on the accel_id's
	 * using list_sort function
	 */
	list_sort(NULL, &accel_table, (list_cmp_func_t)compare_accel_id);

	list_for_each(itr, &accel_table) {
		struct adf_accel_dev *ptr =
				list_entry(itr, struct adf_accel_dev, list);

		if (ptr->hw_device->dev_class == class)
			ptr->hw_device->instance_id = i++;

		if (i == class->instances)
			break;
	}
}

static unsigned int adf_find_free_id(void)
{
	unsigned int i;

	for (i = 0; i < ADF_MAX_DEVICES; i++) {
		if (!id_map[i]) {
			id_map[i] = 1;
			return i;
		}
	}
	return ADF_MAX_DEVICES + 1;
}

unsigned int adf_devmgr_get_id(struct pci_dev *pdev)
{
	int id = 0;
	struct vf_id_map *map = NULL;
	int domain_nr = pci_domain_nr(pdev->bus);

	if (pdev->is_virtfn)
		map = adf_find_vf(domain_nr, (pdev->bus->number << 8 |
					      adf_get_vf_id_pdev(pdev)));
	if (map) {
		id = map->id;
	} else {
		mutex_lock(&table_lock);
		id = adf_find_free_id();
		mutex_unlock(&table_lock);
	}

	return id;
}
EXPORT_SYMBOL_GPL(adf_devmgr_get_id);

/**
 * adf_devmgr_add_dev() - Add accel_dev to the acceleration framework
 * @accel_dev:  Pointer to acceleration device.
 * @pf:		Corresponding PF if the accel_dev is a VF
 *
 * Function adds acceleration device to the acceleration framework.
 * To be used by QAT device specific drivers.
 *
 * Return: 0 on success, error code otherwise.
 */
int adf_devmgr_add_dev(struct adf_accel_dev *accel_dev,
		       struct adf_accel_dev *pf)
{
	struct list_head *itr = NULL;
	int ret = 0;

	if (num_devices == ADF_MAX_DEVICES) {
		dev_err(&GET_DEV(accel_dev), "Only support up to %d devices\n",
			ADF_MAX_DEVICES);
		return -EFAULT;
	}

	mutex_lock(&table_lock);
	atomic_set(&accel_dev->ref_count, 0);

	/* PF on host or VF on guest */
	if (!accel_dev->is_vf || (accel_dev->is_vf && !pf)) {
		struct vf_id_map *map;

		list_for_each(itr, &accel_table) {
			struct adf_accel_dev *ptr =
				list_entry(itr, struct adf_accel_dev, list);

			if (ptr == accel_dev) {
				ret = -EEXIST;
				goto unlock;
			}
		}

		list_add_tail(&accel_dev->list, &accel_table);
		num_devices++;
		map = kzalloc(sizeof(*map), GFP_KERNEL);
		if (!map) {
			ret = -ENOMEM;
			goto unlock;
		}
		map->bdf = ~0;
		map->id = accel_dev->accel_id;
		map->fake_id = map->id;
		map->attached = true;
		list_add_tail(&map->list, &vfs_table);
	} else if (accel_dev->is_vf && pf) {
		/* VF on host */
		struct vf_id_map *map;
		struct pci_dev *pci_dev = accel_to_pci_dev(accel_dev);
		int domain_nr = pci_domain_nr(pci_dev->bus);

		map = adf_find_vf(domain_nr, adf_get_vf_num(accel_dev, pf));
		if (map) {
			struct vf_id_map *next;

			accel_dev->accel_id = map->id;
			list_add_tail(&accel_dev->list, &accel_table);
			map->fake_id++;
			map->attached = true;
			next = list_next_entry(map, list);
			while (next && &next->list != &vfs_table) {
				next->fake_id++;
				next = list_next_entry(next, list);
			}

			ret = 0;
			goto unlock;
		}

		map = kzalloc(sizeof(*map), GFP_KERNEL);
		if (!map) {
			ret = -ENOMEM;
			goto unlock;
		}
		num_devices++;
		list_add_tail(&accel_dev->list, &accel_table);
		map->bdf = adf_get_vf_num(accel_dev, pf);
		map->id = accel_dev->accel_id;
		map->fake_id = map->id;
		map->attached = true;
		map->domain_nr = domain_nr;
		list_add_tail(&map->list, &vfs_table);
	}
unlock:
	adf_update_class_index(accel_dev->hw_device);
	mutex_unlock(&table_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(adf_devmgr_add_dev);

struct list_head *adf_devmgr_get_head(void)
{
	return &accel_table;
}

/**
 * adf_devmgr_rm_dev() - Remove accel_dev from the acceleration framework.
 * @accel_dev:  Pointer to acceleration device.
 * @pf:		Corresponding PF if the accel_dev is a VF
 *
 * Function removes acceleration device from the acceleration framework.
 * To be used by QAT device specific drivers.
 *
 * Return: void
 */
void adf_devmgr_rm_dev(struct adf_accel_dev *accel_dev,
		       struct adf_accel_dev *pf)
{
	mutex_lock(&table_lock);
	if (!accel_dev->is_vf || (accel_dev->is_vf && !pf)) {
		id_map[accel_dev->accel_id] = 0;
		num_devices--;
	} else if (accel_dev->is_vf && pf) {
		struct vf_id_map *map, *next;
		struct pci_dev *pci_dev = accel_to_pci_dev(accel_dev);
		int domain_nr = pci_domain_nr(pci_dev->bus);

		map = adf_find_vf(domain_nr, adf_get_vf_num(accel_dev, pf));
		if (!map) {
			dev_err(&GET_DEV(accel_dev), "Failed to find VF map\n");
			goto unlock;
		}
		map->fake_id--;
		map->attached = false;
		next = list_next_entry(map, list);
		while (next && &next->list != &vfs_table) {
			next->fake_id--;
			next = list_next_entry(next, list);
		}
	}
unlock:
	list_del(&accel_dev->list);
	adf_update_class_index(accel_dev->hw_device);
	mutex_unlock(&table_lock);
}
EXPORT_SYMBOL_GPL(adf_devmgr_rm_dev);

struct adf_accel_dev *adf_devmgr_get_first(void)
{
	struct adf_accel_dev *dev = NULL;

	if (!list_empty(&accel_table))
		dev = list_first_entry(&accel_table, struct adf_accel_dev,
				       list);
	return dev;
}

/**
 * adf_devmgr_pci_to_accel_dev() - Get accel_dev associated with the pci_dev.
 * @pci_dev:  Pointer to pci device.
 *
 * Function returns acceleration device associated with the given pci device.
 * To be used by QAT device specific drivers.
 *
 * Return: pointer to accel_dev or NULL if not found.
 */
struct adf_accel_dev *adf_devmgr_pci_to_accel_dev(struct pci_dev *pci_dev)
{
	struct list_head *itr = NULL;

	mutex_lock(&table_lock);
	list_for_each(itr, &accel_table) {
		struct adf_accel_dev *ptr =
				list_entry(itr, struct adf_accel_dev, list);

		if (ptr->accel_pci_dev.pci_dev == pci_dev) {
			mutex_unlock(&table_lock);
			return ptr;
		}
	}
	mutex_unlock(&table_lock);
	return NULL;
}
EXPORT_SYMBOL_GPL(adf_devmgr_pci_to_accel_dev);

int adf_devmgr_get_real_id(uint32_t fake_id)
{
	int real_id;

	mutex_lock(&table_lock);
	real_id = adf_get_vf_real_id(fake_id);
	mutex_unlock(&table_lock);

	return real_id;
}

struct adf_accel_dev *adf_devmgr_get_dev_by_id(uint32_t id)
{
	struct list_head *itr = NULL;

	mutex_lock(&table_lock);
	list_for_each(itr, &accel_table) {
		struct adf_accel_dev *ptr =
				list_entry(itr, struct adf_accel_dev, list);
		if (ptr->accel_id == id) {
			mutex_unlock(&table_lock);
			return ptr;
		}
	}
	mutex_unlock(&table_lock);
	return NULL;
}

int adf_devmgr_verify_id(uint32_t *id)
{
	struct adf_accel_dev *accel_dev;

	if (*id == ADF_CFG_ALL_DEVICES)
		return 0;

	accel_dev = adf_devmgr_get_dev_by_id(*id);
	if (!accel_dev)
		return -ENODEV;

	/* Correct the id if real and fake differ */
	*id = accel_dev->accel_id;
	return 0;
}

int adf_get_num_dettached_vfs(void)
{
	struct list_head *itr = NULL;
	int vfs = 0;

	mutex_lock(&table_lock);
	list_for_each(itr, &vfs_table) {
		struct vf_id_map *ptr =
			list_entry(itr, struct vf_id_map, list);
		if (ptr->bdf != ~0 && !ptr->attached)
			vfs++;
	}
	mutex_unlock(&table_lock);
	return vfs;
}

void adf_devmgr_get_num_dev(uint32_t *num)
{
	*num = num_devices - adf_get_num_dettached_vfs();
}

/**
 * adf_dev_in_use() - Check whether accel_dev is currently in use
 * @accel_dev: Pointer to acceleration device.
 *
 * To be used by QAT device specific drivers.
 *
 * Return: 1 when device is in use, 0 otherwise.
 */
int adf_dev_in_use(struct adf_accel_dev *accel_dev)
{
	return atomic_read(&accel_dev->ref_count) != 0;
}
EXPORT_SYMBOL_GPL(adf_dev_in_use);

/**
 * adf_dev_get() - Increment accel_dev reference count
 * @accel_dev: Pointer to acceleration device.
 *
 * Increment the accel_dev refcount and if this is the first time
 * incrementing it during this period the accel_dev is in use,
 * increment the module refcount too.
 * If the accel_dev parsed is vf accel_dev on host,
 * increment the corresponding pf accel_dev refcount and its module refcount.
 * To be used by QAT device specific drivers.
 *
 * Return: 0 when successful, EFAULT when fail to bump module refcount
 */
int adf_dev_get(struct adf_accel_dev *accel_dev)
{
	struct adf_accel_dev *pf_accel_dev = NULL;
	struct pci_dev *pf_pci_dev = NULL;

	if (atomic_add_return(1, &accel_dev->ref_count) == 1) {
		if (!try_module_get(accel_dev->owner))
			return -EFAULT;
		if (accel_dev->is_vf) {
			pf_pci_dev = accel_dev->accel_pci_dev.pci_dev->physfn;
			pf_accel_dev = adf_devmgr_pci_to_accel_dev(pf_pci_dev);
			if (pf_accel_dev)
				return adf_dev_get(pf_accel_dev);
		}
	}
	return 0;
}
EXPORT_SYMBOL_GPL(adf_dev_get);

/**
 * adf_dev_put() - Decrement accel_dev reference count
 * @accel_dev: Pointer to acceleration device.
 *
 * Decrement the accel_dev refcount and if this is the last time
 * decrementing it during this period the accel_dev is in use,
 * decrement the module refcount too.
 * If the accel_dev parsed is vf accel_dev on host,
 * decrement the corresponding pf accel_dev refcount and its module refcount.
 * To be used by QAT device specific drivers.
 *
 * Return: void
 */
void adf_dev_put(struct adf_accel_dev *accel_dev)
{
	struct adf_accel_dev *pf_accel_dev = NULL;
	struct pci_dev *pf_pci_dev = NULL;

	if (atomic_sub_return(1, &accel_dev->ref_count) == 0) {
		module_put(accel_dev->owner);
		if (accel_dev->is_vf) {
			pf_pci_dev = accel_dev->accel_pci_dev.pci_dev->physfn;
			pf_accel_dev = adf_devmgr_pci_to_accel_dev(pf_pci_dev);
			if (pf_accel_dev)
				adf_dev_put(pf_accel_dev);
		}
	}
}
EXPORT_SYMBOL_GPL(adf_dev_put);

/**
 * adf_devmgr_in_reset() - Check whether device is in reset
 * @accel_dev: Pointer to acceleration device.
 *
 * To be used by QAT device specific drivers.
 *
 * Return: 1 when the device is being reset, 0 otherwise.
 */
int adf_devmgr_in_reset(struct adf_accel_dev *accel_dev)
{
	return test_bit(ADF_STATUS_RESTARTING, &accel_dev->status);
}
EXPORT_SYMBOL_GPL(adf_devmgr_in_reset);

/**
 * adf_dev_started() - Check whether device has started
 * @accel_dev: Pointer to acceleration device.
 *
 * To be used by QAT device specific drivers.
 *
 * Return: 1 when the device has started, 0 otherwise
 */
int adf_dev_started(struct adf_accel_dev *accel_dev)
{
	return test_bit(ADF_STATUS_STARTED, &accel_dev->status);
}
EXPORT_SYMBOL_GPL(adf_dev_started);

/**
 * adf_devmgr_update_drv_rm() - Indicate specific device driver is removed
 * @dev_id: device id.
 *
 * Function to mark the device when the QAT specific device driver is unloaded
 * from the system.
 */
void adf_devmgr_update_drv_rm(const u16 dev_id)
{
	struct list_head *itr;

	mutex_lock(&table_lock);
	list_for_each(itr, &accel_table) {
		struct adf_accel_dev *ptr =
			list_entry(itr, struct adf_accel_dev, list);
		struct pci_dev *pdev = accel_to_pci_dev(ptr);

		if (pdev->device == dev_id)
			ptr->is_drv_rm = true;
	}
	mutex_unlock(&table_lock);
}
EXPORT_SYMBOL_GPL(adf_devmgr_update_drv_rm);

/*
 * adf_devmgr_get_dev_by_bdf() - Look up accel_dev by BDF
 * @pci_addr: pointer to adf_pci_address structure
 *
 * To be used by DU and SLA ioctls.
 *
 * Return: accel_dev if found, NULL otherwise.
 *
 * Note: Caller has to call adf_dev_put once finished using the accel_dev!
 */
struct adf_accel_dev *
adf_devmgr_get_dev_by_bdf(struct adf_pci_address *pci_addr)
{
	struct adf_accel_dev *accel_dev = NULL;
	struct pci_dev *pci_dev = NULL;
	unsigned int devfn = PCI_DEVFN(pci_addr->dev, pci_addr->func);
	bool dev_found = false;

	mutex_lock(&table_lock);
	list_for_each_entry(accel_dev, &accel_table, list) {
		pci_dev = accel_to_pci_dev(accel_dev);
		if (pci_dev->bus->number == pci_addr->bus &&
		    pci_dev->devfn == devfn &&
		    pci_domain_nr(pci_dev->bus) == pci_addr->domain_nr) {
			dev_found = true;
			break;
		}
	}
	mutex_unlock(&table_lock);
	if (dev_found) {
		adf_dev_get(accel_dev);
		return accel_dev;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(adf_devmgr_get_dev_by_bdf);

/*
 * adf_devmgr_get_dev_by_domain_bus() - Look up accel_dev using pci domain and bus
 * @pci_addr: Address of pci device
 *
 * To be used by DU and SLA ioctls.
 *
 * Return: accel_dev if found, NULL otherwise.
 *
 * Note: Caller has to call adf_dev_put once finished using the accel_dev
 */

struct adf_accel_dev *
adf_devmgr_get_dev_by_pci_domain_bus(struct adf_pci_address *pci_addr)
{
	struct adf_accel_dev *accel_dev = NULL;
	struct pci_dev *pci_dev = NULL;
	bool dev_found = false;

	mutex_lock(&table_lock);
	list_for_each_entry(accel_dev, &accel_table, list) {
		pci_dev = accel_to_pci_dev(accel_dev);
		if (accel_to_pci_dev(accel_dev)->bus->number == pci_addr->bus &&
		    pci_domain_nr(pci_dev->bus) == pci_addr->domain_nr) {
			dev_found = true;
			break;
		}
	}
	mutex_unlock(&table_lock);
	if (dev_found) {
		adf_dev_get(accel_dev);
		return accel_dev;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(adf_devmgr_get_dev_by_pci_domain_bus);

