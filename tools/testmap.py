#!/usr/bin/env python3
"""Which test jobs a kernel change touches: `just check` runs only those.

usage: testmap.py fingerprints OBJDIR
       testmap.py record MAP COVDIR [HARNESS]
       testmap.py select MAP [HARNESS]

tests/all.sh runs its jobs with mgba-shot recording the kernel code each one
executes (TORII_COVERAGE_DIR). `record` turns that into the kernel functions
each passing job ran, with each function's fingerprint, in MAP (JSON); a
failed job is dropped from it, so it runs again. `select` prints the jobs to
run now, one per line, and why on stderr: a job not in MAP, one whose
harness (its test script and the files every job reads, hashed by
tools/check.py) changed, or one that ran a function whose fingerprint
changed since it passed.

A fingerprint covers a function's code with what it refers to resolved, so
it changes only when the function's own behaviour may: a call is the callee's
name, not its address, so moving code changes nothing; an address in a
literal pool is the name of the function or variable it points to; constant
data it points to (strings, tables, other data's pointers, recursively)
counts by content. A change to a header or an inlined function changes every
function it compiles into. What a fingerprint can't see is layout itself:
data moving can change code that didn't change (an overflow that now hits
something else). `just check --full` runs every job for that, before merge.

The kernels are ezkernelnew.elf (run as dist/ezkernelnew.bin) and
ezkerneltest.elf (ezkerneltest.gba); mgba-shot names each coverage file after
the CRC-32 of the ROM it ran, which picks the kernel.
"""

import bisect
import hashlib
import json
import re
import struct
import sys
import zlib
from pathlib import Path

