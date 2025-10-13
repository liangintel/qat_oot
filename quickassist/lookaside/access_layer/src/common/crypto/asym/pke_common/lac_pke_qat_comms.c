/*
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
 */

/**
 ***************************************************************************
 * @file lac_pke_qat_comms.c
 *
 * @ingroup LacAsymCommonQatComms
 *
 * This file implements the API for creating PKE QAT messages and sending
 * these to the QAT. It implements an API for creating a PKE QAT request, and
 * for sending a PKE message to the QAT.
 *
 ***************************************************************************/

/*
****************************************************************************
* Include public/global header files
****************************************************************************
*/

#include "cpa.h"

/*
****************************************************************************
* Include private header files
****************************************************************************
*/
/* ADF incldues */
#include "icp_adf_init.h"
#include "icp_adf_transport.h"
#include "icp_accel_devices.h"
#include "icp_adf_debug.h"

/* QAT includes */
#include "icp_qat_fw_la.h"
#include "icp_qat_fw_pke.h"
#include "icp_qat_fw_mmp.h"
/* SAL includes */
#include "lac_common.h"
#include "lac_mem.h"
#include "lac_mem_pools.h"
#include "lac_list.h"
#include "lac_sym_qat.h"
#include "lac_sal_types_crypto.h"
#include "sal_qat_cmn_msg.h"
#include "lac_pke_qat_comms.h"
#include "lac_pke_utils.h"
#include "lac_pke_mmp.h"
#include "sal_misc_error_stats.h"
#include "lac_kpt_pro_qat_comms.h"

/*
****************************************************************************
* Static Variables
****************************************************************************
*/

/*
****************************************************************************
* Define static function definitions
****************************************************************************
*/

/********************************************************************
 * @ingroup LacAsymCommonQatComms
 *
 * @description
 *      This function fills in LW0 and LW 1 of the PKE Header
 *
 * @param[in]   pMsg            Pointer to 64B Request Msg buffer
 * @param[in]   serviceType     Request types
 * @param[in]   cmnFlags        Common request flag
 *
 *
 * @assumptions
 *

 * @return
 *      None
 *
 *****************************************/

STATIC
void LacPke_HdrWrite(icp_qat_fw_pke_request_t *pMsg,
                     icp_qat_fw_comn_request_id_t serviceType,
                     icp_qat_fw_comn_flags_pke cmnFlags,
                     CpaBoolean atEnabled)
{
    icp_qat_fw_req_pke_hdr_t *pHeader = NULL;

    if (!pMsg)
    {
        LAC_LOG_ERROR("Invalid pointer pMsg");
        return;
    }

    pHeader = &(pMsg->pke_hdr);
    osalMemSet(pHeader, 0, sizeof(icp_qat_fw_req_pke_hdr_t));

    /* LW0 */
    pHeader->resrvd1 = 0;
    pHeader->resrvd2 = 0;
    pHeader->service_type = (uint8_t)serviceType;
    ICP_QAT_FW_PKE_RQ_VALID_FLAG_SET((*pHeader), ICP_QAT_FW_COMN_REQ_FLAG_SET);

    /* LW1 */
    pHeader->comn_req_flags = cmnFlags;
    pHeader->extended_serv_specif_flags = 0;
    if (atEnabled)
    {
        SalQatMsg_AddressTranslationHdrWrite(&(pHeader->comn_req_flags_ext));
    }
}

/**
 ***************************************************************************
 * @ingroup LacAsymCommonQatComms
 *      Resizes parameters for a PKE request if required
 *
 * @description
 *      This function resizes the flat buffer parameters for a PKE request, if
 *  required, by calling icp_LacBufferResize for each input/output flat buffer
 *  parameter in the request data structure.  LacPke_RestoreParams is the
 *  corresponding function for undoing the buffer copies.
 *
 * @param[in] pParamInfo  The data pointers of the flat buffers from the
 *                        clientInputParams and clientOutputParams
 *                        arrays are resized as necessary and stored in
 *                        the pkeInputParams and pkeOutputParams arrays
 *                        respectively.  The client...Params arrays are
 *                        processed one-by-one from the start, and
 *                        processing ends once a NULL parameter is
 *                        encountered.  Consequently, the pke...Params
 *                        arrays should be initialized to zero as NULL
 *                        inputs won't be written as NULL outputs.
 * @param[in/out] pInternalInMemList
 *                        pointer to a list of Booleans that indicate if
 *                        input data buffers passed to QAT are internally or
 *                        externally allocated. This information needs to
 *                        be tracked to ensure we use the corect virt2phys
 *                        function, values may be updated by this function.
 * @param[in/out] pInternalInMemList
 *                        pointer to a list of Booleans that indicate if
 *                        output data buffers passed to QAT are internally
 *                        or externally allocated, values may be updated by
 *                        this function.
 * @param[in] instanceHandle  instanceHandle
 *
 *
 * @retval CPA_STATUS_SUCCESS     No error
 * @retval CPA_STATUS_RESOURCE    Resource error (e.g. failed memory allocation)
 *
 * @see LacPke_RestoreParams()
 * @see icp_LacBufferResize()
 ***************************************************************************/
