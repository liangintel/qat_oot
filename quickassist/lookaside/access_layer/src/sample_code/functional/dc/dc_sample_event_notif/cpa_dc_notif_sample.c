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

/*
 * This is sample code that demonstrates usage of event notifications for
 * DC instances. The test flow as followed:
 * 1) the instances are created and notification callbacks are registered
 * 2) user issues adf_ctl reset
 * 3) the test code polls the device events until all the
 *    CPA_INSTANCE_EVENT_RESTARTED notifications are received.
 * Note: When reset is issued for QAT devices, before calling
 * icp_sal_userStop() wait for restarted events from all instances it has
 * access to from these QAT devices.
 */

#include "cpa.h"
#include "cpa_sample_utils.h"
#include "icp_sal_user.h"

extern int gDebugParam;
Cpa16U numRestartedEventArrived_g = 0;

/*
 * Callback function, this is an event handler.
 */
static void dcEventCallback(const CpaInstanceHandle instanceHandle,
                            void *pCallbackTag,
                            const CpaInstanceEvent instanceEvent)
{
    switch (instanceEvent)
    {
        case CPA_INSTANCE_EVENT_RESTARTING:
            PRINT_DBG("Event 'restarting' detected\n");
            break;
        case CPA_INSTANCE_EVENT_RESTARTED:
            PRINT_DBG("Event 'restarted' detected\n");
            numRestartedEventArrived_g++;
            break;
        case CPA_INSTANCE_EVENT_FATAL_ERROR:
            PRINT_DBG("'Fatal error' event detected\n");
            break;
    }
}

/*
 * This is the main entry point.
 */
CpaStatus dcSampleEventNotif(void)
{
    CpaStatus status = CPA_STATUS_SUCCESS;
    CpaInstanceHandle *dcInstHandle = NULL;
    Cpa16U indexInstance = 0, numInstances = 0;

    status = cpaDcGetNumInstances(&numInstances);
    if (CPA_STATUS_SUCCESS != status)
    {
        PRINT("Failure. cpaDcGetNumInstances() returned %d\n", status);
        return status;
    }

    if (0 == numInstances)
    {
        PRINT_ERR("DC instances are not present\n");
        return CPA_STATUS_FAIL;
    }

    dcInstHandle = qaeMemAlloc(sizeof(CpaInstanceHandle) * numInstances);
    if (NULL == dcInstHandle)
    {
        PRINT_ERR("Unable to allocate memory for Instances\n");
        return CPA_STATUS_RESOURCE;
    }

    status = cpaDcGetInstances(numInstances, dcInstHandle);
    if (CPA_STATUS_SUCCESS != status)
    {
        qaeMemFree((void **)&dcInstHandle);
        return status;
    }

    PRINT_DBG("Setting instance notification callback for the %d instances...",
              numInstances);
    /*
     * Register notification callbacks for all the dc instances we
     * have access to.
     */
    for (indexInstance = 0; indexInstance < numInstances; indexInstance++)
    {
        status = cpaDcInstanceSetNotificationCb(
            dcInstHandle[indexInstance], dcEventCallback, NULL);
        if (CPA_STATUS_SUCCESS != status)
        {
            PRINT("\nFailure. cpaDcInstanceSetNotificationCb() returned %d\n",
                  status);
            break;
        }
    }

    if (CPA_STATUS_SUCCESS == status)
    {
        PRINT("Done\n");
        PRINT("Please reset the device with the adf_ctl reset command\n");
    }

    /*
     * While loop, waiting for the all restarted events from those registered
     * notification callbacks then calling icp_sal_userStop() in main function.
     */
    while ((numRestartedEventArrived_g < numInstances) &&
           (CPA_STATUS_SUCCESS == status))
    {
        status = icp_sal_poll_device_events();
    }

    if (CPA_STATUS_SUCCESS != status)
    {
        PRINT_DBG("Failure. icp_sal_poll_device_events() returned %d", status);
    }
    qaeMemFree((void **)&dcInstHandle);
    return status;
}
