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
 * @file dc_datapath.h
 *
 * @ingroup Dc_DataCompression
 *
 * @description
 *      Definition of the Data Compression datapath parameters.
 *
 *******************
 * **********************************************************/
#ifndef DC_DATAPATH_H_
#define DC_DATAPATH_H_

/* Include batch and pack definitions */
#include "cpa_dc_bp.h"
#include "cpa_dc.h"
#include "dc_session.h"
#include "sal_types_compression.h"

#include "lac_mem_pools.h"

#define LAC_QAT_DC_REQ_SZ_LW 32
#define LAC_QAT_DC_RESP_SZ_LW 8

/* Restriction on the source buffer size for compression due to the firmware
 * processing */
#define DC_SRC_BUFFER_MIN_SIZE (15)

/* Restriction on the destination buffer size for compression due to
 * the management of skid buffers in the firmware */
#define DC_DEST_BUFFER_DYN_MIN_SIZE (128)
#define DC_DEST_BUFFER_STA_MIN_SIZE (64)
#define DC_DEST_BUFFER_DYN_MIN_SIZE_GEN4 (512)
#define DC_DEST_BUFFER_STA_MIN_SIZE_GEN4 (1024)
/* C62x and C3xxx pcie rev0 devices require an additional 32bytes */
#define DC_DEST_BUFFER_STA_ADDITIONAL_SIZE (32)

/* Minimum destination buffer size for decompression */
#define DC_DEST_BUFFER_DEC_MIN_SIZE (1)

/* Restriction on the source and destination buffer sizes for compression due
 * to the firmware taking 32 bits parameters. The max size is 2^32-1 */
#define DC_BUFFER_MAX_SIZE (0xFFFFFFFF)

/* DC Source & Destination buffer type (FLAT/SGL) */
#define DC_DEFAULT_QAT_PTR_TYPE QAT_COMN_PTR_TYPE_SGL
#define DC_DP_QAT_PTR_TYPE QAT_COMN_PTR_TYPE_FLAT

/* Offset to first byte of Input Byte Counter (IBC) in state register */
#define DC_STATE_IBC_OFFSET (8)
/* Size in bytes of input byte counter (IBC) in state register */
#define DC_IBC_SIZE_IN_BYTES (4)

/* Offset to first byte to CRC32 in state register */
#define DC_STATE_CRC32_OFFSET (40)
/* Offset to first byte to output CRC32 in state register */
#define DC_STATE_OUTPUT_CRC32_OFFSET (48)
/* Offset to first byte to input CRC32 in state register */
#define DC_STATE_INPUT_CRC32_OFFSET (52)

/* Offset to first byte of ADLER32 in state register */
#define DC_STATE_ADLER32_OFFSET (44)

/* 8 bit mask value */
#define DC_8_BIT_MASK (0xff)

/* 8 bit shift position */
#define DC_8_BIT_SHIFT_POS (8)

/* Size in bytes of checksum */
#define DC_CHECKSUM_SIZE_IN_BYTES (4)

/* size which needs to be reserved before the results field to
 * align the results field with the API struct  */
#define DC_API_ALIGNMENT_OFFSET (offsetof(CpaDcDpOpData, results))

/* Mask used to check the CompressAndVerify capability bit */
#define DC_CNV_EXTENDED_CAPABILITY (0x01)

/* Mask used to check the CompressAndVerifyAndRecover capability bit */
#define DC_CNVNR_EXTENDED_CAPABILITY (0x100)

/* Mask used to check the LZ4 compress programmable CRC capability bit */
#define DC_LZ4_E2E_COMP_CRC_EXTENDED_CAPABILITY (0x20000000)

/* Default values for CNV integrity checks,
 * those are used to inform hardware of specifying CRC parameters to be used
 * when calculating CRCs */
#define DC_CRC_POLY_DEFAULT 0x04c11db7
#define DC_CRC64_POLY_DEFAULT 0x42f0e1eba9ea3693ULL
#define DC_XOR_FLAGS_DEFAULT 0xe0000
#define DC_XOR_OUT_DEFAULT 0xffffffff
#define DC_XOR64_OUT_DEFAULT 0x0ULL
#define DC_DEFAULT_CRC 0x0
#define DC_DEFAULT_ADLER32 0x1
#define DC_REFLECT_IN_DEFAULT 0x0
#define DC_REFLECT_OUT_DEFAULT 0x0

