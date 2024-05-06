#include <errno.h>

#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/hci_types.h>
#include <zephyr/net/buf.h>

#include "keys.h"

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
	struct bt_hci_cp_le_add_dev_to_rl *entry;
};

struct bt_dev_rl_del_async_t {
	sys_snode_t __node;
	const bt_addr_le_t *peer_id;
};

atomic_t bt_dev_rl_lock;
sys_slist_t bt_dev_rl_ops_del;
sys_slist_t bt_dev_rl_ops_add;

void bt_dev_rl_ops_add_add(struct bt_dev_rl_add_async_t *op)
{
	sys_slist_append(&bt_dev_rl_ops_add, &op->__node);
}

struct bt_dev_rl_add_async_t *bt_dev_rl_ops_add_dequeue(void)
{
	sys_snode_t *node;

	node = sys_slist_get(&bt_dev_rl_ops_add);
	if (!node) {
		return NULL;
	}

	return CONTAINER_OF(node, struct bt_dev_rl_add_async_t, __node);
}

struct k_poll_signal bt_dev_rl_write_done;

void bt_hci_cmd_gen_req(void);
void bt_dev_rl_start_write() {
	k_poll_signal_reset(&bt_dev_rl_write_done);
	atomic_set_bit(&bt_dev_rl_lock, BT_DEV_RL_LOCK_WRITE);
	bt_hci_cmd_gen_req();
}

void bt_dev_rl_add_async(struct bt_dev_rl_add_async_t *op)
{
	/* Append is O(1) */
	bt_dev_rl_ops_add_add(op);
	bt_dev_rl_start_write();
}

/* Legacy support */
int bt_dev_rl_add_sync(const bt_addr_le_t *peer_id, struct bt_irk *peer_irk, struct bt_irk *local_irk)
{
	struct bt_dev_rl_add_async_t op = {
		.entry.peer_id = peer_id,
		.entry.peer_irk = peer_irk,
		.entry.local_irk = local_irk,
	};

	bt_dev_rl_add_async(&op);
	k_poll_signal_wait(&bt_dev_rl_write_done);

	return 0;
}

/* Legacy support */
int bt_dev_rl_del_sync(const bt_addr_le_t *peer_id)
{
	return 0;
}

void bt_hci_cmd_set_hdr(struct net_buf *buf, uint16_t opcode, uint8_t param_len);
void bt_dev_rl_hci_cmd_fill_add(struct net_buf *buf, const bt_addr_le_t *peer_id, struct bt_irk *peer_irk, struct bt_irk *local_irk)
{
	struct bt_hci_cp_le_add_dev_to_rl *cp;

	bt_hci_cmd_set_hdr(buf, BT_HCI_OP_LE_ADD_DEV_TO_RL, sizeof(*cp));
	cp = net_buf_add(buf, sizeof(*cp));
	bt_addr_le_copy(&cp->peer_id_addr, peer_id);
	memcpy(cp->peer_irk, peer_irk, 16);
	memcpy(cp->local_irk, local_irk, 16);
}

int bt_dev_rl_dequeue_cmd(struct net_buf *buf)
{
	struct bt_dev_rl_add_async_t *add_op;

	if ((add_op = bt_dev_rl_ops_add_dequeue())) {
		bt_dev_rl_hci_cmd_fill_add(buf, add_op->peer_id, add_op->peer_irk, add_op->local_irk);
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
