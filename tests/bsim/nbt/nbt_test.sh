#!/usr/bin/env bash
# Copyright 2024 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0
set -eu
source ${ZEPHYR_BASE}/tests/bsim/sh_common.source

# Basic connection test: a central connects to a peripheral and expects a
# notification, using the split controller (ULL LLL)
# Both central and peripheral hosts have their controllers in a separate device
# connected over UART. The controller is the HCI UART async sample.
simulation_id="nbt_basic"
verbosity_level=2
EXECUTE_TIMEOUT=10

pwd1="$PWD"
cd ${BSIM_OUT_PATH}/bin

UART_DIR=/tmp/bs_${USER}/${simulation_id}/
UART_PER=${UART_DIR}/peripheral

# Note the host+app devices are NOT connected to the phy, only the controllers are.

# Peripheral app + host :
Execute ./bs_${BOARD}_tests_bsim_nbt_peripheral_prj_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=10 -nosim -RealEncryption=1 \
  -testid=the_test -rs=23 -uart1_fifob_rxfile=${UART_PER}.rx -uart1_fifob_txfile=${UART_PER}.tx \
  -uart1_log_txfile="$pwd1/uart_tx.log" \
  -uart1_log_rxfile="$pwd1/uart_rx.log"

# Peripheral controller:
Execute ./bs_${BOARD}_samples_bluetooth_hci_uart_async_prj_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=0 -RealEncryption=1 \
  -rs=23 -uart1_fifob_rxfile=${UART_PER}.tx -uart1_fifob_txfile=${UART_PER}.rx

# Central app + host
Execute ./bs_${BOARD}_tests_bsim_nbt_central_prj_conf\
  -v=${verbosity_level} -s=${simulation_id} -d=1 -RealEncryption=1 \
  -testid=the_test -rs=6

Execute ./bs_2G4_phy_v1 -v=${verbosity_level} -s=${simulation_id} \
  -D=2 -sim_length=20e6 $@

wait_for_background_jobs
