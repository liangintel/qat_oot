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
 * @file dc_chain.c
 *
 * @ingroup Dc_Chaining
 *
 * @description
 *      Implementation of the chaining session operations.
 *
 *****************************************************************************/

/*
 *******************************************************************************
 * Include public/global header files
 *******************************************************************************
 */
#ifndef ICP_DC_ONLY
#include "cpa.h"

#include "icp_qat_fw.h"
#include "icp_qat_fw_comp.h"
#include "icp_qat_hw.h"

/*
 *******************************************************************************
 * Include private header files
 *******************************************************************************
 */
#include "dc_chain.h"
#include "dc_datapath.h"
#include "dc_stats.h"
#include "lac_mem_pools.h"
#include "lac_buffer_desc.h"
#include "sal_service_state.h"
#include "sal_qat_cmn_msg.h"
#include "lac_sym_qat_hash_defs_lookup.h"
#include "sal_string_parse.h"
#include "lac_sym.h"
#include "lac_session.h"
#include "lac_sym_qat.h"
#include "lac_sym_hash.h"
#include "lac_sym_alg_chain.h"
#include "lac_sym_auth_enc.h"
#include "sal_hw_gen.h"


static const dc_chain_cmd_tbl_t dc_chain_cmd_table[] = {
    /* link0: additional=2(hash)|dir=0(rsvd)|type=1(crypto)
     * link1: additional=0(static)|dir=0(compression)|type=0(comp)
     */
    { 0x201,
      0x0,
      0x0,
      ICP_QAT_FW_CHAINING_CMD_HASH_STATIC_COMP,
      ICP_QAT_FW_CHAINING_20_CMD_HASH_COMPRESS },
    /* link0: additional=2(hash)|dir=0(rsvd)|type=1(crypto)
     * link1: additional=2(dynamic )|dir=0(compression)|type=0(comp)
     */
    { 0x201,
      0x200,
      0x0,
      ICP_QAT_FW_CHAINING_CMD_HASH_DYNAMIC_COMP,
      ICP_QAT_FW_CHAINING_20_CMD_HASH_COMPRESS },
    /* link0: additional=2(dynamic)|dir=0(compression)|type=0(comp)
     * link1: additional=3(algChain)|dir=1(encrypt)|type=1(crypto)
     */
    { 0x200,
      0x311,
      0x0,
      ICP_QAT_FW_NO_CHAINING,
      ICP_QAT_FW_CHAINING_20_CMD_COMPRESS_ENCRYPT },
    /* link0: additional=3(algChain)|dir=2(decrypt)|type=1(crypto)
     * link1: additional=0(rsvd)|dir=1(decomp)|type=0(comp)
     */
    { 0x321,
      0x10,
      0x0,
      ICP_QAT_FW_NO_CHAINING,
      ICP_QAT_FW_CHAINING_20_CMD_DECRYPT_DECOMPRESS },
};

#ifdef ICP_PARAM_CHECK
#define NUM_OF_SESSION_SUPPORT 2

/**
 *****************************************************************************
 * @ingroup Dc_Chaining
 *      Check that chaining session data is valid
 *
 * @description
 *      Check that all the parameters defined in the pSessionData are valid
 *
 * @param[in]       operation        Chaining opration
 * @param[in]       numSessions      Number of chaining sessions
 * @param[in]       pSessionData     Pointer to an array of
 *                                   CpaDcChainSessionSetupData
 *                                   structures.There should be numSessions
 *                                   entries in the array.
 *
 * @retval CPA_STATUS_SUCCESS        Function executed successfully
 * @retval CPA_STATUS_INVALID_PARAM  Invalid parameter passed in
 *
 *****************************************************************************/
STATIC CpaStatus
dcChainSession_CheckSessionData(CpaDcChainOperations operation,
                                Cpa8U numSessions,
                                const CpaDcChainSessionSetupData *pSessionData)
{
    CpaCySymHashSetupData const *pHashSetupData = NULL;
    CpaCySymCipherSetupData const *pCipherSetupData = NULL;
    CpaDcSessionSetupData *pDcSetupData = NULL;
    CpaCySymSessionSetupData *pCySetupData = NULL;

    /* Currently all supported chaining operations have
     * exactly NUM_OF_SESSION_SUPPORT sessions */
    LAC_CHECK_STATEMENT_LOG(numSessions != NUM_OF_SESSION_SUPPORT,
                            "%s",
                            "Wrong number of sessions "
                            "for a chaining operation");

    switch (operation)
    {
        case CPA_DC_CHAIN_HASH_THEN_COMPRESS:
            pCySetupData = pSessionData[0].pCySetupData;
            pDcSetupData = pSessionData[1].pDcSetupData;

            LAC_CHECK_NULL_PARAM(pCySetupData);
            LAC_CHECK_NULL_PARAM(pDcSetupData);
            pHashSetupData = &(pCySetupData->hashSetupData);
            LAC_CHECK_NULL_PARAM(pHashSetupData);

            /* Check for valid/supported DC Chain parameters */
            LAC_CHECK_STATEMENT_LOG(
                (pSessionData[0].sessType != CPA_DC_CHAIN_SYMMETRIC_CRYPTO),
                "Invalid Chain cySessType=0x%x for HASH_THEN_COMPRESS chaining",
                pSessionData[0].sessType);

            LAC_CHECK_STATEMENT_LOG(
                (pSessionData[1].sessType != CPA_DC_CHAIN_COMPRESS_DECOMPRESS),
                "Invalid Chain dcSessType=0x%x for HASH_THEN_COMPRESS chaining",
                pSessionData[1].sessType);

            /* Check for valid/supported Symmetric Crypto parameters */
            LAC_CHECK_STATEMENT_LOG(
                (pCySetupData->symOperation != CPA_CY_SYM_OP_HASH),
                "Invalid CY symOperation=0x%x for HASH_THEN_COMPRESS chaining",
                pCySetupData->symOperation);

            LAC_CHECK_STATEMENT_LOG(
                (pCySetupData->digestIsAppended == CPA_TRUE),
                "Invalid CY digestIsAppended=0x%x for HASH_THEN_COMPRESS "
                "chaining",
                pCySetupData->digestIsAppended);

            LAC_CHECK_STATEMENT_LOG(
                (pHashSetupData->hashMode == CPA_CY_SYM_HASH_MODE_NESTED),
                "Invalid CY hashMode=0x%x for HASH_THEN_COMPRESS chaining",
                pHashSetupData->hashMode);

            LAC_CHECK_STATEMENT_LOG(
                ((pHashSetupData->hashAlgorithm != CPA_CY_SYM_HASH_SHA1) &&
                 (pHashSetupData->hashAlgorithm != CPA_CY_SYM_HASH_SHA224) &&
                 (pHashSetupData->hashAlgorithm != CPA_CY_SYM_HASH_SHA256)),
                "Invalid CY hashAlgorithm=0x%x for HASH_THEN_COMPRESS chaining",
                pHashSetupData->hashAlgorithm);

            /* Check for valid/supported DC parameters */
            LAC_CHECK_STATEMENT_LOG(
                (pDcSetupData->sessDirection != CPA_DC_DIR_COMPRESS),
                "Invalid DC sessDirection=0x%x for HASH_THEN_COMPRESS chaining",
                pDcSetupData->sessDirection);

            LAC_CHECK_STATEMENT_LOG(
                (pDcSetupData->huffType == CPA_DC_HT_PRECOMP),
                "Invalid DC huffType=0x%x for HASH_THEN_COMPRESS chaining",
                pDcSetupData->huffType);

            LAC_CHECK_STATEMENT_LOG(
                (pDcSetupData->sessState != CPA_DC_STATELESS),
                "Invalid DC sessState=0x%x for HASH_THEN_COMPRESS chaining",
                pDcSetupData->sessState);

            break;
        case CPA_DC_CHAIN_COMPRESS_THEN_AEAD:
            pDcSetupData = pSessionData[0].pDcSetupData;
            pCySetupData = pSessionData[1].pCySetupData;

            LAC_CHECK_NULL_PARAM(pDcSetupData);
            LAC_CHECK_NULL_PARAM(pCySetupData);
            pCipherSetupData = &(pCySetupData->cipherSetupData);
            LAC_CHECK_NULL_PARAM(pCipherSetupData);

            /* Check for valid/supported DC Chain parameters */
            LAC_CHECK_STATEMENT_LOG(
                (pSessionData[0].sessType != CPA_DC_CHAIN_COMPRESS_DECOMPRESS),
                "Invalid Chain dcSessType=0x%x for COMPRESS_THEN_AEAD chaining",
                pSessionData[0].sessType);

            LAC_CHECK_STATEMENT_LOG(
                (pSessionData[1].sessType != CPA_DC_CHAIN_SYMMETRIC_CRYPTO),
                "Invalid Chain cySessType=0x%x for COMPRESS_THEN_AEAD chaining",
                pSessionData[1].sessType);

            /* Check for valid/supported DC parameters */
            LAC_CHECK_STATEMENT_LOG(
                (pDcSetupData->sessDirection != CPA_DC_DIR_COMPRESS),
                "Invalid DC sessDirection=0x%x for COMPRESS_THEN_AEAD chaining",
                pDcSetupData->sessDirection);

            LAC_CHECK_STATEMENT_LOG(
                (pDcSetupData->huffType != CPA_DC_HT_FULL_DYNAMIC),
                "Invalid DC huffType=0x%x for COMPRESS_THEN_AEAD chaining",
                pDcSetupData->huffType);

            LAC_CHECK_STATEMENT_LOG(
                (pDcSetupData->sessState != CPA_DC_STATELESS),
                "Invalid DC sessState=0x%x for COMPRESS_THEN_AEAD chaining",
                pDcSetupData->sessState);

            LAC_CHECK_STATEMENT_LOG(
                (pDcSetupData->compType != CPA_DC_DEFLATE),
                "Invalid DC compType=0x%x for COMPRESS_THEN_AEAD chaining",
                pDcSetupData->compType);

            LAC_CHECK_STATEMENT_LOG((pDcSetupData->autoSelectBestHuffmanTree !=
                                     CPA_DC_ASB_DISABLED),
                                    "Invalid DC autoSelectBestHuffmanTree=0x%x "
                                    "for COMPRESS_THEN_AEAD chaining",
                                    pDcSetupData->autoSelectBestHuffmanTree);

            /* Check for valid/supported Symmetric Crypto parameters */
            LAC_CHECK_STATEMENT_LOG(
                (pCySetupData->symOperation !=
                 CPA_CY_SYM_OP_ALGORITHM_CHAINING),
                "Invalid CY symOperation=0x%x for COMPRESS_THEN_AEAD chaining",
                pCySetupData->symOperation);

            LAC_CHECK_STATEMENT_LOG((pCipherSetupData->cipherDirection !=
                                     CPA_CY_SYM_CIPHER_DIRECTION_ENCRYPT),
                                    "Invalid CY cipherDirection=0x%x for "
                                    "COMPRESS_THEN_AEAD chaining",
                                    pCipherSetupData->cipherDirection);

            LAC_CHECK_STATEMENT_LOG((pCipherSetupData->cipherAlgorithm !=
                                     CPA_CY_SYM_CIPHER_AES_GCM),
                                    "Invalid CY cipherAlgorithm=0x%x for "
                                    "COMPRESS_THEN_AEAD chaining",
                                    pCipherSetupData->cipherAlgorithm);

            LAC_CHECK_STATEMENT_LOG(
                (pCySetupData->algChainOrder !=
                 CPA_CY_SYM_ALG_CHAIN_ORDER_CIPHER_THEN_HASH),
                "Invalid CY algChainOrder=0x%x for COMPRESS_THEN_AEAD chaining",
                pCySetupData->algChainOrder);

            break;
        case CPA_DC_CHAIN_AEAD_THEN_DECOMPRESS:
            pCySetupData = pSessionData[0].pCySetupData;
            pDcSetupData = pSessionData[1].pDcSetupData;

            LAC_CHECK_NULL_PARAM(pCySetupData);
            LAC_CHECK_NULL_PARAM(pDcSetupData);
            pCipherSetupData = &(pCySetupData->cipherSetupData);
            LAC_CHECK_NULL_PARAM(pCipherSetupData);

            /* Check for valid/supported DC Chain parameters */
            LAC_CHECK_STATEMENT_LOG(
                (pSessionData[0].sessType != CPA_DC_CHAIN_SYMMETRIC_CRYPTO),
                "Invalid Chain cySessType=0x%x for AEAD_THEN_DECOMPRESS "
                "chaining",
                pSessionData[0].sessType);

            LAC_CHECK_STATEMENT_LOG(
                (pSessionData[1].sessType != CPA_DC_CHAIN_COMPRESS_DECOMPRESS),
                "Invalid Chain dcSessType=0x%x for AEAD_THEN_DECOMPRESS "
                "chaining",
                pSessionData[1].sessType);

            /* Check for valid/supported Symmetric Crypto parameters */
            LAC_CHECK_STATEMENT_LOG((pCySetupData->symOperation !=
                                     CPA_CY_SYM_OP_ALGORITHM_CHAINING),
                                    "Invalid CY symOperation=0x%x for "
                                    "AEAD_THEN_DECOMPRESS chaining",
                                    pCySetupData->symOperation);

            LAC_CHECK_STATEMENT_LOG((pCipherSetupData->cipherDirection !=
                                     CPA_CY_SYM_CIPHER_DIRECTION_DECRYPT),
                                    "Invalid CY cipherDirection=0x%x for "
                                    "AEAD_THEN_DECOMPRESS chaining",
                                    pCipherSetupData->cipherDirection);

            LAC_CHECK_STATEMENT_LOG((pCipherSetupData->cipherAlgorithm !=
                                     CPA_CY_SYM_CIPHER_AES_GCM),
                                    "Invalid CY cipherAlgorithm=0x%x for "
                                    "AEAD_THEN_DECOMPRESS chaining",
                                    pCipherSetupData->cipherAlgorithm);

            LAC_CHECK_STATEMENT_LOG(
                (pCySetupData->algChainOrder !=
                 CPA_CY_SYM_ALG_CHAIN_ORDER_HASH_THEN_CIPHER),
                "Invalid CY algChainOrder=0x%x for AEAD_THEN_DECOMPRESS "
                "chaining",
                pCySetupData->algChainOrder);

            /* Check for valid/supported DC parameters */
            LAC_CHECK_STATEMENT_LOG(
                (pDcSetupData->sessDirection != CPA_DC_DIR_DECOMPRESS),
                "Invalid DC sessDirection=0x%x for AEAD_THEN_DECOMPRESS "
                "chaining",
                pDcSetupData->sessDirection);

            LAC_CHECK_STATEMENT_LOG(
                (pDcSetupData->sessState != CPA_DC_STATELESS),
                "Invalid DC sessState=0x%x for AEAD_THEN_DECOMPRESS chaining",
                pDcSetupData->sessState);

            break;
        default:
            LAC_INVALID_PARAM_LOG1("Unsupported DC Chain operation=0x%x",
                                   operation);
            return CPA_STATUS_INVALID_PARAM;
    }

    return CPA_STATUS_SUCCESS;
}

