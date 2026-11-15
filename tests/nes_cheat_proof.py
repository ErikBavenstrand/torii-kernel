#!/usr/bin/env python3
"""The proof that Torii's NES cheats run as a cheat device does (tests/nes_cheat_proof.sh).

mGBA has no NES core, so the reference is a model of the device on the bus
(`device`): a Game Genie code changes a read at its CPU address while the byte
there (in whichever bank is mapped) equals its compare, and a raw `AAAA:VV`
code writes its byte each frame (RAM mirrors fold to 0000-07FF). Each case's
cheat file runs on the proof's ROM (tests/nes_cheat_rom.py: NROM, UxROM, MMC1
or MMC3) through Torii on the emulated cart (PocketNES, the file at
/torii/cheats/NP.cht, every cheat picked); the ROM records what it reads into
the NES's RAM, which the test reads inside PocketNES (NES_RAM). Torii patches
the game's copy once at launch rather than the bus, by the mapper's windows
(cheatemu.c's nes_banking); `torii`, a model of that, must match the run byte
for byte, and the device's model too, apart from the cases a static patch
can't do as the bus (`torii_differs`).

usage: nes_cheat_proof.py CARTS [CASE...]   (run inside the torii-emu container)
"""

import os
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from emu_cheat_codes import nes_genie
from nes_cheat_rom import (
    CAP,
    RESULTS,
    TWIN,
    WATCHED,
    bank_size,
    layout,
    prg_size,
    reads,
    rom,
    unique,
)

SHOT = "emu/build-linux/mgba-shot"
FRAMES = 300


@dataclass
class Case:
    name: str
    mapper: str
    cheats: list[str]
    torii_differs: str = ""
    dialog: bool = False


def cases() -> list[Case]:
    return [
        Case("nrom 6 letters, 8000", "nrom", [nes_genie(0x8010, 0xC3)]),
        Case("nrom 8 letters, C000", "nrom", [nes_genie(0xC011, 0xA7, TWIN)]),
        Case(
            "nrom 8 letters, wrong compare",
            "nrom",
            [nes_genie(0x8010, 0xC3, 0x01)],
            dialog=True,
        ),
        Case(
            "uxrom 6 letters, the switchable banks", "uxrom", [nes_genie(0x8012, 0x66)]
        ),
        Case(
            "uxrom 8 letters, two banks of the compare",
            "uxrom",
            [nes_genie(0x8011, 0xA7, TWIN)],
        ),
        Case(
            "uxrom 8 letters, one bank", "uxrom", [nes_genie(0x8010, 0xC3, unique(4))]
        ),
        Case("uxrom 6 letters, the fixed bank", "uxrom", [nes_genie(0xC012, 0x67)]),
        Case("mmc1 8 letters, one bank", "mmc1", [nes_genie(0x8010, 0xC3, unique(5))]),
        Case("mmc1 6 letters, the fixed bank", "mmc1", [nes_genie(0xC010, 0x68)]),
        Case("mmc1 6 letters, the switchable banks", "mmc1", [nes_genie(0x8012, 0x69)]),
        Case(
            "mmc3 8 letters, one bank",
            "mmc3",
            [nes_genie(0x8010, 0xC3, unique(9))],
            torii_differs="the patch is in the bank's bytes, so the bank shows it in A000-BFFF too",
        ),
        Case(
            "mmc3 6 letters, 8000-9FFF",
            "mmc3",
            [nes_genie(0x8012, 0x6A)],
            torii_differs="the patch is in the bank's bytes, so a bank shows it in A000-BFFF too",
        ),
        Case(
            "mmc3 8 letters, A000-BFFF",
            "mmc3",
            [nes_genie(0xA011, 0xA8, TWIN)],
            torii_differs="the patch is in the bank's bytes, so a bank shows it in 8000-9FFF too",
        ),
        Case(
            "mmc3 6 letters, C000 (second to last bank)",
            "mmc3",
            [nes_genie(0xC010, 0x6B)],
        ),
        Case("mmc3 6 letters, E000 (last bank)", "mmc3", [nes_genie(0xE012, 0x6C)]),
        Case("raw poke to RAM", "nrom", ["0300:55"]),
        Case("raw poke through a mirror", "nrom", ["0B80:66"]),
        Case(
            "raw pokes, several lines and cheats",
            "uxrom",
            ["07F0:77+0300:55", "0380:12"],
        ),
        Case("raw poke to PRG-RAM", "mmc3", ["6000:88"]),
        Case("raw poke to the ROM can't run", "nrom", ["8010:55"], dialog=True),
        Case(
            "genie and raw together",
            "mmc1",
            [nes_genie(0x8010, 0xC3, unique(2)), "0300:44", nes_genie(0xC012, 0x6D)],
        ),
    ]


