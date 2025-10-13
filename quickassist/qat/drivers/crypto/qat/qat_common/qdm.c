// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2021 Intel Corporation */
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/pci.h>
#include "qdm.h"

static struct iommu_domain *domain;

static bool iommu_configured_passthrough(void)
{
	static int iommu_pt = -1;
	bool (*iommu_fn)(void);

	if (iommu_pt != -1)
		return iommu_pt;

	iommu_fn = __symbol_get("iommu_default_passthrough");
	if (iommu_fn) {
		iommu_pt = iommu_fn();
		__symbol_put("iommu_default_passthrough");
	} else {
		unsigned long (*kallsym_fn)(const char *sym);

		kallsym_fn = __symbol_get("kallsyms_lookup_name");
		if (!kallsym_fn) {
			iommu_pt = 0;
		} else {
			unsigned long addr;

			addr = kallsym_fn("iommu_pass_through");
			if (addr)
				iommu_pt = *(int *)addr;
			else
				iommu_pt = 0;
			__symbol_put("kallsyms_lookup_name");
		}
	}

	pr_info("QDM: iommu_pass_through is set to %d\n",
		iommu_pt);

	return iommu_pt;
}

static inline int iommu_under_pt(void)
{
	return (iommu_configured_passthrough() && qdm_iommu_present());
}

/**
 * qdm_attach_device() - Attach a device to the QAT IOMMU domain
 * @dev: Device to be attached
 *
 * Function attaches the device to the QDM IOMMU domain.
 *
 * Return: 0 on success, error code otherwise.
 */
int qdm_attach_device(struct device *dev)
{
	if (!dev) {
		pr_err("QDM: Invalid device\n");
		return -ENODEV;
	}

	if (!device_iommu_mapped(dev)) {
		dev_info(dev, "Device is bypassing iommu\n");
		return 0;
	}

	if (!domain) {
		if (iommu_under_pt()) {
			dma_addr_t daddr;
			void *paddr;

			/* To ensure the device is associated with the
			 * default identity domain before any DMA requests
			 * start when iommu works under PT mode in any case.
			 **/
			paddr = dma_alloc_coherent(dev,
						   PAGE_SIZE,
						   &daddr,
						   GFP_KERNEL);
			if (!paddr)
				return -ENODEV;
			dma_free_coherent(dev, PAGE_SIZE, paddr, daddr);
		}

		return 0;
	}

	return iommu_attach_device(domain, dev);
}

/**
 * qdm_detach_device() - Detach a device from the QAT IOMMU domain
 * @dev: Device to be detached
 *
 * Function detaches the device from the QDM IOMMU domain.
 *
 * Return: 0 on success, error code otherwise.
 */
int qdm_detach_device(struct device *dev)
{
	if (!domain)
		return 0;

	if (!dev) {
		pr_err("QDM: Invalid device\n");
		return -ENODEV;
	}

	if (!device_iommu_mapped(dev))
		return 0;

	iommu_detach_device(domain, dev);
	return 0;
}

static int qdm_iommu_mem_map(dma_addr_t *iova, phys_addr_t paddr, size_t size)
{
	if (!domain)
		return 0;

	return iommu_map(domain, *iova, paddr, size,
			IOMMU_READ | IOMMU_WRITE | IOMMU_CACHE,
			GFP_KERNEL);
}

/**
 * qdm_iommu_map() - Map a block of memory to the QAT IOMMU domain
 * @iova:   Device virtual address
 * @vaddr:  Kernel virtual address
 * @size:   Size (in bytes) of the memory block.
 *          Must be a multiple of PAGE_SIZE
 *
 * Function maps a block of memory to the QDM IOMMU domain.
 *
 * Return: 0 on success, error code otherwise.
 */
int qdm_iommu_map(dma_addr_t *iova, void *vaddr, size_t size)
{
	phys_addr_t paddr = (phys_addr_t)virt_to_phys(vaddr);
	*iova = (dma_addr_t)paddr;
	return qdm_iommu_mem_map(iova, paddr, size);
}
EXPORT_SYMBOL_GPL(qdm_iommu_map);

/**
 * qdm_iommu_unmap() - Unmap a block of memory from the QAT IOMMU domain
 * @iova:   Device virtual address
 * @size:   Size (in bytes) of the memory block
 *          Must be the same size as mapped.
 *
 * Function unmaps a block of memory from the QDM IOMMU domain.
 *
 * Return: 0 on success, error code otherwise.
 */
int qdm_iommu_unmap(dma_addr_t iova, size_t size)
{
#if KERNEL_VERSION(3, 2, 45) < LINUX_VERSION_CODE
	size_t unmap_size = 0;
#endif
	if (!domain)
		return 0;

#if KERNEL_VERSION(3, 2, 45) < LINUX_VERSION_CODE
	unmap_size = iommu_unmap(domain, (unsigned long)iova, size);

	if (unmap_size != size) {
		pr_err("QDM: %s: Failed to unmap %zx/%zu\n",
		       __func__, size, unmap_size);
		return -EFAULT;
	}
#else
	iommu_unmap(domain, (unsigned long)iova, size);
#endif

	return 0;
}
EXPORT_SYMBOL_GPL(qdm_iommu_unmap);

/**
 * qdm_hugepage_iommu_map() - Map a hugepage block of memory to the QAT IOMMU
 * domain
 * @iova:   Device virtual address
 * @va_page:  Kernel virtual address
 * @size:   Size (in bytes) of the memory block.
 *          Must be a multiple of PAGE_SIZE
 *
 * Function maps a block of memory to the QDM IOMMU domain.
 *
 * Return: 0 on success, error code otherwise.
 */
int qdm_hugepage_iommu_map(dma_addr_t *iova, void *va_page, size_t size)
{
	phys_addr_t paddr = (phys_addr_t)page_to_phys((struct page *)va_page);
	*iova = (dma_addr_t)paddr;
	return qdm_iommu_mem_map(iova, paddr, size);
}
EXPORT_SYMBOL_GPL(qdm_hugepage_iommu_map);

int __init qdm_init(void)
{
	if (!qdm_iommu_present() || iommu_under_pt())
		return 0;

	domain = iommu_domain_alloc(&pci_bus_type);

	if (!domain) {
		pr_err("QDM: Failed to allocate a domain\n");
		return -1;
	}
	return 0;
}

void __exit qdm_exit(void)
{
	if (domain)
		iommu_domain_free(domain);
	domain = NULL;
}