STATIC
CpaStatus LacPke_ResizeParams(lac_pke_qat_req_data_param_info_t *pParamInfo,
                              CpaBoolean *pInternalInMemList,
                              CpaBoolean *pInternalOutMemList,
                              CpaInstanceHandle instanceHandle)
{
    CpaStatus status = CPA_STATUS_SUCCESS;
    Cpa32U i = 0;
    Cpa32U pke_len = 0;
    Cpa32U payload_len = 0;
    Cpa32U desired_len = 0;
    Cpa32U user_data_offset = 0;

    /* Resize input parameter flat buffers (end if NULL encountered)
     * Check each input, calculate offset if used, resize if necessary
     */
    for (i = 0; i < LAC_MAX_MMP_INPUT_PARAMS; i++)
    {
        if (i >= pParamInfo->indexClientInputParams)
            break;

        user_data_offset = 0;
        desired_len = pParamInfo->clientInputParams[i].dataLenInBytes;
        payload_len = pParamInfo->clientInputParams[i].dataLenInBytes;

        /*  Calculate offset, user inputs larger buffer than inArgSize */
        if (pParamInfo->inArgSizeList[i])
        {
            desired_len = pParamInfo->inArgSizeList[i];

            /* If user submitted too large buffer, we just ignore part of it */
            if (pParamInfo->clientInputParams[i].dataLenInBytes > desired_len)
            {
                user_data_offset =
                    pParamInfo->clientInputParams[i].dataLenInBytes -
                    desired_len;
                payload_len -= user_data_offset;
            }
        }

        pke_len = LAC_ALIGN_POW2_ROUNDUP(desired_len, LAC_QUAD_WORD_IN_BYTES);

        if (payload_len == pke_len)
        {
            /* No need to resize buffer */
            pParamInfo->pkeInputParams[i] =
                pParamInfo->clientInputParams[i].pData + user_data_offset;
        }
        else
        {
            /* Resize buffer (update buffer to quadwords 4, 8, or 9) */
            pParamInfo->pkeInputParams[i] = icp_LacBufferResize(
                instanceHandle,
                pParamInfo->clientInputParams[i].pData + user_data_offset,
                payload_len,
                pke_len,
                &(pInternalInMemList[i]));

            if (!pParamInfo->pkeInputParams[i])
                status = CPA_STATUS_RESOURCE;

            LAC_CHECK_STATUS(status);
        }
    }

    /* Resize output parameter flat buffers (end if NULL encountered)
     * Check each input, calculate offset if used, resize if necessary
     */
    for (i = 0; i < LAC_MAX_MMP_OUTPUT_PARAMS; i++)
    {
        if (i >= pParamInfo->indexClientOutputParams)
            break;

        user_data_offset = 0;
        desired_len = pParamInfo->clientOutputParams[i].dataLenInBytes;
        payload_len = pParamInfo->clientOutputParams[i].dataLenInBytes;

        /*  Calculate offset, user inputs larger buffer than inArgSize */
        if (pParamInfo->outArgSizeList[i])
        {
            desired_len = pParamInfo->outArgSizeList[i];

            if (pParamInfo->clientOutputParams[i].dataLenInBytes > desired_len)
            {
                user_data_offset =
                    pParamInfo->clientOutputParams[i].dataLenInBytes -
                    desired_len;
                payload_len -= user_data_offset;
            }
        }

        pke_len = LAC_ALIGN_POW2_ROUNDUP(desired_len, LAC_QUAD_WORD_IN_BYTES);

        if (payload_len == pke_len)
        {
            /* No need to resize buffer */
            pParamInfo->pkeOutputParams[i] =
                pParamInfo->clientOutputParams[i].pData + user_data_offset;
        }
        else
        {
            LAC_ENSURE_NOT_NULL(pInternalOutMemList);

            if (NULL != pInternalOutMemList)
            {
                /* Resize buffer (update buffer to quadwords 4, 8, or 9) */
                pParamInfo->pkeOutputParams[i] = icp_LacBufferResize(
                    instanceHandle,
                    pParamInfo->clientOutputParams[i].pData + user_data_offset,
                    payload_len,
                    pke_len,
                    &(pInternalOutMemList[i]));
            }

            if (!pParamInfo->pkeOutputParams[i])
                status = CPA_STATUS_RESOURCE;

            LAC_CHECK_STATUS(status);
        }
    }

    return status;
}

/**
 ***************************************************************************
 * @ingroup LacAsymCommonQatComms
 *      Restores parameters for a PKE request
 *
 * @description
 *      This function restores the flat buffer parameters for a PKE request, by
 * calling icp_LacBufferRestore for each input/output flat buffer parameter in
 * the request data structure.  LacPke_ResizeParams is the corresponding
 * function for doing the buffer resize.
 *
 * @param pParamInfo        IN  The data pointers from the pkeInputParams and
 *                              pkeOutputParams arrays are restored and stored
 *                              in the data pointers of the flat buffers in
 *                              the clientInputParams and clientOutputParams
 *                              arrays respectively.  The pke...Params arrays
 *                              are processed one-by-one from the start, and
 *                              processing ends once a NULL parameter is
 *                              encountered.  Consequently, the
 *                              client...Params arrays should be initialized
 *                              to zero as NULL inputs won't be written as
 *                              NULL outputs.
 *
 * @retval CPA_STATUS_SUCCESS       No error
 * @retval CPA_STATUS_RESOURCE       Resource error (e.g. failed memory free)
 *
 * @see LacPke_ResizeParams()
 * @see icp_LacBufferRestore()
 ***************************************************************************/
