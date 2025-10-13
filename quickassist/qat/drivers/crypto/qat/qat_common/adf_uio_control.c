// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2021 Intel Corporation */

#include "adf_common_drv.h"
#include "adf_accel_devices.h"
#include "adf_uio_control.h"
#include "adf_transport_internal.h"

static ssize_t accelid_show(struct adf_uio_control_accel *accel, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", accel->accel_dev->accel_id);
}

static ssize_t first_minor_show(struct adf_uio_control_accel *accel, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", accel->first_minor);
}

static ssize_t last_minor_show(struct adf_uio_control_accel *accel, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", accel->last_minor);
}

static ssize_t num_bundles_show(struct adf_uio_control_accel *accel, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", accel->nb_bundles);
}

static ssize_t type_show(struct adf_uio_control_accel *accel, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n",
			accel->accel_dev->hw_device->dev_class->type);
}

static ssize_t name_show(struct adf_uio_control_accel *accel, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n",
			accel->accel_dev->hw_device->dev_class->name);
}

static ssize_t revid_show(struct adf_uio_control_accel *accel, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n",
			accel->accel_dev->accel_pci_dev.revid);
}

static ssize_t is_vf_show(struct adf_uio_control_accel *accel, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", accel->accel_dev->is_vf);
}

static ssize_t device_mem_avail_show(struct adf_uio_control_accel *accel,
				     char *buf)
{
	struct adf_hw_aram_info *aram_info = accel->accel_dev->aram_info;
	u32 device_mem_avail = 0;

	if (aram_info)
		device_mem_avail = aram_info->inter_buff_aram_region_size;

	return snprintf(buf, PAGE_SIZE, "%u\n", device_mem_avail);
}

struct accel_sysfs_entry {
	struct attribute attr;
	ssize_t (*show)(struct adf_uio_control_accel *, char *);
	ssize_t (*store)(struct adf_uio_control_accel *, const char *, size_t);
};

static struct accel_sysfs_entry accel_accelid = __ATTR_RO(accelid);
static struct accel_sysfs_entry accel_first_minor = __ATTR_RO(first_minor);
static struct accel_sysfs_entry accel_last_minor = __ATTR_RO(last_minor);
static struct accel_sysfs_entry accel_num_bundles = __ATTR_RO(num_bundles);
static struct accel_sysfs_entry accel_type = __ATTR_RO(type);
static struct accel_sysfs_entry accel_name = __ATTR_RO(name);
static struct accel_sysfs_entry accel_revid = __ATTR_RO(revid);
static struct accel_sysfs_entry accel_is_vf = __ATTR_RO(is_vf);
static struct accel_sysfs_entry accel_mem_avail = __ATTR_RO(device_mem_avail);

static struct attribute *accel_default_attrs[] = {
	&accel_accelid.attr,
	&accel_first_minor.attr,
	&accel_last_minor.attr,
	&accel_num_bundles.attr,
	&accel_type.attr,
	&accel_name.attr,
	&accel_revid.attr,
	&accel_is_vf.attr,
	&accel_mem_avail.attr,
	NULL,
};
ATTRIBUTE_GROUPS(accel_default);

static ssize_t accel_show(struct kobject *kobj, struct attribute *attr,
			  char *buf);
static const struct sysfs_ops accel_sysfs_ops = {
	.show = accel_show,
};

static void accel_kobject_free(struct kobject *kobj)
{
	struct adf_uio_control_accel *accel;

	accel = container_of(kobj, struct adf_uio_control_accel, kobj);
	accel->accel_dev->accel = NULL;
	kfree(accel);
}

static ssize_t accel_show(struct kobject *kobj, struct attribute *attr,
			  char *buf)
{
	struct adf_uio_control_accel *accel;
	struct accel_sysfs_entry *entry;

	accel = container_of(kobj, struct adf_uio_control_accel, kobj);
	entry = container_of(attr, struct accel_sysfs_entry, attr);

	if (!entry->show)
		return -EIO;
	return entry->show(accel, buf);
}

static ssize_t dev_name_show(struct adf_uio_control_bundle *bundle, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", bundle->name);
}

