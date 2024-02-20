#include "zephyr/net/buf.h"
#include <stdint.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/hci_raw.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/bluetooth/buf.h>
#include <zephyr/bluetooth/byteorder.h>
#include <../subsys/bluetooth/host/att_internal.h>

#include <bs_tracing.h>

#define H4_NONE          0x00
#define H4_CMD           0x01
#define H4_ACL           0x02
#define H4_SCO           0x03
#define H4_EVT           0x04
#define H4_ISO           0x05
#define BT_L2CAP_CID_ATT 0x0004

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#define EXPECT_ZERO(expr) expect_zero((expr), __FILE__, __LINE__)
static inline void expect_zero(int err, char *where_file, int where_line)
{
	if (err) {
		bs_trace_print(BS_TRACE_ERROR, where_file, where_line, 0, BS_TRACE_AUTOTIME, 0,
			       "err %d\n", err);
	}
}

#define SEND(_data) send(sizeof(*(_data)), (_data))
static void send(size_t size, uint8_t (*data)[size])
{
	struct net_buf *buf;
	LOG_HEXDUMP_DBG(data, size, "!HCI! 00 00 00 00");
	buf = bt_buf_get_tx(BT_BUF_H4, K_NO_WAIT, *data, size);
	if (!buf) {
		LOG_ERR("bt_buf_get_tx failed");
		k_oops();
	}
	EXPECT_ZERO(bt_send(buf));
}

static struct k_fifo c2h_queue;
#define RECV_EXPECT(_data) recv_expect(sizeof(*(_data)), (_data))
static void recv_expect(size_t size, uint8_t (*data)[size])
{
	struct net_buf *buf;

	buf = k_fifo_get(&c2h_queue, K_FOREVER);
	LOG_HEXDUMP_DBG(buf->data, buf->len, "!HCI! 00 00 00 01");
	if (buf->len != size || memcmp(buf->data, data, size)) {
		LOG_ERR("Received packet of wrong data");
		k_oops();
	}
	net_buf_unref(buf);
}

static uint8_t empty[] = {};
static uint8_t h4_cmd_reset[] = {H4_CMD, BT_BYTES_LIST_LE16(BT_HCI_OP_RESET), 0x00};
static uint8_t h4_evt_acl_complete[] = {
	H4_EVT,
	BT_HCI_EVT_NUM_COMPLETED_PACKETS,
	0x05,
	0x01,                  // num handle,count pairs
	BT_BYTES_LIST_LE16(0), // handle
	BT_BYTES_LIST_LE16(1), // completed count
};

static uint8_t h4_cmd_create_connection[] = {
	H4_CMD, // cmd
	BT_BYTES_LIST_LE16(BT_HCI_OP_LE_CREATE_CONN),
	0x19, // param len
	0x60,
	0x00, // scan interval
	0x60,
	0x00, // scan window
	0x00, // filter policy
	0x01, // peer address type
	0x01,
	0x00,
	0x00,
	0x00,
	0x00,
	0xc0, // peer address
	0x00, // own address type
	0x18,
	0x00, // min conn interval
	0x28,
	0x00, // max conn interval
	0x00,
	0x00, // conn latency
	0x2a,
	0x00, // supervision timeout
	0x00,
	0x00, // min ce length
	0x00,
	0x00, // max ce length
};

static uint8_t h4_evt_cmd_set_evt_mask[] = {
	H4_CMD, // cmd
	BT_BYTES_LIST_LE16(BT_HCI_OP_SET_EVENT_MASK),
	0x08, // param len
	BT_BYTES_LIST_LE64(BT_EVT_MASK_LE_META_EVENT),
};

#define EVT_CMD_STATUS(op)                                                                         \
	((uint8_t[]){H4_EVT, BT_HCI_EVT_CMD_STATUS, 0x04, 0x00, 0x01, BT_BYTES_LIST_LE16(op)})

#define EVT_CMD_COMPLETE(op)                                                                       \
	((uint8_t[]){H4_EVT, BT_HCI_EVT_CMD_COMPLETE, 0x04, 0x01, BT_BYTES_LIST_LE16(op), 0x00})

static uint8_t h4_evt_cmd_set_evt_le_mask[] = {
	H4_CMD,
	BT_BYTES_LIST_LE16(BT_HCI_OP_LE_SET_EVENT_MASK),
	0x08, // param len
	BT_BYTES_LIST_LE64(BT_EVT_MASK_LE_CONN_COMPLETE),
};

static uint8_t h4_evt_le_conn_complete[] = {H4_EVT, 0x3e, 0x13, 0x01, 0x00, 0x00, 0x00, 0x00,
					    0x01,   0x01, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x28,
					    0x00,   0x00, 0x00, 0x2a, 0x00, 0x05};

#define H4_ATT_MTU_REQ(mtu)                                                                        \
	((uint8_t[]){H4_ACL, 0x00, 0x00, BT_BYTES_LIST_LE16(3 + 2), BT_BYTES_LIST_LE16(3),         \
		     BT_BYTES_LIST_LE16(BT_L2CAP_CID_ATT), BT_ATT_OP_MTU_REQ,                      \
		     BT_BYTES_LIST_LE16(mtu)})

static uint8_t h4_acl_att_read_x1002[] = {
	H4_ACL,
	0x00,
	0x00,                                 // handle 0, PB=0, BC=0
	BT_BYTES_LIST_LE16(3 + 4),            // acl len
	BT_BYTES_LIST_LE16(3),                // l2cap len
	BT_BYTES_LIST_LE16(BT_L2CAP_CID_ATT), // l2cap cid
	BT_ATT_OP_READ_REQ,
	BT_BYTES_LIST_LE16(0x1002),
};

int main(void)
{
	k_fifo_init(&c2h_queue);
	EXPECT_ZERO(bt_enable_raw(&c2h_queue));

	SEND(&h4_cmd_reset);
	RECV_EXPECT(&EVT_CMD_COMPLETE(BT_HCI_OP_RESET));

	SEND(&h4_evt_cmd_set_evt_mask);
	RECV_EXPECT(&EVT_CMD_COMPLETE(BT_HCI_OP_SET_EVENT_MASK));

	SEND(&h4_evt_cmd_set_evt_le_mask);
	RECV_EXPECT(&EVT_CMD_COMPLETE(BT_HCI_OP_LE_SET_EVENT_MASK));

	SEND(&h4_cmd_create_connection);
	RECV_EXPECT(&EVT_CMD_STATUS(BT_HCI_OP_LE_CREATE_CONN));
	RECV_EXPECT(&h4_evt_le_conn_complete);

	SEND(&H4_ATT_MTU_REQ(512));
	RECV_EXPECT(&h4_evt_acl_complete);
	RECV_EXPECT(&empty);

	SEND(&h4_acl_att_read_x1002);
	RECV_EXPECT(&h4_evt_acl_complete);

	LOG_DBG("tester end");
	k_msleep(60000);
	return 0;
}
