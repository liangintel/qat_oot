/*
 * Copyright 2019-2021 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the Apache License 2.0 (the "License"). You may not use
 * this file except in compliance with the License. You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/**
 * @file endian.h
 *
 * @note
 *    This file is taken from the OpenSSL project to reuse
 *    the functionality and modified in order to prevent conflicts with
 *    public symbols in another existing OpenSSL library:
 *    - renames the macros to have OSSL_ prefix
 *    - modifies path to the header files
 *
 * @par
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
 */

#ifndef OSSL_INTERNAL_ENDIAN_H
#define OSSL_INTERNAL_ENDIAN_H
#pragma once

/*
 * OSSL_IS_LITTLE_ENDIAN and OSSL_IS_BIG_ENDIAN can be used to detect the
 * endianness at compile time. To use it, OSSL_DECLARE_IS_ENDIAN must be
 * used to declare a variable.
 *
 * OSSL_L_ENDIAN and OSSL_B_ENDIAN can be used at preprocessor time.
 * They can be set in the configuration using the lib_cppflags variable.
 * If neither is set, it will fall back to code which can work with
 * either endianness.
 */

#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
#define OSSL_DECLARE_IS_ENDIAN                                                      \
    const int ossl_is_little_endian = __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define OSSL_IS_LITTLE_ENDIAN (ossl_is_little_endian)
#define OSSL_IS_BIG_ENDIAN (!ossl_is_little_endian)
#if defined(OSSL_L_ENDIAN) && (__BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__)
#error "OSSL_L_ENDIAN defined on a big endian machine"
#endif
#if defined(OSSL_B_ENDIAN) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#error "OSSL_B_ENDIAN defined on a little endian machine"
#endif
#if !defined(OSSL_L_ENDIAN) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#define OSSL_L_ENDIAN
#endif
#if !defined(OSSL_B_ENDIAN) && (__BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__)
#define OSSL_B_ENDIAN
#endif
#else
#define OSSL_DECLARE_IS_ENDIAN                                                      \
    const union {                                                              \
        long one;                                                              \
        char little;                                                           \
    } ossl_is_endian = { 1 }

#define OSSL_IS_LITTLE_ENDIAN (ossl_is_endian.little != 0)
#define OSSL_IS_BIG_ENDIAN (ossl_is_endian.little == 0)
#endif

#endif
