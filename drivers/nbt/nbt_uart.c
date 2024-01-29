#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/bluetooth/hci_drv_next.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(nbt_uart, 4);

/* Ideas:
 * - Use a ring-buffer for small RX.
 * - Add timeout for RX. On timeout check the buffer for
 *   consistency. I.e. Is the H4 type recognized and is the length
 *   field sane? Then resume the RX if everything is ok.
 */

static const struct device *const hci_uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_bt_uart));

struct hci_drv_state {
	int tx_result;
	int rx_result;
};

struct hci_drv_state hci_drv_state;

static void uart_cb_tx_done(const struct device *dev, struct uart_event *evt, void *user_data)
{
	/* Always success. UART API does not have a TX IO failure
	 * mode. It can be 'aborted' by timeout or explicit
	 * call, but this driver does not use these functions.
	 */
	hci_drv_state.tx_result = 0;
	hci_drv_cb(drv_sig_tx);
}

static void uart_cb_rx_disabled(const struct device *dev, struct uart_event *evt, void *user_data)
{
	hci_drv_state.rx_result = 0;
	hci_drv_cb(drv_sig_rx);
}

static void uart_cb_rx_stopped(const struct device *dev, struct uart_event *evt, void *user_data)
{
	hci_drv_state.rx_result = -EIO;
	hci_drv_cb(drv_sig_rx);
}

static void uart_cb(const struct device *dev, struct uart_event *evt, void *user_data)
{

	switch (evt->type) {
	case UART_TX_DONE:
		LOG_DBG("TX done");
		uart_cb_tx_done(dev, evt, user_data);
		break;
	case UART_TX_ABORTED:
		LOG_DBG("TX aborted");
		break;
	case UART_RX_RDY:
		LOG_DBG("RX rdy");
		break;
	case UART_RX_BUF_REQUEST:
		LOG_DBG("RX buf request");
		break;
	case UART_RX_BUF_RELEASED:
		LOG_DBG("RX buf released");
		break;
	case UART_RX_DISABLED:
		LOG_DBG("RX disabled");
		uart_cb_rx_disabled(dev, evt, user_data);
		break;
	case UART_RX_STOPPED:
		LOG_DBG("RX stopped");
		uart_cb_rx_stopped(dev, evt, user_data);
		break;
	default:
		LOG_DBG("Unknown event");
		break;
	}
}

int hci_drv_init(void)
{
	int err;

	hci_drv_state = (struct hci_drv_state){
		.tx_result = -EAGAIN,
		.rx_result = -EAGAIN,
	};

	err = uart_callback_set(hci_uart_dev, uart_cb, NULL);
	if (err) {
		LOG_ERR("Failed to set UART callback: %d", err);
		return -EIO;
	}

	return 0;
}

int hci_drv_read_into(uint8_t *dst, size_t len)
{
	int err;

	err = uart_rx_enable(hci_uart_dev, dst, len, SYS_FOREVER_US);
	if (err) {
		LOG_ERR("UART RX start failed: %d", err);
		return -EIO;
	}

	return 0;
}

int hci_drv_process_rx(void)
{
	int err;

	err = hci_drv_state.rx_result;
	hci_drv_state.rx_result = -EAGAIN;

	return err;
}

int hci_drv_process_tx(void)
{
	int err;

	err = hci_drv_state.tx_result;
	hci_drv_state.rx_result = -EAGAIN;

	return err;
}

int hci_drv_write(const uint8_t *src, size_t len)
{
	int err;

	err = uart_tx(hci_uart_dev, src, len, SYS_FOREVER_US);
	if (err) {
		LOG_ERR("UART tx start failed: %d", err);
		return -EIO;
	}

	return 0;
}
