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
 * @file sla_manager.h
 *
 * @description
 *        This is list of SLA APIs. It contains function prototypes
 *        for managing SLAs on Intel(R) QuickAssist Technology.
 *
 ***************************************************************************/
#ifndef SLA_MANAGER_H
#define SLA_MANAGER_H

/* QAT SLA manager arguments related macros */
#ifndef DISABLE_GEN4_SLA
#define SLA_MGR_LOG_USER(format, ...)                                          \
    osalLog(OSAL_LOG_LVL_USER, OSAL_LOG_DEV_STDOUT, format, ##__VA_ARGS__)

#define SLA_MGR_LOG_ERROR(format, ...)                                         \
    osalLog(OSAL_LOG_LVL_ERROR, OSAL_LOG_DEV_STDERR, format, ##__VA_ARGS__)

#define SLA_MGR_LOG_DEV_ERROR(device)                                          \
    osalLog(OSAL_LOG_LVL_ERROR,                                                \
            OSAL_LOG_DEV_STDERR,                                               \
            "Device=%.4x:%.2x:%.2x.%x\n",                                      \
            device.domain_nr,                                                  \
            device.bus,                                                        \
            device.dev,                                                        \
            device.func)

#define SLA_MGR_LOG_DEV_USER(device)                                           \
    osalLog(OSAL_LOG_LVL_USER,                                                 \
            OSAL_LOG_DEV_STDOUT,                                               \
            "%.4x:%.2x:%.2x.%x ",                                              \
            device.domain_nr,                                                  \
            device.bus,                                                        \
            device.dev,                                                        \
            device.func)

#define SLA_MGR_ARGS_CNT_CREATE 6
#define SLA_MGR_ARGS_CNT_UPDATE 6
#define SLA_MGR_ARGS_CREATE_CIR 3
#define SLA_MGR_ARGS_CREATE_PIR 4
#define SLA_MGR_ARGS_UPDATE_CIR 4
#define SLA_MGR_ARGS_UPDATE_PIR 5
#define SLA_MGR_ARGS_SVC_TYPE 5
#else
#define SLA_MGR_ARGS_CNT_CREATE 5
#define SLA_MGR_ARGS_CNT_UPDATE 5
#define SLA_MGR_ARGS_SVC_TYPE 4
#endif
#define SLA_MGR_ARGS_CNT_DELETE 4
#define SLA_MGR_ARGS_CNT_DELETE_ALL 3
#define SLA_MGR_ARGS_CNT_CAPS 3
#define SLA_MGR_ARGS_CNT_LIST 3
#define SLA_MGR_ARGS_CNT_MIN SLA_MGR_ARGS_CNT_LIST
#define SLA_MGR_ARGS_CNT_MAX SLA_MGR_ARGS_CNT_CREATE
#define SLA_MGR_ARGS_CMD 1
#define SLA_MGR_ARGS_PCI_ADDR 2
#define SLA_MGR_ARGS_CREAT_RATE 3
#define SLA_MGR_ARGS_SLA_ID 3
#define SLA_MGR_ARGS_UPDATE_RATE 4

/* Enumeration for SLA manager commands */
typedef enum sla_mgr_cmd_s
{
    SLA_MGR_CMD_CREATE = 0,
    SLA_MGR_CMD_UPDATE,
    SLA_MGR_CMD_DELETE,
    SLA_MGR_CMD_DELETE_ALL,
    SLA_MGR_CMD_GET_CAPS,
    SLA_MGR_CMD_GET_LIST,
    SLA_MGR_CMD_UNKNOWN
} sla_mgr_cmd_t;

/* SLA user arguments structure */
struct sla_mgr_args
{
    Cpa8U command;
    struct adf_pci_address pciAddr;
#ifndef DISABLE_GEN4_SLA
    Cpa32U cir;
    Cpa32U pir;
#else
    Cpa32U rateInSlaUnits;
#endif
    Cpa16U slaId;
    enum adf_svc_type svcType;
};

/*
 ******************************************************************
 * @ingroup sla
 *        Get the Capabilities of SLA
 *
 * @description
 *        This function is used to get the capabilities of SLAs on
 *        the given PF device.
 *
 * @param[in]  pUsrArgs  user arguments structure
 *
 * @retval CPA_STATUS_SUCCESS    Operation successful
 * @retval CPA_STATUS_FAIL       Operation failed
 *
 ******************************************************************
 */
CpaStatus slaMgrGetCaps(struct sla_mgr_args *pUsrArgs);

/*
 ******************************************************************
 * @ingroup sla
 *        Get the list of SLAs configured
 *
 * @description
 *        This function is used to get the list of SLAs configured on
 *        the given PF device.
 *
 * @param[in]  pUsrArgs  user arguments structure
 *
 * @retval CPA_STATUS_SUCCESS    Operation successful
 * @retval CPA_STATUS_FAIL       Operation failed
 *
 ******************************************************************
 */
CpaStatus slaMgrGetList(struct sla_mgr_args *pUsrArgs);

#ifndef DISABLE_GEN4_SLA
/*
 ******************************************************************
 * @ingroup sla
 *        Create the SLA with IR
 *
 * @description
 *        This function is used to create the SLA.
 *
 * @param[in]  pUsrArgs  user arguments structure
 *
 * @retval CPA_STATUS_SUCCESS    Operation successful
 * @retval CPA_STATUS_FAIL       Operation failed
 *
 ******************************************************************
 */
CpaStatus slaMgrCreateSlaIR(struct sla_mgr_args *pUsrArgs);

#else
/*
 ******************************************************************
 * @ingroup sla
 *        Create the SLA
 *
 * @description
 *        This function is used to create the SLA.
 *
 * @param[in]  pUsrArgs  user arguments structure
 *
 * @retval CPA_STATUS_SUCCESS    Operation successful
 * @retval CPA_STATUS_FAIL       Operation failed
 *
 ******************************************************************
 */
CpaStatus slaMgrCreateSla(struct sla_mgr_args *pUsrArgs);
#endif

#ifndef DISABLE_GEN4_SLA
/*
 ******************************************************************
 * @ingroup sla
 *        Update the SLA with IR
 *
 * @description
 *        This function is used to update the SLA.
 *
 * @param[in]  pUsrArgs  user arguments structure
 *
 * @retval CPA_STATUS_SUCCESS    Operation successful
 * @retval CPA_STATUS_FAIL       Operation failed
 *
 ******************************************************************
 */
CpaStatus slaMgrUpdateSlaIR(struct sla_mgr_args *pUsrArgs);

#else
/*
 ******************************************************************
 * @ingroup sla
 *        Update the SLA
 *
 * @description
 *        This function is used to update the SLA.
 *
 * @param[in]  pUsrArgs  user arguments structure
 *
 * @retval CPA_STATUS_SUCCESS    Operation successful
 * @retval CPA_STATUS_FAIL       Operation failed
 *
 ******************************************************************
 */
CpaStatus slaMgrUpdateSla(struct sla_mgr_args *pUsrArgs);
#endif

/*
 ******************************************************************
 * @ingroup sla
 *        Delete the SLA
 *
 * @description
 *        This function is used to delete the SLA configured on
 *        the given PF device.
 *
 * @param[in]  pUsrArgs  user arguments structure
 *
 * @retval CPA_STATUS_SUCCESS    Operation successful
 * @retval CPA_STATUS_FAIL       Operation failed
 *
 ******************************************************************
 */
CpaStatus slaMgrDeleteSla(struct sla_mgr_args *pUsrArgs);

/*
 ******************************************************************
 * @ingroup sla
 *        Delete the list of SLAs configured
 *
 * @description
 *        This function is used to delete the list of SLAs configured on
 *        the given PF device.
 *
 * @param[in]  pUsrArgs  user arguments structure
 *
 * @retval CPA_STATUS_SUCCESS    Operation successful
 * @retval CPA_STATUS_FAIL       Operation failed
 *
 ******************************************************************
 */
CpaStatus slaMgrDeleteSlaList(struct sla_mgr_args *pUsrArgs);

/*
 ******************************************************************
 * @ingroup sla
 *        Convert string to rate in sla units
 *
 * @description
 *        This function is to convert string to rate in sla units.
 *
 * @param[out] pRate     pointer to Rate in sla units
 * @param[in]  pString   pointer to string
 *
 * @retval CPA_STATUS_SUCCESS    Operation successful
 * @retval CPA_STATUS_FAIL       Operation failed
 *
 ******************************************************************
 */
CpaStatus slaMgrStrToRate(Cpa32U *pRate, Cpa8U *pString);

/*
 ******************************************************************
 * @ingroup sla
 *        Convert string to SLA Id
 *
 * @description
 *        This function is to convert string to SLA Id.
 *
 * @param[out] pSlaId    pointer to sla id
 * @param[in]  pString   pointer to string
 *
 * @retval CPA_STATUS_SUCCESS    Operation successful
 * @retval CPA_STATUS_FAIL       Operation failed
 *
 ******************************************************************
 */
void slaMgrStrToSlaId(Cpa16U *pSlaId, Cpa8U *pString);

/*
 ******************************************************************
 * @ingroup sla
 *        Convert string to command type
 *
 * @description
 *        This function is to convert string to command type.
 *
 * @param[in]  pString     pointer to string
 *
 * @retval sla_mgr_cmd_t   enum type of the command.
 *
 ******************************************************************
 */
sla_mgr_cmd_t slaMgrStrToCmd(Cpa8U *pString);
#endif
