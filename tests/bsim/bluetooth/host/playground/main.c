/* Copyright (c) 2023 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <argparse.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/bluetooth.h>

#include <testlib/adv.h>
#include <testlib/att_read.h>
#include <testlib/att_write.h>
#include "bs_macro.h"
#include "bs_sync.h"
#include <stdint.h>
#include <testlib/conn.h>
#include <testlib/log_utils.h>
#include <testlib/scan.h>
#include <testlib/security.h>

#include <../subsys/bluetooth/common/rpa.h>

/* This test uses system asserts to fail tests. */
BUILD_ASSERT(__ASSERT_ON);

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

void bs_sync_all_log(char *log_msg)
{
	/* Everyone meets here. */
	bt_testlib_bs_sync_all();

	if (get_device_nbr() == 0) {
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

void the_test(void)
{
	int err;
	LOG_INF("Starting test");

	bt_addr_le_t addr;
	uint8_t irk[] = {
		0xff, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8,
		0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0,
	};
	BUILD_ASSERT(sizeof(irk) == 16);

	err = bt_addr_le_from_str("C0:00:00:AB:CD:EF", "random", &addr);
	__ASSERT(err == 0, "bt_addr_le_from_str failed with %d", err);
	__ASSERT(addr.type == BT_ADDR_LE_RANDOM, "addr.type is %d", addr.type);
	__ASSERT(BT_ADDR_IS_STATIC(&addr.a), "addr is not random static");

	int created_identity = bt_id_create(&addr, irk);
	__ASSERT(created_identity == BT_ID_DEFAULT, "created_identity is %d", created_identity);

	bt_enable_quiet();

	/* start an advertiser */
	struct bt_le_adv_param adv_param = {
		.options = BT_LE_ADV_OPT_NONE,
		.interval_min = BT_LE_ADV_INTERVAL_MIN,
		.interval_max = BT_LE_ADV_INTERVAL_MAX,
	};

	for (int i = 0; i < 20; i++) {
		err = bt_le_adv_start(&adv_param, NULL, 0, NULL, 0);
		__ASSERT(err == 0, "bt_le_adv_start failed with %d", err);

		k_sleep(K_MSEC(100));

		err = bt_le_adv_stop();
		__ASSERT(err == 0, "bt_le_adv_stop failed with %d", err);

		k_sleep(K_MSEC(100));
	}

	PASS("Done\n");
}
