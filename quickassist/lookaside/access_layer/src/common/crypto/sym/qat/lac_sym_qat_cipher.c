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
 * @file lac_sym_qat_cipher.c      QAT-related support functions for Cipher
 *
 * @ingroup LacSymQat_Cipher
 *
 * @description Functions to support the QAT related operations for Cipher
 ***************************************************************************/

/*
*******************************************************************************
* Include public/global header files
*******************************************************************************
*/

#include "Osal.h"

#include "cpa.h"
#include "icp_accel_devices.h"
#include "icp_adf_debug.h"
#include "lac_sym_qat.h"
#include "lac_sym_qat_cipher.h"
#include "lac_mem.h"
#include "lac_common.h"
#include "cpa_cy_sym.h"
#include "lac_sym_qat.h"
#include "lac_sym_cipher_defs.h"
#include "icp_qat_hw.h"
#include "icp_qat_fw_la.h"


/*****************************************************************************
 *  Internal functions
 *****************************************************************************/

void LacSymQat_CipherCtrlBlockWrite(icp_qat_la_bulk_req_ftr_t *pMsg,
                                    Cpa32U cipherAlgorithm,
                                    Cpa32U targetKeyLenInBytes,
                                    Cpa32U sliceType,
                                    icp_qat_fw_slice_t nextSlice,
                                    Cpa8U cipherCfgOffsetInQuadWord)
{
    icp_qat_fw_cipher_cd_ctrl_hdr_t *cd_ctrl =
        (icp_qat_fw_cipher_cd_ctrl_hdr_t *)&(pMsg->cd_ctrl);

    /* state_padding_sz is nonzero for f8 mode only */
    cd_ctrl->cipher_padding_sz = 0;

    /* Special handling of AES 192 key for UCS slice.
       UCS requires it to have 32 bytes - set is as targetKeyLen
       in this case, and add padding. It makes no sense
       to force applications to provide such key length for couple reasons:
       1. It won't be possible to distinguish between AES 192 and 256 based
          on key lenght only
       2. Only some modes of AES will use UCS slice, then application will
          have to know which ones */
    if (ICP_QAT_FW_LA_USE_UCS_SLICE_TYPE == sliceType &&
        ICP_QAT_HW_AES_192_KEY_SZ == targetKeyLenInBytes)
    {
        targetKeyLenInBytes = ICP_QAT_HW_UCS_AES_192_KEY_SZ;
    }

    /* Base Key is not passed down to QAT in the case of ARC4 or NULL */
    if (LAC_CIPHER_IS_ARC4(cipherAlgorithm) ||
        LAC_CIPHER_IS_NULL(cipherAlgorithm))
    {
        cd_ctrl->cipher_key_sz = 0;
    }
    else if (LAC_CIPHER_IS_KASUMI(cipherAlgorithm))
    {
        cd_ctrl->cipher_key_sz =
            LAC_BYTES_TO_QUADWORDS(ICP_QAT_HW_KASUMI_F8_KEY_SZ);
        cd_ctrl->cipher_padding_sz = ICP_QAT_HW_MODE_F8_NUM_REG_TO_CLEAR;
    }
    else if (LAC_CIPHER_IS_SNOW3G_UEA2(cipherAlgorithm))
    {
        /* For Snow3G UEA2 content descriptor key size is
           key size plus iv size */
        cd_ctrl->cipher_key_sz = LAC_BYTES_TO_QUADWORDS(
            ICP_QAT_HW_SNOW_3G_UEA2_KEY_SZ + ICP_QAT_HW_SNOW_3G_UEA2_IV_SZ);
    }
    else if (LAC_CIPHER_IS_AES_F8(cipherAlgorithm))
    {
        cd_ctrl->cipher_key_sz = LAC_BYTES_TO_QUADWORDS(targetKeyLenInBytes);
        cd_ctrl->cipher_padding_sz = 2 * ICP_QAT_HW_MODE_F8_NUM_REG_TO_CLEAR;
    }
    else if (LAC_CIPHER_IS_ZUC_EEA3(cipherAlgorithm))
    {
        /* For ZUC-128 EEA3 and ZUC-256, the content descriptor
           key size is key size plus iv size */
        if (ICP_QAT_HW_ZUC_3G_EEA3_KEY_SZ == targetKeyLenInBytes)
        {
            cd_ctrl->cipher_key_sz = LAC_BYTES_TO_QUADWORDS(
                ICP_QAT_HW_ZUC_3G_EEA3_KEY_SZ + ICP_QAT_HW_ZUC_3G_EEA3_IV_SZ);
        }
        /* ICP_QAT_HW_ZUC_256_KEY_SZ assumed */
        else
        {
            cd_ctrl->cipher_key_sz = LAC_BYTES_TO_QUADWORDS(
                ICP_QAT_HW_ZUC_256_KEY_SZ + ICP_QAT_HW_ZUC_256_IV_SZ);
        }
    }
    else
    {
        cd_ctrl->cipher_key_sz = LAC_BYTES_TO_QUADWORDS(targetKeyLenInBytes);
    }

    cd_ctrl->cipher_state_sz =
        LAC_BYTES_TO_QUADWORDS(LacSymQat_CipherIvSizeBytesGet(cipherAlgorithm));
    cd_ctrl->cipher_cfg_offset = cipherCfgOffsetInQuadWord;

    /* Amend obtained IV size for ZUC-256 */
    if (LAC_CIPHER_IS_ZUC_256(cipherAlgorithm, targetKeyLenInBytes))
    {
        cd_ctrl->cipher_state_sz =
            LAC_BYTES_TO_QUADWORDS(ICP_QAT_HW_ZUC_256_IV_SZ);
    }

    ICP_QAT_FW_COMN_NEXT_ID_SET(cd_ctrl, nextSlice);
    ICP_QAT_FW_COMN_CURR_ID_SET(cd_ctrl, ICP_QAT_FW_SLICE_CIPHER);
}

