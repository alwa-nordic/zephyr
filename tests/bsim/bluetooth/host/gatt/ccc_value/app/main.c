/* Copyright (c) 2023 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <argparse.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/__assert.h>

#include "testlib/bs_macro.h"
#include "testlib/adv.h"
#include "testlib/connect.h"
#include "testlib/scan.h"
#include "testlib/security.h"

LOG_MODULE_REGISTER(test, LOG_LEVEL_DBG);

static void test_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	LOG_INF("test_ccc_changed value %u", value);
}

/* Ad-hoc randomized UUID. The value is not significant. */
#define TEST_SVC                                                                                   \
	BT_UUID_DECLARE_128(0x81, 0xd3, 0xe6, 0xfd, 0xeb, 0xea, 0x4c, 0x4c, 0x80, 0x69, 0xd1,      \
			    0x1d, 0x47, 0x5f, 0xda, 0x44)

/* Ad-hoc randomized UUID. The value is not significant. */
#define TEST_CHRC                                                                                  \
	BT_UUID_DECLARE_128(0xca, 0x2f, 0xc1, 0x0b, 0x23, 0x0f, 0x4f, 0xfb, 0xb2, 0xfa, 0x4f,      \
			    0xcc, 0xee, 0x32, 0x00, 0xca)

BT_GATT_SERVICE_DEFINE(long_attr_svc, BT_GATT_PRIMARY_SERVICE(TEST_SVC),
		       BT_GATT_CHARACTERISTIC(TEST_CHRC, BT_GATT_CHRC_READ, BT_GATT_PERM_READ, NULL,
					      NULL, NULL),
		       BT_GATT_CCC(test_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));

/* The GATT server device.
 */
void d0(void)
{
	int err;

	err = bt_set_name("d0");
	__ASSERT_NO_MSG(!err);

	err = bt_enable(NULL);
	__ASSERT_NO_MSG(!err);

	err = bt_testlib_adv_conn(NULL, BT_ID_DEFAULT,
				  (BT_LE_ADV_OPT_USE_NAME | BT_LE_ADV_OPT_FORCE_NAME_IN_AD));
	__ASSERT_NO_MSG(!err);
}

/* Client 1.
 */
void d1(void)
{
	bt_addr_le_t scan_result;
	int err;
	struct bt_conn *conn = NULL;

	err = bt_enable(NULL);
	__ASSERT_NO_MSG(!err);

	err = bt_testlib_scan_find_name(&scan_result, "d0");
	__ASSERT_NO_MSG(!err);

	err = bt_testlib_connect(&scan_result, &conn);
	__ASSERT_NO_MSG(!err);
}

void the_test(void)
{
	switch (get_device_nbr()) {
	case 0:
		d0();
	case 1:
		d1();
	default:
		__ASSERT_NO_MSG(false);
	}

	PASS("PASS\n");
}
