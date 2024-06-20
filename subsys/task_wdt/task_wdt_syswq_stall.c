/* Copyright (c) 2024 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/task_wdt/task_wdt.h>

LOG_MODULE_REGISTER(task_wdt_syswq_stall, CONFIG_LOG_DEFAULT_LEVEL);

static int channel_id = -1;
static struct k_work_delayable dwork;
static const k_timeout_t feed_delay = K_MSEC(CONFIG_TASK_WDT_SYSWQ_STALL_TIMEOUT_MS / 2);

static void dog_starved(int _channel_id, void *user_data)
{
	ARG_UNUSED(_channel_id);
	ARG_UNUSED(user_data);

	/*   / \__
	 * (    X\___
	 * /         O
	 * /   (_____/
	 * /_____/   U
	 *
	 * Art stolen (and slightly mangled) by Copilot from Ruth Ginsberg
	 */

	LOG_ERR("SysWQ task stalled for %d ms", CONFIG_TASK_WDT_SYSWQ_STALL_TIMEOUT_MS);
	k_oops();
}

static void feed_dog(struct k_work *work)
{
	ARG_UNUSED(work);

	task_wdt_feed(channel_id);
	k_work_schedule(&dwork, feed_delay);
}

static int init(void)
{
	int ret;

	ret = task_wdt_add(CONFIG_TASK_WDT_SYSWQ_STALL_TIMEOUT_MS, dog_starved, NULL);

	if (ret < 0) {
		LOG_ERR("Failed to add task watchdog channel: %d", ret);
		return ret;
	}

	channel_id = ret;

	k_work_init_delayable(&dwork, feed_dog);
	k_work_schedule(&dwork, feed_delay);

	return 0;
}

SYS_INIT(init, APPLICATION, 0);
