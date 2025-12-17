
extern "C" {
#include "cpa.h"
#include "cpa_dc_dp.h"
#include "icp_sal_poll.h"
#include "icp_sal_user.h"
#include "qae_mem.h"
}

#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sched.h>
#include <sys/epoll.h>
#include <signal.h>

#define EPOLL_MAX_EVENTS 1024
#define SAMPLE_MAX_BUFF 16384
#define SINGLE_INTER_BUFFLIST 1
#define DC_API_VERSION_AT_LEAST(major, minor)                                  \
    (CPA_DC_API_VERSION_NUM_MAJOR > major ||                                   \
     (CPA_DC_API_VERSION_NUM_MAJOR == major &&                                 \
      CPA_DC_API_VERSION_NUM_MINOR >= minor))

#define PRINT(args...) printf(args)

#define PRINT_ERR(args...)                                                     \
    do                                                                         \
    {                                                                          \
        PRINT("%s(%d): ", __FUNCTION__, __LINE__);                               \
        PRINT(args);                                                           \
    } while (0)

#define PRINT_DBG(args...) PRINT_ERR(args)

#define CNV(x) (x)->compressAndVerify
#define SET_CNV(x, v) (CNV(x) = (v))
#define CNV_RECOVERY(x) (x)->compressAndVerifyAndRecover
#define SET_CNV_RECOVERY(x, v) (CNV_RECOVERY(x) = v)
#define INIT_DC_DP_CNV_OPDATA(x)                                               \
    do                                                                         \
    {                                                                          \
        SET_CNV(x, CPA_FALSE);                                              \
        SET_CNV_RECOVERY(x, CPA_FALSE);                                   \
    } while (0)

static Cpa8U sampleData[16384] = {0};
int efd = -1;
int fd = -1;

/*
 *****************************************************************************
 * Forward declaration
 *****************************************************************************
 */
CpaStatus dcDpSample(void);

/*
 * Callback function
 *
 * This function is "called back" (invoked by the implementation of
 * the API) when the operation has completed.
 *
 */
static void dcDpCallback(CpaDcDpOpData *pOpData)
{
    pOpData->pCallbackTag = (void *)1;
}


CpaStatus PHYS_CONTIG_ALLOC(void **ppMemAddr,
                                           Cpa32U sizeBytes,
                                           Cpa32U alignment = 1)
{
    /* Use perf sample code memory allocator */

    /* In this sample all allocations are done from node=0
     * This might not be optimal in a dual processor system.
     */
    *ppMemAddr = qaeMemAllocNUMA(sizeBytes, 0, alignment);
    if (NULL == *ppMemAddr)
    {
        PRINT_ERR("Memory allocation Failed");
        return CPA_STATUS_RESOURCE;
    }
    return CPA_STATUS_SUCCESS;
}

void PHYS_CONTIG_FREE(void **ppMemAddr) {
    if (NULL != *ppMemAddr)
    {
        qaeMemFreeNUMA(ppMemAddr);
        *ppMemAddr = NULL;
    }
}

/*
CpaPhysicalAddr virtAddrToDevAddr(void *pVirtAddr,
                                  CpaInstanceHandle instanceHandle,
                                  CpaAccelerationServiceType type)
{
    CpaStatus status;
    CpaInstanceInfo2 instanceInfo = { 0 };

    // Get the address translation mode 
    switch (type)
    {
        case CPA_ACC_SVC_TYPE_DATA_COMPRESSION:
            status = cpaDcInstanceGetInfo2(instanceHandle, &instanceInfo);
            break;
        default:
            status = CPA_STATUS_UNSUPPORTED;
    }

    if (CPA_STATUS_SUCCESS != status)
    {
        return (CpaPhysicalAddr)(uintptr_t)NULL;
    }

    if (instanceInfo.requiresPhysicallyContiguousMemory)
    {
        return qaeVirtToPhysNUMA(pVirtAddr);
    }
    else
    {
        return (CpaPhysicalAddr)(uintptr_t)pVirtAddr;
    }
}
*/
CpaPhysicalAddr virtAddrToDevAddr(void *pVirtAddr,
                                  CpaInstanceHandle instanceHandle,
                                  CpaAccelerationServiceType type)
{
    return qaeVirtToPhysNUMA(pVirtAddr);
}
/*
CpaPhysicalAddr virtAddrToDevAddr(void *pVirtAddr,
                                  CpaInstanceHandle instanceHandle,
                                  CpaAccelerationServiceType type)
{
    return (CpaPhysicalAddr)(uintptr_t)pVirtAddr;
}
*/

