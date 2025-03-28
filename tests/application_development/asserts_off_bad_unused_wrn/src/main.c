/* Copyright (c) 2024 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#define NDEBUG 1
#include <assert.h>


static int foo(int a)
{
	return 1;
}

int main(void)
{
	int var_unused = 1;

	assert(foo(var_unused));

	return 0;
}
