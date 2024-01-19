#include <zephyr/drivers/bluetooth/hci_drv_next.h>
#include <zephyr/drivers/bluetooth/hci_drv_sync.h>

static struct k_poll_signal hci_drv_signal_rx;
static struct k_poll_signal hci_drv_signal_tx;

void hci_drv_cb(uint8_t drv_sig)
{
	switch (drv_sig) {
		case drv_sig_rx:
			k_poll_signal_raise(&hci_drv_signal_rx, 0);
		case drv_sig_tx:
			k_poll_signal_raise(&hci_drv_signal_tx, 0);
	}
}

int hci_drv_read_sync(uint8_t *dst, size_t len)
{
	int err = -EAGAIN;

	k_poll_signal_init(&hci_drv_signal_rx);

	err = hci_drv_read(dst, len);
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

int hci_drv_write_sync(uint8_t *src, size_t len)
{
	int err;

	k_poll_signal_init(&hci_drv_signal_tx);

	/* Start the asynchrounous write. */
	err = hci_drv_write(src, len);
	if (err) {
		return err;
	}

	/* Poll until finished here. */
	do {
		err = sig_take(hci_drv_signal_tx, K_FOREVER);
		__ASSERT_NO_MSG(err == 0);
		err = hci_drv_process_tx();
	} while (err == -EAGAIN);

	return err;
}
