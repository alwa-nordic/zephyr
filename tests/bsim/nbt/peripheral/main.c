#include <zephyr/drivers/bluetooth/hci_drv_next.h>
#include "bs_macro.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

struct k_poll_signal hci_sig;

void hci_drv_cb(uint8_t drv_sig)
{
	LOG_INF("drv_sig %d", drv_sig);
	k_poll_signal_raise(&hci_sig, 0);
}

void the_test(void)
{
	uint8_t reset[] = {0x01, 0x03, 0x0c, 0x00};
	uint8_t buf[300];
	EXPECT_ZERO(hci_drv_init());
	EXPECT_ZERO(hci_drv_write(reset, sizeof(reset)));
	EXPECT_ZERO(hci_drv_read(buf, 5));

	k_msleep(100);
	hci_drv_process_tx();
	hci_drv_process_rx();

	PASS("Test complete\n");
}