KERNELS = {  # name: (ELF, its objects, the ROM files that run it)
    "n": ("ezkernelnew.elf", "build", ["dist/ezkernelnew.bin", "ezkernelnew.gba"]),
    "t": ("ezkerneltest.elf", "build-test", ["ezkerneltest.gba"]),
}
ROM_BASE, ROM_BITS = 0x08000000, 0x100000 // 2
COVER_BYTES = (ROM_BITS + 0x8000 // 2) // 8  # as mgba-shot writes them
IWRAM_BASE = 0x03000000

SHF_ALLOC, SHF_EXEC = 0x2, 0x4
SHT_PROGBITS, SHT_SYMTAB, SHT_NOBITS = 1, 2, 8
SHT_REL = 9
STT_NOTYPE, STT_OBJECT, STT_FUNC, STT_SECTION, STT_FILE = 0, 1, 2, 3, 4
R_ARM_ABS32 = 2


class Elf:
    """The allocated sections and symbols of a 32-bit little-endian ELF."""

    def __init__(self, path: str):
        data = Path(path).read_bytes()
        shoff, shentsize, shnum, shstrndx = (
            struct.unpack_from("<I", data, 0x20)[0],
            *struct.unpack_from("<HHH", data, 0x2E),
        )
        headers = [
            struct.unpack_from("<10I", data, shoff + i * shentsize)
            for i in range(shnum)
        ]
        names = headers[shstrndx]

        def string(table, at):
            start = table[4] + at
            return data[start : data.index(b"\0", start)].decode()

        self.sections = []  # (start, end, name, flags, bytes or None for NOBITS)
        for h in headers:
            name, kind, flags, addr, offset, size = h[:6]
            if flags & SHF_ALLOC and size and kind in (SHT_PROGBITS, SHT_NOBITS):
                body = data[offset : offset + size] if kind == SHT_PROGBITS else None
                self.sections.append(
                    (addr, addr + size, string(names, name), flags, body)
                )
        self.sections.sort()
        self.section_starts = [s[0] for s in self.sections]

        symtab = next(h for h in headers if h[1] == SHT_SYMTAB)
        strtab = headers[symtab[6]]
        self.symbols = []  # (start, size, key, type)
        self.mapping = []  # ($a/$t/$d mapping symbols) (address, kind)
        file = ""
        for at in range(symtab[4], symtab[4] + symtab[5], 16):
            name_at, value, size, info, _, shndx = struct.unpack_from(
                "<IIIBBH", data, at
            )
            kind, local = info & 0xF, info >> 4 == 0
            name = string(strtab, name_at)
            if kind == STT_FILE:
                file = name
                continue
            if not name or shndx == 0 or shndx >= 0xFF00:
                continue
            if name.startswith("$"):
                self.mapping.append((value, name[1]))
                continue
            if kind in (STT_NOTYPE, STT_OBJECT, STT_FUNC):
                key = f"{file}:{name}" if local else name
                self.symbols.append(
                    (
                        value & ~1 if kind == STT_FUNC else value,
                        size,
                        key,
                        kind,
                        value & 1,
                    )
                )
        self.mapping.sort()
        # Sized symbols win; in code, a label without a size (assembly)
        # reaches to the next symbol in its section
        self.symbols.sort(key=lambda s: (s[0], s[1] == 0))
        sized = []
        for i, (start, size, key, kind, thumb) in enumerate(self.symbols):
            if (
                size == 0
                and sized
                and sized[-1][0] <= start < sized[-1][0] + sized[-1][1]
            ):
                continue  # a label inside a sized symbol
            if size == 0:
                section = self.section(start)
                if not section or not section[3] & SHF_EXEC:
                    continue  # data past a label counts by content, as unlabelled data does
                end = section[1]
                for later in self.symbols[i + 1 :]:
                    if later[0] > start:
                        end = min(end, later[0])
                        break
                sized.append((start, end - start, key, kind, thumb))
            elif size:
                sized.append((start, size, key, kind, thumb))
        self.symbols = sized
        self.starts = [s[0] for s in sized]

    def section(self, address: int):
        i = bisect.bisect_right(self.section_starts, address) - 1
        if i >= 0 and address < self.sections[i][1]:
            return self.sections[i]
        return None

    def symbol(self, address: int):
        """The sized symbol holding address (the innermost, the last that starts at or before it)."""
        i = bisect.bisect_right(self.starts, address) - 1
        while i >= 0 and self.starts[i] > address - 0x40000:
            start, size, *_ = self.symbols[i]
            if start <= address < start + size:
                return self.symbols[i]
            i -= 1
        return None

    def read(self, start: int, size: int) -> bytes:
        section = self.section(start)
        if not section or section[4] is None:
            return b""
        return section[4][start - section[0] : start - section[0] + size]

    def mode(self, address: int, default: str) -> str:
        i = bisect.bisect_right(self.mapping, (address, "\xff")) - 1
        return self.mapping[i][1] if i >= 0 else default


class Section:
    def __init__(self, name: str, data: bytes | None, flags: int, size: int):
        self.name, self.data, self.flags, self.size = name, data, flags, size


class Symbol:
    def __init__(self, name, value, size, kind, local, shndx, file):
        self.name, self.value, self.size, self.kind, self.shndx = (
            name,
            value,
            size,
            kind,
            shndx,
        )
        self.key = f"{file}:{name}" if local else name
        self.defined = 0 < shndx < 0xFF00


class ObjFile:
    """A relocatable 32-bit little-endian ELF: sections, symbols, relocations."""

    def __init__(self, path: Path):
        data = path.read_bytes()
        shoff = struct.unpack_from("<I", data, 0x20)[0]
        shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x2E)
        headers = [
            struct.unpack_from("<10I", data, shoff + i * shentsize)
            for i in range(shnum)
        ]

        def string(table, at):
            begin = headers[table][4] + at
            return data[begin : data.index(b"\0", begin)].decode()

        self.sections = [
            Section(
                string(shstrndx, h[0]),
                None if h[1] == SHT_NOBITS else data[h[4] : h[4] + h[5]],
                h[2],
                h[5],
            )
            for h in headers
        ]
        symtab = next(h for h in headers if h[1] == SHT_SYMTAB)
        self.symbols, self.mapping = [], {}
        file = path.name  # as the linker names an assembled file's locals
        for at in range(symtab[4], symtab[4] + symtab[5], 16):
            name_at, value, size, info, _, shndx = struct.unpack_from(
                "<IIIBBH", data, at
            )
            name = string(symtab[6], name_at)
            if info & 0xF == STT_FILE:
                file = name
            if name.startswith("$") and 0 < shndx < 0xFF00:
                self.mapping.setdefault(shndx, []).append((value, name[1]))
            self.symbols.append(
                Symbol(name, value, size, info & 0xF, info >> 4 == 0, shndx, file)
            )
        for marks in self.mapping.values():
            marks.sort()
        # An assembly label reaches to the next label, or its section's end
        labels = sorted(
            (sym.shndx, sym.value & ~1, sym.size == 0, id(sym), sym)
            for sym in self.symbols
            if sym.defined
            and sym.kind in (STT_NOTYPE, STT_OBJECT, STT_FUNC)
            and not sym.name.startswith("$")
        )
        for i, (shndx, start, _, _, sym) in enumerate(labels):
            if sym.size:
                continue
            end = self.sections[shndx].size
            for later in labels[i + 1 :]:
                if later[0] != shndx:
                    break
                if later[1] > start:
                    end = later[1]
                    break
            sym.size = end - start
        self.rels = {}
        for h in headers:
            if h[1] == SHT_REL:
                self.rels[h[7]] = [
                    (offset, info & 0xFF, info >> 8)
                    for offset, info in struct.iter_unpack(
                        "<II", data[h[4] : h[4] + h[5]]
                    )
                ]
        self.sized = {}
        for sym in self.symbols:
            if (
                sym.defined
                and sym.size
                and sym.kind in (STT_FUNC, STT_OBJECT, STT_NOTYPE)
                and not sym.name.startswith("$")
            ):
                self.sized.setdefault(sym.shndx, []).append((sym.value & ~1, sym))
        for found in self.sized.values():
            found.sort(key=lambda pair: pair[0])

    def symbol_at(self, shndx: int, offset: int):
        for start, sym in self.sized.get(shndx, []):
            if start <= offset < start + sym.size:
                return sym
        return None

    def mode(self, shndx: int, offset: int) -> str:
        marks = self.mapping.get(shndx, [])
        i = bisect.bisect_right(marks, (offset, "\xff")) - 1
        return marks[i][1] if i >= 0 else "t"


