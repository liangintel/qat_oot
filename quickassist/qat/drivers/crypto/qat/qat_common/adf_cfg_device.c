// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2021 Intel Corporation */
#include <linux/pci.h>
#include "adf_cfg.h"
#include "adf_cfg_device.h"
#include "adf_cfg_section.h"

#define ADF_CFG_SVCS_MAX (25)
#define ADF_CFG_DEPRE_PARAMS_NUM (4)

#define ADF_CFG_CAP_DC ADF_ACCEL_CAPABILITIES_COMPRESSION
#define ADF_CFG_CAP_ASYM ADF_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC
#define ADF_CFG_CAP_SYM ADF_ACCEL_CAPABILITIES_CRYPTO_SYMMETRIC
#define ADF_CFG_CAP_CY (ADF_CFG_CAP_ASYM | ADF_CFG_CAP_SYM)
#define ADF_CFG_FW_CAP_HKDF ICP_ACCEL_CAPABILITIES_HKDF
#define ADF_CFG_FW_CAP_ECEDMONT ICP_ACCEL_CAPABILITIES_ECEDMONT
#define ADF_CFG_FW_CAP_EXT_ALGCHAIN ICP_ACCEL_CAPABILITIES_EXT_ALGCHAIN
#define ADF_CFG_FW_CAP_RL ICP_ACCEL_CAPABILITIES_RL
#define ADF_CFG_FW_CAP_CIPHER ICP_ACCEL_CAPABILITIES_CIPHER
#define ADF_CFG_FW_CAP_AUTHENTICATION ICP_ACCEL_CAPABILITIES_AUTHENTICATION

#define ADF_CFG_CY_RINGS \
	(CRYPTO | CRYPTO << ADF_CFG_SERV_RING_PAIR_1_SHIFT | \
	 CRYPTO << ADF_CFG_SERV_RING_PAIR_2_SHIFT | \
	 CRYPTO << ADF_CFG_SERV_RING_PAIR_3_SHIFT)

#define ADF_CFG_SYM_RINGS \
	(SYM | SYM << ADF_CFG_SERV_RING_PAIR_1_SHIFT | \
	 SYM << ADF_CFG_SERV_RING_PAIR_2_SHIFT | \
	 SYM << ADF_CFG_SERV_RING_PAIR_3_SHIFT)

#define ADF_CFG_ASYM_RINGS \
	(ASYM | ASYM << ADF_CFG_SERV_RING_PAIR_1_SHIFT | \
	 ASYM << ADF_CFG_SERV_RING_PAIR_2_SHIFT | \
	 ASYM << ADF_CFG_SERV_RING_PAIR_3_SHIFT)

#define ADF_CFG_CY_DC_RINGS \
	(CRYPTO | CRYPTO << ADF_CFG_SERV_RING_PAIR_1_SHIFT | \
	 NA << ADF_CFG_SERV_RING_PAIR_2_SHIFT | \
	 COMP << ADF_CFG_SERV_RING_PAIR_3_SHIFT)

#define ADF_CFG_ASYM_DC_RINGS \
	(ASYM | ASYM << ADF_CFG_SERV_RING_PAIR_1_SHIFT | \
	 COMP << ADF_CFG_SERV_RING_PAIR_2_SHIFT | \
	 COMP << ADF_CFG_SERV_RING_PAIR_3_SHIFT)

#define ADF_CFG_SYM_DC_RINGS \
	(SYM | SYM << ADF_CFG_SERV_RING_PAIR_1_SHIFT | \
	 COMP << ADF_CFG_SERV_RING_PAIR_2_SHIFT | \
	 COMP << ADF_CFG_SERV_RING_PAIR_3_SHIFT)

#define ADF_CFG_DC_RINGS \
	(COMP | COMP << ADF_CFG_SERV_RING_PAIR_1_SHIFT | \
	 COMP << ADF_CFG_SERV_RING_PAIR_2_SHIFT | \
	 COMP << ADF_CFG_SERV_RING_PAIR_3_SHIFT)

enum icp_qat_capabilities_mask {
	ICP_ACCEL_CAPABILITIES_CRYPTO_SYMMETRIC = BIT(0),
	ICP_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC = BIT(1),
	ICP_ACCEL_CAPABILITIES_CIPHER = BIT(2),
	ICP_ACCEL_CAPABILITIES_AUTHENTICATION = BIT(3),
	ICP_ACCEL_CAPABILITIES_RESERVED_1 = BIT(4),
	ICP_ACCEL_CAPABILITIES_COMPRESSION = BIT(5),
	ICP_ACCEL_CAPABILITIES_DEPRECATED = BIT(6),
	ICP_ACCEL_CAPABILITIES_RAND = BIT(7),
	ICP_ACCEL_CAPABILITIES_ZUC = BIT(8),
	ICP_ACCEL_CAPABILITIES_SHA3 = BIT(9),
	ICP_ACCEL_CAPABILITIES_KPT = BIT(10),
	ICP_ACCEL_CAPABILITIES_RL = BIT(11),
	ICP_ACCEL_CAPABILITIES_HKDF = BIT(12),
	ICP_ACCEL_CAPABILITIES_ECEDMONT = BIT(13),
	ICP_ACCEL_CAPABILITIES_EXT_ALGCHAIN = BIT(14),
	ICP_ACCEL_CAPABILITIES_SHA3_EXT = BIT(15),
	ICP_ACCEL_CAPABILITIES_AESGCM_SPC = BIT(16),
	ICP_ACCEL_CAPABILITIES_CHACHA_POLY = BIT(17),
	ICP_ACCEL_CAPABILITIES_SM2 = BIT(18),
	ICP_ACCEL_CAPABILITIES_SM3 = BIT(19),
	ICP_ACCEL_CAPABILITIES_SM4 = BIT(20),
	ICP_ACCEL_CAPABILITIES_INLINE = BIT(21),
	ICP_ACCEL_CAPABILITIES_CNV_INTEGRITY = BIT(22),
	ICP_ACCEL_CAPABILITIES_CNV_INTEGRITY64 = BIT(23),
	ICP_ACCEL_CAPABILITIES_LZ4_COMPRESSION = BIT(24),
	ICP_ACCEL_CAPABILITIES_LZ4S_COMPRESSION = BIT(25),
	ICP_ACCEL_CAPABILITIES_AES_V2 = BIT(26),
	ICP_ACCEL_CAPABILITIES_KPT2 = BIT(27),
	// Reserved capabilities BIT(28) for CIPHER_CRC
	ICP_ACCEL_CAPABILITIES_ZUC_256 = BIT(29),
	ICP_ACCEL_CAPABILITIES_WIRELESS_CRYPTO_EXT = BIT(30),
	ICP_ACCEL_CAPABILITIES_AUX = BIT(31)
};

struct adf_cfg_enabled_services {
	const char svcs_enabled[ADF_CFG_MAX_VAL_LEN_IN_BYTES];
	u16 rng_to_svc_msk;
	u32 enabled_svc_cap;
	u32 enabled_fw_cap;
};

static char adf_cfg_deprecated_params[][ADF_CFG_MAX_KEY_LEN_IN_BYTES] = {
	ADF_DEV_KPT_ENABLE,
	ADF_STORAGE_FIRMWARE_ENABLED,
	ADF_RL_FIRMWARE_ENABLED,
	ADF_PKE_DISABLED
};

struct adf_cfg_profile {
	enum adf_cfg_fw_image_type fw_image_type;
	struct adf_cfg_enabled_services supported_svcs[ADF_CFG_SVCS_MAX];
};