#define MAX_INSTANCES 1024
/*
 * This function returns a handle to an instance of the data
 * compression API.  It does this by querying the API for all
 * instances and returning the first such instance.
 */
//<snippet name="getInstanceDc">
void sampleDcGetInstance(CpaInstanceHandle *pDcInstHandle)
{
    CpaInstanceHandle dcInstHandles[MAX_INSTANCES];
    Cpa16U numInstances = 0;
    CpaStatus status = CPA_STATUS_SUCCESS;

    *pDcInstHandle = NULL;
    status = cpaDcGetNumInstances(&numInstances);
    if (numInstances >= MAX_INSTANCES)
    {
        numInstances = MAX_INSTANCES;
    }
    if ((status == CPA_STATUS_SUCCESS) && (numInstances > 0))
    {
        status = cpaDcGetInstances(numInstances, dcInstHandles);
        if (status == CPA_STATUS_SUCCESS)
            *pDcInstHandle = dcInstHandles[0];
    }

    if (0 == numInstances)
    {
        PRINT_ERR("No instances found for 'SSL'\n");
        PRINT_ERR("Please check your section names");
        PRINT_ERR(" in the config file.\n");
        PRINT_ERR("Also make sure to use config file version 2.\n");
    }
}

typedef struct {
    char matedata[2048];
    char qat_in1[16*1024];
    char qat_in2[16*1024];
    char qat_out1[16*1024];
    char qat_out2[16*1024];
} db_block_s;

#define HUGE_PAGE_NUM 2
#define SIZE_8M (8*1024*1024)
#define SIZE_1G (1024*1024*1024)
extern int g_blk_num;
extern db_block_s* g_blks[HUGE_PAGE_NUM*(SIZE_1G/SIZE_8M)];

int mem_virt2phy(const void *virtaddr, uint64_t *physaddr_ptr);

/*
 * This function performs a compression operation.
 */
