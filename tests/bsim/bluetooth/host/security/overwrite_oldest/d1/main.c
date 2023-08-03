/* Copyright (c) 2023 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <argparse.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/sys/byteorder.h>

#include "../testlib/bs_macro.h"
#include "../testlib/adv.h"

#include "../common_defs.h"

LOG_MODULE_REGISTER(d0, LOG_LEVEL_DBG);

void the_test(void)
{
	int err;

	err = bt_enable(NULL);
	__ASSERT_NO_MSG(!err);

	__ASSERT_NO_MSG(get_device_nbr() == 1);
	err = bt_set_name("d1");
	__ASSERT_NO_MSG(!err);

	err = bt_testlib_adv_conn(NULL, BT_ID_DEFAULT,
				  (BT_LE_ADV_OPT_USE_NAME | BT_LE_ADV_OPT_FORCE_NAME_IN_AD));
	__ASSERT_NO_MSG(!err);

	PASS("PASS\n");
}
