########################################################################
#  SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
#  Copyright(c) 2014 - 2021 Intel Corporation
########################################################################
define ver
    $$(($(1) * 100 + $(2)))
endef

kernel:=$(call ver,$(VERSION),$(PATCHLEVEL))
OS_ID := $(shell cat /etc/os-release | grep "\<ID\>" | awk 'BEGIN{FS="\""}{print $$2}')
ifeq ($(shell test $(kernel) -ge $(call ver,5,16); echo $$?), 0)
        CONFIG_QAT_MDEV=0
else ifeq ($(shell test "$(OS_ID)" = "rhel" -a $(kernel) -ge $(call ver,5,14); echo $$?), 0)
        CONFIG_QAT_MDEV=0
else ifeq ($(shell test "$(OS_ID)" = "sles" -a $(kernel) -ge $(call ver,5,14); echo $$?), 0)
        CONFIG_QAT_MDEV=0
else ifeq ($(shell test "$(OS_ID)" = "sle_rt" -a $(kernel) -ge $(call ver,5,14); echo $$?), 0)
	CONFIG_QAT_MDEV=0
else ifeq ($(shell test "$(OS_ID)" = "rocky" -a $(kernel) -ge $(call ver,5,14); echo $$?), 0)
	CONFIG_QAT_MDEV=0
else ifeq ($(shell test $(kernel) -ge $(call ver,5,14); echo $$?), 0)
  ifdef CONFIG_VFIO_MDEV
	CONFIG_QAT_MDEV=1
  endif
else ifeq ($(shell test $(kernel) -ge $(call ver,4,10); echo $$?), 0)
  ifdef CONFIG_VFIO_MDEV_DEVICE
	CONFIG_QAT_MDEV=1
  endif
endif
ifeq ($(CONFIG_QAT_MDEV), 1)
ifdef CONFIG_IRQ_BYPASS_MANAGER
  export CONFIG_CRYPTO_DEV_QAT_VDCM=m
endif
endif

obj-$(CONFIG_CRYPTO_DEV_QAT) += qat_common/
obj-$(CONFIG_CRYPTO_DEV_QAT_DH895xCC) += qat_dh895xcc/
obj-$(CONFIG_CRYPTO_DEV_QAT_C3XXX) += qat_c3xxx/
obj-$(CONFIG_CRYPTO_DEV_QAT_C62X) += qat_c62x/
obj-$(CONFIG_CRYPTO_DEV_QAT_C4XXX) += qat_c4xxx/
obj-$(CONFIG_CRYPTO_DEV_QAT_DH895xCCVF) += qat_dh895xccvf/
obj-$(CONFIG_CRYPTO_DEV_QAT_C3XXXVF) += qat_c3xxxvf/
obj-$(CONFIG_CRYPTO_DEV_QAT_C62XVF) += qat_c62xvf/
obj-$(CONFIG_CRYPTO_DEV_QAT_C4XXXVF) += qat_c4xxxvf/
obj-$(CONFIG_CRYPTO_DEV_QAT_D15XX) += qat_d15xx/
obj-$(CONFIG_CRYPTO_DEV_QAT_D15XXVF) += qat_d15xxvf/
obj-$(CONFIG_CRYPTO_DEV_QAT_4XXX) += qat_4xxx/
obj-$(CONFIG_CRYPTO_DEV_QAT_4XXXVF) += qat_4xxxvf/
obj-$(CONFIG_CRYPTO_DEV_QAT_VQAT) += qat_vqat/
obj-$(CONFIG_CRYPTO_DEV_QAT_420XX) += qat_420xx/
obj-$(CONFIG_CRYPTO_DEV_QAT_420XXVF) += qat_420xxvf/
subdir-ccflags-y += -DQAT_420XX=y
ifdef CONFIG_CRYPTO_DEV_QAT_VDCM
subdir-ccflags-y += -DCONFIG_CRYPTO_DEV_QAT_VDCM=y
endif

subdir-ccflags-$(QAT_UIO) += -DQAT_UIO=y
subdir-ccflags-$(QAT_LEGACY_ALGORITHMS) += -DQAT_LEGACY_ALGORITHMS=n

ifdef KPT
subdir-ccflags-$(QAT_UIO) += -DQAT_KPT=y
endif

