/* Copyright (c) 2024 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief HCI Controller to Host Flow Control
 */

#include "conn_internal.h"
#include "hci_core.h"

#include <zephyr/bluetooth/buf.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/net/buf.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>


#define HOST_NUM_COMPLETE_EVT_SIZE_MAX                                                             \
	BT_BUF_EVT_SIZE(sizeof(struct bt_hci_cp_host_num_completed_packets) +                      \
			CONFIG_BT_MAX_CONN * sizeof(struct bt_hci_handle_count))

void bt_conn_trigger_acl_ack_processor(void)
{
	atomic_set_bit(bt_dev.flags, BT_DEV_WORK_ACL_DATA_ACK);
	bt_tx_irq_raise();
}

static void host_flow_control_control_pool_free(struct net_buf *buf)
{
	net_buf_destroy(buf);

	/* Go back to process_acl_data_ack(), as documented over there. */
	bt_conn_trigger_acl_ack_processor();
}

NET_BUF_POOL_DEFINE(host_flow_control_control_pool, 1, HOST_NUM_COMPLETE_EVT_SIZE_MAX,
		    sizeof(struct bt_buf_data), host_flow_control_control_pool_free);

static struct net_buf *acl_ack_cmd_new(void)
{
	struct net_buf *buf;
	struct bt_hci_cmd_hdr *cmd_hdr;
	struct bt_hci_cp_host_num_completed_packets *cp;

	buf = net_buf_alloc(&host_flow_control_control_pool, K_NO_WAIT);
	if (!buf) {
		return NULL;
	}

	net_buf_reserve(buf, BT_BUF_RESERVE);
	bt_buf_set_type(buf, BT_BUF_CMD);

	cmd_hdr = net_buf_add(buf, sizeof(*cmd_hdr));
	cmd_hdr->opcode = sys_cpu_to_le16(BT_HCI_OP_HOST_NUM_COMPLETED_PACKETS);
	cmd_hdr->param_len = sizeof(*cp);

	cp = net_buf_add(buf, sizeof(*cp));
	cp->num_handles = 0;

	return buf;
}

static int acl_ack_cmd_append(struct net_buf *buf, uint16_t handle, uint16_t ack_count)
{
	struct bt_hci_handle_count item = {
		.handle = sys_cpu_to_le16(handle),
		.count = sys_cpu_to_le16(ack_count),
	};

	struct bt_hci_cp_host_num_completed_packets *cp;
	struct bt_hci_cmd_hdr *cmd_hdr;

	cmd_hdr = (void *)buf->data;
	cp = (void *)&buf->data[sizeof(*cmd_hdr)];

	if (net_buf_tailroom(buf) < sizeof(item)) {
		return -ENOMEM;
	}

	net_buf_add_mem(buf, &item, sizeof(item));
	cp->num_handles++;
	cmd_hdr->param_len += sizeof(item);

	return 0;
}

static void bt_conn_lookup_index_exchange(struct bt_conn **connp, uint8_t index)
{
	__ASSERT_NO_MSG(connp);

	if (*connp) {
		bt_conn_unref(*connp);
	}

	*connp = bt_conn_lookup_index(index);
}

bool process_acl_data_ack(void)
{
	int err;
	struct net_buf *buf = NULL;
	struct bt_conn *conn = NULL;

	if (!atomic_test_and_clear_bit(bt_dev.flags, BT_DEV_WORK_ACL_DATA_ACK)) {
		return false;
	}

	for (uint8_t i = 0; i < CONFIG_BT_MAX_CONN; i++) {
		uint16_t ack_count;

		bt_conn_lookup_index_exchange(&conn, i);
		if (!conn) {
			continue;
		}

		ack_count = atomic_get(&conn->acl_ack_outbox);
		if (!ack_count || conn->state != BT_CONN_CONNECTED) {
			continue;
		}

		/* Allocate once we know there is something to send. */
		if (!buf) {
			buf = acl_ack_cmd_new();
		}

		if (!buf) {
			/* Do not set `BT_DEV_WORK_ACL_DATA_ACK` here. We will continue
			 * from the pool destroy callback
			 * host_flow_control_control_pool_free().
			 */
			break;
		}

		err = acl_ack_cmd_append(buf, conn->handle, ack_count);

		if (!err) {
			atomic_sub(&conn->acl_ack_outbox, ack_count);
		} else {
			__ASSERT_NO_MSG(err == -ENOMEM);
			/* We have filled up the buf but there is more. */
			bt_conn_trigger_acl_ack_processor();
		}
	}

	if (conn) {
		bt_conn_unref(conn);
	}

	if (!buf) {
		/* There was nothing to send or unable to allocate. */
		return false;
	}

	err = bt_send(buf);
	if (err) {
		k_oops();
	}

	return true;
}
