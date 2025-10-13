/****************************************************************************
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
 * @file dc_ns_datapath.h
 *
 * @ingroup Dc_DataCompression
 *
 * @description
 *      Definition of the Data Compression datapath parameters.
 *
 *******************
 * **********************************************************/

/* Special values for the session handle in the cookie and the CpaDcDpOpData
 * structure. We tag a request with one of these values according to the API
 * used to submit the request. Then, when a hardware response is picked up and
 * examined by the response handler, we can tell whether the response is for
 * the Traditional NS API or the Data Plane NS API. */
#define DCNS 1
#define DCDPNS 0

void dcNsCompression_ProcessCallback(void *pRespMsg);

/**
 *****************************************************************************
 * @ingroup Dc_DataCompression
 *      Construct compression base request
 *
 * @description
 *      This function will construct a compression base request, i.e. a request
 *      that serves as the base for a Traditional API request or a Data Plane
 *      API request. The function is the NS API equivalent of dcInitSession.
 *
 * @param[out]      pMsg             Pointer to empty message
 * @param[in]       pService         Pointer to compression service
 * @param[in]       pSetupData       Pointer to (de)compression parameters
 *
 * @retval CPA_STATUS_SUCCESS        Function executed successfully
 * @retval CPA_STATUS_UNSUPPORTED    Unsupported algorithm/feature
 *****************************************************************************/

CpaStatus dcNsCreateBaseRequest(icp_qat_fw_comp_req_t *pMsg,
                                sal_compression_service_t *pService,
                                CpaDcNsSetupData *pSetupData);

/**
 *****************************************************************************
 * @ingroup Dc_DataCompression
 *      Set the cnvErrorInjection flag in sal compression service request
 *
 * @description
 *      This function enables/disable the CnVError injection for the sessionless
 *      case. All Compression requests sent are injected with CnV errors.
 *
 * @param[in]       dcInstance       Instance Handle
 * @param[in]       enableCnvErrInj  TRUE/FALSE to Enable/Disable CnV Error
 *                                   Injection
 *
 * @retval CPA_STATUS_SUCCESS        Function executed successfully
 * @retval CPA_STATUS_INVALID_PARAM  Invalid parameter passed in
 * @retval CPA_STATUS_UNSUPPORTED    Unsupported feature
 *****************************************************************************/
CpaStatus dcNsEnableCnvErrorInj(CpaInstanceHandle dcInstance,
                             CpaBoolean enableCnvErrInj);
