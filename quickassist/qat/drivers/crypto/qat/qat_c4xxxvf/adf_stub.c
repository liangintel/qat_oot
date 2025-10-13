// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2016 - 2021 Intel Corporation */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <adf_common_drv.h>

static int __init adfdrv_init(void)
{
	pr_warn("QAT: qat_c4xxxvf is not supported");

	return 0;
}

static void __exit adfdrv_release(void)
{
}

module_init(adfdrv_init);
module_exit(adfdrv_release);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Intel");
MODULE_DESCRIPTION("Intel(R) QuickAssist Technology");
MODULE_VERSION(ADF_DRV_VERSION);