/**
 *****************************************************************************
 * @ingroup Dc_Chaining
 *      Initialization for chaining sessions
 *
 * @description
 *      Check for supported Sym Crypto operations as part of DC Chain
 *
 * @param[in]    pCySessDesc         Session descriptor for Sym Crypto
 *                                   operation.
 * @param[in]    operation           Chaining operation
 *
 * @retval CPA_STATUS_SUCCESS        Function executed successfully
 * @retval CPA_STATUS_INVALID_PARAM  Invalid Sym Crypto parameter
 *
 *****************************************************************************/
STATIC CpaStatus
dcChainSession_CheckCySessDesc(const lac_session_desc_t *pCySessDesc,
                               CpaDcChainOperations operation)
{
    LAC_CHECK_NULL_PARAM(pCySessDesc);

    /* Restrict DC Chain Sym Crypto operations to supported/validated
     * combinations when used as part of a chain operation */
    switch (operation)
    {
        case CPA_DC_CHAIN_HASH_THEN_COMPRESS:
            LAC_CHECK_STATEMENT_LOG(
                (CPA_CY_SYM_OP_HASH != pCySessDesc->symOperation),
                "Invalid CY symOperation=0x%x for HASH_THEN_COMPRESS chaining",
                pCySessDesc->symOperation);

            LAC_CHECK_STATEMENT_LOG(
                ((CPA_CY_SYM_HASH_MODE_PLAIN != pCySessDesc->hashMode) &&
                 (CPA_CY_SYM_HASH_MODE_AUTH != pCySessDesc->hashMode)),
                "Invalid CY hashMode=0x%x for HASH_THEN_COMPRESS chaining",
                pCySessDesc->hashMode);

            LAC_CHECK_STATEMENT_LOG((CPA_FALSE != pCySessDesc->isAuthEncryptOp),
                                    "Invalid CY isAuthEncryptOp=0x%x for "
                                    "HASH_THEN_COMPRESS chaining",
                                    pCySessDesc->isAuthEncryptOp);

            LAC_CHECK_STATEMENT_LOG(
                (CPA_CY_SYM_HASH_SHA1 != pCySessDesc->hashAlgorithm &&
                 CPA_CY_SYM_HASH_SHA224 != pCySessDesc->hashAlgorithm &&
                 CPA_CY_SYM_HASH_SHA256 != pCySessDesc->hashAlgorithm),
                "Invalid CY hashAlgorithm=0x%x for HASH_THEN_COMPRESS chaining",
                pCySessDesc->hashAlgorithm);

            LAC_CHECK_STATEMENT_LOG(
                (ICP_QAT_FW_LA_CMD_AUTH != pCySessDesc->laCmdId),
                "Invalid CY laCmdId=0x%x for HASH_THEN_COMPRESS chaining",
                pCySessDesc->laCmdId);

            LAC_CHECK_STATEMENT_LOG(
                (CPA_FALSE != pCySessDesc->isCipher),
                "Invalid CY isCipher=0x%x for HASH_THEN_COMPRESS chaining",
                pCySessDesc->isCipher);

            LAC_CHECK_STATEMENT_LOG(
                (CPA_TRUE != pCySessDesc->isAuth),
                "Invalid CY isAuth=0x%x for HASH_THEN_COMPRESS chaining",
                pCySessDesc->isAuth);

            break;
        case CPA_DC_CHAIN_COMPRESS_THEN_AEAD:
            LAC_CHECK_STATEMENT_LOG(
                ((CPA_CY_SYM_OP_ALGORITHM_CHAINING !=
                  pCySessDesc->symOperation) &&
                 (CPA_CY_SYM_OP_CIPHER != pCySessDesc->symOperation)),
                "Invalid CY symOperation=0x%x for COMPRESS_THEN_AEAD chaining",
                pCySessDesc->symOperation);

            LAC_CHECK_STATEMENT_LOG(
                (CPA_CY_SYM_CIPHER_AES_GCM != pCySessDesc->cipherAlgorithm),
                "Invalid CY cipherAlgorithm=0x%x for COMPRESS_THEN_AEAD "
                "chaining",
                pCySessDesc->cipherAlgorithm);

            LAC_CHECK_STATEMENT_LOG((CPA_CY_SYM_CIPHER_DIRECTION_ENCRYPT !=
                                     pCySessDesc->cipherDirection),
                                    "Invalid CY cipherDirection=0x%x for "
                                    "COMPRESS_THEN_AEAD chaining",
                                    pCySessDesc->cipherDirection);

            LAC_CHECK_STATEMENT_LOG(
                (CPA_CY_SYM_HASH_AES_GCM != pCySessDesc->hashAlgorithm),
                "Invalid CY hashAlgorithm=0x%x for COMPRESS_THEN_AEAD chaining",
                pCySessDesc->hashAlgorithm);

            LAC_CHECK_STATEMENT_LOG(
                (CPA_CY_SYM_HASH_MODE_AUTH != pCySessDesc->hashMode),
                "Invalid CY hashMode=0x%x for COMPRESS_THEN_AEAD chaining",
                pCySessDesc->hashMode);

            break;
        case CPA_DC_CHAIN_AEAD_THEN_DECOMPRESS:
            LAC_CHECK_STATEMENT_LOG(
                ((CPA_CY_SYM_OP_ALGORITHM_CHAINING !=
                  pCySessDesc->symOperation) &&
                 (CPA_CY_SYM_OP_CIPHER != pCySessDesc->symOperation)),
                "Invalid CY symOperation=0x%x for AEAD_THEN_DECOMPRESS "
                "chaining",
                pCySessDesc->symOperation);

            LAC_CHECK_STATEMENT_LOG(
                (CPA_CY_SYM_CIPHER_AES_GCM != pCySessDesc->cipherAlgorithm),
                "Invalid CY cipherAlgorithm=0x%x for AEAD_THEN_DECOMPRESS "
                "chaining",
                pCySessDesc->cipherAlgorithm);

            LAC_CHECK_STATEMENT_LOG((CPA_CY_SYM_CIPHER_DIRECTION_DECRYPT !=
                                     pCySessDesc->cipherDirection),
                                    "Invalid CY cipherDirection=0x%x for "
                                    "AEAD_THEN_DECOMPRESS chaining",
                                    pCySessDesc->cipherDirection);

            LAC_CHECK_STATEMENT_LOG(
                (CPA_CY_SYM_HASH_AES_GCM != pCySessDesc->hashAlgorithm),
                "Invalid CY hashAlgorithm=0x%x for AEAD_THEN_DECOMPRESS "
                "chaining",
                pCySessDesc->hashAlgorithm);

            LAC_CHECK_STATEMENT_LOG(
                (CPA_CY_SYM_HASH_MODE_AUTH != pCySessDesc->hashMode),
                "Invalid CY hashMode=0x%x for AEAD_THEN_DECOMPRESS chaining",
                pCySessDesc->hashMode);

            break;
        default:
            LAC_INVALID_PARAM_LOG1("Unsupported DC Chain operation=0x%x",
                                   operation);
            return CPA_STATUS_INVALID_PARAM;
    }
    return CPA_STATUS_SUCCESS;
}

/**
 *****************************************************************************
 * @ingroup Dc_Chaining
 *      Initialization for chaining sessions
 *
 * @description
 *      Check for supported Compression operations as part of DC Chain
 *
 * @param[in]    pDcSessDesc         Session descriptor for Compression
 *                                   operation.
 * @param[in]    operation           Chaining operation
 *
 * @retval CPA_STATUS_SUCCESS        Function executed successfully
 * @retval CPA_STATUS_INVALID_PARAM  Invalid Compression parameter
 *
 *****************************************************************************/
STATIC CpaStatus
dcChainSession_CheckDcSessDesc(const dc_session_desc_t *pDcSessDesc,
                               CpaDcChainOperations operation)
{
    LAC_CHECK_NULL_PARAM(pDcSessDesc);

    /* Restrict DC Chain Compression operations to supported/validated
     * combinations when used as part of a chain operation */
    switch (operation)
    {
        case CPA_DC_CHAIN_HASH_THEN_COMPRESS:
            LAC_CHECK_STATEMENT_LOG(
                (CPA_DC_STATELESS != pDcSessDesc->sessState),
                "Invalid DC sessState=0x%x for HASH_THEN_COMPRESS chaining",
                pDcSessDesc->sessState);

            LAC_CHECK_STATEMENT_LOG(
                (CPA_DC_DIR_COMPRESS != pDcSessDesc->sessDirection),
                "Invalid DC sessDirection=0x%x for HASH_THEN_COMPRESS chaining",
                pDcSessDesc->sessDirection);

            LAC_CHECK_STATEMENT_LOG(
                (CPA_DC_HT_PRECOMP == pDcSessDesc->huffType),
                "Invalid DC huffType=0x%x for HASH_THEN_COMPRESS chaining",
                pDcSessDesc->huffType);

            break;
        case CPA_DC_CHAIN_COMPRESS_THEN_AEAD:
            LAC_CHECK_STATEMENT_LOG(
                (CPA_DC_STATELESS != pDcSessDesc->sessState),
                "Invalid DC sessState=0x%x for COMPRESS_THEN_AEAD chaining",
                pDcSessDesc->sessState);

            LAC_CHECK_STATEMENT_LOG(
                (CPA_DC_DIR_COMPRESS != pDcSessDesc->sessDirection),
                "Invalid DC sessDirection=0x%x for COMPRESS_THEN_AEAD chaining",
                pDcSessDesc->sessDirection);

            LAC_CHECK_STATEMENT_LOG(
                (CPA_DC_HT_FULL_DYNAMIC != pDcSessDesc->huffType),
                "Invalid DC huffType=0x%x for COMPRESS_THEN_AEAD chaining",
                pDcSessDesc->huffType);

            LAC_CHECK_STATEMENT_LOG(
                (CPA_DC_DEFLATE != pDcSessDesc->compType),
                "Invalid DC compType=0x%x for COMPRESS_THEN_AEAD chaining",
                pDcSessDesc->compType);

            LAC_CHECK_STATEMENT_LOG(
                (CPA_DC_ASB_DISABLED != pDcSessDesc->autoSelectBestHuffmanTree),
                "Invalid DC autoSelectBestHuffmanTree=0x%x for "
                "COMPRESS_THEN_AEAD chaining",
                pDcSessDesc->autoSelectBestHuffmanTree);

            break;
        case CPA_DC_CHAIN_AEAD_THEN_DECOMPRESS:
            LAC_CHECK_STATEMENT_LOG(
                (CPA_DC_STATELESS != pDcSessDesc->sessState),
                "Invalid DC sessState=0x%x for AEAD_THEN_DECOMPRESS chaining",
                pDcSessDesc->sessState);

            LAC_CHECK_STATEMENT_LOG(
                (CPA_DC_DIR_DECOMPRESS != pDcSessDesc->sessDirection),
                "Invalid DC sessDirection=0x%x for AEAD_THEN_DECOMPRESS "
                "chaining",
                pDcSessDesc->sessDirection);

            break;
        default:
            LAC_INVALID_PARAM_LOG1("Unsupported DC Chain operation=0x%x",
                                   operation);
            return CPA_STATUS_INVALID_PARAM;
    }
    return CPA_STATUS_SUCCESS;
}

