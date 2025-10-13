################################################################
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
################################################################

#Set Upstream code based flags

PREBUILD_BINS = $(shell echo -e "\#include <qat/cpa.h>\n void main () {}" \
		| $(CC) -lqat -lusdm  -xc - -o /dev/null 2> /dev/null; \
		echo $$?)

MYPWD=$(dir $(abspath $(lastword $(MAKEFILE_LIST))))

SAMPLE_PATH?=$(shell pwd)

ifneq ($(PREBUILD_BINS),0)
ifeq ($(findstring quickassist,$(MYPWD)),quickassist)
    SAMPLE_PATH?=$(ICP_ROOT)/quickassist/lookaside/access_layer/src/sample_code/functional
    ICP_API_DIR?=$(ICP_ROOT)/quickassist/include/
    ICP_LAC_DIR?=$(ICP_ROOT)/quickassist/lookaside/access_layer/
    ICP_BUILD_OUTPUT?=$(ICP_ROOT)/build
else
    include $(SAMPLE_PATH)/../../sc_environment.mk
endif
    DRV_OBJ?= libqat_s.so
endif

DC_PATH?=$(SAMPLE_PATH)/dc
SYM_PATH?=$(SAMPLE_PATH)/sym
ASYM_PATH?=$(SAMPLE_PATH)/asym
CHAINING_PATH?=$(SAMPLE_PATH)/dc/chaining_sample

ifndef ICP_OS
    ifeq ($(shell uname -s),Linux)
        ICP_OS=linux_2.6
    else
        ifeq ($(shell uname -s),FreeBSD)
            ICP_OS=freebsd
        endif
    endif
endif

ifneq ($(PREBUILD_BINS),0)
    CMN_ROOT?=$(ICP_ROOT)/quickassist/utilities/libusdm_drv/
    CMN_MODULE_NAME?=libusdm_drv

    ifndef SAMPLE_PATH
    $(error SAMPLE_PATH is undefined. Please set the path to your environment makefile \
            "-> setenv SAMPLE_PATH <functional sample code path>")
    endif

    ifndef ICP_OS
    $(error ICP_OS is undefined. Please set the variable in your environment makefile \
            "-> setenv ICP_OS [linux_2.6|freebsd]")
    endif

    ifndef DC_PATH
    $(error DC_PATH is undefined. Please set the path to your environment makefile \
            "-> setenv DC_PATH <SAMPLE_PATH/dc>")
    endif

    ifndef SYM_PATH
    $(error SYM_PATH is undefined. Please set the path to your environment makefile \
            "-> setenv SYM_PATH <SAMPLE_PATH/sym>")
    endif

    ifndef ASYM_PATH
    $(error ASYM_PATH is undefined. Please set the path to your environment makefile \
            "-> setenv ASYM_PATH <SAMPLE_PATH/asym>")
    endif

    ifndef ICP_API_DIR
    $(error ICP_API_DIR is undefined. Please set the path to the ICP_API_DIR \
            "-> setenv ICP_API_DIR <ICP_ROOT>/quickassist/include/")
    endif

    ifndef  ICP_LAC_DIR
    $(error ICP_LAC_DIR is undefined. Please set the path to the ICP_LAC_DIR \
            "-> setenv ICP_LAC_DIR <ICP_ROOT>/quickassist/lookaside/access_layer/")
    endif
endif

SAMPLE_BUILD_OUTPUT?=$(SAMPLE_PATH)/build
HKDF_SUPPORTED = -1
MONTEDWDS_SUPPORTED = -1
CHAINING = -1

ifneq ("$(wildcard $(ICP_BUILD_OUTPUT)/$(DRV_OBJ))", "")
HKDF_SUPPORTED = $(shell strings $(ICP_BUILD_OUTPUT)/$(DRV_OBJ) | grep -q cpaCyKeyGenTls3 1>&2 2> /dev/null; echo $$?)
MONTEDWDS_SUPPORTED = $(shell strings $(ICP_BUILD_OUTPUT)/$(DRV_OBJ) | grep -q cpaCyEcMontEdwdsPointMultiply 1>&2 2> /dev/null; echo $$?)
CHAINING = $(shell strings $(ICP_BUILD_OUTPUT)/$(DRV_OBJ) | grep -q cpaDcChainPerformOp 1>&2 2> /dev/null; echo $$?)
endif