static struct adf_cfg_profile adf_profiles[] = {
	{ADF_FW_IMAGE_DEFAULT,
		{
			{"cy", ADF_CFG_CY_RINGS, ADF_CFG_CAP_CY,
				ADF_CFG_FW_CAP_CIPHER |
				ADF_CFG_FW_CAP_AUTHENTICATION |
				ADF_CFG_FW_CAP_ECEDMONT |
				ADF_CFG_FW_CAP_EXT_ALGCHAIN},
			{"dc", ADF_CFG_DC_RINGS, ADF_CFG_CAP_DC, 0},
			{"sym", ADF_CFG_SYM_RINGS, ADF_CFG_CAP_SYM,
				ADF_CFG_FW_CAP_CIPHER |
				ADF_CFG_FW_CAP_AUTHENTICATION |
				ADF_CFG_FW_CAP_EXT_ALGCHAIN},
			{"asym", ADF_CFG_ASYM_RINGS, ADF_CFG_CAP_ASYM,
				ADF_CFG_FW_CAP_ECEDMONT},
			{"cy;dc", ADF_CFG_CY_DC_RINGS,
				ADF_CFG_CAP_CY | ADF_CFG_CAP_DC,
				ADF_CFG_FW_CAP_CIPHER |
				ADF_CFG_FW_CAP_AUTHENTICATION |
				ADF_CFG_FW_CAP_ECEDMONT |
				ADF_CFG_FW_CAP_EXT_ALGCHAIN},
			{"dc;cy", ADF_CFG_CY_DC_RINGS,
				ADF_CFG_CAP_CY | ADF_CFG_CAP_DC,
				ADF_CFG_FW_CAP_CIPHER |
				ADF_CFG_FW_CAP_AUTHENTICATION |
				ADF_CFG_FW_CAP_ECEDMONT |
				ADF_CFG_FW_CAP_EXT_ALGCHAIN},
			{"asym;dc", ADF_CFG_ASYM_DC_RINGS,
				ADF_CFG_CAP_ASYM | ADF_CFG_CAP_DC,
				ADF_CFG_FW_CAP_ECEDMONT},
			{"dc;asym", ADF_CFG_ASYM_DC_RINGS,
				ADF_CFG_CAP_ASYM | ADF_CFG_CAP_DC,
				ADF_CFG_FW_CAP_ECEDMONT},
			{"sym;dc", ADF_CFG_SYM_DC_RINGS,
				ADF_CFG_CAP_SYM | ADF_CFG_CAP_DC,
				ADF_CFG_FW_CAP_CIPHER |
				ADF_CFG_FW_CAP_AUTHENTICATION |
				ADF_CFG_FW_CAP_EXT_ALGCHAIN},
			{"dc;sym", ADF_CFG_SYM_DC_RINGS,
				ADF_CFG_CAP_SYM | ADF_CFG_CAP_DC,
				ADF_CFG_FW_CAP_CIPHER |
				ADF_CFG_FW_CAP_AUTHENTICATION |
				ADF_CFG_FW_CAP_EXT_ALGCHAIN},
		}
	},
	{ADF_FW_IMAGE_CRYPTO,
		{
			{"cy", ADF_CFG_CY_RINGS, ADF_CFG_CAP_CY,
				ADF_CFG_FW_CAP_HKDF |
				ADF_CFG_FW_CAP_CIPHER |
				ADF_CFG_FW_CAP_AUTHENTICATION |
				ADF_CFG_FW_CAP_ECEDMONT |
				ADF_CFG_FW_CAP_EXT_ALGCHAIN},
			{"sym", ADF_CFG_SYM_RINGS, ADF_CFG_CAP_SYM,
				ADF_CFG_FW_CAP_HKDF |
				ADF_CFG_FW_CAP_CIPHER |
				ADF_CFG_FW_CAP_AUTHENTICATION |
				ADF_CFG_FW_CAP_EXT_ALGCHAIN},
			{"asym", ADF_CFG_ASYM_RINGS, ADF_CFG_CAP_ASYM,
				ADF_CFG_FW_CAP_ECEDMONT},
		}
	},
	{ADF_FW_IMAGE_COMPRESSION,
		{
			{"dc", ADF_CFG_DC_RINGS, ADF_CFG_CAP_DC, 0},
			{"sym", ADF_CFG_SYM_RINGS, ADF_CFG_CAP_SYM,
				ADF_CFG_FW_CAP_AUTHENTICATION},
			{"sym;dc", ADF_CFG_SYM_DC_RINGS,
				ADF_CFG_CAP_SYM | ADF_CFG_CAP_DC,
				ADF_CFG_FW_CAP_AUTHENTICATION},
			{"dc;sym", ADF_CFG_SYM_DC_RINGS,
				ADF_CFG_CAP_SYM | ADF_CFG_CAP_DC,
				ADF_CFG_FW_CAP_AUTHENTICATION},
		}
	},
	{ADF_FW_IMAGE_CUSTOM1,
		{
			{"cy", ADF_CFG_CY_RINGS, ADF_CFG_CAP_CY,
				ADF_CFG_FW_CAP_HKDF |
				ADF_CFG_FW_CAP_CIPHER |
				ADF_CFG_FW_CAP_AUTHENTICATION |
				ADF_CFG_FW_CAP_ECEDMONT |
				ADF_CFG_FW_CAP_EXT_ALGCHAIN},
			{"dc", ADF_CFG_DC_RINGS, ADF_CFG_CAP_DC, 0},
			{"sym", ADF_CFG_SYM_RINGS, ADF_CFG_CAP_SYM,
				ADF_CFG_FW_CAP_HKDF |
				ADF_CFG_FW_CAP_CIPHER |
				ADF_CFG_FW_CAP_AUTHENTICATION |
				ADF_CFG_FW_CAP_EXT_ALGCHAIN},
			{"asym", ADF_CFG_ASYM_RINGS, ADF_CFG_CAP_ASYM,
				ADF_CFG_FW_CAP_ECEDMONT},
			{"cy;dc", ADF_CFG_CY_DC_RINGS,
				ADF_CFG_CAP_CY | ADF_CFG_CAP_DC,
				ADF_CFG_FW_CAP_HKDF |
				ADF_CFG_FW_CAP_CIPHER |
				ADF_CFG_FW_CAP_AUTHENTICATION |
				ADF_CFG_FW_CAP_ECEDMONT |
				ADF_CFG_FW_CAP_EXT_ALGCHAIN},
			{"dc;cy", ADF_CFG_CY_DC_RINGS,
				ADF_CFG_CAP_CY | ADF_CFG_CAP_DC,
				ADF_CFG_FW_CAP_HKDF |
				ADF_CFG_FW_CAP_CIPHER |
				ADF_CFG_FW_CAP_AUTHENTICATION |
				ADF_CFG_FW_CAP_ECEDMONT |
				ADF_CFG_FW_CAP_EXT_ALGCHAIN},
			{"asym;dc", ADF_CFG_ASYM_DC_RINGS,
				ADF_CFG_CAP_ASYM | ADF_CFG_CAP_DC,
				ADF_CFG_FW_CAP_ECEDMONT},
			{"dc;asym", ADF_CFG_ASYM_DC_RINGS,
				ADF_CFG_CAP_ASYM | ADF_CFG_CAP_DC,
				ADF_CFG_FW_CAP_ECEDMONT},
			{"sym;dc", ADF_CFG_SYM_DC_RINGS,
				ADF_CFG_CAP_SYM | ADF_CFG_CAP_DC,
				ADF_CFG_FW_CAP_HKDF |
				ADF_CFG_FW_CAP_CIPHER |
				ADF_CFG_FW_CAP_AUTHENTICATION |
				ADF_CFG_FW_CAP_EXT_ALGCHAIN},
			{"dc;sym", ADF_CFG_SYM_DC_RINGS,
				ADF_CFG_CAP_SYM | ADF_CFG_CAP_DC,
				ADF_CFG_FW_CAP_HKDF |
				ADF_CFG_FW_CAP_CIPHER |
				ADF_CFG_FW_CAP_AUTHENTICATION |
				ADF_CFG_FW_CAP_EXT_ALGCHAIN},
		}
	}
};

int adf_cfg_get_ring_pairs(struct adf_cfg_device *device,
			   struct adf_cfg_instance *inst,
			   const char *process_name,
			   struct adf_accel_dev *accel_dev)
{
	int i = 0;
	int ret = -EFAULT;
	struct adf_cfg_instance *free_inst = NULL;
	enum adf_bundle_type free_bundle_type;

	dev_dbg(&GET_DEV(accel_dev),
		"get ring pair for section %s, bundle_num is %d.\n",
				process_name, device->bundle_num);

	if (strcmp(ADF_KERNEL_SEC, process_name) &&
	    inst->polling_mode == ADF_CFG_RESP_POLL) {
		for (i = 0; i < device->bundle_num; i++) {
			free_inst =
				adf_cfg_get_free_instance(device,
							  device->bundles[i],
							  inst,
							  process_name);

			if (!free_inst)
				continue;

			ret = adf_cfg_get_ring_pairs_from_bundle(
					device->bundles[i], inst,
					process_name, free_inst, device);
			return ret;
		}
	} else {
		if (!strcmp(ADF_KERNEL_SEC, process_name))
			free_bundle_type = KERNEL;
		else
			free_bundle_type = USER;

		for (i = 0; i < device->bundle_num; i++) {
			if (free_bundle_type == device->bundles[i]->type &&
			    cpumask_equal(&inst->affinity_mask,
					  &device->bundles[i]->affinity_mask)) {
				free_inst =
					adf_cfg_get_free_instance(
							device,
							device->bundles[i],
							inst,
							process_name);

				if (!free_inst)
					continue;

				ret = adf_cfg_get_ring_pairs_from_bundle(
							device->bundles[i],
							inst,
							process_name,
							free_inst, device);

				return ret;

			}
		}
		for (i = 0; i < device->bundle_num; i++) {
			if (adf_cfg_is_free(device->bundles[i])) {
				free_inst =
					adf_cfg_get_free_instance
							(device,
							device->bundles[i],
							inst,
							process_name);
				if (!free_inst)
					continue;

				ret = adf_cfg_get_ring_pairs_from_bundle
						(device->bundles[i],
						inst,
						process_name,
						free_inst,
						device);
				return ret;
			}

		}
	}
	pr_err("Don't have enough rings for instance %s in process %s\n",
	       inst->name, process_name);

