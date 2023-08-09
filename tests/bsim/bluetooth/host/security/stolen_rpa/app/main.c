/* Copyright (c) 2023 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <argparse.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/settings/settings.h>

#include "testlib/bs_macro.h"
#include "testlib/bs_sync.h"
#include "testlib/adv.h"
#include "testlib/connect.h"
#include "testlib/scan.h"
#include "testlib/security.h"
#include "testlib/conn_ref.h"
#include "testlib/att_read.h"
#include "testlib/att_write.h"
#include "testlib/conn_wait.h"

#define OK(err) __ASSERT_NO_MSG(!err)

#define __ASSERT_OH_NO_MSG(cond) __ASSERT((cond), "womp womp")

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

void log_level_set(char *module, uint32_t new_level)
{
	__ASSERT_NO_MSG(IS_ENABLED(CONFIG_LOG_RUNTIME_FILTERING));
	int source_id = log_source_id_get(module);
	__ASSERT(source_id >= 0, "%d", source_id);
	uint32_t result_level = log_filter_set(NULL, Z_LOG_LOCAL_DOMAIN_ID, source_id, new_level);
	__ASSERT(result_level == new_level, "%u", result_level);
}

/* Ad-hoc randomized UUID. The value is not significant. */
#define OVERUSED_UUID_1                                                                            \
	BT_UUID_DECLARE_128(0xdb, 0x1f, 0xe2, 0x52, 0xf3, 0xc6, 0x43, 0x66, 0xb3, 0x92, 0x5d,      \
			    0xc6, 0xe7, 0xc9, 0x59, 0x9d)

/* Ad-hoc randomized UUID. The value is not significant. */
#define OVERUSED_UUID_2                                                                            \
	BT_UUID_DECLARE_128(0x3f, 0xa4, 0x7f, 0x44, 0x2e, 0x2a, 0x43, 0x05, 0xab, 0x38, 0x07,      \
			    0x8d, 0x16, 0xbf, 0x99, 0xf1)

struct bt_gatt_attr attrs[] = {
	BT_GATT_PRIMARY_SERVICE(OVERUSED_UUID_1),
	BT_GATT_CHARACTERISTIC(OVERUSED_UUID_2, BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_NONE, NULL, NULL,
			       NULL),
	BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
};

struct bt_gatt_service svc = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
};

uint16_t find_the_ccc(struct bt_conn *conn)
{
	int err;
	uint16_t svc_handle;
	uint16_t svc_end_handle;
	err = bt_testlib_gatt_discover_service(&svc_handle, &svc_end_handle, conn, 0,
					       BT_GATT_DISCOVER_PRIMARY, OVERUSED_UUID_1, 1,
					       0xffff);
	OK(err);
	LOG_ERR("svc_handle: %u, svc_end_handle: %u", svc_handle, svc_end_handle);
	uint16_t chrc_value_handle;
	uint16_t chrc_end_handle;
	err = bt_testlib_gatt_discover_characteristic(&chrc_value_handle, &chrc_end_handle, NULL,
						      conn, 0, OVERUSED_UUID_2, (svc_handle + 1),
						      svc_end_handle);
	OK(err);
	LOG_ERR("chrc_value_handle: %u, chrc_end_handle: %u", chrc_value_handle, chrc_end_handle);
	uint16_t ccc_handle;
	err = bt_testlib_att_read_by_type_sync(NULL, NULL, &ccc_handle, conn, 0, BT_UUID_GATT_CCC,
					       (chrc_value_handle + 1), chrc_end_handle);
	OK(err);
	LOG_ERR("CCC handle: %u", ccc_handle);
	return ccc_handle;
}

void write_to_the_ccc(struct bt_conn *conn, uint16_t ccc_handle, uint8_t flags)
{
	int err;
	uint8_t new_ccc_val[] = {flags, 0x00};
	err = bt_testlib_att_write(conn, 0, ccc_handle, new_ccc_val, sizeof(new_ccc_val));
	OK(err);
}

uint8_t read_from_the_ccc(struct bt_conn *conn, uint16_t ccc_handle)
{
	int err;
	NET_BUF_SIMPLE_DEFINE(ccc_val, sizeof(2));

	err = bt_testlib_att_read_by_handle_sync(&ccc_val, NULL, conn, 0, ccc_handle, 0);
	__ASSERT_NO_MSG(ccc_val.len == 2);
	OK(err);

	return ccc_val.data[0];
}