void LacSymQat_CipherGetCfgData(lac_session_desc_t *pSession,
                                icp_qat_hw_cipher_algo_t *pAlgorithm,
                                icp_qat_hw_cipher_mode_t *pMode,
                                icp_qat_hw_cipher_dir_t *pDir,
                                icp_qat_hw_cipher_convert_t *pKey_convert)
{

    LAC_ENSURE_NOT_NULL(pSession);
    LAC_ENSURE_NOT_NULL(pAlgorithm);
    LAC_ENSURE_NOT_NULL(pMode);
    LAC_ENSURE_NOT_NULL(pDir);
    LAC_ENSURE_NOT_NULL(pKey_convert);

    /* Set defaults */
    *pKey_convert = ICP_QAT_HW_CIPHER_NO_CONVERT;
    *pAlgorithm = ICP_QAT_HW_CIPHER_ALGO_NULL;
    *pMode = ICP_QAT_HW_CIPHER_ECB_MODE;
    *pDir = ICP_QAT_HW_CIPHER_ENCRYPT;

    /* Set the direction (encrypt/decrypt) */
    if (CPA_CY_SYM_CIPHER_DIRECTION_ENCRYPT == pSession->cipherDirection)
    {
        *pDir = ICP_QAT_HW_CIPHER_ENCRYPT;
    }
    else
    {
        *pDir = ICP_QAT_HW_CIPHER_DECRYPT;
    }

    /* Set the algorithm */
    if (LAC_CIPHER_IS_ARC4(pSession->cipherAlgorithm))
    {
        *pAlgorithm = ICP_QAT_HW_CIPHER_ALGO_ARC4;
        /* Streaming ciphers are a special case. Decrypt = encrypt */
        *pDir = ICP_QAT_HW_CIPHER_ENCRYPT;
    }
    else if (LAC_CIPHER_IS_DES(pSession->cipherAlgorithm))
    {
        *pAlgorithm = ICP_QAT_HW_CIPHER_ALGO_DES;
    }
    else if (LAC_CIPHER_IS_TRIPLE_DES(pSession->cipherAlgorithm))
    {
        *pAlgorithm = ICP_QAT_HW_CIPHER_ALGO_3DES;
    }
    else if (LAC_CIPHER_IS_KASUMI(pSession->cipherAlgorithm))
    {
        *pAlgorithm = ICP_QAT_HW_CIPHER_ALGO_KASUMI;
    }
    else if (LAC_CIPHER_IS_SNOW3G_UEA2(pSession->cipherAlgorithm))
    {
        *pAlgorithm = ICP_QAT_HW_CIPHER_ALGO_SNOW_3G_UEA2;

        /* The KEY_CONVERT bit has to be set for Snow_3G operation */
        *pKey_convert = ICP_QAT_HW_CIPHER_KEY_CONVERT;
    }
    else if (LAC_CIPHER_IS_XTS_MODE(pSession->cipherAlgorithm))
    {
        switch (pSession->cipherKeyLenInBytes)
        {
            case ICP_QAT_HW_AES_128_XTS_KEY_SZ:
                *pAlgorithm = ICP_QAT_HW_CIPHER_ALGO_AES128;
                break;
            case ICP_QAT_HW_AES_256_XTS_KEY_SZ:
                *pAlgorithm = ICP_QAT_HW_CIPHER_ALGO_AES256;
                break;
            default:
                LAC_ENSURE(CPA_FALSE, "Invalid AES XTS key size\n");
                break;
        }
        /* AES decrypt key needs to be reversed. Instead of reversing the key
         * at session registration, it is instead reversed on-the-fly by
         * setting the KEY_CONVERT bit here. In case of UCS slice the key
         * reversing is not supported thus this operation is done in
         * LacSymQat_CipherHwBlockPopulateKeySetup and the KEY_CONVERT bit
         * is not set.
         */
        if (CPA_CY_SYM_CIPHER_DIRECTION_DECRYPT == pSession->cipherDirection &&
            ICP_QAT_FW_LA_USE_UCS_SLICE_TYPE != pSession->cipherSliceType)
        {
            *pKey_convert = ICP_QAT_HW_CIPHER_KEY_CONVERT;
        }
    }
    else if (LAC_CIPHER_IS_AES(pSession->cipherAlgorithm))
    {
        switch (pSession->cipherKeyLenInBytes)
        {
            case ICP_QAT_HW_AES_128_KEY_SZ:
                *pAlgorithm = ICP_QAT_HW_CIPHER_ALGO_AES128;
                break;
            case ICP_QAT_HW_AES_192_KEY_SZ:
                *pAlgorithm = ICP_QAT_HW_CIPHER_ALGO_AES192;
                break;
            case ICP_QAT_HW_AES_256_KEY_SZ:
                *pAlgorithm = ICP_QAT_HW_CIPHER_ALGO_AES256;
                break;
            default:
                LAC_ENSURE(CPA_FALSE, "Invalid AES key size\n");
                break;
        }

        /* AES decrypt key needs to be reversed.  Instead of reversing the key
         * at session registration, it is instead reversed on-the-fly by
         * setting the KEY_CONVERT bit here
         */
        if (CPA_CY_SYM_CIPHER_DIRECTION_DECRYPT == pSession->cipherDirection)
        {
            *pKey_convert = ICP_QAT_HW_CIPHER_KEY_CONVERT;
        }
    }
    else if (LAC_CIPHER_IS_AES_F8(pSession->cipherAlgorithm))
    {
        switch (pSession->cipherKeyLenInBytes)
        {
            case ICP_QAT_HW_AES_128_F8_KEY_SZ:
                *pAlgorithm = ICP_QAT_HW_CIPHER_ALGO_AES128;
                break;
            case ICP_QAT_HW_AES_192_F8_KEY_SZ:
                *pAlgorithm = ICP_QAT_HW_CIPHER_ALGO_AES192;
                break;
            case ICP_QAT_HW_AES_256_F8_KEY_SZ:
                *pAlgorithm = ICP_QAT_HW_CIPHER_ALGO_AES256;
                break;
            default:
                LAC_ENSURE(CPA_FALSE, "Invalid AES F8 key size\n");
                break;
        }
    }
    else if (LAC_CIPHER_IS_ZUC_EEA3(pSession->cipherAlgorithm))
    {
        switch (pSession->cipherKeyLenInBytes)
        {
            case ICP_QAT_HW_ZUC_3G_EEA3_KEY_SZ:
                *pAlgorithm = ICP_QAT_HW_CIPHER_ALGO_ZUC_3G_128_EEA3;
                *pKey_convert = ICP_QAT_HW_CIPHER_KEY_CONVERT;
                break;
            case ICP_QAT_HW_ZUC_256_KEY_SZ:
                *pAlgorithm = ICP_QAT_HW_CIPHER_ALGO_ZUC_256;
                *pKey_convert = ICP_QAT_HW_CIPHER_KEY_CONVERT;
                break;
            default:
                LAC_ENSURE(CPA_FALSE, "Invalid ZUC EEA3 key size\n");
                break;
        }
    }

    else if (LAC_CIPHER_IS_NULL(pSession->cipherAlgorithm))
    {
        *pAlgorithm = ICP_QAT_HW_CIPHER_ALGO_NULL;
    }
    else if (LAC_CIPHER_IS_CHACHA(pSession->cipherAlgorithm))
    {
        switch (pSession->cipherKeyLenInBytes)
        {
            case ICP_QAT_HW_CHACHAPOLY_KEY_SZ:
                *pAlgorithm = ICP_QAT_HW_CIPHER_ALGO_CHACHA20_POLY1305;
                break;
            default:
                LAC_ENSURE(CPA_FALSE, "Invalid CHACHAPOLY key size\n");
                break;
        }
    }
    else if (LAC_CIPHER_IS_SM4(pSession->cipherAlgorithm))
    {
        switch (pSession->cipherKeyLenInBytes)
        {
            case ICP_QAT_HW_SM4_KEY_SZ:
                *pAlgorithm = ICP_QAT_HW_CIPHER_ALGO_SM4;
                break;
            default:
                LAC_ENSURE(CPA_FALSE, "Invalid SM4 key size\n");
                break;
        }

        /* SM4 decrypt key needs to be reversed.  Instead of reversing the key
         * at session registration, it is instead reversed on-the-fly by
         * setting the KEY_CONVERT bit here
         */
        if (CPA_CY_SYM_CIPHER_DIRECTION_DECRYPT == pSession->cipherDirection)
        {
            *pKey_convert = ICP_QAT_HW_CIPHER_KEY_CONVERT;
        }
    }
    else
    {
        LAC_ENSURE(CPA_FALSE, "Algorithm not supported in Cipher\n");
    }

    /* Set the mode */
    if (LAC_CIPHER_IS_CTR_MODE(pSession->cipherAlgorithm))
    {
        *pMode = ICP_QAT_HW_CIPHER_CTR_MODE;
        *pKey_convert = ICP_QAT_HW_CIPHER_NO_CONVERT;
        /* CCP and AES_GCM single pass, despite being limited to CTR/AEAD mode,
         * support both Encrypt/Decrypt modes - this is because of the
         * differences in the hash computation/verification paths in
         * encrypt/decrypt modes respectively.
         * By default CCP is set as CTR Mode.Set AEAD Mode for AES_GCM.
         */
        if (pSession->isSinglePass)
        {
            if (LAC_CIPHER_IS_GCM(pSession->cipherAlgorithm))
                *pMode = ICP_QAT_HW_CIPHER_GCM_MODE;
            else if (LAC_CIPHER_IS_CCM(pSession->cipherAlgorithm))
                *pMode = ICP_QAT_HW_CIPHER_CCM_MODE;
        }
        else
        {
            /* Streaming ciphers are a special case. Decrypt = encrypt
             * Overriding default values previously set for AES
             */
            *pDir = ICP_QAT_HW_CIPHER_ENCRYPT;
        }
    }
    else if (LAC_CIPHER_IS_ECB_MODE(pSession->cipherAlgorithm))
    {
        *pMode = ICP_QAT_HW_CIPHER_ECB_MODE;
    }
    else if (LAC_CIPHER_IS_CBC_MODE(pSession->cipherAlgorithm))
    {
        *pMode = ICP_QAT_HW_CIPHER_CBC_MODE;
    }
    else if (LAC_CIPHER_IS_F8_MODE(pSession->cipherAlgorithm))
    {
        /* Streaming ciphers are a special case. Decrypt = encrypt */
        *pDir = ICP_QAT_HW_CIPHER_ENCRYPT;
        *pMode = ICP_QAT_HW_CIPHER_F8_MODE;
    }
    else if (LAC_CIPHER_IS_ARC4(pSession->cipherAlgorithm))
    {
        ; /* No mode for ARC4 - leave as default */
    }
    else if (LAC_CIPHER_IS_XTS_MODE(pSession->cipherAlgorithm))
    {
        *pMode = ICP_QAT_HW_CIPHER_XTS_MODE;
    }
    else if (LAC_CIPHER_IS_ZUC_EEA3(pSession->cipherAlgorithm))
    {
        ; /* No mode for ZUC EEA3 - leave as default */
    }
    else
    {
        LAC_ENSURE(CPA_FALSE, "Mode not supported in Cipher\n");
    }
}

