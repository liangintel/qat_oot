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
 *
 * @file lac_kpt_ecdsa.c
 *
 * @ingroup Lac_KptEc
 *
 * Elliptic Curve Digital Signature Algorithm with protected private key
 * functions
 *
 * @lld_start
 *
 * @lld_overview
 * This file implements KPT Elliptic Curve DSA api. It implements KPT Ecdsa
 * signature generation (rs). Statistics are maintained per instance.
 * The parameters supplied by the client are checked, and then input/output
 * argument lists are constructed before calling the PKE Comms layer to
 * create and send a request to the QAT.
 *
 * @lld_dependencies
 * - \ref LacAsymCommonQatComms "PKE QAT Comms" : For creating and sending
 * messages to the QAT
 * - \ref LacMem "Mem" : For memory allocation and freeing, and translating
 * between scalar and pointer types
 * - OSAL : For atomics and logging
 *
 * @note
 * The KPT ECDSA feature may be called in Asynchronous or Synchronous modes.
 * In Asynchronous mode the user supplies a Callback function to the API.
 * Control returns to the client after the message has been sent to the QAT
 * and the Callback gets invoked when the QAT completes the operation. There
 * is NO BLOCKING. This mode is preferred for maximum performance.
 * In Synchronous mode the client supplies no Callback function pointer (NULL)
 * and the point of execution is placed on a wait-queue internally, and this
 * is de-queued once the QAT completes the operation. Hence, Synchronous mode
 * is BLOCKING. So avoid using in an interrupt context. To achieve maximum
 * performance from the API Asynchronous mode is preferred.
 *
 * @performance
 *
 * @lld_initialisation
 * On initialization this component clears the stats.
 *
 * @lld_module_algorithms
 *
 * @lld_process_context
 *
 * @lld_end
 *
 ***************************************************************************/

/* API Includes */
#include "cpa.h"
#include "cpa_cy_ecdsa.h"
#include "cpa_cy_kpt.h"

/* OSAL Includes */
#include "Osal.h"

/* ADF Includes */
#include "icp_adf_init.h"
#include "icp_adf_transport.h"
#include "icp_accel_devices.h"
#include "icp_adf_debug.h"

/* QAT includes */
#include "icp_qat_fw_la.h"
#include "icp_qat_fw_mmp.h"
#include "icp_qat_fw_mmp_ids.h"
#include "icp_qat_fw_pke.h"

/* Look Aside Includes */
#include "lac_log.h"
#include "lac_common.h"
#include "lac_mem.h"
#include "lac_mem_pools.h"
#include "lac_pke_utils.h"
#include "lac_pke_qat_comms.h"
#include "lac_sync.h"
#include "lac_ec.h"
#include "lac_sym.h"
#include "lac_list.h"
#include "sal_service_state.h"
#include "lac_sal_types_crypto.h"
#include "sal_statistics.h"
#include "lac_ec_nist_curves.h"
#include "lac_kpt_crypto_qat_comms.h"


/*
*******************************************************************************
* Intel KPT ECDSA Wrapped Private Key(WPK) has a fix format as:
* (d)'||AuthTag.
* The size of WPK is:  sizeof(d) + sizeof(AuthTag).
* E.g. Size of PKE_KPT_ECDSA_SIGN_RS_P256 WPK is: 32 + 16 = 48
*******************************************************************************
*/
#define KPT_ECDSA_SIGN_RS_P256 (48)
#define KPT_ECDSA_SIGN_RS_P384 (64)
#define KPT_ECDSA_SIGN_RS_P576 (88)

#define KPT_ECDSA_CURVE_NUM (3)

static const Cpa8U p256_aad[] = { 0x06, 0x08, 0x2A, 0x86, 0x48,
                                  0xCE, 0x3D, 0x03, 0x01, 0x07 };

static const Cpa8U p384_aad[] = { 0x06, 0x05, 0x2B, 0x81, 0x04, 0x00, 0x22 };

