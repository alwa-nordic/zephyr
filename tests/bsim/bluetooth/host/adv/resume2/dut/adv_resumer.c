/* Copyright (c) 2024 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <bs_tracing.h>
#include <bstests.h>
#include <stdint.h>
#include <testlib/conn.h>
#include <testlib/scan.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/sys/atomic_builtin.h>
#include <zephyr/sys/atomic_types.h>

#include "adv_resumer.h"

LOG_MODULE_REGISTER(bt_conn_evt_sub, LOG_LEVEL_INF);

static void handler(struct k_work *work);
static struct k_work work = {.handler = handler};

static K_MUTEX_DEFINE(sync);

/* Initialized to NULL, which means restarting is disabled. */
static bt_conn_evt_sub_t *g_func;
static enum bt_conn_evt_bits g_enabled;

void bt_conn_evt_sub_set(bt_conn_evt_sub_t *func, enum bt_conn_evt_bits enabled)
{
	k_mutex_lock(&sync, K_FOREVER);
	g_func = func;
	g_enabled = enabled;
	k_mutex_unlock(&sync);

	if (func && (enabled & BT_NOW)) {
		k_work_submit(&work);
	}
}

static void handler(struct k_work *self)
{
	int err;
	//bt_conn_evt_sub_set(NULL, BT_CONNECTED | BT_RECYCLED);

	/* The timeout is defence-in-depth. The lock has a dependency
	 * the blocking Bluetooth API. This can form a deadlock if the
	 * Bluetooth API happens to have a dependency on the
	 * work queue.
	 *
	 * The timeout is not zero to avoid busy-waiting.
	 */
	err = k_mutex_lock(&sync, K_MSEC(1));
	if (err) {
		LOG_DBG("reshed");
		k_work_submit(self);

		/* We did not get the lock. */
		return;
	}

	if (g_func) {
		g_func();
	}

	k_mutex_unlock(&sync);
}

static void trigger(enum bt_conn_evt_bits evt) {

	bt_conn_evt_sub_t *func;
	enum bt_conn_evt_bits enabled;

	k_mutex_lock(&sync, K_FOREVER);
	func = g_func;
	enabled = g_enabled;
	k_mutex_unlock(&sync);

	if (func && (enabled & trigger)) {
		k_work_submit(&work);
	}
}

static void on_conn_connected(struct bt_conn *conn, uint8_t conn_err)
{

}

static void on_conn_recycled(void)
{
	bt_conn_evt_sub_t *func;
	enum bt_conn_evt_bits enabled;

	k_mutex_lock(&sync, K_FOREVER);
	func = g_func;
	enabled = g_enabled;
	k_mutex_unlock(&sync);

	if (func && (enabled & BT_RECYCLED)) {
		k_work_submit(&work);
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = on_conn_connected,
	.recycled = on_conn_recycled,
};
