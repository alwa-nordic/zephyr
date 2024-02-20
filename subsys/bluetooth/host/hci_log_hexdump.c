/* Copyright (c) 2024 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <zephyr/bluetooth/buf.h>
#include <zephyr/logging/log.h>

/* Always enable these logs. They are requested by the user by
 * the fact that this file is used.
 */
LOG_MODULE_REGISTER(bt_hci_log_hexdump, LOG_LEVEL_INF);

#define H4_CMD 0x01
#define H4_ACL 0x02
#define H4_SCO 0x03
#define H4_EVT 0x04
#define H4_ISO 0x05

/** Follows the LINKTYPE_BLUETOOTH_HCI_H4_WITH_PHDR convetion.
 *  @retval 0 Host to Controller
 *  @retval 1 Controller to Host
 */
static uint8_t bt_buf_direction(struct net_buf *buf)
{
	switch (bt_buf_get_type(buf)) {
	case BT_BUF_CMD:
	case BT_BUF_ACL_OUT:
	case BT_BUF_ISO_OUT:
		return 0;
	case BT_BUF_EVT:
	case BT_BUF_ACL_IN:
	case BT_BUF_ISO_IN:
		return 1;
	default:
		__ASSERT_NO_MSG(false);
		return 2;
	}
}

static uint8_t bt_buf_h4_type(struct net_buf *buf)
{
	switch (bt_buf_get_type(buf)) {
	case BT_BUF_CMD:
		return H4_CMD;
	case BT_BUF_EVT:
		return H4_EVT;
	case BT_BUF_ACL_IN:
	case BT_BUF_ACL_OUT:
		return H4_ACL;
	case BT_BUF_ISO_IN:
	case BT_BUF_ISO_OUT:
		return H4_ISO;
	default:
		__ASSERT_NO_MSG(false);
		return 0xff;
	}
}

void bt_hci_log_hexdump(struct net_buf *buf)
{
	uint8_t dir_char = ('0' + bt_buf_direction(buf));
	uint8_t h4_type = bt_buf_h4_type(buf);
	Z_LOG_HEXDUMP(LOG_LEVEL_INF, buf->data, buf->len, "!HCI! 00 00 00 0%c %02x", dir_char,
		      h4_type);
}
