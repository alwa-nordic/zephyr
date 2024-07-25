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
#include <zephyr/logging/log.h>
#include <zephyr/net/buf.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(bt_hci_c2hfc, CONFIG_BT_HCI_CORE_LOG_LEVEL);

static void bt_conn_trigger_acl_ack_processor(void)
{
	atomic_set_bit(bt_dev.flags, BT_DEV_WORK_ACL_DATA_ACK);
	bt_tx_irq_raise();
}

static bool bt_hci_c2hfc_is_enabled(void)
{
	/* We always enable ACL flow control if available in both the
	 * host and controller.
	 */
	return IS_ENABLED(CONFIG_BT_HCI_ACL_FLOW_CONTROL) &&
	       BT_CMD_TEST(bt_dev.supported_commands, 10, 5);
}

void bt_hci_c2hfc_ack(struct bt_conn *conn)
{
	if (!bt_hci_c2hfc_is_enabled()) {
		return;
	}

	atomic_inc(&conn->acl_ack_outbox);
	bt_conn_trigger_acl_ack_processor();
}

static int set_flow_control(void)
{
	struct bt_hci_cp_host_buffer_size *hbs;
	struct net_buf *buf;
	int err;

	/* Check if host flow control is actually supported */
	if (!BT_CMD_TEST(bt_dev.supported_commands, 10, 5)) {
		LOG_WRN("Controller to host flow control not supported");
		return 0;
	}

	buf = bt_hci_cmd_create(BT_HCI_OP_HOST_BUFFER_SIZE, sizeof(*hbs));
	if (!buf) {
		return -ENOBUFS;
	}

	hbs = net_buf_add(buf, sizeof(*hbs));
	(void)memset(hbs, 0, sizeof(*hbs));
	hbs->acl_mtu = sys_cpu_to_le16(CONFIG_BT_BUF_ACL_RX_SIZE);
	hbs->acl_pkts = sys_cpu_to_le16(CONFIG_BT_BUF_ACL_RX_COUNT);

	err = bt_hci_cmd_send_sync(BT_HCI_OP_HOST_BUFFER_SIZE, buf, NULL);
	if (err) {
		return err;
	}

	buf = bt_hci_cmd_create(BT_HCI_OP_SET_CTL_TO_HOST_FLOW, 1);
	if (!buf) {
		return -ENOBUFS;
	}

	net_buf_add_u8(buf, BT_HCI_CTL_TO_HOST_FLOW_ENABLE);
	return bt_hci_cmd_send_sync(BT_HCI_OP_SET_CTL_TO_HOST_FLOW, buf, NULL);
}

int bt_hci_c2hfc_bt_init(void)
{
	if (IS_ENABLED(CONFIG_BT_HCI_ACL_FLOW_CONTROL)) {
		return set_flow_control();
	}

	return 0;
}

static void bt_hci_c2hfc_pool_destroy(struct net_buf *buf)
{
	net_buf_destroy(buf);

	/* Go back to process_acl_data_ack(), as documented over there. */
	bt_conn_trigger_acl_ack_processor();
}

#define HOST_NUM_COMPLETE_EVT_SIZE_MAX                                                             \
	BT_BUF_EVT_SIZE(sizeof(struct bt_hci_cp_host_num_completed_packets) +                      \
			CONFIG_BT_MAX_CONN * sizeof(struct bt_hci_handle_count))

NET_BUF_POOL_DEFINE(bt_hci_c2hfc_pool, 1, HOST_NUM_COMPLETE_EVT_SIZE_MAX,
		    sizeof(struct bt_buf_data), bt_hci_c2hfc_pool_destroy);

static struct net_buf *acl_ack_cmd_new(void)
{
	struct net_buf *buf;
	struct bt_hci_cmd_hdr *cmd_hdr;
	struct bt_hci_cp_host_num_completed_packets *cp;

	buf = net_buf_alloc(&bt_hci_c2hfc_pool, K_NO_WAIT);
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

bool bt_hci_c2hfc_process_tx(void)
{
	int err;
	struct net_buf *buf = NULL;
	struct bt_conn *conn = NULL;

	if (!atomic_test_and_clear_bit(bt_dev.flags, BT_DEV_WORK_ACL_DATA_ACK)) {
		return false;
	}

	for (uint8_t i = 0; i < CONFIG_BT_MAX_CONN; i++) {
		uint16_t ack_count;

		if (conn) {
			bt_conn_unref(conn);
		}

		conn = bt_conn_lookup_index(i);
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

		if (err) {
			__ASSERT_NO_MSG(err == -ENOMEM);
			/* We have filled up the packet but there is more. */
			bt_conn_trigger_acl_ack_processor();
			break;
		}

		atomic_sub(&conn->acl_ack_outbox, ack_count);
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
