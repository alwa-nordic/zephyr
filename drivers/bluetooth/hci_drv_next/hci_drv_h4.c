#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/bluetooth/hci_drv_next.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(hci_drv_h4, CONFIG_BT_HCI_DRIVER_LOG_LEVEL);

/* Ideas:
 * - Use a ring-buffer for small RX.
 * - Add timeout for RX. On timeout check the buffer for
 *   consistency. I.e. Is the H4 type recognized and is the length
 *   field sane? Then resume the RX if everything is ok.
 */

static const struct device *const hci_uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_bt_uart));

struct hci_drv_state {
	uint8_t *rx_buf;

	/* Replace with a ring-buffer to preserve sequencing? */
	enum hci_drv_result tx_result;
	enum hci_drv_result rx_result;
};

struct hci_drv_state hci_drv_state;

static void uart_cb_tx_done(const struct device *dev, struct uart_event *evt, void *user_data)
{
	/* Always success. UART API does not have a TX IO failure
	 * mode. It can be 'aborted' by timeout or explicit
	 * call, but this driver does not use these functions.
	 */
	hci_drv_state->tx_result = HCI_DRV_RESULT_TX_SUCCESS;
	hci_drv_wakeup();
}

static void uart_cb_rx_disabled(const struct device *dev, struct uart_event *evt, void *user_data)
{
	struct hci_drv_state *s = user_data;

	hci_drv_state->rx_result = HCI_DRV_RESULT_RX_SUCCESS;
	hci_drv_wakeup();
}

static void uart_cb_rx_stopped(const struct device *dev, struct uart_event *evt, void *user_data)
{
	struct hci_drv_state *s = user_data;

	hci_drv_state->rx_result = HCI_DRV_RESULT_RX_FAILURE;
	hci_drv_wakeup();
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

	memset(hci_drv_state, 0, sizeof(struct hci_drv_state));

	err = uart_callback_set(hci_uart_dev, uart_cb, NULL);
	if (err) {
		LOG_ERR("Failed to set UART callback: %d", err);
		return -EIO;
	}

	return 0;
}

int hci_drv_read_into(uint8_t *dst, size_t read_len)
{
	int err;

	err = uart_rx_enable(hci_uart_dev, dst, read_len, SYS_FOREVER_US);
	if (err) {
		LOG_ERR("UART RX start failed: %d", err);
		return -EIO;
	}

	return 0;
}

enum hci_drv_result hci_drv_process_rx(void)
{
	if (hci_drv_state->rx_result == HCI_DRV_RESULT_RX_SUCCESS) {
		hci_drv_state->rx_result = HCI_DRV_RESULT_NOTHING;

		return HCI_DRV_RESULT_RX_SUCCESS;
	}

	return HCI_DRV_RESULT_NOTHING;
}

enum hci_drv_result hci_drv_process_tx(void)
{
	if (hci_drv_state->tx_result == HCI_DRV_RESULT_TX_SUCCESS) {
		hci_drv_state->tx_result = HCI_DRV_RESULT_NOTHING;

		return HCI_DRV_RESULT_TX_SUCCESS;
	}

	return HCI_DRV_RESULT_NOTHING;
}

int hci_drv_write_from(const uint8_t *data, size_t size)
{
	int err;

	err = uart_tx(hci_uart_dev, data, size, SYS_FOREVER_US);
	if (err) {
		LOG_ERR("UART tx start failed: %d", err);
		return -EIO;
	}

	return 0;
}