void LacSymQat_CipherHwBlockPopulateCfgData(lac_session_desc_t *pSession,
                                            const void *pCipherHwBlock,
                                            Cpa32U *pSizeInBytes)
{
    icp_qat_hw_cipher_algo_t algorithm = ICP_QAT_HW_CIPHER_ALGO_NULL;
    icp_qat_hw_cipher_mode_t mode = ICP_QAT_HW_CIPHER_ECB_MODE;
    icp_qat_hw_cipher_dir_t dir = ICP_QAT_HW_CIPHER_ENCRYPT;
    icp_qat_hw_cipher_convert_t key_convert;
    icp_qat_hw_cipher_config_t *pCipherConfig =
        (icp_qat_hw_cipher_config_t *)pCipherHwBlock;
    icp_qat_hw_ucs_cipher_config_t *pUCSCipherConfig =
        (icp_qat_hw_ucs_cipher_config_t *)pCipherHwBlock;

    Cpa32U val, reserved;
    Cpa32U aed_hash_cmp_length = 0;

    LAC_ENSURE_NOT_NULL(pCipherConfig);
    LAC_ENSURE_NOT_NULL(pSizeInBytes);

    *pSizeInBytes = 0;

    LacSymQat_CipherGetCfgData(pSession, &algorithm, &mode, &dir, &key_convert);

    /* Build the cipher config into the hardware setup block */
    if (pSession->isSinglePass)
    {
        aed_hash_cmp_length = pSession->hashResultSize;
        reserved =
            ICP_QAT_HW_CIPHER_CONFIG_BUILD_UPPER(pSession->aadLenInBytes);
    }
    else
    {
        reserved = 0;
    }

    val = ICP_QAT_HW_CIPHER_CONFIG_BUILD(
        mode, algorithm, key_convert, dir, aed_hash_cmp_length);

    /* UCS slice has 128-bit configuration register.
       Leacy cipher slice has 64-bit config register */
    if (ICP_QAT_FW_LA_USE_UCS_SLICE_TYPE == pSession->cipherSliceType)
    {
        pUCSCipherConfig->val = val;
        pUCSCipherConfig->reserved[0] = reserved;
        pUCSCipherConfig->reserved[1] = 0;
        pUCSCipherConfig->reserved[2] = 0;
        *pSizeInBytes = sizeof(icp_qat_hw_ucs_cipher_config_t);

        /* For XTS mode use cached forward/reverse key depending on direction */
        if (LAC_CIPHER_IS_XTS_MODE(pSession->cipherAlgorithm))
        {
            Cpa8U *pCipherKey = (Cpa8U *)pCipherHwBlock + *pSizeInBytes;

            if (ICP_QAT_HW_CIPHER_DECRYPT == dir)
            {
                osalMemCopy(pCipherKey,
                            pSession->cipherAesXtsKey1Reverse,
                            pSession->cipherKeyLenInBytes / 2);
            }
            else
            {
                osalMemCopy(pCipherKey,
                            pSession->cipherAesXtsKey1Forward,
                            pSession->cipherKeyLenInBytes / 2);
            }
        }
    }
    else
    {
        pCipherConfig->val = val;
        pCipherConfig->reserved = reserved;
        *pSizeInBytes = sizeof(icp_qat_hw_cipher_config_t);
    }
}

