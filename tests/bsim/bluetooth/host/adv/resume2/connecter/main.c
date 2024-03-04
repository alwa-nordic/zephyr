/* Copyright (c) 2024 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include "zephyr/bluetooth/hci_types.h"
#include <errno.h>
#include <testlib/conn.h>
#include <testlib/scan.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/__assert.h>

LOG_MODULE_REGISTER(connecter, LOG_LEVEL_INF);

int main(void)
{
	int err;

	err = bt_enable(NULL);
	__ASSERT_NO_MSG(!err);

	while (true) {
		bt_addr_le_t result;
		struct bt_conn *conn = NULL;

		bt_testlib_conn_wait_free();

		err = bt_testlib_scan_find_name(&result, "dut");
		__ASSERT_NO_MSG(!err);

		/* The above scan will never timeout, but the below connect has
		 * a built-in timeout in the stack.
		 *
		 * The timeout causes `BT_HCI_ERR_UNKNOWN_CONN_ID`.
		 *
		 * The timeout is a good thing in this app. Maybe the DUT is
		 * going to change its address, so we should scan for the name
		 * again.
		 */

		err = bt_testlib_connect(&result, &conn);
		if (err) {
			__ASSERT_NO_MSG(err == BT_HCI_ERR_UNKNOWN_CONN_ID);
		}

		if (conn) {
			bt_testlib_wait_disconnected(conn);
			bt_testlib_conn_unref(&conn);
		}
	}

	return 0;
}
