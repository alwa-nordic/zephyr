/* Copyright (c) 2024 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <argparse.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/bluetooth.h>

#include <testlib/adv.h>
#include <testlib/att_read.h>
#include <testlib/att_write.h>
#include <testlib/conn.h>
#include <testlib/log_utils.h>
#include <testlib/scan.h>
#include <testlib/security.h>

#include "bs_macro.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

static inline void bt_enable_quiet(void)
{
	bt_testlib_log_level_set("bt_hci_core", LOG_LEVEL_ERR);
	bt_testlib_log_level_set("bt_id", LOG_LEVEL_ERR);

	EXPECT_ZERO(bt_enable(NULL));

	bt_testlib_log_level_set("bt_hci_core", LOG_LEVEL_INF);
	bt_testlib_log_level_set("bt_id", LOG_LEVEL_INF);
}

/* This test uses system asserts to fail tests. */
BUILD_ASSERT(__ASSERT_ON);

void the_test(void)
{
	LOG_INF("Starting test");

	bt_enable_quiet();

	//bt_addr_le_t adva;
	//EXPECT_ZERO(bt_testlib_scan_find_name(&adva, "peripheral"));
	//LOG_INF("Found advertiser");

	PASS("Test complete\n");
}
