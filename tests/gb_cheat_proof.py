#!/usr/bin/env python3
"""The proof that Torii's GB and GBC cheats run as a cheat device does (tests/gb_cheat_proof.sh).

Each case is a cheat file for the proof's ROM (tests/gb_cheat_rom.py: an MBC1, MBC3
or MBC5 game of 8 banks, GB or GBC) that runs three ways: in mGBA's own Game Boy
core with mGBA's cheat device (mgba-shot -E), through Torii on the emulated cart
(Jagoomba, the file at /torii/cheats/CP.cht, every cheat picked), and on paper: a
model of the device (`device`), which patches a Game Genie byte in every bank
that holds its compare byte (or every bank, with none) as the device on the bus
does, and writes a GameShark byte each frame to its WRAM bank. The ROM records
what it reads into WRAM, so the three are compared byte by byte:

- Torii must match the model in every case, apart from what Torii leaves out on
  purpose (a byte the emulators' checksum samples: `torii_skips`).
- mGBA must match the model too, apart from its known shortcuts (`mgba_differs`:
  it patches only the first bank that holds a compare byte, a code without one
  only in the bank mapped when it applies, and ignores a GameShark code's WRAM
  bank), where it must match what that shortcut predicts. That mGBA agrees
  shows the ROM and the model are right; the cases where it differs are
  documented, not skipped.

usage: gb_cheat_proof.py CARTS [CASE...]   (run inside the torii-emu container)
"""

import os
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from emu_cheat_codes import gb_genie
from gb_cheat_rom import (
    BANK0,
    CAP,
    HOME_WRAM,
    RESULTS,
    TWIN,
    bank_byte,
    layout,
    rom,
    watched,
)

SHOT = "emu/build-linux/mgba-shot"
FRAMES = 300  # of the game, in each run: the results settle within a few


@dataclass
class Case:
    name: str
    mbc: str
    gbc: bool
    cheats: list[str]  # one code (lines joined by +) per cheat
    mgba_differs: str = ""  # why mGBA's shortcut differs from the device here
    torii_skips: str = ""  # what Torii leaves out on purpose
    dialog: bool = False  # Torii says after the load that something wasn't applied
    off: list[int] = field(default_factory=list)  # cheats in the file but not picked


def gs(value: int, addr: int, kind: int = 0x01) -> str:
    return f"{kind:02X}{value:02X}{addr & 0xFF:02X}{addr >> 8:02X}"


def cases() -> list[Case]:
    out = []
    for mbc in ("mbc1", "mbc3", "mbc5"):
        out += [
            Case(
                f"{mbc} genie compare, one bank",
                mbc,
                False,
                [gb_genie(0x4010, 0xC3, bank_byte(4))],
            ),
            Case(
                f"{mbc} genie compare, two banks",
                mbc,
                False,
                [gb_genie(0x4011, 0xA7, TWIN)],
                mgba_differs="mGBA patches the first bank holding the compare byte only",
            ),
            Case(
                f"{mbc} genie without compare, switched banks",
                mbc,
                False,
                [gb_genie(0x4012, 0x66)],
                mgba_differs="mGBA patches the bank mapped when it applies the code only",
            ),
            Case(f"{mbc} genie bank 0", mbc, False, [gb_genie(0x0710, 0x3C)]),
            Case(
                f"{mbc} genie bank 0 with compare",
                mbc,
                False,
                [gb_genie(0x0710, 0x3C, BANK0[0x0710])],
            ),
            Case(
                f"{mbc} genie compare matching no bank",
                mbc,
                False,
                [gb_genie(0x4010, 0xC3, 0x01)],
                dialog=True,
            ),
        ]
    out += [
        Case(
            "mbc5 genie bank 0, wrong compare",
            "mbc5",
            False,
            [gb_genie(0x0710, 0x3C, 0x11)],
            dialog=True,
        ),
        Case(
            "mbc5 genie on a checksum byte",
            "mbc5",
            False,
            [gb_genie(0x0780, 0x77)],
            torii_skips="a byte the emulators' checksum samples stays unpatched",
            dialog=True,
        ),
        Case(
            "mbc5 genie picked off",
            "mbc5",
            False,
            [gb_genie(0x4010, 0xC3, bank_byte(4))],
            off=[0],
        ),
        Case("gb gameshark 01 C100", "mbc5", False, [gs(0x55, 0xC100)]),
        Case("gb gameshark 01 through the echo", "mbc5", False, [gs(0x66, 0xE180)]),
        Case("gb gameshark 01 HRAM", "mbc5", False, [gs(0x77, 0xFF90)]),
        Case("gb gameshark 01 D100", "mbc5", False, [gs(0x44, 0xD100)]),
        Case(
            "gb several cheats of both kinds",
            "mbc3",
            False,
            [
                gs(0x55, 0xC100) + "+" + gs(0x77, 0xFF90),
                gb_genie(0x4010, 0xC3, bank_byte(6)),
                gs(0x12, 0xC180),
                gb_genie(0x0710, 0x3D),
            ],
        ),
        Case(
            "gbc genie compare, one bank",
            "mbc5",
            True,
            [gb_genie(0x4010, 0xC3, bank_byte(6))],
        ),
        Case(
            "mbc5 genie compare, the last bank",
            "mbc5",
            False,
            [gb_genie(0x4010, 0xC3, bank_byte(7))],
            mgba_differs="mGBA's compare search stops before the last bank",
        ),
        Case(
            "gbc genie without compare",
            "mbc5",
            True,
            [gb_genie(0x4012, 0x66)],
            mgba_differs="mGBA patches the bank mapped when it applies the code only",
        ),
        Case(
            "gbc gameshark 01 D100, the mapped bank", "mbc5", True, [gs(0x44, 0xD100)]
        ),
        Case(
            f"gbc gameshark 9{HOME_WRAM} D100, the mapped bank",
            "mbc5",
            True,
            [gs(0x45, 0xD100, 0x90 + HOME_WRAM)],
        ),
        Case(
            "gbc gameshark 95 D100, another bank",
            "mbc5",
            True,
            [gs(0x46, 0xD100, 0x95)],
            mgba_differs="mGBA ignores the type byte and writes the mapped bank",
        ),
        Case(
            "gbc gameshark 90 D100, bank 1",
            "mbc5",
            True,
            [gs(0x47, 0xD100, 0x90)],
            mgba_differs="mGBA ignores the type byte and writes the mapped bank",
        ),
        Case(
            "gbc gameshark 01 C100 and HRAM",
            "mbc5",
            True,
            [gs(0x55, 0xC100) + "+" + gs(0x77, 0xFF90)],
        ),
    ]
    return out


