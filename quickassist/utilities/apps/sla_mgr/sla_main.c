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
#include <string.h>
/*
*******************************************************************************
* Include private header files
*******************************************************************************
*/
#include "rl_utils.h"
#include "sla_manager.h"

/* Number of arguments for each commands*/
static int numOfArgCnt[] = {
    SLA_MGR_ARGS_CNT_CREATE, SLA_MGR_ARGS_CNT_UPDATE,
    SLA_MGR_ARGS_CNT_DELETE, SLA_MGR_ARGS_CNT_DELETE_ALL,
    SLA_MGR_ARGS_CNT_CAPS,   SLA_MGR_ARGS_CNT_LIST
};

/*
 ******************************************************************
 * @ingroup sla
 *        Display command line argument help string.
 *
 * @description
 *        This function is used display the command line argument
 *        help string.
 *
 * @param[in]  pExe  pointer to name of executable file
 *
 * @retval None
 *
 ******************************************************************
 */
static void slaMgrPrintHelp(const Cpa8U *pExe)
{
    osalLog(OSAL_LOG_LVL_USER,
            OSAL_LOG_DEV_STDOUT,
            "\nsla mgr tool is used to create, update, delete, list and get");
    osalLog(OSAL_LOG_LVL_USER, OSAL_LOG_DEV_STDOUT, " SLA capabilites.\n");
    osalLog(OSAL_LOG_LVL_USER, OSAL_LOG_DEV_STDOUT, "\nUsage:\n");
    osalLog(OSAL_LOG_LVL_USER, OSAL_LOG_DEV_STDOUT, "\tCreate SLA - ");
#ifndef DISABLE_GEN4_SLA
    SLA_MGR_LOG_USER("%s create <vf_addr> <cir> <pir> <service>\n", pExe);
#else
    osalLog(OSAL_LOG_LVL_USER,
            OSAL_LOG_DEV_STDOUT,
            "%s create <vf_addr> <rate_in_sla_units> <service>\n",
            pExe);
#endif

    osalLog(OSAL_LOG_LVL_USER, OSAL_LOG_DEV_STDOUT, "\tUpdate SLA - ");
#ifndef DISABLE_GEN4_SLA
    SLA_MGR_LOG_USER("%s update <pf_addr> <sla_id> <cir> <pir>\n", pExe);
#else
    osalLog(OSAL_LOG_LVL_USER,
            OSAL_LOG_DEV_STDOUT,
            "%s update <pf_addr> <sla_id> <rate_in_sla_units>\n",
            pExe);
#endif
    osalLog(OSAL_LOG_LVL_USER, OSAL_LOG_DEV_STDOUT, "\tDelete SLA - ");
    osalLog(OSAL_LOG_LVL_USER,
            OSAL_LOG_DEV_STDOUT,
            "%s delete <pf_addr> <sla_id>\n",
            pExe);
    osalLog(OSAL_LOG_LVL_USER, OSAL_LOG_DEV_STDOUT, "\tDelete all SLAs - ");
    osalLog(OSAL_LOG_LVL_USER,
            OSAL_LOG_DEV_STDOUT,
            "%s delete_all <pf_addr>\n",
            pExe);
    osalLog(
        OSAL_LOG_LVL_USER, OSAL_LOG_DEV_STDOUT, "\tQuery SLA capabilities - ");
    osalLog(
        OSAL_LOG_LVL_USER, OSAL_LOG_DEV_STDOUT, "%s caps <pf_addr>\n", pExe);
    osalLog(OSAL_LOG_LVL_USER, OSAL_LOG_DEV_STDOUT, "\tQuery list of SLAs - ");
    osalLog(
        OSAL_LOG_LVL_USER, OSAL_LOG_DEV_STDOUT, "%s list <pf_addr>\n", pExe);
    osalLog(OSAL_LOG_LVL_USER, OSAL_LOG_DEV_STDOUT, "\nOptions:\n");
    osalLog(OSAL_LOG_LVL_USER, OSAL_LOG_DEV_STDOUT, "\tpf_addr           ");
    osalLog(OSAL_LOG_LVL_USER,
            OSAL_LOG_DEV_STDOUT,
            "Physical address in domain_nr(optional):bus:device.function(xxxx:xx.x) format\n");
    osalLog(OSAL_LOG_LVL_USER, OSAL_LOG_DEV_STDOUT, "\tvf_addr           ");
    osalLog(OSAL_LOG_LVL_USER,
            OSAL_LOG_DEV_STDOUT,
            "Virtual address in domain_nr(optional):bus:device.function(xxxx:xx.x) format\n");
    osalLog(OSAL_LOG_LVL_USER, OSAL_LOG_DEV_STDOUT, "\tservice           ");
    osalLog(OSAL_LOG_LVL_USER,
            OSAL_LOG_DEV_STDOUT,
            "Asym(=0) or Sym(=1) cryptographic services\n");
#ifndef DISABLE_GEN4_SLA
    osalLog(OSAL_LOG_LVL_USER, OSAL_LOG_DEV_STDOUT, "\t                  ");
    osalLog(
        OSAL_LOG_LVL_USER, OSAL_LOG_DEV_STDOUT, "Dc(=2) compression service\n");
    SLA_MGR_LOG_USER("\tcir/pir           committed/peak information rate ");
#else
    osalLog(OSAL_LOG_LVL_USER, OSAL_LOG_DEV_STDOUT, "\trate_in_sla_units ");
#endif
    osalLog(OSAL_LOG_LVL_USER,
            OSAL_LOG_DEV_STDOUT,
            "[0-MAX]. MAX is found by querying the capabilities\n");
    osalLog(OSAL_LOG_LVL_USER, OSAL_LOG_DEV_STDOUT, "\t                  ");
#ifndef DISABLE_GEN4_SLA
    SLA_MGR_LOG_USER("1 cir/pir unit is equal to:\n");
    osalLog(OSAL_LOG_LVL_USER, OSAL_LOG_DEV_STDOUT, "\t                  ");
    osalLog(
        OSAL_LOG_LVL_USER,
        OSAL_LOG_DEV_STDOUT,
        "0.1 percent of the available device utilisation - for asym service\n");
#else
    osalLog(OSAL_LOG_LVL_USER,
            OSAL_LOG_DEV_STDOUT,
            "1 rate_in_sla_units is equal to:\n");
    osalLog(OSAL_LOG_LVL_USER, OSAL_LOG_DEV_STDOUT, "\t                  ");
    osalLog(OSAL_LOG_LVL_USER,
            OSAL_LOG_DEV_STDOUT,
            "1 operation per second - for asym service\n");
#endif
    osalLog(OSAL_LOG_LVL_USER, OSAL_LOG_DEV_STDOUT, "\t                  ");
#ifndef DISABLE_GEN4_SLA
    SLA_MGR_LOG_USER("1 Megabit per second - for sym/dc services\n");
#else
    osalLog(OSAL_LOG_LVL_USER,
            OSAL_LOG_DEV_STDOUT,
            "1 Megabits per second - for sym service\n");
#endif
    osalLog(OSAL_LOG_LVL_USER, OSAL_LOG_DEV_STDOUT, "\tsla_id            ");
    osalLog(OSAL_LOG_LVL_USER,
            OSAL_LOG_DEV_STDOUT,
            "Value returned by create command\n");
}

