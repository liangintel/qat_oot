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
 * @file sal_misc_error_stats.c
 *
 * @defgroup SalMiscErrorStats   Sal Miscellaneous Error Statistics
 *
 * @ingroup SalMiscErrorStats
 *
 * @description
 *    This file contains implementation of Miscellaneous error statistic related
 *functions
 *
 *****************************************************************************/

#include "cpa.h"
#include "lac_common.h"
#include "lac_mem.h"
#include "icp_adf_cfg.h"
#include "icp_accel_devices.h"
#include "sal_statistics.h"

#include "icp_adf_user_proxy.h"
#include "icp_adf_debug.h"
#include "lac_sal_types.h"
#include "lac_sal.h"


STATIC OsalAtomic *numMiscError;

/* @ingroup SalMiscErrorStats */
CpaStatus Sal_IncMiscErrStats(sal_service_t *pService)
{
    if (!pService)
    {
        LAC_LOG_ERROR("Invalid Parameter.\n");
        return CPA_STATUS_INVALID_PARAM;
    }
    if (numMiscError && CPA_TRUE == pService->stats->bMiscStatsEnabled)
    {
        osalAtomicInc(numMiscError);
    }
    else
    {
        return CPA_STATUS_RESOURCE;
    }

    return CPA_STATUS_SUCCESS;
}

/* @ingroup SalMiscErrorStats */
CpaStatus Sal_GetMiscErrStats(sal_service_t *pService, OsalAtomic *pMiscStats)
{
    if (!pService || !pMiscStats)
    {
        LAC_LOG_ERROR("Invalid Parameter.\n");
        return CPA_STATUS_INVALID_PARAM;
    }
    if (numMiscError && CPA_TRUE == pService->stats->bMiscStatsEnabled)
    {
        *pMiscStats = osalAtomicGet(numMiscError);
    }
    else
    {
        return CPA_STATUS_RESOURCE;
    }

    return CPA_STATUS_SUCCESS;
}

/* @ingroup SalMiscErrorStats */
CpaStatus Sal_InitMiscErrStats(sal_statistics_collection_t *pStats)
{
    CpaStatus status = CPA_STATUS_SUCCESS;

    if (!numMiscError && pStats && CPA_TRUE == pStats->bMiscStatsEnabled)
    {
        icp_adf_mmap_misc_counter((Cpa64U **)&numMiscError);
    }
    return status;
}

/* @ingroup SalMiscErrorStats */
CpaStatus Sal_CleanMiscErrStats(sal_service_t *pService)
{
    CpaStatus status = CPA_STATUS_SUCCESS;

    if (numMiscError && pService && pService->stats &&
        CPA_TRUE == pService->stats->bMiscStatsEnabled)
    {
        icp_adf_unmap_misc_counter((Cpa64U *)numMiscError);
        numMiscError = NULL;
    }
    return status;
}
