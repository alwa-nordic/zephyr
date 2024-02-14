#include <sys/types.h>
#include <testlib/adv.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>

#include <bs_tracing.h>

#define EXPECT_ZERO(expr) expect_zero((expr), __FILE__, __LINE__)
static inline void expect_zero(int err, char *where_file, int where_line)
{
	if (err) {
		bs_trace_print(BS_TRACE_ERROR, where_file, where_line, 0, BS_TRACE_AUTOTIME, 0,
			       "err %d\n", err);
	}
}

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

static ssize_t test_chrc_read(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			      uint16_t buf_len, uint16_t offset)
{
	ssize_t read_len = MIN(buf_len, 512);

	LOG_INF("Server side buf_len %u read_len %u", buf_len, read_len);
	memset(buf, 0, read_len);
	return read_len;
}

#define TEST_SRVC_UUID                                                                             \
	BT_UUID_DECLARE_128(0xdb, 0x1f, 0xe2, 0x52, 0xf3, 0xc6, 0x43, 0x66, 0xb3, 0x92, 0x5d,      \
			    0xc6, 0xe7, 0xc9, 0x59, 0x9d)

#define TEST_CHRC_UUID                                                                             \
	BT_UUID_DECLARE_128(0x3f, 0xa4, 0x7f, 0x44, 0x2e, 0x2a, 0x43, 0x05, 0xab, 0x38, 0x07,      \
			    0x8d, 0x16, 0xbf, 0x99, 0xf1)

static struct bt_gatt_attr test_srvc_attrs[] = {
	(struct bt_gatt_attr){
		.handle = 0x1000,
		.uuid = BT_UUID_GATT_PRIMARY,
		.perm = BT_GATT_PERM_READ,
		.read = bt_gatt_attr_read_service,
		.user_data = (void *)TEST_CHRC_UUID,
	},
	(struct bt_gatt_attr){
		.handle = 0x1001,
		.uuid = BT_UUID_GATT_CHRC,
		.perm = BT_GATT_PERM_READ,
		.read = bt_gatt_attr_read_chrc,
		.user_data = &(struct bt_gatt_chrc){
			.uuid = TEST_CHRC_UUID,
			.value_handle = 0x1002,
			.properties = BT_GATT_CHRC_READ,
		},
	},
	(struct bt_gatt_attr){
		.handle = 0x1002,
		.uuid = TEST_CHRC_UUID,
		.perm = BT_GATT_PERM_READ,
		.read = test_chrc_read,
	},
};

static struct bt_gatt_service test_srvc = {
	.attrs = test_srvc_attrs,
	.attr_count = ARRAY_SIZE(test_srvc_attrs),
};

int main(void)
{
	bt_addr_le_t addr;
	struct bt_conn *conn = NULL;

	EXPECT_ZERO(bt_gatt_service_register(&test_srvc));
	EXPECT_ZERO(bt_addr_le_from_str("c0:00:00:00:00:01", "random", &addr));
	EXPECT_ZERO(bt_id_create(&addr, NULL));
	EXPECT_ZERO(bt_enable(NULL));
	EXPECT_ZERO(bt_testlib_adv_conn(&conn, BT_ID_DEFAULT, 0));
	LOG_DBG("Connected");

	/* Wait for all magics to finish and not get in the way. */
	k_msleep(10000);

	LOG_DBG("dut end");
	k_msleep(60000);
	return 0;
}
