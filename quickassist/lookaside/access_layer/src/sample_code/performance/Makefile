#########################################################################
#
# @par
#   BSD LICENSE
# 
#   Copyright(c) 2007-2023 Intel Corporation. All rights reserved.
#   All rights reserved.
# 
#   Redistribution and use in source and binary forms, with or without
#   modification, are permitted provided that the following conditions
#   are met:
# 
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in
#       the documentation and/or other materials provided with the
#       distribution.
#     * Neither the name of Intel Corporation nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
# 
#   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
#   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
#   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
#   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
#   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
#   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
#   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
#   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
#   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
#   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
#   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
# 
#  version: QAT20.L.1.2.30-00090
############################################################################


####################Common variables and definitions########################

CC?=gcc

PREBUILD_BINS = -1

PREBUILD_BINS = $(shell echo -e "\#include <qat/cpa.h>\n void main () {}" \
		| $(CC) -lqat -lusdm  -xc - -o /dev/null 2> /dev/null; \
		echo $$?)
MYPWD=$(dir $(abspath $(lastword $(MAKEFILE_LIST))))

INCLUDE_COMPRESSION=1
DO_CRYPTO?=1

SOURCES := cpa_sample_code_main.c

ifneq ($(findstring quickassist,$(MYPWD)),quickassist)
ifneq ($(PREBUILD_BINS),0)
    include $(MYPWD)/../../sc_environment.mk
    include $(PERF_PATH)/../../performance.mk
endif
else
     KBUILD_EXTRA_SYMBOLS += $(PERF_SAMPLE_SRC_ROOT)/../../Module.symvers
     export $KBUILD_EXTRA_SYMBOLS
endif

ifeq ($(PREBUILD_BINS),0)
     SC_SYM_SESSION_INUSE_DISABLED=0
     ifeq ($(ICP_BUILDSYSTEM_PATH),)
         -include build.mk
     endif
endif

ifneq ($(PREBUILD_BINS),0)
    # Ensure The ICP_ENV_DIR environmental var is defined.
    ICP_ENV_DIR?=$(ICP_ROOT)/quickassist/build_system/build_files/env_files
    ICP_BUILDSYSTEM_PATH?=$(ICP_ROOT)/quickassist/build_system

    #Add your project environment Makefile
    include $(ICP_ENV_DIR)/environment.mk
    #Lac include paths
    include $(LAC_DIR)/common.mk
    ICP_OS_LEVEL?=kernel_space

    ifeq ($(ICP_OS_LEVEL),user_space)
    	CMN_ROOT?=$(ICP_ROOT)/quickassist/utilities/libusdm_drv/
        CMN_MODULE_NAME?=libusdm_drv
    else
    	CMN_ROOT?=$(ICP_ROOT)/quickassist/lookaside/access_layer/src/sample_code/performance/qae/
    endif
else
    ICP_OS_LEVEL=user_space
    ICP_OS?=linux_2.6
endif

ICP_OS?=linux
ICP_BUILD_OUTPUT?=$(ICP_ROOT)/build

ifeq ($(ICP_OS),linux_2.6)
OS=linux
else
OS=freebsd
endif

ifneq ($(PREBUILD_BINS),0)
    PERF_SAMPLE_SRC_ROOT ?= $(ICP_ROOT)/quickassist/lookaside/access_layer/src/sample_code/performance
    SAMPLE_BUILD_OUTPUT?=$(PERF_SAMPLE_SRC_ROOT)/../build
    #include the makefile with all the default and common Make variable definitions
    include $(ICP_BUILDSYSTEM_PATH)/build_files/common.mk
else
    PERF_SAMPLE_SRC_ROOT=$(shell pwd)
    SAMPLE_BUILD_OUTPUT=$(PERF_SAMPLE_SRC_ROOT)/../build
endif

ifeq ($(PREBUILD_BINS),0)
    -include $(ICP_ENV_DIR)/environment.mk
    -include $(LAC_DIR)/common.mk
    -include $(ICP_BUILDSYSTEM_PATH)/build_files/common.mk
    -include $(ICP_ENV_DIR)/$(ICP_OS)_$(ICP_OS_LEVEL).mk
endif

#Add the name for the executable, Library or Module output definitions
OUTPUT_NAME=cpa_sample_code