void LacSymQat_CipherHwBlockPopulateKeySetup(
    lac_session_desc_t *pSessionDesc,
    const CpaCySymCipherSetupData *pCipherSetupData,
    Cpa32U targetKeyLenInBytes,
    Cpa32U sliceType,
    const void *pCipherHwBlock,
    Cpa32U *pSizeInBytes)
{
    Cpa8U *pCipherKey = NULL;
    Cpa32U actualKeyLenInBytes = 0;

    *pSizeInBytes = 0;

    LAC_CHECK_NULL_PARAM_NO_RETVAL(pCipherHwBlock);
    LAC_CHECK_NULL_PARAM_NO_RETVAL(pCipherSetupData);

    /* Key is copied into content descriptor for all cases except for
     * Arc4 and Null cipher */
    if (!(LAC_CIPHER_IS_ARC4(pCipherSetupData->cipherAlgorithm) ||
          LAC_CIPHER_IS_NULL(pCipherSetupData->cipherAlgorithm)))
    {
        pCipherKey = (Cpa8U *)pCipherHwBlock;
        actualKeyLenInBytes = pCipherSetupData->cipherKeyLenInBytes;

        /* Special handling of AES 192 key for UCS slice.
           UCS requires it to have 32 bytes - set is as targetKeyLen
           in this case, and add padding. It makes no sense
           to force applications to provide such key length for couple reasons:
           1. It won't be possible to distinguish between AES 192 and 256 based
              on key lenght only
           2. Only some modes of AES will use UCS slice, then application will
              have to know which ones */
        if (ICP_QAT_FW_LA_USE_UCS_SLICE_TYPE == sliceType &&
            ICP_QAT_HW_AES_192_KEY_SZ == targetKeyLenInBytes)
        {
            targetKeyLenInBytes = ICP_QAT_HW_UCS_AES_192_KEY_SZ;
        }

        /* Set the Cipher key field in the cipher block */
        memcpy(pCipherKey, pCipherSetupData->pCipherKey, actualKeyLenInBytes);
        /* Pad the key with 0's if required */
        if (0 < (targetKeyLenInBytes - actualKeyLenInBytes))
        {
            LAC_OS_BZERO(pCipherKey + actualKeyLenInBytes,
                         targetKeyLenInBytes - actualKeyLenInBytes);
        }
        *pSizeInBytes += targetKeyLenInBytes;

        /* For Kasumi in F8 mode Cipher Key is concatenated with
         * Cipher Key XOR-ed with Key Modifier (CK||CK^KM) */
        if (LAC_CIPHER_IS_KASUMI(pCipherSetupData->cipherAlgorithm))
        {
            Cpa32U wordIndex = 0;
            Cpa32U *pu32CipherKey = (Cpa32U *)pCipherSetupData->pCipherKey;
            Cpa32U *pTempKey = (Cpa32U *)(pCipherKey + targetKeyLenInBytes);

            /* XOR Key with KASUMI F8 key modifier at 4 bytes level */
            for (wordIndex = 0;
                 wordIndex < LAC_BYTES_TO_LONGWORDS(targetKeyLenInBytes);
                 wordIndex++)
            {
                pTempKey[wordIndex] = pu32CipherKey[wordIndex] ^
                                      LAC_CIPHER_KASUMI_F8_KEY_MODIFIER_4_BYTES;
            }

            *pSizeInBytes += targetKeyLenInBytes;

            /* also add padding for F8 */
            *pSizeInBytes +=
                LAC_QUADWORDS_TO_BYTES(ICP_QAT_HW_MODE_F8_NUM_REG_TO_CLEAR);
            LAC_OS_BZERO(
                (Cpa8U *)pTempKey + targetKeyLenInBytes,
                LAC_QUADWORDS_TO_BYTES(ICP_QAT_HW_MODE_F8_NUM_REG_TO_CLEAR));
        }
        /* For AES in F8 mode Cipher Key is concatenated with
         * Cipher Key XOR-ed with Key Mask (CK||CK^KM) */
        else if (LAC_CIPHER_IS_AES_F8(pCipherSetupData->cipherAlgorithm))
        {
            Cpa32U index = 0;
            Cpa8U *pTempKey = pCipherKey + (targetKeyLenInBytes / 2);
            *pSizeInBytes += targetKeyLenInBytes;
            /* XOR Key with key Mask */
            for (index = 0; index < targetKeyLenInBytes; index++)
            {
                pTempKey[index] = pCipherKey[index] ^ pTempKey[index];
            }
            pTempKey = (pCipherKey + targetKeyLenInBytes);
            /* also add padding for AES F8 */
            *pSizeInBytes += 2 * targetKeyLenInBytes;
            LAC_OS_BZERO(pTempKey, 2 * targetKeyLenInBytes);
        }
        else if (LAC_CIPHER_IS_SNOW3G_UEA2(pCipherSetupData->cipherAlgorithm))
        {
            /* For Snow3G zero area after the key for FW */
            LAC_OS_BZERO(pCipherKey + targetKeyLenInBytes,
                         ICP_QAT_HW_SNOW_3G_UEA2_IV_SZ);

            *pSizeInBytes += ICP_QAT_HW_SNOW_3G_UEA2_IV_SZ;
        }
        else if (LAC_CIPHER_IS_ZUC_128_EEA3(pCipherSetupData->cipherAlgorithm,
                                            targetKeyLenInBytes))
        {
            /* For ZUC zero area after the key for FW */
            LAC_OS_BZERO(pCipherKey + targetKeyLenInBytes,
                         ICP_QAT_HW_ZUC_3G_EEA3_IV_SZ);

            *pSizeInBytes += ICP_QAT_HW_ZUC_3G_EEA3_IV_SZ;
        }
        else if (LAC_CIPHER_IS_ZUC_256(pCipherSetupData->cipherAlgorithm,
                                       targetKeyLenInBytes))
        {
            /* For ZUC zero area after the key for FW */
            LAC_OS_BZERO(pCipherKey + targetKeyLenInBytes,
                         ICP_QAT_HW_ZUC_256_IV_SZ);

            *pSizeInBytes += ICP_QAT_HW_ZUC_256_IV_SZ;
        }
        /* For AES in XTS mode Cipher Key is concatenated with
         * second Cipher Key which is used for tweak calculation (CK1||CK2).
         * For decryption Cipher Key needs to be converted to reverse key.*/
        else if (LAC_CIPHER_IS_XTS_MODE(pCipherSetupData->cipherAlgorithm) &&
                 ICP_QAT_FW_LA_USE_UCS_SLICE_TYPE == sliceType)
        {
            LAC_CHECK_NULL_PARAM_NO_RETVAL(pSessionDesc);

            Cpa32U key_len = pCipherSetupData->cipherKeyLenInBytes / 2;
            osalMemCopy(pSessionDesc->cipherAesXtsKey1Forward,
                        pCipherSetupData->pCipherKey,
                        key_len);

            osalAESKeyExpansionForward(
                pSessionDesc->cipherAesXtsKey1Forward,
                key_len,
                (UINT32 *)pSessionDesc->cipherAesXtsKey1Reverse);

            osalMemCopy(pSessionDesc->cipherAesXtsKey2,
                        pCipherSetupData->pCipherKey + key_len,
                        key_len);

            if (CPA_CY_SYM_CIPHER_DIRECTION_DECRYPT ==
                pCipherSetupData->cipherDirection)
            {
                osalMemCopy(
                    pCipherKey, pSessionDesc->cipherAesXtsKey1Reverse, key_len);
            }
            else
            {
                osalMemCopy(
                    pCipherKey, pSessionDesc->cipherAesXtsKey1Forward, key_len);
            }
        }
    }
}

