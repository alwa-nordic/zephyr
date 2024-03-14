#!/usr/bin/env python3

import argparse
from pathlib import Path
import sys
from typing import BinaryIO, Optional
import pcapng
import re
import datetime
from collections import defaultdict

LINKTYPE_BLUETOOTH_HCI_H4_WITH_PHDR = 201

bsim_line_pattern = re.compile(
    r"d_(?P<dev_num>\d{2}): @(?P<timestamp>\d{2}:\d{2}:\d{2}\.\d{6}) {2}(?P<line>.*)"
)


def parse_time(time: str) -> datetime.datetime:
    """time: like 00:00:01.123456"""
    return datetime.datetime.strptime(time, "%H:%M:%S.%f")


epoch = datetime.datetime.strptime("", "")


def parse_zlog_time(time: str) -> datetime.datetime:
    """time: like 00:00:01.123,456"""
    return parse_time(time.replace(",", ""))


def time_to_us(time: datetime.datetime) -> int:
    return int((time - epoch).total_seconds() * 1_000_000)


def parse_bsim_line(line: str):
    match = bsim_line_pattern.fullmatch(line)
    if not match:
        return None, None, line
    time = parse_time(match["timestamp"])
    devnum = int(match["dev_num"])
    return time, devnum, match["line"]


zlog_msg_pattern = re.compile(
    "(\x1b\\[[0-9;]+m)?(?P<start>\\[(?P<timestamp>[^]]*)\\] \x1b\\[[0-9;]+m<(?P<prio>[^>]+)> )?(?P<msg>[^\x1b]*)(?P<end>\x1b\\[0m)?"
)


def zlog_msg_match(line):
    # This pattern always matches. If it does not match a zlog
    # message, both start and end will be None.
    zlog_msg = zlog_msg_pattern.fullmatch(line)
    if zlog_msg is None:
        raise ValueError(
            "Line is not in zlog format. Did you mean to say --bsim?\n"
            + f"offending line: {repr(line)}"
        )

    return zlog_msg.groupdict()


def zlog_assembler():
    line = yield
    while True:
        prio = line["prio"]
        msg = line["msg"]
        timestamp = line["timestamp"]
        if timestamp is not None:
            timestamp = parse_zlog_time(timestamp)
        if line["start"]:
            while not line["end"]:
                line = yield
                msg += "\n" + line["msg"]
        line = yield timestamp, prio, msg


def self_starting(g):
    """Make generator into pipe"""

    def launch():
        gi = g()
        next(gi)
        return gi

    return launch


def eval(sexp):
    return sexp[0](*sexp[1:])


# scan1 (flip ($)) |> butts


def dispatch(workers, jobs):
    for worker_id, item in jobs:
        yield worker_id, workers(worker_id)(item)


# map (worker, item) -> Maybe result -> map (worker,)
# not None
def parallel_digest(workers, jobs):
    for worker_id, result in dispatch(workers, jobs):
        if result is not None:
            yield worker_id, result


def map_fst(f, xys):
    return ((f(x), y) for x, y in xys)


def defaultdict_selector(feed):
    return map_fst(defaultdict(self_starting(zlog_assembler)).get, feed)


def bsim_parallel_devices(next_assembler, bsim_log):
    devs = defaultdict(self_starting(next_assembler))
    for time, dev_id, msg in bsim_log:
        # Reassemble only if the log is from a device. 'None'
        # messages where not prefixed with 'd_XX: ', and come
        # from other sources like the run-script that started
        # the simulation. We still want to show them in the log
        # as-is, but we don't parse them in any way.
        msg = devs[dev_id].send(msg)

        # Yield the reassembled message if it is ready.
        if msg is not None:
            yield time, dev_id, msg


zlog_pri_to_syslog = {
    None: b"6",
    "err": b"3",
    "wrn": b"4",
    "inf": b"6",
    "dbg": b"7",
}


def zhexdump_read(zhex):
    return bytes.fromhex("".join(line[:73] for line in zhex.splitlines()[1:]))


def strip_hex(dump):
    return "\n".join(line.partition("|")[0] for line in dump.splitlines())


