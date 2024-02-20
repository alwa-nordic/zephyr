/* Copyright (c) 2024 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_BLUETOOTH_HOST_HCI_LOG_HEXDUMP_H_
#define ZEPHYR_SUBSYS_BLUETOOTH_HOST_HCI_LOG_HEXDUMP_H_

#if defined(CONFIG_BT_HCI_LOG_HEXDUMP)
void bt_hci_log_hexdump(struct net_buf *buf);
#else
static inline void bt_hci_log_hexdump(struct net_buf *buf)
{
}
#endif

#endif /* ZEPHYR_SUBSYS_BLUETOOTH_HOST_HCI_LOG_HEXDUMP_H_ */