RM=rm -f
ifndef ICP_DC_ONLY
DO_CRYPTO?=1
endif
ifeq ($(DO_CRYPTO),1)
        CLEAN_ALL = clean_dc clean_sym clean_asym
        BUILD_FUNCIONAL_SAMPLE_CODE = dc sym asym
else
    CLEAN_ALL = clean_dc
    BUILD_FUNCIONAL_SAMPLE_CODE = dc
endif

output_dir:
	test -d $(SAMPLE_BUILD_OUTPUT) || mkdir $(SAMPLE_BUILD_OUTPUT);

BUILD_DC=dc_dp_sample stateless_sample stateful_sample \
         stateless_multi_op_checksum_sample dc_sample_event_notif

ifeq ($(ICP_OS),linux_2.6)
ifeq ($(CHAINING), 0)
	BUILD_DC += dc_chaining_sample
endif
endif
dc: $(BUILD_DC)
	@echo ; echo "Build dc component";

dc_dp_sample: output_dir
	@cd $(DC_PATH)/dc_dp_sample && $(MAKE) clean && $(MAKE) ICP_OS_LEVEL=user_space
	@cp $(DC_PATH)/dc_dp_sample/dc_dp_sample $(SAMPLE_BUILD_OUTPUT)/;
ifeq ($(ICP_OS_LEVEL),kernel_space)
	@cd $(DC_PATH)/dc_dp_sample && $(MAKE) ICP_OS_LEVEL=kernel_space
	@cp $(DC_PATH)/dc_dp_sample/dc_dp_sample.ko $(SAMPLE_BUILD_OUTPUT)/;
endif

stateless_sample: output_dir
	@cd $(DC_PATH)/stateless_sample && $(MAKE) clean && $(MAKE) ICP_OS_LEVEL=user_space
	@cp $(DC_PATH)/stateless_sample/dc_stateless_sample $(SAMPLE_BUILD_OUTPUT)/;
ifeq ($(ICP_OS_LEVEL),kernel_space)
	@cd $(DC_PATH)/stateless_sample && $(MAKE) ICP_OS_LEVEL=kernel_space
	@cp $(DC_PATH)/stateless_sample/dc_stateless_sample.ko $(SAMPLE_BUILD_OUTPUT)/;
endif

stateful_sample: output_dir
	@cd $(DC_PATH)/stateful_sample && $(MAKE) clean ICP_OS_LEVEL=user_space && $(MAKE) ICP_OS_LEVEL=user_space
	@cp $(DC_PATH)/stateful_sample/dc_stateful_sample $(SAMPLE_BUILD_OUTPUT)/;
	@cp $(DC_PATH)/stateful_sample/paper4 $(SAMPLE_BUILD_OUTPUT)/;

stateless_multi_op_checksum_sample: output_dir
	@cd $(DC_PATH)/stateless_multi_op_checksum_sample && $(MAKE) clean && $(MAKE) ICP_OS_LEVEL=user_space
	@cp $(DC_PATH)/stateless_multi_op_checksum_sample/dc_stateless_multi_op_sample $(SAMPLE_BUILD_OUTPUT)/;
ifeq ($(ICP_OS_LEVEL),kernel_space)
	@cd $(DC_PATH)/stateless_multi_op_checksum_sample && $(MAKE) ICP_OS_LEVEL=kernel_space
	@cp $(DC_PATH)/stateless_multi_op_checksum_sample/dc_stateless_multi_op_sample.ko $(SAMPLE_BUILD_OUTPUT)/;
endif

dc_chaining_sample: output_dir
	@cd $(DC_PATH)/chaining_sample && $(MAKE) clean && $(MAKE) ICP_OS_LEVEL=user_space
	@cp $(DC_PATH)/chaining_sample/chaining_sample $(SAMPLE_BUILD_OUTPUT)/;

