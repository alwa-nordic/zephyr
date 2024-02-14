#include "zephyr/net/buf.h"
#include <stdint.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/hci_raw.h>
#include <zephyr/bluetooth/buf.h>

#include <bs_tracing.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#define EXPECT_ZERO(expr) expect_zero((expr), __FILE__, __LINE__)
static inline void expect_zero(int err, char *where_file, int where_line)
{
	if (err) {
		bs_trace_print(BS_TRACE_ERROR, where_file, where_line, 0, BS_TRACE_AUTOTIME, 0,
			       "err %d\n", err);
	}
}

static void send(uint8_t *data, size_t size)
{
	struct net_buf *buf;

	buf = bt_buf_get_tx(BT_BUF_H4, K_NO_WAIT, data, size);
	if (!buf) {
		LOG_ERR("bt_buf_get_tx failed");
		k_oops();
	}
	EXPECT_ZERO(bt_send(buf));
}

static struct k_fifo c2h_queue;
static void expect_recv(uint8_t *data, size_t size)
{
	struct net_buf *buf;

	LOG_DBG("Waiting for packet");
	buf = k_fifo_get(&c2h_queue, K_FOREVER);
	LOG_DBG("Received packet");
	if (buf->len != size || memcmp(buf->data, data, size)) {
		LOG_ERR("Received packet of wrong data");
		LOG_HEXDUMP_ERR(buf->data, buf->len, "Received data");
		LOG_HEXDUMP_ERR(data, size, "Expected data");
		k_oops();
	}
	net_buf_unref(buf);
}

static uint8_t h4_cmd_reset[] = {0x01, 0x03, 0x0c, 0x00};
static uint8_t h4_evt_cmd_complete_reset[] = {0x04, 0x0e, 0x04, 0x01, 0x03, 0x0c, 0x00};
static uint8_t h4_evt_acl_complete[] = {0x04, 0x13, 0x05, 0x01, 0x00, 0x00, 0x01, 0x00};

static uint8_t h4_cmd_create_connection[] = {
	0x01,                               // cmd
	0x0d, 0x20,                         // le create connection
	0x19,                               // param len
	0x60, 0x00,                         // scan interval
	0x60, 0x00,                         // scan window
	0x00,                               // filter policy
	0x01,                               // peer address type
	0x01, 0x00, 0x00, 0x00, 0x00, 0xc0, // peer address
	0x00,                               // own address type
	0x18, 0x00,                         // min conn interval
	0x28, 0x00,                         // max conn interval
	0x00, 0x00,                         // conn latency
	0x2a, 0x00,                         // supervision timeout
	0x00, 0x00,                         // min ce length
	0x00, 0x00,                         // max ce length
};

static uint8_t h4_evt_cmd_status_create_connection[] = {
	0x04,       // evt
	0x0f,       // cmd status
	0x04,       // param len
	0x00,       // status
	0x01,       // ncmd
	0x0d, 0x20, // le create connection
};



int main(void)
{
	k_fifo_init(&c2h_queue);
	EXPECT_ZERO(bt_enable_raw(&c2h_queue));

	send(h4_cmd_reset, sizeof(h4_cmd_reset));
	expect_recv(h4_evt_cmd_complete_reset, sizeof(h4_evt_cmd_complete_reset));

	send(h4_cmd_create_connection, sizeof(h4_cmd_create_connection));
	expect_recv(h4_evt_cmd_status_create_connection,
		    sizeof(h4_evt_cmd_status_create_connection));

	k_msleep(10000);
	LOG_DBG("tester end");
	bs_trace_exit("End main(), stopping simulation\n");
	return 0;
}