ifeq ($(DO_CRYPTO),1)
EXTRA_CFLAGS += -DDO_CRYPTO
endif

ifeq ($(SC_CHAINING_ENABLED),1)
    EXTRA_CFLAGS += -DSC_CHAINING_ENABLED
endif

ifeq ($(SC_SYM_UPDATE_DISABLED),1)
    EXTRA_CFLAGS += -DSC_SYM_UPDATE_DISABLED
endif

ifeq ($(SC_SYM_SESSION_INUSE_DISABLED),1)
    EXTRA_CFLAGS += -DSC_SYM_SESSION_INUSE_DISABLED
endif

ifeq ($(SC_PARAM_CHECK_ENABLED),1)
        EXTRA_CFLAGS += -DSC_PARAM_CHECK_ENABLED
endif

ifeq ($(SC_STATS_ENABLED),1)
        EXTRA_CFLAGS += -DSC_STATS_ENABLED
endif

ifeq ($(ICP_DELAYED_RESPONSE),1)
    EXTRA_CFLAGS += -DICP_DELAYED_RESPONSE
endif

ifeq ($(ICP_DC_ERROR_SIMULATION),1)
    EXTRA_CFLAGS += -DICP_DC_ERROR_SIMULATION
endif

ifeq ($(SC_KPT2_ENABLED),1)
    EXTRA_CFLAGS += -DSC_KPT2_ENABLED
endif

ifeq ($(SC_CHAINING_EXT_ENABLED),1)
EXTRA_CFLAGS += -DSC_CHAINING_EXT_ENABLED
endif

# List of Source Files to be compiled (to be in a single line or on different lines separated by a "\" and tab.
SOURCES+= framework/$(OS)/$(ICP_OS_LEVEL)/cpa_sample_code_utils.c \
	framework/cpa_sample_code_framework.c \
	common/qat_perf_utils.c 

ifeq ($(DO_CRYPTO),1)
SOURCES+= crypto/cpa_sample_code_crypto_utils.c \
	crypto/cpa_sample_code_sym_perf.c \
	crypto/cpa_sample_code_sym_perf_dp.c \
	crypto/cpa_sample_code_rsa_perf.c \
	crypto/cpa_sample_code_dsa_perf.c \
	crypto/cpa_sample_code_ecdsa_perf.c \
	crypto/cpa_sample_code_dh_perf.c \
	crypto/qat_sym_main.c \
	crypto/qat_sym_utils.c \
	crypto/cpa_sample_code_sym_update_common.c \
	crypto/cpa_sample_code_sym_update.c \
	crypto/cpa_sample_code_sym_update_dp.c
ifeq ($(ICP_OS_LEVEL),user_space)
ifeq ($(SC_KPT2_ENABLED),1)
SOURCES+= crypto/cpa_sample_code_kpt2_common.c \
	crypto/cpa_sample_code_rsa_kpt2_perf.c \
	crypto/cpa_sample_code_ecdsa_kpt2_perf.c
endif
endif
ifeq ($(ICP_OS_LEVEL),user_space)
ifeq ($(SC_SM2_ENABLED),1)
EXTRA_CFLAGS += -DSC_SM2_ENABLED
SOURCES+= crypto/cpa_sample_code_sm2_perf.c \
	crypto/cpa_sample_code_sm2_sign_verify.c \
	crypto/cpa_sample_code_sm2_Keyex_P1_P2.c \
	crypto/cpa_sample_code_sm2_kdf_hash.c \
	crypto/cpa_sample_code_sm2_enc_dec.c
endif
endif

endif

ifeq ($(INCLUDE_COMPRESSION),1)
EXTRA_CFLAGS += -DINCLUDE_COMPRESSION
INCLUDES += -I$(PERF_SAMPLE_SRC_ROOT)/../busy_loop/
SOURCES+=  common/qat_perf_buffer_utils.c \
	compression/cpa_sample_code_dc_utils.c \
	compression/cpa_sample_code_dc_dp.c \
	compression/cpa_sample_code_zlib.c \
	compression/cpa_sample_code_dc_stateful2.c \
	common/qat_perf_latency.c \
	common/qat_perf_sleeptime.c \
	compression/qat_compression_main.c \
	compression/qat_compression_utils.c \
	compression/qat_compression_e2e.c \
	../busy_loop/busy_loop.c