	return ret;
}

static int adf_cfg_get_token_val_str(struct adf_accel_dev *accel_dev,
				     char *val, char *tokens[], int *token_num)
{
	char *ptr = val;
	char *token = NULL;
	char *str = NULL;
	int i = 0;

	for (i = 0; i < ADF_CFG_MAX_TOKENS_IN_CONFIG; i++) {
		token = kzalloc(ADF_CFG_MAX_KEY_LEN_IN_BYTES, GFP_KERNEL);
		if (!token)
			return -ENOMEM;
		str = token;
		while ('\0' != *ptr) {
			if (';' == *ptr) {
				*token = '\0';
				tokens[i] = str;
				ptr++;
				break;
			}
			*token = *ptr;
			token++;
			ptr++;
		}
		if ('\0' == *ptr) {
			*token = '\0';
			tokens[i] = str;
			break;
		}
	}
	*token_num = i + 1;
	dev_dbg(&GET_DEV(accel_dev),
		"ptr is %s, tokens[0] %s, tokens[1] %s, token_num %d.\n",
		val, tokens[0], tokens[1], *token_num);
	return 0;
}

static int adf_cfg_get_user_section(struct adf_accel_dev *accel_dev,
				    char **user_sec_list, int *user_sec_num)
{
	struct adf_cfg_device_data *cfg = accel_dev->cfg;
	struct list_head *list = NULL;
	struct adf_cfg_section *section = NULL;
	char *user_sec = NULL;

	list_for_each(list, &cfg->sec_list) {
		section = list_entry(list, struct adf_cfg_section, list);
		if (strcmp(section->name, ADF_GENERAL_SEC) &&
		    strcmp(section->name, ADF_KERNEL_SEC) &&
		    strcmp(section->name, ADF_INLINE_SEC) &&
		    strcmp(section->name, ADF_ACCEL_SEC)) {
			user_sec = kzalloc(ADF_CFG_MAX_SECTION_LEN_IN_BYTES,
					   GFP_KERNEL);
			if (!user_sec)
				return -ENOMEM;

			dev_dbg(&GET_DEV(accel_dev),
				"user section %s\n", section->name);
			strscpy(user_sec,
				section->name,
				ADF_CFG_MAX_SECTION_LEN_IN_BYTES);
			user_sec_list[*user_sec_num] = user_sec;
			(*user_sec_num) += 1;
		}
	}

	return 0;
}

static int adf_cfg_get_def_serv_mask(struct adf_accel_dev *accel_dev,
				     u16 *def_serv_mask)
{
	(*def_serv_mask) = CRYPTO;
	(*def_serv_mask) |= CRYPTO << ADF_CFG_SERV_RING_PAIR_1_SHIFT;
	(*def_serv_mask) |= NA << ADF_CFG_SERV_RING_PAIR_2_SHIFT;
	(*def_serv_mask) |= COMP << ADF_CFG_SERV_RING_PAIR_3_SHIFT;

	return 0;
}

static int adf_cfg_get_serv_ena_mask(struct adf_accel_dev *accel_dev,
				     char *tokens[], int token_num,
				     u16 num_cy_inst, u16 num_dc_inst,
				     u16 *serv_ena_mask)
{
	int t_ind = 0;
	u8 col_cy_inst = 0;
	u8 col_dc_inst = 0;
	u16 bundle_num = accel_dev->hw_device->num_banks;

	*serv_ena_mask = 0;

	col_cy_inst =
		num_cy_inst ? ((num_cy_inst - 1) / (2 * bundle_num) + 1) : 0;
	col_dc_inst =
		num_dc_inst ? ((num_dc_inst - 1) / (2 * bundle_num) + 1) : 0;

	for (t_ind = 0; t_ind < token_num; t_ind++) {
		if (strncmp(tokens[t_ind],
			    ADF_CFG_CY,
			    strlen(ADF_CFG_CY)) == 0) {
			if (col_cy_inst > 0 &&
			    (col_cy_inst + col_dc_inst > 3)) {
				dev_err(&GET_DEV(accel_dev),
					"number of inst overflow cy %d, dc %d.\n",
					num_cy_inst, num_dc_inst);
				goto failed;
			}
			switch (col_cy_inst) {
			case 0:
				break;
			case 1:
				*serv_ena_mask |= CRYPTO;
				*serv_ena_mask |=
				CRYPTO << ADF_CFG_SERV_RING_PAIR_1_SHIFT;
				break;
			case 2:
				*serv_ena_mask |= CRYPTO;
				*serv_ena_mask |=
				CRYPTO << ADF_CFG_SERV_RING_PAIR_1_SHIFT;
				*serv_ena_mask |=
				CRYPTO << ADF_CFG_SERV_RING_PAIR_2_SHIFT;
				*serv_ena_mask |=
				CRYPTO << ADF_CFG_SERV_RING_PAIR_3_SHIFT;
				break;
			default:
				dev_err(&GET_DEV(accel_dev),
					"number of cy inst overflow %d.\n",
					num_cy_inst);
				goto failed;
			}
		} else if (strncmp(tokens[t_ind],
				   ADF_CFG_DC,
				   strlen(ADF_CFG_DC)) == 0) {
			switch (col_dc_inst) {
			case 0:
				break;
			case 1:
				*serv_ena_mask |=
					COMP << ADF_CFG_SERV_RING_PAIR_3_SHIFT;
				break;
			case 2:
				*serv_ena_mask |=
					COMP << ADF_CFG_SERV_RING_PAIR_3_SHIFT;
				*serv_ena_mask |=
					COMP << ADF_CFG_SERV_RING_PAIR_2_SHIFT;
				break;
			case 3:
				*serv_ena_mask |=
					COMP << ADF_CFG_SERV_RING_PAIR_3_SHIFT;
				*serv_ena_mask |=
					COMP << ADF_CFG_SERV_RING_PAIR_2_SHIFT;
				*serv_ena_mask |=
					COMP << ADF_CFG_SERV_RING_PAIR_1_SHIFT;
				break;
			case 4:
				*serv_ena_mask |=
					COMP << ADF_CFG_SERV_RING_PAIR_3_SHIFT;
				*serv_ena_mask |=
					COMP << ADF_CFG_SERV_RING_PAIR_2_SHIFT;
				*serv_ena_mask |=
					COMP << ADF_CFG_SERV_RING_PAIR_1_SHIFT;
				*serv_ena_mask |= COMP;
				break;
			default:
				dev_err(&GET_DEV(accel_dev),
					"number of dc inst overflow %d.\n",
					num_dc_inst);
				goto failed;
			}
		} else if (strncmp(tokens[t_ind],
				   ADF_CFG_ASYM,
				   strlen(ADF_CFG_ASYM)) == 0) {
			if (col_cy_inst > 0 &&
			    (col_cy_inst + col_dc_inst > 4)) {
				dev_err(&GET_DEV(accel_dev),
					"number of inst overflow asym %d, dc %d.\n",
					num_cy_inst, num_dc_inst);
				goto failed;
			}
			switch (col_cy_inst) {
			case 0:
				break;
			case 1:
				*serv_ena_mask |= ASYM;
				if (col_dc_inst == 3)
					*serv_ena_mask |=
					COMP << ADF_CFG_SERV_RING_PAIR_1_SHIFT;
				break;
			case 2:
				*serv_ena_mask |= ASYM;
				*serv_ena_mask |=
					ASYM << ADF_CFG_SERV_RING_PAIR_1_SHIFT;
				break;
			case 3:
				*serv_ena_mask |= ASYM;
				*serv_ena_mask |=
					ASYM << ADF_CFG_SERV_RING_PAIR_1_SHIFT;
				*serv_ena_mask |=
					ASYM << ADF_CFG_SERV_RING_PAIR_2_SHIFT;
				break;
			case 4:
				*serv_ena_mask |= ASYM;
				*serv_ena_mask |=
					ASYM << ADF_CFG_SERV_RING_PAIR_1_SHIFT;
				*serv_ena_mask |=
					ASYM << ADF_CFG_SERV_RING_PAIR_2_SHIFT;
				*serv_ena_mask |=
					ASYM << ADF_CFG_SERV_RING_PAIR_3_SHIFT;
				break;
			default:
				dev_err(&GET_DEV(accel_dev),
					"number of asym inst overflow %d.\n",
					num_cy_inst);
				goto failed;
			}
		} else if (strncmp(tokens[t_ind],
				   ADF_CFG_SYM,
				   strlen(ADF_CFG_SYM)) == 0) {
			if (col_cy_inst > 0 &&
			    (col_cy_inst + col_dc_inst) > 4) {
				dev_err(&GET_DEV(accel_dev),
					"number of inst overflow sym %d, dc %d.\n",
					num_cy_inst, num_dc_inst);
				goto failed;
			}
			switch (col_cy_inst) {
			case 0:
				break;
			case 1:
				*serv_ena_mask |=
					SYM << ADF_CFG_SERV_RING_PAIR_1_SHIFT;
				if (col_dc_inst == 3)
					*serv_ena_mask |= COMP;
				break;
			case 2:
				*serv_ena_mask |=
					SYM << ADF_CFG_SERV_RING_PAIR_1_SHIFT;
				*serv_ena_mask |= SYM;
				break;
			case 3:
				*serv_ena_mask |=
					SYM << ADF_CFG_SERV_RING_PAIR_1_SHIFT;
				*serv_ena_mask |= SYM;
				*serv_ena_mask |=
					SYM << ADF_CFG_SERV_RING_PAIR_2_SHIFT;
				break;
			case 4:
				*serv_ena_mask |=
					SYM << ADF_CFG_SERV_RING_PAIR_1_SHIFT;
				*serv_ena_mask |= SYM;
				*serv_ena_mask |=
					SYM << ADF_CFG_SERV_RING_PAIR_2_SHIFT;
				*serv_ena_mask |=
					SYM << ADF_CFG_SERV_RING_PAIR_3_SHIFT;
				break;
			default:
				dev_err(&GET_DEV(accel_dev),
					"number of sym inst overflow %d.\n",
					num_cy_inst);
				goto failed;
			}
		} else {
			dev_err(&GET_DEV(accel_dev),
				"Unknown token %s to ServicesEnabled variables.\n",
				tokens[t_ind]);
			goto failed;
		}
	}
	return 0;

failed:
	return -EFAULT;
}