static const Cpa8U p521_aad[] = { 0x06, 0x05, 0x2B, 0x81, 0x04, 0x00, 0x23 };

/*
********************************************************************************
* Number of 'out' arguments in the arguments size list for KPT ECDSA Sign RS
********************************************************************************
*/
#define LAC_KPT_ECDSA_SIGNRS_NUM_OUT_ARGS (2)

struct KptEcdsaAadIdMap
{
    Cpa32U aadLen;
    Cpa32U functionID;
    const Cpa8U *aad;
};

STATIC struct KptEcdsaAadIdMap lacKptEcdsaAadIdMap[KPT_ECDSA_CURVE_NUM] = {
    { sizeof(p256_aad), PKE_KPT_ECDSA_SIGN_RS_P256, p256_aad },
    { sizeof(p384_aad), PKE_KPT_ECDSA_SIGN_RS_P384, p384_aad },
    { sizeof(p521_aad), PKE_KPT_ECDSA_SIGN_RS_P521, p521_aad }
};

#ifndef DISABLE_STATS
#define LAC_KPT_ECDSA_STAT_INC(statistic, pCryptoService)                      \
    do                                                                         \
    {                                                                          \
        if (CPA_TRUE ==                                                        \
            pCryptoService->generic_service_info.stats->bEccStatsEnabled)      \
        {                                                                      \
            osalAtomicInc(                                                     \
                &pCryptoService->pLacEcdsaStatsArr[offsetof(CpaCyEcdsaStats64, \
                                                            statistic) /       \
                                                   sizeof(Cpa64U)]);           \
        }                                                                      \
    } while (0)
/**< @ingroup Lac_KptEc
 * Macro to increment a KPT ECDSA stat (derives offset into array of atomics) */
#else
#define LAC_KPT_ECDSA_STAT_INC(statistic, pCryptoService)                      \
    (pCryptoService) = (pCryptoService)
#endif

/**
 ***************************************************************************
 * @ingroup Lac_KptEc
 *       Get the size of KPT ECDSA private key(d).
 *
 ***************************************************************************/
STATIC Cpa32U LacKptEcdsa_GetKeyLength(const CpaFlatBuffer *pBuffer)
{
    Cpa32U keyLengthInBytes = 0;

    switch (pBuffer->dataLenInBytes)
    {
        case KPT_ECDSA_SIGN_RS_P256:
            keyLengthInBytes = LAC_EC_SIZE_QW4_IN_BYTES;
            break;

        case KPT_ECDSA_SIGN_RS_P384:
            keyLengthInBytes = LAC_EC_SIZE_QW6_IN_BYTES;
            break;

        case KPT_ECDSA_SIGN_RS_P576:
            keyLengthInBytes = LAC_EC_SIZE_QW9_IN_BYTES;
            break;

        default:
            LAC_LOG_ERROR("Invalid Wrapped Private Key Length.");
    }

    return keyLengthInBytes;
}

/**
 *********************************************************************************
 * @ingroup LacKptEc
 * Get optimised MMP function id according to curve additional authenticated
 *data
 *
 *********************************************************************************/
STATIC Cpa32U LacKptEcdsa_GetMmpId(CpaCyKptUnwrapContext *pKptUnwrapContext)
{
    Cpa32U functionID = 0;
    Cpa32U i;

    for (i = 0; i < KPT_ECDSA_CURVE_NUM; i++)
    {
        if (!memcmp(lacKptEcdsaAadIdMap[i].aad,
                    pKptUnwrapContext->additionalAuthData,
                    lacKptEcdsaAadIdMap[i].aadLen))
            functionID = lacKptEcdsaAadIdMap[i].functionID;
    }

    return functionID;
}

/**
 ***************************************************************************
 * @ingroup Lac_KptEc
 *      KPT ECDSA Sign R & S internal callback
 *
 ***************************************************************************/
