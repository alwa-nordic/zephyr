# PCAP Next Generation Dump File Format
# https://www.ietf.org/archive/id/draft-tuexen-opsawg-pcapng-05.html

# Endianness convention: Little endian whenever there is a choice.

import struct

END_OF_OPTIONS = 0


def pad32(data: bytes) -> bytes:
    padding_len = -len(data) % 4
    return data + b"\x00" * padding_len


def block(block_type: int, body: bytes) -> bytes:
    body = pad32(body)
    block_total_len = 12 + len(body)
    return (
        struct.pack("<I", block_type)
        + struct.pack("<I", block_total_len)
        + body
        + struct.pack("<I", block_total_len)
    )


def section_header_block() -> bytes:
    block_type = 0x0A0D0D0A
    byte_order_magic = 0x1A2B3C4D
    major_version = 1
    minor_version = 0
    section_length = 0xFFFFFFFF
    block_body = (
        struct.pack("<I", byte_order_magic)
        + struct.pack("<H", major_version)
        + struct.pack("<H", minor_version)
        + struct.pack("<q", section_length)
    )
    return block(block_type, block_body)


def option(tag: int, data: bytes) -> bytes:
    length = len(data)
    return struct.pack("<H", tag) + struct.pack("<H", length) + pad32(data)


def interface_description_block(link_type: int, name: str) -> bytes:
    OPT_IF_NAME = 2
    block_type = 0x00000001
    snap_len = 0xFFFFFFFF
    block_body = (
        struct.pack("<H", link_type)
        + struct.pack("<H", 0)  # reserved
        + struct.pack("<I", snap_len)
        + option(OPT_IF_NAME, name.encode())
        + option(END_OF_OPTIONS, b"")
    )

    return block(block_type, block_body)


def enhanced_packet_block(interface_id: int, timestamp_us: int, data: bytes) -> bytes:
    block_type = 0x00000006
    captured_packet_len = len(data)
    original_packet_len = captured_packet_len
    block_body = (
        struct.pack("<I", interface_id)
        + struct.pack("<I", timestamp_us // (1 << 32))
        + struct.pack("<I", timestamp_us % (1 << 32))
        + struct.pack("<I", captured_packet_len)
        + struct.pack("<I", original_packet_len)
        + pad32(data)
    )
    return block(block_type, block_body)


LINKTYPE_WIRESHARK_UPPER_PDU = 252
EXPORTED_PDU_TAG_END_OF_OPTIONS = 0
EXPORTED_PDU_TAG_DISSECTOR_NAME = 12


def exported_pdu_tag(tag: int, data: bytes) -> bytes:
    length = len(data)
    # Experiments show this must be big endian and not padded.
    return struct.pack(">H", tag) + struct.pack(">H", length) + data


def exported_pdu(dissector_name: str, data: bytes) -> bytes:
    dissector_name = dissector_name.encode()
    return (
        exported_pdu_tag(EXPORTED_PDU_TAG_DISSECTOR_NAME, dissector_name)
        + exported_pdu_tag(EXPORTED_PDU_TAG_END_OF_OPTIONS, b"")
        + data
    )