ifeq ($(ICP_OS_LEVEL),user_space)
ifeq ($(SC_CHAINING_ENABLED),1)
SOURCES+=  compression/qat_chaining_main.c
endif #chaining enabled
ifeq ($(SC_CHAINING_EXT_ENABLED),1)
SOURCES+=  compression/qat_chaining_ext_main.c
endif #chaining xstore
endif
endif #include compression

SC_ENABLE_DYNAMIC_COMPRESSION?=1
ifeq ($(SC_ENABLE_DYNAMIC_COMPRESSION),1)
	EXTRA_CFLAGS += -DSC_ENABLE_DYNAMIC_COMPRESSION
endif

# FreeBSD does not support e-poll
ifeq ($(ICP_OS),freebsd)
EXTRA_CFLAGS += -DSC_EPOLL_DISABLED
EXTRA_CFLAGS += -DSC_CORE_NUM_POLICY
else
# For everything else check the environment variable
ifeq ($(SC_EPOLL_DISABLED),1)
EXTRA_CFLAGS += -DSC_EPOLL_DISABLED
endif
endif

ifeq ($(LATENCY_CODE),1)
	EXTRA_CFLAGS += -DLATENCY_CODE=1
endif

ifeq ($(STV_BUFFER_SIZE_DEBUG),1)
	EXTRA_CFLAGS += -DSTV_BUFFER_SIZE_DEBUG=1
endif

ifeq ($(STV_ADD_TEST_IDS),1)
	EXTRA_CFLAGS += -DSTV_ADD_TEST_IDS=1
endif

EXTRA_CFLAGS += -DUSE_ZLIB

ifeq ($(ICP_OS_LEVEL),kernel_space)
SOURCES += framework/$(OS)/$(ICP_OS_LEVEL)/cpa_sample_code_module.c
endif

ifeq ($(DO_CRYPTO),1)
SOURCES += crypto/cpa_sample_code_ike_rsa_perf.c
endif

ifneq ($(PREBUILD_BINS),0)
#common includes between all supported OSes
    INCLUDES += -I$(PERF_SAMPLE_SRC_ROOT) \
		-I$(PERF_SAMPLE_SRC_ROOT)/framework/ \
		-I$(PERF_SAMPLE_SRC_ROOT)/framework/$(OS)/$(ICP_OS_LEVEL) \
		-I$(PERF_SAMPLE_SRC_ROOT)/crypto/ \
		-I$(PERF_SAMPLE_SRC_ROOT)/compression/ \
		-I$(PERF_SAMPLE_SRC_ROOT)/common/ \
		-I$(CMN_ROOT)/
    INCLUDES += -I$(PERF_SAMPLE_SRC_ROOT)/../functional/include/
else
    INCLUDES += -I$(PERF_SAMPLE_SRC_ROOT)/../busy_loop/ \
		-I$(PERF_SAMPLE_SRC_ROOT) \
		-I$(PERF_SAMPLE_SRC_ROOT)/framework/ \
		-I$(PERF_SAMPLE_SRC_ROOT)/framework/$(OS)/$(ICP_OS_LEVEL) \
		-I$(PERF_SAMPLE_SRC_ROOT)/crypto/ \
		-I$(PERF_SAMPLE_SRC_ROOT)/compression/ \
		-I$(PERF_SAMPLE_SRC_ROOT)/common/ \
		-I/usr/include/qat \
		-I/usr/local/include/qat
endif

ifeq ($(SC_CHAINING_EXT_ENABLED),1)
    INCLUDES += -I$(LAC_DIR)/src/common/compression/include/
endif

ifeq ($(SC_CHAINING_ENABLED),1)
    INCLUDES += -I$(LAC_DIR)/src/common/compression/include/
endif

ifeq ($(ICP_OS_LEVEL),kernel_space)
    SOURCES += qae/$(OS)/$(ICP_OS_LEVEL)/qae_mem_utils.c
    ifeq ($(OS),linux)
	BUILD_KERNEL_VERSION_MAJOR:=$(shell echo $(KERNELVERSION) | cut -d'.' -f1)
	BUILD_KERNEL_VERSION_MINOR:=$(shell echo $(KERNELVERSION) | cut -d'.' -f2)
    endif
