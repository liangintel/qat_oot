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
 * @file  kpt_rsa_sample_op.c
 *
 *****************************************************************************/

#include "kpt_sample_common.h"

#define BITS_IN_BYTE 8
#define P256_SIZE_IN_BITS 256
#define SHA512_DIGEST_LEN_IN_BYTES (512 / BITS_IN_BYTE)

/* AAD value is hardcode in KPT rather than random number, see KPT document */
static const Cpa8U aad[] = { 0x06, 0x08, 0x2A, 0x86, 0x48,
                             0xCE, 0x3D, 0x03, 0x01, 0x07 };

static const Cpa8U iv[] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
                            0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc };

static const Cpa8U sampleSWK[] = {
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x11, 0x22, 0x33,
    0x44, 0x55, 0x66, 0x77, 0x88, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
    0x77, 0x88, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
};

static Cpa8U nist_p256_a[] = { 0xff, 0xff, 0xff, 0xff, 0x0,  0x0,  0x0,  0x1,
                               0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
                               0x0,  0x0,  0x0,  0x0,  0xff, 0xff, 0xff, 0xff,
                               0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfc };

static Cpa8U nist_p256_b[] = { 0x5a, 0xc6, 0x35, 0xd8, 0xaa, 0x3a, 0x93, 0xe7,
                               0xb3, 0xeb, 0xbd, 0x55, 0x76, 0x98, 0x86, 0xbc,
                               0x65, 0x1d, 0x6,  0xb0, 0xcc, 0x53, 0xb0, 0xf6,
                               0x3b, 0xce, 0x3c, 0x3e, 0x27, 0xd2, 0x60, 0x4b };

static Cpa8U nist_p256_r[] = { 0xff, 0xff, 0xff, 0xff, 0x0,  0x0,  0x0,  0x0,
                               0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                               0xbc, 0xe6, 0xfa, 0xad, 0xa7, 0x17, 0x9e, 0x84,
                               0xf3, 0xb9, 0xca, 0xc2, 0xfc, 0x63, 0x25, 0x51 };