/**
 *****************************************************************************
 * @ingroup Dc_Chaining
 *      Initialization for chaining sessions
 *
 * @description
 *      Check for supported Compression operations as part of DC Chain
 *
 * @param[in]    pChainSessDesc      Session descriptor for DC Chain
 *                                   requests.
 * @param[in]    operation           Chaining operation
 * @param[in]    numSessions         Number of sessions in the chain operation
 *
 * @retval CPA_STATUS_SUCCESS        Function executed successfully
 * @retval CPA_STATUS_INVALID_PARAM  Invalid Compression parameter
 *
 *****************************************************************************/
STATIC CpaStatus
dcChainSession_CheckChainSessDesc(const dc_chain_session_head_t *pChainSessDesc,
                                  const CpaDcChainOperations operation,
                                  const Cpa8U numSessions)
{
    Cpa8U *pTemp;

    LAC_CHECK_NULL_PARAM(pChainSessDesc);

    LAC_CHECK_STATEMENT_LOG(numSessions != NUM_OF_SESSION_SUPPORT,
                            "%s",
                            "Wrong number of sessions "
                            "for a chaining operation");

    pTemp = (Cpa8U *)pChainSessDesc + sizeof(dc_chain_session_head_t);

    /* Restrict DC Chain operations to supported/validated
     * combinations */
    switch (operation)
    {
        case CPA_DC_CHAIN_HASH_THEN_COMPRESS:
            LAC_CHECK_STATEMENT_LOG(
                (DC_CHAIN_TYPE_GET(pTemp) != CPA_DC_CHAIN_SYMMETRIC_CRYPTO),
                "Chain Entry[0] type = 0x%x",
                DC_CHAIN_TYPE_GET(pTemp));

            pTemp += (LAC_SYM_SESSION_SIZE + sizeof(CpaDcChainSessionType));

            LAC_CHECK_STATEMENT_LOG(
                (DC_CHAIN_TYPE_GET(pTemp) != CPA_DC_CHAIN_COMPRESS_DECOMPRESS),
                "HASH_THEN_COMPRESS Chain Entry[1] type = 0x%x",
                DC_CHAIN_TYPE_GET(pTemp));

            break;
        case CPA_DC_CHAIN_COMPRESS_THEN_AEAD:
            LAC_CHECK_STATEMENT_LOG(
                (DC_CHAIN_TYPE_GET(pTemp) != CPA_DC_CHAIN_COMPRESS_DECOMPRESS),
                "Chain Entry[0] type = 0x%x",
                DC_CHAIN_TYPE_GET(pTemp));

            pTemp += (DC_COMP_SESSION_SIZE + sizeof(CpaDcChainSessionType));

            LAC_CHECK_STATEMENT_LOG(
                (DC_CHAIN_TYPE_GET(pTemp) != CPA_DC_CHAIN_SYMMETRIC_CRYPTO),
                "COMPRESS_THEN_AEAD Chain Entry[1] type = 0x%x",
                DC_CHAIN_TYPE_GET(pTemp));

            break;
        case CPA_DC_CHAIN_AEAD_THEN_DECOMPRESS:
            LAC_CHECK_STATEMENT_LOG(
                (DC_CHAIN_TYPE_GET(pTemp) != CPA_DC_CHAIN_SYMMETRIC_CRYPTO),
                "Chain Entry[0] type = 0x%x",
                DC_CHAIN_TYPE_GET(pTemp));

            pTemp += (LAC_SYM_SESSION_SIZE + sizeof(CpaDcChainSessionType));

            LAC_CHECK_STATEMENT_LOG(
                (DC_CHAIN_TYPE_GET(pTemp) != CPA_DC_CHAIN_COMPRESS_DECOMPRESS),
                "AEAD_THEN_DECOMPRESS Chain Entry[1] type = 0x%x",
                DC_CHAIN_TYPE_GET(pTemp));

            break;
        default:
            LAC_INVALID_PARAM_LOG1("Unsupported operation %x\n", operation);
            return CPA_STATUS_INVALID_PARAM;
    }
    return CPA_STATUS_SUCCESS;
}
#endif

/**
 *****************************************************************************
 * @ingroup Dc_Chaining
 *      Get the size for chaining sessions
 *
 * @description
 *      Get the size for chaining sessions, it counts how many bytes are needed
 *      for one chaining request, and it is called by cpaDcChainGetSessionSize
 *
 * @param[in]       dcInstance       Instance handle
 * @param[in]       pSessionData     Pointer to an array of
 *                                   CpaDcChainSessionSetupData
 *                                   structures. There should be numSessions
 *                                   entries in the array.
 * @param[in]       numSessions      Number of chaining sessions
 * @param[out]      pSessionSize     On return, this parameter will be the size
 *                                   of the memory that will be required by
 *                                   cpaDcChainInitSession() for session data.
 *
 * @retval CPA_STATUS_SUCCESS        Function executed successfully
 *
 *****************************************************************************/
STATIC CpaStatus dcChainGetSessionSize(CpaInstanceHandle dcInstance,
                                       CpaDcChainSessionSetupData *pSessionData,
                                       Cpa8U numSessions,
                                       Cpa32U *pSessionSize)
{
    Cpa32U sessSize;
    Cpa32U i;

    sessSize = sizeof(dc_chain_session_head_t);

    for (i = 0; i < numSessions; i++)
    {
        sessSize += sizeof(CpaDcChainSessionType) + LAC_64BYTE_ALIGNMENT +
                    sizeof(LAC_ARCH_UINT);
        if (pSessionData[i].sessType == CPA_DC_CHAIN_COMPRESS_DECOMPRESS)
            sessSize += sizeof(dc_session_desc_t);
        else
            sessSize += sizeof(lac_session_desc_t);
    }

    *pSessionSize = sessSize;

    return CPA_STATUS_SUCCESS;
}

CpaStatus cpaDcChainGetSessionSize(CpaInstanceHandle dcInstance,
                                   CpaDcChainOperations operation,
                                   Cpa8U numSessions,
                                   CpaDcChainSessionSetupData *pSessionData,
                                   Cpa32U *pSessionSize)

{
#ifdef ICP_PARAM_CHECK
    LAC_CHECK_NULL_PARAM(pSessionData);
    LAC_CHECK_NULL_PARAM(pSessionSize);
    LAC_CHECK_STATEMENT_LOG(numSessions != NUM_OF_SESSION_SUPPORT,
                            "%s",
                            "Invalid number of sessions");
#endif

    return dcChainGetSessionSize(
        dcInstance, pSessionData, numSessions, pSessionSize);
}

/**
 *****************************************************************************
 * @ingroup Dc_Chaining
 *      Generate command ID
 *
 * @description
 *      Generate command ID from session data and number of sessions
 *
 * @param[in]       pSessionData     Pointer to an array of
 *                                   CpaDcChainSessionSetupData
 *                                   structures.
 * @param[in]       numSessions      Number of chaining sessions
 * @param[in]       cmd              Command ID
 *
 * @retval CPA_STATUS_SUCCESS        Function executed successfully
 * @retval CPA_STATUS_FAIL           Function failed to find device
 * @retval CPA_STATUS_INVALID_PARAM  Invalid parameter passed in
 *
 *****************************************************************************/
STATIC CpaStatus
dcChainSession_GenerateCmd(CpaInstanceHandle dcInstance,
                           CpaDcChainSessionSetupData *pSessionData,
                           Cpa8U numSessions,
                           icp_qat_comp_chain_cmd_id_t *cmd)
{
    Cpa16U key[DC_CHAIN_MAX_LINK] = { 0 };
    CpaDcChainSessionType sessType;
    CpaCySymCipherSetupData const *pCipherSetupData;
    CpaDcSessionDir dcDir = CPA_DC_DIR_COMPRESS;
    CpaCySymCipherDirection cyDir = CPA_CY_SYM_CIPHER_DIRECTION_ENCRYPT;
    CpaDcHuffType huffType;
    CpaCySymOp cyOpType = CPA_CY_SYM_OP_NONE;
    Cpa32U i, numOfCmds;
    sal_compression_service_t *pService =
        (sal_compression_service_t *)dcInstance;

    for (i = 0; i < numSessions; i++)
    {
        sessType = pSessionData[i].sessType;
        if (CPA_DC_CHAIN_COMPRESS_DECOMPRESS == sessType)
        {
            dcDir = pSessionData[i].pDcSetupData->sessDirection;
            if (dcDir == CPA_DC_DIR_COMPRESS)
                huffType = pSessionData[i].pDcSetupData->huffType;
            else
                huffType = CPA_DC_HT_STATIC;
            key[i] = (huffType << 8) | (dcDir << 4) | sessType;
        }
        else
        {
            cyOpType = pSessionData[i].pCySetupData->symOperation;
            pCipherSetupData = &(pSessionData[i].pCySetupData->cipherSetupData);
            cyDir = pCipherSetupData->cipherDirection;
            if (CPA_CY_SYM_OP_HASH == cyOpType)
            {
                cyDir = NOT_APPLICABLE;
            }
            key[i] = (cyOpType << 8) | (cyDir << 4) | sessType;
        }
    }

    numOfCmds = sizeof(dc_chain_cmd_table) / sizeof(dc_chain_cmd_tbl_t);
    for (i = 0; i < numOfCmds; i++)
    {
        if ((key[0] == dc_chain_cmd_table[i].link0_key) &&
            (key[1] == dc_chain_cmd_table[i].link1_key) &&
            (key[2] == dc_chain_cmd_table[i].link2_key))
        {
            /* Use legacy chaining command id for Gen2 devices */
            if (isDcGen2x(pService))
            {
                *cmd = dc_chain_cmd_table[i].cmd_id;
            }
            else
            {
                *cmd = (icp_qat_comp_chain_cmd_id_t)dc_chain_cmd_table[i]
                           .cmd_20_id;
            }

            /* Check if chaining is supported for the found operation */
            if (ICP_QAT_FW_NO_CHAINING == *cmd)
            {
                return CPA_STATUS_FAIL;
            }
            else
            {
                return CPA_STATUS_SUCCESS;
            }
        }
    }
    return CPA_STATUS_FAIL;
}

/**
 *****************************************************************************
 * @ingroup Dc_Chaining
 *      Initialization for chaining sessions
 *
 * @description
 *      Initialization for chaining sessions, it is called by
 *      cpaDcChainInitSession
 *
 * @param[in]       dcInstance      Instance handle derived from discovery
 *                                  functions.
 * @param[in,out]   pSessionHandle  Pointer to a session handle.
 * @param[in,out]   pSessionData    Pointer to an array of
 *                                  CpaDcChainSessionSetupData structures.
 *                                  There should be numSessions entries in the
 *                                  array.
 * @param[in]       numSessions     Number of sessions for the chaining
 * @param[in]       callbackFn      For synchronous operation this callback
 *                                  shall be a null pointer.
 *
 * @retval CPA_STATUS_SUCCESS        Function executed successfully
 * @retval CPA_STATUS_FAIL           Function failed to find device
 * @retval CPA_STATUS_INVALID_PARAM  Invalid parameter passed in
 *
 *****************************************************************************/
