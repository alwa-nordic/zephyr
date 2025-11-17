#include <stddef.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sflist.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/spinlock.h>

static struct k_spinlock global_lock;

/* Zippers are always stored in pointers with the least
 * significant bit set to 1.
 * Zipperlist are doubly-linked list nodes to allow O(1) removal from list.
 */
struct zipperlist_node {
	void *next;
	void *prev;
};

void zipperlist_insert(struct zipperlist_node *preceding, struct zipperlist_node *node)
{
	k_spinlock_key_t key;

	key = k_spin_lock(&global_lock);
	node->next = list->head;
	node->prev = NULL;
	k_spin_unlock(&global_lock, key);
}

bool foobar_remove(struct foobar *item)
{
	bool removed;

	if (item == NULL) {
		return false;
	}

	removed = sys_sflist_find_and_remove(&foobar_list, &item->node);

	return removed;
}

void zipper_start(sys_sflist_t *list, sys_sfnode_t *zipper)
{
	k_spinlock_key_t key;

	sys_sfnode_init(zipper, FOOBAR_NODE_ZIPPER);

	key = k_spin_lock(&global_lock);
	sys_sflist_prepend(list, zipper);
	k_spin_unlock(&global_lock, key);
}

struct zipperlist_node *zipper_next(struct zipperlist *list, struct zipperlist_zipper *zipper)
{
	k_spinlock_key_t key;
	struct zipperlist_node *next = zipper;

	key = k_spin_lock(&global_lock);
	SYS_SFLIST_ITERATE_FROM_NODE(list, next) {
		/* Find a real node */
		if (sys_sfnode_flags_get(next) == FOOBAR_NODE_REAL){
			break;
		}
	}



	k_spin_unlock(&global_lock, key);

	return next;
}