class Objects:
    """The kernel's object files, whose relocations say which words are references."""

    def __init__(self, folder: str):
        self.defs = {}  # key: (object, symbol)
        for path in sorted(Path(folder).glob("*.o")):
            obj = ObjFile(path)
            for sym in obj.symbols:
                if sym.defined and sym.kind in (STT_FUNC, STT_OBJECT, STT_NOTYPE):
                    self.defs.setdefault(sym.key, (obj, sym))
        self.memo: dict[str, str] = {}

    def all(self) -> dict[str, str]:
        return {
            key: self.fingerprint(obj, sym)
            for key, (obj, sym) in self.defs.items()
            if self.code(obj, sym)
        }

    @staticmethod
    def code(obj, sym) -> bool:
        return sym.kind == STT_FUNC or (
            sym.kind == STT_NOTYPE and obj.sections[sym.shndx].flags & SHF_EXEC != 0
        )

    def target(self, obj, sym, addend: int) -> str:
        """What a relocation points to, in terms that don't move."""
        if sym.kind == STT_SECTION:
            inner = obj.symbol_at(sym.shndx, addend)
            if inner is None:
                if obj.sections[sym.shndx].data is None:
                    return f"b{obj.sections[sym.shndx].name}"
                text = obj.sections[sym.shndx].data[addend : addend + 256]
                text = text[: text.index(b"\0")] if b"\0" in text else text
                return f"s{hashlib.sha1(text).hexdigest()[:16]}"
            return self.target(obj, inner, addend - inner.value)
        found = self.defs.get(sym.key) if sym.defined else self.defs.get(sym.name)
        if found is None:
            return f"x{sym.name}+{addend}"  # the libraries'
        dobj, dsym = found
        if self.code(dobj, dsym):
            return f"f{dsym.key}+{addend & ~1}"
        if dobj.sections[dsym.shndx].data is None:
            return f"b{dsym.key}+{addend}"
        return f"o{dsym.key}+{addend}={self.fingerprint(dobj, dsym)}"

    def fingerprint(self, obj, sym) -> str:
        """sym's bytes with each reference replaced by what it points to (data recursively)."""
        if sym.key in self.memo:
            return self.memo[sym.key]
        self.memo[sym.key] = "cycle"
        section = obj.sections[sym.shndx]
        start = sym.value & ~1
        body = bytearray(section.data[start : start + sym.size])
        refs = []
        for offset, kind, target in obj.rels.get(sym.shndx, []):
            at = offset - start
            if not 0 <= at < len(body):
                continue
            tsym = obj.symbols[target]
            if kind == R_ARM_ABS32:
                addend = struct.unpack_from("<I", body, at)[0]
                body[at : at + 4] = b"\0\0\0\0"
            else:  # calls: the addend is the PC's offset
                addend = 0
                body[at : at + 4] = b"\0\0\0\0"
            refs.append(f"{at}:{kind}:{self.target(obj, tsym, addend)}")
        if self.code(obj, sym):
            # A call within the object, which the assembler resolved
            relocated = {offset - start for offset, _, _ in obj.rels.get(sym.shndx, [])}
            at = 0
            while at + 4 <= len(body):
                here = start + at
                if obj.mode(sym.shndx, here) == "t" and at not in relocated:
                    half, low = struct.unpack_from("<HH", body, at)
                    if half & 0xF800 == 0xF000 and low & 0xF800 == 0xF800:
                        offset = (half & 0x7FF) << 12 | (low & 0x7FF) << 1
                        offset -= (offset & 0x400000) << 1
                        dest = here + 4 + offset
                        inner = obj.symbol_at(sym.shndx, dest)
                        # or a thunk GCC adds unnamed (_call_via_rN: bx, nop)
                        name = (
                            inner.key if inner else section.data[dest : dest + 4].hex()
                        )
                        refs.append(f"{at}:bl:{name}")
                        body[at : at + 4] = b"\0\xf0\0\xf8"
                        at += 4
                        continue
                at += 2
        digest = hashlib.sha1(bytes(body) + "\n".join(refs).encode()).hexdigest()[:16]
        self.memo[sym.key] = digest
        return digest