CpaStatus dcChainInitSessions(CpaInstanceHandle dcInstance,
                              CpaDcSessionHandle pSessionHandle,
                              CpaDcChainSessionSetupData *pSessionData,
                              Cpa8U numSessions,
                              CpaDcCallbackFn callbackFn)
{
    CpaStatus status = CPA_STATUS_SUCCESS;
    dc_chain_session_head_t *pSessHead;
    icp_qat_comp_chain_cmd_id_t chainCmd;
    Cpa8U *pTemp;
    Cpa32U i;
    CpaBoolean cyInitialized = CPA_FALSE;
    lac_session_desc_t *pCySessDesc = NULL;
    sal_compression_service_t *pService =
        (sal_compression_service_t *)dcInstance;

    pSessHead = (dc_chain_session_head_t *)pSessionHandle;
    pTemp = (Cpa8U *)pSessionHandle + sizeof(dc_chain_session_head_t);
    for (i = 0; i < numSessions; i++)
    {
        if (pSessionData[i].sessType == CPA_DC_CHAIN_COMPRESS_DECOMPRESS)
        {
            *(CpaDcChainSessionType *)pTemp = CPA_DC_CHAIN_COMPRESS_DECOMPRESS;
            pTemp += sizeof(CpaDcChainSessionType);

            status = dcInitSession(
                dcInstance,
                (CpaDcSessionHandle)pTemp,
                (CpaDcSessionSetupData *)pSessionData[i].pDcSetupData,
                NULL,
                NULL);

            if (CPA_STATUS_SUCCESS != status)
            {
                if (CPA_TRUE == cyInitialized)
                {
                    /* For crypto, destroy spinlock and mutex */
                    pCySessDesc = pSessHead->pCySessionDesc;
#ifdef ICP_PARAM_CHECK
                    LAC_CHECK_NULL_PARAM(pCySessDesc);
#endif
                    LAC_SPINLOCK_DESTROY(&pCySessDesc->requestQueueLock);
                    osalAtomicSet(0, &pCySessDesc->accessLock);
                }
                LAC_LOG_ERROR("Init compression session failure\n");
                return status;
            }
            pSessHead->pDcSessionDesc = DC_SESSION_DESC_FROM_CTX_GET(pTemp);
            pTemp += DC_COMP_SESSION_SIZE;
        }
        else
        {
            *(CpaDcChainSessionType *)pTemp = CPA_DC_CHAIN_SYMMETRIC_CRYPTO;
            pTemp += sizeof(CpaDcChainSessionType);

            status = LacSym_InitSession(
                dcInstance,
                NULL,
                (CpaCySymSessionSetupData *)pSessionData[i].pCySetupData,
                CPA_FALSE,
                (CpaCySymSessionCtx)pTemp);
            LAC_CHECK_STATUS_LOG(
                status, "%s", "Init symmectric session failure\n");
            pSessHead->pCySessionDesc =
                LAC_SYM_SESSION_DESC_FROM_CTX_GET(pTemp);
            pTemp += LAC_SYM_SESSION_SIZE;
            cyInitialized = CPA_TRUE;
        }
    }
    pSessHead->pdcChainCb = ((void *)NULL != (void *)callbackFn)
                                ? callbackFn
                                : LacSync_GenWakeupSyncCaller;

    osalAtomicSet(0, &pSessHead->pendingChainCbCount);
    /*Fill the pre-build header*/
    status = dcChainSession_GenerateCmd(
        dcInstance, pSessionData, numSessions, &chainCmd);
    if (CPA_STATUS_SUCCESS != status)
    {
        status = cpaDcChainRemoveSession(dcInstance, pSessionHandle);
        LAC_LOG_ERROR("generate chained command failure\n");
        return CPA_STATUS_FAIL;
    }

    /* Store number of links in the chain */
    pSessHead->numLinks = numSessions;
    if (isDcGen2x(pService))
    {
        /* Fill chaining request descriptor header */
        pSessHead->hdr.comn_hdr.service_cmd_id = chainCmd;
        pSessHead->hdr.comn_hdr.service_type =
            ICP_QAT_FW_COMN_REQ_CPM_FW_COMP_CHAIN;
        pSessHead->hdr.comn_hdr.hdr_flags =
            ICP_QAT_FW_COMN_HDR_FLAGS_BUILD(ICP_QAT_FW_COMN_REQ_FLAG_SET);
        pSessHead->hdr.comn_hdr.resrvd1 = 0;
        pSessHead->hdr.comn_hdr.numLinks = numSessions;
    }
    else
    {
        /* Fill chaining request descriptor header */
        pSessHead->hdr.comn_hdr2.service_cmd_id = chainCmd;
        pSessHead->hdr.comn_hdr2.service_type =
            ICP_QAT_FW_COMN_REQ_CPM_FW_COMP_CHAIN;
        pSessHead->hdr.comn_hdr2.hdr_flags =
            ICP_QAT_FW_COMN_HDR_FLAGS_BUILD(ICP_QAT_FW_COMN_REQ_FLAG_SET);
        pSessHead->hdr.comn_hdr2.resrvd1 = 0;
        pSessHead->hdr.comn_hdr2.serv_specif_flags =
            ICP_QAT_FW_COMP_CHAIN_REQ_FLAGS_BUILD(
                ICP_QAT_FW_COMP_CHAIN_NO_APPEND_CRC,
                ICP_QAT_FW_COMP_CHAIN_NO_VERIFY,
                ICP_QAT_FW_COMP_CHAIN_NO_DERIVE_KEY,
                ICP_QAT_FW_COMP_CHAIN_NO_CRC64_CTX);
        pSessHead->hdr.comn_hdr2.comn_req_flags = ICP_QAT_FW_COMN_FLAGS_BUILD(
            DC_DEFAULT_QAT_PTR_TYPE, QAT_COMN_CD_FLD_TYPE_16BYTE_DATA);
        /* Set AT flag in request header if instance supports address
         * translation
         */
        if (pService->generic_service_info.atEnabled)
        {
            SalQatMsg_AddressTranslationHdrWrite(
                &pSessHead->hdr.comn_hdr2.comn_req_flags);
        }
        pSessHead->hdr.comn_hdr2.extended_serv_specif_flags = 0;
    }

    return status;
}

/**
 *****************************************************************************
 * @ingroup Dc_Chaining
 *      Initialize CRC parameters for a DC Chaining session
 *
 * @description
 *      This function is used to initialize E2E programmable CRC parameters.
 *
 * @param[in]       dcInstance      Instance handle derived from discovery
 *                                  functions.
 * @param[in,out]   pSessionHandle  Pointer to a session handle.
 * @param[in]       pCrcControlData Pointer to a user instantiated structure
 *                                  containing the CRC parameters.
 *
 * @retval CPA_STATUS_SUCCESS       Function executed successfully
 * @retval CPA_STATUS_INVALID_PARAM Invalid parameter passed in
 *
 *****************************************************************************/
STATIC CpaStatus dcChainSetCrcControlData(CpaInstanceHandle dcInstance,
                                          CpaDcSessionHandle pSessionHandle,
                                          CpaCrcControlData *pCrcControlData)
{
    CpaStatus status = CPA_STATUS_SUCCESS;
    dc_chain_session_head_t *pSessHead;
    Cpa8U *pTemp;
    Cpa32U i;
    CpaInstanceHandle insHandle = NULL;
    sal_compression_service_t *pService = NULL;

    if (CPA_INSTANCE_HANDLE_SINGLE == dcInstance)
    {
        insHandle = dcGetFirstHandle();
    }
    else
    {
        insHandle = dcInstance;
    }
#ifdef ICP_PARAM_CHECK
    LAC_CHECK_INSTANCE_HANDLE(insHandle);
    LAC_CHECK_NULL_PARAM(pSessionHandle);
    SAL_CHECK_INSTANCE_TYPE(insHandle, SAL_SERVICE_TYPE_COMPRESSION);
    LAC_CHECK_NULL_PARAM(pCrcControlData);
#endif

    pService = (sal_compression_service_t *)insHandle;
    if (NULL == pService->pDcChainService)
    {
        return CPA_STATUS_UNSUPPORTED;
    }

    pSessHead = (dc_chain_session_head_t *)pSessionHandle;
    pTemp = (Cpa8U *)pSessionHandle + sizeof(dc_chain_session_head_t);
    for (i = 0; i < pSessHead->numLinks; i++)
    {
        if (DC_CHAIN_TYPE_GET(pTemp) == CPA_DC_CHAIN_COMPRESS_DECOMPRESS)
        {
            pTemp += sizeof(CpaDcChainSessionType);

            status = dcInitSessionCrcControl((CpaDcSessionHandle)pTemp,
                                             pCrcControlData);

            LAC_CHECK_STATUS_LOG(
                status, "%s", "Init DC crc control data failure\n");

            pTemp += DC_COMP_SESSION_SIZE;
        }
        else
        {
            pTemp += sizeof(CpaDcChainSessionType);

            status = LacSym_InitSessionCrcControl(
                insHandle, (CpaCySymSessionCtx)pTemp, pCrcControlData);

            LAC_CHECK_STATUS_LOG(
                status, "%s", "Init CY crc control data failure\n");

            pTemp += LAC_SYM_SESSION_SIZE;
        }
    }

    return status;
}

CpaStatus cpaDcChainInitSession(CpaInstanceHandle dcInstance,
                                CpaDcSessionHandle pSessionHandle,
                                CpaDcChainOperations operation,
                                Cpa8U numSessions,
                                CpaDcChainSessionSetupData *pSessionData,
                                CpaDcCallbackFn callbackFn)

{
#ifdef ICP_PARAM_CHECK
    CpaStatus status = CPA_STATUS_SUCCESS;
#endif
    CpaInstanceHandle insHandle = NULL;
    sal_compression_service_t *pService = NULL;

    if (CPA_INSTANCE_HANDLE_SINGLE == dcInstance)
    {
        insHandle = dcGetFirstHandle();
    }
    else
    {
        insHandle = dcInstance;
    }

    pService = (sal_compression_service_t *)insHandle;
#ifdef ICP_PARAM_CHECK
    LAC_CHECK_INSTANCE_HANDLE(insHandle);
    SAL_CHECK_ADDR_TRANS_SETUP(insHandle);
    SAL_CHECK_INSTANCE_TYPE(insHandle, SAL_SERVICE_TYPE_COMPRESSION);
    LAC_CHECK_NULL_PARAM(pSessionData);
    LAC_CHECK_NULL_PARAM(pSessionHandle);
    SAL_RUNNING_CHECK(pService);
    status =
        dcChainSession_CheckSessionData(operation, numSessions, pSessionData);
    LAC_CHECK_STATUS(status);
#endif
    if (NULL == pService->pDcChainService)
    {
        return CPA_STATUS_UNSUPPORTED;
    }

    return dcChainInitSessions(
        insHandle, pSessionHandle, pSessionData, numSessions, callbackFn);
}

/**
 *****************************************************************************
 * @ingroup cpaDcChain
 *      Initialize CRC parameters for a DC Chaining session
 *
 * @description
 *      This function is used to initialize E2E programmable CRC parameters.
 *
 * @context
 *      This is a synchronous function and it cannot sleep. It can be executed
 *      in a context that does not permit sleeping.
 * @assumptions
 *      None
 * @sideEffects
 *      None
 * @blocking
 *      No
 * @reentrant
 *      No
 * @threadSafe
 *      Yes
 *
 * @param[in]       dcInstance      Instance handle derived from discovery
 *                                  functions.
 * @param[in,out]   pSessionHandle  Pointer to a session handle.
 * @param[in]       pCrcControlData Pointer to a user instantiated structure
 *                                  containing the CRC parameters.
 *
 * @retval CPA_STATUS_SUCCESS       Function executed successfully.
 * @retval CPA_STATUS_INVALID_PARAM Invalid parameter passed in.
 * @retval CPA_STATUS_FAIL          Operation failed.
 * @retval CPA_STATUS_UNSUPPORTED   Unsupported feature.
 *
 * @pre
 *      dcInstance has been started using cpaDcStartInstance.
 *      cpaDcInitSession has been called to initialize session parameters.
 * @post
 *      None
 * @note
 *      Only a synchronous version of this function is provided.
 *
 * @see
 *      None
 *
 *****************************************************************************/
CpaStatus cpaDcChainSetCrcControlData(CpaInstanceHandle dcInstance,
                                      CpaDcSessionHandle pSessionHandle,
                                      CpaCrcControlData *pCrcControlData)
{

#ifdef ICP_TRACE
    LAC_LOG3("Called with params (0x%lx, 0x%lx, 0x%lx)\n",
             (LAC_ARCH_UINT)dcInstance,
             (LAC_ARCH_UINT)pSessionHandle,
             (LAC_ARCH_UINT)pCrcControlData);
#endif

    return dcChainSetCrcControlData(
        dcInstance, pSessionHandle, pCrcControlData);
}

CpaStatus cpaDcChainRemoveSession(const CpaInstanceHandle dcInstance,
                                  CpaDcSessionHandle pSessionHandle)
{
    CpaStatus status = CPA_STATUS_SUCCESS;
    CpaInstanceHandle insHandle = NULL;
    dc_chain_session_head_t *pSessHead = NULL;
    Cpa64U numPendingRequest = 0;
    sal_compression_service_t *pService = NULL;
    dc_session_desc_t *pDcSessDesc = NULL;
    lac_session_desc_t *pCySessDesc = NULL;
#ifdef ICP_PARAM_CHECK
    LAC_CHECK_NULL_PARAM(pSessionHandle);
#endif
    pSessHead = (dc_chain_session_head_t *)pSessionHandle;

    if (CPA_INSTANCE_HANDLE_SINGLE == dcInstance)
    {
        insHandle = dcGetFirstHandle();
    }
    else
    {
        insHandle = dcInstance;
    }

#ifdef ICP_PARAM_CHECK
    LAC_CHECK_NULL_PARAM(insHandle);
    SAL_CHECK_INSTANCE_TYPE(insHandle, SAL_SERVICE_TYPE_COMPRESSION);
#endif
    pService = (sal_compression_service_t *)insHandle;
    if (NULL == pService->pDcChainService)
    {
        return CPA_STATUS_UNSUPPORTED;
    }

    /* Check if SAL is running otherwise return an error */
    SAL_RUNNING_CHECK(insHandle);

    numPendingRequest = osalAtomicGet(&(pSessHead->pendingChainCbCount));

    /* Check if there are  pending requests */
    if (0 != numPendingRequest)
    {
        LAC_LOG_ERROR1("There are %llu chaining requests pending",
                       numPendingRequest);
        status = CPA_STATUS_RETRY;
    }

    /* Free the CRC lookup table if one was allocated */
    pDcSessDesc = pSessHead->pDcSessionDesc;
#ifdef ICP_PARAM_CHECK
    LAC_CHECK_NULL_PARAM(pDcSessDesc);
#endif
    if (NULL != pDcSessDesc->crcConfig.pCrcLookupTable)
    {
        LAC_OS_FREE(pDcSessDesc->crcConfig.pCrcLookupTable);
    }

    /* For crypto, destroy spinlock and mutex */
    pCySessDesc = pSessHead->pCySessionDesc;
#ifdef ICP_PARAM_CHECK
    LAC_CHECK_NULL_PARAM(pCySessDesc);
#endif
    LAC_SPINLOCK_DESTROY(&pCySessDesc->requestQueueLock);
    osalAtomicSet(0, &pCySessDesc->accessLock);

    return status;
}