dc_sample_event_notif: output_dir
	@cd $(DC_PATH)/dc_sample_event_notif && $(MAKE) clean && $(MAKE) ICP_OS_LEVEL=user_space
	@cp $(DC_PATH)/dc_sample_event_notif/dc_sample_event_notif $(SAMPLE_BUILD_OUTPUT)/;


BUILD_SYN=algchaining_sample ccm_sample cipher_sample \
          gcm_sample hash_file_sample \
          hash_sample ipsec_sample \
          ssl_sample sym_dp_sample 

ifeq ($(SC_WITH_QAT22), 1)
BUILD_SYN += zuc_sample
endif


ifeq ($(HKDF_SUPPORTED), 0)
BUILD_SYN += hkdf_sample
endif

ifeq ($(ICP_OS),linux_2.6)
BUILD_SYN += update_sample
endif

ifeq ($(ICP_HB_FAIL_SIM),y)
BUILD_SYN += heartbeat_sample
endif

sym:$(BUILD_SYN)
	@echo ; echo "Build sym component $(BUILD_SYN)";
algchaining_sample: output_dir
	@cd $(SYM_PATH)/alg_chaining_sample && $(MAKE) clean && $(MAKE) ICP_OS_LEVEL=user_space
	@cp $(SYM_PATH)/alg_chaining_sample/algchaining_sample $(SAMPLE_BUILD_OUTPUT)/;
ifeq ($(ICP_OS_LEVEL),kernel_space)
	@cd $(SYM_PATH)/alg_chaining_sample && $(MAKE) ICP_OS_LEVEL=kernel_space
	@cp $(SYM_PATH)/alg_chaining_sample/algchaining_sample.ko $(SAMPLE_BUILD_OUTPUT)/;
endif

ifdef SC_WITH_QAT22
zuc_sample: output_dir
	@cd $(SYM_PATH)/zuc_sample && $(MAKE) clean && $(MAKE) ICP_OS_LEVEL=user_space
	@cp $(SYM_PATH)/zuc_sample/zuc_sample $(SAMPLE_BUILD_OUTPUT)/;
ifeq ($(ICP_OS_LEVEL),kernel_space)
	@cd $(SYM_PATH)/zuc_sample && $(MAKE) ICP_OS_LEVEL=kernel_space
	@cp $(SYM_PATH)/zuc_sample/zuc_sample.ko $(SAMPLE_BUILD_OUTPUT)/;
endif
endif


ccm_sample: output_dir
	@cd $(SYM_PATH)/ccm_sample && $(MAKE) clean && $(MAKE) ICP_OS_LEVEL=user_space
	@cp $(SYM_PATH)/ccm_sample/ccm_sample $(SAMPLE_BUILD_OUTPUT)/;
ifeq ($(ICP_OS_LEVEL),kernel_space)
	@cd $(SYM_PATH)/ccm_sample && $(MAKE) ICP_OS_LEVEL=kernel_space
	@cp $(SYM_PATH)/ccm_sample/ccm_sample.ko $(SAMPLE_BUILD_OUTPUT)/;
endif

cipher_sample: output_dir
	@cd $(SYM_PATH)/cipher_sample && $(MAKE) clean && $(MAKE) ICP_OS_LEVEL=user_space
	@cp $(SYM_PATH)/cipher_sample/cipher_sample $(SAMPLE_BUILD_OUTPUT)/;
ifeq ($(ICP_OS_LEVEL),kernel_space)
	@cd $(SYM_PATH)/cipher_sample && $(MAKE) ICP_OS_LEVEL=kernel_space
	@cp $(SYM_PATH)/cipher_sample/cipher_sample.ko $(SAMPLE_BUILD_OUTPUT)/;
endif

gcm_sample: output_dir
	@cd $(SYM_PATH)/gcm_sample && $(MAKE) clean && $(MAKE) ICP_OS_LEVEL=user_space
	@cp $(SYM_PATH)/gcm_sample/gcm_sample $(SAMPLE_BUILD_OUTPUT)/;
