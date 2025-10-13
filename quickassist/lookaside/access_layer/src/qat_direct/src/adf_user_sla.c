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
 ****************************************************************************/
/*
*******************************************************************************
* Include public/global header files
*******************************************************************************
*/
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
/*
*******************************************************************************
* Include private header files
*******************************************************************************
*/
#include "cpa.h"
#include "adf_kernel_types.h"
#include "icp_platform.h"
#include "adf_cfg_common.h"
#include "adf_sla_user.h"
#include "icp_accel_devices.h"

static void sla_user_copy(struct adf_pci_address *pTo,
                          struct adf_pci_address *pFrom)
{
    pTo->domain_nr = pFrom->domain_nr;
    pTo->bus = pFrom->bus;
    pTo->dev = pFrom->dev;
    pTo->func = pFrom->func;
}

static CpaStatus sla_user_ioctl(struct adf_user_sla *pSla,
                                int cmd,
                                Cpa16U *pSlaId)
{
    int fd;
    CpaStatus status = CPA_STATUS_SUCCESS;

    fd = open(ADF_CTL_DEVICE_NAME, O_RDWR);
    if (fd < 0)
    {
        ADF_ERROR("Failed to open device file %s\n", ADF_CTL_DEVICE_NAME);
        return CPA_STATUS_FAIL;
    }

    if (ioctl(fd, cmd, pSla))
    {
        ADF_ERROR("Failed to execute ioctl command\n");
        status = CPA_STATUS_FAIL;
    }

    if (pSlaId && (CPA_STATUS_SUCCESS == status))
        *pSlaId = pSla->sla_id;

    close(fd);
    return status;
}

CpaStatus icp_adf_userSlaGetCaps(struct adf_pci_address *pPf,
                                 struct adf_user_sla_caps *pCaps)
{
    static int fd;
    CpaStatus status = CPA_STATUS_SUCCESS;

    if (!pPf || !pCaps)
    {
        ADF_ERROR("Invalid argument\n");
        return CPA_STATUS_INVALID_PARAM;
    }

    sla_user_copy(&pCaps->pf_addr, pPf);

    fd = open(ADF_CTL_DEVICE_NAME, O_RDWR);
    if (fd < 0)
    {
        ADF_ERROR("Failed to open device %s\n", ADF_CTL_DEVICE_NAME);
        return CPA_STATUS_FAIL;
    }

    if (ioctl(fd, IOCTL_SLA_GET_CAPS, pCaps))
    {
        ADF_ERROR("Failed to get SLA capabilities\n");
        status = CPA_STATUS_FAIL;
    }

    close(fd);
    return status;
}

CpaStatus icp_adf_userSlaGetList(struct adf_pci_address *pPf,
                                 struct adf_user_slas *pSlas)
{
    static int fd;
    CpaStatus status = CPA_STATUS_SUCCESS;

    if (!pPf || !pSlas)
    {
        ADF_ERROR("Invalid argument\n");
        return CPA_STATUS_INVALID_PARAM;
    }

    sla_user_copy(&pSlas->pf_addr, pPf);

    fd = open(ADF_CTL_DEVICE_NAME, O_RDWR);
    if (fd < 0)
    {
        ADF_ERROR("Failed to open device %s\n", ADF_CTL_DEVICE_NAME);
        return CPA_STATUS_FAIL;
    }

    if (ioctl(fd, IOCTL_SLA_GET_LIST, pSlas))
    {
        ADF_ERROR("Failed to get SLA lists\n");
        status = CPA_STATUS_FAIL;
    }

    close(fd);
    return status;
}

CpaStatus icp_adf_userSlaCreateIR(struct adf_user_sla *pSla, Cpa16U *pSlaId)
{
    if (!pSla || !pSlaId)
    {
        ADF_ERROR("Invalid argument\n");
        return CPA_STATUS_INVALID_PARAM;
    }

    return sla_user_ioctl(pSla, IOCTL_SLA_CREATE_V2, pSlaId);
}

CpaStatus icp_adf_userSlaCreate(struct adf_user_sla *pSla, Cpa16U *pSlaId)
{
    if (!pSla || !pSlaId)
    {
        ADF_ERROR("Invalid argument\n");
        return CPA_STATUS_INVALID_PARAM;
    }

    return sla_user_ioctl(pSla, IOCTL_SLA_CREATE, pSlaId);
}

CpaStatus icp_adf_userSlaUpdateIR(struct adf_pci_address *pPf,
                                  Cpa16U pSlaId,
                                  Cpa32U cir,
                                  Cpa32U pir)
{
    struct adf_user_sla sla = { 0 };

    if (!pPf)
    {
        ADF_ERROR("Invalid argument\n");
        return CPA_STATUS_INVALID_PARAM;
    }

    sla_user_copy(&sla.pci_addr, pPf);
    sla.sla_id = pSlaId;
    sla.cir = cir;
    sla.pir = pir;

    return sla_user_ioctl(&sla, IOCTL_SLA_UPDATE_V2, NULL);
}

CpaStatus icp_adf_userSlaUpdate(struct adf_pci_address *pPf,
                                Cpa16U pSlaId,
                                Cpa32U rateInSlaUnits)
{
    struct adf_user_sla sla;

    if (!pPf)
    {
        ADF_ERROR("Invalid argument\n");
        return CPA_STATUS_INVALID_PARAM;
    }

    sla_user_copy(&sla.pci_addr, pPf);
    sla.sla_id = pSlaId;
    sla.rate_in_slau = rateInSlaUnits;

    return sla_user_ioctl(&sla, IOCTL_SLA_UPDATE, NULL);
}

CpaStatus icp_adf_userSlaDelete(struct adf_pci_address *pPf, Cpa16U pSlaId)
{
    struct adf_user_sla sla;

    if (!pPf)
    {
        ADF_ERROR("Invalid argument\n");
        return CPA_STATUS_INVALID_PARAM;
    }

    sla_user_copy(&sla.pci_addr, pPf);
    sla.sla_id = pSlaId;

    return sla_user_ioctl(&sla, IOCTL_SLA_DELETE, NULL);
}