STATIC
CpaStatus LacPke_RestoreParams(lac_pke_qat_req_data_param_info_t *pParamInfo)
{
    CpaStatus status = CPA_STATUS_SUCCESS;
    Cpa32U i = 0;
    Cpa32U pke_len = 0;
    Cpa32U payload_len = 0;
    Cpa32U desired_len = 0;
    Cpa32U user_data_offset = 0;

    /* Restore input parameter flat buffers (end if NULL encountered)
     * Check each input, calculate offset if used, restore if necessary
     */
    for (i = 0; i < LAC_MAX_MMP_INPUT_PARAMS; i++)
    {
        if (i >= pParamInfo->indexClientInputParams)
            break;

        user_data_offset = 0;
        desired_len = pParamInfo->inArgSizeList[i];
        payload_len = pParamInfo->clientInputParams[i].dataLenInBytes;

        if (pParamInfo->inArgSizeList[i])
        {
            /* If user submits a too large buffer, we just ignore part of it */
            if (pParamInfo->clientInputParams[i].dataLenInBytes > desired_len)
            {
                user_data_offset =
                    pParamInfo->clientInputParams[i].dataLenInBytes -
                    desired_len;
                payload_len -= user_data_offset;
            }
        }

        /* Calculate the actual size we need for FW */
        pke_len = LAC_ALIGN_POW2_ROUNDUP(desired_len, LAC_QUAD_WORD_IN_BYTES);

        /* If user data is less than rounded, we know we previously
         * resized and that we need to restore the memory
         */
        if (payload_len < pke_len)
        {
            status = icp_LacBufferRestore(
                pParamInfo->clientInputParams[i].pData + user_data_offset,
                pParamInfo->clientInputParams[i].dataLenInBytes -
                    user_data_offset,
                pParamInfo->pkeInputParams[i],
                pke_len,
                CPA_FALSE);
            LAC_CHECK_STATUS(status);
        }
    }

    /* Restore output parameter flat buffers (end if NULL encountered)
     * Check each input, restore if necessary, calculate offset if used
     */
    for (i = 0; i < LAC_MAX_MMP_OUTPUT_PARAMS; i++)
    {
        if (i >= pParamInfo->indexClientOutputParams)
            break;

        user_data_offset = 0;
        desired_len = pParamInfo->outArgSizeList[i];
        payload_len = pParamInfo->clientOutputParams[i].dataLenInBytes;

        if (pParamInfo->outArgSizeList[i])
        {
            /* If user submitted too large buffer, we just ignore part of it */
            if (pParamInfo->clientOutputParams[i].dataLenInBytes > desired_len)
            {
                user_data_offset =
                    pParamInfo->clientOutputParams[i].dataLenInBytes -
                    desired_len;
                payload_len -= user_data_offset;
            }
        }

        /* Calculate the actual size we need for FW */
        pke_len = LAC_ALIGN_POW2_ROUNDUP(desired_len, LAC_QUAD_WORD_IN_BYTES);

        /* If user input data is less than rounded, we know we previously
         * resized and that we need to restore the memory
         */
        if (payload_len < pke_len)
        {
            status = icp_LacBufferRestore(
                pParamInfo->clientOutputParams[i].pData + user_data_offset,
                pParamInfo->clientOutputParams[i].dataLenInBytes -
                    user_data_offset,
                pParamInfo->pkeOutputParams[i],
                pke_len,
                CPA_TRUE);
            LAC_CHECK_STATUS(status);
        }
    }

    return status;
}

/**
 ***************************************************************************
 * @ingroup LacAsymCommonQatComms
 *      Destroys a PKE request
 *
 * @description
 *      This function destroys a PKE request that was created using
 * LacPke_CreateRequest().  It should be called if an error occurs during
 * request create or request send, or else as part of the response callback.
 *
 * @param pRequestHandle    IN  Pointer to the request handle that identifies
 *                              the request to be destroyed.  The request
 *                              handle will get set to LAC_PKE_INVALID_HANDLE.
 *
 * @retval CPA_STATUS_SUCCESS       No error
 * @retval CPA_STATUS_RESOURCE       Resource error (e.g. failed memory free)
 *
 * @see LacPke_CreateRequest()
 ***************************************************************************/
CpaStatus LacPke_DestroyRequest(lac_pke_request_handle_t *pRequestHandle)
{
    CpaStatus status = CPA_STATUS_SUCCESS;
    lac_pke_qat_req_data_t *pReqData = NULL;

    /* extract head request data pointer from the request handle */
    pReqData = *pRequestHandle;

    /* invalidate the request handle */
    *pRequestHandle = LAC_PKE_INVALID_HANDLE;

    /* free all request data structures in the chain - continue even in
       the case of errors */
    while (NULL != pReqData)
    {
        lac_pke_qat_req_data_t *pNextReqData = pReqData->pNextReqData;

        /* restore parameters (i.e. undo resizing) */
        if (CPA_STATUS_SUCCESS != LacPke_RestoreParams(&pReqData->paramInfo))
        {
            status = CPA_STATUS_RESOURCE;
        }
        SalQatMsg_KptFlagClearHdrWrite(
            &(pReqData->u1.request.pke_hdr.extended_serv_specif_flags));
        LAC_MEM_POOL_BLK_SET_OPAQUE(pReqData, ICP_ADF_INVALID_SEND_SEQ);
        Lac_MemPoolEntryFree(pReqData);
        pReqData = pNextReqData;
    }

    return status;
}

/**
 ***************************************************************************
 * @ingroup LacAsymCommonQatComms
 *      PKE callback
 ***************************************************************************/
