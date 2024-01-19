#define SCRATCH_BUF_SIZE 6

struct packetizer {
	int state;
	uint8_t scratch_buf[SCRATCH_BUF_SIZE];
	bool have_hdr;
	bool have_h4 uint8_t h4_type;
	uint8_t hci_evt_type;
	uint8_t hci_evt_meta_type;
	uint8_t hci_evt_len;
	uint16_t parsed;
	uint16_t remaining;
	uint16_t cmd_opcode;
	size_t total_packet_len;
};

/*

for our stack, pulling 1b is inefficient. the host wants a whole packet.
-> read_whole_packet_into(dstbuf, h4_type)

app: init packetizer
app: packetizer.whats-next
pkt: I need 2 bytes
app->drv: give me 2 bytes
-> here, driver can early-return with e.g. 1b
in that case, application has to call it again until it has received the amount pkt wants

...
pkt: I'm done, here's a full packet with this metadata
app: maybe discard, maybe ask the host for a netbuf
app: memcpy into netbuf
app: bt_recv(netbuf)


*/

struct packetizer packetizer;

struct net_buf *smart_alloc(struct packetizer *p)
{
	/* TODO: smarts using the packetizer metadata */
	struct net_buf *buf = bt_buf_get_rx(BT_BUF_ACL_IN, K_NO_WAIT);
	if (!buf) {
		LOG_ERR("Failed to allocate ACL buffer");
		return -ENOBUF;
	}

	return buf;
}

int main()
{
	packetizer_init();
	while (packetizer_finished() == false) {
		int need_bytes = packetizer_next_read_size(&packetizer);

		if (need_bytes) {
			driver_read(need_bytes, packetizer_active_buf());
			driver_wait_for_completion();
			/* at this point, we are guaranteed to have read
			 * `need_bytes` into `packetizer_active_buf()
			 */
			packetizer_cosmic_read(&packetizer);
		}
	}

	// if app doesn't want packet, it gives a NULL buf
	struct net_buf *buf = smart_alloc(&packetizer);
	if (buf) {
		memcpy(buf->data, packetizer_active_buf_start(), packetizer_parsed_len());
		int need_bytes = packetizer_full_packet_len() - packetizer_parsed_len();

		driver_read(need_bytes, buf->data);
		driver_wait_for_completion();
		bt_recv(buf);
	}
}

int packetizer_full_packet_len(void)
{
	return packetizer.remaining + packetizer.parsed;
}

int packetizer_parsed_len(void)
{
	return packetizer.parsed;
}

uint8_t *packetizer_active_buf(void)
{
	return packetizer.scratch_buf;
}

void packetizer_cosmic_read(struct packetizer *p)
{
	/* choose-your-own-adventure machine */
	switch (p->state) {
	case 0: // empty
		p->h4_type = p->scratch_buf[p->parsed];
		__ASSERT_NO_MSG(p->parsed == 0);
		p->parsed += 1;
		p->total_packet_len += 1;

		switch (p->h4_type) {
		case H4_EVT:
			p->state = 1;
			break;
		case H4_ACL:
		case H4_ISO:
			p->state = 2;
			break;
		default:
			__ASSERT(0, "invalid h4 type");
		}
		break;
	case 1: // evt start
		p->hci_evt_type = p->scratch_buf[p->parsed];
		p->parsed += 1;
		p->total_packet_len += 1;

		p->total_packet_len += p->scratch_buf[p->parsed];
		p->parsed += 1;
		p->total_packet_len += 1;

		switch (p->hci_evt_type) {
		case BT_HCI_EVT_CMD_COMPLETE:
			p->state = 4;
			break;
		case BT_HCI_EVT_CMD_STATUS:
			p->state = 5;
			break;
		case BT_HCI_EVT_LE_META_EVENT:
			p->state = 6;
			break;
		default:
			p->remaining = p->scratch_buf[2];
			p->state = 8;
		}
		break;
	case 2: // acl/iso start
		p->parsed += 2; // skip handle
		p->total_packet_len += 2;
		p->remaining = sys_get_le16(&p->scratch_buf[p->parsed]);
		p->parsed += 2;
		p->total_packet_len += 2;
		p->state = 8;
		break;
	case 5: // evt status start
		p->parsed += 1; // status
		// fallthrough
	case 4: // evt cmd start
		p->parsed += 1; // ncmd
		p->cmd_opcode = sys_get_le16(&p->scratch_buf[p->parsed]);
		p->parsed += 2;
		p->remaining = p->total_packet_len - p->parsed;
		p->state = 8;
	case 6: // evt meta start
		p->hci_evt_meta_type = p->scratch_buf[p->parsed];
		p->parsed += 1;
		p->remaining = p->total_packet_len - p->parsed;
		p->state = 8;
	case 8: // payload
		p->state = 0;
	}
}

void packetizer_init()
{
	memset(&packetizer, 0, sizeof(packetizer));
}

size_t packetizer_next_read_size(struct packetizer *p)
{
	/* choose-your-own-adventure state machine */
	switch (p->state) {
	case 0: // empty
		return 1;
	case 1: // evt start
		return 2;
	case 2: // acl start
		return 4;
	case 3: // iso start
		return 4;
	case 4: // evt cmd start
		return 3;
	case 5: // evt status start
		return 4;
	case 6: // evt meta start
		return 1;
	case 7: // payload
		return p->remaining;
	}
}