ifeq ($(ICP_OS_LEVEL),kernel_space)
	@cd $(SYM_PATH)/gcm_sample && $(MAKE) ICP_OS_LEVEL=kernel_space
	@cp $(SYM_PATH)/gcm_sample/gcm_sample.ko $(SAMPLE_BUILD_OUTPUT)/;
endif

hash_file_sample: output_dir
	@cd $(SYM_PATH)/hash_file_sample && $(MAKE) clean  ICP_OS_LEVEL=user_space&& $(MAKE) ICP_OS_LEVEL=user_space
	@cp $(SYM_PATH)/hash_file_sample/hash_file_sample $(SAMPLE_BUILD_OUTPUT)/;

hash_sample: output_dir
	@cd $(SYM_PATH)/hash_sample && $(MAKE) clean && $(MAKE) ICP_OS_LEVEL=user_space
	@cp $(SYM_PATH)/hash_sample/hash_sample $(SAMPLE_BUILD_OUTPUT)/;
ifeq ($(ICP_OS_LEVEL),kernel_space)
	@cd $(SYM_PATH)/hash_sample && $(MAKE) ICP_OS_LEVEL=kernel_space
	@cp $(SYM_PATH)/hash_sample/hash_sample.ko $(SAMPLE_BUILD_OUTPUT)/;
endif

hkdf_sample: output_dir
	@cd $(SYM_PATH)/hkdf_sample && $(MAKE) clean && $(MAKE) ICP_OS_LEVEL=user_space
	@cp $(SYM_PATH)/hkdf_sample/hkdf_sample $(SAMPLE_BUILD_OUTPUT)/;
ifeq ($(ICP_OS_LEVEL),kernel_space)
	@cd $(SYM_PATH)/hkdf_sample && $(MAKE) ICP_OS_LEVEL=kernel_space
	@cp $(SYM_PATH)/hkdf_sample/hkdf_sample.ko $(SAMPLE_BUILD_OUTPUT)/;
endif

heartbeat_sample: output_dir
	@cd $(SYM_PATH)/heartbeat_sample && $(MAKE) clean && $(MAKE) ICP_OS_LEVEL=user_space
	@cp $(SYM_PATH)/heartbeat_sample/heartbeat_sample $(SAMPLE_BUILD_OUTPUT)/;

ipsec_sample: output_dir
	@cd $(SYM_PATH)/ipsec_sample && $(MAKE) clean && $(MAKE) ICP_OS_LEVEL=user_space
	@cp $(SYM_PATH)/ipsec_sample/ipsec_sample $(SAMPLE_BUILD_OUTPUT)/;
ifeq ($(ICP_OS_LEVEL),kernel_space)
	@cd $(SYM_PATH)/ipsec_sample && $(MAKE) ICP_OS_LEVEL=kernel_space
	@cp $(SYM_PATH)/ipsec_sample/ipsec_sample.ko $(SAMPLE_BUILD_OUTPUT)/;
endif

ssl_sample: output_dir
	@cd $(SYM_PATH)/ssl_sample && $(MAKE) clean && $(MAKE) ICP_OS_LEVEL=user_space
	@cp $(SYM_PATH)/ssl_sample/ssl_sample $(SAMPLE_BUILD_OUTPUT)/;
ifeq ($(ICP_OS_LEVEL),kernel_space)
	@cd $(SYM_PATH)/ssl_sample && $(MAKE) ICP_OS_LEVEL=kernel_space
	@cp $(SYM_PATH)/ssl_sample/ssl_sample.ko $(SAMPLE_BUILD_OUTPUT)/;
endif

sym_dp_sample: output_dir
	@cd $(SYM_PATH)/symdp_sample && $(MAKE) clean && $(MAKE) ICP_OS_LEVEL=user_space
	@cp $(SYM_PATH)/symdp_sample/sym_dp_sample $(SAMPLE_BUILD_OUTPUT)/;
