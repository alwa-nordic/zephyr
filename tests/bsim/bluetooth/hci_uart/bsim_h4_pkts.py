import sys
from enum import IntEnum
import datetime
import heapq

# Usage:
# python3 bsim_h4_pkts.py hci_tx.bsim_uart hci_rx.bsim_uart | text2pcap -t '%H:%M:%S.%f' -DE bluetooth-h4 - uart.pcapng

class H4Type(IntEnum):
    CMD = 0x01
    ACL = 0x02
    SCO = 0x03
    EVT = 0x04
    ISO = 0x05

def bsim_bytes(f):
    hdr = next(f)
    assert hdr == 'time(microsecond),byte\n'
    for line in f:
        t_us, c = line.strip().split(',')
        t_us = int(t_us)
        c = int(c, 16)
        assert 0 <= c <= 255
        yield t_us, c

def bytes_append_from_iter(bs, it, count):
    for _ in range(count):
        bs.append(next(it))

def read_one_h4_pkt(it):
    pkt = bytearray()
    pkt_start_t_us, h4_type = next(it)
    pkt.append(h4_type)

    h4_type = H4Type(h4_type)

    it = map(lambda x: x[1], it)

    if h4_type == H4Type.CMD:
        bytes_append_from_iter(pkt, it, 3)
        pkt_len = pkt[-1]

    if h4_type == H4Type.EVT:
        bytes_append_from_iter(pkt, it, 2)
        pkt_len = pkt[-1]

    if h4_type == H4Type.ACL:
        bytes_append_from_iter(pkt, it, 4)
        pkt_len = pkt[-1] << 8 | pkt[-2]

    if h4_type == H4Type.SCO:
        bytes_append_from_iter(pkt, it, 3)
        pkt_len = pkt[-1]

    if h4_type == H4Type.ISO:
        bytes_append_from_iter(pkt, it, 4)
        pkt_len = pkt[-1] << 8 | pkt[-2]
        pkt_len = pkt_len & 0x3FFF

    bytes_append_from_iter(pkt, it, pkt_len)

    return pkt_start_t_us, pkt

def read_pkts(f):
    strm = bsim_bytes(f)
    while True:
        try:
            yield read_one_h4_pkt(strm)
        except StopIteration:
            break

def merge_its(a, b, key=None):
    a = iter(a)
    b = iter(b)

    merged_strm = heapq.merge(fo, fi, key=lambda x: x[0])

    for pkt_start_t_us, pkt in merged_strm:
        t = datetime.timedelta(microseconds=pkt_start_t_us)
        print(f"O {t}")
        print(f"000000 {pkt.hex(' ')}")

def main():
    pkts_o = [(t, 'O', p) for t, p in read_pkts(open(sys.argv[1]))]
    pkts_i = [(t, 'I', p) for t, p in read_pkts(open(sys.argv[2]))]

    pkts = heapq.merge(pkts_o, pkts_i)

    for pkt in pkts:
            pkt_start_t_us, direction, data = pkt
            t = datetime.timedelta(microseconds=pkt_start_t_us)
            print(f"{direction} {t}")
            print(f"000000 {data.hex(' ')}")

if __name__ == "__main__":
    main()
