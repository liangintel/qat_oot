/******************************************************************************
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
 *  version: QAT20.L.1.2.30-00090
 *
 *****************************************************************************/

/******************************************************************************
 * @file adf_uq.c
 *
 * @description
 * User queue ENQCMD implementation and interfaces on user space
 *****************************************************************************/
#include "cpa.h"
#include "lac_sal_types.h"
#include "icp_platform.h"
#include "icp_accel_devices.h"
#include "adf_dev_ring_ctl.h"
#include "adf_platform_common.h"
#include "icp_adf_uq.h"

STATIC int __adf_uq_enqcmd(void *uq_window, const void *desc_addr)
{
    char ret;

    asm volatile(".byte 0xf2, 0x0f, 0x38, 0xf8, 0x02\t\n"
                 "setz %0\t\n"
                 : "=r"(ret)
                 : "a"(uq_window), "d"(desc_addr));
    return ret;
}
STATIC int adf_uq_enqcmd(void *uq_window, const void *desc_addr)
{
    CpaStatus status = (__adf_uq_enqcmd(uq_window, desc_addr) == 0)
                           ? CPA_STATUS_SUCCESS
                           : CPA_STATUS_RETRY;
#ifdef UQ_DEBUG
    static int uq_output_count = 0;
    static int uq_retry_count = 0;
    if (status == CPA_STATUS_SUCCESS)
    {
        __sync_add_and_fetch(&uq_output_count, 1);
        if (uq_output_count % 500 == 0)
        {
            ADF_ERROR("UQ sent %d ENQCMD.\n", uq_output_count);
        }
    }
    else
    {
        __sync_add_and_fetch(&uq_retry_count, 1);
        if (uq_retry_count % 100 == 0)
        {
            ADF_ERROR("UQ ENQCMD %d retry!\n", uq_retry_count);
        }
    }
#endif
    return status;
}

STATIC void adf_uq_populate_uq_desc(void *src,
                                    void *dst,
                                    uint32_t msg_size,
                                    uint32_t desc_cnt,
                                    struct adf_uq_desc *desc)
{

    uintptr_t src_addr = (uintptr_t)src;
    uintptr_t dst_addr = (uintptr_t)dst;

    desc->desc_type = ADF_UQ_DESC_TYPE_REMOTE;
    desc->lreqaddr = (uint32_t)((src_addr >> ADF_UQ_LH_REQ_ADDR_SHIFT) &
                                ADF_UQ_LH_REQ_ADDR_MASK);
    desc->ureqaddr = (uint32_t)(src_addr >> ADF_UQ_UH_REQ_ADDR_SHIFT);
    desc->lrespaddr = (uint32_t)((dst_addr >> ADF_UQ_LH_REQ_ADDR_SHIFT) &
                                 ADF_UQ_LH_REQ_ADDR_MASK);
    desc->urespaddr = (uint32_t)(dst_addr >> ADF_UQ_UH_REQ_ADDR_SHIFT);
    desc->desc_size = (ADF_MSG_SIZE_64_BYTES == msg_size)
                          ? ADF_UQ_MSG_SIZE_64_BYTES
                          : ADF_UQ_MSG_SIZE_128_BYTES;
    desc->desc_cnt = desc_cnt;
}

STATIC adf_dev_ring_handle_t *adf_get_resp_ring(adf_dev_ring_handle_t *ring)
{
    adf_dev_ring_handle_t *next = ring->bank_data->rings[ring->ring_num + 1];

    if (next && next->pollingMask)
        return next;

    return NULL;
}

CpaStatus adf_uq_put_msg(adf_dev_ring_handle_t *ring)
{
    int status;
    struct adf_uq_desc desc = {0};
    adf_dev_ring_handle_t *resp_ring = adf_get_resp_ring(ring);
    void *src_addr = NULL;
    void *dst_addr = NULL;

    ICP_CHECK_FOR_NULL_PARAM(resp_ring);

    src_addr = (void *)(((UARCH_INT)ring->ring_virt_addr) + ring->tail);
    dst_addr =
        (void *)(((UARCH_INT)resp_ring->ring_virt_addr) + resp_ring->tail);
    adf_uq_populate_uq_desc(src_addr, dst_addr, ring->message_size, 0, &desc);
    status = adf_uq_enqcmd(ring->csr_addr, &desc);
    if (CPA_STATUS_SUCCESS == status)
    {
        resp_ring->tail = modulo((resp_ring->tail + resp_ring->message_size),
                                 resp_ring->modulo);
        ring->tail = modulo((ring->tail + ring->message_size), ring->modulo);
    }
    return status;
}