void LacSymQat_CipherHwBlockPopulateCrcData(lac_session_desc_t *pSession,
                                            const void *pCipherHwBlock,
                                            Cpa32U *pSizeInBytes)
{
    icp_qat_fw_chaining_crc64_config_t *pCrcConfig =
        (icp_qat_fw_chaining_crc64_config_t *)pCipherHwBlock;

    LAC_ENSURE_NOT_NULL(pSession);
    LAC_ENSURE_NOT_NULL(pCrcConfig);
    LAC_ENSURE_NOT_NULL(pSizeInBytes);

    pCrcConfig->polynomial = pSession->crcParam.polynomial;
    pCrcConfig->initial_value = pSession->crcParam.initial_value;
    pCrcConfig->reflect_in = pSession->crcParam.reflect_in;
    pCrcConfig->reflect_out = pSession->crcParam.reflect_out;
    pCrcConfig->xor_out = pSession->crcParam.xor_out;

    *pSizeInBytes = sizeof(icp_qat_fw_chaining_crc64_config_t);
}

/*****************************************************************************
 *  External functions
 *****************************************************************************/

Cpa8U LacSymQat_CipherBlockSizeBytesGet(CpaCySymCipherAlgorithm cipherAlgorithm)
{
    if (LAC_CIPHER_IS_ARC4(cipherAlgorithm))
    {
        return LAC_CIPHER_ARC4_BLOCK_LEN_BYTES;
    }
    else if (LAC_CIPHER_IS_AES(cipherAlgorithm) ||
             LAC_CIPHER_IS_AES_F8(cipherAlgorithm))
    {
        return ICP_QAT_HW_AES_BLK_SZ;
    }
    else if (LAC_CIPHER_IS_DES(cipherAlgorithm))
    {
        return ICP_QAT_HW_DES_BLK_SZ;
    }
    else if (LAC_CIPHER_IS_TRIPLE_DES(cipherAlgorithm))
    {
        return ICP_QAT_HW_3DES_BLK_SZ;
    }
    else if (LAC_CIPHER_IS_KASUMI(cipherAlgorithm))
    {
        return ICP_QAT_HW_KASUMI_BLK_SZ;
    }
    else if (LAC_CIPHER_IS_SNOW3G_UEA2(cipherAlgorithm))
    {
        return ICP_QAT_HW_SNOW_3G_BLK_SZ;
    }
    else if (LAC_CIPHER_IS_ZUC_EEA3(cipherAlgorithm))
    {
        return ICP_QAT_HW_ZUC_3G_BLK_SZ;
    }
    else if (LAC_CIPHER_IS_NULL(cipherAlgorithm))
    {
        return LAC_CIPHER_NULL_BLOCK_LEN_BYTES;
    }
    else if (LAC_CIPHER_IS_CHACHA(cipherAlgorithm))
    {
        return ICP_QAT_HW_CHACHAPOLY_BLK_SZ;
    }
    else if (LAC_CIPHER_IS_SM4(cipherAlgorithm))
    {
        return ICP_QAT_HW_SM4_BLK_SZ;
    }
    else
    {
        LAC_ENSURE(CPA_FALSE, "Algorithm not supported in Cipher");
        return 0;
    }
}

