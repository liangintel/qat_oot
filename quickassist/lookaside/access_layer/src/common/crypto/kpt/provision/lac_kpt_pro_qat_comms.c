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
 * @file lac_kpt_pro_qat_comms.c
 *
 * @ingroup LacKpt
 *
 * This file implements KPT key provision functions.
 *
 *****************************************************************************/

/*
********************************************************************************
* Include public/global header files
********************************************************************************
*/
#include "cpa.h"

/*
****************************************************************************
* Include private header files
****************************************************************************
*/
/* Look Aside Includes */
#include "lac_log.h"
#include "lac_common.h"
#include "lac_kpt_pro_qat_comms.h"
#include "lac_common.h"
#include "lac_mem.h"
#include "lac_mem_pools.h"
#include "lac_list.h"
#include "lac_sym_qat.h"
#include "lac_sal_types_crypto.h"
#include "sal_qat_cmn_msg.h"

/* ADF include */
#include "icp_adf_transport.h"


#define LAC_KPT_PRO_SYNC_CALLBACK_TIMEOUT (2000) /* 2000ms */
#define KPT_DEV_CREDENTIAL_SIZE_IN_BYTE (776)
/*
****************************************************************************
* Define static function definitions
****************************************************************************
*/

/**
***************************************************************************
* @ingroup LacKpt
*        Kpt provision client callback function
***************************************************************************/
STATIC
void LacKpt_ProcessProCb(CpaStatus status,
                         CpaInstanceHandle instanceHandle,
                         lac_kpt_pro_op_cb_data_t *pCbData)
{
    lac_kpt_pro_sync_cb pCb = NULL;
    void *pCallbackTag = NULL;
    /* extract info from callback data structure */
    LAC_ASSERT_NOT_NULL(pCbData);
    pCallbackTag = (void *)pCbData->pCallbackTag;
    pCb = (lac_kpt_pro_sync_cb)LAC_CONST_PTR_CAST(pCbData->pClientCb);
    LAC_ASSERT_NOT_NULL(pCb);
    /* invoke the user callback */
    pCb(pCallbackTag, status);
}

/**
***************************************************************************
* @ingroup LacKpt
*        KPT provision sync mode client callback function
***************************************************************************/
STATIC
void LacKpt_Pro_SyncClientCb(void *pCallbackTag, CpaStatus status)
{
    LacSync_GenWakeupSyncCaller(pCallbackTag, status);
}

/**
***************************************************************************
* @ingroup LacKpt
*        KPT provision sync mode client callback function
***************************************************************************/
STATIC
CpaStatus LacKpt_Pro_CreateRequest(lac_kpt_pro_req_handle_t *pReqHandle,
                                   CpaInstanceHandle instanceHandle,
                                   Cpa8U cmdID,
                                   CpaCyKptHandle *pKeyHandle,
                                   CpaFlatBuffer *pSrc,
                                   lac_kpt_pro_op_cb_data_t *pCbData)
{
    CpaStatus status = CPA_STATUS_FAIL;
    lac_kpt_pro_qat_req_data_t *pReqData = NULL;
    sal_crypto_service_t *pCryptoService =
        (sal_crypto_service_t *)instanceHandle;

    status = LAC_OS_MALLOC(&(pReqData), sizeof(lac_kpt_pro_qat_req_data_t));
    if (CPA_STATUS_SUCCESS == status)
    {
        if (NULL == pReqData)
            return CPA_STATUS_RESOURCE;

        LAC_OS_BZERO(pReqData, sizeof(lac_kpt_pro_qat_req_data_t));
        pReqData->cbinfo.cbFunc = LacKpt_ProcessProCb;
        pReqData->cbinfo.pcbData = pCbData;
        pReqData->cbinfo.instanceHandle = instanceHandle;
        pReqData->u1.request.cmd_id = cmdID;
        pReqData->u1.request.service_type = LAC_KPT_PRO_SERVICE_TYPE;
        pReqData->u1.request.opaque_data = (LAC_ARCH_UINT)pReqData;
        pReqData->u1.request.valid = ICP_QAT_FW_COMN_VALID_FLAG_MASK;

        /* Set AT flag in request header if instance supports AT */
        if (pCryptoService->generic_service_info.atEnabled)
        {
            SalQatMsg_AddressTranslationHdrWrite(
                &pReqData->u1.request.comn_req_flags);
        }

        if ((cmdID == KPT_PRO_DEL_SWK_CMD) && (pKeyHandle != NULL))
            pReqData->u1.request.key_handle = *pKeyHandle;

        if ((NULL != pSrc) && (NULL != pSrc->pData))
            pReqData->u1.request.src_addr = LAC_OS_VIRT_TO_PHYS_EXTERNAL(
                pCryptoService->generic_service_info, pSrc->pData);

        if (NULL != pCbData->pVirtAddr)
            pReqData->u1.request.dst_addr = LAC_OS_VIRT_TO_PHYS_INTERNAL(
                &pCryptoService->generic_service_info, pCbData->pVirtAddr);

        *pReqHandle = (lac_kpt_pro_req_handle_t)pReqData;
    }

    return status;
}