/* DC Chain info in compression cookie */
typedef struct dc_chain_info_s
{
    CpaBoolean isDcChaining;
    /* True if this request is part of a DC Chain operation */
} dc_chain_info_t;

/* List of the different OpData types supported as defined in the DC API header
 * file.
 */
typedef enum dc_opdata_type_e
{
    DC_OPDATA_TYPE0 = 0,
    /**< Refer to the API definition for CpaDcOpData format */
    DC_OPDATA_TYPE1
    /**< Refer to the API definition for CpaDcOpData2 format */
} dc_opdata_type_t;

typedef struct dc_opdata_ext_s
{
    dc_opdata_type_t opDataType;
    /**< Indicates the type of OpData being used */
    void *pOpData;
    /**< Pointer to the OpData structure being used */
} dc_opdata_ext_t;

/**
*******************************************************************************
* @ingroup cpaDc Data Compression
*      Compression cookie
* @description
*      This cookie stores information for a particular compression perform op.
*      This includes various user-supplied parameters for the operation which
*      will be needed in our callback function.
*      A pointer to this cookie is stored in the opaque data field of the QAT
*      message so that it can be accessed in the asynchronous callback.
* @note
*      The order of the parameters within this structure is important. It needs
*      to match the order of the parameters in CpaDcDpOpData up to the
*      pSessionHandle. This allows the correct processing of the callback.
*****************************************************************************/
typedef struct dc_compression_cookie_s
{
    Cpa8U dcReqParamsBuffer[DC_API_ALIGNMENT_OFFSET];
    /**< Memory block  - was previously reserved for request parameters.
     * Now size maintained so following members align with API struct,
     * but no longer used for request parameters */
    CpaDcRqResults reserved;
    /**< This is reserved for results to correctly align the structure
     * to match the one from the data plane API */
    CpaInstanceHandle dcInstance;
    /**< Compression instance handle */
    CpaDcSessionHandle pSessionHandle;
    /**< Pointer to the session handle. It is either a real address or a
     * special value used to identify requests coming from the NS API. */
    icp_qat_fw_comp_req_t request;
    /**< Compression request */
    void *callbackTag;
    /**< Opaque data supplied by the client */
    dc_session_desc_t *pSessionDesc;
    /**< Pointer to the session descriptor */
    CpaDcFlush flushFlag;
    /**< Flush flag */
    CpaDcOpData *pDcOpData;
    /**< struct containing flags and CRC related data for this session */
    CpaDcRqResults *pResults;
    /**< Pointer to result buffer holding consumed and produced data */
    Cpa32U srcTotalDataLenInBytes;
    /**< Total length of the source data */
    Cpa32U dstTotalDataLenInBytes;
    /**< Total length of the destination data */
    dc_request_dir_t compDecomp;
    /**< Used to know whether the request is compression or decompression.
     * Useful when defining the session as combined */
#ifdef ICP_DC_ERROR_SIMULATION
    CpaDcReqStatus dcErrorToSimulate;
/**< Dc error inject simulation */
#endif
    CpaBufferList *pUserSrcBuff;
    /**< virtual userspace ptr to source SGL */
    CpaBufferList *pUserDestBuff;
    /**< virtual userspace ptr to destination SGL */
    CpaDcCallbackFn pCbFunc;
    /**< Callback function defined for the traditional sessionless API */
    CpaDcChecksum checksumType;
    /**< Type of checksum */
    dc_integrity_crc_fw_t dataIntegrityCrcs;
    /**< Data integrity table */
    dc_chain_info_t dcChain;
    /**< DC Chain info if DC used as part of a DC Chain operation. */
} dc_compression_cookie_t;

/**
 *****************************************************************************
 * @ingroup Dc_DataCompression
 *      Callback function called for compression and decompression requests in
 *      synchronous and asynchronous modes
 *
 * @description
 *      Called to process compression and decompression response messages. This
 *      callback will check for errors, update the statistics and will call the
 *      user callback
 *
 * @param[in]   pRespMsg        Response message
 * @param[in]   pResults        Pointer to DC results structure
 *
 * @retval CPA_STATUS_SUCCESS       The request completed successfully.
 * @retval CPA_STATUS_UNSUPPORTED   The request was rejected by the HW as an
 *                                  unsupported configuration.
 * @retval CPA_STATUS_FAIL          The request did not complete successfully.
 *                                  The client should examine the 
 *                                  pResults->status for more information on
 *                                  what went wrong.
 *****************************************************************************/
