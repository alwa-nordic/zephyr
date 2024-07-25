#include <zephyr/bluetooth/conn.h>

void bt_hci_c2hfc_ack(struct bt_conn *conn);
int bt_hci_c2hfc_bt_init(void);
bool bt_hci_c2hfc_process_tx(void);
