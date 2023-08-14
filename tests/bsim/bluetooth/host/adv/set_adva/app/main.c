/* Copyright (c) 2023 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <argparse.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/byteorder.h>

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

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

void log_level_set(char *module, uint32_t new_level)
{
	__ASSERT_NO_MSG(IS_ENABLED(CONFIG_LOG_RUNTIME_FILTERING));
	int source_id = log_source_id_get(module);
	__ASSERT(source_id >= 0, "%d", source_id);
	uint32_t result_level = log_filter_set(NULL, Z_LOG_LOCAL_DOMAIN_ID, source_id, new_level);
	__ASSERT(result_level == new_level, "%u", result_level);
}

#define UUID_1                                                                                     \
	BT_UUID_DECLARE_128(0xdb, 0x1f, 0xe2, 0x52, 0xf3, 0xc6, 0x43, 0x66, 0xb3, 0x92, 0x5d,      \
			    0xc6, 0xe7, 0xc9, 0x59, 0x9d)

#define UUID_2                                                                                     \
	BT_UUID_DECLARE_128(0x3f, 0xa4, 0x7f, 0x44, 0x2e, 0x2a, 0x43, 0x05, 0xab, 0x38, 0x07,      \
			    0x8d, 0x16, 0xbf, 0x99, 0xf1)

struct bt_gatt_attr attrs[] = {
	BT_GATT_PRIMARY_SERVICE(UUID_1),
	BT_GATT_CHARACTERISTIC(UUID_2, BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_NONE, NULL, NULL, NULL),
	BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
};

struct bt_gatt_service svc = {
	.attrs = attrs,
	.attr_count = ARRAY_SIZE(attrs),
};

void find_the_chrc(struct bt_conn *conn, uint16_t *ccc_handle, uint16_t *chrc_value_handle)
{
	uint16_t svc_handle;
	uint16_t svc_end_handle;
	uint16_t chrc_end_handle;

	EZ(bt_testlib_gatt_discover_primary(&svc_handle, &svc_end_handle, conn, UUID_1, 1, 0xffff));

	LOG_INF("svc_handle: %u, svc_end_handle: %u", svc_handle, svc_end_handle);

	EZ(bt_testlib_gatt_discover_characteristic(chrc_value_handle, &chrc_end_handle, NULL, conn,
						   UUID_2, (svc_handle + 1), svc_end_handle));

	LOG_INF("chrc_value_handle: %u, chrc_end_handle: %u", *chrc_value_handle, chrc_end_handle);

	EZ(bt_testlib_att_read_by_type_sync(NULL, NULL, ccc_handle, conn, 0, BT_UUID_GATT_CCC,
					    (*chrc_value_handle + 1), chrc_end_handle));

	LOG_INF("CCC handle: %u", *ccc_handle);
}

uint16_t read_from_the_ccc(struct bt_conn *conn, uint16_t ccc_handle)
{
	NET_BUF_SIMPLE_DEFINE(ccc_val, 2);
	EZ(bt_testlib_att_read_by_handle_sync(&ccc_val, NULL, conn, 0, ccc_handle, 0));
	return sys_get_le16(ccc_val.data);
}

K_SEM_DEFINE(subscribed, 0, 1);
K_SEM_DEFINE(notified, 0, 1);

uint8_t notify(struct bt_conn *conn, struct bt_gatt_subscribe_params *params, const void *data,
	       uint16_t length)
{
	LOG_INF("Notify received");
	k_sem_give(&notified);
	return BT_GATT_ITER_CONTINUE;
}

void subscribe_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_subscribe_params *params)
{
	if (err) {
		LOG_ERR("Subscribe failed (err %d)", err);
		return;
	}

	LOG_INF("Subscribed");
	k_sem_give(&subscribed);
}

static struct bt_gatt_subscribe_params params = {
	.value = BT_GATT_CCC_NOTIFY,
	.notify = notify,
	.subscribe = subscribe_cb,
};

void subscribe(struct bt_conn *conn, uint16_t value_handle, uint16_t ccc_handle)
{
	params.ccc_handle = ccc_handle;
	params.value_handle = value_handle;
	EZ(bt_gatt_subscribe(conn, &params));
	k_sem_take(&subscribed, K_FOREVER);
}

void bt_enable_quiet()
{
	log_level_set("bt_hci_core", LOG_LEVEL_ERR);
	log_level_set("bt_id", LOG_LEVEL_ERR);
	log_level_set("fs_nvs", LOG_LEVEL_ERR);
	EZ(bt_enable(NULL));
	EZ(settings_load());
	log_level_set("bt_hci_core", LOG_LEVEL_INF);
	log_level_set("bt_id", LOG_LEVEL_INF);
	log_level_set("fs_nvs", LOG_LEVEL_INF);
}

/* Special sauce {{{ */

