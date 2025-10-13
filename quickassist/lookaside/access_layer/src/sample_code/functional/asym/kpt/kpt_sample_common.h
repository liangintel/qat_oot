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
 * @file  kpt_sample_common.h
 *
 *****************************************************************************/
#ifndef __KPT_SAMPLE_COMMON_H__
#define __KPT_SAMPLE_COMMON_H__

#include "cpa.h"
#include "cpa_cy_kpt.h"

#include "cpa_sample_utils.h"

#include <stdio.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/rsa.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/obj_mac.h>
#if (OPENSSL_VERSION_NUMBER >= 0x30000000L)
#include <openssl/param_build.h>
#endif

#define AUTH_TAG_LEN 16
#define PER_PART_PKEY_E_SIZE 8
#define KEY_PROVISION_RETRY_TIMES_LIMIT 20
#define SWK_LEN_IN_BYTES 32
#define UPPER_HALF_OF_REGISTER 32
#define BUS_DIGIT 8
#define DEVICE_DIGIT 3
#define FUNCTION_DIGIT 7

CpaStatus encryptAndLoadSWK(CpaInstanceHandle instanceHandle,
                            Cpa32U node,
                            CpaCyKptHandle *kptKeyHandle,
                            Cpa8U *sampleSWK);

CpaBoolean encryptPrivateKey(Cpa8U *pPrivateKey,
                             Cpa32U privateKeyLength,
                             Cpa8U *pSWK,
                             Cpa8U *pIv,
                             Cpa32U ivLength,
                             Cpa8U *pWrappedPrivateKey,
                             Cpa32U *pWPKLength,
                             Cpa8U *pAuthTag,
                             Cpa8U *pAad,
                             Cpa32U aadLenInBytes);

void genRandomData(Cpa8U *pWriteRandData, Cpa32U lengthOfRand);

CpaStatus queryCapabilitiesForKpt(CpaInstanceHandle cyInstHandle,
                                  CpaInstanceInfo2 instanceInfo,
                                  CpaCyCapabilitiesInfo *pCapInfo);

#endif