def decode(code: str) -> list[tuple]:
    """('genie', addr, value, compare|None) or ('gs', kind, value, addr) per line, as the device reads them."""
    out = []
    for line in code.split("+"):
        if "-" in line:
            p = line.split("-")
            op1, op2 = int(p[0], 16), int(p[1], 16)
            addr = (op1 & 0xF) << 8 | (op2 >> 4) & 0xFF | ((op2 & 0xF) ^ 0xF) << 12
            compare = None
            if len(p) == 3:
                op3 = int(p[2], 16)
                v = (op3 & 0xF00) << 20 | (op3 & 0xF)
                v = (v >> 2 | v << 30) & 0xFFFFFFFF
                v |= v >> 24
                compare = (v ^ 0xBA) & 0xFF
            out.append(("genie", addr, op1 >> 4, compare))
        else:
            op = int(line, 16)
            out.append(
                ("gs", op >> 24, op >> 16 & 0xFF, (op & 0xFF) << 8 | op >> 8 & 0xFF)
            )
    return out


def predict(case: Case, who: str) -> dict[str, int]:
    """The results block, by name, that the ROM should record: who is device, torii or mgba."""
    image = bytearray(rom(case.mbc, case.gbc))
    ram = {}  # (addr, bank): value, written every frame
    for i, code in enumerate(case.cheats):
        if i in case.off:
            continue
        for line in decode(code):
            if line[0] == "genie":
                _, addr, value, compare = line
                if addr < 0x4000:
                    if (
                        who == "torii"
                        and case.torii_skips
                        and addr < 0x4000
                        and (addr & 127) < 4
                    ):
                        continue
                    if compare is None or image[addr] == compare:
                        image[addr] = value
                    continue
                banks = [
                    b
                    for b in range(1, 8)
                    if compare is None or image[b * 0x4000 + addr - 0x4000] == compare
                ]
                if who == "mgba":
                    # bank 1 is mapped at each frame's end; the search for a compare
                    # stops before the last bank (maxSegment is the last, the loop's < skips it)
                    banks = [1] if compare is None else [b for b in banks if b < 7][:1]
                for b in banks:
                    image[b * 0x4000 + addr - 0x4000] = value
            else:
                _, kind, value, addr = line
                if 0xE000 <= addr < 0xFE00:
                    addr -= 0x2000
                bank = 0
                if 0xD000 <= addr < 0xE000 and case.gbc:
                    bank = HOME_WRAM
                    if kind >= 0x90 and who != "mgba":
                        bank = (kind & 7) or 1
                ram[(addr, bank)] = value
    want = {"ready": 0x42}
    for b in range(1, 8):
        for off in (0x4010, 0x4011, 0x4012):
            want[f"rom {off:04X} bank {b}"] = image[b * 0x4000 + off - 0x4000]
    want["rom 0710"] = image[0x0710]
    want["rom 0780"] = image[0x0780]
    for addr, bank in watched(case.gbc):
        name = f"ram {addr:04X}" + (f" bank {bank}" if bank else "")
        value = ram.get((addr, bank))
        want[name + " value"] = 0 if value is None else value
        want[name + " frames"] = 0 if value is None else CAP
    return want


