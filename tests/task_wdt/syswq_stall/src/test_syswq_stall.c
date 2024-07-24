/* Copyright (c) 2023-2024 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/ztest_assert.h>
#include <zephyr/ztest_error_hook.h>
#include <zephyr/ztest_test.h>
#include <zephyr/ztest.h>

extern ZTEST_BMEM volatile bool fault_in_isr;
extern ZTEST_BMEM volatile k_tid_t valid_fault_tid;

LOG_MODULE_REGISTER(test, LOG_LEVEL_DBG);

static void hurrdurr_blocking_work_handler(struct k_work *work)
{
	k_sleep(K_FOREVER);
}

ZTEST_SUITE(task_wdt_syswq_stall, NULL, NULL, NULL, NULL, NULL);
ZTEST_EXPECT_FAIL(task_wdt_syswq_stall, test_detect_stall);
ZTEST(task_wdt_syswq_stall, test_detect_stall)
{
	k_sleep(K_MSEC(CONFIG_TASK_WDT_SYSWQ_STALL_TIMEOUT_MS * 2));

	ztest_set_fault_valid(true);
	fault_in_isr = true;
	valid_fault_tid = (void*)0x805bb40;

	struct k_work work;
	k_work_init(&work, hurrdurr_blocking_work_handler);
	k_work_submit(&work);
	k_work_flush(&work, &(struct k_work_sync){});
}
