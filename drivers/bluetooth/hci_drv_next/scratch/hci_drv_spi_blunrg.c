#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/bluetooth/hci_drv_next.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(hci_drv_spi_bluenrg, CONFIG_BT_HCI_DRIVER_LOG_LEVEL);

/* Protocol:
 *   Reset pin.
 *   IRQ pin.
 * Half duplex protocol.
Max message length 255. This affects fragmentation.
H4 type header encapsulates HCI.
Each transfer begins with 5 byte header.

Read header from controller: [ready, 0, 0, size,
	ready: 0x02 "ready_now", other: "not ready"
 */
