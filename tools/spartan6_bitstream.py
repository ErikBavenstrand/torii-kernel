#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.9"
# ///
"""Spartan-6 configuration packet parser (UG380 ch.5, 16-bit packets).

Usage: spartan6_bitstream.py BITSTREAM.bin...
"""

import collections
import struct
import sys

REGS = {
    0x00: "CRC",
    0x01: "FAR_MAJ",
    0x02: "FAR_MIN",
    0x03: "FDRI",
    0x04: "FDRO",
    0x05: "CMD",
    0x06: "CTL",
    0x07: "MASK",
    0x08: "STAT",
    0x09: "LOUT",
    0x0A: "COR1",
    0x0B: "COR2",
    0x0C: "PWRDN_REG",
    0x0D: "FLR",
    0x0E: "IDCODE",
    0x0F: "CWDT",
    0x10: "HC_OPT_REG",
    0x12: "CSBO",
    0x13: "GENERAL1",
    0x14: "GENERAL2",
    0x15: "GENERAL3",
    0x16: "GENERAL4",
    0x17: "GENERAL5",
    0x18: "MODE_REG",
    0x19: "PU_GWE",
    0x1A: "PU_GTS",
    0x1B: "MFWR",
    0x1C: "CCLK_FREQ",
    0x1D: "SEU_OPT",
    0x1E: "EXP_SIGN",
    0x1F: "RDBK_SIGN",
    0x20: "BOOTSTS",
    0x21: "EYE_MASK",
    0x22: "CBC_REG",
}
CMDS = {
    0: "NULL",
    1: "WCFG",
    2: "MFW",
    3: "LFRM",
    4: "RCFG",
    5: "START",
    7: "RCRC",
    8: "AGHIGH",
    10: "GRESTORE",
    11: "SHUTDOWN",
    13: "DESYNC",
    14: "IPROG",
}
IDCODES = {
    0x04001093: "XC6SLX9",
    0x04002093: "XC6SLX16",
    0x04000093: "XC6SLX4",
    0x04004093: "XC6SLX25",
    0x04008093: "XC6SLX45",
}
OPS = {0: "NOP", 1: "READ", 2: "WRITE", 3: "RSVD"}


def parse(path):
    with open(path, "rb") as f:
        data = f.read()
    pos = 0
    sections = 0
    while True:
        i = data.find(b"\xaa\x99\x55\x66", pos)
        if i < 0:
            break
        sections += 1
        print(f"--- SYNC at 0x{i:x} (section {sections})")
        p = i + 4
        last_reg = None
        nops = 0
        while p + 2 <= len(data):
            (w,) = struct.unpack(">H", data[p : p + 2])
            if w == 0xFFFF and nops == 0 and last_reg == "DESYNC":
                break
            typ, op = w >> 13, (w >> 11) & 3
            if typ == 1:
                reg, cnt = (w >> 5) & 0x3F, w & 0x1F
                p += 2
                words = list(struct.unpack(f">{cnt}H", data[p : p + 2 * cnt]))
                p += 2 * cnt
                name = REGS.get(reg, f"REG{reg:#x}")
                if op == 0:
                    nops += 1
                    continue
                if nops:
                    print(f"  ({nops} NOP)")
                    nops = 0
                desc = " ".join(f"{x:04x}" for x in words)
                if name == "CMD" and words:
                    desc += f"  -> {CMDS.get(words[0], '?')}"
                    last_reg = CMDS.get(words[0])
                elif name == "IDCODE" and len(words) == 2:
                    idc = (words[0] << 16) | words[1]
                    desc += f"  -> 0x{idc:08x} {IDCODES.get(idc & 0x0FFFFFFF, '?')}"
                print(f"  0x{p - 2 * cnt - 2:06x} T1 {OPS[op]:5} {name:10} {desc}")
                last_t1 = name
            elif typ == 2:
                last_t1 = REGS.get((w >> 5) & 0x3F, "?")
                hi, lo = struct.unpack(">HH", data[p + 2 : p + 6])
                n = (hi << 16) | lo
                p += 6
                if nops:
                    print(f"  ({nops} NOP)")
                    nops = 0
                print(
                    f"  0x{p - 6:06x} T2 {OPS[op]:5} {last_t1:10} {n} words "
                    f"({n * 2} bytes) payload 0x{p:x}..0x{p + 2 * n:x}"
                )
                payload = data[p : p + 2 * n]
                p += 2 * n
                if last_t1 == "FDRI":
                    crc = struct.unpack(">HH", data[p : p + 4])
                    print(f"      auto-CRC after FDRI: {crc[0]:04x}{crc[1]:04x}")
                    p += 4
                c = collections.Counter(payload)
                print(
                    f"      zero bytes {c[0] / max(len(payload), 1):.1%}, "
                    f"distinct bytes {len(c)}"
                )
            else:
                if w == 0xFFFF:
                    p += 2
                    continue
                print(f"  0x{p:06x} unknown word {w:04x}; stop")
                break
        pos = p
    print(f"end at 0x{pos:x} of 0x{len(data):x}")


if __name__ == "__main__":
    for f in sys.argv[1:]:
        print(f"===== {f}")
        parse(f)