endif

ifeq ($(ICP_OS_LEVEL),user_space)
EXTRA_CFLAGS += -DUSER_SPACE -D_GNU_SOURCE
EXTRA_LDFLAGS += -L$(ICP_BUILD_OUTPUT)

ifneq ($(PREBUILD_BINS),0)
    ADDITIONAL_OBJECTS += $(ICP_BUILD_OUTPUT)/libusdm_drv_s.so
    ADDITIONAL_OBJECTS += $(ICP_BUILD_OUTPUT)/libqat_s.so
else
     ADDITIONAL_OBJECTS += -lusdm -lqat
endif

ADDITIONAL_OBJECTS += -L/Lib -lpthread
ADDITIONAL_OBJECTS += -lcrypto 
ifeq ($(OS),linux)
	ADDITIONAL_OBJECTS += -ludev
endif
ADDITIONAL_OBJECTS += -lcrypto -lz
MACHINE?=$(shell uname -m)
ifeq ($(MACHINE), x86_64)
    ifeq ($(ARCH), i386)
            EXTRA_CFLAGS+=-DSAMPLE_KERNEL64_USER32
    endif
    ifeq ($(ARCH), i686)
             EXTRA_CFLAGS+=-DSAMPLE_KERNEL64_USER32
             EXTRA_CFLAGS+=-march=i686
    endif
endif

else
EXTRA_CFLAGS += -DKERNEL_SPACE

# Check for defense with stack protection in kernel
ifeq ($(KERNEL_DEFENSES_STACK_PROTECTION), n)
STACK_PROTECTION=-fstack-protector -fstack-protector-strong
EXTRA_CFLAGS := $(filter-out $(STACK_PROTECTION), $(EXTRA_CFLAGS))
endif
endif

ifneq ($(PREBUILD_BINS),0)
include $(ICP_ENV_DIR)/$(ICP_OS)_$(ICP_OS_LEVEL).mk
endif

RM=rm -f
ifeq ($(OS),linux)
    ifeq ($(ICP_OS_LEVEL),kernel_space)
        ifneq ($(BUILD_KERNEL_VERSION_MAJOR),)
            ifeq ($(shell test $(BUILD_KERNEL_VERSION_MAJOR) -eq 5; echo $$?), 0)
                ifeq ($(shell test $(BUILD_KERNEL_VERSION_MINOR) -lt 19; echo $$?), 0)
                    ADDITIONAL_KERNEL_LIBS=$(patsubst %.c, %.o, $(SOURCES))
                endif
            endif
        endif
    endif
endif

# On the line directly below list the outputs you wish to build for
ifeq ($(ICP_OS_LEVEL),user_space)
install: exe
lib: lib_shared
all: lib_shared exe
else
install: module
endif
.PHONY : clean
clean: perf_clean
perf_clean:
	@echo ; echo "Cleaning performance generated files. $(PERF_SAMPLE_SRC_ROOT) ";
	$(RM) $(PERF_SAMPLE_SRC_ROOT)/build/$(ICP_OS)/user_space/*;
	$(RM) $(PERF_SAMPLE_SRC_ROOT)/crypto/.*.o.cmd ;
	$(RM) $(PERF_SAMPLE_SRC_ROOT)/compression/.*.o.cmd ;
	$(RM) $(PERF_SAMPLE_SRC_ROOT)/framework/.*.o.cmd ;
	$(RM) $(PERF_SAMPLE_SRC_ROOT)/framework/$(OS)/kernel_space/.*.o.cmd ;
	$(RM) $(PERF_SAMPLE_SRC_ROOT)/common/.*.o.cmd ;
	$(RM) $(PERF_SAMPLE_SRC_ROOT)/build/$(ICP_OS)/kernel_space/*;
	$(RM) $(PERF_SAMPLE_SRC_ROOT)/qae/linux/kernel_space/.*.o.cmd;
	$(RM) $(PERF_SAMPLE_SRC_ROOT)/../busy_loop/.*.o.cmd;

###################Include rules makefiles########################
-include $(ICP_BUILDSYSTEM_PATH)/build_files/rules.mk
###################End of Rules inclusion#########################
