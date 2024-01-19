/*
 * Copyright (c) 2017 Nordic Semiconductor ASA
 * Copyright (c) 2015 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/bluetooth/buf.h>
#include <zephyr/bluetooth/l2cap.h>

#include "hci_core.h"
#include "conn_internal.h"
#include "iso_internal.h"

#include <zephyr/bluetooth/hci.h>

#if defined(CONFIG_BT_CONN)
#if defined(CONFIG_BT_ISO)
#define MAX_EVENT_COUNT CONFIG_BT_MAX_CONN + CONFIG_BT_ISO_MAX_CHAN
#else
#define MAX_EVENT_COUNT CONFIG_BT_MAX_CONN
#endif /* CONFIG_BT_ISO */
#elif defined(CONFIG_BT_ISO)
#define MAX_EVENT_COUNT CONFIG_BT_ISO_MAX_CHAN
#endif /* CONFIG_BT_CONN */

#if defined(CONFIG_BT_CONN) || defined(CONFIG_BT_ISO)
#define NUM_COMLETE_EVENT_SIZE                                                                     \
	BT_BUF_EVT_SIZE(sizeof(struct bt_hci_cp_host_num_completed_packets) +                      \
			MAX_EVENT_COUNT * sizeof(struct bt_hci_handle_count))
/* Dedicated pool for HCI_Number_of_Completed_Packets. This event is always
 * consumed synchronously by bt_recv_prio() so a single buffer is enough.
 * Having a dedicated pool for it ensures that exhaustion of the RX pool
 * cannot block the delivery of this priority event.
 */
NET_BUF_POOL_FIXED_DEFINE(num_complete_pool, 1, NUM_COMLETE_EVENT_SIZE, sizeof(struct bt_buf_data),
			  NULL);
#endif /* CONFIG_BT_CONN || CONFIG_BT_ISO */

NET_BUF_POOL_FIXED_DEFINE(discardable_pool, CONFIG_BT_BUF_EVT_DISCARDABLE_COUNT,
			  BT_BUF_EVT_SIZE(CONFIG_BT_BUF_EVT_DISCARDABLE_SIZE),
			  sizeof(struct bt_buf_data), NULL);

#if defined(CONFIG_BT_HCI_ACL_FLOW_CONTROL)
NET_BUF_POOL_DEFINE(acl_in_pool, CONFIG_BT_BUF_ACL_RX_COUNT,
		    BT_BUF_ACL_SIZE(CONFIG_BT_BUF_ACL_RX_SIZE), sizeof(struct acl_data),
		    bt_hci_host_num_completed_packets);

NET_BUF_POOL_FIXED_DEFINE(evt_pool, CONFIG_BT_BUF_EVT_RX_COUNT, BT_BUF_EVT_RX_SIZE,
			  sizeof(struct bt_buf_data), NULL);
#else
NET_BUF_POOL_FIXED_DEFINE(hci_rx_pool, BT_BUF_RX_COUNT, BT_BUF_RX_SIZE, sizeof(struct bt_buf_data),
			  NULL);
#endif /* CONFIG_BT_HCI_ACL_FLOW_CONTROL */

BUILD_ASSERT(IS_ENABLED(CONFIG_BT_HCI_ACL_FLOW_CONTROL));
BUILD_ASSERT(!IS_ENABLED(CONFIG_BT_ISO_UNICAST));
BUILD_ASSERT(!IS_ENABLED(CONFIG_BT_ISO_SYNC_RECEIVER));

struct net_buf *bt_buf_get_evt_num_completed(k_timeout_t timeout);
struct net_buf *bt_buf_get_cmd_complete(k_timeout_t timeout);
static struct net_buf *bt_buf_get_rx_evt(uint8_t evt_code)
{
	switch (evt_code) {
	case BT_HCI_EVT_NUM_COMPLETED_PACKETS:
		return bt_buf_get_evt_num_completed(K_FOREVER);
	case BT_HCI_EVT_CMD_COMPLETE:
	case BT_HCI_EVT_CMD_STATUS:
		return bt_buf_get_cmd_complete(K_FOREVER);
	default:
		return net_buf_alloc(&evt_pool, K_FOREVER);
	}
}

