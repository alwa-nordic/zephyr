#include <stdint.h>
#include <stddef.h>

struct s_args;
typedef void (*s)(struct s_args *args);
struct s_args {
    s state;
    uint16_t required_len;
};

static const struct s_args s_evt = {
	.state = p_evt,
	.required_len = H4_HDR_SIZE + BT_HCI_EVT_HDR_SIZE,
};

static struct s_args p_acl(uint8_t *peek)
{
	uint16_t acl_payload_length;

	acl_payload_length = sys_get_le16(&peek_buf[BT_HCI_ACL_HDR_SIZE + 2]);

	*next_parser = NULL;
	*out_alloc = rx_alloc_h4_acl_alloc;

	return H4_HDR_SIZE + BT_HCI_ACL_HDR_SIZE + acl_payload_length;
}

static const struct s_args s_acl = {
	.state = p_acl,
	.required_len = H4_HDR_SIZE + BT_HCI_ACL_HDR_SIZE,
};

static struct s_args p_h4(uint8_t *peek)
{
	/* hci_type */
	switch (peek[0]) {
	case H4_EVT:
		return s_evt;
	case H4_ACL:
		return s_acl;
	default:
		LOG_ERR("Unexpected hci type: %u", hci_type);
		return s_ioerr;
	}
}

static const struct s_args s_h4 = {
	.state = p_h4,
	.required_len = H4_HDR_SIZE,
};

static void s_start(struct s_args *args)
{
    *args = h4_hdr;
}

extern void read(uint16_t read_len);

int foo(void) {
    struct s_args args;
    s_start(&args);
    while (args->state) {
        read(len);
        args->state(&len, &state);
    }
    return 0;
}
