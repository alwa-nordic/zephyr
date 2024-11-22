/*
 * Copyright (c) 2019-2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/bluetooth.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include <zephyr/ipc/ipc_service.h>

#include <zephyr/net_buf.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/buf.h>
#include <zephyr/bluetooth/hci_raw.h>
#include <zephyr/bluetooth/hci_vs.h>

#include <zephyr/logging/log_ctrl.h>
#include <zephyr/logging/log.h>
#include <zephyr/devicetree.h>

LOG_MODULE_REGISTER(hci_ipc, CONFIG_BT_LOG_LEVEL);

#if DT_HAS_CHOSEN(zephyr_bt_hci)
#define BT_HCI_NODE   DT_CHOSEN(zephyr_bt_hci)
#define BT_HCI_DEV    DEVICE_DT_GET(BT_HCI_NODE)
#define BT_HCI_BUS    BT_DT_HCI_BUS_GET(BT_HCI_NODE)
#define BT_HCI_NAME   BT_DT_HCI_NAME_GET(BT_HCI_NODE)
#else
/* The zephyr,bt-hci chosen property is mandatory, except for unit tests */
BUILD_ASSERT(IS_ENABLED(CONFIG_ZTEST), "Missing DT chosen property for HCI");
#define BT_HCI_DEV    NULL
#define BT_HCI_BUS    0
#define BT_HCI_NAME   ""
#endif

/* Undefined reference? Make sure the HCI driver is compiled in. */
static const struct device *hci_dev = BT_HCI_DEV;
static struct ipc_ept hci_ept;

static struct k_poll_signal ipc_bound_signal = K_POLL_SIGNAL_INITIALIZER(ipc_bound_signal);
#if defined(CONFIG_BT_CTLR_ASSERT_HANDLER) || defined(CONFIG_BT_HCI_VS_FATAL_ERROR)
/* A flag used to store information if the IPC endpoint has already been bound. The end point can't
 * be used before that happens.
 */
static bool ipc_ept_ready;
#endif /* CONFIG_BT_CTLR_ASSERT_HANDLER || CONFIG_BT_HCI_VS_FATAL_ERROR */

#define HCI_FATAL_ERR_MSG true
#define HCI_REGULAR_MSG false

NET_BUF_POOL_FIXED_DEFINE(tx_pool, 1, 0, sizeof(struct bt_buf_data), NULL);

static void hci_ipc_rx(uint8_t *data, size_t len)
{
	int err;
	uint8_t pkt_indicator;
	struct net_buf *buf = NULL;
	size_t remaining = len;

	LOG_HEXDUMP_DBG(data, len, "IPC data:");

	pkt_indicator = *data++;
	remaining -= sizeof(pkt_indicator);

	buf = net_buf_alloc_with_data(&tx_pool, data, remaining, K_NO_WAIT);
	bt_buf_set_type(buf, bt_buf_h4_type_to_out_type(pkt_indicator));

	err = bt_hci_send(hci_dev, buf);

	if (err) {
		LOG_ERR("Driver failed: %d", err);
		k_oops();
	}
}

static void hci_ipc_send(struct net_buf *buf, bool is_fatal_err)
{
	uint8_t pkt_indicator;
	uint8_t retries = 0;
	int ret;

	LOG_DBG("buf %p type %u len %u", buf, bt_buf_get_type(buf), buf->len);

	LOG_HEXDUMP_DBG(buf->data, buf->len, "Controller buffer:");

	switch (bt_buf_get_type(buf)) {
	case BT_BUF_ACL_IN:
		pkt_indicator = BT_HCI_H4_ACL;
		break;
	case BT_BUF_EVT:
		pkt_indicator = BT_HCI_H4_EVT;
		break;
	case BT_BUF_ISO_IN:
		pkt_indicator = BT_HCI_H4_ISO;
		break;
	default:
		LOG_ERR("Unknown type %u", bt_buf_get_type(buf));
		net_buf_unref(buf);
		return;
	}
	net_buf_push_u8(buf, pkt_indicator);

	LOG_HEXDUMP_DBG(buf->data, buf->len, "Final HCI buffer:");

	do {
		ret = ipc_service_send(&hci_ept, buf->data, buf->len);
		if (ret < 0) {
			retries++;
			if (retries > 10) {
				/* Default backend (rpmsg_virtio) has a timeout of 150ms. */
				LOG_WRN("IPC send has been blocked for 1.5 seconds.");
				retries = 0;
			}

			/* The function can be called by the application main thread,
			 * bt_ctlr_assert_handle and k_sys_fatal_error_handler. In case of a call by
			 * Bluetooth Controller assert handler or system fatal error handler the
			 * call can be from ISR context, hence there is no thread to yield. Besides
			 * that both handlers implement a policy to provide error information and
			 * stop the system in an infinite loop. The goal is to prevent any other
			 * damage to the system if one of such exeptional situations occur, hence
			 * call to k_yield is against it.
			 */
			if (is_fatal_err) {
				LOG_ERR("IPC service send error: %d", ret);
			} else {
				/* In the POSIX ARCH, code takes zero simulated time to execute,
				 * so busy wait loops become infinite loops, unless we
				 * force the loop to take a bit of time.
				 *
				 * This delay allows the IPC consumer to execute, thus making
				 * it possible to send more data over IPC afterwards.
				 */
				Z_SPIN_DELAY(500);
				k_yield();
			}
		}
	} while (ret < 0);

	LOG_INF("Sent message of %d bytes.", ret);

	net_buf_unref(buf);
}

