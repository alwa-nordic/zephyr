/* Copyright (c) 2023 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_TESTS_BLUETOOTH_COMMON_TESTLIB_INCLUDE_TESTLIB_ADV_H_
#define ZEPHYR_TESTS_BLUETOOTH_COMMON_TESTLIB_INCLUDE_TESTLIB_ADV_H_

#include <zephyr/bluetooth/bluetooth.h>

/**
 * @brief Start advertising without privacy and wait for a connection.
 *
 * @param[out] conn Resulting connection object. Must be NULL before calling.
 * @param id Local Bluetooth identity handle
 * @param name Optional name to be advertised. No name will be advertised if NULL.
 *
 * @return 0 on success, negative error code on failure
 */
int bt_testlib_adv_conn(struct bt_conn **conn, int id, const char *name);

#endif /* ZEPHYR_TESTS_BLUETOOTH_COMMON_TESTLIB_INCLUDE_TESTLIB_ADV_H_ */
