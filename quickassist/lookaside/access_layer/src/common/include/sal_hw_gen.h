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
 ***************************************************************************
 * @file sal_hw_gen.h
 *
 * @ingroup SalHwGen
 *
 * @description
 *     Functions which return a value corresponding to qat device generation
 *
 ***************************************************************************/

#ifndef SAL_HW_GEN_H
#define SAL_HW_GEN_H

#include "cpa.h"
#include "sal_types_compression.h"
#include "lac_sal_types_crypto.h"

/**
 ***************************************************************************
 * @ingroup SalHwGen
 *
 * @description This function returns whether qat device is gen 4 or not
 *
 * @param[in] pService     pointer to compression service
 *
 ***************************************************************************/

static inline CpaBoolean isDcGen4x(const sal_compression_service_t *pService)
{
    return ((pService->generic_service_info.gen == GEN4) ||
            (pService->generic_service_info.gen == GEN4_2))
               ? CPA_TRUE
               : CPA_FALSE;
}

/**
 ***************************************************************************
 * @ingroup SalHwGen
 *
 * @description This function returns whether qat device is gen 2/3 or not
 *
 * @param[in] pService     pointer to compression service
 *
 ***************************************************************************/

static inline CpaBoolean isDcGen2x(const sal_compression_service_t *pService)
{
    return ((pService->generic_service_info.gen == GEN2) ||
            (pService->generic_service_info.gen == GEN3))
               ? CPA_TRUE
               : CPA_FALSE;
}

/**
 ***************************************************************************
 * @ingroup SalHwGen
 *
 * @description This function returns whether qat device is gen 4 or not
 *
 * @param[in] pService     pointer to crypto service
 *
 ***************************************************************************/

static inline CpaBoolean isCyGen4x(const sal_crypto_service_t *pService)
{
    return ((pService->generic_service_info.gen == GEN4) ||
            (pService->generic_service_info.gen == GEN4_2))
               ? CPA_TRUE
               : CPA_FALSE;
}

/**
 ***************************************************************************
 * @ingroup SalHwGen
 *
 * @description This function returns whether qat device is gen 2/3 or not
 *
 * @param[in] pService     pointer to crypto service
 *
 ***************************************************************************/

static inline CpaBoolean isCyGen2x(const sal_crypto_service_t *pService)
{
    return ((pService->generic_service_info.gen == GEN2) ||
            (pService->generic_service_info.gen == GEN3))
               ? CPA_TRUE
               : CPA_FALSE;
}

#endif /* SAL_HW_GEN_H */
