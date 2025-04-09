To run the test:

```bash
(export BOARD=nrf52_bsim/native; nice west twister -j1 -nGciT. -p "$BOARD" && ./run.sh) && $BSIM_COMPONENTS_PATH/ext_2G4_phy_v1/dump_post_process/csv2pcapng -o tx.pcapng $BSIM_OUT_PATH/results/host_playground/d_2G4_00.Tx.csv
```