static int adf_cfg_chk_serv_ena_tokens(struct adf_accel_dev *accel_dev,
				       char *tokens[], int token_num)
{
	int i = 0;
	bool cy_enabled = false;
	bool dc_enabled = false;
	bool asym_enabled = false;
	bool sym_enabled = false;
	u32 capabilities = GET_HW_DATA(accel_dev)->accel_capabilities_mask;

	for (i = 0; i < token_num; i++) {
		if (strncmp(tokens[i],
			    ADF_CFG_CY,
			    strlen(ADF_CFG_CY)) == 0) {
			cy_enabled = true;
			continue;
		}
		if (strncmp(tokens[i],
			    ADF_CFG_DC,
			    strlen(ADF_CFG_DC)) == 0) {
			dc_enabled = true;
			continue;
		}
		if (strncmp(tokens[i],
			    ADF_CFG_ASYM,
			    strlen(ADF_CFG_ASYM)) == 0) {
			asym_enabled = true;
			continue;
		}
		if (strncmp(tokens[i],
			    ADF_CFG_SYM,
			    strlen(ADF_CFG_SYM)) == 0)
			sym_enabled = true;
	}

	dev_dbg(&GET_DEV(accel_dev),
		"ServicesEnabled %s%s%s%s.\n",
		cy_enabled ? "cy," : "",
		dc_enabled ? "dc," : "",
		asym_enabled ? "asym," : "",
		sym_enabled ? "sym," : "");

	if (!(capabilities & ADF_ACCEL_CAPABILITIES_CRYPTO_SYMMETRIC &&
	      capabilities & ADF_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC) &&
	    cy_enabled) {
		dev_err(&GET_DEV(accel_dev),
			"Device does not support cy service\n");
		goto failed;
	}
	if (!(capabilities & ADF_ACCEL_CAPABILITIES_CRYPTO_SYMMETRIC) &&
	    sym_enabled) {
		dev_err(&GET_DEV(accel_dev),
			"Device does not support sym service\n");
		goto failed;
	}
	if (!(capabilities & ADF_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC) &&
	    asym_enabled) {
		dev_err(&GET_DEV(accel_dev),
			"Device does not support asym service\n");
		goto failed;
	}
	if (!(capabilities & ADF_ACCEL_CAPABILITIES_COMPRESSION) &&
	    dc_enabled) {
		dev_err(&GET_DEV(accel_dev),
			"Device does not support dc service\n");
		goto failed;
	}
	if (cy_enabled && asym_enabled) {
		dev_err(&GET_DEV(accel_dev),
			"ServicesEnabled var cy and asym cannot co-exist.\n");
		goto failed;
	}
	if (cy_enabled && sym_enabled) {
		dev_err(&GET_DEV(accel_dev),
			"ServicesEnabled var cy and sym cannot co-exist.\n");
		goto failed;
	}
	if (asym_enabled && sym_enabled) {
		dev_err(&GET_DEV(accel_dev),
			"ServicesEnabled var asym and sym cannot co-exist.\n");
		goto failed;
	}

	return 0;

failed:
	return -EFAULT;
}

int adf_cfg_get_services_enabled(struct adf_accel_dev *accel_dev,
				 u16 *serv_ena_mask)
{
	char *key = NULL;
	char *val = NULL;
	int token_num = 0;
	char *tokens[ADF_CFG_MAX_NUM_OF_TOKENS];
	u16 num_intr_inst = 0;
	u16 num_cy_inst = 0;
	u16 num_dc_inst = 0;
	u16 bundle_num = accel_dev->hw_device->num_banks;
	int ret = -ENOMEM;
	int i = 0;

	*serv_ena_mask = 0;

	key = kzalloc(ADF_CFG_MAX_KEY_LEN_IN_BYTES, GFP_KERNEL);
	if (!key)
		goto failed;

	val = kzalloc(ADF_CFG_MAX_VAL_LEN_IN_BYTES, GFP_KERNEL);
	if (!val)
		goto failed;

	strscpy(key, ADF_SERVICES_ENABLED, ADF_CFG_MAX_VAL_LEN_IN_BYTES);
	if (adf_cfg_get_param_value(accel_dev, ADF_GENERAL_SEC, key, val))
		goto failed;

	dev_dbg(&GET_DEV(accel_dev), "services enabled string is %s.\n", val);

	ret = adf_cfg_get_token_val_str(accel_dev, val, tokens, &token_num);
	if (ret)
		goto failed;

	ret = adf_cfg_chk_serv_ena_tokens(accel_dev, tokens, token_num);
	if (ret)
		goto failed;

	if (accel_dev->is_vf) {
		adf_cfg_get_def_serv_mask(accel_dev, serv_ena_mask);
		dev_dbg(&GET_DEV(accel_dev),
			"using default service mask 0x%x due to virtualization.\n",
			*serv_ena_mask);
		ret = 0;
		goto failed;
	}

	ret = -EFAULT;
	strscpy(key, ADF_SERVICES_ENABLED, ADF_CFG_MAX_VAL_LEN_IN_BYTES);
	if (adf_cfg_get_param_value(accel_dev, ADF_GENERAL_SEC, key, val))
		goto failed;

	dev_dbg(&GET_DEV(accel_dev), "services enabled string is %s.\n", val);

	ret = adf_cfg_get_intr_inst(accel_dev, &num_intr_inst);
	if (ret)
		goto failed;

	dev_dbg(&GET_DEV(accel_dev),
		"number of interrupt instances %d.\n",
		num_intr_inst);

	if (num_intr_inst > 0) {
		adf_cfg_get_def_serv_mask(accel_dev, serv_ena_mask);
		dev_dbg(&GET_DEV(accel_dev),
			"using default service mask 0x%x due to interrupt/epoll mode instances.\n",
			*serv_ena_mask);
		ret = 0;
		goto failed;
	}

	ret = adf_cfg_get_num_of_inst(accel_dev, &num_cy_inst, &num_dc_inst);
	if (ret)
		goto failed;

	dev_dbg(&GET_DEV(accel_dev),
		"number of cy inst %d, number of dc inst %d.\n",
		num_cy_inst, num_dc_inst);

	if (num_intr_inst > 0 ||
	    (num_cy_inst <= 2 * bundle_num && num_dc_inst <= 2 * bundle_num)) {
		adf_cfg_get_def_serv_mask(accel_dev, serv_ena_mask);
		dev_dbg(&GET_DEV(accel_dev),
			"using default service mask 0x%x due to number of instances.\n",
			*serv_ena_mask);
		ret = 0;
		goto failed;
	}

	ret = adf_cfg_get_serv_ena_mask(accel_dev,
					tokens,
					token_num,
					num_cy_inst,
					num_dc_inst,
					serv_ena_mask);
	if (ret)
		goto failed;

	ret = 0;
failed:
	kfree(val);
	kfree(key);

	for (i = 0; i < token_num; i++)
		kfree(tokens[i]);

	dev_dbg(&GET_DEV(accel_dev),
		"Failed to get enabled services 0x%x in config file.\n",
		*serv_ena_mask);

	return ret;
}
EXPORT_SYMBOL_GPL(adf_cfg_get_services_enabled);

