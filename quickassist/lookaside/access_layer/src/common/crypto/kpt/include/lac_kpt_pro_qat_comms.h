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
 ******************************************************************************
 * @file lac_kpt_pro_qat_comms.h
 *
 * Common definition that are KptProvision specific
 *
 *****************************************************************************/

#ifndef _LAC_KPT_PRO_QAT_COMMS_H_
#define _LAC_KPT_PRO_QAT_COMMS_H_

/*
*******************************************************************************
* Include public/global header files
*******************************************************************************
*/

#include "cpa.h"
#include "cpa_types.h"
#include "cpa_cy_common.h"
#include "cpa_cy_kpt.h"

/*
*******************************************************************************
* Include private header files
*******************************************************************************
*/
/* ADF include */
#include "icp_adf_transport.h"

/* QAT include */
#include "icp_qat_fw_kpt_pro.h"

#define LAC_QAT_KPT_PRO_REQ_SZ_LW (32)
#define LAC_KPT_PRO_SERVICE_TYPE (0xC)

#define KPT_DEV_IPUB_N_SIZE_IN_BYTE (384)
#define KPT_DEV_IPUB_E_SIZE_IN_BYTE (8)

#define KPT_LOAD_SWK_SIZE_IN_BYTE (384)

#define KPT_PRO_LOAD_SWK_CMD (1)
/* Load an encrypted SWK to device*/
#define KPT_PRO_DEL_SWK_CMD (2)
/* Delete a SWK */
#define KPT_PRO_QUERY_DEV_CREDENTIAL_CMD (3)
/* Query device credential (public key and signature)*/

typedef void *lac_kpt_pro_req_handle_t;

#define LAC_KPT_PRO_INVALID_HANDLE ((lac_kpt_pro_req_handle_t)0)

/**
 ******************************************************************************
 * @ingroup LacKptProQatCommon
 * @description
 *  KPT provision response client callback opdata
 *
 *****************************************************************************/
typedef struct lac_kpt_pro_op_cb_data_s
{
    const void *pClientCb;
    void *pCallbackTag;
    Cpa8U cmdID;
    Cpa8U *pVirtAddr;
    Cpa16U rspStatus;
    Cpa64U keyHandle;
} lac_kpt_pro_op_cb_data_t;

/**
 ******************************************************************************
 * @ingroup LacKptProQatCommon
 * @description
 *  KPT provision response client callback function
 *
 *****************************************************************************/
typedef void (*lac_kpt_pro_op_cb_func_t)(CpaStatus status,
                                         CpaInstanceHandle instanceHandle,
                                         lac_kpt_pro_op_cb_data_t *pCbData);
/**
 *****************************************************************************
 * @ingroup LacKptProQatCommon
 * @description
 *  KPT provision sync mode callback function definition
 *
 *****************************************************************************/
typedef void (*lac_kpt_pro_sync_cb)(void *pCallbackTag, CpaStatus status);

/**
 *****************************************************************************
 * @ingroup LacKptProQatCommon
 *
 * @description
 *     Contains the data for a kpt provision operation callback
 *
 *****************************************************************************/
typedef struct lac_kpt_pro_cb_info_s
{
    lac_kpt_pro_op_cb_func_t cbFunc;
    lac_kpt_pro_op_cb_data_t *pcbData;
    CpaInstanceHandle instanceHandle;
} lac_kpt_pro_cb_info_t;

/**
 ******************************************************************************
 * @ingroup LacKptProQatCommon
 *      Request data of KPT provision for QAT messages
 * @description
 *      This structure defines data format of KPT provision request which be
 *issued along with a crypto instances. This is used to store data which is
 *known when the message is sent and which we wish to retrieve when the response
 *message is processed.
 *
 *****************************************************************************/
typedef struct lac_kpt_pro_qat_req_data_s
{
    union lac_kpt_pro_qat_req_data_request_u {
        icp_qat_fw_kpt_pro_request_t request;
        Cpa8U padding[LAC_QAT_KPT_PRO_REQ_SZ_LW * 4];
    } u1;
    /* For asymmetric instance, only the first 64 bytes will be used */
    /* For symmetric instance, all 128 bytes are used */
    lac_kpt_pro_cb_info_t cbinfo;
} lac_kpt_pro_qat_req_data_t;

/**
 *******************************************************************************
 * @ingroup LacKptProQatCommon
 *      Sends a single (unchained) kpt provision request to the QAT.
 * @description
 *      This function takes the parameters for a KPT provison QAT request,
 * creates the request, fills in the KPT provision fields and sends it to the
 * QAT. It does not block waiting for a response. Instead the callback function
 * is invoked when the response from the QAT has been processed.
 *
 * @param[in] instanceHandle      InstanceHandle
 * @param[in] cmdID               KPT provision command id
 * @param[in] pKeyHandle          A pointer refering to swk's identity
 * @param[in] pSrc                A flatbuffer pointer refering to input data
 *                                to QAT.
 * @param[in] pDevCredential      A pointer refering KPT device credential
 * @param[out] pKptStatus         The return code.
 *
 * @retval CPA_STATUS_SUCCESS   No error
 * @retval CPA_STATUS_RESOURCE  Resource error (e.g. failed memory allocation)
 *
 *****************************************************************************/
CpaStatus LacKpt_Pro_SendRequest(CpaInstanceHandle instanceHandle,
                                 Cpa8U cmdID,
                                 CpaCyKptHandle *pKeyHandle,
                                 CpaFlatBuffer *pSrc,
                                 CpaCyKptValidationKey *pDevCredential,
                                 CpaCyKptKeyManagementStatus *pKptStatus);

/**
 ******************************************************************************
 * @ingroup LacKptProQatCommon
 *        Handler of KPT provision response
 *
 * @description
 *       This function will handle KPT provsion response, It should be called
 *       in response callback function.
 *
 * @param[in] pRespMsg          Pointer to the KPT provision response
 *
 * @retval NULL
 *****************************************************************************/
void LacKpt_Pro_RspHandler(void *pRespMsg);
#endif /* _LAC_KPT_PRO_QAT_COMMS_H_ */