ifeq ($(ICP_OS_LEVEL),kernel_space)
	@cd $(SYM_PATH)/symdp_sample && $(MAKE) ICP_OS_LEVEL=kernel_space
	@cp $(SYM_PATH)/symdp_sample/sym_dp_sample.ko $(SAMPLE_BUILD_OUTPUT)/;
endif

update_sample: output_dir
	@cd $(SYM_PATH)/update_sample && $(MAKE) clean && $(MAKE) ICP_OS_LEVEL=user_space
	@cp $(SYM_PATH)/update_sample/sym_dp_update_sample $(SAMPLE_BUILD_OUTPUT)/;

BUILD_ASYM=diffie_hellman_sample prime_sample rsa_sample
ifeq ($(SC_KPT2_ENABLED),1)
BUILD_ASYM += kpt_rsa_sample
BUILD_ASYM += kpt_ecdsa_sample
endif

ifeq ($(MONTEDWDS_SUPPORTED), 0)
BUILD_ASYM += ec_montedwds_sample
BUILD_ASYM += eddsa_sample
endif

asym: $(BUILD_ASYM)
	@echo ; echo "Build asym component";

diffie_hellman_sample: output_dir
	@cd $(ASYM_PATH)/diffie_hellman_sample && $(MAKE) clean && $(MAKE) ICP_OS_LEVEL=user_space
	@cp $(ASYM_PATH)/diffie_hellman_sample/dh_sample $(SAMPLE_BUILD_OUTPUT)/;

prime_sample: output_dir
	@cd $(ASYM_PATH)/prime_sample && $(MAKE) clean && $(MAKE) ICP_OS_LEVEL=user_space
	@cp $(ASYM_PATH)/prime_sample/prime_sample $(SAMPLE_BUILD_OUTPUT)/;

ec_montedwds_sample: output_dir
	@cd $(ASYM_PATH)/ec_montedwds_sample && $(MAKE) clean && $(MAKE) ICP_OS_LEVEL=user_space
	@cp $(ASYM_PATH)/ec_montedwds_sample/ec_montedwds_sample $(SAMPLE_BUILD_OUTPUT)/;

eddsa_sample: output_dir
	@cd $(ASYM_PATH)/eddsa_sample && $(MAKE) clean && $(MAKE) ICP_OS_LEVEL=user_space
	@cp $(ASYM_PATH)/eddsa_sample/eddsa_sample $(SAMPLE_BUILD_OUTPUT)/;

kpt_rsa_sample: output_dir
	@cd $(ASYM_PATH)/kpt/kpt_rsa_sample && $(MAKE) clean && $(MAKE) ICP_OS_LEVEL=user_space
	@cp $(ASYM_PATH)/kpt/kpt_rsa_sample/kpt_rsa_sample $(SAMPLE_BUILD_OUTPUT)/;

kpt_ecdsa_sample: output_dir
	@cd $(ASYM_PATH)/kpt/kpt_ecdsa_sample && $(MAKE) clean && $(MAKE) ICP_OS_LEVEL=user_space
	@cp $(ASYM_PATH)/kpt/kpt_ecdsa_sample/kpt_ecdsa_sample $(SAMPLE_BUILD_OUTPUT)/;

rsa_sample: output_dir
	@cd $(ASYM_PATH)/rsa_sample && $(MAKE) clean && $(MAKE) ICP_OS_LEVEL=user_space
	@cp $(ASYM_PATH)/rsa_sample/rsa_sample $(SAMPLE_BUILD_OUTPUT)/;

CLEAN_DC=clean_dc_dp_sample clean_stateless_sample     \
	 clean_stateful_sample                         \
	 clean_stateless_multi_op_checksum_sample      \
	 clean_dc_sample_event_notif

ifeq ($(ICP_OS),linux_2.6)
CLEAN_DC += clean_dc_chaining_sample
endif

CLEAN_SYM=clean_algchaining_sample clean_ccm_sample    \
	  clean_cipher_sample                          \
	  clean_gcm_sample clean_hash_file_sample      \
	  clean_hash_sample clean_ipsec_sample         \
	  clean_ssl_sample                             \
	  clean_sym_dp_sample