/*
 ******************************************************************
 * @ingroup sla
 *        Parse the command line arguments
 *
 * @description
 *        This function is to parse the command line arguments.
 *
 * @param[in]  argc      number of arguments passed
 * @param[in]  argv      array of character pointers listing all the
 * @param[out] pUsrArgs  user arguments structure
 *
 * @retval CPA_STATUS_SUCCESS    Operation successful
 * @retval CPA_STATUS_FAIL       Operation failed
 *
 ******************************************************************
 */
static CpaStatus slaMgrParseParams(Cpa8U **args,
                                   int argc,
                                   struct sla_mgr_args *pUsrArgs)
{
    sla_mgr_cmd_t cmd;

    if (argc < SLA_MGR_ARGS_CNT_MIN || argc > SLA_MGR_ARGS_CNT_MAX)
    {
        slaMgrPrintHelp(args[0]);
        return CPA_STATUS_FAIL;
    }

    cmd = slaMgrStrToCmd(args[SLA_MGR_ARGS_CMD]);
    if (SLA_MGR_CMD_UNKNOWN == cmd)
    {
        osalLog(
            OSAL_LOG_LVL_ERROR, OSAL_LOG_DEV_STDERR, "Invalid SLA command\n");
        slaMgrPrintHelp(args[0]);
        return CPA_STATUS_FAIL;
    }

    RL_CMP_ARG_COUNT(args, argc, numOfArgCnt[cmd], slaMgrPrintHelp);
    pUsrArgs->command = cmd;

    if (CPA_STATUS_SUCCESS !=
        rlStrToPciAddr(&pUsrArgs->pciAddr, args[SLA_MGR_ARGS_PCI_ADDR]))
        return CPA_STATUS_FAIL;

