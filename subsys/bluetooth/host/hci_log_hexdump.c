/* Copyright (c) 2024 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <zephyr/bluetooth/buf.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>

/* Always enable these logs. They are requested by the user by
 * the fact that this file is used.
 */
LOG_MODULE_REGISTER(bt_hci_log_hexdump, LOG_LEVEL_INF);

void bt_hci_log_hexdump(struct net_buf *buf, enum bt_buf_dir dir)
{
	uint8_t dir_char = ('0' + (dir == BT_BUF_IN ? 1 : 0));
	uint8_t h4_type = buf->data[0];

	Z_LOG_HEXDUMP(LOG_LEVEL_INF, buf->data, buf->len, "!HCI! 00 00 00 0%c %02x", dir_char,
		      h4_type);
}