int adf_cfg_get_num_of_inst(struct adf_accel_dev *accel_dev,
			    u16 *num_cy_inst,
			    u16 *num_dc_inst)
{
	char *key = NULL;
	char *val = NULL;
	char **user_sec_l = NULL;
	int  user_sec_n = 0;

	unsigned long num_inst = 0;
	unsigned long num_proc = 0;
	int ret = -ENOMEM;
	int i = 0;

	*num_cy_inst = 0;
	*num_dc_inst = 0;

	key = kzalloc(ADF_CFG_MAX_KEY_LEN_IN_BYTES, GFP_KERNEL);
	if (!key)
		goto failed;

	val = kzalloc(ADF_CFG_MAX_VAL_LEN_IN_BYTES, GFP_KERNEL);
	if (!val)
		goto failed;

	user_sec_l = kcalloc(ADF_CFG_MAX_NUM_OF_SECTIONS,
			     sizeof(char *),
			     GFP_KERNEL);
	if (!user_sec_l)
		goto failed;

	ret = -EFAULT;
	strscpy(key, ADF_NUM_CY, ADF_CFG_MAX_VAL_LEN_IN_BYTES);
	if (adf_cfg_get_param_value(accel_dev, ADF_KERNEL_SEC, key, val))
		goto failed;

	if (kstrtoul(val, 0, &num_inst))
		goto failed;

	(*num_cy_inst) += num_inst;

	strscpy(key, ADF_NUM_DC, ADF_CFG_MAX_VAL_LEN_IN_BYTES);
	if (adf_cfg_get_param_value(accel_dev, ADF_KERNEL_SEC, key, val))
		goto failed;

	if (kstrtoul(val, 0, &num_inst))
		goto failed;

	(*num_dc_inst) += num_inst;

	if (adf_cfg_get_user_section(accel_dev, user_sec_l, &user_sec_n))
		goto failed;

	for (i = 0; i < user_sec_n; i++) {
		strscpy(key, ADF_NUM_CY, ADF_CFG_MAX_VAL_LEN_IN_BYTES);
		if (adf_cfg_get_param_value(accel_dev, user_sec_l[i], key, val))
			goto failed;

		if (kstrtoul(val, 0, &num_inst))
			goto failed;

		strscpy(key, ADF_NUM_PROCESSES, ADF_CFG_MAX_VAL_LEN_IN_BYTES);
		if (adf_cfg_get_param_value(accel_dev, user_sec_l[i], key, val))
			num_proc = 0;
		else if (kstrtoul(val, 0, &num_proc))
			goto failed;

		(*num_cy_inst) += num_inst * num_proc;

		strscpy(key, ADF_NUM_DC, ADF_CFG_MAX_VAL_LEN_IN_BYTES);
		if (adf_cfg_get_param_value(accel_dev, user_sec_l[i], key, val))
			goto failed;

		if (kstrtoul(val, 0, &num_inst))
			goto failed;

		(*num_dc_inst) += num_inst * num_proc;
	} /* for loop */

	ret = 0;

failed:
	kfree(key);
	kfree(val);
	if (user_sec_l) {
		for (i = 0; i < user_sec_n; i++)
			kfree(user_sec_l[i]);
		kfree(user_sec_l);
	}
	dev_dbg(&GET_DEV(accel_dev),
		"get number of instances in config file. ret %d\n",
		ret);

	return ret;
}

static int
adf_cfg_get_intr_inst_in_kernel_section(struct adf_accel_dev *accel_dev,
					char *key, char *val,
					u16 *num_of_intr_inst)
{
	int ret = -EFAULT;
	int i = 0;
	unsigned long num_inst = 0;
	unsigned long polling_mode = 0;

	strscpy(key, ADF_NUM_CY, ADF_CFG_MAX_VAL_LEN_IN_BYTES);
	if (adf_cfg_get_param_value(accel_dev, ADF_KERNEL_SEC, key, val))
		goto failed;

	if (kstrtoul(val, 0, &num_inst))
		goto failed;

	for (i = 0; i < num_inst; i++) {
		snprintf(key, ADF_CFG_MAX_KEY_LEN_IN_BYTES,
			 ADF_CY_POLL_MODE_FORMAT, i);
		if (adf_cfg_get_param_value(accel_dev, ADF_KERNEL_SEC,
					    key, val))
			goto failed;
		dev_dbg(&GET_DEV(accel_dev), "%s is %s\n", key, val);
		if (kstrtoul(val, 0, &polling_mode))
			goto failed;
		if (polling_mode != 1) {
			(*num_of_intr_inst) += 1;
			ret = 0;
			goto failed;
		}
	}

	strscpy(key, ADF_NUM_DC, ADF_CFG_MAX_VAL_LEN_IN_BYTES);
	if (adf_cfg_get_param_value(accel_dev, ADF_KERNEL_SEC, key, val))
		goto failed;

	if (kstrtoul(val, 0, &num_inst))
		goto failed;

	for (i = 0; i < num_inst; i++) {
		snprintf(key, ADF_CFG_MAX_KEY_LEN_IN_BYTES,
			 ADF_DC_POLL_MODE_FORMAT, i);
		if (adf_cfg_get_param_value(accel_dev, ADF_KERNEL_SEC,
					    key, val))
			goto failed;
		dev_dbg(&GET_DEV(accel_dev), "%s is %s\n", key, val);
		if (kstrtoul(val, 0, &polling_mode))
			goto failed;
		if (polling_mode != 1) {
			(*num_of_intr_inst) += 1;
			ret = 0;
			goto failed;
		}
	}

	ret = 0;
failed:
	return ret;
}

static int
adf_cfg_get_intr_inst_in_user_sections(struct adf_accel_dev *accel_dev,
				       char **user_sec_list, int user_sec_num,
				       char *key, char *val,
				       u16 *num_of_intr_inst)
{
	int ret = -EFAULT;
	int i = 0;
	int j = 0;
	unsigned long num_inst = 0;
	unsigned long polling_mode = 0;

	for (j = 0; j < user_sec_num; j++) {
		strscpy(key, ADF_NUM_CY, ADF_CFG_MAX_VAL_LEN_IN_BYTES);
		if (adf_cfg_get_param_value(accel_dev, user_sec_list[j],
					    key, val))
			goto failed;

		if (kstrtoul(val, 0, &num_inst))
			goto failed;

		for (i = 0; i < num_inst; i++) {
			snprintf(key, ADF_CFG_MAX_KEY_LEN_IN_BYTES,
				 ADF_CY_POLL_MODE_FORMAT, i);
			if (adf_cfg_get_param_value(accel_dev, user_sec_list[j],
						    key, val))
				goto failed;
			dev_dbg(&GET_DEV(accel_dev), "%s is %s\n", key, val);
			if (kstrtoul(val, 0, &polling_mode))
				goto failed;
			if (polling_mode != 1) {
				(*num_of_intr_inst) += 1;
				ret = 0;
				goto failed;
			}
		}

		strscpy(key, ADF_NUM_DC, ADF_CFG_MAX_VAL_LEN_IN_BYTES);
		if (adf_cfg_get_param_value(accel_dev,
					    user_sec_list[j], key, val))
			goto failed;

		if (kstrtoul(val, 0, &num_inst))
			goto failed;

		for (i = 0; i < num_inst; i++) {
			snprintf(key, ADF_CFG_MAX_KEY_LEN_IN_BYTES,
				 ADF_DC_POLL_MODE_FORMAT, i);
			if (adf_cfg_get_param_value(accel_dev, user_sec_list[j],
						    key, val))
				goto failed;
			dev_dbg(&GET_DEV(accel_dev), "%s is %s\n", key, val);
			if (kstrtoul(val, 0, &polling_mode))
				goto failed;
			if (polling_mode != 1) {
				(*num_of_intr_inst) += 1;
				ret = 0;
				goto failed;
			}
		}
	} /* for loop j */

	ret = 0;
failed:
	return ret;
}

