#include <zephyr/sys/__assert.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(adv_restarter, LOG_LEVEL_DBG);

#include <stdint.h>
#include <stddef.h>

#define LEGACY_ADV_DATA_MAX_LEN      0x1f
#define LEGACY_SCAN_RSP_DATA_MAX_LEN 0x1f

static void restart_work_handler(struct k_work *work);
struct k_work restart_work = {.handler = restart_work_handler};

static K_MUTEX_DEFINE(globals_lock);

/* Initialized to zero, which means restarting is disabled. */
uint8_t restart_govenor_max_periperals;

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

static bool ad_is_limited(const struct bt_data *ad, size_t ad_len)
{
	size_t i;

	for (i = 0; i < ad_len; i++) {
		if (ad[i].type == BT_DATA_FLAGS && ad[i].data_len == sizeof(uint8_t) &&
		    ad[i].data != NULL) {
			if (ad[i].data[0] & BT_LE_AD_LIMITED) {
				return true;
			}
		}
	}

	return false;
}

static int bt_adv_restarter_start_unsafe(uint8_t peripherals_limit,
					 const struct bt_le_adv_param *param,
					 const struct bt_data *ad, size_t ad_len,
					 const struct bt_data *sd, size_t sd_len)
{
	int err;

	k_mutex_lock(&globals_lock, K_FOREVER);

	/* Invoking `bt_le_adv_start` here serves to check the
	 * input parameters.
	 */
	err = bt_le_adv_start(param, ad, ad_len, sd, sd_len);
	if (err) {
		goto exit;
	}

	restart_govenor_max_periperals = peripherals_limit;

	deep_copy_params(param);

	copy_ad_len = ad_len;
	err = serialize_data_arr(ad, ad_len, copy_ad_serialized, sizeof(copy_ad_serialized));
	__ASSERT_NO_MSG(!err);

	copy_sd_len = sd_len;
	err = serialize_data_arr(sd, sd_len, copy_sd_serialized, sizeof(copy_sd_serialized));
	__ASSERT_NO_MSG(!err);

exit:
	k_mutex_unlock(&globals_lock);
	return err;
}

int bt_adv_restarter_start(uint8_t peripherals_limit, const struct bt_le_adv_param *param,
			   const struct bt_data *ad, size_t ad_len, const struct bt_data *sd,
			   size_t sd_len)
{
	if (!param) {
		return -EINVAL;
	}

	if (param->options & BT_LE_ADV_OPT_ONE_TIME) {
		return bt_le_adv_start(param, ad, ad_len, sd, sd_len);
	}

	if (ad_is_limited(ad, ad_len)) {
		/* Limited and restarted combination is not
		 * supported yet. We would need to move the
		 * timer out of the stack to be notified of the
		 * timeout.
		 */
		return -ENOSYS;
	}

	return bt_adv_restarter_start_unsafe(peripherals_limit, param, ad, ad_len, sd, sd_len);
}

int bt_adv_restarter_update_data(const struct bt_data *ad, size_t ad_len, const struct bt_data *sd,
				 size_t sd_len)
{
	int err;

	k_mutex_lock(&globals_lock, K_FOREVER);

	err = bt_le_adv_update_data(ad, ad_len, sd, sd_len);
	if (err) {
		goto exit;
	}

	copy_ad_len = ad_len;
	err = serialize_data_arr(ad, ad_len, copy_ad_serialized, sizeof(copy_ad_serialized));
	__ASSERT_NO_MSG(!err);

	copy_sd_len = sd_len;
	err = serialize_data_arr(sd, sd_len, copy_sd_serialized, sizeof(copy_sd_serialized));
	__ASSERT_NO_MSG(!err);

exit:
	k_mutex_unlock(&globals_lock);
	return err;
}

int bt_adv_restarter_stop(void)
{
	int err;

	k_mutex_lock(&globals_lock, K_FOREVER);

	restart_govenor_max_periperals = 0;

	err = bt_le_adv_stop();

	k_mutex_unlock(&globals_lock);

	return err;
}

/** @brief Parse the advertising data into a bt_data array.
 *
 *  @note The resulting entries in the bt_data memory borrow
 *  from the input ad_struct.
 */
static void bt_data_parse_into(struct bt_data *bt_data, size_t bt_data_count, uint8_t *ad_struct)
{
	size_t pos = 0;
	for (size_t i = 0; i < copy_ad_len; i++) {
		bt_data[i].data_len = ad_struct[pos++];
		bt_data[i].type = ad_struct[pos++];
		bt_data[i].data = &ad_struct[pos];
		pos += bt_data[i].data_len;
	}
}

static int try_restart_ignore_oom(void)
{
	int err;

	/* Zephyr Bluetooth stack does not ingest the serialized
	 * AD format, but requires an array of `bt_data` with
	 * pointers.
	 *
	 * Worst case, the array length here is 15. Sizeof
	 * `bt_data` is 8. That's 120 bytes of stack space here.
	 * Please download more RAM before using Zephyr
	 * Bluetooth.
	 *
	 * This is in addtion the the actual storage of the
	 * data, which is the expected 62 bytes of static RAM.
	 */
	struct bt_data ad[copy_ad_len];
	struct bt_data sd[copy_sd_len];

	bt_data_parse_into(ad, copy_ad_len, copy_ad_serialized);
	bt_data_parse_into(sd, copy_sd_len, copy_sd_serialized);

	err = bt_le_adv_start(&param_copy, ad, copy_ad_len, sd, copy_sd_len);

	switch (err) {
	case -ENOMEM:
	case -ECONNREFUSED:
		/* Retry later */
		return 0;
	default:
		return err;
	}
}

static void _count_peripheral_loop(struct bt_conn *conn, void *count_)
{
	size_t *count = count_;
	struct bt_conn_info conn_info;

	bt_conn_get_info(conn, &conn_info);

	/* Note that advertisers register as 'central'.
	 */
	if (conn_info.role == BT_CONN_ROLE_PERIPHERAL) {
		(*count)++;
	}
}

static size_t count_conn_marked_peripheral(void)
{
	size_t count = 0;

	bt_conn_foreach(BT_CONN_TYPE_LE, _count_peripheral_loop, &count);

	return count;
}

static bool should_restart(void)
{
	uint8_t peripheral_count;

	peripheral_count = count_conn_marked_peripheral();
	return peripheral_count < restart_govenor_max_periperals;
}

static void restart_work_handler(struct k_work *self)
{
	int err;

	/* The timeout is defence-in-depth. The lock has a
	 * dependency the blocking Bluetooth API. This can form
	 * a deadlock if the Bluetooth API has a dependency on
	 * the work queue.
	 */
	err = k_mutex_lock(&globals_lock, K_MSEC(100));
	if (err) {
		LOG_DBG("reshed");
		k_work_submit(self);

		/* We did not get the lock. */
		return;
	}

	if (should_restart()) {
		err = try_restart_ignore_oom();
		if (err) {
			LOG_ERR("Failed to restart advertising (err %d)", err);
		}
	}

	k_mutex_unlock(&globals_lock);
}

static void on_conn_recycled(void)
{
	k_work_submit(&restart_work);
}

BT_CONN_CB_DEFINE(conn_cb) = {
	.recycled = on_conn_recycled,
};
