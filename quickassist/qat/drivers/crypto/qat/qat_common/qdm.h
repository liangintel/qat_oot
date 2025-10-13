/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2021 Intel Corporation */
#ifndef __QDM_H__
#define __QDM_H__
#include <linux/pci.h>
#include <linux/types.h>

struct device;


#ifndef pci_bus_iommu_present
static inline int pci_device_iommu_mapped(struct device *dev, void *data)
{
	return (dev->iommu_group) ? 1 : 0;
}

static inline bool pci_bus_iommu_present(const struct bus_type *bus)
{
	return bus_for_each_dev(bus, NULL, NULL, pci_device_iommu_mapped);
}
#endif

static inline bool qdm_iommu_present(void)
{
	return pci_bus_iommu_present(&pci_bus_type);
}

int qdm_init(void);
void qdm_exit(void);
int qdm_attach_device(struct device *dev);
int qdm_detach_device(struct device *dev);
int qdm_iommu_map(dma_addr_t *iova, void *vaddr, size_t size);
int qdm_iommu_unmap(dma_addr_t iova, size_t size);
int qdm_hugepage_iommu_map(dma_addr_t *iova, void *va_page, size_t size);

#endif /* __QDM_H__ */
