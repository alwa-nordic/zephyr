# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(bsim_test_nbt_central)

target_sources(app PRIVATE
  main.c
  bs_main.c
)

zephyr_include_directories(
  ${BSIM_COMPONENTS_PATH}/libUtilv1/src/
  ${BSIM_COMPONENTS_PATH}/libPhyComv1/src/
  )

add_subdirectory(${ZEPHYR_BASE}/tests/bluetooth/common/testlib testlib)

target_link_libraries(app PRIVATE
  testlib
)
