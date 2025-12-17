#include <iostream>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <libaio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <linux/mman.h>

extern "C" {
#include "qae_mem.h"
}

// #define FILE_PATH "test_direct_io.dat"
// #define BUF_SIZE (8*1024*1024) // maximum is 1016*1024*1024 when 1G hugepage implemented
// #define ALIGN_SIZE (8*1024*1024) // Direct I/O need memory align

#define SIZE_8M (8*1024*1024)
#define SIZE_1G (1024*1024*1024)

#define CMD_ERROR printf

/* The pfn (page frame number) are bits 0-54 of page. */
#define PFN_MASK 0x7fffffffffffffULL
#define PAGEMAP_FILE "/proc/self/pagemap"

/*
 * Use linux system page map file (proc/self/pagemap) to get the physical
 * address. Called in the vfio noiommu mode for virtual to physical address
 * translation.
 */
int mem_virt2phy(const void *virtaddr, uint64_t *physaddr_ptr)
{
    int fd, retval;
    uint64_t page;
    unsigned long virt_pfn;
    int page_size;
    off_t offset;

    *physaddr_ptr = 0;

    /* standard page size */
    page_size = getpagesize();

    fd = open(PAGEMAP_FILE, O_RDONLY);
    if (fd < 0)
    {
        CMD_ERROR("%s(): could not open %s: %s\n",
                  __func__,
                  PAGEMAP_FILE,
                  strerror(errno));
        return -EPERM;
    }

    virt_pfn = (unsigned long)virtaddr / page_size;
    offset = sizeof(uint64_t) * virt_pfn;
    if (lseek(fd, offset, SEEK_SET) == (off_t) -1)
    {
        CMD_ERROR(
            "%s(): seek failure in %s: %d\n", __func__, PAGEMAP_FILE, errno);
        close(fd);
        return -EINVAL;
    }

    retval = read(fd, &page, sizeof(page));
    if (retval < 0)
    {
        CMD_ERROR(
            "%s(): could not read %s: %d\n", __func__, PAGEMAP_FILE, errno);
        return retval;
    }
    else if (retval != sizeof(page))
    {
       CMD_ERROR("%s(): read %d bytes from %s "
                "but expected %zu:\n",
                __func__, retval, PAGEMAP_FILE, sizeof(page));
       return -EINVAL;
    }

    if (close(fd))
    {
        CMD_ERROR("%s(): closing %s failed: %s\n",
                  __func__, PAGEMAP_FILE, strerror(errno));
    }

    if ((page & PFN_MASK) == 0) {
        CMD_ERROR("%s(): PAGEMAP_FILE:%s failed: page=0x%lX, (page & PFN_MASK)=0x%llX\n",
                  __func__, PAGEMAP_FILE, page, (page & PFN_MASK));
        return -EINVAL;
    }

    *physaddr_ptr = ((page & PFN_MASK) * page_size)
                        + ((unsigned long)virtaddr % page_size);

    return 0;
}

//#define HUGEPAGE_FILE_DIR "/dev/hugepages/qat-usdm.XXXXXX"
#define HUGEPAGE_FILE_DIR "/tmp/qat-usdm.XXXXXX"
#define HUGEPAGE_FILE_LEN (sizeof(HUGEPAGE_FILE_DIR))
void *hugepage_allocate()
{
    void *addr = NULL;
    int ret = 0;
    int hpg_fd;
    char hpg_fname[HUGEPAGE_FILE_LEN];

    /*
     * for every mapped huge page there will be a separate file descriptor
     * created from a temporary file, we should NOT close fd explicitly, it
     * will be reclaimed by the OS when the process gets terminated, and
     * meanwhile the huge page binding to the fd will be released, this could
     * guarantee the memory cleanup order between user buffers and ETR.
     */
    snprintf(hpg_fname, sizeof(HUGEPAGE_FILE_DIR), "%s", HUGEPAGE_FILE_DIR);
    hpg_fd = mkstemp(hpg_fname);

    if (hpg_fd < 0)
    {
        CMD_ERROR("%s:%d mkstemp(%s) for hpg_fd failed\n",
                  __func__,
                  __LINE__,
                  hpg_fname);
        return NULL;
    }

    unlink(hpg_fname);

    addr = mmap(NULL,
                    SIZE_1G,
                    PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_HUGETLB | MAP_HUGE_1GB,
                    hpg_fd,
                    0);

    if (MAP_FAILED == addr)
    {
        CMD_ERROR("%s:%d mmap(%s) for hpg_fd failed\n",
                  __func__,
                  __LINE__,
                  hpg_fname);
        close(hpg_fd);
        return NULL;
    }

    ret = madvise(addr, SIZE_1G, MADV_DONTFORK);
    if (0 != ret)
    {
        munmap(addr, SIZE_1G);
        CMD_ERROR("%s:%d madvise(%s) for hpg_fd failed\n",
                  __func__,
                  __LINE__,
                  hpg_fname);
        close(hpg_fd);
        return NULL;
    }

    //((dev_mem_info_t *)addr)->hpg_fd = hpg_fd;

    return addr;
}
