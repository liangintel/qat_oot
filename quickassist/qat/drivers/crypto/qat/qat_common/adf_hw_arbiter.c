// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2021 Intel Corporation */

#include <linux/bitops.h>
#include <linux/mutex.h>
#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "adf_cfg_common.h"
#include "adf_transport_internal.h"

#ifdef QAT_UIO
#include "adf_uio_control.h"
#endif
#define ADF_ARB_REG_SIZE 0x4

#define WRITE_CSR_ARB_SARCONFIG(csr_addr, csr_offset, index, value) \
	ADF_CSR_WR(csr_addr, (csr_offset) + \
	(ADF_ARB_REG_SIZE * (index)), value)
static DEFINE_MUTEX(csr_arb_lock);

#define WRITE_CSR_ARB_WRK_2_SER_MAP(csr_addr, csr_offset, \
	wrk_to_ser_map_offset, index, value) \
	ADF_CSR_WR(csr_addr, ((csr_offset) + (wrk_to_ser_map_offset)) + \
	(ADF_ARB_REG_SIZE * (index)), value)

#ifdef QAT_HB_FAIL_SIM
void adf_write_csr_arb_wrk_2_ser_map(void *csr_addr, u32 csr_offset,
				     u32 wrk_to_ser_map_offset,
				     size_t index, u32 value)
{
	WRITE_CSR_ARB_WRK_2_SER_MAP(csr_addr, csr_offset,
				    wrk_to_ser_map_offset,
				    index, value);
}
#endif

int adf_init_arb(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct arb_info info;
	void __iomem *csr = accel_dev->transport->banks[0].csr_addr;
	u32 arb_cfg = 0x1U << 31 | 0x4U << 4 | 0x1;
	u32 arb, i;
	const u32 *thd_2_arb_cfg;

	hw_data->get_arb_info(&info);

	/* Service arb configured for 32 bytes responses and
	 * ring flow control check enabled.
	 */
	for (arb = 0; arb < ADF_ARB_NUM; arb++)
		WRITE_CSR_ARB_SARCONFIG(csr, info.arbiter_offset,
					arb, arb_cfg);

	/* Map worker threads to service arbiters */
	if (hw_data->get_arb_mapping) {
		hw_data->get_arb_mapping(accel_dev, &thd_2_arb_cfg);
		if (!thd_2_arb_cfg)
			return -EFAULT;

		for (i = 0; i < hw_data->num_engines; i++)
			WRITE_CSR_ARB_WRK_2_SER_MAP(csr,
						    info.arbiter_offset,
						    info.wrk_thd_2_srv_arb_map,
						    i, *(thd_2_arb_cfg + i));
	}

	return 0;
}
EXPORT_SYMBOL_GPL(adf_init_arb);

void adf_update_ring_arb(struct adf_etr_ring_data *ring)
{
	int shift;
	u32 arben, arben_tx, arben_rx, arb_mask;
	struct adf_accel_dev *accel_dev = ring->bank->accel_dev;
	struct adf_hw_csr_info *csr_info =
		&accel_dev->hw_device->csr_info;
	struct adf_hw_csr_ops *csr_ops = &csr_info->csr_ops;

	arb_mask = csr_info->arb_enable_mask;
	shift = hweight32(arb_mask);
	arben_tx = ring->bank->ring_mask & arb_mask;
	arben_rx = (ring->bank->ring_mask >> shift) & arb_mask;
	arben = arben_tx & arben_rx;

	csr_ops->write_csr_ring_srv_arb_en(ring->bank->csr_addr,
					   ring->bank->bank_number,
					   arben);
}

#ifdef QAT_UIO
void adf_update_uio_ring_arb(struct adf_uio_control_bundle *bundle)
{
	int shift;
	u32 arben, arben_tx, arben_rx, arb_mask;
	struct adf_accel_dev *accel_dev =
		bundle->uio_priv.accel->accel_dev;
	struct adf_hw_csr_info *csr_info =
		&accel_dev->hw_device->csr_info;
	struct adf_hw_csr_ops *csr_ops = &csr_info->csr_ops;

	arb_mask = csr_info->arb_enable_mask;
	shift = hweight32(arb_mask);

	arben_tx = bundle->rings_enabled & arb_mask;
	arben_rx = (bundle->rings_enabled >> shift) & arb_mask;
	arben = arben_tx & arben_rx;
	csr_ops->write_csr_ring_srv_arb_en(bundle->csr_addr, 0, arben);
}
#endif

