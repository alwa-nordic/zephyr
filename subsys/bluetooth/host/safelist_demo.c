#include <zephyr/kernel.h>
#include <zephyr/sys/sflist.h>
#include <zephyr/sys/util.h>

struct foobar;

typedef void (*foobar_method_t)(struct foobar *self, void *context);

struct foobar {
	sys_sfnode_t node;
	foobar_method_t method;
};

struct foobar_zipper {
	sys_sfnode_t node;
};

/* Flag bit set on transient zipper nodes to distinguish them from callbacks. */
#define FOOBAR_NODE_FLAG_ZIPPER 0x1U

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
	return (sys_sfnode_flags_get(node) & FOOBAR_NODE_FLAG_ZIPPER) != 0U;
}

static sys_sfnode_t *foobar_next_real_node(const sys_sfnode_t *node)
{
	sys_sfnode_t *cursor = sys_sflist_peek_next(node);

	while ((cursor != NULL) && foobar_node_is_zipper(cursor)) {
		cursor = sys_sflist_peek_next(cursor);
	}

	return cursor;
}

static void foobar_zipper_prepare(struct foobar_zipper *zipper)
{
	sys_sfnode_init(&zipper->node, FOOBAR_NODE_FLAG_ZIPPER);
}

int foobar_add(struct foobar *item)
{
	if (item == NULL) {
		return -EINVAL;
	}

	sys_sfnode_init(&item->node, 0U);

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

	if (removed) {
		sys_sfnode_init(&item->node, 0U);
	}

	return removed;
}

void foobar_iterate(void *context)
{
	struct foobar_zipper zipper;
	sys_sfnode_t *next;

	foobar_zipper_prepare(&zipper);

	foobar_mutex_lock();
	sys_sflist_prepend(&foobar_list, &zipper.node);

	for (;;) {
		next = foobar_next_real_node(&zipper.node);
		if (next == NULL) {
			break;
		}

		struct foobar *entry = CONTAINER_OF(next, struct foobar, node);
		foobar_method_t method = entry->method;

		(void)sys_sflist_find_and_remove(&foobar_list, &zipper.node);
		sys_sflist_insert(&foobar_list, &entry->node, &zipper.node);

		if (method == NULL) {
			continue;
		}

		foobar_mutex_unlock();
		method(entry, context);
		foobar_mutex_lock();
	}

	(void)sys_sflist_find_and_remove(&foobar_list, &zipper.node);
	foobar_mutex_unlock();
}
