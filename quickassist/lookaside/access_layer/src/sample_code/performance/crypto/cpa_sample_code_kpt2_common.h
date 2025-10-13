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

#ifndef CPA_SAMPLE_CODE_KPT2_COMMON_H
#define CPA_SAMPLE_CODE_KPT2_COMMON_H

#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/bn.h>
#include <openssl/obj_mac.h>
#if (OPENSSL_VERSION_NUMBER >= 0x30000000L)
#include <openssl/param_build.h>
#endif
#include <sys/socket.h>
#include <arpa/inet.h>
#include <ctype.h>
#include "cpa_cy_kpt.h"
#include "cpa_sample_code_crypto_utils.h"

#if CY_API_VERSION_AT_LEAST(3, 0)

#define AUTH_TAG_LEN_IN_BYTES 16
#define IV_LEN_IN_BYTES 12
#define MAX_SWK_PER_PHYSICAL_DEVICE 128
#define SWK_LEN_IN_BYTES 32
#define NUM_OF_SWK_ONLY_ONE 1
#define PER_PART_PKEY_E_SIZE 8
#define NUM_KEY_PAIRS (2)
#define KEY_PROVISION_RETRY_TIMES_LIMIT 20
#define KEY_PROVISION_RETRY_DELAY_MS 300
/* KPT Stolen Key Test */
#define KPT_KEY_HANDLE_LENTH 21
#define PRIVATE_KEY_LENTH_LEN_IN_BYTE 4
#define PORT_NO 8000
#define SOCKET_BZERO_BYTES 8
#define SOCKET_BACKLOG_NO 10

CpaStatus encryptAndLoadSWK(CpaInstanceHandle instanceHandle,
                            CpaCyKptHandle *kptKeyHandle,
                            Cpa8U *sampleSWK);

CpaBoolean encryptPrivateKey(Cpa8U *pPrivateKey,
                             Cpa32U privateKeyLength,
                             Cpa8U *pSWK,
                             Cpa8U *pIv,
                             Cpa32U ivLength,
                             Cpa8U *pWrappedPrivateKey,
                             Cpa32U *pWPKLenth,
                             Cpa8U *pAuthTag,
                             Cpa8U *pAad,
                             Cpa32U aadLenInBytes);

CpaStatus sendMessageToThief(Cpa8U *sendMsg, Cpa32U msgLenInBytes);

CpaStatus recvMessageFromOwner(Cpa8U *recvMsg,
                               Cpa32U msgLenInBytes,
                               char tagAddr[IP_ADDR_LEN]);

Cpa64U strToNumeric(Cpa8U *pstr);
#endif /* CY_API_VERSION_AT_LEAST(3, 0) */
#endif /* CPA_SAMPLE_CODE_KPT2_COMMON_H */