CpaStatus cpaDcChainResetSession(const CpaInstanceHandle dcInstance,
                                 CpaDcSessionHandle pSessionHandle)
{
    CpaStatus status = CPA_STATUS_SUCCESS;
    CpaInstanceHandle insHandle = NULL;
    dc_session_desc_t *pDcDescriptor = NULL;
    dc_chain_session_head_t *pSessHead = NULL;
    Cpa64U numPendingRequest = 0;
    Cpa8U *pTemp;
    Cpa32U i;
    sal_compression_service_t *pService = NULL;
#ifdef ICP_PARAM_CHECK
    LAC_CHECK_NULL_PARAM(pSessionHandle);
#endif
    pSessHead = (dc_chain_session_head_t *)pSessionHandle;

    if (CPA_INSTANCE_HANDLE_SINGLE == dcInstance)
    {
        insHandle = dcGetFirstHandle();
    }
    else
    {
        insHandle = dcInstance;
    }

#ifdef ICP_PARAM_CHECK
    LAC_CHECK_NULL_PARAM(insHandle);
    SAL_CHECK_INSTANCE_TYPE(insHandle, SAL_SERVICE_TYPE_COMPRESSION);
#endif
    pService = (sal_compression_service_t *)insHandle;
    if (NULL == pService->pDcChainService)
    {
        return CPA_STATUS_UNSUPPORTED;
    }
    /* Check if SAL is running otherwise return an error */
    SAL_RUNNING_CHECK(insHandle);

    numPendingRequest = osalAtomicGet(&(pSessHead->pendingChainCbCount));

    /* Check if there are stateless pending requests */
    if (0 != numPendingRequest)
    {
        LAC_LOG_ERROR1("There are %llu chaining requests pending",
                       numPendingRequest);
        return CPA_STATUS_RETRY;
    }

    pTemp = (Cpa8U *)pSessionHandle + sizeof(dc_chain_session_head_t);
    for (i = 0; i < pSessHead->numLinks; i++)
    {
        if (DC_CHAIN_TYPE_GET(pTemp) == CPA_DC_CHAIN_SYMMETRIC_CRYPTO)
        {
            pTemp += sizeof(CpaDcChainSessionType);
            pTemp += LAC_SYM_SESSION_SIZE;
        }
        else
        {
            pTemp += sizeof(CpaDcChainSessionType);
            pDcDescriptor = DC_SESSION_DESC_FROM_CTX_GET(pTemp);
            break;
        }
    }
    LAC_CHECK_NULL_PARAM(pDcDescriptor);
    /* Reset chaining session descriptor */
    pDcDescriptor->requestType = DC_REQUEST_FIRST;
    pDcDescriptor->cumulativeConsumedBytes = 0;

    return status;
}

/**
 * ************************************************************************
 * @ingroup Dc_Chaining
 *     Create compression request and link it to chaining request
 *
 * @param[out]   pChainCookie        Chaining cookie
 * @param[in]    pDcCookie           Compression cookie
 * @param[in]    dcInstance          Compression instance handle
 * @param[in]    operation           Chaining operation
 * @param[in]    pSessionHandle      Compression session handle
 * @param[in]    pSrcBuff            Source buffer for compression
 * @param[in]    pDestBuff           Destination buffer for comprssion
 * @param[in]    pResults            Chaining result
 * @param[in]    pDcOp               Pointer to compression operation data
 *
 * @retval CPA_STATUS_SUCCESS        Function executed successfully
 * @retval CPA_STATUS_FAIL           Function failed to find device
 * @retval CPA_STATUS_INVALID_PARAM  Invalid parameter passed in
 * @retval CPA_STATUS_UNSUPPORTED    Function not supported
 *
 * *************************************************************************/
STATIC CpaStatus dcChainPrepare_CompRequest(CpaInstanceHandle dcInstance,
                                            dc_chain_cookie_t *pChainCookie,
                                            CpaDcChainOperations operation,
                                            Cpa8U *pSessionHandle,
                                            dc_opdata_ext_t *pDcOpDataExt,
                                            dc_compression_cookie_t *pDcCookie,
                                            CpaBufferList *pSrcBuff,
                                            CpaBufferList *pDestBuff,
                                            CpaDcChainRqResults *pResults)
{
    CpaStatus status = CPA_STATUS_SUCCESS;
    dc_session_desc_t *pDcSessDesc = NULL;
    sal_compression_service_t *pDcService =
        (sal_compression_service_t *)dcInstance;
    sal_dc_chain_service_t *pChainService = pDcService->pDcChainService;
    CpaDcOpData *pDcOpData = NULL;
    dc_request_dir_t compDecomp;
    CpaDcRqResults dcResults = { 0 };
    Cpa64U rspDescPhyAddr = 0;
    dc_cnv_mode_t cnvMode;
    Cpa8U asbFlag = ICP_QAT_FW_COMP_CHAIN_NO_ASB;
    Cpa8U cnvFlag = ICP_QAT_FW_COMP_CHAIN_NO_CNV;
    Cpa8U cnvnrFlag = ICP_QAT_FW_COMP_CHAIN_NO_CNV_RECOVERY;
    void *pDcLinkRsp = NULL;
    icp_qat_fw_comp_chain_req_t *pChainReq = NULL;
    icp_qat_fw_chain_stor2_req_t *pChainStor2Req = NULL;
    icp_qat_fw_comp_req_t *pMsg = NULL;

    pDcSessDesc = DC_SESSION_DESC_FROM_CTX_GET(pSessionHandle);
    LAC_CHECK_NULL_PARAM(pDcSessDesc);

    /* Legacy CpaDcOpData. DC OpData extensions not supported. */
    if (DC_OPDATA_TYPE0 == pDcOpDataExt->opDataType)
    {
        pDcOpData = (CpaDcOpData *)pDcOpDataExt->pOpData;
    }
    else
    {
        if (CPA_TRUE == ((CpaDcOpData2 *)pDcOpDataExt->pOpData)->appendCRC64)
        {
            LAC_UNSUPPORTED_PARAM_LOG(
                "DC Chaining extension appendCRC64 is not supported");
            return CPA_STATUS_UNSUPPORTED;
        }
        pDcOpData = &((CpaDcOpData2 *)pDcOpDataExt->pOpData)->dcOpData;
    }

#ifdef ICP_PARAM_CHECK
    LAC_CHECK_NULL_PARAM(pDcOpData);
    LAC_CHECK_STATEMENT_LOG(
        pDcOpData->flushFlag == CPA_DC_FLUSH_SYNC,
        "%s",
        "CPA_DC_FLUSH_SYNC not supported for compression.\n");
    LAC_CHECK_STATEMENT_LOG(
        pDcOpData->flushFlag == CPA_DC_FLUSH_NONE,
        "%s",
        "CPA_DC_FLUSH_NONE flag not supported for compression.\n");
    LAC_CHECK_STATEMENT_LOG(
        pDcOpData->compressAndVerifyAndRecover == CPA_TRUE &&
            operation != CPA_DC_CHAIN_HASH_THEN_COMPRESS,
        "%s",
        "compressAndVerifyAndRecover not supported for chain compression.\n");
    status = dcChainSession_CheckDcSessDesc(pDcSessDesc, operation);
    LAC_CHECK_STATUS(status);
#endif

    if (pDcOpData->compressAndVerifyAndRecover)
        cnvMode = DC_CNVNR;
    else if (pDcOpData->compressAndVerify)
        cnvMode = DC_CNV;
    else
        cnvMode = DC_NO_CNV;

    if (CPA_DC_DIR_COMPRESS == pDcSessDesc->sessDirection)
        compDecomp = DC_COMPRESSION_REQUEST;
    else
        compDecomp = DC_DECOMPRESSION_REQUEST;

#ifdef CNV_STRICT_MODE
    if (CPA_FALSE == pDcOpData->compressAndVerify)
    {
        LAC_UNSUPPORTED_PARAM_LOG(
            "Data compression without verification not allowed");
        return CPA_STATUS_UNSUPPORTED;
    }
#endif

    /* Add DC Chaining info to the compression cookie */
    pDcCookie->dcChain.isDcChaining = CPA_TRUE;

    /* This is the common DC function to create the requests for
     * compression or decompression. It creates the DC request
     * message but does not send it. */
    status = dcCreateRequest(pDcCookie,
                             pDcService,
                             pDcSessDesc,
                             pSessionHandle,
                             pSrcBuff,
                             pDestBuff,
                             &dcResults,
                             pDcOpData->flushFlag,
                             pDcOpData,
                             NULL,
                             compDecomp,
                             cnvMode);
    pDcCookie->pResults = NULL;

    /* If this is not a DC_REQUEST_FIRST then for DC Chaining we must
     * re-seed both adler32 and crc32 with the previous checksums */
    pMsg = (icp_qat_fw_comp_req_t *)&pDcCookie->request;
    if ((DC_REQUEST_FIRST != pDcSessDesc->requestType) &&
        (CPA_DC_STATELESS == pDcSessDesc->sessState))
    {
        pMsg->comp_pars.crc.legacy.initial_adler = pResults->adler32;
        pMsg->comp_pars.crc.legacy.initial_crc32 = pResults->crc32;
    }

    LAC_CHECK_STATUS(status);

    pDcLinkRsp = Lac_MemPoolEntryAlloc(pChainService->dc_chain_serv_resp_pool);
    if (NULL == pDcLinkRsp)
    {
        return CPA_STATUS_RESOURCE;
    }
    else if ((void *)CPA_STATUS_RETRY == pDcLinkRsp)
    {
        return CPA_STATUS_RETRY;
    }

    rspDescPhyAddr = (icp_qat_addr_width_t)LAC_OS_VIRT_TO_PHYS_INTERNAL(
        &pDcService->generic_service_info, pDcLinkRsp);
    pChainCookie->pDcRspAddr = pDcLinkRsp;
    pChainCookie->pDcCookieAddr = pDcCookie;

    if (isDcGen2x(pDcService))
    {
        pChainReq = (icp_qat_fw_comp_chain_req_t *)&pChainCookie->request;
        pChainReq->compReqAddr =
            (icp_qat_addr_width_t)LAC_OS_VIRT_TO_PHYS_INTERNAL(
                &pDcService->generic_service_info, &pDcCookie->request);
        pChainReq->compRespAddr = rspDescPhyAddr;

        if (pDcOpData->compressAndVerifyAndRecover)
        {
            cnvnrFlag = ICP_QAT_FW_COMP_CHAIN_CNV_RECOVERY;
        }

        if (pDcOpData->compressAndVerify)
        {
            cnvFlag = ICP_QAT_FW_COMP_CHAIN_CNV;
        }

        switch (pDcSessDesc->autoSelectBestHuffmanTree)
        {
            case CPA_DC_ASB_DISABLED:
                break;
            case CPA_DC_ASB_STATIC_DYNAMIC:
            case CPA_DC_ASB_UNCOMP_STATIC_DYNAMIC_WITH_STORED_HDRS:
            case CPA_DC_ASB_UNCOMP_STATIC_DYNAMIC_WITH_NO_HDRS:
            case CPA_DC_ASB_ENABLED:
                asbFlag = ICP_QAT_FW_COMP_CHAIN_ASB;
                break;
            default:
                break;
        }

        /* Fill extend flag */
        pChainReq->extendFlags |= ICP_QAT_FW_COMP_CHAIN_REQ_EXTEND_FLAGS_BUILD(
            cnvFlag,
            asbFlag,
            ICP_QAT_FW_COMP_CHAIN_NO_CBC,
            ICP_QAT_FW_COMP_CHAIN_NO_XTS,
            ICP_QAT_FW_COMP_CHAIN_NO_CCM,
            cnvnrFlag);
    }
    else
    {
        pChainStor2Req = (icp_qat_fw_chain_stor2_req_t *)&pChainCookie->request;
        pChainStor2Req->compReqAddr =
            (icp_qat_addr_width_t)LAC_OS_VIRT_TO_PHYS_INTERNAL(
                &pDcService->generic_service_info, &pDcCookie->request);
        pChainStor2Req->compRespAddr = rspDescPhyAddr;
    }

    return status;
}