static ssize_t dev_minor_show(struct adf_uio_control_bundle *bundle, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", bundle->device_minor);
}

static ssize_t hardware_bundle_show(struct adf_uio_control_bundle *bundle,
				    char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", bundle->hardware_bundle_number);
}

static ssize_t rings_reserved_show(struct adf_uio_control_bundle *bundle,
				   char *buf)
{
	struct adf_uio_instance_rings *instance_rings = NULL;
	char *dst = buf;
	size_t size;
	size_t bufsize = PAGE_SIZE;

	size = scnprintf(dst, bufsize, "0x%04X: ", bundle->rings_used);
	dst += size;
	bufsize -= size;

	mutex_lock(&bundle->list_lock);
	if (!list_empty(&bundle->list))
		list_for_each_entry(instance_rings, &bundle->list, list) {
			size = scnprintf(dst, bufsize,
					 "PID %d, rings 0x%04X. ",
					 instance_rings->user_pid,
					 instance_rings->ring_mask);
			dst += size;
			bufsize -= size;
		}
	mutex_unlock(&bundle->list_lock);

	size = scnprintf(dst, bufsize, "\n");
	dst += size;
	return dst - buf;
}

struct bundle_sysfs_entry {
	struct attribute attr;
	ssize_t (*show)(struct adf_uio_control_bundle *, char *);
	ssize_t (*store)(struct adf_uio_control_bundle *, const char *, size_t);
};

static struct bundle_sysfs_entry bundle_dev_name = __ATTR_RO(dev_name);
static struct bundle_sysfs_entry bundle_dev_minor = __ATTR_RO(dev_minor);
static struct bundle_sysfs_entry bundle_hardware_bundle =
						__ATTR_RO(hardware_bundle);
static struct bundle_sysfs_entry bundle_rings_reserved =
						__ATTR_RO(rings_reserved);

static struct attribute *bundle_default_attrs[] = {
	&bundle_dev_name.attr,
	&bundle_dev_minor.attr,
	&bundle_hardware_bundle.attr,
	&bundle_rings_reserved.attr,
	NULL,
};
ATTRIBUTE_GROUPS(bundle_default);

static ssize_t bundle_show(struct kobject *kobj, struct attribute *attr,
			   char *buf);
static const struct sysfs_ops bundle_sysfs_ops = {
	.show = bundle_show,
};

static void bundle_kobject_free(struct kobject *kobj)
{
	struct adf_uio_control_bundle *bundle;

	bundle = container_of(kobj, struct adf_uio_control_bundle, kobj);
	bundle->uio_priv.bundle = NULL;
	bundle->uio_priv.accel = NULL;
	kfree(bundle->uio_info.name);
	kfree(bundle->uio_info.mem[0].name);
	if (bundle->vma && bundle->vma->vm_private_data)
		bundle->vma->vm_private_data = NULL;
	kfree(bundle);
}

static ssize_t bundle_show(struct kobject *kobj, struct attribute *attr,
			   char *buf)
{
	struct adf_uio_control_bundle *bundle;
	struct bundle_sysfs_entry *entry;

	bundle = container_of(kobj, struct adf_uio_control_bundle, kobj);
	entry = container_of(attr, struct bundle_sysfs_entry, attr);

	if (!entry->show)
		return -EIO;
	return entry->show(bundle, buf);
}

void adf_uio_sysfs_bundle_delete(struct adf_accel_dev *accel_dev,
				 unsigned int bundle_num)
{
	struct adf_uio_control_bundle *bundle =
		accel_dev->accel->bundle[bundle_num];

	adf_uio_bundle_unref(bundle);
	accel_dev->accel->bundle[bundle_num] = NULL;
}

static void adf_uio_sysfs_cleanup(struct adf_accel_dev *accel_dev)
{
	adf_uio_accel_unref(accel_dev->accel);
}

static struct kobj_type accel_ktype = {
	.release = accel_kobject_free,
	.sysfs_ops = &accel_sysfs_ops,
	.default_groups = accel_default_groups,
};

