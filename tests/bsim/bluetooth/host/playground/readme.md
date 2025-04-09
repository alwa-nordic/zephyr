To run the test:

```bash
(export BOARD=nrf52_bsim/native; nice west twister -j1 -nGciT. -p "$BOARD" && rr record ./run.sh) && $BSIM_COMPONENTS_PATH/ext_2G4_phy_v1/dump_post_process/csv2pcapng -o tx.pcapng $BSIM_OUT_PATH/results/host_playground/d_2G4_00.Tx.csv
```

This will generate a `tx.pcapng` file in the current directory that can be opened with Wireshark.

Test a generated RPA like this:

```bash
./rpa_resolve.py "0xff, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0" 63:c7:08:b8:2c:47
```