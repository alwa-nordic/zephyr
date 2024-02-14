#include <testlib/adv.h>
#include <zephyr/bluetooth/addr.h>
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

int main(void)
{
	bt_addr_le_t addr;
	struct bt_conn *conn = NULL;
	struct bt_gatt_discover_params disc_params = {};

	EXPECT_ZERO(bt_addr_le_from_str("c0:00:00:00:00:01", "random", &addr));
	EXPECT_ZERO(bt_id_create(&addr, NULL));
	EXPECT_ZERO(bt_enable(NULL));
	EXPECT_ZERO(bt_testlib_adv_conn(&conn, BT_ID_DEFAULT, 0));

	/* Wait for all magics to finish and not get in the way. */
	k_msleep(10000);


	LOG_DBG("dut end");
	bs_trace_exit("End main(), stopping simulation\n");
	return 0;
}
