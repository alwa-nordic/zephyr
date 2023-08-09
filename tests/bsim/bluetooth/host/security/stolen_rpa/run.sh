#!/usr/bin/env bash

set -eu
dotslash="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"

args_all=(-s=stolen_rpa -D=3)
args_dev=(-v=3 -RealEncryption=1 -testid=the_test)
sim_seconds=60

echo "Simulation time: $sim_seconds seconds"

# Required for 
cd "${BSIM_OUT_PATH}/bin"

("$dotslash"/app/build/zephyr/zephyr.elf "${args_all[@]}" "${args_dev[@]}" -d=0 || echo d0 $? ) &
("$dotslash"/app/build/zephyr/zephyr.elf "${args_all[@]}" "${args_dev[@]}" -d=1 || echo d1 $? ) &
("$dotslash"/app/build/zephyr/zephyr.elf "${args_all[@]}" "${args_dev[@]}" -d=2 || echo d2 $? ) &
(./bs_2G4_phy_v1 "${args_all[@]}" -v=6 -sim_length=$((sim_seconds * 10**6)) || echo phy $?) &

wait
