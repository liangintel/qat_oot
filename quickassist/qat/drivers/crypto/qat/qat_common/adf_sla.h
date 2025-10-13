/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2020 Intel Corporation */
#ifndef ADF_SLA_H_
#define ADF_SLA_H_
#include <linux/list.h>
#include "adf_sla_user.h"

int adf_sla_create(struct adf_user_sla *sla);
int adf_sla_update(struct adf_user_sla *sla);
int adf_sla_delete(struct adf_user_sla *sla);
int adf_sla_get_caps(struct adf_user_sla_caps *sla_caps);
int adf_sla_get_list(struct adf_user_slas *slas);

#endif