int adf_cfg_get_intr_inst(struct adf_accel_dev *accel_dev,
			  u16 *num_of_intr_inst)
{
	char *key = NULL;
	char *val = NULL;
	char **user_sec_l = NULL;
	int  user_sec_n = 0;
	int j = 0;
	int ret = -ENOMEM;
	*num_of_intr_inst = 0;

	key = kzalloc(ADF_CFG_MAX_KEY_LEN_IN_BYTES, GFP_KERNEL);
	if (!key)
		goto failed;

	val = kzalloc(ADF_CFG_MAX_VAL_LEN_IN_BYTES, GFP_KERNEL);
	if (!val)
		goto failed;

	user_sec_l = kcalloc(ADF_CFG_MAX_NUM_OF_SECTIONS,
			     sizeof(char *),
			     GFP_KERNEL);
	if (!user_sec_l)
		goto failed;

	ret = adf_cfg_get_intr_inst_in_kernel_section(accel_dev,
						      key,
						      val,
						      num_of_intr_inst);
	if (ret || (*num_of_intr_inst))
		goto failed;

	ret = adf_cfg_get_user_section(accel_dev, user_sec_l, &user_sec_n);
	if (ret)
		goto failed;

	ret = adf_cfg_get_intr_inst_in_user_sections(accel_dev,
						     user_sec_l,
						     user_sec_n,
						     key,
						     val,
						     num_of_intr_inst);
	if (ret)
		goto failed;

failed:
	kfree(key);
	kfree(val);
	if (user_sec_l) {
		for (j = 0; j < user_sec_n; j++)
			kfree(user_sec_l[j]);
		kfree(user_sec_l);
	}

	return ret;
}

void adf_cfg_set_asym_rings_mask(struct adf_accel_dev *accel_dev)
{
	int service;
	u16 ena_srv_mask;
	u16 service_type;
	u16 asym_mask = 0;
	struct adf_cfg_device *cfg_dev = accel_dev->cfg->dev;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;

	if (!cfg_dev) {
		hw_data->asym_rings_mask = ADF_CFG_DEF_ASYM_MASK;
		return;
	}

	ena_srv_mask = accel_dev->hw_device->ring_to_svc_map;

	/* parse each service */
	for (service = 0;
	     service < ADF_CFG_MAX_SERVICES;
	     service++) {
		service_type =
			GET_SRV_TYPE(ena_srv_mask, service);
		switch (service_type) {
		case CRYPTO:
		case ASYM:
			SET_ASYM_MASK(asym_mask, service);
			if (service_type == CRYPTO)
				service++;
			break;
		}
	}

	hw_data->asym_rings_mask = asym_mask;
}
EXPORT_SYMBOL_GPL(adf_cfg_set_asym_rings_mask);

void adf_cfg_gen_dispatch_arbiter(struct adf_accel_dev *accel_dev,
				  const u32 *thrd_to_arb_map,
				  u32 *thrd_to_arb_map_gen,
				  u32 total_engines)
{
	int engine;
	int thread;
	int service;
	int bits;
	u32 thread_ability;
	u32 ability_map;
	u32 service_mask;
	u16 ena_srv_mask;
	u16 service_type;
	struct adf_cfg_device *device = accel_dev->cfg->dev;

	if (!device) {
		/* if not set, return the default dispatch arbiter */
		for (engine = 0;
		     engine < total_engines;
		     engine++) {
			thrd_to_arb_map_gen[engine] = thrd_to_arb_map[engine];
		}
		return;
	}
	ena_srv_mask = accel_dev->hw_device->ring_to_svc_map;

	for (engine = 0; engine < total_engines; engine++) {
		bits = 0;
		/* ability_map is used to indicate the threads ability */
		ability_map = thrd_to_arb_map[engine];
		thrd_to_arb_map_gen[engine] = 0;
		/* parse each thread on the engine */
		for (thread = 0;
		     thread < ADF_NUM_THREADS_PER_AE;
		     thread++) {
			/* get the ability of this thread */
			thread_ability = ability_map & ADF_THRD_ABILITY_MASK;
			ability_map >>= ADF_THRD_ABILITY_BIT_LEN;
			/* parse each service */
			for (service = 0;
			     service < ADF_CFG_MAX_SERVICES;
			     service++) {
				service_type =
					GET_SRV_TYPE(ena_srv_mask, service);
				switch (service_type) {
				case CRYPTO:
					service_mask = ADF_CFG_ASYM_SRV_MASK;
					if (thread_ability & service_mask)
						thrd_to_arb_map_gen[engine] |=
								(1 << bits);
					bits++;
					service++;
					service_mask = ADF_CFG_SYM_SRV_MASK;
					break;
				case COMP:
					service_mask = ADF_CFG_DC_SRV_MASK;
					break;
				case SYM:
					service_mask = ADF_CFG_SYM_SRV_MASK;
					break;
				case ASYM:
					service_mask = ADF_CFG_ASYM_SRV_MASK;
					break;
				default:
					service_mask = ADF_CFG_UNKNOWN_SRV_MASK;
				}
				if (thread_ability & service_mask)
					thrd_to_arb_map_gen[engine] |=
								(1 << bits);
				bits++;
			}
		}
	}
}
EXPORT_SYMBOL_GPL(adf_cfg_gen_dispatch_arbiter);

static int adf_cfg_get_caps_enabled(struct adf_accel_dev *accel_dev,
				    u32 *enabled_svc_caps, u32 *enabled_fw_caps)
{
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	char val[ADF_CFG_MAX_VAL_LEN_IN_BYTES];
	u8 i = 0;
	struct adf_cfg_enabled_services *svcs = NULL;
	enum adf_cfg_fw_image_type fw_image_type = ADF_FW_IMAGE_DEFAULT;

	*enabled_svc_caps = 0;
	*enabled_fw_caps = 0;

	/* Get the services enabled by user */
	snprintf(key, sizeof(key), ADF_SERVICES_ENABLED);
	if (adf_cfg_get_param_value(accel_dev, ADF_GENERAL_SEC, key, val))
		return -EFAULT;

	/*
	 * Only the PF driver has the hook for get_fw_image_type as the VF's
	 * enabled service is from PFVF communication. The fw_image_type for
	 * the VF is set to DEFAULT since this type contains all kinds of
	 * enabled service.
	 */
#if defined(QAT_UIO) || defined(QAT_ESXI)
	if (accel_dev->hw_device->get_fw_image_type) {
		if (accel_dev->hw_device->get_fw_image_type(accel_dev,
							    &fw_image_type))
			return -EFAULT;
	}
#endif /* QAT_UIO || QAT_ESXI */

	for (i = 0; i < ADF_CFG_SVCS_MAX; i++) {
		svcs = &adf_profiles[fw_image_type].supported_svcs[i];

		if (!strncmp(svcs->svcs_enabled, "",
			     ADF_CFG_MAX_VAL_LEN_IN_BYTES))
			break;

		if (!strncmp(val,
			     svcs->svcs_enabled,
			     ADF_CFG_MAX_VAL_LEN_IN_BYTES)) {
			*enabled_svc_caps = svcs->enabled_svc_cap;
			*enabled_fw_caps = svcs->enabled_fw_cap;
			return 0;
		}
	}

	dev_err(&GET_DEV(accel_dev),
		"Invalid ServicesEnabled %s for ServicesProfile: %d\n",
		val, fw_image_type);

	return -EFAULT;
}

static int adf_cfg_check_enabled_services(struct adf_accel_dev *accel_dev,
					  u32 enabled_svc_caps)
{
	u32 hw_caps = GET_HW_DATA(accel_dev)->accel_capabilities_mask;

	if ((enabled_svc_caps & hw_caps) == enabled_svc_caps)
		return 0;

	dev_err(&GET_DEV(accel_dev), "Unsupported device configuration\n");

	return -EFAULT;
}

