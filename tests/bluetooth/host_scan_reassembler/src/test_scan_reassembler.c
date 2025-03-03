/* Copyright (c) 2025 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/kernel/thread.h>
#include <zephyr/net_buf.h>

#include <zephyr/logging/log.h>

#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>
#include <zephyr/ztest_test.h>

#include <../subsys/bluetooth/host/scan_reassembler.c>

LOG_MODULE_REGISTER(test, LOG_LEVEL_DBG);

/* Pool used for simulating HCI advertising reports. */
NET_BUF_POOL_FIXED_DEFINE(test_pool, 10, 255, 0, NULL);
struct bt_scan_reassembler_state test_state;

ZTEST_SUITE(bt_host_scan_reassembler, NULL, NULL, NULL, NULL, NULL);
ZTEST(bt_host_scan_reassembler, test_foobar)
{
}
