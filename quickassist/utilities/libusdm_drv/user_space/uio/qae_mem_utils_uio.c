/***************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 *   redistributing this file, you may do so under either license.
 * 
 *   GPL LICENSE SUMMARY
 * 
 *   Copyright(c) 2007-2023 Intel Corporation. All rights reserved.
 * 
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of version 2 of the GNU General Public License as
 *   published by the Free Software Foundation.
 * 
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   General Public License for more details.
 * 
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *   The full GNU General Public License is included in this distribution
 *   in the file called LICENSE.GPL.
 * 
 *   Contact Information:
 *   Intel Corporation
 * 
 *   BSD LICENSE
 * 
 *   Copyright(c) 2007-2023 Intel Corporation. All rights reserved.
 *   All rights reserved.
 * 
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 * 
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 * 
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * 
 *  version: QAT20.L.1.2.30-00090
 *
 ***************************************************************************/
/**
 ****************************************************************************
 * @file qae_mem_utils_uio.c
 *
 * This file provides for Linux user space memory allocation. It uses
 * a driver that allocates the memory in kernel memory space (to ensure
 * physically contiguous memory) and maps it to
 * user space for use by the  quick assist sample code
 *
 ***************************************************************************/
#include "qae_page_table_uio.h"
#include "qae_mem_utils_common.h"
#ifdef ICP_THREAD_SPECIFIC_USDM
#include "qae_mem_multi_thread.h"
#else
#include "qae_mem_lib_utils.h"
#endif
/**************************************************************************
                                   macro
**************************************************************************/
#define QAE_MEM "/dev/usdm_drv"

/**************************************************************************
    static variable
**************************************************************************/
int g_fd = -1;
#ifndef ICP_THREAD_SPECIFIC_USDM
int g_strict_node = 1;
#endif

/**************************************************************************
                                  function
**************************************************************************/
API_LOCAL
void __qae_set_free_page_table_fptr(free_page_table_fptr_t fp)
{
    free_page_table_fptr = fp;
}

API_LOCAL
void __qae_set_loadaddr_fptr(load_addr_fptr_t fp)
{
    load_addr_fptr = fp;
}

API_LOCAL
void __qae_set_loadkey_fptr(load_key_fptr_t fp)
{
    load_key_fptr = fp;
}

static inline void ioctl_free_slab(const int fd, dev_mem_info_t *memInfo)
{
    int ret = 0;

    ret = mem_ioctl(fd, DEV_MEM_IOC_MEMFREE, memInfo);
    if (unlikely(ret))
    {
        CMD_ERROR("%s:%d ioctl call for mem free failed, ret = %d\n",
                  __func__,
                  __LINE__,
                  ret);
    }
}

API_LOCAL
void __qae_finish_free_slab(const int fd, dev_mem_info_t *slab)
{
    if (HUGE_PAGE == slab->type)
    {
        __qae_hugepage_free_slab(slab);
        __qae_hugepage_iommu_unmap(fd, slab);
    }
    else
    {
        ioctl_free_slab(fd, slab);
    }
}

#ifndef ICP_THREAD_SPECIFIC_USDM
/**************************************
 * Memory functions
 *************************************/
