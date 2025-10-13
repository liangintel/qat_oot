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
 * @file dc_header_cksum_lz4.c
 *
 * @defgroup Dc_DataCompression DC Data Compression
 *
 * @ingroup Dc_DataCompression
 *
 * @description
 *      Used to calculate the xxhash for LZ4 header checksum
 *
 *****************************************************************************/
#include "dc_header_cksum_lz4.h"


/* *******************************************************************
 *  32-bit hash functions
 *********************************************************************/
static const Cpa32U XXHASH_PRIME32_A = 0x9E3779B1U;
static const Cpa32U XXHASH_PRIME32_B = 0x85EBCA77U;
static const Cpa32U XXHASH_PRIME32_C = 0xC2B2AE3DU;
static const Cpa32U XXHASH_PRIME32_D = 0x27D4EB2FU;
static const Cpa32U XXHASH_PRIME32_E = 0x165667B1U;

#define XXH32_STRIP_SIZE 16
#define ROTATE_LEFT_32(n, d) ((n << d) | (n >> (-d & 31)))

STATIC Cpa32U dc_hdr_cksum_finalise(Cpa32U xxHash32)
{
    xxHash32 = xxHash32 ^ (xxHash32 >> 15);
    xxHash32 = xxHash32 * XXHASH_PRIME32_B;
    xxHash32 = xxHash32 ^ (xxHash32 >> 13);
    xxHash32 = xxHash32 * XXHASH_PRIME32_C;
    xxHash32 = xxHash32 ^ (xxHash32 >> 16);
    return (xxHash32);
}

STATIC Cpa32U dc_hdr_cksum_consume_remaining(Cpa32U xxHash32Accumulator,
                                             const Cpa8U *ptr,
                                             Cpa32U remainingBytes)
{
    /* Input buffer less that 16 bytes. Each round is a block of 4 bytes(strip).
     * The bytes are processed in blocks of 4 and we keep processing until
     * we have less than 4 bytes left in the buffer */
    while (remainingBytes >= 4)
    {
        xxHash32Accumulator += *(Cpa32U *)ptr * XXHASH_PRIME32_C;
        xxHash32Accumulator =
            ROTATE_LEFT_32(xxHash32Accumulator, 17) * XXHASH_PRIME32_D;
        ptr += 4;
        remainingBytes -= 4;
    }
    /* Remaining bytes left after above calculation use following */
    while (remainingBytes > 0)
    {
        xxHash32Accumulator += (*ptr++) * XXHASH_PRIME32_E;
        xxHash32Accumulator =
            ROTATE_LEFT_32(xxHash32Accumulator, 11) * XXHASH_PRIME32_A;
        remainingBytes -= 1;
    }

    return dc_hdr_cksum_finalise(xxHash32Accumulator);
}

/*Calculate the XXH32 on a block of data */
STATIC CpaStatus dc_hdr_cksum_calculate(const Cpa8U *xxH32input,
                                        Cpa32U dataLength,
                                        Cpa32U seed,
                                        Cpa32U *result)
{
    Cpa32U xxHash32Accumulator = 0;
#ifdef ICP_PARAM_CHECK
    /* Check for null parameters */
    LAC_CHECK_NULL_PARAM(xxH32input);
    LAC_CHECK_NULL_PARAM(result);
#endif

    if (dataLength < XXH32_STRIP_SIZE)
        xxHash32Accumulator = seed + XXHASH_PRIME32_E;
    else /* TODO: Add support for input buffer >= 16. */
        return CPA_STATUS_FAIL;

    /* Add data length to accumulator */
    xxHash32Accumulator += (Cpa32U)dataLength;

    /* Consume the remaining bytes of input (< 16) */
    *result = dc_hdr_cksum_consume_remaining(
        xxHash32Accumulator, xxH32input, dataLength);

    return CPA_STATUS_SUCCESS;
}

CpaStatus dc_hdr_cksum(const void *xxH32input,
                       const Cpa32U dataLength,
                       Cpa8U *checksum)
{
    CpaStatus status = CPA_STATUS_SUCCESS;
    Cpa32U result = 0;
    Cpa32U seed = 0;

    LAC_CHECK_PARAM_RANGE(dataLength, 2, 16);

    status = dc_hdr_cksum_calculate(xxH32input, dataLength, seed, &result);

    if (CPA_STATUS_SUCCESS != status)
        return CPA_STATUS_FAIL;

    *checksum = (Cpa8U)(result >> 8) & 0xFF;

    return CPA_STATUS_SUCCESS;
}
