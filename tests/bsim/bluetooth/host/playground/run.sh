#!/usr/bin/env bash
# Copyright 2023 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

set -eu

export ZEPHYR_BASE
: ${ZEPHYR_BASE:="$(t="$(west topdir)" && echo "$t/$(west config zephyr.base)")"}

source ${ZEPHYR_BASE}/tests/bsim/sh_common.source

device_count=2
simulation_id="host_playground"
dev_exe=bs_${BOARD_TS}_$(guess_test_long_name)_prj_conf
args_all=(-s=${simulation_id} -D="${device_count}")
args_dev=(-v=2 -RealEncryption=1 -testid=the_test)

cd "${BSIM_OUT_PATH}/bin"

Execute ./bs_2G4_phy_v1 "${args_all[@]}" -v=6 -sim_length=60e6

for i in $(seq 0 $((device_count - 1))); do
  Execute ./${dev_exe} "${args_all[@]}" "${args_dev[@]}" -d="${i}"
done

wait_for_background_jobs
