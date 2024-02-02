#define EVT_OFFSET_HCI_TRANSPORT 1

enum hci_transport_evt {
	EVT_HCI_TRANSPORT_RX_DATA_AVAILABLE = EVT_OFFSET_HCI_TRANSPORT,
	RX_REQ,
	RX_DONE,

	TX_ACCEPT,
	EVT_HCI_TRANSPORT_TX_READY,
	EVT_HCI_TRANSPORT_TX_IDLE_RAISE,
};

// isr
void hci_transport_evt_handler(enum hci_transport_evt);

// read
// async
// Only one invocation per rx ready.
// will invoke read_stop when read_cb when
int rx_into(uint8_t *dst, uint8_t dst_size);

// read_cb
// isr
void rx_into_cb();



////////////

/* A tx_req will result in a tx_accept. A tx_accept should be followed
 * by some number of tx, then a tx_done.
 */
int tx_req();
int tx(uint8_t buf, size_t size);
int tx_done();

/* Desync stuff */
int tx_abort();
