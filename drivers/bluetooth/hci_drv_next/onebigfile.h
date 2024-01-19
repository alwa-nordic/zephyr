

/**

Gives the first 4-bytes of data from the H4 stream.

These 4 bytes need to be available to tell us the next read
size.

In these 4 bytes, there is a lot of juicy information for a
allocator.
	- h4
	- If evt:
		- evt type
		- evt len
	- If acl/iso:
		- handle
		- least sig. octet of len

E_please_reset (basically treat these as a HCI hw error)
	E_recv_bad_h4_type
	E_lower_io_error
*/
// called from driver
// Guaranteed to return non-NULL
// Blocking allowed
struct net_buf *get_buf_from_host(uint8_t peek_buf[4]);

// to driver
// this will loop forever
// it will call get_buf
// it will then call lower driver stuff
// it will then call bt_recv
// and loop
int driver_main(void);