int update_accel_cap_mask(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	unsigned long pke_disabled = 0;
	char val[ADF_CFG_MAX_VAL_LEN_IN_BYTES];

	if (!adf_cfg_get_param_value(accel_dev, ADF_GENERAL_SEC,
				     ADF_PKE_DISABLED, val)) {
		if (kstrtoul(val, 0, &pke_disabled))
			return -EFAULT;
	}
	if (hw_data->get_accel_cap) {
		hw_data->accel_capabilities_mask =
			hw_data->get_accel_cap(accel_dev);
	}
	if (pke_disabled) {
		hw_data->accel_capabilities_mask &=
			~ADF_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(update_accel_cap_mask);

int update_accel_cap_mask_c62x(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	unsigned long pke_disabled = 0;
	char val[ADF_CFG_MAX_VAL_LEN_IN_BYTES];
	u32 enabled_svc_caps = 0;
	u32 enabled_fw_caps = 0;

	if (!adf_cfg_get_param_value(accel_dev, ADF_GENERAL_SEC,
				     ADF_PKE_DISABLED, val)) {
		if (kstrtoul(val, 0, &pke_disabled))
			return -EFAULT;
	}

	if (hw_data->get_accel_cap) {
		hw_data->accel_capabilities_mask =
			hw_data->get_accel_cap(accel_dev);
	}

	if (pke_disabled) {
		hw_data->accel_capabilities_mask &=
			~ADF_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC;
	}

	if (adf_cfg_get_caps_enabled(accel_dev, &enabled_svc_caps,
				     &enabled_fw_caps))
		return -EFAULT;

	if (adf_cfg_check_enabled_services(accel_dev, enabled_svc_caps))
		return -EFAULT;

	if (!(enabled_svc_caps & ADF_CFG_CAP_ASYM))
		hw_data->accel_capabilities_mask &= ~ADF_CFG_CAP_ASYM;
	if (!(enabled_svc_caps & ADF_CFG_CAP_SYM))
		hw_data->accel_capabilities_mask &= ~ADF_CFG_CAP_SYM;
	if (!(enabled_svc_caps & ADF_CFG_CAP_DC))
		hw_data->accel_capabilities_mask &= ~ADF_CFG_CAP_DC;

	/* Enable FW defined capabilities*/
	if (enabled_fw_caps)
		hw_data->accel_capabilities_mask |= enabled_fw_caps;

	/* Disable FW not defined capabilities*/
	if (!(enabled_fw_caps & ICP_ACCEL_CAPABILITIES_CIPHER))
		hw_data->accel_capabilities_mask &=
			~ICP_ACCEL_CAPABILITIES_CIPHER;
	if (!(enabled_fw_caps & ICP_ACCEL_CAPABILITIES_AUTHENTICATION))
		hw_data->accel_capabilities_mask &=
			~ICP_ACCEL_CAPABILITIES_AUTHENTICATION;

	return 0;
}
EXPORT_SYMBOL_GPL(update_accel_cap_mask_c62x);

void adf_cfg_get_accel_algo_cap(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u32 cipher_capabilities_mask = 0;
	u32 hash_capabilities_mask = 0;
	u32 accel_capabilities_mask = 0;
	u32 asym_capabilities_mask = 0;

	if (hw_data->get_accel_cap) {
		accel_capabilities_mask =
			hw_data->get_accel_cap(accel_dev);
	}

	if (accel_capabilities_mask & ICP_ACCEL_CAPABILITIES_CIPHER) {
		SET_BIT(cipher_capabilities_mask, ADF_CY_SYM_CIPHER_NULL);
#ifdef QAT_LEGACY_ALGORITHMS
		SET_BIT(cipher_capabilities_mask, ADF_CY_SYM_CIPHER_ARC4);
		SET_BIT(cipher_capabilities_mask, ADF_CY_SYM_CIPHER_AES_ECB);
		SET_BIT(cipher_capabilities_mask, ADF_CY_SYM_CIPHER_DES_CBC);
		SET_BIT(cipher_capabilities_mask, ADF_CY_SYM_CIPHER_DES_ECB);
		SET_BIT(cipher_capabilities_mask, ADF_CY_SYM_CIPHER_3DES_ECB);
		SET_BIT(cipher_capabilities_mask, ADF_CY_SYM_CIPHER_3DES_CBC);
		SET_BIT(cipher_capabilities_mask, ADF_CY_SYM_CIPHER_3DES_CTR);
		SET_BIT(cipher_capabilities_mask, ADF_CY_SYM_CIPHER_AES_F8);
#endif
		SET_BIT(cipher_capabilities_mask, ADF_CY_SYM_CIPHER_AES_CBC);
		SET_BIT(cipher_capabilities_mask, ADF_CY_SYM_CIPHER_AES_CTR);
		SET_BIT(cipher_capabilities_mask, ADF_CY_SYM_CIPHER_KASUMI_F8);
		SET_BIT(cipher_capabilities_mask,
			ADF_CY_SYM_CIPHER_SNOW3G_UEA2);
		SET_BIT(cipher_capabilities_mask, ADF_CY_SYM_CIPHER_AES_XTS);
	}

	if (accel_capabilities_mask & ICP_ACCEL_CAPABILITIES_AUTHENTICATION) {
#ifdef QAT_LEGACY_ALGORITHMS
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_MD5);
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_SHA1);
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_SHA224);
#endif
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_SHA256);
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_SHA384);
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_SHA512);
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_AES_XCBC);
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_KASUMI_F9);
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_SNOW3G_UIA2);
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_AES_CMAC);
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_AES_CBC_MAC);
	}

	if ((accel_capabilities_mask & ICP_ACCEL_CAPABILITIES_CIPHER) &&
	    (accel_capabilities_mask &
		   ICP_ACCEL_CAPABILITIES_AUTHENTICATION)) {
		SET_BIT(cipher_capabilities_mask, ADF_CY_SYM_CIPHER_AES_CCM);
		SET_BIT(cipher_capabilities_mask, ADF_CY_SYM_CIPHER_AES_GCM);
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_AES_CCM);
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_AES_GCM);
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_AES_GMAC);
	}

	if (accel_capabilities_mask & ICP_ACCEL_CAPABILITIES_ZUC) {
		SET_BIT(cipher_capabilities_mask, ADF_CY_SYM_CIPHER_ZUC_EEA3);
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_ZUC_EIA3);
	}

	if (accel_capabilities_mask & ICP_ACCEL_CAPABILITIES_SHA3)
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_SHA3_256);

	if (accel_capabilities_mask & ICP_ACCEL_CAPABILITIES_CHACHA_POLY) {
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_POLY);
		SET_BIT(cipher_capabilities_mask, ADF_CY_SYM_CIPHER_CHACHA);
	}

	if (accel_capabilities_mask & ICP_ACCEL_CAPABILITIES_SM3)
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_SM3);

	if (accel_capabilities_mask & ICP_ACCEL_CAPABILITIES_SHA3_EXT) {
#ifdef QAT_LEGACY_ALGORITHMS
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_SHA3_224);
#endif
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_SHA3_256);
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_SHA3_384);
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_SHA3_512);
	}

	if (accel_capabilities_mask & ICP_ACCEL_CAPABILITIES_SM4) {
#ifdef QAT_LEGACY_ALGORITHMS
		SET_BIT(cipher_capabilities_mask, ADF_CY_SYM_CIPHER_SM4_ECB);
#endif
		SET_BIT(cipher_capabilities_mask, ADF_CY_SYM_CIPHER_SM4_CBC);
		SET_BIT(cipher_capabilities_mask, ADF_CY_SYM_CIPHER_SM4_CTR);
	}

	if (accel_capabilities_mask &
		ICP_ACCEL_CAPABILITIES_WIRELESS_CRYPTO_EXT) {
		SET_BIT(cipher_capabilities_mask,
			ADF_CY_SYM_CIPHER_SNOW3G_UEA2);
		SET_BIT(hash_capabilities_mask, ADF_CY_SYM_HASH_SNOW3G_UIA2);
	}
	if (accel_capabilities_mask & ICP_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC) {
#ifdef QAT_LEGACY_ALGORITHMS
		SET_BIT(asym_capabilities_mask, ADF_CY_ASYM_DH);
		SET_BIT(asym_capabilities_mask, ADF_CY_ASYM_DSA);
#endif
		SET_BIT(asym_capabilities_mask, ADF_CY_ASYM_RSA);
		SET_BIT(asym_capabilities_mask, ADF_CY_ASYM_ECC);
		SET_BIT(asym_capabilities_mask, ADF_CY_ASYM_ECDH);
		SET_BIT(asym_capabilities_mask, ADF_CY_ASYM_ECDSA);
		SET_BIT(asym_capabilities_mask, ADF_CY_ASYM_KEY);
		SET_BIT(asym_capabilities_mask, ADF_CY_ASYM_LARGE_NUMBER);
		SET_BIT(asym_capabilities_mask, ADF_CY_ASYM_PRIME);
	}

	hw_data->cipher_capabilities_mask = cipher_capabilities_mask;
	hw_data->hash_capabilities_mask = hash_capabilities_mask;
	hw_data->asym_capabilities_mask = asym_capabilities_mask;
}
EXPORT_SYMBOL_GPL(adf_cfg_get_accel_algo_cap);