def recorded(text: str, base: int, gbc: bool) -> dict[str, int]:
    """The results block from mgba-shot's -m lines for 0x80 bytes at base."""
    data = bytearray()
    for line in text.splitlines():
        if line.startswith("mem frame "):
            data += bytes(int(x, 16) for x in line.split(":", 1)[1].split())
    assert len(data) >= 0x80, f"no results block in the output ({len(data)} bytes)"
    return {name: data[off] for name, off in layout(gbc).items()}


def cht(case: Case, mgba: bool = False) -> str:
    """The case's file; mgba leaves out the cheats not picked (mgba-shot -E turns every one on)."""
    codes = [c for i, c in enumerate(case.cheats) if not (mgba and i in case.off)]
    lines = [f"cheats = {len(codes)}", ""]
    for i, code in enumerate(codes):
        on = "false" if i in case.off and not mgba else "true"
        lines += [
            f'cheat{i}_desc = "Cheat {i}"',
            f'cheat{i}_code = "{code}"',
            f"cheat{i}_enable = {on}",
            "",
        ]
    return "\n".join(lines)


def sh(
    *args: str, check: bool = False, env: dict | None = None
) -> subprocess.CompletedProcess:
    return subprocess.run(args, capture_output=True, text=True, check=check, env=env)


def run(case: Case, carts: Path, xgb_ram: int) -> list[str]:
    """Runs the case both ways; returns its failures."""
    d = carts / case.name.replace(" ", "-").replace(",", "")
    d.mkdir(parents=True)
    ext = ".gbc" if case.gbc else ".gb"
    game = d / ("CP" + ext)
    game.write_bytes(rom(case.mbc, case.gbc))
    (d / "CP.cht").write_text(cht(case))
    (d / "mgba.cht").write_text(cht(case, mgba=True))
    fails = []
    # mGBA's GB core and cheat device
    picked = len(case.cheats) > len(case.off)
    cheats = ["-E", str(d / "mgba.cht")] if picked else []
    m = sh(
        SHOT, str(game), *cheats, "-m", f"{FRAMES}:{RESULTS:#x}:0x80", "-f", str(FRAMES)
    )
    (d / "mgba.txt").write_text(m.stdout + m.stderr)
    # Torii on the emulated cart
    sd = d / "sd.img"
    sh("mkfs.fat", "-C", "-F", "32", "-n", "PROOF", str(sd), "65536", check=True)
    sh("mcopy", "-i", str(sd), str(game), "::/CP" + ext, check=True)
    sh("mmd", "-i", str(sd), "::/torii", "::/torii/cheats", check=True)
    sh("mcopy", "-i", str(sd), str(d / "CP.cht"), "::/torii/cheats/CP.cht", check=True)
    nav = sh(
        "bash",
        "-c",
        '. tests/nav.sh; nav_launch "$1" at=100',
        "_",
        "/CP" + ext,
        check=True,
    ).stdout.split()
    ack = (
        ["-w", "screen ready: dialog", "-k", "@10:A", "-k", "@14:-"]
        if case.dialog
        else []
    )
    t = sh(
        SHOT,
        "dist/ezkernelnew.bin",
        *nav,
        *ack,
        "-w",
        "launch: starting the game",
        "-m",
        f"@{FRAMES}:{xgb_ram:#x}:0x80",
        env={**os.environ, "OMEGA_DIR": str(d)},
    )
    (d / "torii.txt").write_text(t.stdout)
    (d / "torii.log").write_text(t.stderr)
    if t.returncode != 0:
        return [f"Torii's run failed (status {t.returncode})"]
    if "kernel error:" in t.stderr or "crash at frame" in t.stderr:
        fails.append("Torii logged an error or a crash")
    try:
        got = {
            "mgba": recorded(m.stdout, RESULTS, case.gbc),
            "torii": recorded(t.stdout, xgb_ram, case.gbc),
        }
    except AssertionError as e:
        return fails + [str(e)]
    device = predict(case, "device")
    for who in ("mgba", "torii"):
        want = predict(case, who)
        for name, value in want.items():
            if got[who][name] != value:
                fails.append(
                    f"{who}: {name} is {got[who][name]:02X}, wanted {value:02X}"
                )
        if who == "mgba" and (want != device) != bool(case.mgba_differs):
            fails.append(
                "the model's mGBA shortcut and the case's mgba_differs disagree"
            )
    return fails


def main() -> int:
    carts = Path(sys.argv[1])
    only = sys.argv[2:]
    elf = "emulators/out/jagoombacolor.elf"
    xgb_ram = int(
        sh("python3", "tests/elf_sym.py", elf, "XGB_RAM", check=True).stdout, 16
    )
    status = 0
    for case in cases():
        if only and case.name not in only:
            continue
        fails = run(case, carts, xgb_ram)
        notes = "; ".join(x for x in (case.mgba_differs, case.torii_skips) if x)
        if fails:
            status = 1
            print(f"FAIL: gb cheat proof: {case.name}: " + "; ".join(fails[:4]))
        else:
            print(
                f"ok: {case.name}" + (f" ({notes})" if notes else ""), file=sys.stderr
            )
    return status


if __name__ == "__main__":
    sys.exit(main())
