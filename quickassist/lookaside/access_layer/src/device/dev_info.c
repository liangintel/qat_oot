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
 *****************************************************************************
 * @file dev_info.c
 *
 * @defgroup Device
 *
 * @description
 *    This file contains implementation of functions for device level APIs
 *
 *****************************************************************************/

/* QAT-API includes */
#include "cpa_dev.h"
#include "icp_accel_devices.h"
#include "uio_user_cfg.h"
#include "lac_common.h"
#include "icp_adf_cfg.h"
#include "lac_sal_types.h"
#include "icp_adf_accel_mgr.h"
#include "icp_platform.h"
#include "sal_string_parse.h"
#include "adf_devmgr.h"
#include "lac_sal.h"

CpaStatus cpaGetNumDevices(Cpa16U *numDevices)
{
#ifdef ICP_PARAM_CHECK
    LAC_CHECK_NULL_PARAM(numDevices);
#endif

    return icp_amgr_getNumInstances(numDevices);
}

CpaStatus cpaGetDeviceInfo(Cpa16U device, CpaDeviceInfo *deviceInfo)
{
    CpaStatus status = CPA_STATUS_SUCCESS;
    icp_accel_dev_t *pDevice = NULL;
    Cpa16U numDevicesAvail = 0;
    Cpa32U capabilitiesMask = 0;
    Cpa32U enabledServices = 0;

#ifdef ICP_PARAM_CHECK
    LAC_CHECK_NULL_PARAM(deviceInfo);
#endif
    status = icp_amgr_getNumInstances(&numDevicesAvail);
    /* Check if the application is not attempting to access a
     * device that does not exist.
     */
    if (0 == numDevicesAvail)
    {
        LAC_LOG_ERROR("Failed to retrieve number of devices!");
        return CPA_STATUS_FAIL;
    }

    /* Clear the entire capability structure before initialising it */
    ICP_MEMSET(deviceInfo, sizeof(CpaDeviceInfo), 0x00);
    /* Bus/Device/Function should be 0xFF until initialised */
    deviceInfo->bdf = 0xffff;

    pDevice = icp_adf_getAccelDevByAccelId(device);
    if (NULL == pDevice)
    {
        LAC_LOG_ERROR("Failed to retrieve device");
        return status;
    }

    /* Device of interest is found, retrieve the information for it */
    deviceInfo->sku = pDevice->sku;
    deviceInfo->deviceId = pDevice->pciDevId;
    deviceInfo->bdf = icp_adf_cfgGetBusAddress(pDevice->accelId);
    deviceInfo->numaNode = pDevice->numa_node;

    deviceInfo->isVf = pDevice->isVf;

    status = SalCtrl_GetEnabledServices(pDevice, &enabledServices);
    if (CPA_STATUS_SUCCESS != status)
    {
        LAC_LOG_ERROR("Failed to retrieve enabled services!");
        return status;
    }

    status = icp_amgr_getAccelDevCapabilities(pDevice, &capabilitiesMask);
    if (CPA_STATUS_SUCCESS != status)
    {
        LAC_LOG_ERROR("Failed to retrieve accel capabilities mask!");
        return status;
    }

    /* Determine if Compression service is enabled */
    if (enabledServices & SAL_SERVICE_TYPE_COMPRESSION)
    {
        deviceInfo->dcEnabled =
            (((capabilitiesMask & ICP_ACCEL_CAPABILITIES_COMPRESSION) != 0)
                 ? CPA_TRUE
                 : CPA_FALSE);
    }

    /* Determine if Inline service is enabled */
    if (enabledServices & SAL_SERVICE_TYPE_INLINE)
    {
        deviceInfo->inlineEnabled =
            (((capabilitiesMask & ICP_ACCEL_CAPABILITIES_INLINE) != 0)
                 ? CPA_TRUE
                 : CPA_FALSE);
    }

    /* Determine if Crypto service is enabled */
    if (enabledServices & SAL_SERVICE_TYPE_CRYPTO)
    {
        deviceInfo->cySymEnabled =
            (((capabilitiesMask & ICP_ACCEL_CAPABILITIES_CRYPTO_SYMMETRIC))
                 ? CPA_TRUE
                 : CPA_FALSE);
        deviceInfo->cyAsymEnabled =
            (((capabilitiesMask & ICP_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC) !=
              0)
                 ? CPA_TRUE
                 : CPA_FALSE);
    }
    /* Determine if Crypto Sym service is enabled */
    if (enabledServices & SAL_SERVICE_TYPE_CRYPTO_SYM)
    {
        deviceInfo->cySymEnabled =
            (((capabilitiesMask & ICP_ACCEL_CAPABILITIES_CRYPTO_SYMMETRIC))
                 ? CPA_TRUE
                 : CPA_FALSE);
    }
    /* Determine if Crypto Asym service is enabled */
    if (enabledServices & SAL_SERVICE_TYPE_CRYPTO_ASYM)
    {
        deviceInfo->cyAsymEnabled =
            (((capabilitiesMask & ICP_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC) !=
              0)
                 ? CPA_TRUE
                 : CPA_FALSE);
    }
    deviceInfo->deviceMemorySizeAvailable = pDevice->deviceMemAvail;

    return status;
}
