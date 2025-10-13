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

/**
 *****************************************************************************
 * @file lac_kpt.h
 *
 * @ingroupi LacKpt
 *
 * @description
 *           Key Protection Technology Crypto service common include file.
 *           This is the common include location for KPT crypto service
 *
 *****************************************************************************/
#ifndef __LAC_KPT_CRYPTO_QAT_COMMS_H__
#define __LAC_KPT_CRYPTO_QAT_COMMS_H__

/*
********************************************************************************
* Include public/global header files
********************************************************************************
*/

#include "cpa.h"
#include "cpa_cy_kpt.h"

/* Include LAC files */
#include "lac_mem_pools.h"

/** @ingroup LacKpt
 * KPT unwrap context memory buffer structure
 * ------------------------------------------------
 * |KeyHandle(8B)|           IV(16B)              |
 * ------------------------------------------------
 * The total size is 24 bytes
 */
#define MAX_KPT_IV_LENGTH (16)
#define LAC_KPT_UNWRAP_CTX_SIZE_IN_BYTES                                       \
    (sizeof(CpaCyKptHandle) + MAX_KPT_IV_LENGTH)

/* A key size that is guaranteed to be invalid */
#define LAC_KPT_PKE_INVALID_KEY_SIZE (0)
/* KPT key size mapping table columns */
#define LAC_KPT_PKE_NUM_COLUMNS (2)

/**
 ***************************************************************************
 * @ingroup LacKpt
 *       Build a Kpt Crypto flat buffer which contains WPK unwrap context
 *
 * @description
 *       This function builds a kpt crypto input flat buffer which contains
 *wrapped private key(wpk)'s unwrap context, FW will parse it to get necessary
 *information to unwrapp the wpk.
 *
 * @param[in] pMempool             Pointer to a preallocated memory pool entry.
 * @param[in] pKptUnwrapContext    Pointer to WPK unwrapping context.
 * @retval NULL
 ***************************************************************************/
void LacKpt_BuildUnwrapCtxMemBuffer(Cpa8U *pMempool,
                                    CpaCyKptUnwrapContext *pKptUnwrapContext);

/**
 ***************************************************************************
 * @ingroup LacKpt
 *       Get the Clear Private Key(CPK) size according to WPK size
 *
 * @description
 *       Get the Clear Private Key(CPK) size according to WPK size
 *
 * @param[in] sizeInBytes        WPK size.
 * @param[in] pSizeTable         Key size mapping tabel between WPK and CPK
 * @param[in] numTableEntries    Entry count of key size mapping table
 *
 * @retval CPK size
 ***************************************************************************/
Cpa32U LacKpt_GetCpkSize(Cpa32U sizeInBytes,
                         const Cpa32U pSizeTable[][LAC_KPT_PKE_NUM_COLUMNS],
                         Cpa32U numTableEntries);

/**
 ***************************************************************************
 * @ingroup LacKpt
 *       Allocate a memory pool entry from a spefic memory pool
 *
 * @description
 *       This function  apply for a memory pool entry from a pre-allocated
 * memory pool.
 *
 * @param[in]  poolID        Memory pool id of target memory pool.
 * @param[out] ppMemPool     Pointer to the pointer of allocated memory entry.
 *
 *
 * @retval CPA_STATUS_SUCCESS       Function executed successfully.
 * @retval CPA_STATUS_RESOURCE      Does not have available memory pool entry
 ***************************************************************************/
CpaStatus LacKpt_MemPoolMalloc(Cpa8U **ppMemPool, lac_memory_pool_id_t poolID);

/**
 ***************************************************************************
 * @ingroup LacKpt
 *       Free a memory pool entry
 *
 * @description
 *       This function free a allocated memory pool entry.
 *
 * @param[in] pMemPool     Pointer to allocated memory entry.
 *
 * @retval NULL
 ***************************************************************************/
void LacKpt_MemPoolFree(Cpa8U *pMemPool);
#endif
