#include <stddef.h>
#include <stdint.h>

/**
 * Call at startup and desync.
 *
 * Abort any ongoing operations.
 *
 * The lower-transport will be synchronized before the next read
 * completes.
 *
 * Eg. For H4 UART, it will discard until a hci-reset pattern is
 * found. The HCI reset pattern that was found will be available
 * in the next read.
 */
int hci_drv_init(void);

/*
Alternative sync function:

Discard until a hci-reset pattern is found. The hci reset found
will be in dst after completion.

Do the process and signal stuff just like for `hci_drv_read`.
*/
int hci_drv_read_hci_reset(uint8_t *dst, size_t len);

/**
 * Start an asynchronous read operation. `hci_drv_process` will
 * return the readiness status of the operation.
 * `hci_drv_wakeup` will be signaled when the operation ends or
 * the driver wants to borrow a thread.
 *
 * Each HCI packet is prefaced with a H4 packet type byte.
 *
 * Reading past an HCI packet boundary is UB.
 */
int hci_drv_read(uint8_t *dst, size_t len);

int hci_drv_write(const uint8_t *src, size_t size);


enum drv_sig {
	drv_sig_rx,
	drv_sig_tx,
};

/**
 * IRQ raise. This is a request for a process to call into
 * `hci_drv_process*`.  This symbol must be defined by the user
 * of the driver.
 *
 * Do not call the process functions directly from this ISR.
 * Please return from the ISR as soon as possible, and call the
 * process function from a thread you own.
 */
void hci_drv_cb(uint8_t drv_sig, int err);

/**
 * Async step. May perform some fast, non-blocking internal
 * operations, and then dequeues an event. Returns `-EAGAIN` if
 * there is no event available yet.
 *
 * @retval 0 Operation sucessfully completed.
 * @retval -EIO
 * @retval -EAGAIN
 */
int hci_drv_process_rx(void);
int hci_drv_process_tx(void);
