########################################################################
#  SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
#  Copyright(c) 2022 Intel Corporation
########################################################################
.NOTPARALLEL:   # Not safe for parallel execution at this time
ccflags-y := -I$(src)/../qat_common
obj-$(CONFIG_CRYPTO_DEV_QAT_420XXVF) += qat_420xxvf.o
qat_420xxvf-objs := adf_stub.o