STATIC CpaStatus adf_uq_push_single_desc(adf_dev_ring_handle_t *ring,
                                         uint32_t nr_req)
{
    CpaStatus status = CPA_STATUS_RETRY;
    void *src_addr = NULL;
    void *dst_addr = NULL;
    struct adf_uq_desc desc = {0};
    adf_dev_ring_handle_t *resp_ring = adf_get_resp_ring(ring);

    ICP_CHECK_FOR_NULL_PARAM(resp_ring);

    src_addr =
        (void *)(((UARCH_INT)ring->ring_virt_addr) + ring->csrTailOffset);
    dst_addr =
        (void *)(((UARCH_INT)resp_ring->ring_virt_addr) + resp_ring->tail);
    adf_uq_populate_uq_desc(
        src_addr, dst_addr, ring->message_size, nr_req - 1, &desc);
    status = adf_uq_enqcmd(ring->csr_addr, &desc);
    if (CPA_STATUS_SUCCESS == status)
    {
        resp_ring->tail =
            modulo((resp_ring->tail + resp_ring->message_size * nr_req),
                   resp_ring->modulo);
        ring->csrTailOffset = modulo(
            (ring->csrTailOffset + ring->message_size * nr_req), ring->modulo);
    }
    return status;
}

STATIC CpaStatus adf_uq_push_batch_desc(adf_dev_ring_handle_t *ring,
                                        uint32_t pending_req_cnt)
{
    CpaStatus status = CPA_STATUS_SUCCESS;
    uint32_t nr_batch = 0;
    uint32_t remaining_req = 0;
    uint32_t i = 0;

    nr_batch = pending_req_cnt >> ADF_UQ_MAX_BATCH_SHIFT;
    /* push batches of req into single uq desc*/
    for (i = 0; i < nr_batch; i++)
    {
        /*
         * In batch mode, we have to wait current batched ENQCMD being pushed to
         * SWQ successfully before next single or batch request sent to SWQ.
         */
        do
        {
            status = adf_uq_push_single_desc(ring, ADF_UQ_MAX_BATCH_NR);
        } while (CPA_STATUS_RETRY == status);
    }

    remaining_req = pending_req_cnt - (nr_batch << ADF_UQ_MAX_BATCH_SHIFT);
    if (remaining_req != 0)
    {
        do
        {
            status = adf_uq_push_single_desc(ring, remaining_req);
        } while (CPA_STATUS_RETRY == status);
    }

    return CPA_STATUS_SUCCESS;
}

CpaStatus adf_uq_push_dp_msg(adf_dev_ring_handle_t *ring)
{
    CpaStatus status = CPA_STATUS_RETRY;
    uint32_t batch_size = 0;
    uint32_t pending_req_cnt = 0;
    CpaBoolean is_single_batch = CPA_FALSE;

    is_single_batch =
        (ring->tail >= ring->csrTailOffset) ? CPA_TRUE : CPA_FALSE;
    batch_size = is_single_batch
                   ? (ring->tail - ring->csrTailOffset)
                   : (ring->tail + ring->ring_size - ring->csrTailOffset);

    /* Single shot case, normally none batch mode hits this branch */
    if (batch_size == ring->message_size)
    {
        status = adf_uq_push_single_desc(ring, 1);
        if (CPA_STATUS_RETRY == status)
        {
            /* In single shot mode, user shall take care of the user queue push
             * failed case, we shall rollback all the pointers to beginning of
             * this single shot operation.
             */
            ring->tail =
                modulo(ring->tail - ring->message_size, ring->modulo);
            *ring->in_flight -= 1;
        }
    }
    else
    {
        /* Batch mode cases */
        if (is_single_batch)
        {
            /*
             * If current number of request are not exceed virtual ring buffer's
             * right boundary, we simply push these requests together.
             */
            pending_req_cnt = batch_size / ring->message_size;
            status = adf_uq_push_batch_desc(ring, pending_req_cnt);
        }
        else
        {
            /*
             * The requests across the ring buffer right boundary, we shall
             * split them to two parts. The first part is previouse tail pointer
             * to the end of ring buffer's right boundary.
             */
            pending_req_cnt =
                (ring->ring_size - ring->csrTailOffset) / ring->message_size;
            /*
             * The other part is the ring buffer left boundary to the current
             * ring tail pointer.
             */
            status = adf_uq_push_batch_desc(ring, pending_req_cnt);
            if (CPA_STATUS_SUCCESS == status)
            {
                pending_req_cnt = ring->tail / ring->message_size;
                status = adf_uq_push_batch_desc(ring, pending_req_cnt);
            }
        }
    }

    return status;
}

