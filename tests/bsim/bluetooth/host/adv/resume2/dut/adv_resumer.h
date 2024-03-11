/* Copyright (c) 2024 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <zephyr/sys/util.h>

typedef void bt_conn_evt_sub_t(void);

enum bt_conn_evt_bits {
	BT_NOW = BIT(0),
	BT_CONNECTED = BIT(1),
	BT_RECYCLED = BIT(2),
};

/**
 * @brief Start or stop advertising with resumption.
 *
 * @p adv_starter reference is Sync. The reference is dropped by
 * the next call to @ref adv_resumer_set.
 *
 * This function is synchronized with itself and the resume
 * mechanism. After this function returns, the previous @p
 * adv_starter provided will not invoked, and it is safe to
 * modify global variabled accessed by @c adv_starter.
 *
 * @param adv_starter Function to invoke when it might be
 * possible to start an advertiser. Set to `NULL` to disable.
 *
 * This function is always successful.
 */
void bt_conn_evt_sub_set(bt_conn_evt_sub_t *func, enum bt_conn_evt_bits enabled);
