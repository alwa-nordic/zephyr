/* Copyright (c) 2023 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/bluetooth/bluetooth.h>

int bt_testlib_adv_conn(struct bt_le_ext_adv **adv, struct bt_conn **conn, int id, const bt_addr_le_t *adva, uint32_t adv_options)
;
