/* Copyright (c) 2024 Nordic Semiconductor
 * Copyright (c) 2017 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <zephyr/types.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/l2cap.h>

#include <zephyr/sys/atomic.h>
#include <zephyr/shell/shell.h>

#include "bt.h"

BUILD_ASSERT(IS_ENABLED(CONFIG_BT_L2CAP_SEG_RECV));

static void l2cap_seg_recv(struct bt_l2cap_chan *chan, size_t sdu_len, off_t seg_offset,
			   struct net_buf_simple *seg)
{
	shell_print(ctx_shell, "L2CAP chan %p seg_recv sdu_len %u seg_offset %ld seg_len %u", chan,
		    sdu_len, seg_offset, seg->len);

	if (seg->len) {
		shell_hexdump(ctx_shell, seg->data, seg->len);
	}
}

static void l2cap_status(struct bt_l2cap_chan *chan, atomic_t *status)
{
	shell_print(ctx_shell, "L2CAP chan %p status %u", chan, (uint32_t)*status);
}

static void l2cap_connected(struct bt_l2cap_chan *chan)
{
	shell_print(ctx_shell, "L2CAP chan %p connected", chan);
}

static void l2cap_disconnected(struct bt_l2cap_chan *chan)
{
	shell_print(ctx_shell, "L2CAP chan %p disconnected", chan);
}

static const struct bt_l2cap_chan_ops l2cap_ops = {
	.seg_recv = l2cap_seg_recv,
	.status = l2cap_status,
	.connected = l2cap_connected,
	.disconnected = l2cap_disconnected,
};

struct bt_l2cap_le_chan lechan_pool[1];
struct bt_l2cap_chan *free_chan = &lechan_pool[0].chan;
struct bt_l2cap_chan *default_chan;

static struct bt_l2cap_chan *chan_alloc(void)
{
	return atomic_ptr_clear((void **)&free_chan);
}

static int cmd_connect(const struct shell *sh, size_t argc, char *argv[])
{
	unsigned long psm;
	int err;
	struct bt_l2cap_chan *chan;

	if (!default_conn) {
		shell_error(sh, "Not connected");
		return -ENOTCONN;
	}

	chan = chan_alloc();
	if (chan == NULL) {
		shell_error(sh, "L2CAP chan already allocated");
		return -ENOSPC;
	}

	chan->ops = &l2cap_ops;
	default_chan = chan;

	err = 0;
	psm = shell_strtoul(argv[1], 16, &err);
	if (err || psm > 0xffff) {
		shell_error(sh, "Invalid PSM");
		return -EINVAL;
	}

	err = bt_l2cap_chan_connect(default_conn, chan, psm);
	if (err < 0) {
		shell_error(sh, "L2CAP bt_l2cap_chan_connect %p err %d", chan, err);
	} else {
		shell_print(sh, "L2CAP bt_l2cap_chan_connect %p pending", chan);
	}

	return err;
}

static int cmd_send_credits(const struct shell *sh, size_t argc, char *argv[])
{
	int err;
	unsigned long additional_credits;

	err = 0;
	additional_credits = shell_strtoul(argv[1], 0, &err);
	if (err || additional_credits > 0xffff) {
		shell_error(sh, "Invalid credit amount");
		return 1;
	}

	bt_l2cap_chan_give_credits(default_chan, additional_credits);

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(l2cap_seg_recv_cmds,
			       SHELL_CMD_ARG(connect, NULL, "<psm>", cmd_connect, 1, 1),
			       SHELL_CMD_ARG(send_credits, NULL, "<credit amount>",
					     cmd_send_credits, 1, 1),
			       SHELL_SUBCMD_SET_END);

static int cmd_l2cap_seg_recv(const struct shell *sh, size_t argc, char **argv)
{
	if (argc == 1) {
		shell_help(sh);
		return 1;
	}

	shell_error(sh, "%s unknown parameter: %s", argv[0], argv[1]);

	return -ENOEXEC;
}

SHELL_CMD_ARG_REGISTER(l2cap_seg_recv, &l2cap_seg_recv_cmds, "Bluetooth L2CAP shell commands",
		       cmd_l2cap_seg_recv, 1, 1);
