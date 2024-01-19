#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(bt_driver, CONFIG_BT_HCI_DRIVER_LOG_LEVEL);

struct net_buf *read_one_pkt(void)
{
	int err;
	uint8_t h4_type;
	uint8_t peek_buf[4];

	err = hci_drv_read_sync(peek_buf, 4);
	__ASSERT_NO_MSG(err == 0);

	h4_type = peek_buf[0];

	/* h4_type */
	switch (h4_type) {
	case H4_EVT:
		read_size = peek_buf[2];
		break;
	case H4_ACL:
	case H4_ISO:
		read_size = peek_buf[3];
		break;
	}

	struct net_buf *buf = bt_buf_get_rx(peek_buf);
	/* Icky: The first byte hold the type. But this is
	 * already registered by `bt_buf_get_rx`.
	 */
	net_buf_add_mem(buf, &peek_buf[1], 3);

	err = hci_drv_read_sync(net_buf_add(buf, read_size), read_size);
	__ASSERT_NO_MSG(err == 0);

	switch (h4_type) {
	case H4_EVT:
		read_size = 0;
		break;
	case H4_ACL:
	case H4_ISO:
		read_size = buf->data[3] * 0x100;
		break;
	}

	if (read_size) {
		err = hci_drv_read_sync(net_buf_add(buf, read_size), read_size);
		__ASSERT_NO_MSG(err == 0);
	}

	return buf;
}


static int encapsulate_h4(struct net_buf *buf)
{
	uint8_t h4_type;

	switch (bt_buf_get_type(buf)) {
	case BT_BUF_H4:
		/* Already decorated. */
		return 0;
	case BT_BUF_CMD:
		h4_type = H4_CMD;
		break;
	case BT_BUF_EVT:
		h4_type = H4_EVT;
		break;
	case BT_BUF_ACL_OUT:
	case BT_BUF_ACL_IN:
		h4_type = H4_ACL;
		break;
	case BT_BUF_ISO_OUT:
	case BT_BUF_ISO_IN:
		h4_type = H4_ISO;
		break;
	default:
		return -EINVAL;
	}

	__ASSERT_NO_MSG(net_buf_headroom(buf) >= H4_HDR_SIZE);
	net_buf_push_u8(buf, h4_type);
	bt_buf_set_type(buf, BT_BUF_H4);
}

static int hci_drv_next_send(struct net_buf *buf)
{
	int err;

	err = encapsulate_h4(buf);
	if (err) {
		return err;
	}

	err = hci_drv_write_sync(buf->data, buf->len);
	if (err) {
		return err;
	}

	net_buf_unref(buf);
	return 0;
}

static void rx_thread_main(void *p1, void *p2, void *p3)
{
	while (1) {
		struct net_buf *pkt;

		pkt = read_one_pkt();
		bt_recv(pkt);
	}
}

static K_KERNEL_STACK_DEFINE(rx_thread_stack, CONFIG_BT_DRV_RX_STACK_SIZE);
static struct k_thread rx_thread_data;
static int hci_drv_next_open(void)
{
	int ret;

	ret = hci_drv_init();
	if (ret < 0) {
		return -EIO;
	}

	tid = k_thread_create(&rx_thread_data, rx_thread_stack,
			      K_KERNEL_STACK_SIZEOF(rx_thread_stack), rx_thread_main, NULL, NULL,
			      NULL, K_PRIO_COOP(CONFIG_BT_RX_PRIO), 0, K_NO_WAIT);

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

	return bt_hci_driver_register(&drv);
}

SYS_INIT(bt_uart_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);