STATIC
void LacKptEcdsa_SignRSCallback(CpaStatus status,
                                CpaBoolean signStatus,
                                CpaInstanceHandle instanceHandle,
                                lac_pke_op_cb_data_t *pCbData)
{
    CpaCyEcdsaSignRSCbFunc pCb = NULL;
    void *pCallbackTag = NULL;
    CpaCyEcdsaSignRSOpData *pOpData = NULL;
    CpaFlatBuffer *pR = NULL;
    CpaFlatBuffer *pS = NULL;
    Cpa8U *pMemPool = NULL;
    sal_crypto_service_t *pCryptoService =
        (sal_crypto_service_t *)instanceHandle;

    /* Extract info from callback data structure */
    LAC_ASSERT_NOT_NULL(pCbData);
    pCb = (CpaCyEcdsaSignRSCbFunc)pCbData->pClientCb;
    pCallbackTag = pCbData->pCallbackTag;
    pOpData = (CpaCyEcdsaSignRSOpData *)pCbData->pClientOpData;
    pR = pCbData->pOutputData1;
    pS = pCbData->pOutputData2;
    pMemPool = (Cpa8U *)(pCbData->pOpaqueData);

    LAC_ASSERT_NOT_NULL(pCb);
    LAC_ASSERT_NOT_NULL(pOpData);
    LAC_ASSERT_NOT_NULL(pR);
    LAC_ASSERT_NOT_NULL(pR->pData);
    LAC_ASSERT_NOT_NULL(pS);
    LAC_ASSERT_NOT_NULL(pS->pData);

    /* Free Mem Pool Entry */
    if (pMemPool)
        LacKpt_MemPoolFree(pMemPool);

    /* Increment stats */
    if (CPA_STATUS_SUCCESS == status)
    {
        LAC_KPT_ECDSA_STAT_INC(numKptEcdsaSignRSCompleted, pCryptoService);
    }
    else
    {
        LAC_KPT_ECDSA_STAT_INC(numKptEcdsaSignRSCompletedErrors,
                               pCryptoService);
    }

    if ((CPA_FALSE == signStatus) && (CPA_STATUS_SUCCESS == status))
    {
        LAC_KPT_ECDSA_STAT_INC(numKptEcdsaSignRSCompletedOutputInvalid,
                               pCryptoService);
    }

    /* Invoke the user callback */
    pCb(pCallbackTag, status, pOpData, signStatus, pR, pS);
}

#ifdef ICP_PARAM_CHECK
/**
 ***************************************************************************
 * @ingroup Lac_KptEc
 *      KPT ECDSA Sign R & S parameter check
 *
 ***************************************************************************/
STATIC
CpaStatus LacKptEcdsa_SignRSBasicParamCheck(
    const CpaInstanceHandle instanceHandle,
    const CpaCyKptEcdsaSignRSOpData *pOpData,
    CpaBoolean *pSignStatus,
    CpaFlatBuffer *pR,
    CpaFlatBuffer *pS,
    CpaCyKptUnwrapContext *pKptUnwrapContext)
{

    /* Check for NULL pointers */
    LAC_CHECK_NULL_PARAM(pSignStatus);
    LAC_CHECK_NULL_PARAM(pOpData);
    LAC_CHECK_NULL_PARAM(pR);
    LAC_CHECK_NULL_PARAM(pS);
    LAC_CHECK_NULL_PARAM(pKptUnwrapContext);

    /* Check flat buffers in pOpData for NULL and dataLen of 0 */
    LAC_CHECK_NULL_PARAM(pOpData->privateKey.pData);
    LAC_CHECK_SIZE(&(pOpData->privateKey), CHECK_NONE, 0);
    LAC_CHECK_NULL_PARAM(pOpData->m.pData);
    LAC_CHECK_SIZE(&(pOpData->m), CHECK_NONE, 0);
    LAC_CHECK_NULL_PARAM(pR->pData);
    LAC_CHECK_SIZE(pR, CHECK_NONE, 0);
    LAC_CHECK_NULL_PARAM(pS->pData);
    LAC_CHECK_SIZE(pS, CHECK_NONE, 0);

    return CPA_STATUS_SUCCESS;
}
#endif

