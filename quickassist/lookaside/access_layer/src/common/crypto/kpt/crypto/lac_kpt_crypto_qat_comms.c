/***************************************************************************
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
 ***************************************************************************/

/**
 *****************************************************************************
 * @file lac_kpt_crypto_qat_comms.c
 *
 * @ingroup LacKpt
 *
 * This file implements KPT crypto common function
 *
 *****************************************************************************/

/*
********************************************************************************
* Include public/global header files
********************************************************************************
*/
#include "cpa.h"

/* SAL includes */
#include "lac_common.h"
#include "lac_kpt_crypto_qat_comms.h"


/* KPT WPK size array index */
#define LAC_KPT_PKE_WPK_SIZE_COLUMN (0)
/* KPT CPK size array index */
#define LAC_KPT_PKE_CPK_SIZE_COLUMN (1)

/**
 ***************************************************************************
 * @ingroup LacKpt
 *      Build a Kpt unwrap context memory buffer
 ***************************************************************************/
void LacKpt_BuildUnwrapCtxMemBuffer(Cpa8U *pMemPool,
                                    CpaCyKptUnwrapContext *pKptUnwrapContext)
{
    if (!pMemPool)
        return;

    osalMemSet(pMemPool, 0, LAC_KPT_UNWRAP_CTX_SIZE_IN_BYTES);
    /* The first part is kptHandle */
    *((CpaCyKptHandle *)pMemPool) = pKptUnwrapContext->kptHandle;
    /* The rest part is IV */
    memcpy(pMemPool + sizeof(CpaCyKptHandle),
           pKptUnwrapContext->iv,
           CPA_CY_KPT_MAX_IV_LENGTH);
}

/**
 ***************************************************************************
 * @ingroup LacKpt
 *      Get the Clear Private Key(CPK) size according to WPK size
 ***************************************************************************/
Cpa32U LacKpt_GetCpkSize(Cpa32U sizeInBytes,
                         const Cpa32U pSizeTable[][LAC_KPT_PKE_NUM_COLUMNS],
                         Cpa32U numTableEntries)
{
    Cpa32U size = LAC_KPT_PKE_INVALID_KEY_SIZE;
    Cpa32U sizeIndex = 0;

    for (sizeIndex = 0; sizeIndex < numTableEntries; sizeIndex++)
    {
        if (pSizeTable[sizeIndex][LAC_KPT_PKE_WPK_SIZE_COLUMN] == sizeInBytes)
        {
            size = pSizeTable[sizeIndex][LAC_KPT_PKE_CPK_SIZE_COLUMN];
            break;
        }
    }

    return size;
}

/**
 ***************************************************************************
 * @ingroup LacKpt
 *      Allocate a Kpt unwrap ctx memory pool according to pool id
 ***************************************************************************/
CpaStatus LacKpt_MemPoolMalloc(Cpa8U **ppMemPool, lac_memory_pool_id_t poolId)
{
    CpaStatus status = CPA_STATUS_SUCCESS;
    Cpa8U *pMemPool = NULL;

    do
    {
        pMemPool = (Cpa8U *)Lac_MemPoolEntryAlloc(poolId);
        if (NULL == pMemPool)
        {
            LAC_LOG_ERROR("Cannot get kpt unwrap ctx mem pool entry");
            status = CPA_STATUS_RESOURCE;
        }
        else if ((void *)CPA_STATUS_RETRY == pMemPool)
        {
            osalYield();
        }
    } while ((void *)CPA_STATUS_RETRY == pMemPool);
    *ppMemPool = pMemPool;

    return status;
}

/**
 ***************************************************************************
 * @ingroup LacKpt
 *     Free a Kpt unwrap ctx memory pool
 ***************************************************************************/
void LacKpt_MemPoolFree(Cpa8U *pMemPool)
{
    if (pMemPool)
    {
        Lac_MemPoolEntryFree(pMemPool);
    }
}