ifeq ($(SC_WITH_QAT22),1)
CLEAN_SYM += clean_zuc_sample
endif


ifeq ($(HKDF_SUPPORTED), 0)
CLEAN_SYM += clean_hkdf_sample
endif

ifeq ($(ICP_OS),linux_2.6)
CLEAN_SYM += clean_update_sample
endif

ifeq ($(ICP_HB_FAIL_SIM),y)
CLEAN_SYM += clean_heartbeat_sample
endif

CLEAN_ASYM=clean_diffie_hellman_sample                 \
	   clean_prime_sample						   \
	   clean_rsa_sample
ifeq ($(SC_KPT2_ENABLED),1)
CLEAN_ASYM += clean_kpt_rsa_sample
CLEAN_ASYM += clean_kpt_ecdsa_sample
endif

ifeq ($(MONTEDWDS_SUPPORTED), 0)
CLEAN_ASYM += clean_ec_montedwds_sample
CLEAN_ASYM += clean_eddsa_sample
endif
clean_dc: $(CLEAN_DC)
	@echo ; echo "Build dc component.";
clean_sym: $(CLEAN_SYM)
	@echo ; echo "Build sym component.";
clean_asym: $(CLEAN_ASYM)
	@echo ; echo "Build asym component.";

clean_dc_dp_sample:
	@cd $(DC_PATH)/dc_dp_sample && $(MAKE) clean ICP_OS_LEVEL=user_space && \
	    $(MAKE) ICP_OS_LEVEL=kernel_space clean;
	$(RM) $(DC_PATH)/dc_dp_sample/dc_dp_sample
	$(RM) $(SAMPLE_BUILD_OUTPUT)/dc_dp_sample;
	$(RM) $(DC_PATH)/dc_dp_sample/dc_dp_sample.ko;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/dc_dp_sample.ko;

clean_stateless_sample:
	@cd $(DC_PATH)/stateless_sample && $(MAKE) clean ICP_OS_LEVEL=user_space && \
	    $(MAKE) ICP_OS_LEVEL=kernel_space clean;
	$(RM) $(DC_PATH)/stateless_sample/dc_stateless_sample.ko;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/dc_stateless_sample.ko;
	$(RM) $(DC_PATH)/stateless_sample/dc_stateless_sample;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/dc_stateless_sample;

clean_stateful_sample:
	@cd $(DC_PATH)/stateful_sample && $(MAKE) clean ICP_OS_LEVEL=user_space
	$(RM) $(DC_PATH)/stateful_sample/dc_stateful_sample;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/dc_stateful_sample;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/paper4;

clean_stateless_multi_op_checksum_sample:
	@cd $(DC_PATH)/stateless_multi_op_checksum_sample && \
	    $(MAKE) clean ICP_OS_LEVEL=user_space &&         \
	    $(MAKE) ICP_OS_LEVEL=kernel_space clean;
	$(RM) $(DC_PATH)/stateless_multi_op_checksum_sample/dc_stateless_multi_op_sample.ko;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/dc_stateless_multi_op_sample.ko;
	$(RM) $(DC_PATH)/stateless_multi_op_checksum_sample/dc_stateless_multi_op_sample;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/dc_stateless_multi_op_sample;


clean_algchaining_sample:
	@cd $(SYM_PATH)/alg_chaining_sample && \
	    $(MAKE) clean ICP_OS_LEVEL=user_space && \
	    $(MAKE) ICP_OS_LEVEL=kernel_space clean;
	$(RM) $(SYM_PATH)/alg_chaining_sample/algchaining_sample.ko;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/algchaining_sample.ko;
	$(RM) $(SYM_PATH)/alg_chaining_sample/algchaining_sample ;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/algchaining_sample;

clean_zuc_sample:
	@cd $(SYM_PATH)/zuc_sample && $(MAKE) clean ICP_OS_LEVEL=user_space && \
	    $(MAKE) ICP_OS_LEVEL=kernel_space clean;
	$(RM) $(SYM_PATH)/zuc_sample/zuc_sample.ko;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/zuc_sample.ko;
	$(RM) $(SYM_PATH)/zuc_sample/zuc_sample;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/zuc_sample;

