#ifndef __HUGEPAGE_H__
#define __HUGEPAGE_H__

#define SIZE_1G (1024*1024*1024)

int mem_virt2phy(const void *virtaddr, uint64_t *physaddr_ptr);
void *alloc_1g_hugepage();
void free_1g_hugepage(void* page_addr);

#endif