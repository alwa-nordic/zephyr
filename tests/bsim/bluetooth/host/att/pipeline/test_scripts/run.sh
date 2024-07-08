#!/usr/bin/env bash
# Copyright 2023 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

source ${ZEPHYR_BASE}/tests/bsim/sh_common.source

dut_exe="bs_${BOARD_TS}_tests_bsim_bluetooth_host_att_pipeline_dut_prj_conf"
tester_exe="bs_${BOARD_TS}_tests_bsim_bluetooth_host_att_pipeline_tester_prj_conf"

simulation_id="att_pipeline"
verbosity_level=2
sim_length_us=100e6

cd ${BSIM_OUT_PATH}/bin

# Test: DUT must tolerate pipelined ATT requests
#
# Purpose:
#
# Check that DUT GATT server gracefully handles a pipelining
# client.
#
# The DUT GATT server must remain available to a well-behaved
# peer while a bad peer tries to spam ATT requests.
#
# Test procedure:
#
#  - The well-behaved peer performs a discovery procedure
#    repeatedly.
#  - The bad peer spams ATT requests as fast as possible.
#  - The connection with the well-behaved peer shall remain
#    responsive.
#  - Either: The DUT may disconnect the bad peer ACL after
#    receiving a protocol violation occurs. The bad peer shall
#    be able to reconnect and continue the bad behavior.
#  - Or: The DUT may process and respond to the pipelined
#    requests, preserving their ordering.

Execute ./bs_2G4_phy_v1 \
    -v=${verbosity_level} -s="${simulation_id}" -D=2 -sim_length=${sim_length_us} $@

Execute "./$tester_exe" \
    -v=${verbosity_level} -s="${simulation_id}" -d=1 -testid=tester_1 -RealEncryption=1 -rs=100

Execute "./$dut_exe" \
    -v=${verbosity_level} -s="${simulation_id}" -d=0 -testid=dut_1 -RealEncryption=1

wait_for_background_jobs

# Test: DUT must not pipeline ATT requests
#
# Purpose:
#
# Basic check that the DUT GATT client does not pipeline ATT
# requests.
#
# Sending a new request on a bearer before the response to the
# previous request has been received ('pipelining') is a
# protocol violation.
#
# Test procedure:
#
#  - DUT is excercised by calling `gatt_write` in a loop.
#  - Tester does not immediately respond but delays the response
#    a bit to ensure the LL has time to transport any extra
#    requests, exposing a bug.
#  - Tester verifies there is no such extra request while it's
#    delaying the response. Detecting an extra request proves a
#    protocol violation.

Execute ./bs_2G4_phy_v1 \
    -v=${verbosity_level} -s="${simulation_id}" -D=3 -sim_length=${sim_length_us} $@

Execute "./$tester_exe" \
    -v=${verbosity_level} -s="${simulation_id}" -d=2 -testid=tester -RealEncryption=1 -rs=100

Execute "./$dut_exe" \
    -v=${verbosity_level} -s="${simulation_id}" -d=1 -testid=dut -RealEncryption=1 -rs=2000

Execute "./$dut_exe" \
    -v=${verbosity_level} -s="${simulation_id}" -d=0 -testid=dut -RealEncryption=1

wait_for_background_jobs