Cpa32U LacSymQat_CipherIvSizeBytesGet(CpaCySymCipherAlgorithm cipherAlgorithm)
{
    if (CPA_CY_SYM_CIPHER_ARC4 == cipherAlgorithm)
    {
        return LAC_CIPHER_ARC4_STATE_LEN_BYTES;
    }
    else if (LAC_CIPHER_IS_KASUMI(cipherAlgorithm))
    {
        return ICP_QAT_HW_KASUMI_BLK_SZ;
    }
    else if (LAC_CIPHER_IS_SNOW3G_UEA2(cipherAlgorithm))
    {
        return ICP_QAT_HW_SNOW_3G_UEA2_IV_SZ;
    }
    else if (LAC_CIPHER_IS_ZUC_EEA3(cipherAlgorithm))
    {
        return ICP_QAT_HW_ZUC_3G_EEA3_IV_SZ;
    }
    else if (LAC_CIPHER_IS_CHACHA(cipherAlgorithm))
    {
        return ICP_QAT_HW_CHACHAPOLY_IV_SZ;
    }
    else if (LAC_CIPHER_IS_ECB_MODE(cipherAlgorithm))
    {
        return 0;
    }
    else
    {
        return (Cpa32U)LacSymQat_CipherBlockSizeBytesGet(cipherAlgorithm);
    }
}

