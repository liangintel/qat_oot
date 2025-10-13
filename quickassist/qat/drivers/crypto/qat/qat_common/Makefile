########################################################################
#  SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
#  Copyright(c) 2014 - 2022 Intel Corporation
########################################################################

obj-$(CONFIG_CRYPTO_DEV_QAT) += intel_qat.o
intel_qat-objs := adf_cfg.o \
	adf_isr.o \
	adf_ctl_drv.o \
	adf_dev_mgr.o \
	adf_dev_err.o \
	adf_init.o \
	adf_accel_engine.o \
	adf_aer.o \
	adf_transport.o \
	adf_admin.o \
	adf_hw_arbiter.o \
	qat_uclo.o \
	qat_hal.o \
	adf_cfg_dev_dbg.o \
	adf_pf2vf_dbg.o \
	adf_heartbeat.o \
	adf_heartbeat_dbg.o \
	adf_ver_dbg.o \
	adf_clock.o \
	adf_cnvnr_freq_counters.o \
	adf_gen2_hw_csr_data.o \
	adf_gen4_hw_csr_data.o \
	adf_gen4vf_hw_csr_data.o \
	adf_vqat_hw_csr_data.o \
	adf_adi.o \
	adf_gen4_adi_hal.o \
	adf_vdcm_iov_transport.o \
	adf_vdcm_iov_msg.o \
	adf_vqat_isr.o \
	adf_svm.o \
	adf_pasid.o \
	adf_sla.o

intel_qat-objs += adf_ctl_rl.o
intel_qat-objs += adf_rl_v2.o
intel_qat-objs += adf_gen4_hw_data.o
intel_qat-objs += adf_gen4_timer.o
intel_qat-objs += adf_gen4_ras.o
intel_qat-objs += adf_gen4_kpt.o
intel_qat-objs += adf_uq.o
intel_qat-objs += adf_gen4_pm.o
intel_qat-objs += adf_gen4_tl.o
intel_qat-objs += adf_rl.o

ifeq ($(QAT_NO_LKCF), y)
intel_qat-objs += qat_crypto_stubs.o
else
intel_qat-objs += qat_crypto.o \
	qat_algs.o \
	qat_asym_algs.o \
	adf_lkca.o
endif

intel_qat-$(CONFIG_DEBUG_FS) += adf_transport_debug.o
intel_qat-$(CONFIG_PCI_IOV) += adf_sriov.o adf_pf2vf_msg.o \
			       adf_vf2pf_msg.o adf_vf_isr.o \
			       adf_pf2vf_get_sla.o \
			       adf_pf2vf_ring_to_svc_map.o \
			       adf_pf2vf_capabilities.o

#Check kernel version and update sign if not defined yet
KERNELLESSTHAN4_13 := $(shell set -e;                                \
	if [ $(VERSION) -lt 4 ];                                     \
	then                                                         \
		echo TRUE;                                           \
	else                                                         \
		if [ $(VERSION) -eq 4 ] && [ $(PATCHLEVEL) -lt 13 ]; \
		then                                                 \
			echo TRUE;                                   \
		else                                                 \
			echo FALSE;                                  \
		fi;                                                  \
	fi)
ifeq ($(QAT_PKE_MAX_SZ_SIGN)$(QAT_PKE_MAX_SZ_UNSIGN),)
	ifeq ($(KERNELLESSTHAN4_13),TRUE)
		QAT_PKE_MAX_SZ_SIGN = y
		export QAT_PKE_MAX_SZ_SIGN
	else
		QAT_PKE_MAX_SZ_UNSIGN = y
		export QAT_PKE_MAX_SZ_UNSIGN
	endif
endif

ccflags-$(QAT_PKE_MAX_SZ_SIGN) += -DQAT_PKE_MAX_SZ_SIGN=""
ccflags-$(QAT_PKE_MAX_SZ_UNSIGN) += -DQAT_PKE_MAX_SZ_SIGN=unsigned
ccflags-$(QAT_INC_DMA_DIRECT) += -DQAT_INC_DMA_DIRECT=""
intel_qat-$(CONFIG_CRYPTO_DEV_QAT_VDCM) += adf_vdcm_core.o \
					   adf_vdcm_helper.o \
					   adf_vdcm_adi.o \
					   adf_vdcm_dbg.o