static Cpa8U nist_p256_p[] = { 0xff, 0xff, 0xff, 0xff, 0x0,  0x0,  0x0,  0x1,
                               0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
                               0x0,  0x0,  0x0,  0x0,  0xff, 0xff, 0xff, 0xff,
                               0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

static Cpa8U nist_p256_xg[] = {
    0x6b, 0x17, 0xd1, 0xf2, 0xe1, 0x2c, 0x42, 0x47, 0xf8, 0xbc, 0xe6,
    0xe5, 0x63, 0xa4, 0x40, 0xf2, 0x77, 0x3,  0x7d, 0x81, 0x2d, 0xeb,
    0x33, 0xa0, 0xf4, 0xa1, 0x39, 0x45, 0xd8, 0x98, 0xc2, 0x96
};

static Cpa8U nist_p256_yg[] = {
    0x4f, 0xe3, 0x42, 0xe2, 0xfe, 0x1a, 0x7f, 0x9b, 0x8e, 0xe7, 0xeb,
    0x4a, 0x7c, 0xf,  0x9e, 0x16, 0x2b, 0xce, 0x33, 0x57, 0x6b, 0x31,
    0x5e, 0xce, 0xcb, 0xb6, 0x40, 0x68, 0x37, 0xbf, 0x51, 0xf5
};

static Cpa8U nist_p256_xp[] = {
    0xd4, 0x7,  0x8c, 0x89, 0x34, 0xc7, 0x7b, 0x8f, 0xb7, 0xde, 0x74,
    0x38, 0x5a, 0xf4, 0x93, 0x11, 0x36, 0x17, 0x44, 0x4,  0xce, 0x79,
    0x9b, 0x0,  0x5a, 0xe6, 0x7c, 0xfb, 0xaf, 0x92, 0x1b, 0x14
};

static Cpa8U nist_p256_yp[] = { 0x3a, 0x5b, 0xba, 0x62, 0x84, 0xf6, 0x54, 0x79,
                                0xa4, 0x70, 0xb1, 0xb1, 0xaa, 0x40, 0x32, 0xe3,
                                0xf4, 0x8,  0xad, 0x98, 0xdc, 0x67, 0xa8, 0x8a,
                                0xfd, 0xf0, 0x4e, 0x5c, 0xd3, 0x61, 0x2d, 0x1 };

static Cpa8U nist_p256_d[] = { 0xb4, 0xf0, 0x54, 0x82, 0xa0, 0x24, 0x3,  0x9a,
                               0x4f, 0x69, 0x1d, 0x4c, 0xd5, 0xbe, 0xbc, 0x0,
                               0xca, 0x7e, 0xa2, 0x4,  0x3c, 0xde, 0xbb, 0x52,
                               0xd,  0x3e, 0x3f, 0xa0, 0xc9, 0xd0, 0xb5, 0x2b };

static void freeEcdsaOpDataMemory(CpaCyEcdsaVerifyOpData ecdsaVerifyOpdata)
{
    if (NULL != ecdsaVerifyOpdata.a.pData)
    {
        qaeMemFreeNUMA((void **)&ecdsaVerifyOpdata.a.pData);
    }
    if (NULL != ecdsaVerifyOpdata.b.pData)
    {
        qaeMemFreeNUMA((void **)&ecdsaVerifyOpdata.b.pData);
    }
    if (NULL != ecdsaVerifyOpdata.n.pData)
    {
        qaeMemFreeNUMA((void **)&ecdsaVerifyOpdata.n.pData);
    }
    if (NULL != ecdsaVerifyOpdata.q.pData)
    {
        qaeMemFreeNUMA((void **)&ecdsaVerifyOpdata.q.pData);
    }
    if (NULL != ecdsaVerifyOpdata.xg.pData)
    {
        qaeMemFreeNUMA((void **)&ecdsaVerifyOpdata.xg.pData);
    }
    if (NULL != ecdsaVerifyOpdata.yg.pData)
    {
        qaeMemFreeNUMA((void **)&ecdsaVerifyOpdata.yg.pData);
    }
    if (NULL != ecdsaVerifyOpdata.xp.pData)
    {
        qaeMemFreeNUMA((void **)&ecdsaVerifyOpdata.xp.pData);
    }
    if (NULL != ecdsaVerifyOpdata.yp.pData)
    {
        qaeMemFreeNUMA((void **)&ecdsaVerifyOpdata.yp.pData);
    }
}

static CpaStatus kptEcdsaSignRS(CpaInstanceHandle cyInstHandle,
                                Cpa32U node,
                                CpaFlatBuffer *r,
                                CpaFlatBuffer *s,
                                CpaFlatBuffer digest)
{
    CpaStatus status = CPA_STATUS_SUCCESS;
    CpaCyKptEcdsaSignRSOpData kptSignRSOpData = { 0 };
    CpaCyKptUnwrapContext kptUnwrapCtx = { 0 };
    CpaFlatBuffer wpkAndAuthTag = { 0 };
    CpaBoolean stat = CPA_TRUE;
    Cpa32U wpkSize = 0;
    Cpa8U authTag[AUTH_TAG_LEN] = { 0 };
    CpaFlatBuffer privateKey = { 0 };
    CpaBoolean signStatus = CPA_FALSE;

    status = encryptAndLoadSWK(
        cyInstHandle, node, &kptUnwrapCtx.kptHandle, (Cpa8U *)sampleSWK);
    if (CPA_STATUS_SUCCESS != status)
    {
        PRINT_ERR("encrypt And Load SWK failed!\n");
        return status;
    }

    memcpy(kptUnwrapCtx.iv, iv, sizeof(iv));
    memcpy(kptUnwrapCtx.additionalAuthData, aad, sizeof(aad));
    kptUnwrapCtx.aadLenInBytes = sizeof(aad);

    wpkAndAuthTag.dataLenInBytes =
        (P256_SIZE_IN_BITS + BITS_IN_BYTE - 1) / BITS_IN_BYTE + AUTH_TAG_LEN;
    wpkAndAuthTag.pData =
        qaeMemAllocNUMA(wpkAndAuthTag.dataLenInBytes, node, BYTE_ALIGNMENT_64);
    if (NULL == wpkAndAuthTag.pData)
    {
        PRINT_ERR("qaeMemAlloc wpkAndAuthTag error\n");
        return CPA_STATUS_FAIL;
    }
    memset(wpkAndAuthTag.pData, 0, wpkAndAuthTag.dataLenInBytes);

    privateKey.dataLenInBytes =
        (P256_SIZE_IN_BITS + BITS_IN_BYTE - 1) / BITS_IN_BYTE;
    privateKey.pData = qaeMemAlloc(privateKey.dataLenInBytes);
    if (NULL == privateKey.pData)
    {
        PRINT_ERR("qaeMemAlloc privateKey failed\n");
        qaeMemFreeNUMA((void **)&wpkAndAuthTag.pData);
        return CPA_STATUS_FAIL;
    }
    memset(privateKey.pData, 0, privateKey.dataLenInBytes);
    memcpy(privateKey.pData + privateKey.dataLenInBytes - sizeof(nist_p256_d),
           nist_p256_d,
           sizeof(nist_p256_d));

    stat = encryptPrivateKey(privateKey.pData,
                             privateKey.dataLenInBytes,
                             (Cpa8U *)sampleSWK,
                             (Cpa8U *)iv,
                             sizeof(iv),
                             wpkAndAuthTag.pData,
                             &wpkSize,
                             authTag,
                             (Cpa8U *)aad,
                             sizeof(aad));
    if (CPA_TRUE != stat)
    {
        PRINT_ERR("encryptPrivateKey failed!\n");
        qaeMemFreeNUMA((void **)&wpkAndAuthTag.pData);
        qaeMemFree((void **)&privateKey.pData);
        return CPA_STATUS_FAIL;
    }

    memcpy(wpkAndAuthTag.pData + wpkSize, authTag, AUTH_TAG_LEN);

    kptSignRSOpData.privateKey = wpkAndAuthTag;

    kptSignRSOpData.m.dataLenInBytes = digest.dataLenInBytes;
    kptSignRSOpData.m.pData = qaeMemAllocNUMA(
        kptSignRSOpData.m.dataLenInBytes, node, BYTE_ALIGNMENT_64);
    if (NULL == kptSignRSOpData.m.pData)
    {
        PRINT_ERR("qaeMemAllocNUMA kptSignRSOpData.m.pData failed!\n");
        qaeMemFreeNUMA((void **)&wpkAndAuthTag.pData);
        qaeMemFree((void **)&privateKey.pData);
        return CPA_STATUS_FAIL;
    }
    memset(kptSignRSOpData.m.pData, 0, kptSignRSOpData.m.dataLenInBytes);
    memcpy(kptSignRSOpData.m.pData,
           digest.pData,
           kptSignRSOpData.m.dataLenInBytes);

    status = cpaCyKptEcdsaSignRS(cyInstHandle,
                                 NULL,
                                 NULL,
                                 &kptSignRSOpData,
                                 &signStatus,
                                 r,
                                 s,
                                 &kptUnwrapCtx);
    if ((CPA_STATUS_SUCCESS != status) || (CPA_TRUE != signStatus))
    {
        PRINT_ERR("cpaCyKptEcdsaSignRS failed\n");
        status = CPA_STATUS_FAIL;
    }

    qaeMemFreeNUMA((void **)&wpkAndAuthTag.pData);
    qaeMemFree((void **)&privateKey.pData);
    qaeMemFreeNUMA((void **)&kptSignRSOpData.m.pData);
    return status;
}

static CpaStatus ecdsaVerifySign(CpaInstanceHandle cyInstHandle,
                                 Cpa32U node,
                                 CpaFlatBuffer r,
                                 CpaFlatBuffer s,
                                 CpaFlatBuffer digest)
{
    CpaStatus status = CPA_STATUS_SUCCESS;
    CpaCyEcdsaVerifyOpData ecdsaVerifyOpdata = { 0 };
    CpaBoolean verifyStatus = CPA_FALSE;

    ecdsaVerifyOpdata.fieldType = CPA_CY_EC_FIELD_TYPE_PRIME;

    ecdsaVerifyOpdata.a.dataLenInBytes = sizeof(nist_p256_a);
    ecdsaVerifyOpdata.a.pData = (Cpa8U *)qaeMemAllocNUMA(
        ecdsaVerifyOpdata.a.dataLenInBytes, node, BYTE_ALIGNMENT_64);
    if (NULL == ecdsaVerifyOpdata.a.pData)
    {
        PRINT_ERR("qaeMemAllocNUMA a elliptic curve coefficient failed\n");
        freeEcdsaOpDataMemory(ecdsaVerifyOpdata);
        return CPA_STATUS_FAIL;
    }
    memset(ecdsaVerifyOpdata.a.pData, 0, ecdsaVerifyOpdata.a.dataLenInBytes);
    memcpy(ecdsaVerifyOpdata.a.pData, nist_p256_a, sizeof(nist_p256_a));

    ecdsaVerifyOpdata.b.dataLenInBytes = sizeof(nist_p256_b);
    ecdsaVerifyOpdata.b.pData = (Cpa8U *)qaeMemAllocNUMA(
        ecdsaVerifyOpdata.b.dataLenInBytes, node, BYTE_ALIGNMENT_64);
    if (NULL == ecdsaVerifyOpdata.b.pData)
    {
        PRINT_ERR("qaeMemAllocNUMA b elliptic curve coefficient failed\n");
        freeEcdsaOpDataMemory(ecdsaVerifyOpdata);
        return CPA_STATUS_FAIL;
    }
    memset(ecdsaVerifyOpdata.b.pData, 0, ecdsaVerifyOpdata.b.dataLenInBytes);
    memcpy(ecdsaVerifyOpdata.b.pData, nist_p256_b, sizeof(nist_p256_b));

    ecdsaVerifyOpdata.m.dataLenInBytes = digest.dataLenInBytes;
    ecdsaVerifyOpdata.m.pData = (Cpa8U *)qaeMemAllocNUMA(
        ecdsaVerifyOpdata.m.dataLenInBytes, node, BYTE_ALIGNMENT_64);
    if (NULL == ecdsaVerifyOpdata.m.pData)
    {
        PRINT_ERR("qaeMemAllocNUMA digest failed\n");
        freeEcdsaOpDataMemory(ecdsaVerifyOpdata);
        return CPA_STATUS_FAIL;
    }
    memcpy(ecdsaVerifyOpdata.m.pData,
           digest.pData,
           ecdsaVerifyOpdata.m.dataLenInBytes);

    ecdsaVerifyOpdata.n.dataLenInBytes = sizeof(nist_p256_r);
    ecdsaVerifyOpdata.n.pData = (Cpa8U *)qaeMemAllocNUMA(
        ecdsaVerifyOpdata.n.dataLenInBytes, node, BYTE_ALIGNMENT_64);
    if (NULL == ecdsaVerifyOpdata.n.pData)
    {
        PRINT_ERR("qaeMemAllocNUMA order of the base point G failed\n");
        freeEcdsaOpDataMemory(ecdsaVerifyOpdata);
        return CPA_STATUS_FAIL;
    }
    memset(ecdsaVerifyOpdata.n.pData, 0, ecdsaVerifyOpdata.n.dataLenInBytes);
    memcpy(ecdsaVerifyOpdata.n.pData, nist_p256_r, sizeof(nist_p256_r));

    ecdsaVerifyOpdata.q.dataLenInBytes = sizeof(nist_p256_p);
    ecdsaVerifyOpdata.q.pData = (Cpa8U *)qaeMemAllocNUMA(
        ecdsaVerifyOpdata.q.dataLenInBytes, node, BYTE_ALIGNMENT_64);
    if (NULL == ecdsaVerifyOpdata.q.pData)
    {
        PRINT_ERR("qaeMemAllocNUMA prime modulus or irreducible polynomial "
                  "over GF failed\n");
        freeEcdsaOpDataMemory(ecdsaVerifyOpdata);
        return CPA_STATUS_FAIL;
    }
    memset(ecdsaVerifyOpdata.q.pData, 0, ecdsaVerifyOpdata.q.dataLenInBytes);
    memcpy(ecdsaVerifyOpdata.q.pData, nist_p256_p, sizeof(nist_p256_p));

    ecdsaVerifyOpdata.r = r;
    ecdsaVerifyOpdata.s = s;

    ecdsaVerifyOpdata.xg.dataLenInBytes = sizeof(nist_p256_xg);
    ecdsaVerifyOpdata.xg.pData = (Cpa8U *)qaeMemAllocNUMA(
        ecdsaVerifyOpdata.xg.dataLenInBytes, node, BYTE_ALIGNMENT_64);
    if (NULL == ecdsaVerifyOpdata.xg.pData)
    {
        PRINT_ERR("qaeMemAllocNUMA x coordinate of base point G failed\n");
        freeEcdsaOpDataMemory(ecdsaVerifyOpdata);
        return CPA_STATUS_FAIL;
    }
    memset(ecdsaVerifyOpdata.xg.pData, 0, ecdsaVerifyOpdata.xg.dataLenInBytes);
    memcpy(ecdsaVerifyOpdata.xg.pData, nist_p256_xg, sizeof(nist_p256_xg));

    ecdsaVerifyOpdata.yg.dataLenInBytes = sizeof(nist_p256_yg);
    ecdsaVerifyOpdata.yg.pData = (Cpa8U *)qaeMemAllocNUMA(
        ecdsaVerifyOpdata.yg.dataLenInBytes, node, BYTE_ALIGNMENT_64);
    if (NULL == ecdsaVerifyOpdata.yg.pData)
    {
        PRINT_ERR("qaeMemAllocNUMA y coordinate of base point G failed\n");
        freeEcdsaOpDataMemory(ecdsaVerifyOpdata);
        return CPA_STATUS_FAIL;
    }
    memset(ecdsaVerifyOpdata.yg.pData, 0, ecdsaVerifyOpdata.yg.dataLenInBytes);
    memcpy(ecdsaVerifyOpdata.yg.pData, nist_p256_yg, sizeof(nist_p256_yg));

    ecdsaVerifyOpdata.xp.dataLenInBytes = sizeof(nist_p256_xp);
    ecdsaVerifyOpdata.xp.pData = (Cpa8U *)qaeMemAllocNUMA(
        ecdsaVerifyOpdata.xp.dataLenInBytes, node, BYTE_ALIGNMENT_64);
    if (NULL == ecdsaVerifyOpdata.xp.pData)
    {
        PRINT_ERR(
            "qaeMemAllocNUMA x coordinate of point P (public key) failed\n");
        freeEcdsaOpDataMemory(ecdsaVerifyOpdata);
        return CPA_STATUS_FAIL;
    }
    memset(ecdsaVerifyOpdata.xp.pData, 0, ecdsaVerifyOpdata.xp.dataLenInBytes);
    memcpy(ecdsaVerifyOpdata.xp.pData, nist_p256_xp, sizeof(nist_p256_xp));

    ecdsaVerifyOpdata.yp.dataLenInBytes = sizeof(nist_p256_yp);
    ecdsaVerifyOpdata.yp.pData = (Cpa8U *)qaeMemAllocNUMA(
        ecdsaVerifyOpdata.yp.dataLenInBytes, node, BYTE_ALIGNMENT_64);
    if (NULL == ecdsaVerifyOpdata.yp.pData)
    {
        PRINT_ERR("qaeMemAllocNUMA y coordinate of base point G failed\n");
        freeEcdsaOpDataMemory(ecdsaVerifyOpdata);
        return CPA_STATUS_FAIL;
    }
    memset(ecdsaVerifyOpdata.yp.pData, 0, ecdsaVerifyOpdata.yp.dataLenInBytes);
    memcpy(ecdsaVerifyOpdata.yp.pData, nist_p256_yp, sizeof(nist_p256_yp));

    status = cpaCyEcdsaVerify(
        cyInstHandle, NULL, NULL, &ecdsaVerifyOpdata, &verifyStatus);
    if ((CPA_STATUS_SUCCESS != status) || (CPA_TRUE != verifyStatus))
    {
        PRINT_ERR("ECDSA Verify function failed\n");
        status = CPA_STATUS_FAIL;
    }

    freeEcdsaOpDataMemory(ecdsaVerifyOpdata);
    return status;
}

CpaStatus kptEcdsaOp(void)
{
    CpaStatus status = CPA_STATUS_SUCCESS;
    CpaInstanceHandle cyInstHandle = 0;
    CpaCyCapabilitiesInfo pCapInfo = { 0 };
    CpaInstanceInfo2 instanceInfo = { 0 };
    Cpa32U node = 0;
    CpaFlatBuffer r = { 0 };
    CpaFlatBuffer s = { 0 };
    CpaFlatBuffer msg = { 0 };
    CpaFlatBuffer z = { 0 };
    CpaFlatBuffer digest = { 0 };

    sampleAsymGetInstance(&cyInstHandle);
    if (cyInstHandle == NULL)
    {
        return CPA_STATUS_FAIL;
    }

    status = cpaCyInstanceGetInfo2(cyInstHandle, &instanceInfo);
    if (CPA_STATUS_SUCCESS != status)
    {
        PRINT_ERR("%s::%d cpaCyInstanceGetInfo2 failed", __func__, __LINE__);
        return status;
    }

    status = queryCapabilitiesForKpt(cyInstHandle, instanceInfo, &pCapInfo);
    if ((CPA_STATUS_SUCCESS != status) ||
        ((CPA_STATUS_SUCCESS == status) && !pCapInfo.kptSupported))
    {
        return status;
    }

    status = cpaCyStartInstance(cyInstHandle);
    if (CPA_STATUS_SUCCESS != status)
    {
        PRINT_ERR("cpaCyStartInstance failed\n");
        return status;
    }
    status = cpaCySetAddressTranslation(cyInstHandle, sampleVirtToPhys);
    if (CPA_STATUS_SUCCESS != status)
    {
        PRINT_ERR("cpaCySetAddressTranslation failed\n");
        cpaCyStopInstance(cyInstHandle);
        return status;
    }
    sampleCyStartPolling(cyInstHandle);

    node = instanceInfo.nodeAffinity;

    msg.dataLenInBytes = (P256_SIZE_IN_BITS + BITS_IN_BYTE - 1) / BITS_IN_BYTE;
    msg.pData =
        (Cpa8U *)qaeMemAllocNUMA(msg.dataLenInBytes, node, BYTE_ALIGNMENT_64);
    if (NULL == msg.pData)
    {
        PRINT_ERR("qaeMemAllocNUMA msg failed\n");
        sampleCyStopPolling();
        cpaCyStopInstance(cyInstHandle);
        return CPA_STATUS_FAIL;
    }
    memset(msg.pData, 0, msg.dataLenInBytes);

    PRINT("\ngenerate random message\n");
    genRandomData(msg.pData, msg.dataLenInBytes);
    hexLog(msg.pData, msg.dataLenInBytes, "message");

    digest.dataLenInBytes = SHA512_DIGEST_LEN_IN_BYTES;
    digest.pData = (Cpa8U *)qaeMemAllocNUMA(
        digest.dataLenInBytes, node, BYTE_ALIGNMENT_64);
    if (NULL == digest.pData)
    {
        PRINT_ERR("qaeMemAllocNUMA digest failed\n");
        qaeMemFreeNUMA((void **)&msg.pData);
        sampleCyStopPolling();
        cpaCyStopInstance(cyInstHandle);
        return CPA_STATUS_FAIL;
    }
    memset(digest.pData, 0, digest.dataLenInBytes);

    PRINT("\ncalculate digest\n");
    if (NULL == SHA512(msg.pData, msg.dataLenInBytes, digest.pData))
    {
        PRINT_ERR("ECDSA Calc Digest Error\n");
        qaeMemFreeNUMA((void **)&msg.pData);
        qaeMemFreeNUMA((void **)&digest.pData);
        sampleCyStopPolling();
        cpaCyStopInstance(cyInstHandle);
        return CPA_STATUS_FAIL;
    }
    hexLog(digest.pData, digest.dataLenInBytes, "digest");

    r.dataLenInBytes = (P256_SIZE_IN_BITS + BITS_IN_BYTE - 1) / BITS_IN_BYTE;
    r.pData =
        (Cpa8U *)qaeMemAllocNUMA(r.dataLenInBytes, node, BYTE_ALIGNMENT_64);
    if (NULL == r.pData)
    {
        PRINT_ERR("qaeMemAllocNUMA sign R failed\n");
        qaeMemFreeNUMA((void **)&msg.pData);
        qaeMemFreeNUMA((void **)&digest.pData);
        sampleCyStopPolling();
        cpaCyStopInstance(cyInstHandle);
        return CPA_STATUS_FAIL;
    }
    memset(r.pData, 0, r.dataLenInBytes);

    s.dataLenInBytes = (P256_SIZE_IN_BITS + BITS_IN_BYTE - 1) / BITS_IN_BYTE;
    s.pData =
        (Cpa8U *)qaeMemAllocNUMA(s.dataLenInBytes, node, BYTE_ALIGNMENT_64);
    if (NULL == s.pData)
    {
        PRINT_ERR("qaeMemAllocNUMA sign S failed\n");
        qaeMemFreeNUMA((void **)&msg.pData);
        qaeMemFreeNUMA((void **)&digest.pData);
        qaeMemFreeNUMA((void **)&r.pData);
        sampleCyStopPolling();
        cpaCyStopInstance(cyInstHandle);
        return CPA_STATUS_FAIL;
    }
    memset(s.pData, 0, s.dataLenInBytes);

    z.dataLenInBytes = (P256_SIZE_IN_BITS + BITS_IN_BYTE - 1) / BITS_IN_BYTE;
    z.pData =
        (Cpa8U *)qaeMemAllocNUMA(z.dataLenInBytes, node, BYTE_ALIGNMENT_64);
    if (NULL == z.pData)
    {
        PRINT_ERR("qaeMemAllocNUMA sign S failed\n");
        qaeMemFreeNUMA((void **)&msg.pData);
        qaeMemFreeNUMA((void **)&digest.pData);
        qaeMemFreeNUMA((void **)&r.pData);
        qaeMemFreeNUMA((void **)&s.pData);
        sampleCyStopPolling();
        cpaCyStopInstance(cyInstHandle);
        return CPA_STATUS_FAIL;
    }
    memset(z.pData, 0, z.dataLenInBytes);
    memcpy(z.pData, digest.pData, z.dataLenInBytes);

    PRINT("\ncalling kptEcdsaSignRS : Sign the digest of a random message "
          "using elliptic curve data\n");
    status = kptEcdsaSignRS(cyInstHandle, node, &r, &s, z);
    if (CPA_STATUS_SUCCESS != status)
    {
        PRINT_ERR("kptEcdsaSignRS Error\n");
        qaeMemFreeNUMA((void **)&msg.pData);
        qaeMemFreeNUMA((void **)&digest.pData);
        qaeMemFreeNUMA((void **)&r.pData);
        qaeMemFreeNUMA((void **)&s.pData);
        qaeMemFreeNUMA((void **)&z.pData);
        sampleCyStopPolling();
        cpaCyStopInstance(cyInstHandle);
        return CPA_STATUS_FAIL;
    }

    PRINT("\ncalling ecdsaVerifySign : verify the signatures to the digest\n");
    status = ecdsaVerifySign(cyInstHandle, node, r, s, z);
    if (CPA_STATUS_SUCCESS != status)
    {
        PRINT_ERR("ecdsaVerifySign Error\n");
    }
    else
    {
        PRINT("\nSignature verification succeeded\n");
    }
    sampleCyStopPolling();
    cpaCyStopInstance(cyInstHandle);

    qaeMemFreeNUMA((void **)&msg.pData);
    qaeMemFreeNUMA((void **)&digest.pData);
    qaeMemFreeNUMA((void **)&r.pData);
    qaeMemFreeNUMA((void **)&s.pData);
    qaeMemFreeNUMA((void **)&z.pData);
    return status;
}
