/*
*/

#include "zephyr/kernel.h"
#include "zephyr/net_buf.h"
#include "zephyr/sys/slist.h"
#include <stdint.h>
#include <zephyr/bluetooth/bluetooth.h>

/* This is all the state of the scan reassembler module.
 *
 * For testing purposes, it's injectable.
 *
 * It is intended to be a singleton.
 *
 * All functions are thread safe by default with regards to this type.
 * Functions will internally acquire the embedded mutex when necessary.
 *
 * The contents of this struct, including the mutex are private.
 *
 * Constructor @ref bt_scan_reassembler_init must be used.
 */
struct bt_scan_reassembler_state {
	struct net_buf_pool *pool;
	struct k_mutex mutex;
	sys_slist_t hci_queue;
	struct net_buf *buf;
	uint8_t subreports;
	uint8_t sid;
	bt_addr_le_t addr;
};

/**
 * Initialize the reassembler state structure.
 *
 * Not thread-safe with regards to @p state. The embedded mutex is initialized in this function.
 * Otherwise thread-safe.
 *
 * Isr-ok.
 */
static inline void bt_scan_reassembler_init(struct bt_scan_reassembler_state *state, struct net_buf_pool *pool)
{
	*state = (struct bt_scan_reassembler_state){};
	state->pool = pool;
	k_mutex_init(&state->mutex);
}

/* This function clears the scan report queue, discarding all
 * data and releasing the buffers. This does not guarantee all
 * buffers have returned to the pool, since an application
 * callback may be executing.
 *
 * This does not deinitialize @p state.
 *
 * thread-safe
 * isr-ok
 */
static inline void bt_scan_reassembler_reset(struct bt_scan_reassembler_state *state)
{
	/* This function returns the state to what it was */
	k_mutex_lock(&state->mutex, K_FOREVER);

	for (struct net_buf *buf; (buf = net_buf_slist_get(&state->hci_queue));) {
		net_buf_unref(buf);
	}
	if (state->buf) {
		net_buf_unref(state->buf);
	}
	state->buf = NULL;
	state->subreports = 0;
	state->addr = (bt_addr_le_t){};

	k_mutex_unlock(&state->mutex);
}

/*
 * The caller must ensure the buffer's net_buf_unref is isr-ok.
 * Invoking this function gives it ownership of the net_buf's node.
 *
 * isr-ok
 */
void bt_scan_reassembler_hci_add_ext_report(struct bt_scan_reassembler_state *state, struct net_buf *buf);
// If there are any buffers in the queue, this will
// immediately process the buffer at the head of the queue in a
// non-blocking way and release the reference, skipping any
// callbacks for delivering the data to upper layers if that
// could block callback may block.
//
// Even though this will always release the queued reference,
// there may be another reference on the stack of an executing
// callback.
//
// To have the desired effect of making a buffer available,
// this may have to be invoked multiple times.
//
// TODO: Instead ensure the next buffer is also freed if a
// callback is active.
//
// This may have to be invoked twice, if the buffer that is
// released by the reassembler is currently held in an application callback.
/*
 * isr-ok
 */
void bt_scan_reassembler_try_safe_release_oldest_buf(void);

/**

 - The queue is empty.
 - A cb is invoked. (Tail call into cb.)
 - A buffer was released. (Tail call into net_buf_unref.)
 */

typedef void bt_scan_report_cb_t(struct bt_le_scan_recv_info *info, struct net_buf_simple *buf);
void bt_scan_reassembler_process_one_cb(bt_scan_report_cb_t *cb);
bool bt_scan_rx_work_pending(void);
