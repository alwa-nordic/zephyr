/*
 * Copyright (c) 2017-2021 Nordic Semiconductor ASA
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SUBSYS_BLUETOOTH_HOST_SCAN_H_
#define SUBSYS_BLUETOOTH_HOST_SCAN_H_

#include <stdint.h>

#include <zephyr/sys/atomic.h>
#include <zephyr/bluetooth/bluetooth.h>

/**
 * Reasons why a scanner can be running.
 * Used as input to @ref bt_le_scan_user_add
 * and @ref bt_le_scan_user_remove.
 */
enum bt_le_scan_user {
	/** The application explicitly instructed the stack to scan for advertisers
	 * using the API @ref bt_le_scan_start.
	 * Users of the scanner module may not use this flag as input to @ref bt_le_scan_user_add
	 * and @ref bt_le_scan_user_remove. Use ®ref bt_le_scan_start and @ref bt_le_scan_stop
	 * instead.
	 */
	BT_LE_SCAN_USER_EXPLICIT_SCAN,

	/**
	 * Periodic sync syncing to a periodic advertiser.
	 */
	BT_LE_SCAN_USER_PER_SYNC,

	/**
	 * Scanning to find devices to connect to.
	 */
	BT_LE_SCAN_USER_CONN,

	/**
	 * Special state for a NOP for @ref bt_le_scan_user_add and @ref bt_le_scan_user_remove to
	 * not add/remove any user.
	 */
	BT_LE_SCAN_USER_NONE,
	BT_LE_SCAN_USER_NUM_FLAGS,
};

void bt_scan_reset(void);

bool bt_id_scan_random_addr_check(void);
bool bt_le_scan_active_scanner_running(void);

int bt_le_scan_set_enable(uint8_t enable);

void bt_periodic_sync_disable(void);

/**
 * Start / update the scanner.
 *
 * This API updates the users of the scanner.
 * Multiple users can be enabled at the same time.
 * Depending on all the users, scan parameters are selected
 * and the scanner is started or updated, if needed.
 * This API may update the scan parameters, for example if the scanner is already running
 * when another user that demands higher duty-cycle is being added.
 * It is not allowed to add a user that was already added.
 *
 * Every SW module that informs the scanner that it should run, needs to eventually remove
 * the flag again using @ref bt_le_scan_user_remove once it does not require
 * the scanner to run, anymore.
 *
 * If flag is set to @ref BT_LE_SCAN_USER_NONE, no user is being added. Instead, the
 * existing users are checked and the scanner is started, stopped or updated.
 * For platforms where scanning and initiating at the same time is not supported,
 * this allows the background scanner to be started or stopped once the device starts to
 * initiate a connection.
 *
 * @param flag user requesting the scanner
 *
 * @retval 0 in case of success
 * @retval -EALREADY if the user is already enabled
 * @retval -EPERM    if the explicit scanner is being enabled while the initiator is running
 *                    and the device does not support this configuration.
 * @retval -ENOBUFS  if no hci command buffer could be allocated
 * @retval -EBUSY    if the scanner is updated in a different thread. The user was added but
 *                   the scanner was not started/stopped/updated.
 * @returns negative error codes for errors in @ref bt_hci_cmd_send_sync
 */
int bt_le_scan_user_add(enum bt_le_scan_user flag);

/**
 * Stop / update the scanner.
 *
 * This API updates the users of the scanner.
 * Depending on all enabled users, scan parameters are selected
 * and the scanner is stopped or updated, if needed.
 * This API may update the scan parameters, for example if the scanner is already running
 * when a user that demands higher duty-cycle is being removed.
 * Removing a user that was not added does not result in an error.
 *
 * This API allows removing the user why the scanner is running.
 * If all users for the scanner to run are removed, this API will stop the scanner.
 *
 * If flag is set to @ref BT_LE_SCAN_USER_NONE, no user is being added. Instead, the
 * existing users are checked and the scanner is started, stopped or updated.
 * For platforms where scanning and initiating at the same time is not supported,
 * this allows the background scanner to be started or stopped once the device starts to
 * initiate a connection.
 *
 * @param flag user releasing the scanner
 *
 * @retval 0 in case of success
 * @retval -ENOBUFS  if no hci command buffer could be allocated
 * @retval -EBUSY    if the scanner is updated in a different thread. The user was removed but
 *                   the scanner was not started/stopped/updated.
 * @returns negative error codes for errors in @ref bt_hci_cmd_send_sync
 */
int bt_le_scan_user_remove(enum bt_le_scan_user flag);

/**
 * Check if the explicit scanner was enabled.
 */
bool bt_le_explicit_scanner_running(void);

/**
 * Check if an explicit scanner uses the same parameters
 *
 * @param create_param Parameters used for connection establishment.
 *
 * @return true If explicit scanner uses the same parameters
 * @return false If explicit scanner uses different parameters
 */
bool bt_le_explicit_scanner_uses_same_params(const struct bt_conn_le_create_param *create_param);

/**
 * Check if the scan module needs bt_scan_rx_work() to be run.
 */
bool bt_scan_rx_work_pending(void);

/**
 * Deliver some advertising reports to the application. This function
 * will use the current thread to invoke application callbacks.
 */
void bt_scan_rx_work(void);

/**
 * Append an advertising report to the scan module. This function is
 * intended to be used by the controller to deliver advertising reports
 * to the scan module.
 *
 * The buffer node ownership is taken.
 * The buffer's destroy callback must be ISR safe.
 *
 * @param buf A buffer containing the advertising report.
 */
void bt_scan_append_ext_adv_report(struct net_buf *buf);

/**
 * Immediately force processing advertising reports, discarding
 * data if necessary, until this frees one HCI buffer.
 * Processing advertising reports in this manner will nok invoke
 * the application scan result callbacks.
 *
 * This is intended to safely discard advertising reports and
 * reclaim an HCI event buffer.
 *
 * isr-ok
 */
void bt_scan_drop_buf(void);


void reset_reassembling_advertiser(void);

void le_adv_recv(bt_addr_le_t *addr, struct bt_le_scan_recv_info *info, struct net_buf_simple *buf,
	uint16_t len);
#endif /* defined SUBSYS_BLUETOOTH_HOST_SCAN_H_ */