/**
 ***************************************************************************
 * @ingroup Lac_KptEc
 *      KPT ECDSA Sign R & S synchronous function
 *
 ***************************************************************************/
STATIC CpaStatus LacKptEcdsa_SignRSSyn(const CpaInstanceHandle instanceHandle,
                                       const CpaCyKptEcdsaSignRSOpData *pOpData,
                                       CpaBoolean *pSignStatus,
                                       CpaFlatBuffer *pR,
                                       CpaFlatBuffer *pS,
                                       CpaCyKptUnwrapContext *pKptUnwrapContext)
{
    CpaStatus status = CPA_STATUS_SUCCESS;
    lac_sync_op_data_t *pSyncCallbackData = NULL;
    sal_crypto_service_t *pCryptoService =
        (sal_crypto_service_t *)instanceHandle;

    status = LacSync_CreateSyncCookie(&pSyncCallbackData);
    /*
     * Call the asynchronous version of the function
     * with the generic synchronous callback function as a parameter.
     */
    if (CPA_STATUS_SUCCESS == status)
    {
        status = cpaCyKptEcdsaSignRS(instanceHandle,
                                     LacSync_GenDualFlatBufVerifyCb,
                                     pSyncCallbackData,
                                     pOpData,
                                     pSignStatus,
                                     pR,
                                     pS,
                                     pKptUnwrapContext);
    }
    else
    {
        LAC_KPT_ECDSA_STAT_INC(numKptEcdsaSignRSRequestErrors, pCryptoService);
        return status;
    }

    if (CPA_STATUS_SUCCESS == status)
    {
        CpaStatus wCbStatus = CPA_STATUS_FAIL;
        wCbStatus = LacSync_WaitForCallback(pSyncCallbackData,
                                            LAC_PKE_SYNC_CALLBACK_TIMEOUT,
                                            &status,
                                            pSignStatus);
        if (CPA_STATUS_SUCCESS != wCbStatus)
        {
            LAC_KPT_ECDSA_STAT_INC(numKptEcdsaSignRSCompletedErrors,
                                   pCryptoService);
            status = wCbStatus;
        }
    }
    else
    {
        /* As the Request was not sent the Callback will never
         * be called, so need to indicate that we're finished
         * with cookie so it can be destroyed. */
        LacSync_SetSyncCookieComplete(pSyncCallbackData);
    }

    LacSync_DestroySyncCookie(&pSyncCallbackData);
    return status;
}

/**
 ***************************************************************************
 * @ingroup Lac_KptEc
 *     This function performs KPT ECDSA Sign R & S.
 *
 ***************************************************************************/
