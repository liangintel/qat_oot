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
 ******************************************************************************
 * @file cpa_fips_sample.h
 *
 * @defgroup fipsSample FIPS sample code
 *
 * This header file contains the header file references to be used throughout
 * the FIPS sample code.
 *
 *****************************************************************************/
/**
 ******************************************************************************
 * \mainpage FIPS Sample Code Low Level Description
 *<dl>
 * <dt> Top level header file <dd>
 *   cpa_fips_sample.h
 * <dt> GCM <dd>
 *   cpa_fips_sample_aes_gcm.h
 * <dt> RSA <dd>
 *   cpa_fips_sample_rsa.h
 * <dt> DSA <dd>
 *   cpa_fips_sample_dsa.h
 * <dt> ECDSA <dd>
 *   cpa_fips_sample_ecdsa.h
 * <dt> Utility code <dd>
 *   cpa_fips_sample_utils.h
 * <dt> Kernel Module <dd>
 *   cpa_fips_sample_linux_kernel_module.c
 *</dl>
 *
 *****************************************************************************/

#ifndef _CPA_FIPS_SAMPLE_H_
#define _CPA_FIPS_SAMPLE_H_

#ifdef __cplusplus
extern "C"
#endif /*__cplusplus*/

#include <string.h>
#include <sched.h>
#include <errno.h>

#include "cpa.h"
#include "cpa_types.h"
#include "cpa_cy_common.h"
#include "cpa_cy_im.h"
#include "cpa_cy_key.h"
#include "cpa_cy_sym.h"
#include "cpa_cy_prime.h"
#include "cpa_cy_ln.h"
#include "cpa_cy_nrbg.h"
#include "cpa_cy_drbg.h"

#include "qae_mem.h"

#endif /*_CPA_FIPS_SAMPLE_H_*/