    switch (cmd)
    {
        case SLA_MGR_CMD_CREATE:
#ifndef DISABLE_GEN4_SLA
            if (CPA_STATUS_SUCCESS !=
                slaMgrStrToRate(&pUsrArgs->cir, args[SLA_MGR_ARGS_CREATE_CIR]))
                return CPA_STATUS_FAIL;
            if (CPA_STATUS_SUCCESS !=
                slaMgrStrToRate(&pUsrArgs->pir, args[SLA_MGR_ARGS_CREATE_PIR]))
                return CPA_STATUS_FAIL;
            if (CPA_STATUS_SUCCESS !=
                rlStrToSvc(&pUsrArgs->svcType, args[SLA_MGR_ARGS_SVC_TYPE]))
                return CPA_STATUS_FAIL;
#else
            if (CPA_STATUS_SUCCESS !=
                slaMgrStrToRate(&pUsrArgs->rateInSlaUnits,
                                args[SLA_MGR_ARGS_CREAT_RATE]))
                return CPA_STATUS_FAIL;
            if (CPA_STATUS_SUCCESS !=
                rlStrToSvc(&pUsrArgs->svcType, args[SLA_MGR_ARGS_SVC_TYPE]))
                return CPA_STATUS_FAIL;
#endif
            break;
        case SLA_MGR_CMD_UPDATE:
#ifndef DISABLE_GEN4_SLA
            slaMgrStrToSlaId(&pUsrArgs->slaId, args[SLA_MGR_ARGS_SLA_ID]);
            if (CPA_STATUS_SUCCESS !=
                slaMgrStrToRate(&pUsrArgs->cir, args[SLA_MGR_ARGS_UPDATE_CIR]))
                return CPA_STATUS_FAIL;
            if (CPA_STATUS_SUCCESS !=
                slaMgrStrToRate(&pUsrArgs->pir, args[SLA_MGR_ARGS_UPDATE_PIR]))
                return CPA_STATUS_FAIL;
#else
            slaMgrStrToSlaId(&pUsrArgs->slaId, args[SLA_MGR_ARGS_SLA_ID]);
            if (CPA_STATUS_SUCCESS !=
                slaMgrStrToRate(&pUsrArgs->rateInSlaUnits,
                                args[SLA_MGR_ARGS_UPDATE_RATE]))
                return CPA_STATUS_FAIL;
#endif
            break;
        case SLA_MGR_CMD_DELETE:
            slaMgrStrToSlaId(&pUsrArgs->slaId, args[SLA_MGR_ARGS_SLA_ID]);
            break;
        case SLA_MGR_CMD_GET_CAPS:

        case SLA_MGR_CMD_GET_LIST:

        case SLA_MGR_CMD_DELETE_ALL:
            break;
        default:
            slaMgrPrintHelp(args[0]);
            return CPA_STATUS_FAIL;
    }
    return CPA_STATUS_SUCCESS;
}

/*
 ******************************************************************
 * @ingroup sla
 *        Main function to execute the SLA manager application
 *
 * @description
 *        This function is used to parse command line arguments
 *        and execute the commands on the given PF device.
 *
 * @param[in]  argc  number of arguments passed
 * @param[in]  argv  array of character pointers listing all the
 *                   arguments
 *
 * @retval  0    Operation successful
 * @retval -1    Operation failed
 *
 ******************************************************************
 */
int main(int argc, char *argv[])
{
    struct sla_mgr_args usrArgs = (const struct sla_mgr_args){ 0 };
    CpaStatus status = CPA_STATUS_FAIL;

    if (CPA_STATUS_SUCCESS != slaMgrParseParams((Cpa8U **)argv, argc, &usrArgs))
        return -1;

    switch (usrArgs.command)
    {
        case SLA_MGR_CMD_CREATE:
#ifndef DISABLE_GEN4_SLA
            status = slaMgrCreateSlaIR(&usrArgs);
#else
            status = slaMgrCreateSla(&usrArgs);
#endif
            break;
        case SLA_MGR_CMD_UPDATE:
#ifndef DISABLE_GEN4_SLA
            status = slaMgrUpdateSlaIR(&usrArgs);
#else
            status = slaMgrUpdateSla(&usrArgs);
#endif
            break;
        case SLA_MGR_CMD_DELETE:
            status = slaMgrDeleteSla(&usrArgs);
            break;
        case SLA_MGR_CMD_GET_CAPS:
            status = slaMgrGetCaps(&usrArgs);
            break;
        case SLA_MGR_CMD_GET_LIST:
            status = slaMgrGetList(&usrArgs);
            break;
        case SLA_MGR_CMD_DELETE_ALL:
            status = slaMgrDeleteSlaList(&usrArgs);
            break;
    }
    if (CPA_STATUS_SUCCESS != status)
    {
        osalLog(OSAL_LOG_LVL_ERROR,
                OSAL_LOG_DEV_STDERR,
                "Failed to execute SLA command\n");
        return -1;
    }
    return 0;
}
