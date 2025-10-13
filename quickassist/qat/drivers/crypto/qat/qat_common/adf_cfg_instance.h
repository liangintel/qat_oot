/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2021 Intel Corporation */
#ifndef ADF_CFG_INSTANCE_H_
#define ADF_CFG_INSTANCE_H_

#include "adf_cfg_common.h"

struct adf_cfg_bundle;

struct adf_cfg_instance {
	enum adf_cfg_service_type stype;
	char name[ADF_CFG_MAX_STR_LEN];
	int polling_mode;
	cpumask_t affinity_mask;
	/* rings within an instance for services */
	int asym_tx;
	int asym_rx;
	int sym_tx;
	int sym_rx;
	int dc_tx;
	int dc_rx;
	int bundle;
};

void crypto_instance_init(struct adf_cfg_instance *instance,
			  struct adf_cfg_bundle *bundle);
void dc_instance_init(struct adf_cfg_instance *instance,
		      struct adf_cfg_bundle *bundle);
void asym_instance_init(struct adf_cfg_instance *instance,
			struct adf_cfg_bundle *bundle);
void sym_instance_init(struct adf_cfg_instance *instance,
		       struct adf_cfg_bundle *bundle);
#endif