/**
 ***************************************************************************
 * @ingroup LacKpt
 *      Destroy Kpt provision requests
 ***************************************************************************/
STATIC
void LacKpt_Pro_DestroyRequest(lac_kpt_pro_req_handle_t *pReqHandle)
{
    if (NULL != pReqHandle)
        LAC_OS_FREE(*pReqHandle);
}

/**
 ***************************************************************************
 * @ingroup LacKpt
 *      Kpt Provision request create and send to QAT
 ***************************************************************************/
STATIC
CpaStatus LacKpt_Pro_SendSingleRequest(CpaInstanceHandle instanceHandle,
                                       Cpa8U cmdID,
                                       CpaCyKptHandle *pKeyHandle,
                                       CpaFlatBuffer *pSrc,
                                       lac_kpt_pro_op_cb_data_t *pCbData)
{
    CpaStatus status = CPA_STATUS_SUCCESS;
    sal_crypto_service_t *pCryptoService =
        (sal_crypto_service_t *)instanceHandle;
    lac_kpt_pro_req_handle_t reqHandle = LAC_KPT_PRO_INVALID_HANDLE;
    lac_kpt_pro_qat_req_data_t *pReqData = NULL;

    status = LacKpt_Pro_CreateRequest(
        &reqHandle, instanceHandle, cmdID, pKeyHandle, pSrc, pCbData);
    if (CPA_STATUS_SUCCESS == status)
    {
        LAC_ASSERT_NOT_NULL(reqHandle);
        pReqData = (lac_kpt_pro_qat_req_data_t *)reqHandle;

        switch (pCryptoService->generic_service_info.type)
        {
            case SAL_SERVICE_TYPE_CRYPTO_ASYM:
                status =
                    SalQatMsg_transPutMsg(pCryptoService->trans_handle_asym_tx,
                                          (void *)&(pReqData->u1.request),
                                          LAC_QAT_KPT_PRO_REQ_SZ_LW / 2,
                                          LAC_LOG_MSG_KPT_PRO,
                                          NULL);
                break;
            case SAL_SERVICE_TYPE_CRYPTO_SYM:
                status =
                    SalQatMsg_transPutMsg(pCryptoService->trans_handle_sym_tx,
                                          (void *)&(pReqData->u1.request),
                                          LAC_QAT_KPT_PRO_REQ_SZ_LW,
                                          LAC_LOG_MSG_KPT_PRO,
                                          NULL);
                break;
            default:
                status = CPA_STATUS_INVALID_PARAM;
                break;
        }

        if (CPA_STATUS_SUCCESS != status)
        {
            LacKpt_Pro_DestroyRequest(&reqHandle);
        }
    }
    return status;
}

