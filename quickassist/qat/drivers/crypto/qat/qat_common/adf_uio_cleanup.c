// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2021 Intel Corporation */

#include <linux/delay.h>
#include <linux/sched.h>
#include "adf_uio_control.h"
#include "adf_accel_devices.h"
#include "adf_transport_access_macros.h"
#include "adf_common_drv.h"
#include "adf_uio_cleanup.h"

#define     ADF_RING_EMPTY_MAX_RETRY 15
#define     ADF_RING_EMPTY_RETRY_DELAY 2

struct bundle_orphan_ring {
	unsigned long  tx_mask;
	unsigned long  rx_mask;
	unsigned long  asym_mask;
	void __iomem   *csr_base;
	struct adf_uio_control_bundle *bundle;
};

/* if orphan->tx_mask does not match with orphan->rx_mask */
static void check_orphan_ring(struct adf_uio_control_accel *accel,
			      struct bundle_orphan_ring *orphan,
			      struct adf_hw_device_data *hw_data)
{
	int i;
	struct adf_accel_dev *accel_dev = accel->accel_dev;
	struct adf_hw_csr_ops *csr_ops =
		&accel_dev->hw_device->csr_info.csr_ops;
	int tx_rx_gap = hw_data->tx_rx_gap;
	u8 num_rings_per_bank = hw_data->num_rings_per_bank;
	void __iomem *csr_base = orphan->csr_base;

	for (i = 0; i < num_rings_per_bank; i++) {
		if (test_bit(i, &orphan->tx_mask)) {
			int rx_ring = i + tx_rx_gap;

			if (!test_bit(rx_ring, &orphan->rx_mask)) {
				__clear_bit(i, &orphan->tx_mask);

				/* clean up this tx ring  */
				csr_ops->write_csr_ring_config(csr_base,
							       0, i, 0);
				csr_ops->write_csr_ring_base(csr_base,
							     0, i, 0);
			}

		} else if (test_bit(i, &orphan->rx_mask)) {
			int tx_ring = i - tx_rx_gap;

			if (!test_bit(tx_ring, &orphan->tx_mask)) {
				__clear_bit(i, &orphan->rx_mask);

				/* clean up this rx ring */
				csr_ops->write_csr_ring_config(csr_base,
							       0, i, 0);
				csr_ops->write_csr_ring_base(csr_base,
							     0, i, 0);
			}
		}
	}
}

static int get_orphan_bundle(struct uio_info *info,
			     struct adf_uio_control_accel *accel,
			     struct bundle_orphan_ring **orphan,
			     u32 pid)
{
	int i;
	int ret = 0;
	void __iomem *csr_base;
	unsigned long tx_mask;
	unsigned long asym_mask;
	struct adf_accel_dev *accel_dev = accel->accel_dev;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct adf_hw_csr_ops *csr_ops = &hw_data->csr_info.csr_ops;
	u8 num_rings_per_bank = hw_data->num_rings_per_bank;
	struct bundle_orphan_ring *orphan_bundle;
	u64 base;
	struct list_head *entry = NULL;
	struct qat_uio_bundle_dev *priv = info->priv;
	struct adf_uio_control_bundle *bundle = priv->bundle;
	struct adf_uio_instance_rings *instance_rings;
	u16 ring_mask = 0;

	orphan_bundle = kzalloc(sizeof(*orphan_bundle), GFP_KERNEL);
	if (!orphan_bundle)
		return -ENOMEM;

	csr_base = info->mem[0].internal_addr;
	orphan_bundle->csr_base = csr_base;

	orphan_bundle->tx_mask = 0;
	orphan_bundle->rx_mask = 0;
	tx_mask = accel_dev->hw_device->tx_rings_mask;
	asym_mask = accel_dev->hw_device->asym_rings_mask;

	orphan_bundle->bundle = bundle;

	/* Get ring mask for this process. */
	mutex_lock(&bundle->list_lock);
	list_for_each(entry, &bundle->list) {
		instance_rings = list_entry(entry,
					    struct adf_uio_instance_rings,
					    list);
		if (instance_rings->user_pid == pid) {
			ring_mask = instance_rings->ring_mask;
			break;
		}
	}
	mutex_unlock(&bundle->list_lock);

	for (i = 0; i < num_rings_per_bank; i++) {
		base = csr_ops->read_csr_ring_base(csr_base, 0, i);

		if (!base)
			continue;
		if (!(ring_mask & 1 << i))
			continue; /* Not reserved for this process. */

		if (test_bit(i, &tx_mask))
			__set_bit(i, &orphan_bundle->tx_mask);
		else
			__set_bit(i, &orphan_bundle->rx_mask);

		if (test_bit(i, &asym_mask))
			__set_bit(i, &orphan_bundle->asym_mask);
	}

	if (orphan_bundle->tx_mask || orphan_bundle->rx_mask)
		check_orphan_ring(accel, orphan_bundle, hw_data);

	*orphan = orphan_bundle;

	return ret;
}

