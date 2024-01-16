/* Copyright (c) 2024 Nordic Semiconductor
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/drivers/bluetooth/hci_driver.h>
#include <zephyr/drivers/bluetooth/hci_drv_next.h>

#include <errno.h>
#include <stddef.h>

#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(bt_driver, CONFIG_BT_HCI_DRIVER_LOG_LEVEL);

/*

rename 'next' to 'shim'?

*/

/* Not implemented */
BUILD_ASSERT(!IS_ENABLED(CONFIG_BT_ISO));



#define BT_HCI_EVT_CMD_COMPLETE_SIZE sizeof(struct bt_hci_evt_cmd_complete)
static int rx_alloc_h4_evt_cmd_complete(uint8_t peek_buf, uint8_t peek_len, struct net_buf **buf)
{
	uint16_t opcode;
	size_t peek_need = 0;

	if (peek_len < BT_HCI_EVT_CMD_COMPLETE_SIZE) {
		return BT_HCI_EVT_CMD_COMPLETE_SIZE - peek_len;
	}

	opcode = sys_get_le16(&peek_buf[1]);

	if (opcode == 0) {
		*buf = bt_buf_get_rx(BT_BUF_EVT, K_FOREVER);
	} else {
		*buf = bt_buf_get_cmd_complete(opcode, K_FOREVER);
	}

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

	/* A non-zero opcode responds to a command. Events that respond to a
	 * command must use the special allocator.
	 */
	if (opcode != 0) {
		/* Special allocator for command responses. */
		*buf = bt_buf_get_cmd_complete(opcode, K_FOREVER);
	} else {
		/* Otherwise, treat it like a normal event. It conveys only
		 * flow-control information.
		 */
		*buf = bt_buf_get_rx(BT_BUF_EVT, K_FOREVER);
	}

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

static uint8_t *active_buf;

static int rx_poll_base(void)
{
	int err;
	struct net_buf *buf = NULL;

	err = rx_poll();
	if (err) {
		/* `err` can be `-EAGAIN`, which is not a error, but is still an
		 * early return.
		 */
		return err;
	}


	int ret = rx_alloc_h4(active_buf, peek_len, &buf);
	if (ret < 0) {
		return ret;
	} else if (ret == 0) {
		bt_recv(buf);
	} else {
		err = rx_peek_add(ret);
		if (err) {
			return err;
		}
	}

	return 0;
}

static int sig_take(struct k_poll_signal *signal, k_timeout_t timeout)
{
	int err;
	struct k_poll_event ev[] = {
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, signal),
	};

	err = k_poll(ev, ARRAY_SIZE(ev), timeout);
	if (err) {
		return err;
	}

	k_poll_signal_reset(signal);

	return 0;
}



struct k_poll_signal *hci_drv_signal_rx;
struct k_poll_signal *hci_drv_signal_tx;
/* ISR */
void hci_drv_wakeup(bool rx, bool tx)
{
	if (rx) {
		k_poll_signal_raise(&hci_drv_signal_rx, 0);
	}
	if (tx) {
		k_poll_signal_raise(&hci_drv_signal_tx, 0);
	}
}


static int blocking_write(uint8_t *src, size_t len)
{
	int err = -EAGAIN;

	err = hci_drv_write_from(src, len);
	if (err) {
		return -EIO;
	}

	/* Old-style drivers have blocking TX. */
	while (err == -EAGAIN) {
		err = sig_take(hci_drv_signal_tx, K_FOREVER);
		__ASSERT_NO_MSG(err == 0);
		err = rx_poll();
	}

	return err;
}

static int hci_drv_next_send(struct net_buf *buf)
{
	int err;
	int result = HCI_DRV_RESULT_NOTHING;

	err = hci_drv_write_from(buf->data, buf->len);
	if (err) {
		return -EIO;
	}

	/* Old-style drivers have blocking TX. */
	while (result == HCI_DRV_RESULT_NOTHING) {
		err = sig_take(hci_drv_signal_tx, K_FOREVER);
		__ASSERT_NO_MSG(!err);
		result = hci_drv_process_tx();
	}

	net_buf_unref(buf);

	if (result != HCI_DRV_RESULT_TX_SUCCESS) {
		return -EIO;
	}

	return 0;
}


static bool busy;
static uint8_t peek_buf[4];
static size_t peek_len;

static int rx_peek_add(size_t len)
{
	int err;

	__ASSERT_NO_MSG(!busy);
	__ASSERT_NO_MSG((peek_len + len) <= sizeof(peek_buf));

	err = hci_drv_read_into(peek_buf, len);
	if (err) {
		return err;
	}

	busy = true;
	peek_len += len;

	return 0;
}

static int rx_poll(void)
{
	int result;

	result = hci_drv_process_rx();

	if (result == HCI_DRV_RESULT_RX_FAILURE) {
		return -EIO;
	}

	if (busy && result == -EAGAIN) {
		return -EAGAIN;
	}

	busy = false;
	return 0;
}

static int blocking_read(uint8_t *dst, size_t len)
{
	int err = -EAGAIN;

	err = hci_drv_read_into(dst, len);
	if (err) {
		return -EIO;
	}

	/* Old-style drivers have blocking TX. */
	while (err == -EAGAIN) {
		err = sig_take(hci_drv_signal_rx, K_FOREVER);
		__ASSERT_NO_MSG(err == 0);
		err = rx_poll();
	}

	return err;
}


static void rx_thread_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (1) {
	}
}

static K_KERNEL_STACK_DEFINE(rx_thread_stack, CONFIG_BT_DRV_RX_STACK_SIZE);
static struct k_thread rx_thread_data;
static int hci_drv_next_open(void)
{
	int ret;

	k_poll_signal_init(&hci_drv_signal_rx);
	k_poll_signal_init(&hci_drv_signal_tx);

	ret = hci_drv_init();
	if (ret < 0) {
		return -EIO;
	}

	(void)k_thread_create(&rx_thread_data, rx_thread_stack,
			      K_KERNEL_STACK_SIZEOF(rx_thread_stack), rx_thread_entry, NULL, NULL,
			      NULL, K_PRIO_COOP(CONFIG_BT_RX_PRIO), 0, K_NO_

	return 0;
}

/* Not implemented */
BUILD_ASSERT(!IS_ENABLED(CONFIG_BT_HCI_SETUP));

static const struct bt_hci_driver drv = {
	.name = "H:4",
	.bus = BT_HCI_DRIVER_BUS_UART,
	.open = hci_drv_next_open,
	.send = hci_drv_next_send,
};

static int bt_uart_init(void)
{
	if (!device_is_ready(h4_dev)) {
		return -ENODEV;
	}

	bt_hci_driver_register(&drv);

	return 0;
}

SYS_INIT(bt_uart_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);
