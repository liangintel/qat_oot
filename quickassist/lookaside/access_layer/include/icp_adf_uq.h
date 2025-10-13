
/*****************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 *   redistributing this file, you may do so under either license.
 * 
 *   GPL LICENSE SUMMARY
 * 
 *   Copyright(c) 2007-2023 Intel Corporation. All rights reserved.
 * 
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of version 2 of the GNU General Public License as
 *   published by the Free Software Foundation.
 * 
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   General Public License for more details.
 * 
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *   The full GNU General Public License is included in this distribution
 *   in the file called LICENSE.GPL.
 * 
 *   Contact Information:
 *   Intel Corporation
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
 * 
 *  version: QAT20.L.1.2.30-00090
 *
 *****************************************************************************/

/*****************************************************************************
 * @file icp_adf_uq.h
 *
 * @description
 *      File contains Public API Definitions for ADF UQ.
 *
 *****************************************************************************/
#ifndef ICP_ADF_UQ_H
#define ICP_ADF_UQ_H
#include "adf_dev_ring_ctl.h"

#define ADF_UQ_MAX_BATCH_SHIFT 5
#define ADF_UQ_MAX_BATCH_NR (1 << ADF_UQ_MAX_BATCH_SHIFT)

#define ADF_UQ_LH_REQ_ADDR_SHIFT 2
#define ADF_UQ_LH_REQ_ADDR_SIZE 30
#define ADF_UQ_LH_REQ_ADDR_MASK ((1UL << ADF_UQ_LH_REQ_ADDR_SIZE) - 1)

#define ADF_UQ_UH_REQ_ADDR_SHIFT 32

#define ADF_UQ_MSG_SIZE_64_BYTES 0
#define ADF_UQ_MSG_SIZE_128_BYTES 1

#define ADF_UQ_DESC_TYPE_EMBEDDED 0
#define ADF_UQ_DESC_TYPE_REMOTE 1

#define ADF_UQ_IR_DO_NOT_SEND_INT 0
#define ADF_UQ_IR_SEND_INT 1

struct adf_uq_desc
{
    uint32_t pasid : 20;
    uint32_t resv1 : 11;
    uint32_t priv : 1;
    uint32_t user_handler;
    uint32_t desc_type : 1;
    uint32_t uli : 1;
    uint32_t lrespaddr : 30;
    uint32_t urespaddr;
    uint32_t desc_size : 1;
    uint32_t resv2 : 1;
    uint32_t lreqaddr : 30;
    uint32_t ureqaddr;
    uint32_t desc_cnt : 5;
    uint32_t resv3 : 27;
    uint32_t resv4[9];
} __attribute__((__packed__));

/*
 * adf_uq_put_msg
 *
 * Description
 * Submit request via enqcmd
 */
CpaStatus adf_uq_put_msg(adf_dev_ring_handle_t *ring);

/*
 * adf_uq_push_dp_msg
 *
 * Description
 * Submit DP request via enqcmd
 */
CpaStatus adf_uq_push_dp_msg(adf_dev_ring_handle_t *ring);
#endif /* ICP_ADF_UQ_H */
