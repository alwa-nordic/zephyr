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
	BT_DEV_RL_ADD_PENDING,
	BT_DEV_RL_DEL_PENDING,
	BT_DEV_RL_USER_ADV,
	BT_DEV_RL_USER_ADV_REQ,
};

struct bt_dev_rl_add_async_t {
	sys_snode_t __node;
	const struct bt_hci_cp_le_add_dev_to_rl *entry;
};

struct bt_dev_rl_del_async_t {
	sys_snode_t __node;
	const bt_addr_le_t *peer_id;
};

atomic_t bt_dev_rl_users;

/* Short-locking mutex to protect both the signal and the
 * sys_slist, which has a thread-unsafe implementation.
 *
 * This can be a spinlock instead.
 */
struct k_mutex bt_dev_rl_mutex;

/** not thread-safe, synchronize on @ref bt_dev_rl_mutex. */
sys_slist_t bt_dev_rl_ops_del;
/** not thread-safe, synchronize on @ref bt_dev_rl_mutex. */
sys_slist_t bt_dev_rl_ops_add;

/* Atomic invariant protected by `bt_dev_rl_mutex`: The signal
 * is never observed raised while either of the sys_slist have
 * any items.
 */
struct k_poll_signal bt_dev_rl_idle;


struct bt_dev_rl_add_async_t *bt_dev_rl_ops_add_dequeue(void)
{
	sys_snode_t *node;

	k_mutex_lock(&bt_dev_rl_mutex, K_FOREVER);
	node = sys_slist_get(&bt_dev_rl_ops_add);
	if (sys_slist_is_empty(&bt_dev_rl_ops_add)) {
	}
	k_mutex_unlock(&bt_dev_rl_mutex);

	if (!node) {
		return NULL;
	}

	return CONTAINER_OF(node, struct bt_dev_rl_add_async_t, __node);
}

struct bt_dev_rl_del_async_t *bt_dev_rl_ops_del_dequeue(void)
{
	sys_snode_t *node;

	k_mutex_lock(&bt_dev_rl_mutex, K_FOREVER);
	node = sys_slist_get(&bt_dev_rl_ops_del);
	k_mutex_unlock(&bt_dev_rl_mutex);

	if (!node) {
		return NULL;
	}

	return CONTAINER_OF(node, struct bt_dev_rl_del_async_t, __node);
}

void bt_hci_cmd_gen_req(void);
void bt_dev_rl_add_async(struct bt_dev_rl_add_async_t *op)
{
	k_mutex_lock(&bt_dev_rl_mutex, K_FOREVER);

	/* Signal shall not be observed high while items are pending. */
	k_poll_signal_reset(&bt_dev_rl_idle);

	sys_slist_append(&bt_dev_rl_ops_add, &op->__node);

	atomic_set_bit(&bt_dev_rl_users, BT_DEV_RL_ADD_PENDING);
	bt_hci_cmd_gen_req();

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

void bt_dev_rl_hci_cmd_fill_del(struct net_buf *buf,  const bt_addr_le_t *peer_id)
{
	bt_hci_cmd_set_hdr(buf, BT_HCI_OP_LE_REM_DEV_FROM_RL, sizeof(*peer_id));
	net_buf_add_mem(buf, peer_id, sizeof(*peer_id));
}

int bt_dev_rl_dequeue_cmd(struct net_buf *buf)
{
	struct bt_dev_rl_add_async_t *add_op;
	struct bt_dev_rl_del_async_t *del_op;

	if ((del_op = bt_dev_rl_ops_del_dequeue())) {
		bt_dev_rl_hci_cmd_fill_del(buf, del_op->peer_id);
		return 0;
	}

	if ((add_op = bt_dev_rl_ops_add_dequeue())) {
		bt_dev_rl_hci_cmd_fill_add(buf, add_op->entry);
		return 0;
	}

	k_poll_signal_raise(&bt_dev_rl_idle, 0);

	return -EAGAIN;
}

int bt_adv_rl_preempt(struct net_buf *cmd_buf);
int bt_adv_rl_ready(struct net_buf *cmd_buf);

typedef int buf_to_int_t(struct net_buf *cmd_buf);

buf_to_int_t *bt_dev_rl_gen_hci_cmd_select(void)
{
	if (atomic_test_bit(&bt_dev_rl_users, BT_DEV_RL_ADD_PENDING)) {
		if (bt_dev_rl_users & BT_DEV_RL_USER_ADV) {
			return bt_adv_rl_preempt;
		}

		return bt_dev_rl_dequeue_cmd;
	}

	if (atomic_test_bit(&bt_dev_rl_users, BT_DEV_RL_USER_ADV_REQ)) {
		return bt_adv_rl_ready;
	}

	k_poll_signal_raise(&bt_dev_rl_idle, 0);
	return NULL;
}

int bt_dev_rl_gen_hci_cmd(struct net_buf *cmd_buf)
{
	buf_to_int_t *next_gen;

	k_mutex_lock(&bt_dev_rl_mutex, K_FOREVER);

	next_gen = bt_dev_rl_gen_hci_cmd_select();

	if (next_gen) {
		k_mutex_unlock(&bt_dev_rl_mutex);
		return next_gen(cmd_buf);

	}

	k_mutex_unlock(&bt_dev_rl_mutex);

	return -ECANCELED;
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
