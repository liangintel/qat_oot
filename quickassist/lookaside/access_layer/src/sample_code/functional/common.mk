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

#Set Upstream code based flags

PREBUILD_BINS = $(shell echo -e "\#include <qat/cpa.h>\n void main () {}" \
		| $(CC) -lqat -lusdm  -xc - -o /dev/null 2> /dev/null; \
		echo $$?)

#QA API and SAL PATHS
MYPWD=$(dir $(abspath $(lastword $(MAKEFILE_LIST))))

ifneq ($(findstring quickassist,$(MYPWD)),quickassist)
    SAMPLE_PATH:=$(MYPWD)/
    include $(SAMPLE_PATH)/../../sc_environment.mk
endif

ifneq ($(PREBUILD_BINS),0)
    ifndef ICP_ROOT
        $(error ICP_ROOT is undefined. Please set the path to the ICP_ROOT)
    endif

    ICP_API_DIR?=$(ICP_ROOT)/quickassist/include/
    ICP_LAC_DIR?=$(ICP_ROOT)/quickassist/lookaside/access_layer/
    SAMPLE_PATH?=$(ICP_ROOT)/quickassist/lookaside/access_layer/src/sample_code/functional/
    ICP_BUILD_OUTPUT?=$(ICP_ROOT)/build
    ICP_OSAL_DIR?=$(ICP_ROOT)/quickassist/utilities/osal/

    CMN_ROOT?=$(ICP_ROOT)/quickassist/utilities/libusdm_drv/
    CMN_MODULE_NAME?=libusdm_drv
endif

ifndef ICP_DC_ONLY
DO_CRYPTO?=1
endif
ifeq ($(DO_CRYPTO),1)
        EXTRA_CFLAGS+=-DDO_CRYPTO
endif

SC_ENABLE_DYNAMIC_COMPRESSION?=1
ifeq ($(SC_ENABLE_DYNAMIC_COMPRESSION),1)
        EXTRA_CFLAGS+=-DSC_ENABLE_DYNAMIC_COMPRESSION
endif

ifeq ($(PREBUILD_BINS),0)
    SAMPLE_PATH=$(shell pwd)/../../../functional/
endif

ifneq ($(PREBUILD_BINS),0)
#include files
    INCLUDES += -I$(ICP_API_DIR) \
		-I$(ICP_API_DIR)lac \
		-I$(ICP_API_DIR)dc \
		-I$(ICP_LAC_DIR)include \
		-I$(SAMPLE_PATH)include
else
    INCLUDES += -I$(SAMPLE_PATH)include \
		-I/usr/include/qat \
		-I/usr/local/include/qat
endif

#default builds user
ICP_OS_LEVEL?=user_space

ifeq ($(shell uname -s),Linux)
      OS=linux
      ICP_OS=linux_2.6
else
      ifeq ($(shell uname -s),FreeBSD)
            OS=freebsd
            ICP_OS=freebsd
      endif
endif

RM=rm -vf
RM-DIR=rm -rfv

ifeq ($(ICP_OS_LEVEL),user_space)
#############################################################
#
# Build user space executible
#
############################################################

ifneq ($(PREBUILD_BINS),0)
    ADDITIONAL_OBJECTS += -L/usr/Lib -L$(ICP_BUILD_OUTPUT)

    ADDITIONAL_OBJECTS += $(ICP_BUILD_OUTPUT)/libqat_s.so

    ADDITIONAL_OBJECTS += $(ICP_BUILD_OUTPUT)/libusdm_drv_s.so

    ifeq ($(ICP_OS),linux_2.6)
        ADDITIONAL_OBJECTS += -ludev
    endif

    USER_INCLUDES= -I$(CMN_ROOT)/

        ADDITIONAL_OBJECTS += -lpthread -lcrypto -lz
else
        ADDITIONAL_OBJECTS += -lpthread -lcrypto -lz -lusdm -lqat
endif

USER_INCLUDES+= $(INCLUDES)
ifdef SYSROOT
EXTRA_CFLAGS += --sysroot=$(SYSROOT)
endif

ifeq ($(ICP_DEBUG),y)
EXTRA_CFLAGS += -g
endif

default: clean
	$(CC) -Wall -O1 $(USER_INCLUDES)  -DUSER_SPACE $(EXTRA_CFLAGS) \
	$(USER_SOURCE_FILES) $(ADDITIONAL_OBJECTS) -o $(OUTPUT_NAME)

clean:
	$(RM) *.o $(OUTPUT_NAME)
else
#############################################################
#
# Build kernel space module
#
############################################################
EXTRA_CFLAGS+=$(INCLUDES)
KBUILD_EXTRA_SYMBOLS += $(SAMPLE_PATH)/../../Module.symvers
export $(KBUILD_EXTRA_SYMBOLS)

default: clean
	$(MAKE) -C $(KERNEL_SOURCE_ROOT) M=$(PWD) modules

clean:
	$(RM) *.mod.* *.ko *.o *.a
	$(RM) modules.order Module.symvers .*.*.*
	$(RM-DIR) .tmp_versions
endif
