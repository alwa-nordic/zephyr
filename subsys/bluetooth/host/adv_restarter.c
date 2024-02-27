#include "zephyr/sys/__assert.h"
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(adv_restarter, LOG_LEVEL_DBG);

#include <stdint.h>
#include <stddef.h>

#define LEGACY_ADV_DATA_MAX_LEN      0x1f
#define LEGACY_SCAN_RSP_DATA_MAX_LEN 0x1f

/* Initialized to zero to disable. */
uint8_t govenor_max_periperals;

/* Storage for deep copy of adverising parameters. */
bt_addr_le_t param_dir_adv_peer;
struct bt_le_adv_param param_copy;
uint8_t copy_ad_serialized[LEGACY_ADV_DATA_MAX_LEN];
uint8_t copy_ad_len;
uint8_t copy_sd_serialized[LEGACY_SCAN_RSP_DATA_MAX_LEN];
uint8_t copy_sd_len;

static void deep_copy_params(const struct bt_le_adv_param *param)
{
	param_copy = *param;

	if (param_copy.peer) {
		param_dir_adv_peer = *param_copy.peer;
		param_copy.peer = &param_dir_adv_peer;
	}
}

static int serialize_data_arr(const struct bt_data *ad, size_t ad_count, uint8_t *buf,
			      size_t buf_size)
{
	size_t pos = 0;

	if (bt_data_get_len(ad, ad_count) > buf_size) {
		return -EFBIG;
	}

	for (size_t i = 0; i < ad_count; i++) {
		pos += bt_data_serialize(&ad[i], &buf[pos]);
	}

	return 0;
}

int bt_adv_restarter_start(const struct bt_le_adv_param *param, const struct bt_data *ad,
			   size_t ad_len, const struct bt_data *sd, size_t sd_len)
{
	int err;

	if (!param) {
		return -EINVAL;
	}

	if (!(param->options & BT_LE_ADV_OPT_ONE_TIME)) {
		return -EINVAL;
	}

	err = serialize_data_arr(ad, ad_len, copy_ad_serialized, sizeof(copy_ad_serialized));
	if (err) {
		__ASSERT_NO_MSG(err = -EFBIG);
		LOG_ERR("Adv data too large for legacy adv");
		return -EFBIG;
	}
	copy_ad_len = ad_len;

	err = serialize_data_arr(sd, sd_len, copy_sd_serialized, sizeof(copy_sd_serialized));
	if (err) {
		__ASSERT_NO_MSG(err = -EFBIG);
		LOG_ERR("Scan response data too large for legacy adv");
		return -EFBIG;
	}
	copy_sd_len = sd_len;

	deep_copy_params(param);

	err = bt_le_adv_start(param, ad, ad_len, sd, sd_len);
	return err;
}

/** @brief Parse the advertising data into a bt_data array.
 *
 *  @note The resulting entries in the bt_data memory borrow
 *  from the input ad_struct.
 */
void bt_data_parse_into(struct bt_data *bt_data, size_t bt_data_count, uint8_t *ad_struct)
{
	size_t pos = 0;
	for (size_t i = 0; i < copy_ad_len; i++) {
		bt_data[i].data_len = ad_struct[pos++];
		bt_data[i].type = ad_struct[pos++];
		bt_data[i].data = &ad_struct[pos];
		pos += bt_data[i].data_len;
	}
}

int try_restart(void)
{
	int err;
	struct bt_data ad[copy_ad_len];
	struct bt_data sd[copy_sd_len];

	bt_data_parse_into(ad, copy_ad_len, copy_ad_serialized);
	bt_data_parse_into(sd, copy_sd_len, copy_sd_serialized);

	err = bt_le_adv_start(&param_copy, ad, copy_ad_len, sd, copy_sd_len);

	switch (err) {
	case -ENOMEM:
	case -ECONNREFUSED:
		/* Retry later */
		break;
	default:
		LOG_ERR("Failed to restart advertising (err %d)", err);
	}

	return err;
}

void on_recycled(void)
{
	if (govenor_max_periperals) {
		try_restart();
	}
}

BT_CONN_CB_DEFINE(conn_cb) = {
	.recycled = on_recycled,
};