def kernels() -> dict[str, tuple[Elf, dict[str, str], set[str]]]:
    """Each kernel present: its ELF, fingerprints and the CRC-32s of its ROM files."""
    out = {}
    for name, (elf_path, objects, roms) in KERNELS.items():
        if not Path(elf_path).exists():
            continue
        elf = Elf(elf_path)
        crcs = {
            f"{zlib.crc32(Path(r).read_bytes()):08X}" for r in roms if Path(r).exists()
        }
        out[name] = (elf, Objects(objects).all(), crcs)
    return out


def covered(elf: Elf, bits: bytes) -> set[str]:
    """The functions holding the halfwords a coverage file marks."""
    found = set()
    last = None
    for match in re.finditer(rb"[^\0]", bits):
        byte = match.start()
        for bit in range(8):
            if not bits[byte] >> bit & 1:
                continue
            index = byte * 8 + bit
            if index < ROM_BITS:
                address = ROM_BASE + index * 2
            else:
                address = IWRAM_BASE + (index - ROM_BITS) * 2
            if last and last[0] <= address < last[0] + last[1]:
                continue
            sym = elf.symbol(address)
            if sym:
                last = sym
                found.add(sym[2])
    return found


def load(path: str) -> dict:
    try:
        return json.loads(Path(path).read_text())
    except (OSError, ValueError):
        return {"jobs": {}}


