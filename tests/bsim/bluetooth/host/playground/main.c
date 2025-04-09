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

	/* First identity */
	bt_addr_le_t addr1;
	uint8_t irk1[] = {
		0xff, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8,
		0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0,
	};
	BUILD_ASSERT(sizeof(irk1) == 16);

	/* Second identity */
	bt_addr_le_t addr2;
	uint8_t irk2[] = {
		0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
		0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff, 0xe0,
	};
	BUILD_ASSERT(sizeof(irk2) == 16);

	/* Set up first identity */
	err = bt_addr_le_from_str("C0:00:00:AB:CD:EF", "random", &addr1);
	__ASSERT(err == 0, "bt_addr_le_from_str failed with %d", err);
	__ASSERT(addr1.type == BT_ADDR_LE_RANDOM, "addr1.type is %d", addr1.type);
	__ASSERT(BT_ADDR_IS_STATIC(&addr1.a), "addr1 is not random static");
	
	/* Set up second identity */
	err = bt_addr_le_from_str("D0:00:00:12:34:56", "random", &addr2);
	__ASSERT(err == 0, "bt_addr_le_from_str failed with %d", err);
	__ASSERT(addr2.type == BT_ADDR_LE_RANDOM, "addr2.type is %d", addr2.type);
	__ASSERT(BT_ADDR_IS_STATIC(&addr2.a), "addr2 is not random static");
	
	bt_enable_quiet();

	/* Create first identity */
	int created_identity1 = bt_id_create(&addr1, irk1);
	LOG_INF("created_identity1: %d", created_identity1);
	__ASSERT(created_identity1 != BT_ID_DEFAULT, "created_identity1 is %d", created_identity1);

	/* Create second identity */
	int created_identity2 = bt_id_create(&addr2, irk2);
	LOG_INF("created_identity2: %d", created_identity2);
	__ASSERT(created_identity2 != BT_ID_DEFAULT, "created_identity2 is %d", created_identity2);
	__ASSERT(created_identity1 != created_identity2, "Identities should be different");

	/* Create first extended advertiser */
	struct bt_le_adv_param adv_param1 = {
		.id = created_identity1,
		.options = BT_LE_ADV_OPT_EXT_ADV,  /* Enable extended advertising */
		.interval_min = BT_LE_ADV_INTERVAL_MIN,
		.interval_max = BT_LE_ADV_INTERVAL_MAX,
	};

	struct bt_le_ext_adv *adv1;
	/* Create first advertising set */
	err = bt_le_ext_adv_create(&adv_param1, NULL, &adv1);
	__ASSERT(err == 0, "bt_le_ext_adv_create for adv1 failed with %d", err);

	/* Create second extended advertiser */
	struct bt_le_adv_param adv_param2 = {
		.id = created_identity2,
		.options = BT_LE_ADV_OPT_EXT_ADV,  /* Enable extended advertising */
		.interval_min = BT_LE_ADV_INTERVAL_MIN,
		.interval_max = BT_LE_ADV_INTERVAL_MAX,
	};

	struct bt_le_ext_adv *adv2;
	/* Create second advertising set */
	err = bt_le_ext_adv_create(&adv_param2, NULL, &adv2);
	__ASSERT(err == 0, "bt_le_ext_adv_create for adv2 failed with %d", err);

	/* Configure advertising start parameters */
	struct bt_le_ext_adv_start_param start_param = {
		.timeout = 0,    /* No timeout */
		.num_events = 0, /* No limit on events */
	};

	/* Update advertising parameters for both advertisers */
	err = bt_le_ext_adv_update_param(adv1, &adv_param1);
	__ASSERT(err == 0, "bt_le_ext_adv_update_param for adv1 failed with %d", err);

	err = bt_le_ext_adv_update_param(adv2, &adv_param2);
	__ASSERT(err == 0, "bt_le_ext_adv_update_param for adv2 failed with %d", err);


	for (int i = 0; i < 10; i++) {
		/* Start first extended advertising */
		err = bt_le_ext_adv_start(adv1, &start_param);
		__ASSERT(err == 0, "bt_le_ext_adv_start for adv1 failed with %d", err);

		/* Start second extended advertising */
		err = bt_le_ext_adv_start(adv2, &start_param);
		__ASSERT(err == 0, "bt_le_ext_adv_start for adv2 failed with %d", err);

		k_sleep(K_MSEC(100));

		/* Stop both extended advertising */
		err = bt_le_ext_adv_stop(adv1);
		__ASSERT(err == 0, "bt_le_ext_adv_stop for adv1 failed with %d", err);

		err = bt_le_ext_adv_stop(adv2);
		__ASSERT(err == 0, "bt_le_ext_adv_stop for adv2 failed with %d", err);

		k_sleep(K_MSEC(100));
	}

	/* Delete both advertising sets when done */
	err = bt_le_ext_adv_delete(adv1);
	__ASSERT(err == 0, "bt_le_ext_adv_delete for adv1 failed with %d", err);

	err = bt_le_ext_adv_delete(adv2);
	__ASSERT(err == 0, "bt_le_ext_adv_delete for adv2 failed with %d", err);

	PASS("Done\n");
}