CpaStatus LacSymQat_CipherRequestParamsPopulate(
    lac_session_desc_t *pSessionDesc,
    icp_qat_fw_la_bulk_req_t *pReq,
    Cpa32U cipherOffsetInBytes,
    Cpa32U cipherLenInBytes,
    Cpa64U ivBufferPhysAddr,
    Cpa8U *pIvBufferVirt)
{
    icp_qat_fw_la_cipher_req_params_t *pCipherReqParams;
    icp_qat_fw_cipher_cd_ctrl_hdr_t *pCipherCdCtrlHdr;
    icp_qat_fw_serv_specif_flags *pCipherSpecificFlags;

#ifdef ICP_PARAM_CHECK

    LAC_ENSURE(NULL != pReq,
               "LacSymQat_CipherRequestParamsPopulate - "
               "pReq is NULL\n");
#endif
    pCipherReqParams = (icp_qat_fw_la_cipher_req_params_t *)
                               ((Cpa8U *)&(pReq->serv_specif_rqpars) +
                               ICP_QAT_FW_CIPHER_REQUEST_PARAMETERS_OFFSET);
    pCipherCdCtrlHdr = (icp_qat_fw_cipher_cd_ctrl_hdr_t *)&(pReq->cd_ctrl);
    pCipherSpecificFlags = &(pReq->comn_hdr.serv_specif_flags);

    pCipherReqParams->cipher_offset = cipherOffsetInBytes;
    pCipherReqParams->cipher_length = cipherLenInBytes;

    /* Don't copy the buffer into the Msg if
     * it's too big for the cipher_IV_array
     * OR if the FW needs to update it
     * OR if there's no buffer supplied
     * OR if last partial
     */
    if ((pCipherCdCtrlHdr->cipher_state_sz >
         LAC_SYM_QAT_HASH_IV_REQ_MAX_SIZE_QW) ||
        (ICP_QAT_FW_LA_UPDATE_STATE_GET(*pCipherSpecificFlags) ==
         ICP_QAT_FW_LA_UPDATE_STATE) ||
        (pIvBufferVirt == NULL) ||
        (ICP_QAT_FW_LA_PARTIAL_GET(*pCipherSpecificFlags) ==
         ICP_QAT_FW_LA_PARTIAL_END))
    {
        /* Populate the field with a ptr to the flat buffer */
        pCipherReqParams->u.s.cipher_IV_ptr = ivBufferPhysAddr;
        pCipherReqParams->u.s.resrvd1 = 0;
        /* Set the flag indicating the field format */
        ICP_QAT_FW_LA_CIPH_IV_FLD_FLAG_SET(*pCipherSpecificFlags,
                                           ICP_QAT_FW_CIPH_IV_64BIT_PTR);
    }
    else
    {
        /* Populate the field with the contents of the buffer,
         * zero field first as data may be smaller than the field */
        osalMemSet(pCipherReqParams->u.cipher_IV_array,
                   0,
                   LAC_LONGWORDS_TO_BYTES(ICP_QAT_FW_NUM_LONGWORDS_4));

        /* In case of XTS mode using UCS slice always embedd IV.
         * IV provided by user needs to be encrypted to calculate initial tweak,
         * use pCipherReqParams->u.cipher_IV_array as destination buffer for
         * tweak value */
        if (ICP_QAT_FW_LA_USE_UCS_SLICE_TYPE == pSessionDesc->cipherSliceType &&
            LAC_CIPHER_IS_XTS_MODE(pSessionDesc->cipherAlgorithm))
        {
            osalAESEncrypt(pSessionDesc->cipherAesXtsKey2,
                           pSessionDesc->cipherKeyLenInBytes / 2,
                           pIvBufferVirt,
                           (Cpa8U *)pCipherReqParams->u.cipher_IV_array);
        }
        else
        {
            osalMemCopy(
                pCipherReqParams->u.cipher_IV_array,
                pIvBufferVirt,
                LAC_QUADWORDS_TO_BYTES(pCipherCdCtrlHdr->cipher_state_sz));
        }
        /* Set the flag indicating the field format */
        ICP_QAT_FW_LA_CIPH_IV_FLD_FLAG_SET(*pCipherSpecificFlags,
                                           ICP_QAT_FW_CIPH_IV_16BYTE_DATA);
    }

    return CPA_STATUS_SUCCESS;
}

