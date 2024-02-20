import struct

struct_fmt_pcap_hdr_s = '<IHHIIII'

def block(type: int, body: bytes):
    block_total_len = 12 + len(body)
    pass

def section_header(body):
    block_type = 0x0A0D0D0A
    byte_order_magic = 0x1A2B3C4D
    major_version = 1
    minor_version = 0
    section_length = -1

def interface_description(link_type):
    block_type = 0x0A0D0D0A
    snap_len = 0


LINKTYPE_BLUETOOTH_HCI_H4_WITH_PHDR = 201
LINKTYPE_NFLOG = 239