void adf_enable_ring_arb(struct adf_accel_dev *accel_dev, void *csr_addr,
			 u32 bank, unsigned int mask)
{
	struct adf_hw_csr_ops *csr_ops =
		&accel_dev->hw_device->csr_info.csr_ops;
	u32 arbenable;

	mutex_lock(&csr_arb_lock);
	arbenable = csr_ops->read_csr_ring_srv_arb_en(csr_addr, bank);
	arbenable |= mask & 0xFF;
	csr_ops->write_csr_ring_srv_arb_en(csr_addr, bank, arbenable);
	mutex_unlock(&csr_arb_lock);
}

void adf_disable_ring_arb(struct adf_accel_dev *accel_dev, void *csr_addr,
			  u32 bank, unsigned int mask)
{
	struct adf_hw_csr_ops *csr_ops =
		&accel_dev->hw_device->csr_info.csr_ops;
	u32 arbenable;

	mutex_lock(&csr_arb_lock);
	arbenable = csr_ops->read_csr_ring_srv_arb_en(csr_addr, bank);
	arbenable &= ~mask & 0xFF;
	csr_ops->write_csr_ring_srv_arb_en(csr_addr, bank, arbenable);
	mutex_unlock(&csr_arb_lock);
}

#ifdef QAT_UIO
#ifdef QAT_KPT
void adf_update_kpt_wrk_arb(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	void __iomem *csr = accel_dev->transport->banks[0].csr_addr;
	const u32 *thd_to_arb_cfg;
	u32 thd6_arb, i;
	struct arb_info info;

	hw_data->get_arb_info(&info);

	/* Map worker threads to service arbiters */
	hw_data->get_arb_mapping(accel_dev, &thd_to_arb_cfg);

	/* Update thread 6 of AE 0 service arbiter for KPT enabled */
	thd6_arb = *(thd_to_arb_cfg);
	thd6_arb &= 0xF0FFFFFF;
	for (i = 0; i < hw_data->num_engines; i++)
		WRITE_CSR_ARB_WRK_2_SER_MAP(csr,
					    info.arbiter_offset,
					    info.wrk_thd_2_srv_arb_map,
					    0, thd6_arb);
}
EXPORT_SYMBOL_GPL(adf_update_kpt_wrk_arb);

#endif
#endif
void adf_exit_arb(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct arb_info info;
	void __iomem *csr;
	unsigned int i;
	struct adf_hw_csr_ops *csr_ops;

	if (!accel_dev->transport)
		return;

	csr_ops = &accel_dev->hw_device->csr_info.csr_ops;
	csr = accel_dev->transport->banks[0].csr_addr;

	hw_data->get_arb_info(&info);

	/* Reset arbiter configuration */
	for (i = 0; i < ADF_ARB_NUM; i++)
		WRITE_CSR_ARB_SARCONFIG(csr,
					info.arbiter_offset, i, 0);

	/* Unmap worker threads to service arbiters */
	if (hw_data->get_arb_mapping) {
		for (i = 0; i < hw_data->num_engines; i++)
			WRITE_CSR_ARB_WRK_2_SER_MAP(csr,
						    info.arbiter_offset,
						    info.wrk_thd_2_srv_arb_map,
						    i, 0);
	}

	/* Disable arbitration on all rings */
	for (i = 0; i < GET_MAX_BANKS(accel_dev); i++)
		csr_ops->write_csr_ring_srv_arb_en(csr, i, 0);
}
EXPORT_SYMBOL_GPL(adf_exit_arb);

void adf_disable_arb(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_csr_ops *csr_ops;
	void __iomem *csr;
	unsigned int i;

	if (!accel_dev || !accel_dev->transport)
		return;

	csr_ops = &accel_dev->hw_device->csr_info.csr_ops;
	csr = accel_dev->transport->banks[0].csr_addr;

	/* Disable arbitration on all rings */
	for (i = 0; i < GET_MAX_BANKS(accel_dev); i++)
		csr_ops->write_csr_ring_srv_arb_en(csr, i, 0);
}
EXPORT_SYMBOL_GPL(adf_disable_arb);