void LacPke_MsgCallback(void *pRespMsg)
{
    CpaInstanceHandle instanceHandle = CPA_INSTANCE_HANDLE_SINGLE;
    CpaStatus status = CPA_STATUS_SUCCESS;
    CpaBoolean pass = CPA_TRUE, isFwUnSupp = CPA_FALSE;
    Cpa8U comnErr = ERR_CODE_NO_ERROR;
    icp_qat_fw_pke_resp_t *pPkeRespMsg = NULL;
    lac_pke_request_handle_t requestHandle = LAC_PKE_INVALID_HANDLE;
    lac_pke_qat_req_data_t *pReqData = NULL;
    lac_pke_op_cb_func_t pCbFunc = NULL;
    lac_pke_op_cb_data_t cbData = {0};

    /* cast response message to PKE response message type */
    pPkeRespMsg = (icp_qat_fw_pke_resp_t *)pRespMsg;

    /* Check for FW Unsupported error */
    isFwUnSupp = ICP_QAT_FW_COMN_RESP_UNSUPPORTED_REQUEST_STAT_GET(
        pPkeRespMsg->pke_resp_hdr.resp_status.pke_resp_flags);
    /* check QAT response status */
    pass =
        (CpaBoolean)(ICP_QAT_FW_COMN_STATUS_FLAG_OK ==
                     ICP_QAT_FW_PKE_RESP_PKE_STAT_GET(
                         pPkeRespMsg->pke_resp_hdr.resp_status.pke_resp_flags));

    comnErr = pPkeRespMsg->pke_resp_hdr.resp_status.comn_err_code;

    /* log the slice hang and endpoint push/pull error inside the response */
    if (ERR_CODE_SSM_ERROR == (Cpa8S)comnErr)
    {
        LAC_LOG_ERROR("The slice hang error is detected on the MMP slice. ");
    }
    else if (ERR_CODE_ENDPOINT_ERROR == (Cpa8S)comnErr)
    {
        LAC_LOG_ERROR(
            "The PCIe End Point Push/Pull or TI/RI Parity error detected.");
    }

    if (LAC_KPT_PRO_SERVICE_TYPE == pPkeRespMsg->pke_resp_hdr.response_type)
    {
        LacKpt_Pro_RspHandler(pRespMsg);
        return;
    }

    /* extract request data pointer from the opaque data */
    LAC_MEM_SHARED_READ_TO_PTR(pPkeRespMsg->opaque_data, pReqData);

    /* extract fields from request data struct */
    pCbFunc = pReqData->cbInfo.cbFunc;
    cbData = pReqData->cbInfo.cbData;
    instanceHandle = pReqData->cbInfo.instanceHandle;

    SAL_MISC_ERR_STATS_INC(
        comnErr,
        &((sal_crypto_service_t *)instanceHandle)->generic_service_info);
    /* destroy the request */
    requestHandle = (lac_pke_request_handle_t)pReqData->pHeadReqData;
    status = LacPke_DestroyRequest(&requestHandle);

    if (isFwUnSupp)
    {
        status = CPA_STATUS_UNSUPPORTED;
    }

    /* call the client callback */
    (*pCbFunc)(status, pass, instanceHandle, &cbData);
}

/**
 ***************************************************************************
 * @ingroup LacAsymCommonQatComms
 *      PKE dummy response generation
 ***************************************************************************/
CpaStatus LacPke_SwRespMsgCallback(lac_memblk_bucket_t *pBucket)
{
    lac_mem_blk_t **pBucketBlk = NULL;
    lac_mem_blk_t *pCurrentBlk = NULL;
    Cpa32U numBucketBlks = 0;
    Cpa32U numSwResp = 0;
    Cpa32U startIndex = 0;
    Cpa32U iter = 0;
    lac_pke_qat_req_data_t *pReqData = NULL;
    lac_pke_op_cb_func_t pCbFunc = NULL;
    lac_pke_op_cb_data_t cbData = {0};
    CpaInstanceHandle instanceHandle = CPA_INSTANCE_HANDLE_SINGLE;
    CpaStatus status = CPA_STATUS_RETRY;

    LAC_ASSERT_NOT_NULL(pBucket);
    pBucketBlk = pBucket->mem_blk;
    LAC_ASSERT_NOT_NULL(pBucketBlk);
    startIndex = pBucket->startIndex;
    numBucketBlks = pBucket->numBucketBlks;
    numSwResp = pBucket->numBlksInRing;

    for (iter = 0; iter < numSwResp; iter++)
    {
        pCurrentBlk = pBucketBlk[(startIndex + iter) % numBucketBlks];
        pReqData = (lac_pke_qat_req_data_t *)((LAC_ARCH_UINT)(pCurrentBlk) +
                                              sizeof(lac_mem_blk_t));
        LAC_LOG_DEBUG1("Asym dummy response index = %llx", pCurrentBlk->opaque);
        /* extract fields from request data struct */
        pCbFunc = pReqData->cbInfo.cbFunc;
        cbData = pReqData->cbInfo.cbData;
        instanceHandle = pReqData->cbInfo.instanceHandle;
        (*pCbFunc)(CPA_STATUS_FAIL, CPA_FALSE, instanceHandle, &cbData);
        Lac_MemPoolEntryFree(pReqData);
    }

    if (0 != numSwResp)
    {
        status = CPA_STATUS_SUCCESS;
    }
    return status;
}

/**
 ***************************************************************************
 * @ingroup LacAsymCommonQatComms
 *      Init PKE requests
 ***************************************************************************/

void LacPke_InitAsymRequest(Cpa8U *pData, CpaInstanceHandle instanceHandle)
{
    lac_pke_qat_req_data_t *pReqData = (lac_pke_qat_req_data_t *)pData;
    /* No flag update done or necessary*/
    icp_qat_fw_comn_flags_pke cmnRequestFlags = ICP_QAT_FW_COMN_FLAGS_BUILD(
        QAT_COMN_PTR_TYPE_FLAT, QAT_COMN_CD_FLD_TYPE_64BIT_ADR);
    icp_qat_fw_req_pke_mid_t *pMid = NULL;
    sal_crypto_service_t *pCryptoService =
        (sal_crypto_service_t *)instanceHandle;

    /* LWs 0-1 set as for common header */
    LacPke_HdrWrite((&(pReqData->u1.request)),
                    ICP_QAT_FW_COMN_REQ_CPM_FW_PKE,
                    cmnRequestFlags,
                    pCryptoService->generic_service_info.atEnabled);

    /* LWs 2-5 are part of PKE header, but not common header so set up
     * separately */

    /*LW2-3*/
    pReqData->u1.request.pke_hdr.cd_pars.content_desc_addr = 0;
    /* LW4-5 */
    pReqData->u1.request.pke_hdr.cd_pars.content_desc_resrvd = 0;
    pReqData->u1.request.pke_hdr.cd_pars.func_id = 0;

    /*
     * Common request middle part (LW 6-11)
     * */
    pMid = &(pReqData->u1.request.pke_mid);

    LAC_MEM_SHARED_WRITE_FROM_PTR(pMid->opaque_data, pReqData);

    pMid->src_data_addr = LAC_OS_VIRT_TO_PHYS_INTERNAL(
        &pCryptoService->generic_service_info, &pReqData->u2.inArgList);

    pMid->dest_data_addr = LAC_OS_VIRT_TO_PHYS_INTERNAL(
        &pCryptoService->generic_service_info, &pReqData->u3.outArgList);

    /* part of LW12 */
    pReqData->u1.request.resrvd1 = 0;

    /* LW13 */
    pReqData->u1.request.resrvd2 = 0;

    /* LW14-15 */
    pReqData->u1.request.next_req_adr = 0;
}

