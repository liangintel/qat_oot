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
#include <fcntl.h>
#include <stdlib.h>

#include "icp_adf_tl.h"
#include "icp_platform.h"

#define TL_DISABLE "0"
#define TL_ENABLE "1"

#define FILE_NAME_LEN 64
#define CONTROL_FILE_STRING                                                    \
    "/sys/devices/pci%4.4x:%2.2x/%4.4x:%2.2x:%2.2x.%1.1x/telemetry/control"

static int open_tl_fd(struct adf_pci_address *pPciAddr,
                      char file_option[])
{
    int result = -1;
    char name[FILE_NAME_LEN] = { 0 };
    int fd = -1;

    result = snprintf(name,
                      FILE_NAME_LEN,
                      file_option,
                      pPciAddr->domain_nr,
                      pPciAddr->bus,
                      pPciAddr->domain_nr,
                      pPciAddr->bus,
                      pPciAddr->dev,
                      pPciAddr->func);
    if (result < 0)
    {
        ADF_ERROR("File name print failed\n");
        return fd;
    }
    
    fd = open(name, O_WRONLY);
    if (fd == -1)
    {
        ADF_ERROR("sysfs file failed to open\n");
        return fd;
    }

    return fd;
}

/* This function stops the telemetry feature */
CpaStatus icp_adf_dev_telemetry_stop(struct adf_pci_address *pPciAddr)
{
    int fd = -1;
    CpaStatus status = CPA_STATUS_SUCCESS;
    int result = -1;

    fd = open_tl_fd(pPciAddr, CONTROL_FILE_STRING);
    if (fd == -1)
    {
        ADF_ERROR("open_tl_fd failed\n");
        return CPA_STATUS_FAIL;
    }

    /* This file write command turns off telemetry feature using sysfs. */
    result = dprintf(fd, TL_DISABLE);
    if (result < 0)
    {
        ADF_ERROR("Print to file failed\n");
        status = CPA_STATUS_FAIL;
    }

    close(fd);

    return status;
}

/* This function starts the telemetry feature */
CpaStatus icp_adf_dev_telemetry_start(struct adf_pci_address *pPciAddr)
{
    int fd = -1;
    CpaStatus status = CPA_STATUS_SUCCESS;
    int result = -1;

    fd = open_tl_fd(pPciAddr, CONTROL_FILE_STRING);
    if (fd == -1)
    {
        ADF_ERROR("open_tl_fd failed\n");
        return CPA_STATUS_FAIL;
    }

    /* This file write command turns on telemetry feature using sysfs. */
    result = dprintf(fd, TL_ENABLE);
    if (result < 0)
    {
        ADF_ERROR("Print to file failed\n");
        status = CPA_STATUS_FAIL;
    }

    close(fd);

    return status;
}
