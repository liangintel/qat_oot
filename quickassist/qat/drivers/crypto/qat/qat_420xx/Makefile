########################################################################
#  SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
#  Copyright(c) 2021 Intel Corporation
########################################################################
.NOTPARALLEL:   # Not safe for parallel execution at this time
ccflags-y := -I$(src)/../qat_common
obj-$(CONFIG_CRYPTO_DEV_QAT_420XX) += qat_420xx.o
qat_420xx-objs := adf_stub.o