CpaStatus dcCompression_CommonProcessCallback(
    icp_qat_fw_comp_resp_t *pCompRespMsg,
    CpaDcRqResults *pResults);

/**
 *****************************************************************************
 * @ingroup Dc_DataCompression
 *      Callback function called for compression and decompression requests in
 *      synchronous and asynchronous modes
 *
 * @description
 *      Called to process compression and decompression response messages. This
 *      callback will call dcCompression_CommonProcessCallback for standalone
 *      DC callback handling. For DC Chaining callback handling it will call
 *      dcChainProcessResults.
 *
 * @param[in]   pRespMsg        Response message
 *
 *****************************************************************************/
void dcCompression_ProcessCallback(void *pRespMsg);

/**
 ***************************************************************************
 * @ingroup Dc_DataCompression
 *      Generates DC dummy response
 *
 * @description
 *      This function is called during the error state of the device to
 * generate dummy responses from the DC request memory pool.
 *
 * @param[in] pBucket               pointer to the bucket of memblks
 *
 * @retval CPA_STATUS_SUCCESS       Successfully polled a memory pool with data
 *                                  that generate dummy responses.
 * @retval CPA_STATUS_RETRY         There are no inflight requests in the
 *                                  memory pool associated with the instance
 *
 ***************************************************************************/
CpaStatus dcCompression_SwRespMsgCallback(lac_memblk_bucket_t *pBucket);

#ifdef ICP_PARAM_CHECK
CpaStatus dcCheckOpData(sal_compression_service_t *pService,
                        CpaDcOpData *pOpData);
#endif

/**
*****************************************************************************
* @ingroup Dc_DataCompression
*      Describes CNV and CNVNR modes
*
* @description
*      This enum is used to indicate the CNV modes.
*
*****************************************************************************/
typedef enum dc_cnv_mode_s
{
    DC_NO_CNV = 0,
    /* CNV = FALSE, CNVNR = FALSE */
    DC_CNV,
    /* CNV = TRUE, CNVNR = FALSE */
    DC_CNVNR,
    /* CNV = TRUE, CNVNR = TRUE */
} dc_cnv_mode_t;

/**
 *****************************************************************************
 * @ingroup Dc_DataCompression
 *      Create the requests for compression or decompression
 *
 * @description
 *      Create the requests for compression or decompression. This function
 *      will update the cookie will all required information.
 *
 * @param[out]  pCookie             Pointer to the compression cookie
 * @param[in]   pService            Pointer to the compression service
 * @param[in]   pSessionDesc        Pointer to the session descriptor
 * @param[in]   pSessionHandle      Session handle
 * @param[in]   pSrcBuff            Pointer to data buffer for compression
 * @param[in]   pDestBuff           Pointer to buffer space for data after
 *                                  compression
 * @param[in]   pResults            Pointer to results structure
 * @param[in]   flushFlag           Indicates the type of flush to be
 *                                  performed
 * @param[in]   pOpData             Pointer to request information structure
 *                                  holding parameters for cpaDcCompress2
 *                                  and CpaDcDecompressData2
 * @param[in]   callbackTag         Pointer to the callback tag
 * @param[in]   compDecomp          Direction of the operation
 * @param[in]   cnvMode             CNV Mode
 *
 * @retval CPA_STATUS_SUCCESS       Function executed successfully
 * @retval CPA_STATUS_INVALID_PARAM Invalid parameter passed in
 *
 *****************************************************************************/
CpaStatus dcCreateRequest(dc_compression_cookie_t *pCookie,
                          sal_compression_service_t *pService,
                          dc_session_desc_t *pSessionDesc,
                          CpaDcSessionHandle pSessionHandle,
                          CpaBufferList *pSrcBuff,
                          CpaBufferList *pDestBuff,
                          CpaDcRqResults *pResults,
                          CpaDcFlush flushFlag,
                          CpaDcOpData *pOpData,
                          void *callbackTag,
                          dc_request_dir_t compDecomp,
                          dc_cnv_mode_t cnvMode);
#endif /* DC_DATAPATH_H_ */
