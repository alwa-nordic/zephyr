int driver_driver(void)
{
	struct net_buf *buf;
	struct k_poll_event ev;

	for (;;) {
		int ret;
		size_t read_len;
		size_t next_read_len;
		size_t scratch_free;

		ret = get_next_read_size(scratch, scratch_len);
		if (ret < 0) {
			return ret;
		}
		next_read_len = ret;

		scratch_free = scratch_cap - scratch_len;
		read_len = min(next_read_len, scratch_free);

		if (read_len == 0) {
			break;
		}

		ret = drv_read(&scratch[scratch_len], read_len);
		if (ret < 0) {
			return ret;
		}

		scratch_len += ret;
	}

	int err = -EAGAIN;
	while (err == -EAGAIN) {
		err = crazy_alloc(scratch, scratch_len, &buf, &ev);
		if (err == -EAGAIN) {
			err = k_poll(&ev, 1, K_FOREVER);
			continue;
		}

		if (err == -NOBUFS) {
			/* Discard */

		}
	}
}

/*
Google keywords: - online parsing - incremental parsing



A parser is an algorithm that determines whether a given input string is
in a language and, as a side-effect, usually produces a parse tree for
the input.

Other side effects: Determining the size of the whole packet. This is
        equivalent to determining the packet boundary.

        Selecting an allocator. This is classifying the packet.
        The packet allocator is a new concept:
                It selects a pool.
                It selects an action to take if the pool is empty.
                        Wait. Then it also provides an event.
                        Discard.
                        Error.
                Selecting the allocator and returning it is better than
                eturning a buffer because it allways succeeds. The
                possible failures are delayed until the allocator is used. This moves the error handling up the stack, which is good.

        These are multiple tasks that can be done in parallel. The tasks may terminate independently.
        Determining the size of the packet may be done in fewer
        input bytes than determining the allocator. Eg.

        The parser is incremental. We are reading from a stream, so we don't know the size of the packet.

        We could feed the parser one byte at a time.
        The parser says if the parse was successful or not.
        This is isomorphic to returning the number 1 to continue the parse, and 0 to stop the parse.
        We can generalize and let the parser return a larger than 1 to indicate how many bytes to read next.

        We have thus a number of outputs which can be produced, some independently:
                - parse error
                - the allocator
                - the size of the packet
                - the number of bytes to read next

        Once enough bytes have been read, all possible outputs are produced.

        We could group the "successful outputs" into one. This output would then be produced only once both subtask are complete.

        Error | Success (allocator, size) | Partial (next_read_len)
        Then the allocator is invoked and may return.
        Success (buffer) | Discard | Wait (ev) | Error

        This is fairly nice and clear.

        We can make the allocator and the size of the packet known asap instead.
        This fits well with C-programming, where we don't return a struct, but
	rather write trough pointers.

        In HCI, the size of the packet comes fairly early, so determining
        the allocator is the longer-running task. It may not be worth the
	extra complexity to make the size known early.
*/

int parse_hci_h4(uint8_t *buf, size_t buf_len, alloc_t *out_alloc, size_t *out_size);