void LacSymQat_CipherArc4StateInit(const Cpa8U *pKey,
                                   Cpa32U keyLenInBytes,
                                   Cpa8U *pArc4CipherState)
{
    Cpa32U i = 0;
    Cpa32U j = 0;
    Cpa32U k = 0;

    for (i = 0; i < LAC_CIPHER_ARC4_KEY_MATRIX_LEN_BYTES; ++i)
    {
        pArc4CipherState[i] = (Cpa8U)i;
    }

    for (i = 0, k = 0; i < LAC_CIPHER_ARC4_KEY_MATRIX_LEN_BYTES; ++i, ++k)
    {
        Cpa8U swap = 0;

        if (k >= keyLenInBytes)
            k -= keyLenInBytes;

        j = (j + pArc4CipherState[i] + pKey[k]);
        if (j >= LAC_CIPHER_ARC4_KEY_MATRIX_LEN_BYTES)
            j %= LAC_CIPHER_ARC4_KEY_MATRIX_LEN_BYTES;

        /* Swap state[i] & state[j] */
        swap = pArc4CipherState[i];
        pArc4CipherState[i] = pArc4CipherState[j];
        pArc4CipherState[j] = swap;
    }

    /* Initialise i & j values for QAT */
    pArc4CipherState[LAC_CIPHER_ARC4_KEY_MATRIX_LEN_BYTES] = 0;
    pArc4CipherState[LAC_CIPHER_ARC4_KEY_MATRIX_LEN_BYTES + 1] = 0;
}

/* Update the cipher_key_sz in the Request cache prepared and stored
 * in the session */
void LacSymQat_CipherXTSModeUpdateKeyLen(lac_session_desc_t *pSessionDesc,
                                         Cpa32U newKeySizeInBytes)
{
    icp_qat_fw_cipher_cd_ctrl_hdr_t *pCipherControlBlock = NULL;

    pCipherControlBlock =
        (icp_qat_fw_cipher_cd_ctrl_hdr_t *)&(pSessionDesc->reqCacheFtr.cd_ctrl);

    pCipherControlBlock->cipher_key_sz =
        LAC_BYTES_TO_QUADWORDS(newKeySizeInBytes);
}