/**
 ***************************************************************************
 * @ingroup LacAsymCommonQatComms
 *      PKE EcMontEdwds request creation
 ***************************************************************************/
CpaStatus LacPkeEcMontEdwds_CreateRequest(
    lac_pke_request_handle_t *pRequestHandle,
    Cpa32U functionalityId,
    icp_qat_fw_mmp_input_param_t *pInArgList,
    icp_qat_fw_mmp_output_param_t *pOutArgList,
    lac_pke_op_cb_func_t pPkeOpCbFunc,
    lac_pke_op_cb_data_t *pCbData,
    CpaInstanceHandle instanceHandle)
{
    CpaStatus status = CPA_STATUS_SUCCESS;
    lac_pke_qat_req_data_t *pReqData;
    sal_crypto_service_t *pCryptoService =
        (sal_crypto_service_t *)instanceHandle;

    /* Allocate request data */
    do
    {
        pReqData = Lac_MemPoolEntryAlloc(pCryptoService->lac_pke_req_pool);
        if ((NULL == pReqData))
        {
            LAC_LOG_ERROR("Cannot get a mem pool entry");
            status = CPA_STATUS_RESOURCE;
        }
        else if ((void *)CPA_STATUS_RETRY == pReqData)
        {
            osalYield();
        }
    } while ((void *)CPA_STATUS_RETRY == pReqData);

    if (CPA_STATUS_SUCCESS == status)
    {
        LAC_ASSERT_NOT_NULL(pReqData);

        /* Ensure correct request structure alignment */
        LAC_ASSERT(LAC_ADDRESS_ALIGNED(&pReqData->u1.request,
                                       LAC_OPTIMAL_ALIGNMENT_SHIFT),
                   "request structure not correctly aligned");

        /* Ensure correct input argument list structure alignment */
        LAC_ASSERT(LAC_ADDRESS_ALIGNED(&pReqData->u2.inArgList,
                                       LAC_OPTIMAL_ALIGNMENT_SHIFT),
                   "inArgList structure not correctly aligned");

        /* Ensure correct output argument list structure alignment */
        LAC_ASSERT(LAC_ADDRESS_ALIGNED(&pReqData->u3.outArgList,
                                       LAC_OPTIMAL_ALIGNMENT_SHIFT),
                   "outArgList structure not correctly aligned");


        /* Clear the previous param info */
        LAC_OS_BZERO(&pReqData->paramInfo, sizeof(pReqData->paramInfo));

        /* Initialize handle for single request, or first in a chain */
        if (*pRequestHandle == LAC_PKE_INVALID_HANDLE)
        {
            /* Store request data pointer in the request handle */
            *pRequestHandle = (lac_pke_request_handle_t)pReqData;

            /* Initialize next, head, and tail request data pointers */
            pReqData->pNextReqData = NULL;
            pReqData->pHeadReqData = pReqData;

            /* Note: tail pointer is only valid in head request data struct */
            pReqData->pTailReqData = pReqData;
        }
        else /* Handle second or subsequent request in a chain */
        {
            lac_pke_qat_req_data_t *pHeadReqData = NULL;
            lac_pke_qat_req_data_t *pTailReqData = NULL;

            /* Extract head request data pointer from the request handle */
            pHeadReqData = *pRequestHandle;
            LAC_ASSERT_NOT_NULL(pHeadReqData);

            /* Get tail request data pointer from head request data pointer */
            pTailReqData = pHeadReqData->pTailReqData;
            LAC_ASSERT_NOT_NULL(pTailReqData);

            /* Chain the two requests */
            pTailReqData->u1.request.next_req_adr =
                LAC_OS_VIRT_TO_PHYS_INTERNAL(
                    &pCryptoService->generic_service_info, pReqData);

            /* Chain the request data structures */
            pTailReqData->pNextReqData = pReqData;
            pHeadReqData->pTailReqData = pReqData;
            pReqData->pNextReqData = NULL;
            pReqData->pHeadReqData = pHeadReqData;
            /* Note: tail pointer not stored here as it changes (unlike head) */
        }

        /* Populate request data structure */
        pReqData->cbInfo.cbFunc = pPkeOpCbFunc;
        pReqData->cbInfo.cbData = *pCbData;
        pReqData->cbInfo.instanceHandle = instanceHandle;
        pReqData->pNextReqData = NULL;
    }

    if (CPA_STATUS_SUCCESS == status)
    {
        Cpa8U i = 0;

        /* Set functionality ID */
        pReqData->u1.request.pke_hdr.cd_pars.func_id = functionalityId;

        /* LWs 14 and 15 set to zero for this request for now */
        pReqData->u1.request.next_req_adr = 0;

        /* Store correctly sized in parameters in QAT structure*/
        for (i = 0; 0 != pInArgList->flat_array[i]; i++)
        {
            /* Get user input data */
            CpaFlatBuffer *inParam = (CpaFlatBuffer *)pInArgList->flat_array[i];

            LAC_MEM_SHARED_WRITE_VIRT_TO_PHYS_PTR_EXTERNAL(
                pCryptoService->generic_service_info,
                pReqData->u2.inArgList.flat_array[i],
                inParam->pData);
        }
        /* Set number in inputs */
        pReqData->u1.request.input_param_count = i;

        /* Store correctly sized out parameters in QAT structure */
        for (i = 0; 0 != pOutArgList->flat_array[i]; i++)
        {
            /* Get user input data */
            CpaFlatBuffer *outParam =
                (CpaFlatBuffer *)pOutArgList->flat_array[i];

            LAC_MEM_SHARED_WRITE_VIRT_TO_PHYS_PTR_EXTERNAL(
                pCryptoService->generic_service_info,
                pReqData->u3.outArgList.flat_array[i],
                outParam->pData);
        }
        /* Set number in outputs */
        pReqData->u1.request.output_param_count = i;

        LAC_ASSERT(
            ((pReqData->u1.request.input_param_count +
              pReqData->u1.request.output_param_count) <= LAC_MAX_MMP_PARAMS),
            "number of input/output parameters exceeds maximum allowed");
    }

    /* Clean up in the event of an error */
    if (CPA_STATUS_SUCCESS != status)
    {
        /* Destroy the request (chain) */
        (void)LacPke_DestroyRequest(pRequestHandle);
    }

    return status;
}

