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

#define FILE_PATH "test_direct_io.dat"
#define BUF_SIZE (8*1024*1024) // maximum is 1016*1024*1024 when 1G hugepage implemented
#define ALIGN_SIZE (8*1024*1024) // Direct I/O need memory align

inline void check_io_error(int ret, const char* msg) {
    if (ret < 0) {
        std::cerr << msg << " failed: " << strerror(-ret) << std::endl;
        exit(EXIT_FAILURE);
    }
}

#define CMD_ERROR printf

/* The pfn (page frame number) are bits 0-54 of page. */
#define PFN_MASK 0x7fffffffffffffULL
#define PAGEMAP_FILE "/proc/self/pagemap"

/*
 * Use linux system page map file (proc/self/pagemap) to get the physical
 * address. Called in the vfio noiommu mode for virtual to physical address
 * translation.
 */
static int mem_virt2phy(const void *virtaddr, uint64_t *physaddr_ptr)
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

#define SIZE_8M (8*1024*1024)
#define SIZE_1G (1024*1024*1024)

//#define HUGEPAGE_FILE_DIR "/dev/hugepages/qat-usdm.XXXXXX"
#define HUGEPAGE_FILE_DIR "/tmp/qat-usdm.XXXXXX"
#define HUGEPAGE_FILE_LEN (sizeof(HUGEPAGE_FILE_DIR))
static void *hugepage_allocate()
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

typedef struct {
    char matedata[2048];
    char qat_in[16*1024];
    char qat_out[16*1024];
} db_block_s;

#define HUGE_PAGE_NUM 2
char* g_hugepages[HUGE_PAGE_NUM] = {0};

int g_blk_num = 0;
db_block_s* g_blks[HUGE_PAGE_NUM*(SIZE_1G/SIZE_8M)] = {0};

int main() {
    int fd;
    char *write_buf, *read_buf;
    char* write_bufs[250] = {0};
    io_context_t io_ctx;
    struct iocb write_iocb, read_iocb;
    struct io_event events[1];
    int i, j;

    // --- hugepage allocation test ---
    for (i = 0; i<HUGE_PAGE_NUM; i++) {
        g_hugepages[i] = (char*)hugepage_allocate();
        // check virtual addr 8MB alignment
        if (0 == g_hugepages[i] || (unsigned long long)g_hugepages[i] % SIZE_8M) {
            std::cerr << "virtual address is not aligned" << std::endl;
            return EXIT_FAILURE;
        }
        // check physical addr 8MB alignment
        uint64_t physaddr_ptr = 0;
        mem_virt2phy(g_hugepages[i], &physaddr_ptr);
        printf("1G hugepage %d allocated and aligned. virtual: %p, physical: 0x%lX\n", i, g_hugepages[i], physaddr_ptr);
        if (0 == physaddr_ptr || physaddr_ptr % SIZE_8M) {
            std::cerr << "physical address is not aligned" << std::endl;
            return EXIT_FAILURE;
        }
        // split into 8MB blocks
        for (j=0; j<(SIZE_1G/SIZE_8M); j++) {
            g_blks[g_blk_num] = (db_block_s*)(g_hugepages[i]+j*SIZE_8M);
            g_blk_num++;
        }

        // printf("1G hugepage %d allocated and aligned. virtual: %p, physical: 0x%lX\n", i, g_hugepages[i], physaddr_ptr);
    }

    // --- aio test ---
    // allocate memory
    write_buf = (char*)g_blks[1];
    printf("write_buf=%p.\n", write_buf);
    if (!write_buf) {
        std::cerr << "Failed to allocate aligned memory for write" << std::endl;
        return EXIT_FAILURE;
    }

    if ((unsigned long long)write_buf % ALIGN_SIZE) {
        std::cerr << "The address is not 4096 aligned" << std::endl;
        return EXIT_FAILURE;
    }

    if (posix_memalign((void**)&read_buf, ALIGN_SIZE, BUF_SIZE) != 0) {
        std::cerr << "Failed to allocate aligned memory for read" << std::endl;
        return EXIT_FAILURE;
    }

    // fill write buf
    memset(write_buf, 'A', BUF_SIZE);

    // initiate libaio ctx
    memset(&io_ctx, 0, sizeof(io_ctx));
    if (io_setup(1, &io_ctx) < 0) {
        std::cerr << "io_setup failed: " << strerror(errno) << std::endl;
        return EXIT_FAILURE;
    }

    // openfile（using Direct I/O - O_DIRECT）
    fd = open(FILE_PATH, O_RDWR | O_CREAT | O_DIRECT, 0644);
    //fd = open(FILE_PATH, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        std::cerr << "Failed to open file: " << strerror(errno) << std::endl;
        return EXIT_FAILURE;
    }

    // async submit
    iocb *iocbs[1];

    io_prep_pwrite(&write_iocb, fd, write_buf, BUF_SIZE, 0);
    write_iocb.data = write_buf;

    iocbs[0] = &write_iocb;
    if (io_submit(io_ctx, 1, iocbs) < 0) {
        std::cerr << "io_submit (write) failed: " << strerror(errno) << std::endl;
        close(fd);
        return EXIT_FAILURE;
    }

    // wait for complete
    int num_events = io_getevents(io_ctx, 1, 1, events, NULL);
    if (num_events != 1) {
        std::cerr << "io_getevents (write) failed" << std::endl;
        close(fd);
        return EXIT_FAILURE;
    }
    check_io_error(events[0].res, "Async write");

    std::cout << "Successfully wrote " << BUF_SIZE << " bytes to file." << std::endl;

    // submit async read
    io_prep_pread(&read_iocb, fd, read_buf, BUF_SIZE, 0);
    read_iocb.data = read_buf;

    iocbs[0] = &read_iocb;
    if (io_submit(io_ctx, 1, iocbs) < 0) {
        std::cerr << "io_submit (read) failed: " << strerror(errno) << std::endl;
        close(fd);
        return EXIT_FAILURE;
    }

    // wait for complete
    num_events = io_getevents(io_ctx, 1, 1, events, NULL);
    if (num_events != 1) {
        std::cerr << "io_getevents (read) failed" << std::endl;
        close(fd);
        return EXIT_FAILURE;
    }
    check_io_error(events[0].res, "Async read");

    std::cout << "Successfully read " << BUF_SIZE << " bytes from file." << std::endl;

    // check if the read data is the same as written data
    if (memcmp(write_buf, read_buf, BUF_SIZE) == 0) {
        std::cout << "Data verification: PASSED" << std::endl;
    } else {
        std::cerr << "Data verification: FAILED" << std::endl;
    }

    // clean resource
    close(fd);
    io_destroy(io_ctx);
    //free(write_buf);
    // qaeMemFreeNUMA((void**)&write_buf);
    free(read_buf);

    return EXIT_SUCCESS;
}