static CpaStatus compPerformOp(CpaInstanceHandle dcInstHandle,
                               CpaDcSessionHandle sessionHdl,
                               CpaDcHuffType huffType)
{
    CpaStatus status = CPA_STATUS_SUCCESS;
    CpaPhysBufferList *pBufferListSrc = NULL;
    CpaPhysBufferList *pBufferListDst = NULL;
    CpaPhysBufferList *pBufferListDst2 = NULL;
    Cpa32U bufferSize = sizeof(sampleData);
    Cpa32U dstBufferSize = bufferSize;
    Cpa32U numBuffers = 0;
    Cpa32U bufferListMemSize = 0;
    // Cpa8U *pSrcBuffer = NULL;
    // Cpa8U *pSrcBuffer2 = NULL;
    // Cpa8U *pDstBuffer = NULL;
    // Cpa8U *pDst2Buffer = NULL;
    CpaDcDpOpData *pOpData = NULL;
    Cpa32U checksum = 0;

    //<snippet name="memAlloc">
    numBuffers = 2;
    /* Size of CpaPhysBufferList and array of CpaPhysFlatBuffers */
    bufferListMemSize =
        sizeof(CpaPhysBufferList) + (numBuffers * sizeof(CpaPhysFlatBuffer));

    /* Allocate 8-byte aligned source buffer List */
    status = PHYS_CONTIG_ALLOC(&pBufferListSrc, bufferListMemSize, 8);
    if (CPA_STATUS_SUCCESS == status)
    {
        /* Allocate first data buffer to hold half the data */
        // status = PHYS_CONTIG_ALLOC(&pSrcBuffer, (sizeof(sampleData)) / 2);
    }
    if (CPA_STATUS_SUCCESS == status)
    {
        /* Allocate second data buffer to hold half the data */
        // status = PHYS_CONTIG_ALLOC(&pSrcBuffer2, (sizeof(sampleData)) / 2);
    }
    if (CPA_STATUS_SUCCESS == status)
    {
        /* copy source into buffer */
        // memcpy(pSrcBuffer, sampleData, sizeof(sampleData) / 2);
        // memcpy(pSrcBuffer2,
        //        &(sampleData[sizeof(sampleData) / 2]),
        //        sizeof(sampleData) / 2);
        memcpy(g_blks[0]->qat_in1, sampleData, sizeof(sampleData) / 2);
        memcpy(g_blks[0]->qat_in2,
               &(sampleData[sizeof(sampleData) / 2]),
               sizeof(sampleData) / 2);

        /* Build source bufferList */
        pBufferListSrc->numBuffers = 2;
        pBufferListSrc->flatBuffers[0].dataLenInBytes = sizeof(sampleData) / 2;
        if(mem_virt2phy(g_blks[0]->qat_in1, &(pBufferListSrc->flatBuffers[0].bufferPhysAddr))) {
            printf("mem_virt2phy qat_in1 failed.\n");
            exit(0);
        }
        // pBufferListSrc->flatBuffers[0].bufferPhysAddr =
            // virtAddrToDevAddr((Cpa64U *)(uintptr_t)pSrcBuffer,
            //                   dcInstHandle,
            //                   CPA_ACC_SVC_TYPE_DATA_COMPRESSION);
        pBufferListSrc->flatBuffers[1].dataLenInBytes = sizeof(sampleData) / 2;
        if(mem_virt2phy(g_blks[0]->qat_in2, &(pBufferListSrc->flatBuffers[1].bufferPhysAddr))) {
            printf("mem_virt2phy qat_in2 failed.\n");
            exit(0);
        }
        // pBufferListSrc->flatBuffers[1].bufferPhysAddr =
            // virtAddrToDevAddr((Cpa64U *)(uintptr_t)pSrcBuffer2,
            //                   dcInstHandle,
            //                   CPA_ACC_SVC_TYPE_DATA_COMPRESSION);
        //</snippet>
    }

    /* Destination buffer size is set as sizeof(sampelData) for a
     * Deflate compression operation with DC_API_VERSION < 2.5.
     * cpaDcDeflateCompressBound API is used to get maximum output buffer size
     * for a Deflate compression operation with DC_API_VERSION >= 2.5 */
#if DC_API_VERSION_AT_LEAST(2, 5)
    status = cpaDcDeflateCompressBound(
        dcInstHandle, huffType, bufferSize, &dstBufferSize);
    if (CPA_STATUS_SUCCESS != status)
    {
        PRINT_ERR("cpaDcDeflateCompressBound API failed. (status = %d)\n",
                  status);
        return CPA_STATUS_FAIL;
    }
#endif

    if (CPA_STATUS_SUCCESS == status)
    {
        /* Allocate destination buffer the same size as source buffer but in
           an SGL with 1 buffer */
        bufferListMemSize = sizeof(CpaPhysBufferList) + dstBufferSize;
        status =
            PHYS_CONTIG_ALLOC(&pBufferListDst, bufferListMemSize, 8);
    }

    if (CPA_STATUS_SUCCESS == status)
    {
        // status = PHYS_CONTIG_ALLOC(&pDstBuffer, bufferSize);
    }
    if (CPA_STATUS_SUCCESS == status)
    {
        /* Build destination bufferList */
        pBufferListDst->numBuffers = 1;
        pBufferListDst->flatBuffers[0].dataLenInBytes = bufferSize;
        if(mem_virt2phy(g_blks[0]->qat_out1, &(pBufferListDst->flatBuffers[0].bufferPhysAddr))) {
            printf("mem_virt2phy qat_out1 failed.\n");
            exit(0);
        }
        // pBufferListDst->flatBuffers[0].bufferPhysAddr =
            // virtAddrToDevAddr((Cpa64U *)(uintptr_t)pDstBuffer,
            //                   dcInstHandle,
            //                   CPA_ACC_SVC_TYPE_DATA_COMPRESSION);

        //<snippet name="opDataDp">
        /* Allocate memory for operational data. Note this needs to be
         * 8-byte aligned, contiguous, resident in DMA-accessible
         * memory.
         */
        status = PHYS_CONTIG_ALLOC(&pOpData, sizeof(CpaDcDpOpData), 8);
    }

    struct timeval now;
    uint64_t start_us;
    uint64_t end_us;
    uint64_t cost;
    

    if (CPA_STATUS_SUCCESS == status)
    {
        memset(pOpData, 0, sizeof(CpaDcDpOpData));
        pOpData->bufferLenToCompress = sizeof(sampleData);
        pOpData->bufferLenForData = dstBufferSize;
        pOpData->dcInstance = dcInstHandle;
        pOpData->pSessionHandle = sessionHdl;
        pOpData->srcBuffer =
            virtAddrToDevAddr((Cpa64U *)(uintptr_t)pBufferListSrc,
                              dcInstHandle,
                              CPA_ACC_SVC_TYPE_DATA_COMPRESSION);
        pOpData->srcBufferLen = CPA_DP_BUFLIST;
        pOpData->destBuffer =
            virtAddrToDevAddr((Cpa64U *)(uintptr_t)pBufferListDst,
                              dcInstHandle,
                              CPA_ACC_SVC_TYPE_DATA_COMPRESSION);
        pOpData->destBufferLen = CPA_DP_BUFLIST;
        pOpData->sessDirection = CPA_DC_DIR_COMPRESS;
        INIT_DC_DP_CNV_OPDATA(pOpData);
        pOpData->thisPhys =
            virtAddrToDevAddr((Cpa64U *)(uintptr_t)pOpData,
                              dcInstHandle,
                              CPA_ACC_SVC_TYPE_DATA_COMPRESSION);
        pOpData->pCallbackTag = (void *)0;
        //</snippet>

        PRINT_DBG("cpaDcDpEnqueueOp\n");

        gettimeofday(&now, NULL);
        start_us = now.tv_sec * 1000000 + now.tv_usec;

        /** Enqueue and submit operation */
        //<snippet name="perform">
        status = cpaDcDpEnqueueOp(pOpData, CPA_TRUE);
        //</snippet>
        if (CPA_STATUS_SUCCESS != status)
        {
            PRINT_ERR("cpaDcDpEnqueueOp failed. (status = %d)\n", status);
	    return CPA_STATUS_FAIL;
        }
    }

    int n;
    //poll for compression
    if (CPA_STATUS_SUCCESS == status)
    {
        struct epoll_event events[EPOLL_MAX_EVENTS];
        n = epoll_wait(efd, events, EPOLL_MAX_EVENTS, -1);
        if (n != 1) {
            PRINT_ERR("epoll_wait failed. (events = %d)\n", n);
            return CPA_STATUS_FAIL;
        }
        int tmp;
        int bytes = read(events[0].data.fd, &tmp, sizeof(int));
        if (bytes <= 0) {
            PRINT_ERR("read failed. (events = %d)\n", n);
            return CPA_STATUS_FAIL;
        }

        status = icp_sal_DcPollDpInstance(dcInstHandle, 0);
        if (CPA_STATUS_SUCCESS != status || (pOpData->pCallbackTag != (void *)1)) {
            PRINT_ERR("icp_sal_DcPollDpInstance failed. (status = %d)\n", status);
            return CPA_STATUS_FAIL;
        }
    }

    gettimeofday(&now, NULL);
    end_us = now.tv_sec * 1000000 + now.tv_usec;
    cost = end_us - start_us;
    printf("compress cost %ld us\n", cost);


    /*
     * We now check the results
     */
    if (CPA_STATUS_SUCCESS == status)
    {
        if (pOpData->responseStatus != CPA_STATUS_SUCCESS)
        {
            PRINT_ERR(
                "status from compression operation failed. (status = %d)\n",
                pOpData->responseStatus);
            status = CPA_STATUS_FAIL;
        }
        else
        {
            if (pOpData->results.status != CPA_DC_OK)
            {
                PRINT_ERR("Results status not as expected (status = %d)\n",
                          pOpData->results.status);
                status = CPA_STATUS_FAIL;
            }
            else
            {
                PRINT_DBG("Data consumed %d\n", pOpData->results.consumed);
                PRINT_DBG("CRC checksum 0x%x\n", pOpData->results.checksum);
                printf("compressed len: %d\n", pOpData->results.produced);
                printf("------\n");
            }
            /* To compare the checksum with decompressed output */
            checksum = pOpData->results.checksum;
        }
    }

    /*
     * We now ensure we can decompress to the original buffer.
     */
    if (CPA_STATUS_SUCCESS == status)
    {
        /* Dst is now the Src buffer - update the length with amount of
           compressed data added to the buffer */
        pBufferListDst->flatBuffers[0].dataLenInBytes =
            pOpData->results.produced;

        /* Allocate memory for new destination bufferList Dst2, we can use
         * stateless decompression here because in this scenario we know
         * that all transmitted data before compress was less than some
         * max size */
        status =
            PHYS_CONTIG_ALLOC(&pBufferListDst2, bufferListMemSize, 8);
        if (CPA_STATUS_SUCCESS == status)
        {
            // status = PHYS_CONTIG_ALLOC(&pDst2Buffer, SAMPLE_MAX_BUFF);
        }

        if (CPA_STATUS_SUCCESS == status)
        {
            gettimeofday(&now, NULL);
            start_us = now.tv_sec * 1000000 + now.tv_usec;

            /* Build destination 2 bufferList */
            pBufferListDst2->numBuffers = 1;
            pBufferListDst2->flatBuffers[0].dataLenInBytes = SAMPLE_MAX_BUFF;
            if(mem_virt2phy(g_blks[0]->qat_out2, &(pBufferListDst2->flatBuffers[0].bufferPhysAddr))) {
                printf("mem_virt2phy qat_out2 failed.\n");
                exit(0);
            }
            // pBufferListDst2->flatBuffers[0].bufferPhysAddr =
            //     virtAddrToDevAddr((Cpa64U *)(uintptr_t)pDst2Buffer,
            //                       dcInstHandle,
            //                       CPA_ACC_SVC_TYPE_DATA_COMPRESSION);

            /** Can reuse prev OpData
             */
            pOpData->bufferLenToCompress = pOpData->results.produced;
            pOpData->bufferLenForData = SAMPLE_MAX_BUFF;
            pOpData->dcInstance = dcInstHandle;
            pOpData->pSessionHandle = sessionHdl;
            pOpData->srcBuffer =
                virtAddrToDevAddr((Cpa64U *)(uintptr_t)pBufferListDst,
                                  dcInstHandle,
                                  CPA_ACC_SVC_TYPE_DATA_COMPRESSION);
            pOpData->srcBufferLen = CPA_DP_BUFLIST;

            pOpData->destBuffer = virtAddrToDevAddr(
                (Cpa64U *)(uintptr_t)pBufferListDst2,
                dcInstHandle,
                CPA_ACC_SVC_TYPE_DATA_COMPRESSION);
            pOpData->destBufferLen = CPA_DP_BUFLIST;
            pOpData->sessDirection = CPA_DC_DIR_DECOMPRESS;


            INIT_DC_DP_CNV_OPDATA(pOpData);
            pOpData->thisPhys =
                virtAddrToDevAddr((Cpa64U *)(uintptr_t)pOpData,
                                  dcInstHandle,
                                  CPA_ACC_SVC_TYPE_DATA_COMPRESSION);
            pOpData->pCallbackTag = (void *)0;

            gettimeofday(&now, NULL);
            end_us = now.tv_sec * 1000000 + now.tv_usec;
            cost = end_us - start_us;
            printf("prepare cost %ld us\n", cost);

            PRINT_DBG("cpaDcDpEnqueueOpBatch\n");

            gettimeofday(&now, NULL);
            start_us = now.tv_sec * 1000000 + now.tv_usec;

            /** Enqueue symmetric operation */
            status = cpaDcDpEnqueueOpBatch(1, &pOpData, CPA_TRUE);

            if (CPA_STATUS_SUCCESS != status)
            {
                PRINT_ERR(
                    "cpaDcDpEnqueueOpBatch Decomp failed. (status = %d)\n",
                    status);
            }
            gettimeofday(&now, NULL);
            end_us = now.tv_sec * 1000000 + now.tv_usec;
            cost = end_us - start_us;
            printf("enqueue cost %ld us\n", cost);
        }

        if (CPA_STATUS_SUCCESS == status)   //poll for decompression
        {
            struct epoll_event events[EPOLL_MAX_EVENTS];
	        int n = epoll_wait(efd, events, EPOLL_MAX_EVENTS, -1);
            if (n != 1) {
                PRINT_ERR("epoll_wait failed. (events = %d)\n", n);
                return CPA_STATUS_FAIL;
            }
            int tmp;
            int bytes = read(events[0].data.fd, &tmp, sizeof(int));
            if (bytes <= 0) {
                PRINT_ERR("read failed. (events = %d)\n", n);
                return CPA_STATUS_FAIL;
            }

            /* Poll for responses.
             * Polling functions are implementation specific */
            status = icp_sal_DcPollDpInstance(dcInstHandle, 0);
            if (CPA_STATUS_SUCCESS != status || (pOpData->pCallbackTag != (void *)1)) {
                PRINT_ERR("icp_sal_DcPollDpInstance failed. (status = %d)\n", status);
                return CPA_STATUS_FAIL;
            }
        }

        gettimeofday(&now, NULL);
        end_us = now.tv_sec * 1000000 + now.tv_usec;
        cost = end_us - start_us;
        printf("decompress cost %ld us\n", cost);
        printf("------\n");


        /*
         * We now check the results
         */
        if (CPA_STATUS_SUCCESS == status)
        {
            if (pOpData->responseStatus != CPA_STATUS_SUCCESS)
            {
                PRINT_ERR("status from decompression operation failed. (status "
                          "= %d)\n",
                          pOpData->responseStatus);

                status = CPA_STATUS_FAIL;
            }
            else
            {
                if (pOpData->results.status != CPA_DC_OK)
                {
                    PRINT_ERR("Results status not as expected (status = %d)\n",
                              pOpData->results.status);
                    status = CPA_STATUS_FAIL;
                }
                else
                {
                    PRINT_DBG("Data consumed %d\n", pOpData->results.consumed);
                    PRINT_DBG("Data produced %d\n", pOpData->results.produced);
                    PRINT_DBG("CRC checksum 0x%x\n", pOpData->results.checksum);

                    /* Compare with original data */
                    if (0 ==
                        memcmp(g_blks[0]->qat_out2, sampleData, sizeof(sampleData)))
                    {
                        PRINT_DBG("Output matches expected output\n");
                    }
                    else
                    {
                        PRINT_ERR("Output does not match expected output\n");
                        status = CPA_STATUS_FAIL;
                    }
                    if (checksum == pOpData->results.checksum)
                    {
                        PRINT_DBG("successfully. Checksums match after compression and "
                                  "decompression\n");
                    }
                    else
                    {
                        PRINT_ERR("Checksums does not match after compression "
                                  "and decompression\n");
                        status = CPA_STATUS_FAIL;
                    }
                }
            }
        }
    }

    /*
     * Free the memory!
     */
    PHYS_CONTIG_FREE(&pOpData);
    // PHYS_CONTIG_FREE(&pSrcBuffer);
    // PHYS_CONTIG_FREE(&pSrcBuffer2);
    PHYS_CONTIG_FREE(&pBufferListSrc);
    // PHYS_CONTIG_FREE(&pDstBuffer);
    PHYS_CONTIG_FREE(&pBufferListDst);
    // PHYS_CONTIG_FREE(&pDst2Buffer);
    PHYS_CONTIG_FREE(&pBufferListDst2);

    return status;
}