/**
 ***************************************************************************
 * @ingroup LacAsymCommonQatComms
 *      PKE request creation
 ***************************************************************************/

CpaStatus LacPkeKpt_CreateRequest(lac_pke_request_handle_t *pRequestHandle,
                                  Cpa32U functionalityId,
                                  Cpa32U *pInArgSizeList,
                                  Cpa32U *pOutArgSizeList,
                                  icp_qat_fw_mmp_input_param_t *pInArgList,
                                  icp_qat_fw_mmp_output_param_t *pOutArgList,
                                  CpaBoolean *pInternalInMemList,
                                  CpaBoolean *pInternalOutMemList,
                                  lac_pke_op_cb_func_t pPkeOpCbFunc,
                                  lac_pke_op_cb_data_t *pCbData,
                                  CpaInstanceHandle instanceHandle)
{
    CpaStatus status = CPA_STATUS_SUCCESS;
    status = LacPke_CreateRequest(pRequestHandle,
                                  functionalityId,
                                  pInArgSizeList,
                                  pOutArgSizeList,
                                  pInArgList,
                                  pOutArgList,
                                  pInternalInMemList,
                                  pInternalOutMemList,
                                  pPkeOpCbFunc,
                                  pCbData,
                                  instanceHandle);

    if (CPA_STATUS_SUCCESS == status)
    {
        lac_pke_qat_req_data_t *pReqData = *pRequestHandle;
        SalQatMsg_KptFlagHdrWrite(
            &(pReqData->u1.request.pke_hdr.extended_serv_specif_flags));
    }

    return status;
}

/**
 ***************************************************************************
 * @ingroup LacAsymCommonQatComms
 *      PKE request creation
 ***************************************************************************/

