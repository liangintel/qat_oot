/***************************************************************************
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
 ****************************************************************************/
/****************************************************************************
 * @file rl_utils.h
 *
 * @description
 *        This is list of util functions used by SLA manager
 *        and Device Utilization applications.
 *
 ****************************************************************************/
#ifndef RL_UTILS_H
#define RL_UTILS_H
/*
*******************************************************************************
* Include public/global header files
*******************************************************************************
*/
#include "cpa.h"
#include "adf_kernel_types.h"
#include "adf_sla_user.h"
#include "Osal.h"

/* To parse the PCI address bus, dev and functions */
#define RL_BASE_HEX 16
#define RL_BASE_DEC 10
#define RL_MAX_PCI_FUNC 7
#define RL_BDF_FORMAT 7
#define RL_DBDF_FORMAT 12

/*Maximum chars in cmd string*/
#define RL_MAX_CMD_STR_CHAR 10

/* To validate arguments count and display help string */
#define RL_CMP_ARG_COUNT(args, argc, count, printHelp)                         \
    do                                                                         \
    {                                                                          \
        if (count != argc)                                                     \
        {                                                                      \
            osalLog(OSAL_LOG_LVL_ERROR,                                        \
                    OSAL_LOG_DEV_STDERR,                                       \
                    "Invalid number of arguments\n");                          \
            printHelp((Cpa8U *)args[0]);                                       \
            return CPA_STATUS_INVALID_PARAM;                                   \
        }                                                                      \
    } while (0);

/*
 ******************************************************************
 * @ingroup rl
 *        Copy PCI address
 *
 * @description
 *        This function is used to copy PCI address.
 *
 * @param[out] pTo    destination PCI address structure
 * @param[in]  pFrom  source PCI address structure
 *
 * @retval None
 *
 ******************************************************************
 */
void rlCopyPciAddr(struct adf_pci_address *pTo, struct adf_pci_address *pFrom);

/*
 ******************************************************************
 * @ingroup rl
 *        Convert string to PCI Address
 *
 * @description
 *        This function is to convert string to PCI Address.
 *
 * @param[out] pPciAddr  pointer to PCI address structure
 * @param[in]  pString   pointer to string
 *
 * @retval CPA_STATUS_SUCCESS         Operation successful
 * @retval CPA_STATUS_FAIL            Operation failed
 * @retval CPA_STATUS_INVALID_PARAM   Invalid/null arguments
 *
 ******************************************************************
 */
CpaStatus rlStrToPciAddr(struct adf_pci_address *pPciAddr, Cpa8U *pString);

/*
 ******************************************************************
 * @ingroup rl
 *        Convert string to Svc enum type
 *
 * @description
 *        This function is to convert string to Svc enum type.
 *
 * @param[out] pType     pointer to enum type
 * @param[in]  pString   pointer to string
 *
 * @retval CPA_STATUS_SUCCESS         Operation successful
 * @retval CPA_STATUS_FAIL            Operation failed
 * @retval CPA_STATUS_INVALID_PARAM   Invalid/null arguments
 *
 ******************************************************************
 */
CpaStatus rlStrToSvc(enum adf_svc_type *pType, Cpa8U *pString);

/*
 ******************************************************************
 * @ingroup rl
 *        Convert Svc enum type to string
 *
 * @description
 *        This function is to convert Svc enum type to string.
 *
 * @param[in]  pType     pointer to enum type
 *
 * @retval pointer to svc type string
 *
 ******************************************************************
 */
const Cpa8U *rlSvcToStr(enum adf_svc_type svcType);
#endif