#if defined(CONFIG_BT_CTLR_ASSERT_HANDLER)
void bt_ctlr_assert_handle(char *file, uint32_t line)
{
	/* Disable interrupts, this is unrecoverable */
	(void)irq_lock();

#if defined(CONFIG_BT_HCI_VS_FATAL_ERROR)
	/* Generate an error event only when IPC service endpoint is already bound. */
	if (ipc_ept_ready) {
		/* Prepare vendor specific HCI debug event */
		struct net_buf *buf;

		buf = hci_vs_err_assert(file, line);
		if (buf != NULL) {
			/* Send the event over ipc */
			hci_ipc_send(buf, HCI_FATAL_ERR_MSG);
		} else {
			LOG_ERR("Can't create Fatal Error HCI event: %s at %d", __FILE__, __LINE__);
		}
	} else {
		LOG_ERR("IPC endpoint is not ready yet: %s at %d", __FILE__, __LINE__);
	}

	LOG_ERR("Halting system");

#else /* !CONFIG_BT_HCI_VS_FATAL_ERROR */
	LOG_ERR("Controller assert in: %s at %d", file, line);

#endif /* !CONFIG_BT_HCI_VS_FATAL_ERROR */

	/* Flush the logs before locking the CPU */
	LOG_PANIC();

	while (true) {
	};
}
#endif /* CONFIG_BT_CTLR_ASSERT_HANDLER */

#if defined(CONFIG_BT_HCI_VS_FATAL_ERROR)
void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	/* Disable interrupts, this is unrecoverable */
	(void)irq_lock();

	/* Generate an error event only when there is a stack frame and IPC service endpoint is
	 * already bound.
	 */
	if (esf != NULL && ipc_ept_ready) {
		/* Prepare vendor specific HCI debug event */
		struct net_buf *buf;

		buf = hci_vs_err_stack_frame(reason, esf);
		if (buf != NULL) {
			hci_ipc_send(buf, HCI_FATAL_ERR_MSG);
		} else {
			LOG_ERR("Can't create Fatal Error HCI event.\n");
		}
	}

	LOG_ERR("Halting system");

	/* Flush the logs before locking the CPU */
	LOG_PANIC();

	while (true) {
	};

	CODE_UNREACHABLE;
}
#endif /* CONFIG_BT_HCI_VS_FATAL_ERROR */

static void hci_ept_bound(void *priv)
{
#if defined(CONFIG_BT_CTLR_ASSERT_HANDLER) || defined(CONFIG_BT_HCI_VS_FATAL_ERROR)
	ipc_ept_ready = true;
#endif /* CONFIG_BT_CTLR_ASSERT_HANDLER || CONFIG_BT_HCI_VS_FATAL_ERROR */
	k_poll_signal_raise(&ipc_bound_signal, 0);
}

static void hci_ept_recv(const void *data, size_t len, void *priv)
{
	LOG_INF("Received message of %u bytes.", len);
	hci_ipc_rx((uint8_t *) data, len);
}

static struct ipc_ept_cfg hci_ept_cfg = {
	.name = "nrf_bt_hci",
	.cb = {
		.bound    = hci_ept_bound,
		.received = hci_ept_recv,
	},
};

int signal_wait(struct k_poll_signal *signal)
{
	struct k_poll_event evs[] = {
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL,
					 K_POLL_MODE_NOTIFY_ONLY,
					 signal),
	};

	return k_poll(evs, ARRAY_SIZE(evs), K_FOREVER);
}

int bt_hci_recv(const struct device *dev, struct net_buf *buf)
{
	signal_wait(&ipc_bound_signal);

	hci_ipc_send(buf, HCI_FATAL_ERR_MSG);

	return 0;
}

/* ZLL needs two buffers here to not hang.
 *
 * The second buffer is probably needed when ZLL allocates while
 * holding up the IPC thread. This needs to be investigated
 * further.
 */
NET_BUF_POOL_FIXED_DEFINE(rx_pool, 2, BT_BUF_RX_SIZE, sizeof(struct bt_buf_data), NULL);

struct net_buf *bt_buf_get_rx(enum bt_buf_type type, k_timeout_t timeout)
{
	struct net_buf *buf;

	buf = net_buf_alloc(&rx_pool, timeout);
	bt_buf_set_type(buf, type);

	return buf;
}

struct net_buf *bt_buf_get_evt(uint8_t evt, bool discardable, k_timeout_t timeout)
{
	return bt_buf_get_rx(BT_BUF_EVT, timeout);
}

int main(void)
{
	int err;
	const struct device *hci_ipc_instance =
		DEVICE_DT_GET(DT_CHOSEN(zephyr_bt_hci_ipc));

	/* incoming events and data from the controller */
	static K_FIFO_DEFINE(rx_queue);

	LOG_DBG("Start");

	/* Enable the raw interface, this will in turn open the HCI driver */
	err = bt_hci_open(hci_dev, bt_hci_recv);
	if (err) {
		LOG_ERR("HCI driver open failed (%d)", err);
		return err;
	}

	/* Initialize IPC service instance and register endpoint. */
	err = ipc_service_open_instance(hci_ipc_instance);
	if (err < 0 && err != -EALREADY) {
		LOG_ERR("IPC service instance initialization failed: %d\n", err);
	}

	err = ipc_service_register_endpoint(hci_ipc_instance, &hci_ept, &hci_ept_cfg);
	if (err) {
		LOG_ERR("Registering endpoint failed with %d", err);
	}

	return 0;
}
