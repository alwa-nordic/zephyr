#include <errno.h>

#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/net/buf.h>

#include "keys.h"

#include "syscalls/kernel.h"
#include "zephyr/kernel.h"
#include "zephyr/sys/atomic.h"
#include "zephyr/sys/slist.h"
#include "zephyr/sys/util.h"

enum bt_dev_rl_read_lock_t {
	BT_DEV_RL_LOCK_WRITE,
	BT_DEV_RL_LOCK_ADV_HAS,
	BT_DEV_RL_LOCK_ADV_WANT,
};

struct bt_dev_rl_add_async_t {
	sys_snode_t __node;
	const struct bt_hci_cp_le_add_dev_to_rl *entry;
};

struct bt_dev_rl_del_async_t {
	sys_snode_t __node;
	const bt_addr_le_t *peer_id;
};

atomic_t bt_dev_rl_lock;

/* Short-locking mutex to protect both the signal and the
 * sys_slist, which has a thread-unsafe implementation.
 *
 * This can be a spinlock instead.
 */
struct k_mutex bt_dev_rl_mutex;

/* sys_slist operations are not thread-safe */
sys_slist_t bt_dev_rl_ops_del;
sys_slist_t bt_dev_rl_ops_add;

/* Atomic invariant protected by `bt_dev_rl_mutex`: The signal
 * is never observed raised while either of the sys_slist have
 * any items.
 */
struct k_poll_signal bt_dev_rl_idle;


struct bt_dev_rl_add_async_t *bt_dev_rl_ops_add_dequeue(void)
{
	sys_snode_t *node;

	/* Mutex protects unsafe implementation `sys_slist_get`. */
	k_mutex_lock(&bt_dev_rl_mutex, K_FOREVER);
	node = sys_slist_get(&bt_dev_rl_ops_add);
	k_mutex_unlock(&bt_dev_rl_mutex);

	if (!node) {
		return NULL;
	}

	return CONTAINER_OF(node, struct bt_dev_rl_add_async_t, __node);
}

void bt_hci_cmd_gen_req(void);
void bt_dev_rl_start_write() {
	atomic_set_bit(&bt_dev_rl_lock, BT_DEV_RL_LOCK_WRITE);
	bt_hci_cmd_gen_req();
}

void bt_dev_rl_add_async(struct bt_dev_rl_add_async_t *op)
{
	k_mutex_lock(&bt_dev_rl_mutex, K_FOREVER);

	/* Not thread-safe */
	sys_slist_append(&bt_dev_rl_ops_add, &op->__node);

	/* Signal shall not be observed high while items are pending.
	 */
	k_poll_signal_reset(&bt_dev_rl_idle);

	bt_dev_rl_start_write();

	k_mutex_unlock(&bt_dev_rl_mutex);
}

/* Legacy support */
int bt_dev_rl_add_sync(const struct bt_hci_cp_le_add_dev_to_rl *entry)
{
	struct bt_dev_rl_add_async_t op = {
		.entry = entry,
	};

	bt_dev_rl_add_async(&op);
	k_poll_signal_wait(&bt_dev_rl_idle);

	return 0;
}

/* Legacy support */
int bt_dev_rl_del_sync(const bt_addr_le_t *peer_id)
{
	return 0;
}

void bt_hci_cmd_set_hdr(struct net_buf *buf, uint16_t opcode, uint8_t param_len);
void bt_dev_rl_hci_cmd_fill_add(struct net_buf *buf, const struct bt_hci_cp_le_add_dev_to_rl *entry)
{
	bt_hci_cmd_set_hdr(buf, BT_HCI_OP_LE_ADD_DEV_TO_RL, sizeof(*entry));
	net_buf_add_mem(buf, entry, sizeof(*entry));
}

int bt_dev_rl_dequeue_cmd(struct net_buf *buf)
{
	struct bt_dev_rl_add_async_t *add_op;

	if ((add_op = bt_dev_rl_ops_add_dequeue())) {
		bt_dev_rl_hci_cmd_fill_add(buf, add_op->entry);
		return 0;
	}

	return -EAGAIN;
}

int bt_adv_pause(struct net_buf *cmd_buf);

int bt_dev_rl_gen_hci_cmd(struct net_buf *cmd_buf)
{
	if (!(bt_dev_rl_lock & BT_DEV_RL_LOCK_WRITE)) {
		return -ECANCELED;
	}

	if (bt_dev_rl_lock & BT_DEV_RL_LOCK_ADV_HAS) {
		return bt_adv_pause(cmd_buf);
	}
	/* And so on for scan and initiator */

	return bt_dev_rl_dequeue_cmd(cmd_buf);
}

// Signals:
//
// Read lock. Incoming signal. Explicit lock signals form other Bluetooth modules.
//  - scan: (Scanner)
//  - conn: (Initiator)
//  - adv:  (Advertiser)
//
// Pauseable. Incoming signal. Each read lock specifies whether
// it is possible to initiate the pause procedure.
//
// Pause. Outgoing procedure. Only fires if all read-locks are
// pauseable. This instructs the read locks to stop the HCI
// roles using the resolve list.
//
// The pause procedure is async and driven by availability of
// HCI command credits.
//
// A resume operation follows a pause and RL update messages. It
// is also driven async by this module. The other modules may
// re-engage the read lock.
//
// The invoker of the change to RL has to lend the memory
// holding the address to be deleted or entry to be added. This
// can be a sys_slist node?
