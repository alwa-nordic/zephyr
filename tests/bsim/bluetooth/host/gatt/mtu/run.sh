#!/usr/bin/env bash

set -eu
dotslash="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
bin_dir="${BSIM_OUT_PATH}/bin"
BOARD="${BOARD:-nrf52_bsim}"

cd "${dotslash}"

compile_path0="${bin_dir}/bs_${BOARD}_"
compile_path0+="$(realpath --relative-to "$(west topdir)"/zephyr tester/prj.conf | tr /. _)"

compile_path1="${bin_dir}/bs_${BOARD}_"
compile_path1+="$(realpath --relative-to "$(west topdir)"/zephyr dut/prj.conf | tr /. _)"

args_all=(-s=mtu)
args_dev=(-v=2 -RealEncryption=1)
sim_seconds=60

echo "Simulation time: $sim_seconds seconds"

# bs_2G4_phy_v1 requires pwd to at its location
cd "${BSIM_OUT_PATH}/bin"

("${compile_path0}" "${args_all[@]}" "${args_dev[@]}" -d=0 || echo d0 $?) &
("${compile_path1}" "${args_all[@]}" "${args_dev[@]}" -d=1 || echo d1 $?) &
(./bs_2G4_phy_v1 "${args_all[@]}" -D=2 -v=6 -sim_length=$((sim_seconds * 10 ** 6)) || echo phy $?) &

#gdb --args "${compile_path1}" "${args_all[@]}" "${args_dev[@]}" -d=1

wait
