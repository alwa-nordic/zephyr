/* Copyright (c) 2023 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/__assert.h>


#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(testlib_adv, LOG_LEVEL_INF);

int bt_testlib_adv_conn(struct bt_le_ext_adv **adv, struct bt_conn **conn, int id, const bt_addr_le_t *adva, uint32_t adv_options)
{
	__ASSERT_NO_MSG(adv);
	__ASSERT_NO_MSG(*adv == NULL);
	__ASSERT_NO_MSG(!conn || *conn == NULL);
	int api_err;
	struct bt_le_adv_param param = {};
	struct bt_le_ext_adv_start_param start_param = BT_LE_EXT_ADV_START_DEFAULT[0];
	start_param.conn = conn;

	param.id = id;
	param.interval_min = BT_GAP_ADV_FAST_INT_MIN_1;
	param.interval_max = BT_GAP_ADV_FAST_INT_MAX_1;
	param.options |= BT_LE_ADV_OPT_CONNECTABLE;
	param.options |= adv_options;

	api_err = bt_le_ext_adv_create(&param, NULL, adv);

	if (!api_err && !bt_addr_le_eq(adva, BT_ADDR_LE_ANY)) {
		LOG_WRN("Pressing x to hack");
		int err = bt_le_ext_adv_set_adva(*adv, adva);
		__ASSERT_NO_MSG(!err);
	}

	if (!api_err) {
		api_err = bt_le_ext_adv_start(*adv, &start_param);
	}

	return api_err;
}