void load_from_file(const char* path, unsigned char* data, int len)
{
    FILE* stream = fopen(path, "r");
    if (stream == NULL) {
	abort();
    }
    int ret = fread(data, 1, len, stream);
    if (ret != len)
    {
        fprintf(stderr, "load_from_file error, ret:%d", ret);
	abort();
    }

    fclose(stream);
}

/*
 * This is the main entry point for the sample data compression code.
 * demonstrates the sequence of calls to be made to the API in order
 * to create a session, perform one or more stateless compression operations,
 * and then tear down the session.
 */
CpaStatus dcDpSample(void)
{
    CpaStatus status = CPA_STATUS_SUCCESS;
    CpaDcInstanceCapabilities cap = {0};
    Cpa32U sess_size = 0;
    Cpa32U ctx_size = 0;
    CpaDcSessionHandle sessionHdl = NULL;
    CpaInstanceHandle dcInstHandle = NULL;
    CpaDcSessionSetupData sd = {0};
    /* Variables required to setup the intermediate buffer */
    CpaBufferList **bufferInterArray = NULL;
    Cpa16U numInterBuffLists = 0;
    Cpa16U bufferNum = 0;
    Cpa32U buffMetaSize = 0;

    load_from_file("test_page", sampleData, 16384);

    /*
     * In this simplified version of instance discovery, we discover
     * exactly one instance of a data compression service.
     * Note this is the same as was done for "traditional" api.
     */
    sampleDcGetInstance(&dcInstHandle);
    if (dcInstHandle == NULL)
    {
        return CPA_STATUS_FAIL;
    }
    status = icp_sal_DcGetFileDescriptor(dcInstHandle, &fd);
    if (status != CPA_STATUS_SUCCESS) {
        printf("get fd faild: %d \n", status);
        return CPA_STATUS_FAIL;
    }

    //efd = epoll_create(1024);
    efd = epoll_create1(EPOLL_CLOEXEC);
    if (efd == -1) {
        printf("epoll_create faild \n");
        return CPA_STATUS_FAIL;
    }
    struct epoll_event event;
    event.data.fd = fd;
    //event.events = EPOLLIN | EPOLLET;
    event.events = EPOLLIN;
    if (-1 == epoll_ctl(efd, EPOLL_CTL_ADD, fd, &event)) {
        printf("epoll_ctl faild \n");
        return CPA_STATUS_FAIL;
    }

    /* Query Capabilities */
    PRINT_DBG("cpaDcQueryCapabilities\n");
    status = cpaDcQueryCapabilities(dcInstHandle, &cap);
    if (status != CPA_STATUS_SUCCESS)
    {
        return status;
    }

    if (!cap.statelessDeflateCompression ||
        !cap.statelessDeflateDecompression || !cap.checksumCRC32 ||
        !cap.dynamicHuffman)
    {
        PRINT_ERR("Error: Unsupported functionality\n");
        return CPA_STATUS_FAIL;
    }

    if (cap.dynamicHuffmanBufferReq)
    {

        status = cpaDcBufferListGetMetaSize(dcInstHandle, 1, &buffMetaSize);

        if (CPA_STATUS_SUCCESS == status)
        {
            status = cpaDcGetNumIntermediateBuffers(dcInstHandle,
                                                    &numInterBuffLists);
        }
        if (CPA_STATUS_SUCCESS == status && 0 != numInterBuffLists)
        {
            status = PHYS_CONTIG_ALLOC(
                &bufferInterArray, numInterBuffLists * sizeof(CpaBufferList *));
        }
        for (bufferNum = 0; bufferNum < numInterBuffLists; bufferNum++)
        {
            if (CPA_STATUS_SUCCESS == status)
            {
                status = PHYS_CONTIG_ALLOC(&bufferInterArray[bufferNum],
                                           sizeof(CpaBufferList));
            }
            if (CPA_STATUS_SUCCESS == status)
            {
                status = PHYS_CONTIG_ALLOC(
                    &bufferInterArray[bufferNum]->pPrivateMetaData,
                    buffMetaSize);
            }

            if (CPA_STATUS_SUCCESS == status)
            {
                status =
                    PHYS_CONTIG_ALLOC(&bufferInterArray[bufferNum]->pBuffers,
                                      sizeof(CpaFlatBuffer));
            }

            if (CPA_STATUS_SUCCESS == status)
            {
                /* Implementation requires an intermediate buffer approximately
                           twice the size of the output buffer */
                status = PHYS_CONTIG_ALLOC(
                    &bufferInterArray[bufferNum]->pBuffers->pData,
                    2 * SAMPLE_MAX_BUFF);
                bufferInterArray[bufferNum]->numBuffers = 1;
                bufferInterArray[bufferNum]->pBuffers->dataLenInBytes =
                    2 * SAMPLE_MAX_BUFF;
            }

        } /* End numInterBuffLists */
    }

    /*
     * Set the address translation function for the instance
     */
    status = cpaDcSetAddressTranslation(dcInstHandle, qaeVirtToPhysNUMA);

    /* Start DataCompression component */
    PRINT_DBG("cpaDcStartInstance\n");
    status =
        cpaDcStartInstance(dcInstHandle, numInterBuffLists, bufferInterArray);

    if (CPA_STATUS_SUCCESS == status)
    {
        /* Register callback function for the instance */
        //<snippet name="regCb">
        status = cpaDcDpRegCbFunc(dcInstHandle, dcDpCallback);
        //</snippet>
    }

    /*
     * We now populate the fields of the session operational data and create
     * the session.  Note that the size required to store a session is
     * implementation-dependent, so we query the API first to determine how
     * much memory to allocate, and then allocate that memory.
     */
    //<snippet name="initSession">
    if (CPA_STATUS_SUCCESS == status)
    {
        sd.compLevel = CPA_DC_L12;
        sd.compType = CPA_DC_DEFLATE;
        sd.huffType = CPA_DC_HT_FULL_DYNAMIC;
        /* If the implementation supports it, the session will be configured
         * to select static Huffman encoding over dynamic Huffman as
         * the static encoding will provide better compressibility.
         */
        if (cap.autoSelectBestHuffmanTree)
        {
#if DC_API_VERSION_AT_LEAST(3, 1)
            sd.autoSelectBestHuffmanTree = CPA_DC_ASB_ENABLED;
#else
            sd.autoSelectBestHuffmanTree = CPA_DC_ASB_STATIC_DYNAMIC;
#endif
        }
        else
        {
            sd.autoSelectBestHuffmanTree = CPA_DC_ASB_DISABLED;
        }
        sd.sessDirection = CPA_DC_DIR_COMBINED;
        sd.sessState = CPA_DC_STATELESS;
#if (CPA_DC_API_VERSION_NUM_MAJOR == 1 && CPA_DC_API_VERSION_NUM_MINOR < 6)
        sd.deflateWindowSize = 7;
#endif
        sd.checksum = CPA_DC_CRC32;

        /* Determine size of session context to allocate */
        PRINT_DBG("cpaDcGetSessionSize\n");
        status = cpaDcGetSessionSize(dcInstHandle, &sd, &sess_size, &ctx_size);
    }

    if (CPA_STATUS_SUCCESS == status)
    {
        /* Allocate session memory */
        status = PHYS_CONTIG_ALLOC(&sessionHdl, sess_size);
    }

    /* Initialize the Stateless session */
    if (CPA_STATUS_SUCCESS == status)
    {
        PRINT_DBG("cpaDcDpInitSession\n");
        status = cpaDcDpInitSession(dcInstHandle,
                                    sessionHdl, /* session memory */
                                    &sd);       /* session setup data */
    }
    //</snippet>

    if (CPA_STATUS_SUCCESS == status)
    {
        CpaStatus sessionStatus = CPA_STATUS_SUCCESS;
        /* Perform Compression operation */
	int sleep_time = 0;
	for (int i = 0; i < 5; i++) {
        printf("====== loop %dth\n", i);
        status = compPerformOp(dcInstHandle, sessionHdl, sd.huffType);
	    sleep_time = 1000000;
	    printf("sleep %d us ...\n", sleep_time);
	    usleep(sleep_time);
	}

        PRINT_DBG("cpaDcDpRemoveSession\n");
        //<snippet name="removeSession">
        sessionStatus = cpaDcDpRemoveSession(dcInstHandle, sessionHdl);
        //</snippet>

        /* Maintain status of remove session only when status of all operations
         * before it are successful. */
        if (CPA_STATUS_SUCCESS == status)
        {
            status = sessionStatus;
        }
    }

    /*
     * Free up memory, stop the instance, etc.
     */

    /* Free session Context */
    PHYS_CONTIG_FREE(&sessionHdl);

    PRINT_DBG("cpaDcStopInstance\n");
    cpaDcStopInstance(dcInstHandle);

    /* Free intermediate buffers */
    if (bufferInterArray != NULL)
    {
        for (bufferNum = 0; bufferNum < numInterBuffLists; bufferNum++)
        {
            PHYS_CONTIG_FREE(&bufferInterArray[bufferNum]->pBuffers->pData);
            PHYS_CONTIG_FREE(&bufferInterArray[bufferNum]->pBuffers);
            PHYS_CONTIG_FREE(&bufferInterArray[bufferNum]->pPrivateMetaData);
        }
        PHYS_CONTIG_FREE(&bufferInterArray);
    }

    if (CPA_STATUS_SUCCESS == status)
    {
        PRINT_DBG("Sample code ran successfully\n");
    }
    else
    {
        PRINT_DBG("Sample code failed with status of %d\n", status);
    }

    return status;
}

int qat_test()
{
    CpaStatus stat = CPA_STATUS_SUCCESS;


    PRINT_DBG("Starting Data Plane Compression Sample Code App ...\n");

    stat = icp_sal_userStart("SSL");
    if (CPA_STATUS_SUCCESS != stat)
    {
        PRINT_ERR("Failed to start user process SSL\n");
        return (int)stat;
    }
//exit(1);
//raise(SIGQUIT);

    stat = dcDpSample();
    if (CPA_STATUS_SUCCESS != stat)
    {
        PRINT_ERR("\nData Plane Compression Sample Code App failed\n");
    }
    else
    {
        PRINT_DBG("\nData Plane Compression Sample Code App finished\n");
    }

    icp_sal_userStop();


    return (int)stat;
}
