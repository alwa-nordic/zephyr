#!/usr/bin/env bash

set -eu
dotslash="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
bin_dir="${BSIM_OUT_PATH}/bin"
BOARD="${BOARD:-nrf52_bsim}"

cd "${dotslash}"

(
	cd tester/
	./_build.sh
)

(
	cd dut/
	./_build.sh
)
