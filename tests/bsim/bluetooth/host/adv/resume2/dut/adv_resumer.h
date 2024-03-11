/* Copyright (c) 2024 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

typedef void adv_starter_t(void);

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
 * When @p adv_starter is `NULL`, this function calls
 * `bt_le_adv_stop`.
 *
 * @param adv_starter Function to invoke when it might be
 * possible to start an advertiser. Set to `NULL` to disable.
 *
 * This function is always successful.
 */
void adv_resumer_set(adv_starter_t *adv_starter);