clean_cmac_sample:
	@cd $(SYM_PATH)/cmac_sample && $(MAKE) clean ICP_OS_LEVEL=user_space;
	$(RM) $(SYM_PATH)/cmac_sample/cmac_sample;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/cmac_sample;

clean_ccm_sample:
	@cd $(SYM_PATH)/ccm_sample && $(MAKE) clean ICP_OS_LEVEL=user_space &&\
	    $(MAKE) ICP_OS_LEVEL=kernel_space clean;
	$(RM) $(SYM_PATH)/ccm_sample/ccm_sample.ko;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/ccm_sample.ko;
	$(RM) $(SYM_PATH)/ccm_sample/ccm_sample;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/ccm_sample;

clean_cipher_sample:
	@cd $(SYM_PATH)/cipher_sample && $(MAKE) clean ICP_OS_LEVEL=user_space &&\
	    $(MAKE) ICP_OS_LEVEL=kernel_space clean;
	$(RM) $(SYM_PATH)/cipher_sample/cipher_sample.ko;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/cipher_sample.ko;
	$(RM) $(SYM_PATH)/cipher_sample/cipher_sample;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/cipher_sample;

clean_gcm_sample:
	@cd $(SYM_PATH)/gcm_sample && $(MAKE) clean ICP_OS_LEVEL=user_space && \
	    $(MAKE) ICP_OS_LEVEL=kernel_space clean;
	$(RM) $(SYM_PATH)/gcm_sample/gcm_sample.ko;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/gcm_sample.ko;
	$(RM) $(SYM_PATH)/gcm_sample/gcm_sample;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/gcm_sample;

clean_hash_file_sample:
	@cd $(SYM_PATH)/hash_file_sample && $(MAKE) clean ICP_OS_LEVEL=user_space;
	$(RM) $(SYM_PATH)/hash_file_sample/hash_file_sample
	$(RM) $(SAMPLE_BUILD_OUTPUT)/hash_file_sample;

clean_hash_sample:
	@cd $(SYM_PATH)/hash_sample && $(MAKE) clean ICP_OS_LEVEL=user_space &&\
	    $(MAKE) ICP_OS_LEVEL=kernel_space clean
	$(RM) $(SYM_PATH)/hash_sample/hash_sample.ko;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/hash_sample.ko;
	$(RM) $(SYM_PATH)/hash_sample/hash_sample;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/hash_sample;

clean_heartbeat_sample:
	@cd $(SYM_PATH)/heartbeat_sample && $(MAKE) clean ICP_OS_LEVEL=user_space;
	$(RM) $(SYM_PATH)/heartbeat_sample/heartbeat_sample;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/heartbeat_sample;

clean_ipsec_sample:
	@cd $(SYM_PATH)/ipsec_sample && $(MAKE) clean ICP_OS_LEVEL=user_space &&\
	    $(MAKE) ICP_OS_LEVEL=kernel_space clean;
	$(RM) $(SYM_PATH)/ipsec_sample/ipsec_sample.ko;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/ipsec_sample.ko;
	$(RM) $(SYM_PATH)/ipsec_sample/ipsec_sample;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/ipsec_sample;

clean_ssl_sample:
	@cd $(SYM_PATH)/ssl_sample && $(MAKE) clean ICP_OS_LEVEL=user_space &&\
	    $(MAKE) ICP_OS_LEVEL=kernel_space clean;
	$(RM) $(SYM_PATH)/ssl_sample/ssl_sample.ko;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/ssl_sample.ko;
	$(RM) $(SYM_PATH)/ssl_sample/ssl_sample;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/ssl_sample;

clean_sym_dp_sample:
	@cd $(SYM_PATH)/symdp_sample && $(MAKE) clean ICP_OS_LEVEL=user_space &&\
	    $(MAKE) ICP_OS_LEVEL=kernel_space clean;
	$(RM) $(SYM_PATH)/symdp_sample/sym_dp_sample.ko;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/sym_dp_sample.ko;
	$(RM) $(SYM_PATH)/symdp_sample/sym_dp_sample;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/sym_dp_sample;

