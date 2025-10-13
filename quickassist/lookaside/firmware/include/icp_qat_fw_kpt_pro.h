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
 * @file icp_qat_fw_kpt_pro.h
 * @defgroup icp_qat_fw_kpt_pro ICP QAT FW KPT Provision Processing
 *      Definitions
 * @ingroup icp_qat_fw
 * Revision: 0.1
 * @brief
 *      This file documents the external interfaces that the QAT FW running
 *      on the QAT Acceleration Engine provides to clients wanting to
 *      accelerate crypto asymmetric applications
 */

#ifndef _ICP_QAT_FW_KPT_PRO_H_
#define _ICP_QAT_FW_KPT_PRO_H_

/*
****************************************************************************
* Include local header files
****************************************************************************
*/
#include "icp_qat_fw_pke.h"

/**
 ***************************************************************************
 * @ingroup icp_qat_fw_kpt_pro
 *      Request data for QAT messages
 *
 * @description
 *      This structure defines the request data format for KPT provision
 *      messages, This is used to store data which is known when the message
 *      is send and which we wish to retrieve when the response message is
 *      processed.
 *
 **************************************************************************/
typedef struct icp_qat_fw_kpt_pro_request_s
{
    Cpa8U resrvd0;
    Cpa8U cmd_id;
    /** kpt provision command id */
    Cpa8U service_type;
    /** kpt provision service type */
    Cpa8U resrvd1:6;
    Cpa8U genx:1;
    /** 'generation' flag */
    Cpa8U valid:1;
    /** 'valid' flag */
    Cpa16U serv_specif_flags;
    Cpa8U comn_req_flags;
    Cpa8U resrvd2;
    /** physical address of eSWK, length is fixed to 384 bytes */
    Cpa64U src_addr;
    /** kpt provision key handle */
    Cpa64U key_handle;
    Cpa64U opaque_data;
    Cpa64U dst_addr;
    /** physical address of destination buffer, length is fixed to 776 bytes */
    Cpa8U resrvd3[24];
} icp_qat_fw_kpt_pro_request_t;

/**
 *****************************************************************************
 * @ingroup icp_qat_fw_kpt_pro
 *      Response data for QAT messages
 * @description
 *      Define the KPT provision response format
 *
 *****************************************************************************/
typedef struct icp_qat_fw_kpt_pro_resp_data_s
{
    Cpa8U resrvd0;
    Cpa8U cmd_id;
    /** kpt provision request command id, copied from the request
     * to the response */
    Cpa8U service_type;
    /** kpt provision response service type, copied from the
     * request to the response */
    Cpa8U resrvd1:6;
    Cpa8U genx:1;
    /** 'generation' flag */
    Cpa8U valid:1;
    /** 'valid' flag */
    icp_qat_fw_pke_resp_status_t rsp_status;
    /** kpt provision request operation result */
    Cpa16U resrvd2;
    /** kpt provision key handle */
    Cpa64U opaque_data;
    /** opaque data pointer, it's a copy of the callback data
     * passed when the request was created */
    Cpa64U key_handle;
    Cpa64U dst_addr;
    /** physical address of destination flat buffer, copied from the request to
     * the response */
} icp_qat_fw_kpt_pro_resp_data_t;
#endif /* _ICP_QAT_FW_KPT_PRO_H_ */
