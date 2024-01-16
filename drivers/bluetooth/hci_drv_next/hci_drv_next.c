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
	enum hci_drv_result result;

	result = hci_drv_process_rx();

	if (result == HCI_DRV_RESULT_RX_FAILURE) {
		return -EIO;
	}

	if (busy && result == HCI_DRV_RESULT_NOTHING) {
		return -EAGAIN;
	}

	busy = false;
	return 0;
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

static int rx_poll_base(void)
{
	int err;

	err = rx_poll();
	if (err == -EIO) {
		return -EIO;
	}
	__ASSERT_NO_MSG(err == 0 || err = -EAGAIN);

	if (busy) {
		return 0;
	}

	return 0;
}

static int rx_poll_hci_type(void)
{
	int err;
	struct k_poll_event ev[] = {
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY,
					 hci_drv_signal_rx),
	};

	err = k_poll(ev, ARRAY_SIZE(ev), K_NO_WAIT);
	if (err) {
		return err;
	}

	err = hci_drv_read_into(peek_buf, 1);
	if (err) {
		return err;
	}

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

static int hci_drv_next_send(struct net_buf *buf)
{
	int err;
	int result = HCI_DRV_RESULT_NOTHING;
	struct k_poll_event ev[] = {
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY,
					 hci_drv_signal_tx),
	};

	err = hci_drv_write_from(buf->data, buf->len);
	if (err) {
		return -EIO;
	}

	/* Old-style drivers have blocking TX. */
	while (result == HCI_DRV_RESULT_NOTHING) {
		err = k_poll(ev, ARRAY_SIZE(ev), K_FOREVER);
		__ASSERT_NO_MSG(err == 0);
		k_poll_signal_reset(hci_drv_signal_tx);
		result = hci_drv_process_tx();
	}

	net_buf_unref(buf);

	if (result != HCI_DRV_RESULT_TX_SUCCESS) {
		return -EIO;
	}

	return 0;
}

static int is_unsolicited_cmd_complete(uint8_t *data, size_t len)
{
}

static book is_evt_discardable(


static int read_data(uint8_t *data, size_t len)
{
	int err;
	int result = HCI_DRV_RESULT_NOTHING;
	struct k_poll_event ev[] = {
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY,
					 hci_drv_signal_rx),
	};

	err = hci_drv_read_into(data, len);
	if (err) {
		return -EIO;
	}

	/* Old-style drivers have blocking TX. */
	while (result == HCI_DRV_RESULT_NOTHING) {
		err = k_poll(ev, ARRAY_SIZE(ev), K_FOREVER);
		__ASSERT_NO_MSG(err == 0);
		k_poll_signal_reset(hci_drv_signal_rx);
		result = hci_drv_process_rx();
	}

	if (result != HCI_DRV_RESULT_TX_SUCCESS) {
		return -EIO;
	}

	return 0;
}

int read_data_into_netbuf(struct net_buf *buf, size_t read_size)
{
}

uint8_t rx_data[4];
size_t rx_data_len;

static inline void get_evt_hdr(void)
{
	struct bt_hci_evt_hdr *hdr = &rx.evt;

	h4_read_hdr();

	if (rx.hdr_len == sizeof(*hdr) && rx.remaining < sizeof(*hdr)) {
		switch (rx.evt.evt) {
		case BT_HCI_EVT_LE_META_EVENT:
			rx.remaining++;
			rx.hdr_len++;
			break;
#if defined(CONFIG_BT_BREDR)
		case BT_HCI_EVT_INQUIRY_RESULT_WITH_RSSI:
		case BT_HCI_EVT_EXTENDED_INQUIRY_RESULT:
			rx.discardable = true;
			break;
#endif
		}
	}

	if (!rx.remaining) {
		if (rx.evt.evt == BT_HCI_EVT_LE_META_EVENT &&
		    (rx.hdr[sizeof(*hdr)] == BT_HCI_EVT_LE_ADVERTISING_REPORT)) {
			LOG_DBG("Marking adv report as discardable");
			rx.discardable = true;
		}

		rx.remaining = hdr->len - (rx.hdr_len - sizeof(*hdr));
		LOG_DBG("Got event header. Payload %u bytes", hdr->len);
		rx.have_hdr = true;
	}
}

static void rx_thread_entry(void *p1, void *p2, void *p3)
{
	int err;
	struct net_buf *buf;
	uint8_t h4_type = H4_NONE;

	while (true) {
		if (!buf) {
			if (h4_type == H4_NONE) {
				err = read_data(&h4_type, 1);
				if (err) {
					LOG_ERR("Failed to read h4 type");
				}
			}

			if (h4_type == H4_ACL) {
				buf = bt_buf_get_rx(BT_BUF_ACL_IN, K_FOREVER);
				err = read_data(&buf->data, BT_HCI_ACL_HDR_SIZE);
				if (err) {
					LOG_ERR("Failed to read acl header");
				}
				net_buf_add(BT_HCI_ACL_HDR_SIZE);
				err = read_data(&buf->data, BT_HCI_ACL_HDR_SIZE);
				net_buf_add(acl_payload_length);
			}

			if (h4_type == H4_ISO) {
				/* Implement timeout for ISO? */
				buf = bt_buf_get_rx(BT_BUF_ISO_IN, K_FOREVER);
			}

			if (h4_type == H4_EVT) {
				buf = bt_buf_get_evt(evt_code, discardable, K_FOREVER);
			}
		}

		if (err) {
			/* Resync not implemented */
			LOG_ERR("Read error");
			k_oops();
		}
	}

	if (is_unsolicited_cmd_complete(data, len)) {
		buf = bt_buf_get_cmd_complete(timeout);
	} else if (is_evt(data, len)) {
		buf = bt_buf_get_evt(rx.evt.evt, rx.discardable, timeout);
	} else if (is_acl(data, len)) {
		buf = bt_buf_get_rx(BT_BUF_ACL_IN, timeout);
	} else if (is_iso(data, len)) {
		buf = bt_buf_get_rx(BT_BUF_ISO_IN, timeout);
	} else {
		/* Ignore */
	}

	/* Use this function on unsolicited cmd complete. */
	bt_buf_get_rx(BT_BUF_EVT, timeout)
		/* Use this function for any other evt. */
		bt_buf_get_evt(rx.evt.evt, rx.discardable, timeout);
	/* Use this function for ACL and ISO. */
	buf = bt_buf_get_rx(BT_BUF_ISO_IN, timeout);
}

static K_KERNEL_STACK_DEFINE(rx_thread_stack, CONFIG_BT_DRV_RX_STACK_SIZE);
static struct k_thread rx_thread_data;
static rx_thread_start(void)
{
	(void)k_thread_create(&rx_thread_data, rx_thread_stack,
			      K_KERNEL_STACK_SIZEOF(rx_thread_stack), rx_thread_entry, NULL, NULL,
			      NULL, K_PRIO_COOP(CONFIG_BT_RX_PRIO), 0, K_NO_WAIT);
}

static int hci_drv_next_open(void)
{
	int ret;

	k_poll_signal_init(&hci_drv_signal_rx);
	k_poll_signal_init(&hci_drv_signal_tx);

	ret = hci_drv_init();
	if (ret < 0) {
		return -EIO;
	}

	rx_thread_start();

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
