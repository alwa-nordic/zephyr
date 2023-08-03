/* Copyright (c) 2023 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/logging/log.h>

#include "../common_defs.h"

#include "../testlib/att_read.h"
#include "../testlib/bs_macro.h"
#include "../testlib/connect.h"
#include "../testlib/scan.h"
#include "../testlib/security.h"

LOG_MODULE_REGISTER(d0, LOG_LEVEL_DBG);


static int bond_deleted_call_count = 0;
static void bond_deleted(uint8_t id, const bt_addr_le_t *peer)
{
	bond_deleted_call_count++;
	LOG_INF("Bond deleted :) %d", id);
}

struct bt_conn_auth_info_cb auth_info_cb = {
	.bond_deleted = bond_deleted,
};

void the_test(void)
{
	bt_addr_le_t scan_result;
	int err;
	struct bt_conn *conn = NULL;

	err = bt_conn_auth_info_cb_register(&auth_info_cb);
	__ASSERT(!err, "err %d", err);


	LOG_INF("Bonding with d1.");

	err = bt_enable(NULL);
	__ASSERT(!err, "err %d", err);

	err = bt_testlib_scan_find_name(&scan_result, "d1");
	__ASSERT(!err, "err %d", err);

	err = bt_testlib_connect(&scan_result, &conn);
	__ASSERT(!err, "err %d", err);

	err = bt_testlib_secure(conn, BT_SECURITY_L2);
	__ASSERT(!err, "err %d", err);

	err = bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	__ASSERT(!err, "err %d", err);

	bt_conn_unref(conn);
	conn = NULL;


	LOG_INF("Bonding with d2.");

	err = bt_testlib_scan_find_name(&scan_result, "d2");
	__ASSERT(!err, "err %d", err);

	err = bt_testlib_connect(&scan_result, &conn);
	__ASSERT(!err, "err %d", err);

	__ASSERT_NO_MSG(bond_deleted_call_count == 0);

	err = bt_testlib_secure(conn, BT_SECURITY_L2);
	__ASSERT(!err, "err %d", err);

	__ASSERT_NO_MSG(bond_deleted_call_count == 1);

	err = bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	__ASSERT(!err, "err %d", err);

	bt_conn_unref(conn);
	conn = NULL;

	PASS("PASS\n");
}