/**
***************************************************************************
* @ingroup LacKpt
*        KPT Provision sync mode packet sending function
***************************************************************************/
CpaStatus LacKpt_Pro_SendRequest(CpaInstanceHandle instanceHandle,
                                 Cpa8U cmdID,
                                 CpaCyKptHandle *pKeyHandle,
                                 CpaFlatBuffer *pSrc,
                                 CpaCyKptValidationKey *pDevCredential,
                                 CpaCyKptKeyManagementStatus *pKptStatus)
{
    CpaStatus status = CPA_STATUS_FAIL;
    lac_kpt_pro_op_cb_data_t cbData = {0};
    lac_sync_op_data_t *pSyncCallbackData = NULL;
    Cpa8U *pVirt_addr = NULL;

    status = LacSync_CreateSyncCookie(&pSyncCallbackData);
    if (CPA_STATUS_SUCCESS != status)
    {
        LAC_LOG_ERROR("Failed to create sync cookie");
        return status;
    }

    cbData.cmdID = cmdID;
    cbData.pCallbackTag = (void *)pSyncCallbackData;
    cbData.pClientCb = (void *)LacKpt_Pro_SyncClientCb;
    cbData.pVirtAddr = NULL;
    if (NULL != pDevCredential)
    {
        sal_crypto_service_t *pCryptoService =
            (sal_crypto_service_t *)instanceHandle;

        status = LAC_OS_CAMALLOC(&pVirt_addr,
                                 KPT_DEV_CREDENTIAL_SIZE_IN_BYTE,
                                 LAC_64BYTE_ALIGNMENT,
                                 pCryptoService->nodeAffinity);

        if (CPA_STATUS_SUCCESS == status)
            cbData.pVirtAddr = pVirt_addr;
    }

    if (CPA_STATUS_SUCCESS == status)
        status = LacKpt_Pro_SendSingleRequest(
            instanceHandle, cmdID, pKeyHandle, pSrc, &cbData);

    if (CPA_STATUS_SUCCESS == status)
    {
        status = LacSync_WaitForCallback(pSyncCallbackData,
                                         LAC_KPT_PRO_SYNC_CALLBACK_TIMEOUT,
                                         &status,
                                         NULL);

        if (CPA_STATUS_SUCCESS == status)
        {
            if (cmdID != cbData.cmdID)
            {
                status = CPA_STATUS_FAIL;
                LAC_LOG_ERROR("Get a different command id from kpt provision"
                              "response");
            }

            if (CPA_STATUS_SUCCESS == status)
            {
                *pKptStatus = cbData.rspStatus;

                if (NULL != pDevCredential && NULL != cbData.pVirtAddr &&
                    NULL != pDevCredential->publicKey.modulusN.pData &&
                    NULL != pDevCredential->publicKey.publicExponentE.pData)
                {
                    memcpy(pDevCredential->publicKey.modulusN.pData,
                           cbData.pVirtAddr,
                           KPT_DEV_IPUB_N_SIZE_IN_BYTE);
                    memcpy(pDevCredential->publicKey.publicExponentE.pData,
                           cbData.pVirtAddr + KPT_DEV_IPUB_N_SIZE_IN_BYTE,
                           KPT_DEV_IPUB_E_SIZE_IN_BYTE);
                    memcpy(pDevCredential->signature,
                           cbData.pVirtAddr + KPT_DEV_IPUB_N_SIZE_IN_BYTE +
                               KPT_DEV_IPUB_E_SIZE_IN_BYTE,
                           CPA_CY_RSA3K_SIG_SIZE_INBYTES);
                }

                if (NULL != pKeyHandle)
                    *pKeyHandle = cbData.keyHandle;
            }
        }
    }
    else
        LacSync_SetSyncCookieComplete(pSyncCallbackData);

    if (NULL != cbData.pVirtAddr)
        LAC_OS_CAFREE(cbData.pVirtAddr);

    LacSync_DestroySyncCookie(&pSyncCallbackData);

    return status;
}

/**
***************************************************************************
* @ingroup LacKpt
*        KPT Provision response handler
***************************************************************************/
void LacKpt_Pro_RspHandler(void *pRespMsg)
{
    lac_kpt_pro_qat_req_data_t *pReqData = NULL;
    lac_kpt_pro_op_cb_data_t *pCbData = NULL;
    lac_kpt_pro_op_cb_func_t pCbFunc = NULL;
    lac_kpt_pro_req_handle_t requestHandle = CPA_INSTANCE_HANDLE_SINGLE;
    CpaInstanceHandle instanceHandle = CPA_INSTANCE_HANDLE_SINGLE;
    CpaStatus status = CPA_STATUS_SUCCESS;
    icp_qat_fw_kpt_pro_resp_data_t *pKptProRespMsg =
        (icp_qat_fw_kpt_pro_resp_data_t *)pRespMsg;
    Cpa8U pkeRespFlags = pKptProRespMsg->rsp_status.pke_resp_flags;

    LAC_MEM_SHARED_READ_TO_PTR(pKptProRespMsg->opaque_data, pReqData);

    requestHandle = (lac_kpt_pro_req_handle_t)pReqData;
    pCbFunc = pReqData->cbinfo.cbFunc;
    pCbData = pReqData->cbinfo.pcbData;
    instanceHandle = pReqData->cbinfo.instanceHandle;
    pCbData->cmdID = pKptProRespMsg->cmd_id;
    pCbData->keyHandle = pKptProRespMsg->key_handle;

    /* According to FW spec, some response code can be got earlier */
    if (ICP_QAT_FW_COMN_STATUS_FLAG_OK == pkeRespFlags)
       pCbData->rspStatus = CPA_CY_KPT_SUCCESS;
    else if (ICP_QAT_FW_PKE_RESP_PKE_STAT_GET(pkeRespFlags))
       pCbData->rspStatus = pKptProRespMsg->rsp_status.comn_err_code;
    else
       pCbData->rspStatus = CPA_CY_KPT_FAILED;

    LacKpt_Pro_DestroyRequest(&requestHandle);
    (*pCbFunc)(status, instanceHandle, pCbData);

    return;
}