clean_update_sample:
	@cd $(SYM_PATH)/update_sample && $(MAKE) clean ICP_OS_LEVEL=user_space 
	$(RM) $(SYM_PATH)/update_sample/sym_dp_update_sample;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/sym_dp_update_sample;

clean_hkdf_sample:
	@cd $(SYM_PATH)/hkdf_sample && $(MAKE) clean ICP_OS_LEVEL=user_space && \
	    $(MAKE) ICP_OS_LEVEL=kernel_space clean;
	$(RM) $(SYM_PATH)/hkdf_sample/hkdf_sample;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/hkdf_sample;
	$(RM) $(SYM_PATH)/hkdf_sample/hkdf_sample.ko;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/hkdf_sample.ko;

clean_diffie_hellman_sample:
	@cd $(ASYM_PATH)/diffie_hellman_sample && $(MAKE) clean ICP_OS_LEVEL=user_space 
	$(RM) $(ASYM_PATH)/diffie_hellman_sample/dh_sample;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/dh_sample;

clean_kpt_rsa_sample:
	@cd $(ASYM_PATH)/kpt/kpt_rsa_sample && $(MAKE) clean ICP_OS_LEVEL=user_space
	$(RM) $(ASYM_PATH)/kpt/kpt_rsa_sample/kpt_rsa_sample;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/kpt_rsa_sample;

clean_kpt_ecdsa_sample:
	@cd $(ASYM_PATH)/kpt/kpt_ecdsa_sample && $(MAKE) clean ICP_OS_LEVEL=user_space
	$(RM) $(ASYM_PATH)/kpt/kpt_ecdsa_sample/kpt_ecdsa_sample;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/kpt_ecdsa_sample;

clean_rsa_sample:
	@cd $(ASYM_PATH)/rsa_sample && $(MAKE) clean ICP_OS_LEVEL=user_space
	$(RM) $(ASYM_PATH)/rsa_sample/rsa_sample;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/rsa_sample;

clean_prime_sample:
	@cd $(ASYM_PATH)/prime_sample && $(MAKE) clean ICP_OS_LEVEL=user_space 
	$(RM) $(ASYM_PATH)/prime_sample/prime_sample;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/prime_sample;

clean_ec_montedwds_sample:
	@cd $(ASYM_PATH)/ec_montedwds_sample && $(MAKE) clean ICP_OS_LEVEL=user_space &&\
	$(RM) $(ASYM_PATH)/ec_montedwds_sample/ec_montedwds_sample;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/ec_montedwds_sample;

clean_eddsa_sample:
	@cd $(ASYM_PATH)/eddsa_sample && $(MAKE) clean ICP_OS_LEVEL=user_space &&\
	$(RM) $(ASYM_PATH)/edsa_sample/eddsa_sample;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/eddsa_sample;

clean_dc_chaining_sample:
	@cd $(DC_PATH)/chaining_sample && $(MAKE) clean ICP_OS_LEVEL=user_space &&\
	    $(MAKE) ICP_OS_LEVEL=kernel_space clean;
	$(RM) $(DC_PATH)/chaining_sample/chaining_sample;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/chaining_sample;

clean_dc_sample_event_notif:
	@cd $(DC_PATH)/dc_sample_event_notif && $(MAKE) clean ICP_OS_LEVEL=user_space &&\
	    $(MAKE) ICP_OS_LEVEL=kernel_space clean;
	$(RM) $(DC_PATH)/dc_sample_event_notif/dc_sample_event_notif;
	$(RM) $(SAMPLE_BUILD_OUTPUT)/dc_sample_event_notif;

all: $(BUILD_FUNCIONAL_SAMPLE_CODE)

ifeq ($(ICP_OS_LEVEL),kernel_space)
.NOTPARALLEL:all
endif

.PHONY: clean
clean: $(CLEAN_ALL)
	@echo; echo "Cleaning functional sample code.";

