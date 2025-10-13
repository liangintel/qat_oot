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
/*
*******************************************************************************
* Include public/global header files
*******************************************************************************
*/
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
/*
*******************************************************************************
* Include private header files
*******************************************************************************
*/
#include "rl_utils.h"

CpaStatus rlStrToPciAddr(struct adf_pci_address *pPciAddr, Cpa8U *pString)
{
    unsigned long pciFunc = 0;
    Cpa8U *ptr = NULL;

    if (!pString)
    {
        osalLog(OSAL_LOG_LVL_ERROR,
                OSAL_LOG_DEV_STDERR,
                "PCI Address string is NULL\n");
        return CPA_STATUS_INVALID_PARAM;
    }

    /* Expected format: <domain_nr(optional)>:<bus>:<dev>.<func> */
    if (strnlen((const char *)pString, RL_DBDF_FORMAT) > RL_BDF_FORMAT)
    {
        pPciAddr->domain_nr =
            strtoul((const char *)pString, (char **)&ptr, RL_BASE_HEX);
        pPciAddr->bus =
            strtoul((const char *)ptr + 1, (char **)&ptr, RL_BASE_HEX);
    }
    else
    {
        pPciAddr->bus =
            strtoul((const char *)pString, (char **)&ptr, RL_BASE_HEX);
    }

    pPciAddr->dev = strtoul((const char *)ptr + 1, (char **)&ptr, RL_BASE_HEX);

    errno = 0;
    pciFunc = strtoul((const char *)ptr + 1, (char **)&ptr, RL_BASE_HEX);
    if ('\0' == *pString || '\0' != *ptr || RL_MAX_PCI_FUNC < pciFunc ||
        (0 == pciFunc && (ERANGE == errno || EINVAL == errno)))
    {
        osalLog(OSAL_LOG_LVL_ERROR,
                OSAL_LOG_DEV_STDERR,
                "Invalid PCI address = %s\n",
                pString);
        return CPA_STATUS_FAIL;
    }
    pPciAddr->func = pciFunc;
    return CPA_STATUS_SUCCESS;
}

CpaStatus rlStrToSvc(enum adf_svc_type *pType, Cpa8U *pString)
{
    if (!pType || !pString)
    {
        osalLog(OSAL_LOG_LVL_ERROR,
                OSAL_LOG_DEV_STDERR,
                "Invalid argument pType or pString\n");
        return CPA_STATUS_INVALID_PARAM;
    }
    switch (*pString)
    {
        case '0':
            *pType = ADF_SVC_ASYM;
            break;
        case '1':
            *pType = ADF_SVC_SYM;
            break;
        case '2':
            *pType = ADF_SVC_DC;
            break;
        default:
            osalLog(OSAL_LOG_LVL_ERROR,
                    OSAL_LOG_DEV_STDERR,
                    "Invalid svc %s\n",
                    pString);
            return CPA_STATUS_FAIL;
    }
    return CPA_STATUS_SUCCESS;
}

const Cpa8U *rlSvcToStr(enum adf_svc_type svcType)
{
    switch (svcType)
    {
        case ADF_SVC_ASYM:
            return (Cpa8U *)"asym";
        case ADF_SVC_SYM:
            return (Cpa8U *)"sym";
        case ADF_SVC_DC:
            return (Cpa8U *)"dc";
        case ADF_SVC_NONE:
            return (Cpa8U *)"none";
    }

    return (Cpa8U *)"unknown";
}

void rlCopyPciAddr(struct adf_pci_address *pTo, struct adf_pci_address *pFrom)
{
    pTo->domain_nr = pFrom->domain_nr;
    pTo->bus = pFrom->bus;
    pTo->dev = pFrom->dev;
    pTo->func = pFrom->func;
}