def decode(code: str) -> list[tuple]:
    """('genie', addr, value, compare|None) or ('raw', addr, value) per line."""
    letters = "APZLGITYEOXUKSVN"
    out = []
    for line in code.split("+"):
        if ":" in line:
            a, v = line.split(":")
            out.append(("raw", int(a, 16), int(v, 16)))
            continue
        n = [letters.index(c) for c in line]
        addr = 0x8000 | (n[3] & 7) << 12 | (n[5] & 7) << 8 | (n[4] & 8) << 8
        addr |= (n[2] & 7) << 4 | (n[1] & 8) << 4 | (n[4] & 7) | (n[3] & 8)
        if len(n) == 6:
            value = (n[1] & 7) << 4 | (n[0] & 8) << 4 | (n[0] & 7) | (n[5] & 8)
            out.append(("genie", addr, value, None))
        else:
            value = (n[1] & 7) << 4 | (n[0] & 8) << 4 | (n[0] & 7) | (n[7] & 8)
            compare = (n[7] & 7) << 4 | (n[6] & 8) << 4 | (n[6] & 7) | (n[5] & 8)
            out.append(("genie", addr, value, compare))
    return out


def torii_places(mapper: str, addr: int) -> list[int]:
    """PRG offsets a code for addr patches, as cheatemu.c's nes_banking does it."""
    a, prg = addr - 0x8000, prg_size(mapper)
    if mapper == "nrom":
        return [a % prg]
    if mapper in ("uxrom", "mmc1"):
        if a >= 0x4000:
            return [prg - 0x4000 + (a & 0x3FFF)]
        return [b * 0x4000 + a for b in range(prg // 0x4000 - 1)]
    n8 = prg // 0x2000
    if a >= 0x6000:
        return [prg - 0x2000 + (a & 0x1FFF)]
    if a >= 0x4000:
        return [(n8 - 2) * 0x2000 + (a & 0x1FFF)]
    return [b * 0x2000 + (a & 0x1FFF) for b in range(n8 - 2)]


def predict(case: Case, who: str) -> dict[str, int]:
    """The results block by name: who is device (the bus) or torii (the patched copy)."""
    image = bytearray(rom(case.mapper)[16 : 16 + prg_size(case.mapper)])
    original = bytes(image)
    lines = [x for code in case.cheats for x in decode(code)]
    genies = [x for x in lines if x[0] == "genie"]
    if who == "torii":
        for _, addr, value, compare in genies:
            for at in torii_places(case.mapper, addr):
                sampled = at < 0x4000 and (at & 127) < 4  # the emulator's checksum's
                if (compare is None or image[at] == compare) and not sampled:
                    image[at] = value
    want = {"ready": 0x42}
    for cpu, off, _ in reads(case.mapper):
        v = image[off]
        if who == "device":
            for _, addr, value, compare in genies:
                if addr == cpu and (compare is None or original[off] == compare):
                    v = value
        want[f"rom {cpu:04X} bank {off // bank_size(case.mapper)}"] = v
    ram = {}
    for x in lines:
        if x[0] == "raw":
            a = x[1] & 0x7FF if x[1] < 0x2000 else x[1]
            if a < 0x800 or 0x6000 <= a < 0x8000:
                ram[a] = x[2]
    for addr in WATCHED:
        want[f"ram {addr:04X} value"] = ram.get(addr, 0)
        want[f"ram {addr:04X} frames"] = CAP if addr in ram else 0
    return want


def cht(case: Case) -> str:
    out = [f"cheats = {len(case.cheats)}", ""]
    for i, code in enumerate(case.cheats):
        out += [
            f'cheat{i}_desc = "Cheat {i}"',
            f'cheat{i}_code = "{code}"',
            f"cheat{i}_enable = true",
            "",
        ]
    return "\n".join(out)


def sh(
    *args: str, check: bool = False, env: dict | None = None
) -> subprocess.CompletedProcess:
    return subprocess.run(args, capture_output=True, text=True, check=check, env=env)


def run(case: Case, carts: Path, nes_ram: int) -> list[str]:
    d = carts / case.name.replace(" ", "-").replace(",", "").replace("(", "").replace(
        ")", ""
    )
    d.mkdir(parents=True)
    game = d / "NP.nes"
    game.write_bytes(rom(case.mapper))
    (d / "NP.cht").write_text(cht(case))
    sd = d / "sd.img"
    sh("mkfs.fat", "-C", "-F", "32", "-n", "PROOF", str(sd), "65536", check=True)
    sh("mcopy", "-i", str(sd), str(game), "::/NP.nes", check=True)
    sh("mmd", "-i", str(sd), "::/torii", "::/torii/cheats", check=True)
    sh("mcopy", "-i", str(sd), str(d / "NP.cht"), "::/torii/cheats/NP.cht", check=True)
    nav = sh(
        "bash",
        "-c",
        '. tests/nav.sh; nav_launch "$1" at=100',
        "_",
        "/NP.nes",
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
        f"@{FRAMES}:{nes_ram + RESULTS:#x}:0x100",
        env={**os.environ, "OMEGA_DIR": str(d)},
    )
    (d / "torii.txt").write_text(t.stdout)
    (d / "torii.log").write_text(t.stderr)
    if t.returncode != 0:
        return [f"Torii's run failed (status {t.returncode})"]
    fails = []
    if "kernel error:" in t.stderr or "crash at frame" in t.stderr:
        fails.append("Torii logged an error or a crash")
    data = bytearray()
    for line in t.stdout.splitlines():
        if line.startswith("mem frame "):
            data += bytes(int(x, 16) for x in line.split(":", 1)[1].split())
    if len(data) < 0x100:
        return fails + ["no results block"]
    got = {name: data[off] for name, off in layout(case.mapper).items()}
    want, device = predict(case, "torii"), predict(case, "device")
    for name, value in want.items():
        if got[name] != value:
            fails.append(f"{name} is {got[name]:02X}, wanted {value:02X}")
    if (want != device) != bool(case.torii_differs):
        diff = [n for n in want if want[n] != device[n]]
        fails.append(
            f"Torii's static patch and the bus disagree unexpectedly: {diff[:4]}"
        )
    return fails


def main() -> int:
    carts = Path(sys.argv[1])
    only = sys.argv[2:]
    nes_ram = int(
        sh(
            "python3",
            "tests/elf_sym.py",
            "emulators/out/pocketnes.elf",
            "NES_RAM",
            check=True,
        ).stdout,
        16,
    )
    status = 0
    for case in cases():
        if only and case.name not in only:
            continue
        fails = run(case, carts, nes_ram)
        if fails:
            status = 1
            print(f"FAIL: nes cheat proof: {case.name}: " + "; ".join(fails[:4]))
        else:
            print(
                f"ok: {case.name}"
                + (f" ({case.torii_differs})" if case.torii_differs else ""),
                file=sys.stderr,
            )
    return status


if __name__ == "__main__":
    sys.exit(main())