static void put_orphan_bundle(struct bundle_orphan_ring **bundle)
{
	if (!bundle || !*bundle)
		return;

	kfree(*bundle);
	*bundle = NULL;
}

/* cleanup all ring  */
static void cleanup_all_ring(struct adf_uio_control_accel *accel,
			     struct bundle_orphan_ring *orphan)
{
	int i;
	void __iomem *csr_base = orphan->csr_base;
	unsigned long  mask = orphan->rx_mask | orphan->tx_mask;
	struct adf_accel_dev *accel_dev = accel->accel_dev;
	struct adf_hw_csr_ops *csr_ops =
		&accel_dev->hw_device->csr_info.csr_ops;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u8 num_rings_per_bank = hw_data->num_rings_per_bank;

	mutex_lock(&orphan->bundle->lock);
	orphan->bundle->rings_enabled &= ~mask;
	adf_update_uio_ring_arb(orphan->bundle);
	mutex_unlock(&orphan->bundle->lock);

	for (i = 0; i < num_rings_per_bank; i++) {
		if (!test_bit(i, &mask))
			continue;

		csr_ops->write_csr_ring_config(csr_base, 0, i, 0);
		csr_ops->write_csr_ring_base(csr_base, 0, i, 0);
	}
}

/* Return true, if number of messages in tx ring is equal to number
 * of messages in corresponding rx ring, else false.
 */
static bool is_all_resp_recvd(struct adf_hw_csr_ops *csr_ops,
			      struct bundle_orphan_ring *bundle,
			      const u8 num_rings_per_bank)
{
	u32 rx_tail = 0, tx_head = 0;
	u32 rx_ring_msg_offset = 0, tx_ring_msg_offset = 0;
	u32 tx_rx_offset = num_rings_per_bank / 2, idx = 0;
	u32 retry = 0, delay = ADF_RING_EMPTY_RETRY_DELAY;

	do {
		for_each_set_bit(idx, &bundle->tx_mask, tx_rx_offset) {
			rx_tail = csr_ops->read_csr_ring_tail(bundle->csr_base, 0,
						     (idx + tx_rx_offset));
			tx_head = csr_ops->read_csr_ring_head(bundle->csr_base, 0, idx);

			/* Normalize messages in tx rings to match rx ring
			 * message size, i.e., size of response message(32).
			 * Asym messages are 64 bytes each, so right shift
			 * by 1 to normalize to 32. Sym and compression
			 * messages are 128 bytes each, so right shift by 2
			 * to normalize to 32.
			 */
			if (bundle->asym_mask & (1 << idx))
				tx_ring_msg_offset = (tx_head >> 1);
			else
				tx_ring_msg_offset = (tx_head >> 2);

			rx_ring_msg_offset = rx_tail;

			if (tx_ring_msg_offset != rx_ring_msg_offset)
				break;
		}
		if (idx == tx_rx_offset)
			/* All Tx and Rx ring message counts match */
			return true;

		msleep(delay);
		delay *= 2;
	} while (++retry < ADF_RING_EMPTY_MAX_RETRY);

	return false;
}

static int bundle_need_cleanup(struct adf_uio_control_accel *accel,
			       struct uio_info *info,
			       const u8 num_rings_per_bank)
{
	int i;
	struct adf_accel_dev *accel_dev = accel->accel_dev;
	struct adf_hw_csr_ops *csr_ops =
		&accel_dev->hw_device->csr_info.csr_ops;
	void __iomem *csr_base = info->mem[0].internal_addr;

	if (!csr_base)
		return 0;

	for (i = 0; i < num_rings_per_bank; i++) {
		if (csr_ops->read_csr_ring_base(csr_base, 0, i))
			return 1;
	}

	return 0;
}

