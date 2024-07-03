/* Copyright (c) 2024 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include "zephyr/bluetooth/addr.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/sys/atomic_builtin.h>
#include <zephyr/sys/atomic_types.h>

#include <testlib/addr.h>
#include <testlib/conn.h>
#include <testlib/scan.h>

#include <bs_tracing.h>
#include <bstests.h>

extern enum bst_result_t bst_result;

LOG_MODULE_REGISTER(dut, LOG_LEVEL_INF);

atomic_t connected_count;

static void on_connected(struct bt_conn *conn, uint8_t conn_err)
{
	atomic_t count = atomic_inc(&connected_count) + 1;

	LOG_INF("Connected. Current count %d", count);
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	atomic_t count = atomic_dec(&connected_count) - 1;

	LOG_INF("Disconnected. Current count %d", count);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = on_connected,
	.disconnected = on_disconnected,
};

int main(void)
{
	int err;
	bt_addr_le_t addr = bt_addr_le_static_c100(0);

	bst_result = In_progress;

	err = bt_id_create(&addr, NULL);
	__ASSERT_NO_MSG(!err);

	err = bt_enable(NULL);
	__ASSERT_NO_MSG(!err);

	err = bt_le_adv_start(BT_LE_ADV_CONN, NULL, 0, NULL, 0);
	__ASSERT_NO_MSG(!err);

	LOG_INF("Waiting for a connection...");
	while (atomic_get(&connected_count) == 0) {
		k_msleep(1000);
	}

	LOG_INF("Disabling Bluetooth...");
	err = bt_disable();
	__ASSERT_NO_MSG(!err);


	if (atomic_get(&connected_count) != 0) {
		LOG_ERR("Missing disconnected event");
		bst_result = Failed;
		bs_trace_silent_exit(1);
	}

	bst_result = Passed;
	LOG_INF("Test passed");
	bs_trace_silent_exit(0);
}
