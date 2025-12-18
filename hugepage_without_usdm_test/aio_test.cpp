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
#include "hugepage.h"
#include "main.h"

extern "C" {
#include "qae_mem.h"
}

#define FILE_PATH "test_direct_io.dat"
#define BUF_SIZE (8*1024*1024) // maximum is 1016*1024*1024 when 1G hugepage implemented
#define ALIGN_SIZE (8*1024*1024) // Direct I/O need memory align

#define SIZE_8M (8*1024*1024)
#define SIZE_1G (1024*1024*1024)

inline void check_io_error(int ret, const char* msg) {
    if (ret < 0) {
        std::cerr << msg << " failed: " << strerror(-ret) << std::endl;
        exit(EXIT_FAILURE);
    }
}

int g_blk_num = 0;
db_block_s* g_blks[HUGE_PAGE_NUM*(SIZE_1G/SIZE_8M)] = {0};

int aio_test()
{
    int fd;
    char *write_buf, *read_buf;
    char* write_bufs[250] = {0};
    io_context_t io_ctx;
    struct iocb write_iocb, read_iocb;
    struct io_event events[1];
    int i, j;

    // --- hugepage allocation test ---
    for (i = 0; i<HUGE_PAGE_NUM; i++) {
        g_hugepages[i] = (char*)alloc_1g_hugepage();
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