struct net_buf *bt_buf_get_rx(uint8_t h4_peek[4])
{
	struct net_buf *buf = NULL;

	switch (h4_type) {
	case H4_EVT:
		buf = bt_buf_get_rx_evt(h4_peek[1]);
		type = BT_BUF_EVT;
		break;
	case H4_ACL:
		buf = net_buf_alloc(&acl_in_pool, K_FOREVER);
		type = BT_BUF_ACL_IN;
		break;
	}

	if (buf) {
		net_buf_reserve(buf, BT_BUF_RESERVE);
		bt_buf_set_type(buf, type);
	}

	return buf;
}

struct net_buf *bt_buf_get_cmd_complete(k_timeout_t timeout)
{
	struct net_buf *buf;

	buf = (struct net_buf *)atomic_ptr_clear((atomic_ptr_t *)&bt_dev.sent_cmd);
	if (buf) {
		bt_buf_set_type(buf, BT_BUF_EVT);
		buf->len = 0U;
		net_buf_reserve(buf, BT_BUF_RESERVE);

		return buf;
	}

	return bt_buf_get_rx(BT_BUF_EVT, timeout);
}

struct net_buf *bt_buf_get_evt_discardable(k_timeout_t timeout)
{
	struct net_buf *buf;

	buf = net_buf_alloc(&discardable_pool, timeout);
	if (buf) {
		net_buf_reserve(buf, BT_BUF_RESERVE);
		bt_buf_set_type(buf, BT_BUF_EVT);
	}

	return buf;
}

struct net_buf *bt_buf_get_evt_num_completed(k_timeout_t timeout)
{
	struct net_buf *buf;

	buf = net_buf_alloc(&num_complete_pool, timeout);
	if (buf) {
		net_buf_reserve(buf, BT_BUF_RESERVE);
		bt_buf_set_type(buf, BT_BUF_EVT);
	}

	return buf;
}

struct net_buf *bt_buf_get_evt(uint8_t evt, bool discardable, k_timeout_t timeout)
{
	switch (evt) {
#if defined(CONFIG_BT_CONN) || defined(CONFIG_BT_ISO)
	case BT_HCI_EVT_NUM_COMPLETED_PACKETS:
		return bt_buf_get_evt_num_completed(timeout);
#endif /* CONFIG_BT_CONN || CONFIG_BT_ISO */
	case BT_HCI_EVT_CMD_COMPLETE:
	case BT_HCI_EVT_CMD_STATUS:
		return bt_buf_get_cmd_complete(timeout);
	default:
		return bt_buf_get_rx(BT_BUF_EVT, timeout);
}

#if defined(CONFIG_BT_HCI_ACL_FLOW_CONTROL)
struct net_buf_pool *bt_buf_get_evt_pool(void)
{
	return &evt_pool;
}

struct net_buf_pool *bt_buf_get_acl_in_pool(void)
{
	return &acl_in_pool;
}
#else
struct net_buf_pool *bt_buf_get_hci_rx_pool(void)
{
	return &hci_rx_pool;
}
#endif /* CONFIG_BT_HCI_ACL_FLOW_CONTROL */

#if defined(CONFIG_BT_BUF_EVT_DISCARDABLE_COUNT)
struct net_buf_pool *bt_buf_get_discardable_pool(void)
{
	return &discardable_pool;
}
#endif /* CONFIG_BT_BUF_EVT_DISCARDABLE_COUNT */

#if defined(CONFIG_BT_CONN) || defined(CONFIG_BT_ISO)
struct net_buf_pool *bt_buf_get_num_complete_pool(void)
{
	return &num_complete_pool;
}
#endif /* CONFIG_BT_CONN || CONFIG_BT_ISO */

typedef struct net_buf *bt_alloc_func(k_timeout_t timeout);
