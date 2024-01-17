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


static struct net_buf *bt_buf_get_cmd_complete2(uint16_t opcode, k_timeout_t timeout)
{
	/* A non-zero opcode responds to a command. Events that respond to a
	 * command must use the special allocator.
	 */
	if (opcode != 0) {
		/* Special allocator for command responses. */
		return bt_buf_get_cmd_complete(K_FOREVER);
	}

	/* Otherwise, treat it like a normal event. */
	return bt_buf_get_rx(BT_BUF_EVT, K_FOREVER);
}

#define BT_HCI_EVT_CMD_COMPLETE_SIZE sizeof(struct bt_hci_evt_cmd_complete)
static int rx_alloc_h4_evt_cmd_complete(uint8_t peek_buf, uint8_t peek_len, struct net_buf **buf)
{
	uint16_t opcode;
	size_t peek_need = 0;

	if (peek_len < BT_HCI_EVT_CMD_COMPLETE_SIZE) {
		return BT_HCI_EVT_CMD_COMPLETE_SIZE - peek_len;
	}

	opcode = sys_get_le16(&peek_buf[1]);

	*buf = bt_buf_get_cmd_complete2(opcode, K_FOREVER);

	if (!*buf) {
		LOG_ERR("Failed to allocate buffer");
		return -ENOBUFS;
	}

	return 0;
}

#define BT_HCI_EVT_CMD_STATUS_SIZE sizeof(struct bt_hci_evt_cmd_status)
static int rx_alloc_h4_evt_cmd_status(uint8_t peek_buf, uint8_t peek_len, struct net_buf **buf)
{
	uint16_t opcode;
	size_t peek_need = 0;

	if (peek_len < BT_HCI_EVT_CMD_STATUS_SIZE) {
		return BT_HCI_EVT_CMD_STATUS_SIZE - peek_len;
	}

	opcode = sys_get_le16(&peek_buf[2]);

	*buf = bt_buf_get_cmd_complete2(opcode, K_FOREVER);

	if (!*buf) {
		LOG_ERR("Failed to allocate buffer");
		return -ENOBUFS;
	}

	return 0;
}

#define BT_HCI_EVT_LE_META_SIZE sizeof(struct bt_hci_evt_le_meta_event)
static int rx_alloc_h4_evt_le_meta(uint8_t peek_buf, uint8_t peek_len, struct net_buf **buf)
{
	uint8_t subevent_code;

	if (peek_len < BT_HCI_EVT_LE_META_SIZE) {
		return BT_HCI_EVT_LE_META_SIZE - peek_len;
	}

	subevent_code = peek_buf[0];

	switch (subevent_code) {
	case BT_HCI_EVT_LE_ADVERTISING_REPORT:
		/* Discardable TODO */
		*buf = bt_buf_get_evt(BT_HCI_EVT_LE_META_EVENT, true, K_NO_WAIT);
		break;
	default:
		/* Events do not have flow control. Wait forever. */
		*buf = bt_buf_get_rx(BT_BUF_EVT, K_FOREVER);
	}

	return peek_need - peek_len;
}

static int rx_alloc_h4_evt(uint8_t peek_buf, uint8_t peek_len, struct net_buf **buf)
{
	uint8_t evt_code;
	size_t evt_len;

	if (peek_len < BT_HCI_EVT_HDR_SIZE) {
		return BT_HCI_EVT_HDR_SIZE - peek_len;
	}

	evt_code = peek_buf[0];
	evt_len = peek_buf[1];

	peek_buf = &peek_buf[BT_HCI_EVT_HDR_SIZE];
	peek_len -= BT_HCI_EVT_HDR_SIZE;

	switch (evt_code) {
	case BT_HCI_EVT_CMD_COMPLETE:
		return rx_alloc_h4_evt_cmd_complete(peek_buf, peek_len, buf);
	case BT_HCI_EVT_CMD_STATUS:
		return rx_alloc_h4_evt_cmd_status(peek_buf, peek_len, buf);
	case BT_HCI_EVT_LE_META_EVENT:
		return rx_alloc_h4_evt_le_meta(peek_buf, peek_len, buf);
	default:
		/* Events do not have flow control. Wait forever. */
		*buf = bt_buf_get_evt(evt_code, false, K_FOREVER);
		if (!*buf) {
			LOG_ERR("Failed to allocate buffer");
			return -ENOBUFS;
		}
	}

	return evt_len - peek_len;
}

static int rx_alloc_h4_acl(uint8_t peek_buf, uint8_t peek_len, struct net_buf **buf)
{
	uint16_t acl_payload_length;

	if (peek_len < BT_HCI_ACL_HDR_SIZE) {
		return BT_HCI_ACL_HDR_SIZE - peek_len;
	}

	acl_payload_length = sys_get_le16(&peek_buf[2]);

	peek_buf = &peek_buf[BT_HCI_ACL_HDR_SIZE];
	peek_len -= BT_HCI_ACL_HDR_SIZE;

	if (!buf) {
		/* ACL buffers are ensured by host flow control. Don't wait. */
		*buf = bt_buf_get_rx(BT_BUF_ACL_IN, K_NO_WAIT);
		if (!buf) {
			LOG_ERR("Failed to allocate ACL buffer");
			return -ENOBUF;
		}
	}

	return acl_payload_length - peek_len;
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
 * @retval -EIO Lookahead buffer contains invalid HCI packet.
 */
static int rx_alloc_h4(uint8_t peek_buf, uint8_t peek_len, struct net_buf **buf)
{
	uint8_t hci_type;
	size_t next_hdr_len;

	if (peek_len < H4_HDR_SIZE) {
		return H4_HDR_SIZE - peek_len;
	}

	hci_type = peek_buf[0];

	peek_buf = &peek_buf[H4_HDR_SIZE];
	peek_len -= H4_HDR_SIZE;

	switch (hci_type) {
	case H4_EVT:
		return rx_alloc_h4_evt(peek_buf, peek_len, buf);
	case H4_ACL:
		return rx_alloc_h4_acl(peek_buf, peek_len, buf);
	default:
		LOG_ERR("Unexpected hci type: %u", hci_type);
		return -EIO;
	}
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
