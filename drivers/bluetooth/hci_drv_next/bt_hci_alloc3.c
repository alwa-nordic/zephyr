/* Copyright (c) 2024 Nordic Semiconductor
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/drivers/bluetooth/hci_driver.h>
#include <zephyr/drivers/bluetooth/hci_drv_next.h>

#include <errno.h>
#include <stddef.h>

#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(bt_hci_alloc, CONFIG_BT_HCI_DRIVER_LOG_LEVEL);

typedef static int rx_parser_t(uint8_t *peek_buf, uint8_t peek_len, rx_alloc_t *out_alloc,
			       rx_parser_t *next_parser);

static struct net_buf *bt_buf_get_cmd_complete2(uint16_t opcode, k_timeout_t timeout)
{
	/* A non-zero opcode responds to a command. Events that respond to a
	 * command must use the special allocator.
	 */
	if (opcode != 0) {
		/* Special allocator for command responses. */
		return bt_buf_get_cmd_complete(timeout);
	}

	/* Otherwise, treat it like a normal event. */
	return bt_buf_get_rx(BT_BUF_EVT, timeout);
}

#define BT_HCI_EVT_CMD_COMPLETE_SIZE sizeof(struct bt_hci_evt_cmd_complete)
static int rx_alloc_h4_evt_cmd_complete(uint8_t peek_buf, uint8_t peek_len, struct net_buf **buf,
					struct k_poll_event *event)
{
	size_t evt_len;
	uint16_t opcode;

	if (peek_len < BT_HCI_EVT_CMD_COMPLETE_SIZE) {
		return BT_HCI_EVT_CMD_COMPLETE_SIZE - peek_len;
	}

	evt_len = peek_buf[1];
	opcode = sys_get_le16(&peek_buf[1]);

	*buf = bt_buf_get_cmd_complete2(opcode, K_NO_WAIT);

	if (!*buf) {
		return -EAGAIN;
	}

	return 0;
}

#define BT_HCI_EVT_CMD_STATUS_SIZE sizeof(struct bt_hci_evt_cmd_status)
static int rx_alloc_h4_evt_cmd_status(uint8_t peek_buf, uint8_t peek_len, struct net_buf **buf)
{
	size_t evt_len;
	uint16_t opcode;

	if (peek_len < BT_HCI_EVT_CMD_STATUS_SIZE) {
		return BT_HCI_EVT_CMD_STATUS_SIZE - peek_len;
	}

	evt_len = peek_buf[1];
	opcode = sys_get_le16(&peek_buf[2]);

	*buf = bt_buf_get_cmd_complete2(opcode, K_NO_WAIT);

	if (!*buf) {
		return -EAGAIN;
	}

	return 0;
}

#define BT_HCI_EVT_LE_META_SIZE sizeof(struct bt_hci_evt_le_meta_event)
static int rx_alloc_h4_evt_le_meta(uint8_t peek_buf, uint8_t peek_len, struct net_buf **buf)
{
	size_t evt_len;
	uint8_t subevent_code;

	if (peek_len < BT_HCI_EVT_LE_META_SIZE) {
		return BT_HCI_EVT_LE_META_SIZE - peek_len;
	}

	evt_len = peek_buf[1];
	subevent_code = peek_buf[2];

	switch (subevent_code) {
	case BT_HCI_EVT_LE_ADVERTISING_REPORT:
		/* Discardable TODO */
		*buf = bt_buf_get_evt(BT_HCI_EVT_LE_META_EVENT, true, K_NO_WAIT);
		break;
	default:
		/* Events do not have flow control. Wait forever. */
		*buf = bt_buf_get_rx(BT_BUF_EVT, K_FOREVER);
	}

	return evt_len - peek_len;
}

static int rx_alloc_h4_evt_default(uint8_t peek_buf, uint8_t peek_len, struct net_buf **buf)
{
	size_t evt_len;
	uint8_t subevent_code;

	evt_code = peek_buf[0];
	evt_len = peek_buf[1];

	/* Events do not have flow control. Failing to allocate is not
	 * an error.
	 */
	*buf = bt_buf_get_rx(BT_BUF_EVT, K_NO_WAIT);
	if (!*buf) {
		return -EAGAIN;
	}

	return evt_len - peek_len;
}

