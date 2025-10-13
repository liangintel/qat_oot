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
#include "adf_sla_user.h"
#include "icp_sal_sla.h"
#include "icp_adf_sla.h"

CpaStatus icp_sal_userSlaGetCaps(struct adf_pci_address *pPf,
                                 struct adf_user_sla_caps *pCaps)
{
    return icp_adf_userSlaGetCaps(pPf, pCaps);
}

CpaStatus icp_sal_userSlaGetList(struct adf_pci_address *pPf,
                                 struct adf_user_slas *pSlas)
{
    return icp_adf_userSlaGetList(pPf, pSlas);
}

CpaStatus icp_sal_userSlaCreateIR(struct adf_user_sla *pSla, Cpa16U *pSlaId)
{
    return icp_adf_userSlaCreateIR(pSla, pSlaId);
}

CpaStatus icp_sal_userSlaCreate(struct adf_user_sla *pSla, Cpa16U *pSlaId)
{
    return icp_adf_userSlaCreate(pSla, pSlaId);
}

CpaStatus icp_sal_userSlaUpdateIR(struct adf_pci_address *pPciAddr,
                                  Cpa16U pSlaId,
                                  Cpa32U cir,
                                  Cpa32U pir)
{
    return icp_adf_userSlaUpdateIR(pPciAddr, pSlaId, cir, pir);
}

CpaStatus icp_sal_userSlaUpdate(struct adf_pci_address *pPciAddr,
                                Cpa16U pSlaId,
                                Cpa32U rateInSlaUnits)
{
    return icp_adf_userSlaUpdate(pPciAddr, pSlaId, rateInSlaUnits);
}

CpaStatus icp_sal_userSlaDelete(struct adf_pci_address *pPciAddr, Cpa16U pSlaId)
{
    return icp_adf_userSlaDelete(pPciAddr, pSlaId);
}