static inline int qaeOpenFd(void)
{
/* Check if it is a new process or child. */
#ifdef CACHE_PID
    const int is_new_pid =
        cache_pid == NULL || (cache_pid != NULL && *((pid_t *)cache_pid) == 0);
#else
    const int is_new_pid = check_pid();
#endif

    if (g_fd < 0 || is_new_pid)
    {
        __qae_ResetControl();

        CMD_DEBUG("%s:%d Memory file handle is not initialized. "
                  "Initializing it now \n",
                  __func__,
                  __LINE__);

        if (g_fd > 0)
            close(g_fd);
        g_fd = qae_open(QAE_MEM, O_RDWR);
        if (g_fd < 0)
        {
            CMD_ERROR("%s:%d Unable to initialize memory file handle %s \n",
                      __func__,
                      __LINE__,
                      QAE_MEM);
            return -ENOENT;
        }

#ifdef CACHE_PID
        /* Cache pid */
        if (!cache_pid)
        {
            int page_size = getpagesize();

            cache_pid = qae_mmap(NULL,
                                 page_size,
                                 PROT_READ | PROT_WRITE,
                                 MAP_PRIVATE | MAP_ANON,
                                 -1,
                                 0);
            if (cache_pid == NULL)
            {
                CMD_ERROR("%s:%d Unable to mmap aligned memory \n",
                          __func__,
                          __LINE__);
                close(g_fd);
                return -ENOMEM;
            }

            if (qae_madvise(cache_pid, page_size, MADV_WIPEONFORK))
            {
                CMD_ERROR("%s:%d Unable to update page properties\n",
                          __func__,
                          __LINE__);
                qae_munmap(cache_pid, page_size);
                cache_pid = NULL;
                close(g_fd);
                g_fd = -1;
                return -ENOMEM;
            }
        }

        *((pid_t *)cache_pid) = getpid();
#endif

        if (__qae_init_hugepages(g_fd))
            return -EIO;
    }
    return 0;
}

int __qae_open(void)
{
    return qaeOpenFd();
}


int __qae_free_special(void)
{
    int ret = 0;
    /* Send ioctl to kernel space to remove block for this pid */
    if (g_fd > 0)
    {
        ret = mem_ioctl(g_fd, DEV_MEM_IOC_RELEASE, NULL);
        if (ret)
        {
            CMD_ERROR("%s:%d ioctl call for mem release failed, ret = %d\n",
                      __func__,
                      __LINE__,
                      ret);
        }
        close(g_fd);
        g_fd = -1;
    }

    return ret;
}
#endif

static inline void *mem_protect(void *const addr, const size_t len)
{
    int ret = 0;

    ret = qae_madvise(addr, len, MADV_DONTFORK);
    if (0 != ret)
    {
        munmap(addr, len);
        return NULL;
    }
    return addr;
}

static inline void *mmap_phy_addr(const int fd,
                                  const uint64_t phy_addr,
                                  const size_t len)
{
    void *addr = NULL;

    addr = qae_mmap(NULL,
                    len,
                    PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_LOCKED,
                    fd,
                    phy_addr);

    if (MAP_FAILED == addr)
        return NULL;

    addr = mem_protect(addr, len);

    return addr;
}

#ifndef ICP_THREAD_SPECIFIC_USDM
static inline dev_mem_info_t *ioctl_alloc_slab(const int fd,
                                               const size_t size,
                                               const uint32_t alignment,
                                               const int node,
                                               enum slabType type)
{
    UNUSED(alignment);
    dev_mem_info_t params = { 0 };
    int ret = 0;
    dev_mem_info_t *slab = NULL;

    params.size = size;
    params.nodeId = node;
    params.type = type;

    ret = mem_ioctl(fd, DEV_MEM_IOC_MEMALLOC, &params);
    if (ret)
    {
        CMD_ERROR("%s:%d ioctl call for mem allocation failed, ret = %d\n",
                  __func__,
                  __LINE__,
                  ret);
        return NULL;
    }

    if (node != params.nodeId)
    {
        g_strict_node = 0;
    }

    if (SMALL == type)
        slab = mmap_phy_addr(fd, params.phy_addr, params.size);
    else
        slab = mmap_phy_addr(fd, params.phy_addr, getpagesize());

    if (NULL == slab)
    {
        CMD_ERROR("%s:%d mmap on memory allocated through ioctl failed\n",
                  __func__,
                  __LINE__);
        ret = mem_ioctl(fd, DEV_MEM_IOC_MEMFREE, &params);
        if (unlikely(ret))
        {
            CMD_ERROR("%s:%d ioctl call for mem free failed, ret = %d\n",
                      __func__,
                      __LINE__,
                      ret);
        }
        return NULL;
    }

