/*
 * Copyright (c) 2024 Croxel, Inc.
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/services/nus.h>

#define DEVICE_NAME     CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_SRV_VAL),
};

static K_SEM_DEFINE(wakeup_adv_starter, 0, 1);

static void on_conn_connected(struct bt_conn *conn, uint8_t err)
{
	k_sem_give(&wakeup_adv_starter);
}

static void on_conn_recycled(void)
{
	k_sem_give(&wakeup_adv_starter);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = on_conn_connected,
	.recycled = on_conn_recycled,
};

static struct k_thread app_bt_loop;

static K_THREAD_STACK_DEFINE(app_bt_loop_stack, CONFIG_MAIN_STACK_SIZE);

static void app_bt_loop_entry(void *p1, void *p2, void *p3)
{
	for (;;) {
		int err;

		err = bt_le_adv_start(BT_LE_ADV_CONN_ONE_TIME, ad, ARRAY_SIZE(ad), sd,
				      ARRAY_SIZE(sd));
		__ASSERT_NO_MSG(!err || err == -EALREADY || err == -ENOMEM);

		k_sem_take(&wakeup_adv_starter, K_FOREVER);
	}
}

static int bt_nus_auto_start(void)
{
	int err;

	err = bt_enable(NULL);
	__ASSERT_NO_MSG(!err);

	k_thread_name_set(&app_bt_loop, "app_bt_loop");
	k_thread_create(&app_bt_loop, app_bt_loop_stack, K_THREAD_STACK_SIZEOF(app_bt_loop_stack),
			app_bt_loop_entry, NULL, NULL, NULL, CONFIG_MAIN_THREAD_PRIORITY, 0,
			K_NO_WAIT);

	return 0;
}

SYS_INIT(bt_nus_auto_start, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
