#!/usr/bin/env bash

: "${BSIM_OUT_PATH:?BSIM_OUT_PATH must be defined}"

here="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"

( cd "$here"/d0 && west build -b nrf52_bsim ) || exit 1
( cd "$here"/d1 && west build -b nrf52_bsim ) || exit 2
( cd "$here"/d2 && west build -b nrf52_bsim ) || exit 3

( cd "$BSIM_OUT_PATH"/bin && ./bs_2G4_phy_v1 -v=2 -s=foo -D=3 ) &
( cd "$BSIM_OUT_PATH"/bin && "$here"/d0/build/zephyr/zephyr.exe -v=2 -RealEncryption=1 -testid=the_test -s=foo -d=0 ) &
( cd "$BSIM_OUT_PATH"/bin && "$here"/d1/build/zephyr/zephyr.exe -v=2 -RealEncryption=1 -testid=the_test -s=foo -d=1 ) &
( cd "$BSIM_OUT_PATH"/bin && "$here"/d2/build/zephyr/zephyr.exe -v=2 -RealEncryption=1 -testid=the_test -s=foo -d=2 ) &

wait