class PcapngOutput:
    def __init__(self, file: BinaryIO) -> None:
        self.interfaces = {}
        self.file = file
        self.file.write(pcapng.section_header_block())

    def _add_interface(self, name: str, link_type: int):
        self.file.write(pcapng.interface_description_block(link_type, name))
        ifid = len(self.interfaces)
        self.interfaces[name, link_type] = ifid
        return ifid

    def _find_or_add_interface(self, name: str, link_type: int):
        if (name, link_type) not in self.interfaces:
            return self._add_interface(name, link_type)
        return self.interfaces[name, link_type]

    def output_on_interface(self, name: str, link_type: int, time: int, data: bytes):
        ifid = self._find_or_add_interface(name, link_type)
        time_us = time_to_us(time)
        self.file.write(pcapng.enhanced_packet_block(ifid, time_us, data))


def syslog_pdu(msg: str, *, pri: Optional[bytes] = None) -> bytes:
    msg = msg.encode()
    if pri:
        msg = b"<" + pri + b">" + msg
    return msg


def output_syslog(
    pcap: PcapngOutput, dev: str, time_us: int, pri: bytes | None, msg: str
):
    ifname = f"log_{dev}"
    pcap.output_on_interface(
        ifname,
        pcapng.LINKTYPE_WIRESHARK_UPPER_PDU,
        time_us,
        pcapng.exported_pdu("syslog", syslog_pdu(msg, pri=pri)),
    )


def output_h4(pcap: PcapngOutput, dev: str, time_us: int, data: bytes):
    ifname = f"hci_{dev}"
    pcap.output_on_interface(
        ifname,
        LINKTYPE_BLUETOOTH_HCI_H4_WITH_PHDR,
        time_us,
        data,
    )


def map_bsim(f, bsim_log):
    return ((t, d, f(m)) for (t, d, m) in bsim_log)


def digest(f, feed):
    for x in feed:
        result = f(x)
        if result is not None:
            yield result


def main(args):
    pcap = PcapngOutput(args.output)

    output = args.input
    output = (line.rstrip("\n") for line in output)
    output = (line.replace("\x00", "") for line in output)

    if args.bsim:
        output = (parse_bsim_line(line) for line in output)
        output = map_bsim(zlog_msg_match, output)
        output = bsim_parallel_devices(zlog_assembler, output)
        prev_time = epoch
        for line in output:
            if not line:
                continue
            bstime, devnum, (ztime, prio, msg) = line
            time = bstime
            _ = ztime
            if time is None:
                time = prev_time
            prev_time = time
            pri = zlog_pri_to_syslog[prio]
            devname = str(devnum)
            if "!HCI!" in msg:
                data = strip_hex(msg.partition("!HCI!")[2])
                msg = bytes.fromhex(data)
                output_h4(pcap, devname, time, msg)
            else:
                output_syslog(pcap, devname, time, pri, msg)
    else:
        output = map(zlog_msg_match, output)
        x = zlog_assembler()
        next(x)
        output = digest(x.send, output)

        prev_time = epoch
        for time, prio, msg in output:
            if time is None:
                time = prev_time
            pri = zlog_pri_to_syslog[prio]
            if "!HCI!" in msg:
                data = strip_hex(msg.partition("!HCI!")[2])
                msg = bytes.fromhex(data)
                output_h4(pcap, "0", time, msg)
            else:
                output_syslog(pcap, "0", time, pri, msg)


def valid_path(path: str) -> Path:
    path = Path(path)
    if not path.exists():
        raise argparse.ArgumentTypeError(f"'{path.resolve()}' doesn't exist")
    return path


def open_wb(path: str) -> Path:
    return open(path, "wb")


def read_args() -> dict:
    parser = argparse.ArgumentParser(description="Pcap made easy")
    parser.add_argument(
        "--bsim",
        action="store_true",
        help="Read from bsim log format (with the 'd_XX: @HH:MM:SS.ssssss  ' prefix)",
    )

    parser.add_argument(
        "input",
        type=argparse.FileType("r"),
        help="input filename",
    )

    parser.add_argument(
        "output",
        type=argparse.FileType("wb"),
        help="output filename.pcapng",
    )

    return parser.parse_args()


if __name__ == "__main__":
    args = read_args()
    main(args)
