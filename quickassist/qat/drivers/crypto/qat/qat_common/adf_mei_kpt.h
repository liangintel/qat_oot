/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2017 Intel Corporation */
#ifndef ADF_KPT_MEI_H_
#define ADF_KPT_MEI_H_

#include <linux/slab.h>
#include <linux/completion.h>
#include <linux/watchdog.h>
#include <linux/uuid.h>
#include <linux/mei_cl_bus.h>

#define MEI_KPT_REINIT_KPT      0x01
#define MEI_KPT_REINIT_KPT_RSP  0x81
#define MEI_KPT_DISCOVERY_TIMEOUT   2000

#define ADF_MEI_KPT_UUID UUID_LE(0xa2b66ab9, 0x996c, 0x41f7, \
		    0x9b, 0x7f, 0x96, 0xb5, 0xbb, 0x46, 0xfe, 0x40)
struct adf_mei_kpt {
	struct mei_cl_device *cldev;
	struct completion response;
	u32 firmware_version;
};

struct adf_mei_kpt_request {
	u8 cmd;
} __packed;

struct adf_mei_kpt_response {
	u8 cmd;
	u8 status;
} __packed;

int adf_mei_send_discovery_kpt(void);

#endif