CpaStatus cpaCyKptEcdsaSignRS(const CpaInstanceHandle instanceHandle,
                              const CpaCyEcdsaSignRSCbFunc pCb,
                              void *pCallbackTag,
                              const CpaCyKptEcdsaSignRSOpData *pOpData,
                              CpaBoolean *pSignStatus,
                              CpaFlatBuffer *pR,
                              CpaFlatBuffer *pS,
                              CpaCyKptUnwrapContext *pKptUnwrapContext)
{
    CpaStatus status = CPA_STATUS_SUCCESS;
    sal_crypto_service_t *pCryptoService = NULL;
    Cpa32U keyLengthInBytes = 0;
    Cpa8U *pMemPool = NULL;
    Cpa32U functionID = LAC_PKE_INVALID_FUNC_ID;
    CpaFlatBuffer *pUnwrapCtxBuf = NULL;

#ifdef ICP_TRACE
    LAC_LOG7("Called with params (0x%lx, 0x%lx, 0x%lx, 0x%lx, "
             "%d, 0x%lx, 0x%lx)\n",
             (LAC_ARCH_UINT)instanceHandle,
             (LAC_ARCH_UINT)pCb,
             (LAC_ARCH_UINT)pCallbackTag,
             (LAC_ARCH_UINT)pOpData,
             (LAC_ARCH_UINT)pR,
             (LAC_ARCH_UINT)pS,
             (LAC_ARCH_UINT)pKptUnwrapContext);
#endif

    /* Ensure LAC is initialised - return error if not */
    SAL_RUNNING_CHECK(instanceHandle);

#ifdef ICP_PARAM_CHECK
    /* Instance checks - if fail, no inc stats just return */
    /* check for valid acceleration handle */
    LAC_CHECK_INSTANCE_HANDLE(instanceHandle);
    SAL_CHECK_ADDR_TRANS_SETUP(instanceHandle);
    /* Ensure this is a asym instance with pke enabled */
    SAL_CHECK_INSTANCE_TYPE(instanceHandle, SAL_SERVICE_TYPE_CRYPTO_ASYM);
    /* Check the input parameters are valid */
    status = LacKptEcdsa_SignRSBasicParamCheck(
        instanceHandle, pOpData, pSignStatus, pR, pS, pKptUnwrapContext);
#endif

    pCryptoService = (sal_crypto_service_t *)instanceHandle;
    /* Check if the API has been called in synchronous mode */
    if (NULL == pCb)
    {
        /* Call synchronous mode function */
        return LacKptEcdsa_SignRSSyn(
            instanceHandle, pOpData, pSignStatus, pR, pS, pKptUnwrapContext);
    }

    if (CPA_STATUS_SUCCESS == status)
    {
        keyLengthInBytes = LacKptEcdsa_GetKeyLength(&(pOpData->privateKey));
        if (LAC_KPT_PKE_INVALID_KEY_SIZE == keyLengthInBytes)
        {
            LAC_INVALID_PARAM_LOG("Invalid KPT Clear Private Key Size");
            status = CPA_STATUS_INVALID_PARAM;
        }
    }

    if (CPA_STATUS_SUCCESS == status)
    {
        functionID = LacKptEcdsa_GetMmpId(pKptUnwrapContext);
        if (LAC_PKE_INVALID_FUNC_ID == functionID)
        {
            LAC_INVALID_PARAM_LOG("Invalid Function ID");
            status = CPA_STATUS_INVALID_PARAM;
        }
    }

    if (CPA_STATUS_SUCCESS == status)
    {
        status =
            LacKpt_MemPoolMalloc(&pMemPool, pCryptoService->lac_pke_kpt_pool);
        if (CPA_STATUS_SUCCESS == status)
        {
            icp_qat_fw_mmp_input_param_t in = { .flat_array = { 0 } };
            icp_qat_fw_mmp_output_param_t outRS = { .flat_array = { 0 } };
            lac_pke_op_cb_data_t cbData = { 0 };

            /* Holding the calculated size of the input/output parameters */
            Cpa32U inArgSizeList[LAC_MAX_MMP_INPUT_PARAMS] = { 0 };
            Cpa32U outArgSizeList[LAC_MAX_MMP_OUTPUT_PARAMS] = { 0 };

            CpaBoolean internalMemInList[LAC_MAX_MMP_INPUT_PARAMS] = {
                CPA_FALSE
            };
            CpaBoolean internalMemOutList[LAC_MAX_MMP_OUTPUT_PARAMS] = {
                CPA_FALSE
            };

            /* Clear output buffers */
            osalMemSet(pR->pData, 0, pR->dataLenInBytes);
            osalMemSet(pS->pData, 0, pS->dataLenInBytes);

            LAC_MEM_SHARED_WRITE_FROM_PTR(
                in.mmp_kpt_ecdsa_sign_rs_p256.kpt_wrapped,
                &(pOpData->privateKey));
            inArgSizeList[LAC_IDX_OF(
                icp_qat_fw_mmp_kpt_ecdsa_sign_rs_p256_input_t, kpt_wrapped)] =
                pOpData->privateKey.dataLenInBytes;
            /* Input memory to QAT is external allocated */
            internalMemInList[LAC_IDX_OF(
                icp_qat_fw_mmp_kpt_ecdsa_sign_rs_p256_input_t, kpt_wrapped)] =
                CPA_FALSE;

            LAC_MEM_SHARED_WRITE_FROM_PTR(in.mmp_kpt_ecdsa_sign_rs_p256.e,
                                          &(pOpData->m));
            inArgSizeList[LAC_IDX_OF(
                icp_qat_fw_mmp_kpt_ecdsa_sign_rs_p256_input_t, e)] =
                keyLengthInBytes;
            internalMemInList[LAC_IDX_OF(
                icp_qat_fw_mmp_kpt_ecdsa_sign_rs_p256_input_t, e)] = CPA_FALSE;

            LacKpt_BuildUnwrapCtxMemBuffer(pMemPool, pKptUnwrapContext);
            pUnwrapCtxBuf =
                (CpaFlatBuffer *)(pMemPool + LAC_KPT_UNWRAP_CTX_SIZE_IN_BYTES);
            pUnwrapCtxBuf->dataLenInBytes = LAC_KPT_UNWRAP_CTX_SIZE_IN_BYTES;
            pUnwrapCtxBuf->pData = pMemPool;

            LAC_MEM_SHARED_WRITE_FROM_PTR(
                in.mmp_kpt_ecdsa_sign_rs_p256.key_unwrap_context,
                pUnwrapCtxBuf);
            inArgSizeList[LAC_IDX_OF(
                icp_qat_fw_mmp_kpt_ecdsa_sign_rs_p256_input_t,
                key_unwrap_context)] = LAC_KPT_UNWRAP_CTX_SIZE_IN_BYTES;
            internalMemInList[LAC_IDX_OF(
                icp_qat_fw_mmp_kpt_ecdsa_sign_rs_p256_input_t,
                key_unwrap_context)] = CPA_TRUE;

            LAC_MEM_SHARED_WRITE_FROM_PTR(outRS.mmp_kpt_ecdsa_sign_rs_p256.r,
                                          pR);
            LAC_MEM_SHARED_WRITE_FROM_PTR(outRS.mmp_kpt_ecdsa_sign_rs_p256.s,
                                          pS);
            /* Output memory to QAT is externally allocated */
            LAC_EC_SET_LIST_PARAMS(internalMemOutList,
                                   LAC_KPT_ECDSA_SIGNRS_NUM_OUT_ARGS,
                                   CPA_FALSE);
            LAC_EC_SET_LIST_PARAMS(outArgSizeList,
                                   LAC_KPT_ECDSA_SIGNRS_NUM_OUT_ARGS,
                                   keyLengthInBytes);

            /* Populate callback data */
            cbData.pClientCb = pCb;
            cbData.pCallbackTag = pCallbackTag;
            cbData.pClientOpData = pOpData;
            cbData.pOpaqueData = pMemPool;
            cbData.pOutputData1 = pR;
            cbData.pOutputData2 = pS;

            /* Send a PKE KPT request to the QAT */
            status = LacPkeKpt_SendSingleRequest(functionID,
                                                 inArgSizeList,
                                                 outArgSizeList,
                                                 &in,
                                                 &outRS,
                                                 internalMemInList,
                                                 internalMemOutList,
                                                 LacKptEcdsa_SignRSCallback,
                                                 &cbData,
                                                 instanceHandle);

            if (CPA_STATUS_SUCCESS != status)
            {
                /* Free Mem Pool */
                if (NULL != pMemPool)
                {
                    LacKpt_MemPoolFree(pMemPool);
                }
            }
        }
    }

    if (CPA_STATUS_SUCCESS == status)
    {
        /* Increment stats */
        LAC_KPT_ECDSA_STAT_INC(numKptEcdsaSignRSRequests, pCryptoService);
    }
    else
    {
        /* Increment stats */
        LAC_KPT_ECDSA_STAT_INC(numKptEcdsaSignRSRequestErrors, pCryptoService);
    }

    return status;
}
