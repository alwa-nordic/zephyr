/* Copyright (c) 2024 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_BLUETOOTH_HOST_HCI_LOG_HEXDUMP_H_
#define ZEPHYR_SUBSYS_BLUETOOTH_HOST_HCI_LOG_HEXDUMP_H_

#include <zephyr/bluetooth/buf.h>
#include <zephyr/net_buf.h>

#if defined(CONFIG_BT_HCI_LOG_HEXDUMP)
void bt_hci_log_hexdump(struct net_buf *buf, enum bt_buf_dir dir);
#else
static inline void bt_hci_log_hexdump(struct net_buf *buf, enum bt_buf_dir dir)
{
	ARG_UNUSED(buf);
	ARG_UNUSED(dir);
}
#endif

#endif /* ZEPHYR_SUBSYS_BLUETOOTH_HOST_HCI_LOG_HEXDUMP_H_ */
