/* Copyright (c) 2024 Nordic Semiconductor
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/drivers/bluetooth/hci_driver.h>
#include <zephyr/drivers/bluetooth/hci_drv_next.h>

#include <stddef.h>

static void h4_encapsulate(struct net_buf *buf)
{
	uint8_t h4_type;

	switch (bt_buf_get_type(buf)) {
	case BT_BUF_H4:
		/* Already decorated. */
		return;
	case BT_BUF_CMD:
		h4_type = H4_CMD;
		break;
	case BT_BUF_EVT:
		h4_type = H4_EVT;
		break;
	case BT_BUF_ACL_OUT:
	case BT_BUF_ACL_IN:
		h4_type = H4_ACL;
		break;
	case BT_BUF_ISO_OUT:
	case BT_BUF_ISO_IN:
		h4_type = H4_ISO;
		break;
	default:
		__ASSERT_NO_MSG(false);
		return;
	}

	__ASSERT_NO_MSG(net_buf_headroom(buf) >= H4_HDR_SIZE);
	net_buf_push_u8(buf, h4_type);
	bt_buf_set_type(buf, BT_BUF_H4);
}