int adf_cfg_device_init(struct adf_cfg_device *device,
			struct adf_accel_dev *accel_dev)
{
	int i = 0;
	/* max_inst indicates the max instance number one bank can hold */
	int max_inst = accel_dev->hw_device->tx_rx_gap;
	int ret = -ENOMEM;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;

	device->bundle_num = 0;
	device->bundles = (struct adf_cfg_bundle **)
		kcalloc(hw_data->num_banks, sizeof(struct adf_cfg_bundle *),
			GFP_KERNEL);
	if (!device->bundles)
		goto failed;

	device->bundle_num = hw_data->num_banks;
	device->bundles_free = hw_data->num_banks;

	device->instances = (struct adf_cfg_instance **)
		kzalloc(sizeof(struct adf_cfg_instance *)
			* device->bundle_num * max_inst,
			GFP_KERNEL);
	if (!device->instances)
		goto failed;

	device->instance_index = 0;

	dev_dbg(&GET_DEV(accel_dev), "init device with bundle information\n");

	ret = -EFAULT;

	/* Update the acceleration capability mask based on User capability */
	if (hw_data->update_accel_cap_mask) {
		if (hw_data->update_accel_cap_mask(accel_dev))
			goto failed;
	}

	/*
	 * Update the algorithms capability mask based on
	 * qat legacy algorithms
	 */
	if (hw_data->get_accel_algo_cap)
		hw_data->get_accel_algo_cap(accel_dev);
	/* parse and get value of ServicesEnabled */
	if (hw_data->get_ring_to_svc_map) {
		if (hw_data->get_ring_to_svc_map(accel_dev,
						 &hw_data->ring_to_svc_map))
			goto failed;
	}

	ret = -ENOMEM;
	/*
	 * 1) get the config information to generate the ring to service
	 *    mapping table
	 * 2) init each bundle of this device
	 */
	for (i = 0; i < device->bundle_num; i++) {
		device->bundles[i] =
			kzalloc(sizeof(struct adf_cfg_bundle), GFP_KERNEL);
		if (!device->bundles[i])
			goto failed;

		device->bundles[i]->max_section = max_inst;
		adf_cfg_bundle_init(device->bundles[i], device, i, accel_dev);
	}

	return 0;

failed:
	for (i = 0; i < device->bundle_num; i++) {
		if (device->bundles[i])
			adf_cfg_bundle_clear(device->bundles[i], accel_dev);
	}

	for (i = 0; i < (device->bundle_num * max_inst); i++) {
		if (device->instances && device->instances[i])
			kfree(device->instances[i]);
	}

	kfree(device->instances);
	device->instances = NULL;

	dev_err(&GET_DEV(accel_dev), "Failed to do device init\n");
	return ret;
}

void adf_cfg_device_clear(struct adf_cfg_device *device,
			  struct adf_accel_dev *accel_dev)
{
	int i = 0;

	dev_dbg(&GET_DEV(accel_dev), "clear device with bundle information\n");
	for (i = 0; i < device->bundle_num; i++) {
		if (device->bundles && device->bundles[i]) {
			adf_cfg_bundle_clear(device->bundles[i], accel_dev);
			kfree(device->bundles[i]);
			device->bundles[i] = NULL;
		}
	}

	kfree(device->bundles);
	device->bundles = NULL;

	for (i = 0; i < device->instance_index; i++) {
		if (device->instances && device->instances[i]) {
			kfree(device->instances[i]);
			device->instances[i] = NULL;
		}
	}

	kfree(device->instances);
	device->instances = NULL;
}

void adf_cfg_device_clear_all(struct adf_accel_dev *accel_dev)
{
	down_write(&accel_dev->cfg->lock);
	if (accel_dev->cfg->dev) {
		adf_cfg_device_clear(accel_dev->cfg->dev, accel_dev);
		kfree(accel_dev->cfg->dev);
		accel_dev->cfg->dev = NULL;
	}
	up_write(&accel_dev->cfg->lock);
}
EXPORT_SYMBOL_GPL(adf_cfg_device_clear_all);

int adf_config_device(struct adf_accel_dev *accel_dev)
{
	struct adf_cfg_device_data *cfg = NULL;
	struct adf_cfg_device *cfg_device = NULL;
	struct adf_cfg_section *sec;
	struct list_head *list = NULL;
	int ret = -ENOMEM;

	if (!accel_dev)
		return ret;

	cfg = accel_dev->cfg;
	cfg->dev = NULL;
	cfg_device = (struct adf_cfg_device *)
			kzalloc(sizeof(*cfg_device), GFP_KERNEL);
	if (!cfg_device)
		goto failed;

	ret = -EFAULT;

	if (adf_cfg_device_init(cfg_device, accel_dev))
		goto failed;

	cfg->dev = cfg_device;

	/* GENERAL and KERNEL section must be processed before others */
	if (adf_cfg_process_filter_by(accel_dev, ADF_GENERAL_SEC))
		goto failed;
	if (adf_cfg_process_filter_by(accel_dev, ADF_KERNEL_SEC))
		goto failed;
	if (adf_cfg_process_filter_by(accel_dev, ADF_SIOV_SEC))
		goto failed;

	/* process user sections */
	if (adf_cfg_process_filter_by(accel_dev, NULL))
		goto failed;

	/* newly added accel section */
	ret = adf_cfg_process_section(accel_dev,
				      ADF_ACCEL_SEC,
				      accel_dev->accel_id);
	if (ret)
		goto failed;

	/*
	 * put item-remove task after item-process
	 * because during process we may fetch values from those items
	 */
	list_for_each(list, &cfg->sec_list) {
		sec = list_entry(list, struct adf_cfg_section, list);
		if (!sec->is_derived) {
			dev_dbg(&GET_DEV(accel_dev), "Clean up section %s\n",
				sec->name);
			ret = adf_cfg_cleanup_section(accel_dev,
						      sec->name,
						      accel_dev->accel_id);
			if (ret)
				goto failed;
		}
	}

	ret = 0;
failed:
	if (ret) {
		if (cfg_device) {
			adf_cfg_device_clear(cfg_device, accel_dev);
			kfree(cfg_device);
			cfg->dev = NULL;
		}
		adf_cfg_del_all(accel_dev);
		dev_err(&GET_DEV(accel_dev), "Failed to config device\n");
	}

	return ret;
}
EXPORT_SYMBOL_GPL(adf_config_device);

#if defined(QAT_UIO) || defined(QAT_ESXI)
int adf_cfg_get_fw_image_type(struct adf_accel_dev *accel_dev,
			      enum adf_cfg_fw_image_type *fw_image_type)
{
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	char val[ADF_CFG_MAX_VAL_LEN_IN_BYTES];

	snprintf(key, sizeof(key), ADF_SERVICES_PROFILE);
	if (adf_cfg_get_param_value(accel_dev, ADF_GENERAL_SEC,
				    key, val)) {
		memcpy(val, ADF_SERVICES_DEFAULT,
		       sizeof(ADF_SERVICES_DEFAULT));
		dev_info(&GET_DEV(accel_dev),
			 "Enabling default configuration\n");
	}
	adf_cfg_fw_string_to_id(val, accel_dev, &fw_image_type);

	return 0;
}
EXPORT_SYMBOL_GPL(adf_cfg_get_fw_image_type);
#endif /* QAT_UIO || QAT_ESXI */

int adf_cfg_check_deprecated_params(struct adf_accel_dev *accel_dev)
{
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	char val[ADF_CFG_MAX_VAL_LEN_IN_BYTES];
	u8 i = 0;
	struct adf_accel_pci *accel_pci_dev = &accel_dev->accel_pci_dev;
	struct pci_dev *pdev = accel_pci_dev->pci_dev;

	for (i = 0; i < ADF_CFG_DEPRE_PARAMS_NUM; i++) {
		/* give a warning if the deprecated params are set by user */
		snprintf(key, sizeof(key), adf_cfg_deprecated_params[i]);

		/* skip valid param specific to device type */
		if (!strncmp(ADF_RL_FIRMWARE_ENABLED, key, sizeof(key)))
			if (pdev && IS_QAT_GEN3(pdev->device))
				continue;

		if (!adf_cfg_get_param_value(accel_dev, ADF_GENERAL_SEC,
					     key, val)) {
			dev_warn(&GET_DEV(accel_dev),
				 "Parameter '%s' has been deprecated\n", key);
		}
	}
	return 0;
}
EXPORT_SYMBOL_GPL(adf_cfg_check_deprecated_params);