static int rx_alloc_h4_evt_parse(uint8_t peek_buf, uint8_t peek_len, struct net_buf *buf)
{
	uint8_t evt_code;
	size_t evt_len;

	evt_code = peek_buf[H4_HDR_SIZE + 0];

	/* Don't consume. Subparsers look at the whole event. */

	switch (evt_code) {
	case BT_HCI_EVT_CMD_COMPLETE:
		return rx_alloc_h4_evt_cmd_complete(peek_buf, peek_len, alloc);
	case BT_HCI_EVT_CMD_STATUS:
		return rx_alloc_h4_evt_cmd_status(peek_buf, peek_len, alloc);
	case BT_HCI_EVT_LE_META_EVENT:
		return rx_alloc_h4_evt_le_meta(peek_buf, peek_len, alloc);
	default:
		return rx_alloc_h4_evt_default(peek_buf, peek_len, alloc);
	}
}

static int rx_alloc_h4_evt_read(uint8_t peek_buf, uint8_t peek_len, struct net_buf *buf)
{
	*next_parser = rx_alloc_h4_evt_parse;
	return H4_HDR_SIZE + BT_HCI_EVT_HDR_SIZE;
}

static int rx_alloc_h4_acl_alloc(struct net_buf *buf)
{
	/* ACL buffers are ensured by host flow control. Don't wait.
	 * What about if we don't enable host flow control?
	 */
	*buf = bt_buf_get_rx(BT_BUF_ACL_IN, K_NO_WAIT);
	if (!*buf) {
		LOG_ERR("Failed to allocate ACL buffer");
		return -ENOBUF;
	}
	return 0;
}

static int rx_alloc_h4_acl_parse(uint8_t *peek_buf, uint8_t peek_len, rx_alloc_t *out_alloc,
				 rx_parser_t *next_parser)
{
	uint16_t acl_payload_length;

	acl_payload_length = sys_get_le16(&peek_buf[BT_HCI_ACL_HDR_SIZE + 2]);

	*next_parser = NULL;
	*out_alloc = rx_alloc_h4_acl_alloc;

	return H4_HDR_SIZE + BT_HCI_ACL_HDR_SIZE + acl_payload_length;
}

static int rx_alloc_h4_acl_read(uint8_t peek_buf, uint8_t peek_len, struct net_buf *buf)
{
	*next_parser = rx_alloc_h4_acl_parse;
	return H4_HDR_SIZE + BT_HCI_ACL_HDR_SIZE;
}


static int rx_alloc_h4_parse(uint8_t *peek_buf, uint8_t peek_len, rx_alloc_t *out_alloc,
			     rx_parser_t *next_parser)
{
	/* hci_type */
	switch (peek_buf[0]) {
	case H4_EVT:
		*next_parser = rx_alloc_h4_evt_read;
		return 0;
	case H4_ACL:
		*next_parser = rx_alloc_h4_acl_read;
		return 0;
	default:
		break default : LOG_ERR("Unexpected hci type: %u", hci_type);
		*next_parser = NULL;
		return -EIO;
	}
}

/**
 * @brief Allocate a net_buf for the HCI packet in a H4 stream.
 *
 * @param peek_buf Lookahead buffer into the H4 stream
 * @param peek_len Lookahead content length
 * @param[out] buf Allocated net_buf. Once the net_buf is allocated, it can be
 * used as the new buffer.
 *
 * @retval 0 HCI packet completed.
 * @retval +n Lookahead buffer is missing at least `n` bytes.
 * @retval -EIO Lookahead buffer contains invalid HCI packet.
 */
static int rx_alloc_h4_read(uint8_t *peek_buf, uint8_t peek_len, rx_alloc_t *out_alloc,
			    rx_parser_t *next_parser)
{
	*next_parser = rx_alloc_h4_parse;
	return H4_HDR_SIZE;
}

// try a function-pointer-based approach
// there is a next_parser function pointer
// tall call into that
// the next_parser is stored top-level to allow immediate
// resumption
// in addition, the parser offset into the peek buffer is kept
// as well. This is not as important, since we can also make all
// the parsers make assumption about the absolute offset. That
// is probably clearer.
// Then the only state is the peek buffer itself (and its
// length) and the next_parser is a shortcut.

// scratch buffer big enough for the biggest rx.
// poll into this buffer and parse accordingly until i have a
// full packet.
// only when i have the full packet, do i decide what pool to
// allocate from and copy the data.

// what's big enough?
// h4_hdr + hci_acl_hdr + max_acl_payload
// 1 + 4 + 16