static void cleanup_orphan_ring(struct bundle_orphan_ring *orphan,
				struct adf_uio_control_accel *accel)
{
	struct adf_accel_dev *accel_dev = accel->accel_dev;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct adf_hw_csr_ops *csr_ops = &hw_data->csr_info.csr_ops;
	u8 number_rings_per_bank = hw_data->num_rings_per_bank;

	/* disable the interrupt */
	csr_ops->write_csr_int_col_en(orphan->csr_base, 0, 0);

	/* Wait firmware finish the in-process ring
	 * 1. disable all tx rings
	 * 2. check if all responses are received
	 * 3. reset all rings
	 */
	adf_disable_ring_arb(accel_dev, orphan->csr_base, 0, orphan->tx_mask);

	if (!is_all_resp_recvd(csr_ops, orphan, number_rings_per_bank)) {
		dev_err(&GET_DEV(accel_dev), "Failed to clean up orphan rings\n");
		return;
	}

	/* When the execution reaches here, it is assumed that
	 * there is no inflight request in the rings and that
	 * there is no in-process ring.
	 */
	cleanup_all_ring(accel, orphan);
	pr_debug("QAT: orphan rings cleaned\n");
}

void adf_uio_do_cleanup_orphan(struct uio_info *info,
			       struct adf_uio_control_accel *accel,
			       u32 pid, u8 *comm)
{
	int ret;
	struct bundle_orphan_ring *orphan = NULL;
	struct adf_accel_dev *accel_dev = accel->accel_dev;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	u8 number_rings_per_bank = hw_data->num_rings_per_bank;
	struct qat_uio_bundle_dev *priv = info->priv;
	struct adf_uio_control_bundle *bundle = priv->bundle;
	struct adf_uio_instance_rings *instance_rings, *tmp;

	ret = get_orphan_bundle(info, accel, &orphan, pid);
	if (ret < 0) {
		dev_err(&GET_DEV(accel_dev),
			"get orphan ring failed to cleanup bundle\n");
		return;
	}

	/*
	 * If driver supports ring pair reset, no matter process
	 * exits normally or abnormally, just do ring pair reset.
	 * ring pair reset will reset all ring pair registers to
	 * default value. Driver only needs to reset ring mask
	 */
	if (hw_data->ring_pair_reset) {
		hw_data->ring_pair_reset(accel_dev,
					 orphan->bundle->hardware_bundle_number);
		mutex_lock(&orphan->bundle->lock);
		/*
		 * If processes exit normally, rx_mask, tx_mask
		 * and rings_enabled are all 0, below expression
		 * have no impact on rings_enabled.
		 * If processes exit abnormally, rings_enabled
		 * will be set as 0 by below expression.
		 */
		orphan->bundle->rings_enabled &= ~(orphan->rx_mask |
						   orphan->tx_mask);
		mutex_unlock(&orphan->bundle->lock);
		goto out;
	}

	if (!orphan->tx_mask && !orphan->rx_mask)
		goto out;

	if (!bundle_need_cleanup(accel, info, number_rings_per_bank))
		goto out;

	dev_warn(&GET_DEV(accel_dev), "Process %d %s exit with orphan rings\n",
		 pid, comm);
	/* If the device is in reset phase, we do not need to clean the ring
	 * here since we have disabled BME and will clean the ring in
	 * stop/shutdown stage.
	 */
	if (!test_bit(ADF_STATUS_RESTARTING, &accel_dev->status))
		cleanup_orphan_ring(orphan, accel);
out:
	put_orphan_bundle(&orphan);
	/* If the user process died without releasing the rings
	 * then force a release here.
	 */
	mutex_lock(&bundle->list_lock);
	list_for_each_entry_safe(instance_rings, tmp, &bundle->list, list) {
		if (instance_rings->user_pid == pid) {
			bundle->rings_used &= ~instance_rings->ring_mask;
			list_del(&instance_rings->list);
			kfree(instance_rings);
			break;
		}
	}
	mutex_unlock(&bundle->list_lock);
}
