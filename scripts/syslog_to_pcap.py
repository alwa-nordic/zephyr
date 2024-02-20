import sys
import pcapng
import re
import datetime
from collections import defaultdict

LINKTYPE_BLUETOOTH_HCI_H4 = 187
LINKTYPE_BLUETOOTH_HCI_H4_WITH_PHDR = 201

PHDR_C2H = b"\x00\x00\x00\x00"
PHDR_H2C = b"\x00\x00\x00\x01"
H4_HCI_RESET = b"\x01\x03\x0C\x00"

ansi_term_escape_pattern = re.compile("\x1b\\[[0-9;]*[a-zA-Z]")


def remove_ansi_term_escapes(data: str) -> str:
    return ansi_term_escape_pattern.sub("", data)


bsim_line_pattern = re.compile(
    r"d_(?P<dev_num>\d{2}): @(?P<timestamp>\d{2}:\d{2}:\d{2}\.\d{6}) {2}(?P<line>.*)"
)


def parse_time(time: str) -> int:
    """time: like 00:00:01.123456"""
    return datetime.datetime.strptime(time, "%H:%M:%S.%f")


def time_to_us(time: datetime.datetime) -> int:
    epoch = datetime.datetime.strptime("", "")
    return int((time - epoch).total_seconds() * 1_000_000)


def parse_bsim_line(line: str):
    match = bsim_line_pattern.fullmatch(line)
    if not match:
        return None, None, line
    time_us = time_to_us(parse_time(match["timestamp"]))
    devnum = int(match["dev_num"])
    return time_us, devnum, match["line"]


zlog_msg_pattern = re.compile(
    "(?P<start> \\[(?P<timestamp>[^]]*)\\] \x1b\\[[0-9;]+m<(?P<prio>[^>]+)> )?(?P<msg>[^\x1b]*)(?P<end>\x1b\\[0m)?"
)

def zlog_msg_match(line):
    # This pattern always matches. If it does not match a zlog
    # message, both start and end will be None.
    return zlog_msg_pattern.fullmatch(line).groupdict()


# input tuples: start msg end
# states: line-based, (msg-based current-msg)
# transitions: start -> msg, msg -> msg, msg -> end


def zlog_assembler():
    line = yield
    while True:
        prio = line["prio"]
        msg = line["msg"]
        if line["start"]:
            while not line["end"]:
                line = yield
                msg += "\n" + line["msg"]
        line = yield prio, msg

def launch_zlog_assembler():
    x = zlog_assembler()
    next(x)
    return x

def bsim_parallel_devices(lines):
    devs = defaultdict(launch_zlog_assembler)
    for line in lines:
        time_us, devnum, msg = line

        # Reassemble only if the log is from a device. 'None'
        # messages where not prefixed with 'd_XX: ', and come
        # from other sources like the run-script that started
        # the simulation. We still want to show them in the log
        # as-is, but we don't parse them in any way.
        if devnum is not None:
            msg = devs[devnum].send(zlog_msg_match(msg))
        else:
            msg = (None, msg)

        # Yield the reassembled message if it is ready.
        if msg is not None:
            yield time_us, devnum, msg

zlog_pri_to_syslog = {
    None: b'6',
    'err': b'3',
    'wrn': b'4',
    'inf': b'6',
    'dbg': b'7',
}

def zhexdump_read(zhex):
    return bytes.fromhex(''.join(line[:73] for line in zhex.splitlines()[1:]))

PHDR_C2H = b"\x00\x00\x00\x00"
PHDR_H2C = b"\x00\x00\x00\x01"

with open("/dev/stdout", "wb") as f:
    f.write(pcapng.section_header_block())
    # Syslog 'None'
    f.write(pcapng.interface_description_block(pcapng.LINKTYPE_WIRESHARK_UPPER_PDU))
    # Syslog 'd1'
    f.write(pcapng.interface_description_block(pcapng.LINKTYPE_WIRESHARK_UPPER_PDU))
    # H4 'd1'
    f.write(pcapng.interface_description_block(LINKTYPE_BLUETOOTH_HCI_H4_WITH_PHDR))
    # Syslog 'd2'
    f.write(pcapng.interface_description_block(pcapng.LINKTYPE_WIRESHARK_UPPER_PDU))
    # H4 'd2'
    f.write(pcapng.interface_description_block(LINKTYPE_BLUETOOTH_HCI_H4_WITH_PHDR))

    def output_syslog(devnum: int, time_us: int, pri: bytes | None, data: bytes):
        if pri:
            data = b"<" + pri + b">" + data
        f.write(
            pcapng.enhanced_packet_block(
                devnum, time_us, pcapng.exported_pdu("syslog", data)
            )
        )

    def output_h4(devnum: int, time_us: int, data: bytes):
        f.write(
            pcapng.enhanced_packet_block(
                devnum, time_us, data
            )
        )

    output = sys.stdin
    output = (line.rstrip("\n") for line in output)
    output = (parse_bsim_line(line) for line in output)
    output = bsim_parallel_devices(output)
    for line in output:
        if not line:
            continue
        time_us, devnum, (prio, msg) = line
        if time_us is None:
            time_us = 0
        if devnum is None:
            devnum = 0
        else:
            devnum *= 2
            devnum += 1
        pri = zlog_pri_to_syslog[prio]
        if "!H2C!" in msg:
            devnum = devnum + 1
            msg = PHDR_C2H + zhexdump_read(msg)
            output_h4(devnum, time_us, msg)
        elif "recv_expect: I\n" in msg:
            devnum = devnum + 1
            msg = PHDR_H2C + zhexdump_read(msg)
            output_h4(devnum, time_us, msg)
        else:
            msg =  msg.encode()
            output_syslog(devnum, time_us, pri, msg)
