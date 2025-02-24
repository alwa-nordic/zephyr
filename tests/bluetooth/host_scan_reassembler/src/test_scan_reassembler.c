/* Copyright (c) 2025 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/kernel/thread.h>
#include <zephyr/net_buf.h>

#include <zephyr/logging/log.h>

#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>
#include <zephyr/ztest_test.h>

#include <zephyr/drivers/bluetooth.h>
#include <zephyr/drivers/uart/serial_test.h>

LOG_MODULE_REGISTER(test, LOG_LEVEL_DBG);

/* Start the DUT "main thread". The settings for this thread are selected as
 * true as possible to the real main thread. {{{
 */
static struct k_thread hci_uart_thread;
static K_THREAD_PINNED_STACK_DEFINE(hci_uart_thread_stack, CONFIG_MAIN_STACK_SIZE);
static void hci_uart_thread_entry(void *p1, void *p2, void *p3)
{
	extern void hci_uart_main(void);
	hci_uart_main();
}
static int sys_init_spawn_hci_uart(void)
{
	k_thread_create(&hci_uart_thread, hci_uart_thread_stack,
			K_THREAD_STACK_SIZEOF(hci_uart_thread_stack), hci_uart_thread_entry, NULL,
			NULL, NULL, CONFIG_MAIN_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&hci_uart_thread, "hci_uart_main");
	return 0;
}
SYS_INIT(sys_init_spawn_hci_uart, POST_KERNEL, 64);
/* }}} */

ZTEST_SUITE(bt_host_scan_reassembler, NULL, NULL, NULL, NULL, NULL);
ZTEST(bt_host_scan_reassembler, test_foobar)
{
}