int adf_uio_sysfs_create(struct adf_accel_dev *accel_dev)
{
	struct kobject *dev_kobj;
	struct pci_dev *pdev = accel_dev->accel_pci_dev.pci_dev;
	struct adf_uio_control_accel *accel;
	int nb_bundles = accel_dev->hw_device->num_banks;
	int ret;

	dev_kobj = &pdev->dev.kobj;

	/* Allow the struct to have the maximum number of bundle pointers,
	 * so that the number of bundles assigned to user-space can be
	 * changed dynamically.
	 */
	accel = kzalloc(sizeof(*accel) + nb_bundles * sizeof(accel->bundle[0]),
			GFP_KERNEL);
	if (!accel)
		return -ENOMEM;

	mutex_lock(&uio_lock);
	accel_dev->accel = accel;
	accel->accel_dev = accel_dev;

	ret = kobject_init_and_add(&accel->kobj, &accel_ktype, dev_kobj,
				   "uio_ctrl");
	if (ret) {
		dev_err(&GET_DEV(accel_dev), "kobject_init_and_add failed for uio_ctrl\n");
		kfree(accel);
		accel_dev->accel = NULL;
		mutex_unlock(&uio_lock);
		return ret;
	}

	mutex_unlock(&uio_lock);
	return 0;
}

static struct kobj_type bundle_ktype = {
	.release = bundle_kobject_free,
	.sysfs_ops = &bundle_sysfs_ops,
	.default_groups = bundle_default_groups,
};

int
adf_find_hw_bundle_by_usr(struct adf_accel_dev *accel_dev, u32 uio_bundle_nr)
{
	struct adf_etr_data *etr_data = accel_dev->transport;
	u32 uio_bundle_find = 0;
	u32 hw_bundle_nr;

	/* Find a available hardware bundle index */
	for (hw_bundle_nr = 0;
		 hw_bundle_nr < GET_MAX_BANKS(accel_dev);
		 hw_bundle_nr++) {
		if (etr_data->banks[hw_bundle_nr].type == USER) {
			if (uio_bundle_nr == uio_bundle_find)
				return hw_bundle_nr;
			uio_bundle_find++;
		}
	}
	return INVALID_BUNDLE_INDEX;
}

int adf_uio_sysfs_bundle_create(struct pci_dev *pdev,
				unsigned int bundle_num,
				struct adf_uio_control_accel *accel)
{
	struct adf_accel_dev *accel_dev = accel->accel_dev;
	struct adf_uio_control_bundle *bundle;
	u32 hw_bundle_num = 0;
	int ret;

	if (!accel_dev)
		return -ENODEV;

	bundle = kzalloc(sizeof(*bundle), GFP_KERNEL);
	if (!bundle)
		return -ENOMEM;

	accel->bundle[bundle_num] = bundle;

	hw_bundle_num = adf_find_hw_bundle_by_usr(accel_dev, bundle_num);

	ret = kobject_init_and_add(&bundle->kobj, &bundle_ktype,
				   &accel_dev->accel->kobj, "bundle_%u",
				   hw_bundle_num);
	if (ret) {
		dev_err(&GET_DEV(accel_dev), "kobject_init_and_add failed for bundle\n");
		accel->bundle[bundle_num] = NULL;
		kfree(bundle);
		return ret;
	}

	return 0;
}

void adf_uio_sysfs_delete(struct adf_accel_dev *accel_dev)
{
	mutex_lock(&uio_lock);
	adf_uio_sysfs_cleanup(accel_dev);
#ifdef QAT_UIO
	if (accel_dev->accel)
		accel_dev->accel->accel_dev = NULL;
#endif
	mutex_unlock(&uio_lock);
}

void adf_uio_accel_ref(struct adf_uio_control_accel *accel)
{
	if (accel)
		kobject_get(&accel->kobj);
}

void adf_uio_accel_unref(struct adf_uio_control_accel *accel)
{
	if (accel)
		kobject_put(&accel->kobj);
}

void adf_uio_bundle_ref(struct adf_uio_control_bundle *bundle)
{
	if (bundle)
		kobject_get(&bundle->kobj);
}

void adf_uio_bundle_unref(struct adf_uio_control_bundle *bundle)
{
	if (bundle)
		kobject_put(&bundle->kobj);
}