void play(bool alice, bool bob, bool eve)
{
	bt_addr_le_t bobs_adva;
	int err;
	struct bt_conn *conn = NULL;
	struct bt_conn_info conn_info;
	struct bt_le_ext_adv *adv = NULL;
	char str[BT_ADDR_LE_STR_LEN];
	uint16_t ccc_handle = 0;

	if (get_device_nbr() == 0) {
		LOG_ERR("#####################  Act 1: The setup");
	}
	bt_testlib_bs_sync();

	if (alice) {
		LOG_INF("I'm Alice");
	}
	if (bob) {
		LOG_INF("I'm Bob");
	}
	if (eve) {
		LOG_INF("I'm Eve");
	}

	if (alice) {
		err = bt_gatt_service_register(&svc);
		OK(err);
	}

	log_level_set("bt_hci_core", LOG_LEVEL_ERR);
	log_level_set("bt_id", LOG_LEVEL_ERR);
	err = bt_enable(NULL);
	OK(err);
	err = settings_load();
	OK(err);
	log_level_set("bt_hci_core", LOG_LEVEL_INF);
	log_level_set("bt_id", LOG_LEVEL_INF);

	if (bob) {
		// Step 0: Bob and Eve set names to "bob".
		err = bt_set_name("bob");
		OK(err);

		err = bt_testlib_adv_conn(
			&adv, &conn, BT_ID_DEFAULT, BT_ADDR_LE_ANY,
			(BT_LE_ADV_OPT_USE_NAME | BT_LE_ADV_OPT_FORCE_NAME_IN_AD));
		OK(err);
	}
	if (alice || eve) {
		err = bt_testlib_scan_find_name(&bobs_adva, "bob");
		OK(err);
	}

	/* Bob is still adverising. */

	if (get_device_nbr() == 0) {
		LOG_ERR("#####################  Act 2: The bonding");
	}
	bt_testlib_bs_sync();

	if (alice) {
		err = bt_testlib_connect(&bobs_adva, &conn);
		OK(err);
		err = bt_testlib_secure(conn, BT_SECURITY_L2);
		OK(err);
		err = bt_conn_get_info(conn, &conn_info);
		OK(err);
		(void)bt_addr_le_to_str(conn_info.le.dst, str, ARRAY_SIZE(str));
		LOG_INF("I think I'm talking to: %s", str);
	}
	if (bob) {
		bt_testlib_wait_connected(conn);
		err = bt_le_ext_adv_delete(adv);
		adv = NULL;
		OK(err);
	}
	bt_testlib_bs_sync();

	if (bob) {
		ccc_handle = find_the_ccc(conn);
		write_to_the_ccc(conn, ccc_handle, 1);
	}
	bt_testlib_bs_sync();

	if (alice) {
		err = bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		OK(err);
		bt_testlib_conn_unref(&conn);
	}
	if (bob) {
		bt_testlib_wait_disconnected(conn);
		bt_testlib_conn_unref(&conn);
	}

	if (get_device_nbr() == 0) {
		LOG_ERR("#####################  Act 3: The deception");
	}
	bt_testlib_bs_sync();

	if (eve) {
		err = bt_testlib_adv_conn(
			&adv, &conn, BT_ID_DEFAULT, &bobs_adva,
			(BT_LE_ADV_OPT_USE_NAME | BT_LE_ADV_OPT_FORCE_NAME_IN_AD));
		OK(err);
		bt_testlib_wait_connected(conn);
		err = bt_le_ext_adv_delete(adv);
		adv = NULL;
		OK(err);
	}
	if (alice) {
		err = bt_testlib_connect(&bobs_adva, &conn);
		OK(err);
		err = bt_conn_get_info(conn, &conn_info);
		OK(err);
		(void)bt_addr_le_to_str(conn_info.le.dst, str, ARRAY_SIZE(str));
		LOG_INF("I think I'm talking to: %s", str);
	}
	bt_testlib_bs_sync();
	if (eve) {
		ccc_handle = find_the_ccc(conn);
		write_to_the_ccc(conn, ccc_handle, 3);
	}
	bt_testlib_bs_sync();

	if (alice) {
		err = bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		OK(err);
		bt_testlib_wait_disconnected(conn);
		bt_testlib_conn_unref(&conn);

	}
	if (eve) {
		bt_testlib_wait_disconnected(conn);
		bt_testlib_conn_unref(&conn);
	}

	if (get_device_nbr() == 0) {
		LOG_ERR("#####################  Act 4: The aftermath");
	}
	bt_testlib_bs_sync();

	if (bob) {
		err = bt_testlib_adv_conn(
			&adv, &conn, BT_ID_DEFAULT, BT_ADDR_LE_ANY,
			(BT_LE_ADV_OPT_USE_NAME | BT_LE_ADV_OPT_FORCE_NAME_IN_AD));
		OK(err);
		bt_testlib_wait_connected(conn);
		err = bt_le_ext_adv_delete(adv);
		adv = NULL;
		OK(err);
	}
	if (alice) {
		err = bt_testlib_connect(&bobs_adva, &conn);
		OK(err);
		err = bt_conn_get_info(conn, &conn_info);
		OK(err);
		(void)bt_addr_le_to_str(conn_info.le.dst, str, ARRAY_SIZE(str));
		LOG_INF("I think I'm talking to: %s", str);
	}
	bt_testlib_bs_sync();
	if (bob) {
		uint8_t unsecured = read_from_the_ccc(conn, ccc_handle);
		LOG_ERR("unsecured: %u", unsecured);
		err = bt_testlib_secure(conn, BT_SECURITY_L2);
		OK(err);
		uint8_t final = read_from_the_ccc(conn, ccc_handle);
		LOG_ERR("final: %u", final);
		__ASSERT_OH_NO_MSG(unsecured == 0);
		__ASSERT_OH_NO_MSG(final == 1);
	}
}

void the_test(void)
{
	bool alice = get_device_nbr() == 0;
	bool bob = get_device_nbr() == 1;
	bool eve = get_device_nbr() == 2;
	play(alice, bob, eve);
}