CpaStatus LacPke_CreateRequest(lac_pke_request_handle_t *pRequestHandle,
                               Cpa32U functionalityId,
                               Cpa32U *pInArgSizeList,
                               Cpa32U *pOutArgSizeList,
                               icp_qat_fw_mmp_input_param_t *pInArgList,
                               icp_qat_fw_mmp_output_param_t *pOutArgList,
                               CpaBoolean *pInternalInMemList,
                               CpaBoolean *pInternalOutMemList,
                               lac_pke_op_cb_func_t pPkeOpCbFunc,
                               lac_pke_op_cb_data_t *pCbData,
                               CpaInstanceHandle instanceHandle)
{
    CpaStatus status = CPA_STATUS_SUCCESS;
    lac_pke_qat_req_data_t *pReqData = NULL;
    size_t i = 0;
    sal_crypto_service_t *pCryptoService =
        (sal_crypto_service_t *)instanceHandle;

    /* allocate request data */
    do
    {
        pReqData = Lac_MemPoolEntryAlloc(pCryptoService->lac_pke_req_pool);
        if ((NULL == pReqData))
        {
            LAC_LOG_ERROR("Cannot get a mem pool entry");
            status = CPA_STATUS_RESOURCE;
        }
        else if ((void *)CPA_STATUS_RETRY == pReqData)
        {
            osalYield();
        }
    } while ((void *)CPA_STATUS_RETRY == pReqData);

    if (CPA_STATUS_SUCCESS == status)
    {
        LAC_ASSERT_NOT_NULL(pReqData);

        /* ensure correct request structure alignment */
        LAC_ASSERT(LAC_ADDRESS_ALIGNED(&pReqData->u1.request,
                                       LAC_OPTIMAL_ALIGNMENT_SHIFT),
                   "request structure not correctly aligned");

        /* ensure correct input argument list structure alignment */
        LAC_ASSERT(LAC_ADDRESS_ALIGNED(&pReqData->u2.inArgList,
                                       LAC_OPTIMAL_ALIGNMENT_SHIFT),
                   "inArgList structure not correctly aligned");

        /* ensure correct output argument list structure alignment */
        LAC_ASSERT(LAC_ADDRESS_ALIGNED(&pReqData->u3.outArgList,
                                       LAC_OPTIMAL_ALIGNMENT_SHIFT),
                   "outArgList structure not correctly aligned");


        /* initialize handle for single request, or first in a chain */
        if (*pRequestHandle == LAC_PKE_INVALID_HANDLE)
        {
            /* store request data pointer in the request handle */
            *pRequestHandle = (lac_pke_request_handle_t)pReqData;

            /* initialize next, head, and tail request data pointers */
            pReqData->pNextReqData = NULL;
            pReqData->pHeadReqData = pReqData;
            /* note: tail pointer is only valid in head request data struct */
            pReqData->pTailReqData = pReqData;
        }
        else /* handle second or subsequent request in a chain */
        {
            lac_pke_qat_req_data_t *pHeadReqData = NULL;
            lac_pke_qat_req_data_t *pTailReqData = NULL;

            /* extract head request data pointer from the request handle */
            pHeadReqData = *pRequestHandle;
            LAC_ASSERT_NOT_NULL(pHeadReqData);

            /* get tail request data pointer from head request data pointer */
            pTailReqData = pHeadReqData->pTailReqData;
            LAC_ASSERT_NOT_NULL(pTailReqData);

            /* chain the two requests */
            pTailReqData->u1.request.next_req_adr =
                LAC_OS_VIRT_TO_PHYS_INTERNAL(
                    &pCryptoService->generic_service_info, pReqData);

            /* chain the request data structures */
            pTailReqData->pNextReqData = pReqData;
            pHeadReqData->pTailReqData = pReqData;
            pReqData->pNextReqData = NULL;
            pReqData->pHeadReqData = pHeadReqData;
            /* note: tail pointer not stored here as it changes (unlike head) */
        }

        /* populate request data structure */
        pReqData->cbInfo.cbFunc = pPkeOpCbFunc;
        pReqData->cbInfo.cbData = *pCbData;
        pReqData->cbInfo.instanceHandle = instanceHandle;
        pReqData->pNextReqData = NULL;

        /* clear the previous param info */
        LAC_OS_BZERO(&pReqData->paramInfo, sizeof(pReqData->paramInfo));

        /* if the list is passed by the user, store it in prealocated memory */
        if (NULL != pInArgSizeList)
        {
            memcpy(&pReqData->paramInfo.inArgSizeList,
                   pInArgSizeList,
                   sizeof(pReqData->paramInfo.inArgSizeList));
        }
        if (NULL != pOutArgSizeList)
        {
            memcpy(&pReqData->paramInfo.outArgSizeList,
                   pOutArgSizeList,
                   sizeof(pReqData->paramInfo.outArgSizeList));
        }

        /** @performance : the caller's input/output parameter lists are copied
           here into internal structures.  it would be more efficient, if
           possible, to have the caller populate the internal structure
           directly. */

        /* store input parameters in req struct (end if NULL encountered) */
        for (i = 0;
             (i < LAC_MAX_MMP_INPUT_PARAMS) && (0 != pInArgList->flat_array[i]);
             i++)
        {
            /* Make a copy for flat buffer from user in case it is released */
            memcpy(&pReqData->paramInfo.clientInputParams[i],
                   (void *)(LAC_ARCH_UINT)pInArgList->flat_array[i],
                   sizeof(CpaFlatBuffer));
        }
        pReqData->paramInfo.indexClientInputParams = i;

        /* store output parameters in req struct (end if NULL encountered) */
        for (i = 0; (i < LAC_MAX_MMP_OUTPUT_PARAMS) &&
                    (0 != pOutArgList->flat_array[i]);
             i++)
        {
            /* Make a copy for flat buffer from user in case it is released */
            memcpy(&pReqData->paramInfo.clientOutputParams[i],
                   (void *)(LAC_ARCH_UINT)pOutArgList->flat_array[i],
                   sizeof(CpaFlatBuffer));
        }
        pReqData->paramInfo.indexClientOutputParams = i;

        /* resize parameters */
        status = LacPke_ResizeParams(&pReqData->paramInfo,
                                     pInternalInMemList,
                                     pInternalOutMemList,
                                     instanceHandle);
    }

    if (CPA_STATUS_SUCCESS == status)
    {
        Cpa8U numInputParams = 0;
        Cpa8U numOutputParams = 0;

        pReqData->u1.request.pke_hdr.cd_pars.func_id = functionalityId;

        /* LW 14 and 15 set to zero for this request for now */
        pReqData->u1.request.next_req_adr = 0;

        /* store correctly sized in params in QAT struct
           (end if NULL encountered) */
        for (i = 0; (i < LAC_MAX_MMP_INPUT_PARAMS) &&
                    (NULL != pReqData->paramInfo.pkeInputParams[i]);
             i++)
        {
            if (CPA_TRUE == pInternalInMemList[i])
            {
                /* pkeInputParams[i] is referencing internally allocated
                   memory */
                pReqData->u2.inArgList.flat_array[i] =
                    LAC_OS_VIRT_TO_PHYS_INTERNAL(
                        &pCryptoService->generic_service_info,
                        pReqData->paramInfo.pkeInputParams[i]);
            }
            else
            {
                /* pkeInputParams[i] is referencing externally allocated
                   memory */
                LAC_MEM_SHARED_WRITE_VIRT_TO_PHYS_PTR_EXTERNAL(
                    pCryptoService->generic_service_info,
                    pReqData->u2.inArgList.flat_array[i],
                    pReqData->paramInfo.pkeInputParams[i]);
            }
        }
        numInputParams = (Cpa8U)i;

        /* store correctly sized out params in QAT struct
            (end if NULL encountered) */
        for (i = 0; (i < LAC_MAX_MMP_OUTPUT_PARAMS) &&
                    (NULL != pReqData->paramInfo.pkeOutputParams[i]);
             i++)
        {
            if ((NULL != pInternalOutMemList) &&
                (CPA_TRUE == pInternalOutMemList[i]))
            {
                pReqData->u3.outArgList.flat_array[i] =
                    LAC_OS_VIRT_TO_PHYS_INTERNAL(
                        &pCryptoService->generic_service_info,
                        pReqData->paramInfo.pkeOutputParams[i]);
            }
            else
            {
                LAC_MEM_SHARED_WRITE_VIRT_TO_PHYS_PTR_EXTERNAL(
                    pCryptoService->generic_service_info,
                    pReqData->u3.outArgList.flat_array[i],
                    pReqData->paramInfo.pkeOutputParams[i]);
            }
        }
        numOutputParams = (Cpa8U)i;

        LAC_ASSERT(((numInputParams + numOutputParams) <= LAC_MAX_MMP_PARAMS),
                   "number of input/output parameters exceeds maximum allowed");

        /* Complete LW12 */
        pReqData->u1.request.input_param_count = numInputParams;
        pReqData->u1.request.output_param_count = numOutputParams;
    }

    /* clean up in the event of an error */
    if (CPA_STATUS_SUCCESS != status)
    {
        /* destroy the request (chain) */
        (void)LacPke_DestroyRequest(pRequestHandle);
    }

    return status;
}

