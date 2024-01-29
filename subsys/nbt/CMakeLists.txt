# SPDX-License-Identifier: Apache-2.0

add_library(nbt)

target_link_libraries(nbt PUBLIC kernel zephyr_interface)

target_sources(nbt PRIVATE nbt_event_loop.c)
