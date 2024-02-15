import pcapng

LINKTYPE_BLUETOOTH_HCI_H4_WITH_PHDR = 201

PHDR_C2H = b"\x00\x00\x00\x00"
PHDR_H2C = b"\x00\x00\x00\x01"
H4_HCI_RESET = b"\x01\x03\x0C\x00"

with open("test.pcapng", "wb") as f:
    f.write(pcapng.section_header_block())
    f.write(pcapng.interface_description_block(pcapng.LINKTYPE_WIRESHARK_UPPER_PDU))
    f.write(pcapng.interface_description_block(LINKTYPE_BLUETOOTH_HCI_H4_WITH_PHDR))
    f.write(pcapng.enhanced_packet_block(0, 0, pcapng.exported_pdu("syslog", b"Hello, world!")))
    f.write(pcapng.enhanced_packet_block(1, 0, (PHDR_H2C + H4_HCI_RESET)))