/**
 ***************************************************************************
 * @ingroup LacAsymCommonQatComms
 *      PKE request send to QAT
 ***************************************************************************/
CpaStatus LacPke_SendRequest(lac_pke_request_handle_t *pRequestHandle,
                             CpaInstanceHandle instanceHandle)
{
    CpaStatus status = CPA_STATUS_SUCCESS;
    lac_pke_qat_req_data_t *pHeadReqData = NULL;
    Cpa64U seq_num = ICP_ADF_INVALID_SEND_SEQ;
    sal_crypto_service_t *pCryptoService =
        (sal_crypto_service_t *)instanceHandle;

    LAC_ASSERT_NOT_NULL(pRequestHandle);

    /* extract head request data pointer from the request handle */
    pHeadReqData = *pRequestHandle;
    LAC_ASSERT_NOT_NULL(pHeadReqData);

    /* send the request (chain) */
    status = SalQatMsg_transPutMsg(pCryptoService->trans_handle_asym_tx,
                                   (void *)&(pHeadReqData->u1.request),
                                   LAC_QAT_ASYM_REQ_SZ_LW,
                                   LAC_LOG_MSG_PKE,
                                   &seq_num);

    if (CPA_STATUS_SUCCESS != status)
    {
        /* destroy the request (chain) */
        (void)LacPke_DestroyRequest(pRequestHandle);
        return status;
    }

    LAC_MEM_POOL_BLK_SET_OPAQUE(pHeadReqData, seq_num);

    return status;
}

/**
 ***************************************************************************
 * @ingroup LacAsymCommonQatComms
 *      PKE Ec_MontEdwds request create and send to QAT
 ***************************************************************************/
CpaStatus LacPkeEcMontEdwds_SendSingleRequest(
    Cpa32U functionalityId,
    icp_qat_fw_mmp_input_param_t *pInArgList,
    icp_qat_fw_mmp_output_param_t *pOutArgList,
    lac_pke_op_cb_func_t pPkeOpCbFunc,
    lac_pke_op_cb_data_t *pCbData,
    CpaInstanceHandle instanceHandle)
{
    CpaStatus status = CPA_STATUS_SUCCESS;
    lac_pke_request_handle_t requestHandle = LAC_PKE_INVALID_HANDLE;

    /* Prepare the Ec_MontEdwds request */
    status = LacPkeEcMontEdwds_CreateRequest(&requestHandle,
                                             functionalityId,
                                             pInArgList,
                                             pOutArgList,
                                             pPkeOpCbFunc,
                                             pCbData,
                                             instanceHandle);

    /* Send the request */
    if (CPA_STATUS_SUCCESS == status)
        status = LacPke_SendRequest(&requestHandle, instanceHandle);

    return status;
}

/**
 ***************************************************************************
 * @ingroup LacAsymCommonQatComms
 *      PKE request create and send to QAT
 ***************************************************************************/
CpaStatus LacPkeKpt_SendSingleRequest(
    Cpa32U functionalityId,
    Cpa32U *pInArgSizeList,
    Cpa32U *pOutArgSizeList,
    icp_qat_fw_mmp_input_param_t *pInArgList,
    icp_qat_fw_mmp_output_param_t *pOutArgList,
    CpaBoolean *pInMemBool,
    CpaBoolean *pOutMemBool,
    lac_pke_op_cb_func_t pPkeOpCbFunc,
    lac_pke_op_cb_data_t *pCbData,
    CpaInstanceHandle instanceHandle)
{
    CpaStatus status = CPA_STATUS_SUCCESS;
    lac_pke_request_handle_t requestHandle = LAC_PKE_INVALID_HANDLE;

    /* prepare the request */
    status = LacPkeKpt_CreateRequest(&requestHandle,
                                     functionalityId,
                                     pInArgSizeList,
                                     pOutArgSizeList,
                                     pInArgList,
                                     pOutArgList,
                                     pInMemBool,
                                     pOutMemBool,
                                     pPkeOpCbFunc,
                                     pCbData,
                                     instanceHandle);

    if (CPA_STATUS_SUCCESS == status)
    {
        /* send the request */
        status = LacPke_SendRequest(&requestHandle, instanceHandle);
    }

    return status;
}

/**
 ***************************************************************************
 * @ingroup LacAsymCommonQatComms
 *      PKE request create and send to QAT
 ***************************************************************************/
CpaStatus LacPke_SendSingleRequest(Cpa32U functionalityId,
                                   Cpa32U *pInArgSizeList,
                                   Cpa32U *pOutArgSizeList,
                                   icp_qat_fw_mmp_input_param_t *pInArgList,
                                   icp_qat_fw_mmp_output_param_t *pOutArgList,
                                   CpaBoolean *pInMemBool,
                                   CpaBoolean *pOutMemBool,
                                   lac_pke_op_cb_func_t pPkeOpCbFunc,
                                   lac_pke_op_cb_data_t *pCbData,
                                   CpaInstanceHandle instanceHandle)
{
    CpaStatus status = CPA_STATUS_SUCCESS;
    lac_pke_request_handle_t requestHandle = LAC_PKE_INVALID_HANDLE;

    /* prepare the request */
    status = LacPke_CreateRequest(&requestHandle,
                                  functionalityId,
                                  pInArgSizeList,
                                  pOutArgSizeList,
                                  pInArgList,
                                  pOutArgList,
                                  pInMemBool,
                                  pOutMemBool,
                                  pPkeOpCbFunc,
                                  pCbData,
                                  instanceHandle);

    if (CPA_STATUS_SUCCESS == status)
    {
        /* send the request */
        status = LacPke_SendRequest(&requestHandle, instanceHandle);
    }

    return status;
}