def record(map_path: str, cov_dir: str, harness: dict[str, str]) -> None:
    found = kernels()
    by_crc = {crc: name for name, (_, _, crcs) in found.items() for crc in crcs}
    testmap = load(map_path)
    kept = dropped = 0
    for marker in sorted(Path(cov_dir).rglob("job")):
        job_dir = marker.parent
        job = marker.read_text().strip()
        if harness and job not in harness:
            continue  # a job of jobs: tests/shots.py's scenarios count instead
        status = (
            (job_dir / "status").read_text().strip()
            if (job_dir / "status").exists()
            else "?"
        )
        if status != "0":
            testmap["jobs"].pop(job, None)
            dropped += 1
            continue
        # Each run's file (CRC-XXXXXX), or one per kernel merged (CRC.cov)
        runs: dict[str, int] = {}
        for cov in job_dir.iterdir():
            if cov.is_file() and cov.name not in ("job", "status"):
                crc = cov.name.split("-")[0].split(".")[0]
                runs[crc] = runs.get(crc, 0) | int.from_bytes(
                    cov.read_bytes(), "little"
                )
        ran = {}
        for crc, bits in runs.items():
            kernel = by_crc.get(crc)
            if kernel is None:
                continue
            elf, prints, _ = found[kernel]
            for key in covered(elf, bits.to_bytes(COVER_BYTES, "little")):
                ran[f"{kernel}:{key}"] = prints.get(key, "-")
        testmap["jobs"][job] = {"harness": harness.get(job, ""), "ran": ran}
        kept += 1
    Path(map_path).parent.mkdir(parents=True, exist_ok=True)
    Path(map_path).write_text(json.dumps(testmap, sort_keys=True))
    print(
        f"testmap: recorded {kept} passing job(s), dropped {dropped} failed",
        file=sys.stderr,
    )


def select(map_path: str, jobs: list[str], harness: dict[str, str]) -> list[str]:
    testmap = load(map_path)
    prints = {name: p for name, (_, p, _) in kernels().items()}
    chosen = []
    for job in jobs:
        seen = testmap["jobs"].get(job)
        if seen is None:
            why = "not run yet"
        elif seen["harness"] != harness.get(job, ""):
            why = "its script or the test harness changed"
        else:
            changed = [
                key
                for key, fp in seen["ran"].items()
                if prints.get(key.split(":", 1)[0], {}).get(key.split(":", 1)[1], "-")
                != fp
            ]
            if not changed:
                continue
            names = sorted(k.split(":", 1)[1] for k in changed)
            why = f"ran {len(changed)} changed function(s): {', '.join(names[:4])}" + (
                ", ..." if len(names) > 4 else ""
            )
        chosen.append(job)
        print(f"  {job}: {why}", file=sys.stderr)
    return chosen


def main() -> int:
    args = sys.argv[1:]
    if args[:1] == ["fingerprints"] and len(args) == 2:
        for key, fp in sorted(Objects(args[1]).all().items()):
            print(fp, key)
        return 0
    if args[:1] == ["record"] and len(args) in (3, 4):
        harness = json.loads(Path(args[3]).read_text()) if len(args) == 4 else {}
        record(args[1], args[2], harness)
        return 0
    if args[:1] == ["select"] and len(args) in (2, 3):
        harness = json.loads(Path(args[2]).read_text()) if len(args) == 3 else {}
        jobs = sys.stdin.read().splitlines()
        print("\n".join(select(args[1], jobs, harness)))
        return 0
    sys.exit(__doc__)


if __name__ == "__main__":
    sys.exit(main())