/**
 * ************************************************************************
 * @ingroup Dc_Chaining
 *     Create symmetric crypto request for chaining request
 *     and link crypto request address to chaining request,
 *     FW will parse chaining request on ring to get crypto
 *     request address.
 *
 * @param[in]    dcInstance          chaining Instance handle
 * @param[in,out]    pChainCookie    Chaining cookie
 * @param[in]    operation           Chaining operation
 * @param[in]    pCySymOp            Symmetric crypto operation data
 * @param[in]    pCyCookie           Symmetric crypto cookie
 * @param[in]    pSrcBuffer          Source buffer for crypto
 * @param[in]    pDestBuffer         Destination buffer for crypto
 * @param[in,out]    pResults        chaining response result
 * @param[in]    pSessionHandle      chaining session handle
 *
 * @retval CPA_STATUS_SUCCESS        Function executed successfully
 * @retval CPA_STATUS_FAIL           Function failed to find device
 * @retval CPA_STATUS_INVALID_PARAM  Invalid parameter passed in
 * @retval CPA_STATUS_RESOURCE       Error related to system resources
 * @retval CPA_STATUS_RETRY          Need to try it again
 * @retval CPA_STATUS_UNSUPPORTED    Function not supported
 *
 * *************************************************************************/
STATIC CpaStatus dcChainPrepare_SymRequest(CpaInstanceHandle dcInstance,
                                           dc_chain_cookie_t *pChainCookie,
                                           CpaDcChainOperations operation,
                                           Cpa8U *pSessionHandle,
                                           lac_sym_bulk_cookie_t *pCyCookie,
                                           cy_opdata_ext_t *pCySymOpExt,
                                           CpaBufferList *pSrcBuff,
                                           CpaBufferList *pDestBuff,
                                           CpaDcChainRqResults *pResults)
{
    CpaStatus status = CPA_STATUS_SUCCESS;
    /*compression service and chain service*/
    sal_compression_service_t *pDcService =
        (sal_compression_service_t *)dcInstance;
    sal_dc_chain_service_t *pChainService = pDcService->pDcChainService;
    lac_session_desc_t *pCySessDesc = NULL;
    void *pCyLinkRsp = NULL;
    Cpa64U rspDescPhyAddr = 0;
    icp_qat_fw_comp_chain_req_t *pChainReq = NULL;
    icp_qat_fw_chain_stor2_req_t *pChainStor2Req = NULL;
    CpaCySymOpData *pCySymOp = NULL;
    pCySessDesc = LAC_SYM_SESSION_DESC_FROM_CTX_GET(pSessionHandle);
    CpaCySymDeriveOpData *pDeriveCtxData = NULL;

    /* Legacy CpaCySymOpData. CY OpData extensions not supported. */
    if (CY_OPDATA_TYPE0 == pCySymOpExt->opDataType)
    {
        pCySymOp = (CpaCySymOpData *)pCySymOpExt->pOpData;
    }
    else
    {
        pDeriveCtxData =
            &((CpaCySymOpData2 *)pCySymOpExt->pOpData)->deriveCtxData;
        if ((NULL != pDeriveCtxData->pContext) ||
            (0 != pDeriveCtxData->contextLen))
        {
            LAC_UNSUPPORTED_PARAM_LOG(
                "DC Chaining extension sym key derivation is not supported");
            return CPA_STATUS_UNSUPPORTED;
        }
        pCySymOp = &((CpaCySymOpData2 *)pCySymOpExt->pOpData)->symOpData;
    }

#ifdef ICP_PARAM_CHECK
    LAC_CHECK_NULL_PARAM(pCySymOp);
    LAC_CHECK_NULL_PARAM(pResults);

    LAC_CHECK_STATEMENT_LOG(
        (pCySymOp->packetType == CPA_CY_SYM_PACKET_TYPE_PARTIAL ||
         pCySymOp->packetType == CPA_CY_SYM_PACKET_TYPE_LAST_PARTIAL),
        "%s",
        "Packet type not supported for crypto operation.\n");

    if ((!pCySessDesc->digestIsAppended) &&
        ((CPA_CY_SYM_OP_ALGORITHM_CHAINING == pCySessDesc->symOperation) ||
         (CPA_CY_SYM_OP_HASH == pCySessDesc->symOperation)))
    {
        /* Check that pDigestResult is not NULL */
        LAC_CHECK_NULL_PARAM(pCySymOp->pDigestResult);
    }

    status = dcChainSession_CheckCySessDesc(pCySessDesc, operation);
    LAC_CHECK_STATUS(status);
#endif
    pCySymOp->sessionCtx = pSessionHandle;

    if (CPA_STATUS_SUCCESS == status)
    {
        /* Add DC Chaining info to the CY cookie */
        pCyCookie->dcChain.isDcChaining = CPA_TRUE;

        /* This is the common CY function to create the requests for
         * crypto operations. It creates the crypto request
         * message but does not send it for DC chained requests. */
        status = LacAlgChain_Perform(dcInstance,
                                     pCySessDesc,
                                     NULL,
                                     pCySymOp,
                                     pCyCookie,
                                     pSrcBuff,
                                     pDestBuff,
                                     &pResults->verifyResult);
    }
    LAC_CHECK_STATUS(status);

    pCyLinkRsp = Lac_MemPoolEntryAlloc(pChainService->dc_chain_serv_resp_pool);
    if (NULL == pCyLinkRsp)
    {
        return CPA_STATUS_RESOURCE;
    }
    else if ((void *)CPA_STATUS_RETRY == pCyLinkRsp)
    {
        return CPA_STATUS_RETRY;
    }

    rspDescPhyAddr = (icp_qat_addr_width_t)LAC_OS_VIRT_TO_PHYS_INTERNAL(
        &pDcService->generic_service_info, pCyLinkRsp);
    pChainCookie->pCyRspAddr = pCyLinkRsp;
    pChainCookie->pCyCookieAddr = pCyCookie;

    if (isDcGen2x(pDcService))
    {
        pChainReq = (icp_qat_fw_comp_chain_req_t *)&pChainCookie->request;
        pChainReq->symCryptoReqAddr =
            (icp_qat_addr_width_t)LAC_OS_VIRT_TO_PHYS_INTERNAL(
                &pDcService->generic_service_info, &pCyCookie->qatMsg);
        pChainReq->symCryptoRespAddr = rspDescPhyAddr;
    }
    else
    {
        pChainStor2Req = (icp_qat_fw_chain_stor2_req_t *)&pChainCookie->request;
        pChainStor2Req->symCryptoReqAddr =
            (icp_qat_addr_width_t)LAC_OS_VIRT_TO_PHYS_INTERNAL(
                &pDcService->generic_service_info, &pCyCookie->qatMsg);
        pChainStor2Req->symCryptoRespAddr = rspDescPhyAddr;
    }

    return status;
}

/* Free memory */
STATIC void dcChainOp_MemPoolEntryFree(void *pEntry)
{
    if ((void *)CPA_STATUS_RETRY == pEntry)
        return;

    if (NULL != pEntry)
        Lac_MemPoolEntryFree(pEntry);

    return;
}

/**
 *****************************************************************************
 * @ingroup Dc_Chaining
 *      Chaining perform operation
 *
 * @description
 *      Chaining perform operation, it is called at cpaDcChainPerformOp,
 *      which is used to perform chaining requests.
 *
 * @param[in]       dcInstance         Instance handle derived from discovery
 *                                     functions.
 * @param[in]       pSessionHandle     Pointer to a session handle.
 * @param[in]       pSrcBuff           Source buffer
 * @param[in]       pDestBuff          Destination buffer
 * @param[in]       pInterBuff         Pointer to intermediate buffer to be
 *                                     used as internal staging area for
 *                                     chaining operations.
 * @param[in]       operation          Chaining operation
 * @param[in]       numOperations      Number of operations for the chaining
 * @param[in]       pChainOpDataExt    Extensible chaining operation data
 * @param[in,out]   pResultsExt        Extensible chaining response result
 * @param[in]       callbackTag        For synchronous operation this callback
 *                                     shall be a null pointer.
 *
 * @retval CPA_STATUS_SUCCESS        Function executed successfully
 * @retval CPA_STATUS_FAIL           Function failed to find device
 * @retval CPA_STATUS_INVALID_PARAM  Invalid parameter passed in
 * @retval CPA_STATUS_RESOURCE       Failed to allocate required resources
 * @retval CPA_STATUS_RETRY          Request re-submission needed
 * @retval CPA_STATUS_UNSUPPORTED    Function is not supported.
 *
 *****************************************************************************/
CpaStatus dcChainPerformOp(CpaInstanceHandle dcInstance,
                           CpaDcSessionHandle pSessionHandle,
                           CpaBufferList *pSrcBuff,
                           CpaBufferList *pDestBuff,
                           CpaBufferList *pInterBuff,
                           CpaDcChainOperations operation,
                           Cpa8U numOperations,
                           dc_chain_opdata_ext_t *pChainOpDataExt,
                           dc_chain_results_ext_t *pResultsExt,
                           void *callbackTag)

