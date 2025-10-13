########################################################################
#  SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
#  Copyright(c) 2014 - 2021 Intel Corporation
########################################################################
export CONFIG_CRYPTO_DEV_QAT=m
export CONFIG_CRYPTO_DEV_QAT_DH895xCC=m
export CONFIG_CRYPTO_DEV_QAT_DH895xCCVF=m
export CONFIG_CRYPTO_DEV_QAT_C3XXX=m
export CONFIG_CRYPTO_DEV_QAT_C62X=m
export CONFIG_CRYPTO_DEV_QAT_C3XXXVF=m
export CONFIG_CRYPTO_DEV_QAT_C62XVF=m
export CONFIG_CRYPTO_DEV_QAT_C4XXX=m
export CONFIG_CRYPTO_DEV_QAT_C4XXXVF=m
export CONFIG_CRYPTO_DEV_QAT_D15XX=m
export CONFIG_CRYPTO_DEV_QAT_D15XXVF=m
export CONFIG_CRYPTO_DEV_QAT_4XXX=m
export CONFIG_CRYPTO_DEV_QAT_4XXXVF=m
export CONFIG_CRYPTO_DEV_QAT_VQAT=m
export QAT_UIO?=y
export ICP_HB_FAIL_SIM?=n
export QAT_LEGACY_ALGORITHMS?=n
export QAT19_ONLY?=n
export CONFIG_CRYPTO_DEV_QAT_420XX=m
export CONFIG_CRYPTO_DEV_QAT_420XXVF=m

OS_ID := $(shell cat /etc/os-release | grep "\<ID\>" | awk 'BEGIN{FS="="}{print $$2}')
ifeq ($(shell test $(OS_ID) = "ubuntu"; echo $$?), 0)
	subdir-ccflags-y += -DOS_UBUNTU_COMPATIBLE=y
endif

ifeq ($(shell test $(OS_ID) = "centos"; echo $$?), 0)
        subdir-ccflags-y += -DOS_CENTOS_COMPATIBLE=y
endif

BKC_ID := $(shell uname -r)
ifeq ($(findstring bkc,$(BKC_ID)),bkc)
	subdir-ccflags-y += -DOS_BKC_COMPATIBLE=y
endif

ifeq ($(ICP_HB_FAIL_SIM),y)
$(info Compiling with Heartbeat Failure Simulation feature)
endif

OS_ID := $(shell cat /etc/os-release | grep "\<ID\>" | awk 'BEGIN{FS="="}{print $$2}')
$(info $(OS_ID))
ifeq ($(shell test $(OS_ID) = "intel-axxia"; echo $$?), 0)
VERSION_ID := $(shell uname -r | cut -d'.' -f1-2 )
$(info $(VERSION_ID))
ifeq ($(shell test $(VERSION_ID) = "5.10"; echo $$?), 0)
subdir-ccflags-y += -DOS_YOCTO_05_10
endif
endif

ifeq ($(shell test $(OS_ID) = "sles"; echo $$?), 0)
SLE_VERSION_ID := $(shell cat /etc/os-release | grep "\<VERSION_ID\>" | awk 'BEGIN{FS="="}{print $$2}')
ifeq ($(shell test $(SLE_VERSION_ID) = "15.5"; echo $$?), 0)
subdir-ccflags-y += -DOS_SLE15_SP5=y
endif
endif

ifeq ($(ICP_ERR_INJECTION_SIM),y)
$(info Compiling with Debug Injection Simulation feature)
endif

ifeq ($(KERNELRELEASE),)
KDIR ?= $(INSTALL_MOD_PATH)/lib/modules/$(shell uname -r)/build
ifneq ($(shell if [ -e $(KDIR)/include/config/auto.conf ]; then echo 1; fi),1)
  $(error ERROR: Kernel header files not found.  Install the appropriate \
    kernel development package necessary for building external kernel modules \
    or run 'make oldconfig && make modules_prepare' on kernel src to fix it)
endif
include $(KDIR)/include/config/auto.conf
INSTALL_FW_PATH ?= $(INSTALL_MOD_PATH)/lib/firmware


default: modules

# Error out on missing kernel config dependencies
CONFIG_%:
	$(if $($@), , $(error $@ not enabled in kernel configuration))

KCONFIG_DEP := CONFIG_X86 CONFIG_PCI CONFIG_CRYPTO CONFIG_CRYPTO_HW
KCONFIG_DEP += CONFIG_FW_LOADER
KCONFIG_DEP += CONFIG_CRYPTO_AEAD CONFIG_CRYPTO_AUTHENC CONFIG_CRYPTO_ALGAPI
KCONFIG_DEP += CONFIG_CRYPTO_AES CONFIG_CRYPTO_CBC
KCONFIG_DEP += CONFIG_CRYPTO_SHA1 CONFIG_CRYPTO_SHA256 CONFIG_CRYPTO_SHA512
ifeq ($(QAT_UIO),y)
KCONFIG_DEP += CONFIG_UIO
endif


.PHONY: modules
modules: $(KCONFIG_DEP)
	$(MAKE) -C $(KDIR) M=$(CURDIR) $@

.PHONY: modules_install
modules_install: modules
	@# Install the external/out-of-tree modules under the
	@# $(INSTALL_MOD_PATH)/lib/modules/$(KERNELRELEASE)/updates/
	@# directory in order to supersede any in-kernel modules with the
	@# same name.  Do not remove the latter modules as that could corrupt
	@# the package manager (e.g. rpm) database.
	$(MAKE) -C $(KDIR) M=$(CURDIR) INSTALL_MOD_DIR=updates $@

.PHONY: clean
clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) $@

.PHONY: help
help:
	@echo ''
	@echo '  Building external/out-of-tree Intel(R) QuickAssist Technology modules.'
	@echo ''
	@echo '  Syntax: make [install-location-options] [target]'
	@echo ''
	@echo '  install-location-options'
	@echo '    [KDIR=<path>]             - path to kernel source (default: running kernel)'
	@echo '    [INSTALL_MOD_PATH=<path>] - optional prefix to the default /lib/modules/...'
	@echo '    [INSTALL_FW_PATH=<path>]  - default: $$(INSTALL_MOD_PATH)/lib/firmware/'
	@echo ''
	@echo '  target'
	@echo '    [modules]                 - default target, build the module(s)'
	@echo '    {modules_install}         - install the module(s) and firmware'
	@echo '    {clean}                   - remove generated files'
	@echo ''

else
subdir-ccflags-y += -include $(src)/compat/qat_compat.h
subdir-ccflags-y += -include $(src)/compat/qat_compat_aux.h
obj-m := drivers/crypto/qat/
endif
