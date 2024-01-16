#include <stddef.h>
#include <stdint.h>

/**
 * @brief
 * @param drv_state
 */
int hci_drv_init(void);


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
int hci_drv_read_into(uint8_t *dst, size_t read_len);

/**
 * IRQ raise. This is a request for a process to call into
 * `hci_drv_process`. The user defines this symbol.
 *
 * Do not call the process function directly from the ISR.
 * Please return from the ISR as soon as possible, and call the
 * process function from a thread you own.
 */
void hci_drv_wakeup(bool rx, bool tx);

/**
 * Async step. May perform some fast, non-blocking internal
 * operations, and then dequeues an event. Returns `Nothing` if
 * there is no event available yet.
 *
 * @retval -EIO
 * @retval -EAGAIN
 */
int hci_drv_process_rx(void);
int hci_drv_process_tx(void);


int hci_drv_write_from(const uint8_t *data, size_t size);