{
    /* Compression service and chain service */
    sal_compression_service_t *pDcService =
        (sal_compression_service_t *)dcInstance;
    sal_dc_chain_service_t *pChainService;
    /* Session descriptor for compression and crypto */
    dc_session_desc_t *pDcSessDesc = NULL;
    dc_chain_session_head_t *pSessHead = NULL;
    /* Cookies for chaining (compression + crypto) */
    dc_chain_cookie_t *pChainCookie = NULL;
    dc_compression_cookie_t *pDcCookie = NULL;
    lac_sym_bulk_cookie_t *pCyCookie = NULL;
    /* Request for chaining (compression + crypto) */
    icp_qat_fw_comp_chain_req_t *pChainReq = NULL;
    icp_qat_fw_chain_stor2_req_t *pChainStor2Req = NULL;
    CpaStatus status = CPA_STATUS_SUCCESS;
    Cpa8U *pTemp;
    Cpa32U i;
    dc_extd_ftrs_t *pExtendedFtrs = NULL;
    CpaBufferList *pChainSrcBuff = NULL;
    CpaBufferList *pChainDestBuff = NULL;
    CpaDcChainOpData *pChainSubOpData = NULL;
    CpaDcChainSubOpData2 *pChainSubOpData2 = NULL;
    dc_opdata_ext_t dcOpDataExt;
    cy_opdata_ext_t cyOpDataExt;
    CpaDcChainRqResults *pResults = NULL;

    /* Check that chain operation is supported */
    pExtendedFtrs = (dc_extd_ftrs_t *)&(
        pDcService->generic_service_info.dcExtendedFeatures);
    switch (operation)
    {
        case CPA_DC_CHAIN_HASH_THEN_COMPRESS:
            if (!(pExtendedFtrs->is_chain_hash_then_compress))
            {
                LAC_INVALID_PARAM_LOG(
                    "DC Chain operation HASH_THEN_COMPRESS not supported");
                return CPA_STATUS_UNSUPPORTED;
            }
            break;
        case CPA_DC_CHAIN_COMPRESS_THEN_AEAD:
            if (!(pExtendedFtrs->is_chain_compress_then_aead))
            {
                LAC_INVALID_PARAM_LOG(
                    "DC Chain operation COMPRESS_THEN_AEAD not supported");
                return CPA_STATUS_UNSUPPORTED;
            }
            break;
        case CPA_DC_CHAIN_AEAD_THEN_DECOMPRESS:
            if (!(pExtendedFtrs->is_chain_aead_then_decompress))
            {
                LAC_INVALID_PARAM_LOG(
                    "DC Chain operation AEAD_THEN_DECOMPRESS not supported");
                return CPA_STATUS_UNSUPPORTED;
            }
            break;
        default:
            LAC_INVALID_PARAM_LOG1("Invalid DC Chain operation=0x%x",
                                   operation);
            return CPA_STATUS_INVALID_PARAM;
    }

    pChainService = pDcService->pDcChainService;
    pSessHead = (dc_chain_session_head_t *)pSessionHandle;

    if (DC_CHAIN_OPDATA_TYPE0 == pChainOpDataExt->opDataType)
    {
        pChainSubOpData = (CpaDcChainOpData *)pChainOpDataExt->pOpData;
    }
    else
    {
        if (CPA_TRUE ==
            ((CpaDcChainOpData2 *)pChainOpDataExt->pOpData)->testIntegrity)
        {
            LAC_UNSUPPORTED_PARAM_LOG(
                "DC Chaining extension testIntegrity is not supported");
            return CPA_STATUS_UNSUPPORTED;
        }
        pChainSubOpData2 =
            ((CpaDcChainOpData2 *)pChainOpDataExt->pOpData)->pChainOpData;
    }

    if (DC_CHAIN_RESULTS_TYPE0 == pResultsExt->resultsType)
    {
        pResults = (CpaDcChainRqResults *)pResultsExt->pResults;
    }
    else
    {
        pResults =
            &((CpaDcChainRqVResults *)pResultsExt->pResults)->chainRqResults;
    }

    /* Allocate chaining cookie */
    pChainCookie = (dc_chain_cookie_t *)Lac_MemPoolEntryAlloc(
        pChainService->dc_chain_cookie_pool);
    if (NULL == pChainCookie)
    {
        return CPA_STATUS_RESOURCE;
    }
    else if ((void *)CPA_STATUS_RETRY == pChainCookie)
    {
        return CPA_STATUS_RETRY;
    }

    /* Populate chaining cookie */
    LAC_OS_BZERO(pChainCookie, sizeof(dc_chain_cookie_t));
    pChainCookie->dcInstance = dcInstance;
    pChainCookie->pSessionHandle = pSessionHandle;
    pChainCookie->extResults = *pResultsExt;
    pChainCookie->callbackTag = callbackTag;

    /* Build chaining common header */
    if (isDcGen2x(pDcService))
    {
        pChainReq = (icp_qat_fw_comp_chain_req_t *)&pChainCookie->request;
        LAC_OS_BZERO(pChainReq, sizeof(icp_qat_fw_comp_chain_req_t));
        osalMemCopy((void *)pChainReq,
                    (void *)(&pSessHead->hdr.comn_hdr),
                    sizeof(icp_qat_comp_chain_req_hdr_t));
        /* Save cookie pointer into request descriptor */
        LAC_MEM_SHARED_WRITE_FROM_PTR(pChainReq->opaque_data, pChainCookie);
    }
    else
    {
        pChainStor2Req = (icp_qat_fw_chain_stor2_req_t *)&pChainCookie->request;
        LAC_OS_BZERO(pChainStor2Req, sizeof(icp_qat_fw_chain_stor2_req_t));

        /* Populates the QAT common request header part of the message
         * (LW 0 to 1) */
        osalMemCopy((void *)(&pChainStor2Req->comn_hdr),
                    (void *)(&pSessHead->hdr.comn_hdr2),
                    sizeof(icp_qat_fw_comn_req_hdr_t));
    }

    osalAtomicInc(&(pSessHead->pendingChainCbCount));
    pTemp = (Cpa8U *)pSessionHandle + sizeof(dc_chain_session_head_t);
    for (i = 0; i < numOperations; i++)
    {
        /* Use intermediate buffer if provided */
        if (NULL == pInterBuff)
        {
            pChainSrcBuff = pSrcBuff;
            pChainDestBuff = pDestBuff;
        }
        else
        {
            /* First chain operation */
            if (FIRST_DC_CHAIN_ITEM == i)
            {
                /* First chain operation is from pSrcBuff => pInterBuff */
                pChainSrcBuff = pSrcBuff;
                pChainDestBuff = pInterBuff;
            }
            else
            {
                /* Next chain operation is from pInterBuff => pDestBuff */
                pChainSrcBuff = pInterBuff;
                pChainDestBuff = pDestBuff;
            }
        }

        if (DC_CHAIN_TYPE_GET(pTemp) == CPA_DC_CHAIN_COMPRESS_DECOMPRESS)
        {
            pTemp += sizeof(CpaDcChainSessionType);
            pDcSessDesc = DC_SESSION_DESC_FROM_CTX_GET(pTemp);
            pDcCookie = (dc_compression_cookie_t *)Lac_MemPoolEntryAlloc(
                pDcService->compression_mem_pool);
            if (NULL == pDcCookie)
            {
                status = CPA_STATUS_RESOURCE;
                goto out_err;
            }
            else if ((void *)CPA_STATUS_RETRY == pDcCookie)
            {
                status = CPA_STATUS_RETRY;
                goto out_err;
            }

            pDcCookie->srcTotalDataLenInBytes = 0;
            if (DC_CHAIN_OPDATA_TYPE0 == pChainOpDataExt->opDataType)
            {
                dcOpDataExt.opDataType = DC_OPDATA_TYPE0;
                dcOpDataExt.pOpData = pChainSubOpData[i].pDcOp;

                /* For Decrypt/Decompress operations, we get the number of bytes
                 * for the 'decompress' from the preceeding 'decrypt' operation.
                 */
                if (CPA_DC_CHAIN_AEAD_THEN_DECOMPRESS == operation)
                {
                    if (NULL != pChainSubOpData[0].pCySymOp)
                    {
                        /* Buffer length is the output from the cipher operation
                         */
                        pDcCookie->srcTotalDataLenInBytes =
                            pChainSubOpData[0]
                                .pCySymOp->messageLenToCipherInBytes;
                    }
                    else
                    {
                        LAC_LOG_ERROR("pCySymOp is NULL\n");
                        status = CPA_STATUS_INVALID_PARAM;
                        goto out_err;
                    }
                }
            }
            else
            {
                dcOpDataExt.opDataType = DC_OPDATA_TYPE1;
                dcOpDataExt.pOpData = pChainSubOpData2[i].pDcOp2;

                /* For Decrypt/Decompress operations, we get the number of bytes
                 * for the 'decompress' from the preceeding 'decrypt' operation.
                 */
                if (CPA_DC_CHAIN_AEAD_THEN_DECOMPRESS == operation)
                {
                    if (NULL != pChainSubOpData2[0].pCySymOp2)
                    {
                        /* Buffer length is the output from the cipher operation
                         */
                        pDcCookie->srcTotalDataLenInBytes =
                            pChainSubOpData2[0]
                                .pCySymOp2->symOpData.messageLenToCipherInBytes;
                    }
                    else
                    {
                        LAC_LOG_ERROR("pCySymOp2 is NULL\n");
                        status = CPA_STATUS_INVALID_PARAM;
                        goto out_err;
                    }
                }
            }

            status = dcChainPrepare_CompRequest(dcInstance,
                                                pChainCookie,
                                                operation,
                                                pTemp,
                                                &dcOpDataExt,
                                                pDcCookie,
                                                pChainSrcBuff,
                                                pChainDestBuff,
                                                pResults);
            if (CPA_STATUS_SUCCESS != status)
                goto out_err;
            pTemp += DC_COMP_SESSION_SIZE;
        }
        else
        {
            pTemp += sizeof(CpaDcChainSessionType);
            pCyCookie = (lac_sym_bulk_cookie_t *)Lac_MemPoolEntryAlloc(
                pChainService->lac_sym_cookie_pool);
            if (NULL == pCyCookie)
            {
                LAC_LOG_ERROR("Cannot get symmetric crypto mem pool entry\n");
                status = CPA_STATUS_RESOURCE;
                goto out_err;
            }
            else if ((void *)CPA_STATUS_RETRY == pCyCookie)
            {
                status = CPA_STATUS_RETRY;
                goto out_err;
            }

            if (DC_CHAIN_OPDATA_TYPE0 == pChainOpDataExt->opDataType)
            {
                cyOpDataExt.opDataType = CY_OPDATA_TYPE0;
                cyOpDataExt.pOpData = pChainSubOpData[i].pCySymOp;
            }
            else
            {
                cyOpDataExt.opDataType = CY_OPDATA_TYPE1;
                cyOpDataExt.pOpData = pChainSubOpData2[i].pCySymOp2;
            }

            status = dcChainPrepare_SymRequest(dcInstance,
                                               pChainCookie,
                                               operation,
                                               pTemp,
                                               pCyCookie,
                                               &cyOpDataExt,
                                               pChainSrcBuff,
                                               pChainDestBuff,
                                               pResults);
            if (CPA_STATUS_SUCCESS != status)
                goto out_err;
            pTemp += LAC_SYM_SESSION_SIZE;
        }
    }

    if (NULL == pDcSessDesc)
    {
        LAC_LOG_ERROR("No compression request on Chaining\n");
        status = CPA_STATUS_INVALID_PARAM;
        goto out_err;
    }

    if (!isDcGen2x(pDcService))
    {
        /* Populates the QAT common request middle part of the message
         * (LW 6 to 13) */
        SalQatMsg_CmnMidWrite(
            (icp_qat_fw_la_bulk_req_t *)&pChainCookie->request,
            pChainCookie,
            DC_DEFAULT_QAT_PTR_TYPE,
            (Cpa64U)NULL,
            (Cpa64U)NULL,
            0,
            0);
    }

    /*Put message on the ring*/
    status = SalQatMsg_transPutMsg(pDcService->trans_handle_compression_tx,
                                   (void *)&pChainCookie->request,
                                   LAC_QAT_DC_REQ_SZ_LW,
                                   LAC_LOG_MSG_DC,
                                   NULL);

    /*update stats*/
    if (CPA_STATUS_SUCCESS == status)
    {
        if (pDcSessDesc->sessDirection == CPA_DC_DIR_COMPRESS)
        {
            COMPRESSION_STAT_INC(numCompRequests, pDcService);
        }
        else
        {
            COMPRESSION_STAT_INC(numDecompRequests, pDcService);
        }
    }
    else
    {
        if (pDcSessDesc->sessDirection == CPA_DC_DIR_COMPRESS)
        {
            COMPRESSION_STAT_INC(numCompRequestsErrors, pDcService);
        }
        else
        {
            COMPRESSION_STAT_INC(numDecompRequestsErrors, pDcService);
        }
        goto out_err;
    }
    return CPA_STATUS_SUCCESS;

out_err:
    osalAtomicDec(&(pSessHead->pendingChainCbCount));
    dcChainOp_MemPoolEntryFree(pDcCookie);
    dcChainOp_MemPoolEntryFree(pCyCookie);
    dcChainOp_MemPoolEntryFree(pChainCookie->pDcRspAddr);
    dcChainOp_MemPoolEntryFree(pChainCookie->pCyRspAddr);
    dcChainOp_MemPoolEntryFree(pChainCookie);
    return status;
}

CpaStatus cpaDcChainPerformOp(CpaInstanceHandle dcInstance,
                              CpaDcSessionHandle pSessionHandle,
                              CpaBufferList *pSrcBuff,
                              CpaBufferList *pDestBuff,
                              CpaDcChainOperations operation,
                              Cpa8U numOpDatas,
                              CpaDcChainOpData *pChainOpData,
                              CpaDcChainRqResults *pResults,
                              void *callbackTag)
{
#ifdef ICP_PARAM_CHECK
    CpaStatus status = CPA_STATUS_SUCCESS;
#endif
    CpaInstanceHandle insHandle = NULL;
    sal_compression_service_t *pService = NULL;
    dc_chain_opdata_ext_t chainOpDataExt;
    dc_chain_results_ext_t chainResultsExt;

    if (CPA_INSTANCE_HANDLE_SINGLE == dcInstance)
    {
        insHandle = dcGetFirstHandle();
    }
    else
    {
        insHandle = dcInstance;
    }

#ifdef ICP_PARAM_CHECK
    LAC_CHECK_NULL_PARAM(pSessionHandle);
    LAC_CHECK_NULL_PARAM(pSrcBuff);
    LAC_CHECK_NULL_PARAM(pDestBuff);
    LAC_CHECK_NULL_PARAM(insHandle);
    LAC_CHECK_NULL_PARAM(pResults);
    LAC_CHECK_NULL_PARAM(pChainOpData);
    SAL_CHECK_ADDR_TRANS_SETUP(insHandle);
    SAL_CHECK_INSTANCE_TYPE(insHandle, SAL_SERVICE_TYPE_COMPRESSION);
    status = dcChainSession_CheckChainSessDesc(
        (dc_chain_session_head_t *)pSessionHandle, operation, numOpDatas);
    LAC_CHECK_STATUS(status);
#endif
    pService = (sal_compression_service_t *)insHandle;
    if (NULL == pService->pDcChainService)
    {
        return CPA_STATUS_UNSUPPORTED;
    }

    /* Check if SAL is initialised otherwise return an error */
    SAL_RUNNING_CHECK(insHandle);

    chainOpDataExt.opDataType = DC_CHAIN_OPDATA_TYPE0;
    chainOpDataExt.pOpData = pChainOpData;
    chainResultsExt.resultsType = DC_CHAIN_RESULTS_TYPE0;
    chainResultsExt.pResults = pResults;
    return dcChainPerformOp(insHandle,
                            pSessionHandle,
                            pSrcBuff,
                            pDestBuff,
                            NULL,
                            operation,
                            numOpDatas,
                            &chainOpDataExt,
                            &chainResultsExt,
                            callbackTag);
}

