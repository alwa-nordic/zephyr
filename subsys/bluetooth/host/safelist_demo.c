#include <zephyr/kernel.h>
#include <zephyr/sys/sflist.h>
#include <zephyr/sys/util.h>

struct foobar;
#include <zephyr/spinlock.h>

static struct k_spinlock global_lock;

typedef void (*foobar_method_t)(struct foobar *self, void *context);

struct foobar {
	sys_sfnode_t node;
	foobar_method_t method;
};

struct foobar_zipper {
	sys_sfnode_t node;
};

/* Flag bit set on transient zipper nodes to distinguish them from callbacks. */
enum foobar_node_type {
	FOOBAR_NODE_REAL = 0,
	FOOBAR_NODE_ZIPPER = 1,
};

static sys_sflist_t foobar_list = SYS_SFLIST_STATIC_INIT(NULL);

#if defined(CONFIG_MULTITHREADING)
static K_MUTEX_DEFINE(foobar_lock);

static void foobar_mutex_lock(void)
{
	(void)k_mutex_lock(&foobar_lock, K_FOREVER);
}

static void foobar_mutex_unlock(void)
{
	k_mutex_unlock(&foobar_lock);
}
#else
static void foobar_mutex_lock(void)
{
}

static void foobar_mutex_unlock(void)
{
}
#endif

static bool foobar_node_is_zipper(const sys_sfnode_t *node)
{
	return (sys_sfnode_flags_get(node) == FOOBAR_NODE_ZIPPER);
}

static sys_sfnode_t *foobar_next_real_node(const sys_sfnode_t *node)
{
	sys_sfnode_t *cursor = sys_sflist_peek_next(node);

	while ((cursor != NULL) && foobar_node_is_zipper(cursor)) {
		cursor = sys_sflist_peek_next(cursor);
	}

	return cursor;
}

int foobar_add(struct foobar *item)
{
	/* Mark as real node (not a zipper) */
	sys_sfnode_init(&item->node, FOOBAR_NODE_REAL);

	foobar_mutex_lock();
	sys_sflist_append(&foobar_list, &item->node);
	foobar_mutex_unlock();

	return 0;
}

bool foobar_remove(struct foobar *item)
{
	bool removed;

	if (item == NULL) {
		return false;
	}

	foobar_mutex_lock();
	removed = sys_sflist_find_and_remove(&foobar_list, &item->node);
	foobar_mutex_unlock();

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

sys_sfnode_t *zipper_next(sys_sflist_t *list, sys_sfnode_t *zipper)
{
	k_spinlock_key_t key;
	sys_sfnode_t *next = zipper;

	key = k_spin_lock(&global_lock);
sys_sflist_remove
	SYS_SFLIST_ITERATE_FROM_NODE(list, next) {
		/* Find a real node */
		if (sys_sfnode_flags_get(next) == FOOBAR_NODE_REAL){
			break;
		}
	}



	k_spin_unlock(&global_lock, key);

	return next;
}

void foobar_iterate(sys_sfnode_t *zipper)
{
	/* This function is written to tail call at the point of the callback.
	 */
	sys_sfnode_t zipper;

	sys_sfnode_t *next;

	sys_sfnode_init(&zipper, FOOBAR_NODE_ZIPPER);

	{
		k_spinlock_key_t key = k_spin_lock(&global_lock);
		sys_sflist_prepend(&foobar_list, &zipper);
		k_spin_unlock(&global_lock, key);
	}

	sys_sfnode_t *cursor = &zipper;

	key = k_spin_lock(&global_lock);
	while ((cursor = sys_sflist_peek_next(cursor)) != NULL){
		if (sys_sfnode_flags_get(cursor) == FOOBAR_NODE_ZIPPER){
			continue;
		}

		struct foobar *entry = CONTAINER_OF(next, struct foobar, node);
		foobar_method_t method = entry->method;

		(void)sys_sflist_find_and_remove(&foobar_list, &zipper.node);
		sys_sflist_insert(&foobar_list, &entry->node, &zipper.node);

		if (method == NULL) {
			continue;
		}

		k_spin_unlock(&global_lock, key);
		method(entry, context);
		key = k_spin_lock(&global_lock);
	}

	(void)sys_sflist_find_and_remove(&foobar_list, &zipper.node);
	k_spin_unlock(&global_lock, key);
}