    if (SMALL == type)
        slab->virt_addr = slab;
    else
    {
        slab->virt_addr = mmap_phy_addr(fd, params.phy_addr, params.size);

        if (NULL == slab->virt_addr)
        {
            CMD_ERROR("%s:%d mmap failed for large memory allocation\n",
                      __func__,
                      __LINE__);
            munmap(slab, getpagesize());
            ret = mem_ioctl(fd, DEV_MEM_IOC_MEMFREE, &params);
            if (unlikely(ret))
            {
                CMD_ERROR("%s:%d ioctl call for mem free failed, ret = %d\n",
                          __func__,
                          __LINE__,
                          ret);
            }
            return NULL;
        }
    }
    return slab;
}

API_LOCAL
dev_mem_info_t *__qae_alloc_slab(const int fd,
                                 const size_t size,
                                 const uint32_t alignment,
                                 const int node,
                                 enum slabType type)
{
    dev_mem_info_t *slab = NULL;

    if (HUGE_PAGE == type)
    {
        slab = __qae_hugepage_alloc_slab(fd, size, node, type);
    }
    else
    {
        slab = ioctl_alloc_slab(fd, size, alignment, node, type);
    }

    /* Store a slab into the hash table for a fast lookup. */
    if (slab)
        add_slab_to_hash(slab);

    return slab;
}
#else
static inline dev_mem_info_t *ioctl_alloc_slab(const int fd,
                                               const size_t size,
                                               const uint32_t alignment,
                                               const int node,
                                               enum slabType type,
                                               qae_mem_info_t *tls_ptr)
{
    UNUSED(alignment);
    dev_mem_info_t params = { 0 };
    int ret = 0;
    dev_mem_info_t *slab = NULL;

    params.size = size;
    params.nodeId = node;
    params.type = type;

    ret = mem_ioctl(fd, DEV_MEM_IOC_MEMALLOC, &params);
    if (ret)
    {
        CMD_ERROR("%s:%d ioctl call for mem allocation failed, ret = %d\n",
                  __func__,
                  __LINE__,
                  ret);
        return NULL;
    }

    if (node != params.nodeId)
    {
        tls_ptr->g_strict_node = 0;
    }

    if (SMALL == type)
        slab = mmap_phy_addr(fd, params.phy_addr, params.size);
    else
        slab = mmap_phy_addr(fd, params.phy_addr, getpagesize());

    if (NULL == slab)
    {
        CMD_ERROR("%s:%d mmap on memory allocated through ioctl failed\n",
                  __func__,
                  __LINE__);
        ret = mem_ioctl(fd, DEV_MEM_IOC_MEMFREE, &params);
        if (unlikely(ret))
        {
            CMD_ERROR("%s:%d ioctl call for mem free failed, ret = %d\n",
                      __func__,
                      __LINE__,
                      ret);
        }
        return NULL;
    }

    if (SMALL == type)
        slab->virt_addr = slab;
    else
    {
        slab->virt_addr = mmap_phy_addr(fd, params.phy_addr, params.size);

        if (NULL == slab->virt_addr)
        {
            CMD_ERROR("%s:%d mmap failed for large memory allocation\n",
                      __func__,
                      __LINE__);
            munmap(slab, getpagesize());
            ret = mem_ioctl(fd, DEV_MEM_IOC_MEMFREE, &params);
            if (unlikely(ret))
            {
                CMD_ERROR("%s:%d ioctl call for mem free failed, ret = %d\n",
                          __func__,
                          __LINE__,
                          ret);
            }
            return NULL;
        }
    }

    return slab;
}

API_LOCAL
dev_mem_info_t *__qae_alloc_slab(const int fd,
                                 const size_t size,
                                 const uint32_t alignment,
                                 const int node,
                                 enum slabType type,
                                 qae_mem_info_t *tls_ptr)
{
    dev_mem_info_t *slab = NULL;

    if (HUGE_PAGE == type)
    {
        slab = __qae_hugepage_alloc_slab(fd, size, node, type);
    }
    else
    {
        slab = ioctl_alloc_slab(fd, size, alignment, node, type, tls_ptr);
    }

    /* Store a slab into the hash table for a fast lookup. */
    if (slab)
        add_slab_to_hash(slab, tls_ptr);

    return slab;
}
#endif