static struct bt_conn *adv_custom_addr_conn = NULL;
static K_SEM_DEFINE(adv_custom_addr_sem, 0, 1);

void adv_custom_addr_cb(struct bt_le_ext_adv *adv, struct bt_le_ext_adv_connected_info *info)
{
	__ASSERT_NO_MSG(adv_custom_addr_conn == NULL);
	adv_custom_addr_conn = bt_conn_ref(info->conn);
	k_sem_give(&adv_custom_addr_sem);
}

static const struct bt_le_ext_adv_cb adv_custom_addr_cb_obj = {
	.connected = adv_custom_addr_cb,
};

void adv_conn_wait_custom_adva(struct bt_conn **conn, const bt_addr_le_t *adva)
{
	struct bt_le_adv_param adv_param;
	struct bt_le_ext_adv *adv;

	__ASSERT_NO_MSG(adva);
	__ASSERT_NO_MSG(conn);
	__ASSERT_NO_MSG(*conn == NULL);

	adv_param = *BT_LE_ADV_CONN_NAME_AD;
	adv_param.options |= BT_LE_ADV_OPT_MANUAL_ADDRESS;
	EZ(bt_le_ext_adv_create(&adv_param, &adv_custom_addr_cb_obj, &adv));
	/* === AdvA is set here. === */
	EZ(bt_le_ext_adv_set_adva(adv, adva));
	EZ(bt_le_ext_adv_start(adv, BT_LE_EXT_ADV_START_DEFAULT));

	EZ(k_sem_take(&adv_custom_addr_sem, K_FOREVER));
	*conn = adv_custom_addr_conn;
	adv_custom_addr_conn = NULL;

	EZ(bt_le_ext_adv_delete(adv));
}

/* }}} */

void play(bool central, bool peripheral)
{
	bt_addr_le_t adva_special;
	bt_addr_le_t adva_special_found;
	bt_addr_le_t adva_other;
	struct bt_gatt_subscribe_params;
	struct bt_conn *conn_other = NULL;
	struct bt_conn *conn_special = NULL;
	uint16_t ccc_handle = 0;
	uint16_t chrc_value_handle = 0;

	EZ(bt_addr_le_from_str("C2:34:56:78:9A:FF", "random", &adva_special));
	__ASSERT_NO_MSG(BT_ADDR_IS_STATIC(&adva_special.a));

	if (peripheral) {
		EZ(bt_gatt_service_register(&svc));
	}

	bt_enable_quiet();

	if (peripheral) {
		EZ(bt_set_name("peripheral"));
		adv_conn_wait_custom_adva(&conn_special, &adva_special);
	}
	if (central) {
		EZ(bt_testlib_scan_find_name(&adva_special_found, "peripheral"));
		EZ(bt_testlib_connect(&adva_special_found, &conn_special));
		bt_set_bondable(false);
		EZ(bt_testlib_secure(conn_special, BT_SECURITY_L2));
	}

	bt_testlib_bs_sync();
	if (central) {
		LOG_INF("Special connection paired");
	}
	bt_testlib_bs_sync();

	if (peripheral) {
		EZ(bt_testlib_adv_conn(&conn_other, BT_ID_DEFAULT,
				       (BT_LE_ADV_OPT_USE_NAME | BT_LE_ADV_OPT_FORCE_NAME_IN_AD)));
		bt_testlib_wait_connected(conn_other);
	}

	if (central) {
		EZ(bt_testlib_scan_find_name(&adva_other, "peripheral"));
		EZ(bt_testlib_connect(&adva_other, &conn_other));

		bt_set_bondable(true);
		EZ(bt_testlib_secure(conn_other, BT_SECURITY_L2));
	}

	bt_testlib_bs_sync();
	if (central) {
		LOG_INF("Other connection bonded");
	}
	bt_testlib_bs_sync();

	if (central) {
		find_the_chrc(conn_other, &ccc_handle, &chrc_value_handle);
		subscribe(conn_other, chrc_value_handle, ccc_handle);
	}

	bt_testlib_bs_sync();
	if (central) {
		LOG_INF("Subscribed to notifications");
	}
	bt_testlib_bs_sync();

	if (peripheral) {
		EZ(bt_gatt_notify(NULL, &svc.attrs[2], NULL, 0));
	}
	if (central) {
		k_sem_take(&notified, K_FOREVER);
	}

	bt_testlib_bs_sync();
	PASS("\n");
}

void the_test(void)
{
	bool central = get_device_nbr() == 0;
	bool peripheral = get_device_nbr() == 1;
	play(central, peripheral);
}
