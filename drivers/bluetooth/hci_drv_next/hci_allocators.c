/*
This file contains an asynchronous front-end for bt_buf.

Each of these allocators can return
	Success (net_buf) | Error
*/

static int alloc_cmd_complete(struct net_buf **out_buf, ev *ev)
{
	*out_buf = bt_buf_get_cmd_complete(K_NO_WAIT);
}

static struct net_buf *bt_buf_get_cmd_complete2(uint16_t opcode, k_timeout_t timeout)
{
	/* A non-zero opcode responds to a command. Events that respond to a
	 * command must use the special allocator.
	 */
	if (opcode != 0) {
		/* Special allocator for command responses. */
		return bt_buf_get_cmd_complete(timeout);
	}

	/* Otherwise, treat it like a normal event. */
	return bt_buf_get_rx(BT_BUF_EVT, timeout);
}
