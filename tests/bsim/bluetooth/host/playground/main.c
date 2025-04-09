/* Copyright (c) 2025 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <argparse.h>
#include <bs_tracing.h>
#include <bsim_args_runner.h>
#include <stddef.h>
#include <stdint.h>
#include <testlib/adv.h>
#include <testlib/att_read.h>
#include <testlib/att_write.h>
#include <testlib/conn.h>
#include <testlib/log_utils.h>
#include <testlib/scan.h>
#include <testlib/security.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/logging/log_core.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>

#include "bs_macro.h"
#include "bs_sync.h"

/* This test uses system asserts to fail tests. */
BUILD_ASSERT(__ASSERT_ON);

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

void bs_sync_all_log(char *log_msg)
{
	/* Everyone meets here. */
	bt_testlib_bs_sync_all();

	if (bsim_args_get_global_device_nbr() == 0) {
		LOG_WRN("Sync point: %s", log_msg);
	}

	/* Everyone waits for d0 to finish logging. */
	bt_testlib_bs_sync_all();
}

static inline void bt_enable_quiet(void)
{
	bt_testlib_log_level_set("bt_hci_core", LOG_LEVEL_ERR);
	bt_testlib_log_level_set("bt_id", LOG_LEVEL_ERR);

	EXPECT_ZERO(bt_enable(NULL));

	bt_testlib_log_level_set("bt_hci_core", LOG_LEVEL_INF);
	bt_testlib_log_level_set("bt_id", LOG_LEVEL_INF);
}

/* Just some UUID. The value is not significant. */
#define UUID_1                                                                                     \
	BT_UUID_DECLARE_128(0xdb, 0x1f, 0xe2, 0x52, 0xf3, 0xc6, 0x43, 0x66, 0xb3, 0x92, 0x5d,      \
			    0xc6, 0xe7, 0xc9, 0x59, 0x9d)

/* Just some UUID. The value is not significant. */
#define UUID_2                                                                                     \
	BT_UUID_DECLARE_128(0x3f, 0xa4, 0x7f, 0x44, 0x2e, 0x2a, 0x43, 0x05, 0xab, 0x38, 0x07,      \
			    0x8d, 0x16, 0xbf, 0x99, 0xf1)

static struct bt_gatt_attr attrs[] = {
	BT_GATT_PRIMARY_SERVICE(UUID_1),
};

static struct bt_gatt_service sample_service_1 = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
};

static struct bt_gatt_attr attrs_2[] = {
	BT_GATT_PRIMARY_SERVICE(UUID_2),
};

static struct bt_gatt_service sample_service_2 = {
	.attrs = attrs_2,
	.attr_count = ARRAY_SIZE(attrs_2),
};

static void playground_dut(void)
{
	struct bt_conn *conn = NULL;
	int err;

	bt_enable_quiet();

	bt_gatt_service_register(&sample_service_1);

	err = bt_testlib_adv_conn(&conn, BT_ID_DEFAULT, "foo");
	EXPECT_ZERO(err);

	bs_sync_all_log("Connected");

	bs_sync_all_log("Service 1 discovered");

	err = bt_gatt_service_unregister(&sample_service_1);
	EXPECT_ZERO(err);

	err = bt_gatt_service_register(&sample_service_2);
	EXPECT_ZERO(err);
	
	bs_sync_all_log("After service change");

	PASS("DUT done\n");
}

static void playground_tester(void)
{
	struct bt_conn *conn = NULL;
	int err;
	bt_addr_le_t dut_addr;
	uint16_t result_handle_1;
	uint16_t result_handle_2;

	bt_enable_quiet();

	err = bt_testlib_scan_find_name(&dut_addr, "foo");
	EXPECT_ZERO(err);

	err = bt_testlib_connect(&dut_addr, &conn);
	EXPECT_ZERO(err);

	bs_sync_all_log("Connected");

	err = bt_testlib_gatt_discover_primary(&result_handle_1, NULL, conn, UUID_1, 1, 0xffff);
	EXPECT_ZERO(err);

	LOG_INF("Found service 1 on handle %u", result_handle_1);
	
	bs_sync_all_log("Service 1 discovered");
	bs_sync_all_log("After service change");

	err = bt_testlib_gatt_discover_primary(&result_handle_2, NULL, conn, UUID_2, 1, 0xffff);
	EXPECT_ZERO(err);

	LOG_INF("Found service 2 on handle %u", result_handle_2);
	
	if (result_handle_1 == result_handle_2) {
		LOG_ERR("Service handles. That's illegal!");
	} else {
		PASS("Tester done\n");
	}

	bs_trace_exit(0);
}

void test_main(void)
{
	int dev_nbr = bsim_args_get_global_device_nbr();

	if (bt_testlib_bs_device_count() != 2) {
		LOG_ERR("This test requires exactly 2 devices");
		return;
	}

	if (dev_nbr == 0) {
		playground_dut();
	} else if (dev_nbr == 1) {
		playground_tester();
	}
}