CpaStatus cpaDcChainPerformOp2(CpaInstanceHandle dcInstance,
                               CpaDcSessionHandle pSessionHandle,
                               CpaBufferList *pSrcBuff,
                               CpaBufferList *pDestBuff,
                               CpaBufferList *pInterBuff,
                               CpaDcChainOpData2 opData,
                               CpaDcChainRqVResults *pResults,
                               void *callbackTag)
{
#ifdef ICP_PARAM_CHECK
    CpaStatus status = CPA_STATUS_SUCCESS;
#endif
    CpaInstanceHandle insHandle = NULL;
    sal_compression_service_t *pService = NULL;
    dc_chain_opdata_ext_t chainOpDataExt;
    dc_chain_results_ext_t chainResultsExt;

    if (CPA_INSTANCE_HANDLE_SINGLE == dcInstance)
    {
        insHandle = dcGetFirstHandle();
    }
    else
    {
        insHandle = dcInstance;
    }

#ifdef ICP_PARAM_CHECK
    LAC_CHECK_NULL_PARAM(pSessionHandle);
    LAC_CHECK_NULL_PARAM(pSrcBuff);
    LAC_CHECK_NULL_PARAM(pDestBuff);
    LAC_CHECK_NULL_PARAM(pInterBuff);
    LAC_CHECK_NULL_PARAM(insHandle);
    LAC_CHECK_NULL_PARAM(pResults);
    LAC_CHECK_NULL_PARAM(opData.pChainOpData);
    SAL_CHECK_ADDR_TRANS_SETUP(insHandle);
    SAL_CHECK_INSTANCE_TYPE(insHandle, SAL_SERVICE_TYPE_COMPRESSION);
    status = dcChainSession_CheckChainSessDesc(
        (dc_chain_session_head_t *)pSessionHandle,
        opData.operation,
        opData.numOpDatas);
    LAC_CHECK_STATUS(status);
#endif
    pService = (sal_compression_service_t *)insHandle;
    if (NULL == pService->pDcChainService)
    {
        return CPA_STATUS_UNSUPPORTED;
    }

    /* Check if SAL is initialised otherwise return an error */
    SAL_RUNNING_CHECK(insHandle);

    chainOpDataExt.opDataType = DC_CHAIN_OPDATA_TYPE1;
    chainOpDataExt.pOpData = &opData;
    chainResultsExt.resultsType = DC_CHAIN_RESULTS_TYPE1;
    chainResultsExt.pResults = pResults;
    return dcChainPerformOp(insHandle,
                            pSessionHandle,
                            pSrcBuff,
                            pDestBuff,
                            pInterBuff,
                            opData.operation,
                            opData.numOpDatas,
                            &chainOpDataExt,
                            &chainResultsExt,
                            callbackTag);
}

/**
 ************************************************************************
 * @ingroup Dc_Chaining
 *    Process symmetric crypto response message for chaining
 *
 * @param[in]    pCySessionDesc   Symmetric crypto session description
 * @param[in]    pCyRespMsg       Symmetric crypto response message
 *
 * @param[out]   pResults         Chaining request result
 *
 * @retval void
 *
 **************************************************************************/
STATIC void dcChainCallback_ProcessSymCrypto(lac_session_desc_t *pCySessionDesc,
                                             icp_qat_fw_la_resp_t *pCyRespMsg,
                                             CpaDcChainRqResults *pResults)
{
    Cpa8U opStatus = ICP_QAT_FW_COMN_STATUS_FLAG_OK;
    Cpa8U comnErr = ERR_CODE_NO_ERROR;
#ifdef ICP_PARAM_CHECK
    LAC_CHECK_NULL_PARAM_NO_RETVAL(pCyRespMsg);
#endif

    opStatus = pCyRespMsg->comn_resp.comn_status;
    comnErr = pCyRespMsg->comn_resp.comn_error.s.comn_err_code;
    /* Log the slice hang and endpoint push/pull error inside the response */
    if (ERR_CODE_SSM_ERROR == (Cpa8S)comnErr)
    {
        LAC_LOG_ERROR("Slice hang detected on CPM cipher or auth slice. ");
    }
    else if (ERR_CODE_ENDPOINT_ERROR == (Cpa8S)comnErr)
    {
        LAC_LOG_ERROR(
            "The PCIe End Point Push/Pull or TI/RI Parity error detected.");
    }

    pResults->cyStatus = CPA_STATUS_FAIL;
    pResults->verifyResult = CPA_FALSE;
    if (ICP_QAT_FW_COMN_STATUS_FLAG_OK ==
        ICP_QAT_FW_COMN_RESP_CRYPTO_STAT_GET(opStatus))
    {
        pResults->cyStatus = CPA_STATUS_SUCCESS;
        if (CPA_TRUE == pCySessionDesc->digestVerify)
            pResults->verifyResult = CPA_TRUE;
    }

    return;
}

/**
 *****************************************************************************
 * @ingroup Dc_Chaining
 *      Process chaining result
 *
 * @description
 *      Process chaining result, it is called at dcCompression_ProcessCallback
 *      when repsonse type is chaining
 *
 * @param[in,out]   pRespMsg     Pointer to firmware response message
 *
 * @retval void
 *
 *****************************************************************************/
void dcChainProcessResults(void *pRespMsg)
{
    CpaStatus status = CPA_STATUS_FAIL;
    dc_chain_cookie_t *pChainCookie = NULL;
    dc_chain_session_head_t *pSessHead = NULL;
    icp_qat_fw_comp_chain_resp_t *pChainRespMsg = NULL;
    Cpa64U *pReqData = NULL;
    void *callbackTag = NULL;
    CpaDcChainRqResults *pResults = NULL;
    dc_chain_results_ext_t *pResultsExt = NULL;
    CpaDcOpData *pOpData = NULL;
    CpaDcCallbackFn pCbFunc = NULL;
    CpaDcRqResults dcResults = { 0 };
    icp_qat_fw_comp_resp_t *pCompRespMsg = NULL;
    dc_compression_cookie_t *pDcCookie = NULL;

    icp_qat_fw_la_resp_t *pCyRespMsg = NULL;
    lac_session_desc_t *pCySessionDesc = NULL;
    lac_sym_bulk_cookie_t *pCyCookie = NULL;
    CpaBoolean chainSubReqFail = CPA_FALSE;
    sal_compression_service_t *pDcService = NULL;
    Cpa8U respStatus = 0;

    pChainRespMsg = (icp_qat_fw_comp_chain_resp_t *)pRespMsg;
#ifdef ICP_PARAM_CHECK
    LAC_ASSERT_NOT_NULL(pChainRespMsg);
    if (!pChainRespMsg)
    {
        goto dcChainProcessResultsExit;
    }
#endif
    LAC_MEM_SHARED_READ_TO_PTR(pChainRespMsg->opaque_data, pReqData);
    pChainCookie = (dc_chain_cookie_t *)pReqData;
#ifdef ICP_PARAM_CHECK
    LAC_ASSERT_NOT_NULL(pChainCookie);
    if (!pChainCookie)
    {
        goto dcChainProcessResultsExit;
    }
#endif
    pSessHead = (dc_chain_session_head_t *)pChainCookie->pSessionHandle;
    callbackTag = pChainCookie->callbackTag;
    pCySessionDesc = pSessHead->pCySessionDesc;
    pCbFunc = pSessHead->pdcChainCb;
    pCompRespMsg =
        (icp_qat_fw_comp_resp_t *)((Cpa8U *)pChainCookie->pDcRspAddr);
    pCyRespMsg = (icp_qat_fw_la_resp_t *)((Cpa8U *)pChainCookie->pCyRspAddr);
    pResultsExt = &pChainCookie->extResults;
#ifdef ICP_PARAM_CHECK
    LAC_ASSERT_NOT_NULL(pResultsExt);
    if (!pResultsExt)
    {
        goto dcChainProcessResultsExit;
    }
#endif
    if (DC_CHAIN_RESULTS_TYPE0 == pResultsExt->resultsType)
    {
        pResults = (CpaDcChainRqResults *)pResultsExt->pResults;
    }
    else
    {
        pResults =
            &((CpaDcChainRqVResults *)pResultsExt->pResults)->chainRqResults;
    }
    pDcCookie =
        (dc_compression_cookie_t *)((Cpa8U *)pChainCookie->pDcCookieAddr);
    pCyCookie = (lac_sym_bulk_cookie_t *)((Cpa8U *)pChainCookie->pCyCookieAddr);
#ifdef ICP_PARAM_CHECK
    LAC_ASSERT_NOT_NULL(pDcCookie);
    if (!pDcCookie)
    {
        goto dcChainProcessResultsExit;
    }
#endif

    pOpData = (CpaDcOpData *)pDcCookie->pDcOpData;
    pDcService = (sal_compression_service_t *)(pDcCookie->dcInstance);

#ifdef ICP_PARAM_CHECK
    LAC_ASSERT_NOT_NULL(pDcService);
    if (!pDcService)
    {
        goto dcChainProcessResultsExit;
    }

    LAC_ASSERT_NOT_NULL(pOpData);
    if (!pOpData)
    {
        goto dcChainProcessResultsExit;
    }

    LAC_ASSERT_NOT_NULL(pCyCookie);
    if (!pCyCookie)
    {
        goto dcChainProcessResultsExit;
    }

    LAC_ASSERT_NOT_NULL(pResults);
    if (!pResults)
    {
        goto dcChainProcessResultsExit;
    }
#endif

    /* Check for Unsupported request */
    respStatus = pChainRespMsg->rspStatus;
    if (ICP_QAT_FW_COMN_RESP_UNSUPPORTED_REQUEST_STAT_GET(respStatus))
    {
        if (DC_CHAIN_RESULTS_TYPE0 != pResultsExt->resultsType)
        {
            ((CpaDcChainRqVResults *)pResultsExt->pResults)->chainStatus =
                CPA_STATUS_FAIL;
        }
    }
    else
    {
        /* Check for crypto or compression errors */
        if (ICP_QAT_FW_COMN_RESP_CRYPTO_STAT_GET(respStatus) ||
            ICP_QAT_FW_COMN_RESP_CMP_STAT_GET(respStatus))
        {
            chainSubReqFail = CPA_TRUE;
            if (DC_CHAIN_RESULTS_TYPE0 != pResultsExt->resultsType)
            {
                ((CpaDcChainRqVResults *)pResultsExt->pResults)->chainStatus =
                    CPA_STATUS_FAIL;
            }
        }
        else
        {
            if (DC_CHAIN_RESULTS_TYPE0 != pResultsExt->resultsType)
            {
                ((CpaDcChainRqVResults *)pResultsExt->pResults)->chainStatus =
                    CPA_STATUS_SUCCESS;
            }
        }

        /*Process crypto response result*/
        dcChainCallback_ProcessSymCrypto(pCySessionDesc, pCyRespMsg, pResults);

        if (CPA_STATUS_SUCCESS == pResults->cyStatus)
        {
            /* Process compression response */
            status =
                dcCompression_CommonProcessCallback(pCompRespMsg, &dcResults);

            /* Save the results */
            if ((pOpData->integrityCrcCheck) &&
                (CPA_TRUE ==
                 pDcService->generic_service_info.integrityCrcCheck))
            {
                pResults->adler32 = pOpData->pCrcData->adler32;
                pResults->crc32 = pOpData->pCrcData->crc32;

                if (DC_CHAIN_RESULTS_TYPE0 != pResultsExt->resultsType)
                {
                    /* Set storedCrc64 to zero as extensions not supported */
                    ((CpaDcChainRqVResults *)pResultsExt->pResults)
                        ->storedCrc64 = 0;
                    ((CpaDcChainRqVResults *)pResultsExt->pResults)->iDcCrc64 =
                        pOpData->pCrcData->integrityCrc64b.iCrc;
                    ((CpaDcChainRqVResults *)pResultsExt->pResults)->oDcCrc64 =
                        pOpData->pCrcData->integrityCrc64b.oCrc;
                }
            }
            else
            {
                pResults->adler32 =
                    pCompRespMsg->comp_resp_pars.crc.legacy.curr_adler_32;
                pResults->crc32 =
                    pCompRespMsg->comp_resp_pars.crc.legacy.curr_crc32;
            }
            pResults->consumed = dcResults.consumed;
            pResults->produced = dcResults.produced;
            pResults->dcStatus = dcResults.status;
            if ((CPA_DC_OK == pResults->dcStatus) && !(chainSubReqFail))
            {
                status = CPA_STATUS_SUCCESS;
            }
        }
    }

#ifdef ICP_PARAM_CHECK
dcChainProcessResultsExit:
#endif
    if (NULL != pChainCookie)
    {
        if (NULL != pChainCookie->pDcRspAddr)
        {
            Lac_MemPoolEntryFree(pChainCookie->pDcRspAddr);
        }
        if (NULL != pChainCookie->pCyRspAddr)
        {
            Lac_MemPoolEntryFree(pChainCookie->pCyRspAddr);
        }
    }
    if (NULL != pDcCookie)
    {
        Lac_MemPoolEntryFree(pDcCookie);
    }
    if (NULL != pCyCookie)
    {
        Lac_MemPoolEntryFree(pCyCookie);
    }
    if (NULL != pChainCookie)
    {
        Lac_MemPoolEntryFree(pChainCookie);
    }
    osalAtomicDec(&(pSessHead->pendingChainCbCount));

    /*pCbFunc can never be NULL, its default is LacSync_GenWakeupSyncCaller*/
    pCbFunc(callbackTag, status);
}
#endif /* End #ifndef ICP_DC_ONLY */
