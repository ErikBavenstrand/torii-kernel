#!/usr/bin/env python3
"""The menu's strings: lang/en.ini to C, and lang/<code>.ini to packs.

usage: lang.py sources [--check]        source/lang_ids.h and source/lang_en.c
       lang.py pack SRC.ini -o OUT.lng  one language pack
       lang.py packs [-o DIR] [--zip Z] every lang/<code>.ini but English, and
                                        the test pseudo-locale (en-XA), to DIR
                                        (default dist/lang); the release's
                                        packs, without en-XA, to the zip Z
                                        (default dist/torii-lang.zip)
       lang.py pseudo -o OUT.ini        the pseudo-locale's text: English with
                                        accented letters and its own formats

lang/en.ini is the source of truth: every string the menu shows has a key
there, and the kernel reads it as S(STR_KEY) (source/lang.h). A pack holds a
translation's strings; one missing from it shows in English.

The .ini format (UTF-8):
  # a comment              a whole line only
  [section]                keys after it are section.key
  key = text               the text to the line's end, its spaces at both
                           ends dropped
  key = "text"             quoted: kept as it is, spaces too
  key = \"\"\"             a block: the lines up to one holding only \"\"\",
  line one                 joined by line breaks (an empty last line ends
  line two                 the text with one)
  \"\"\"
  In every text \\n is a line break, \\t a tab, \\\\ a backslash and \\" a
  quote. {1} to {9} are what the code fills in (lang_fill), in any order the
  language needs; [format]'s texts use {day} and {month}. A key ending .one
  or .other is a count's forms: .one for 1, .other for any other number.

[meta] isn't strings: code (the file's name, as "sv"), name (as the
language calls itself, shown in Settings), and in en.ini launch, the
sections a game's launch reads after the game fills PSRAM (lang.h:
lang_before_launch). Those sections come first in en.ini.

A pack (.lng), little-endian:
  0   "TLNG"
  4   u16 format version (FORMAT), u16 the strings' count
  8   u32 CRC32 of English's keys in order: a pack built against other keys
      is stale, and the kernel takes none of it (Rockbox's version check)
  12  u32 the pack's bytes
  16  u32 CRC32 of the bytes from 20 on
  20  char code[8], char name[24], NUL-padded
  52  u32 font offset, u32 font bytes: 0 until 1.0's script fonts
  60  u32 the string pool's offset (64 + 2 x count)
  64  u16 offsets[count]: each string's place in the pool, 0 for one the
      pack doesn't have (the pool starts with a NUL)
  then the pool: the strings, each ended by a NUL
A pack is at most 64 KB (PACK_MAX): the room it has in PSRAM. Every
placeholder and every .one/.other pair must match English's, checked here.
"""

import io
import re
import sys
import zipfile
import zlib
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ENGLISH = ROOT / "lang" / "en.ini"
IDS_H = ROOT / "source" / "lang_ids.h"
EN_C = ROOT / "source" / "lang_en.c"

FORMAT = 1
MAGIC = b"TLNG"
HEADER = 64
PACK_MAX = 0x10000
# What lang_before_launch copies into the row cache's room (ui_list.c):
# a launch's sections' offsets and strings
LAUNCH_MAX = 0x2000
CODE_BYTES = 8
NAME_BYTES = 24
ENGLISH_ID = 0xE1E1  # Settings' id for English, as older kernels stored it
PSEUDO_CODE = "en-XA"

KEY_RE = re.compile(r"^[a-z0-9_]+(\.[a-z0-9_]+)*$")
SECTION_RE = re.compile(r"^\[([a-z0-9_.]+)\]$")
CODE_RE = re.compile(r"^[a-z]{2,3}(-[A-Za-z0-9]{2,8})?$")
PLACEHOLDER_RE = re.compile(r"\{([1-9]|day|month)\}")
FORMATS = {
    # key: the placeholders it needs
    "format.decimal": set(),
    "format.thousands": set(),
    "format.date": {"day", "month"},
    "format.date_numeric": {"day", "month"},
}


class LangError(Exception):
    pass


@dataclass
class Lang:
    path: Path
    meta: dict[str, str] = field(default_factory=dict)
    keys: list[str] = field(default_factory=list)
    text: dict[str, str] = field(default_factory=dict)
    line: dict[str, int] = field(default_factory=dict)
    sections: list[str] = field(default_factory=list)

    def where(self, key: str) -> str:
        return f"{self.path}:{self.line[key]}"


def unescape(s: str, where: str) -> str:
    out = []
    i = 0
    while i < len(s):
        c = s[i]
        if c == "\\":
            if i + 1 >= len(s):
                raise LangError(f"{where}: a backslash ends the line")
            n = s[i + 1]
            if n not in 'nt\\"':
                raise LangError(f"{where}: unknown escape \\{n}")
            out.append({"n": "\n", "t": "\t", "\\": "\\", '"': '"'}[n])
            i += 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def parse(text: str, path: Path) -> Lang:
    lang = Lang(path)
    lines = text.split("\n")
    section = None
    i = 0
    while i < len(lines):
        raw = lines[i]
        i += 1
        where = f"{path}:{i}"
        stripped = raw.strip()
        if not stripped or stripped.startswith("#"):
            continue
        m = SECTION_RE.match(stripped)
        if m:
            section = m.group(1)
            if section in lang.sections:
                raise LangError(f"{where}: section [{section}] again")
            if section != "meta":
                lang.sections.append(section)
            continue
        if "=" not in raw:
            raise LangError(f"{where}: not a key = text line")
        key, value = raw.split("=", 1)
        key = key.strip()
        value = value.strip()
        if section is None:
            raise LangError(f"{where}: a key before any [section]")
        if not KEY_RE.match(key):
            raise LangError(
                f"{where}: key {key!r}: lower-case letters, digits, _ and . only"
            )
        start = i
        if value == '"""':
            block = []
            while True:
                if i >= len(lines):
                    raise LangError(f'{where}: a """ block with no end')
                if lines[i].strip() == '"""':
                    i += 1
                    break
                block.append(lines[i])
                i += 1
            value = unescape("\n".join(block), where)
        elif len(value) >= 2 and value.startswith('"') and value.endswith('"'):
            value = unescape(value[1:-1], where)
        else:
            value = unescape(value, where)
        if section == "meta":
            lang.meta[key] = value
            continue
        full = f"{section}.{key}"
        if full in lang.text:
            raise LangError(f"{where}: {full} again (first at line {lang.line[full]})")
        if "\0" in value:
            raise LangError(f"{where}: a NUL in the text")
        lang.keys.append(full)
        lang.text[full] = value
        lang.line[full] = start
    for need in ("code", "name"):
        if need not in lang.meta:
            raise LangError(f"{path}: [meta] has no {need}")
    code = lang.meta["code"]
    if not CODE_RE.match(code) or len(code) >= CODE_BYTES:
        raise LangError(
            f"{path}: code {code!r} isn't a language code such as sv or pt-BR"
        )
    if code != path.stem:
        raise LangError(f"{path}: code {code} isn't the file's name")
    if len(lang.meta["name"].encode()) >= NAME_BYTES:
        raise LangError(f"{path}: name is over {NAME_BYTES - 1} bytes")
    check_text(lang)
    return lang


def latin1(s: str) -> bool:
    return all(ord(c) <= 0xFF for c in s)


def check_text(lang: Lang) -> None:
    for key in lang.keys:
        t = lang.text[key]
        if not latin1(t):
            bad = next(c for c in t if ord(c) > 0xFF)
            raise LangError(
                f"{lang.where(key)}: {bad!r} isn't Latin-1, which the fonts draw"
            )
        names = {m.group(1) for m in PLACEHOLDER_RE.finditer(t)}
        named = {n for n in names if not n.isdigit()}
        if named and not key.startswith("format."):
            raise LangError(f"{lang.where(key)}: {{{min(named)}}} is for [format] only")
    for key, need in FORMATS.items():
        if key not in lang.text:
            if lang.meta["code"] == "en":
                raise LangError(f"{lang.path}: [format] has no {key.split('.', 1)[1]}")
            continue
        t = lang.text[key]
        got = placeholders(t)
        if got != need:
            raise LangError(
                f"{lang.where(key)}: needs {sorted(need) or 'no placeholders'}, has {sorted(got)}"
            )
        if key in ("format.decimal", "format.thousands") and len(t) > 1:
            raise LangError(f"{lang.where(key)}: one character at most")
        if key == "format.decimal" and len(t) != 1:
            raise LangError(f"{lang.where(key)}: one character")


def placeholders(t: str) -> set[str]:
    return {m.group(1) for m in PLACEHOLDER_RE.finditer(t)}


def stem(key: str) -> str:
    """A count's key without its .one/.other."""
    for form in (".one", ".other"):
        if key.endswith(form):
            return key[: -len(form)]
    return key


def check_english(en: Lang) -> None:
    for i, key in enumerate(en.keys):
        if key.endswith(".one") and (
            i + 1 >= len(en.keys) or en.keys[i + 1] != stem(key) + ".other"
        ):
            raise LangError(f"{en.where(key)}: .one needs its .other on the next line")
        if key.endswith(".other") and (i == 0 or en.keys[i - 1] != stem(key) + ".one"):
            raise LangError(
                f"{en.where(key)}: .other needs its .one on the line before"
            )
        if stem(key) != key and stem(key) in en.text:
            raise LangError(f"{en.where(key)}: {stem(key)} is a text and a count")
    launch = en.meta.get("launch", "").split()
    for s in launch:
        if s not in en.sections:
            raise LangError(f"{en.path}: launch names [{s}], which isn't a section")
    if en.sections[: len(launch)] != launch:
        raise LangError(
            f"{en.path}: the launch sections ({' '.join(launch)}) come first, in that order"
        )
    ids = [ident(k) for k in en.keys]
    if len(set(ids)) != len(ids):
        raise LangError(f"{en.path}: two keys make one id")


def launch_end(en: Lang) -> int:
    launch = set(en.meta.get("launch", "").split())
    n = 0
    while n < len(en.keys) and en.keys[n].split(".", 1)[0] in launch:
        n += 1
    return n


def ident(key: str) -> str:
    key = key.removesuffix(".one")
    return "STR_" + key.replace(".", "_").upper()


def key_hash(en: Lang) -> int:
    return zlib.crc32("\n".join(en.keys).encode()) & 0xFFFFFFFF


def code_id(code: str) -> int:
    """Settings' id for a pack's language: lang.c's lang_code_id."""
    h = 0x811C9DC5
    for b in code.encode():
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    v = (h ^ (h >> 16)) & 0xFFFF
    if v in (0, 0xFFFF, ENGLISH_ID):
        v ^= 0x5A5A
    return v


def check_pack(lang: Lang, en: Lang) -> list[str]:
    """Errors raise; returns warnings (strings the pack doesn't have)."""
    for key in lang.keys:
        if key not in en.text:
            raise LangError(f"{lang.where(key)}: {key} isn't in English (lang/en.ini)")
        want, got = placeholders(en.text[key]), placeholders(lang.text[key])
        if want != got:
            raise LangError(
                f"{lang.where(key)}: placeholders {sorted(got)}, English has {sorted(want)}"
            )
        if stem(key) != key:
            other = stem(key) + (".other" if key.endswith(".one") else ".one")
            if other not in lang.text:
                raise LangError(f"{lang.where(key)}: {key} without {other}")
    return [k for k in en.keys if k not in lang.text]


def c_string(s: str) -> str:
    out = ['"']
    for b in s.encode():
        c = chr(b)
        if c == "\\":
            out.append("\\\\")
        elif c == '"':
            out.append('\\"')
        elif c == "\n":
            out.append("\\n")
        elif c == "\t":
            out.append("\\t")
        elif 0x20 <= b < 0x7F:
            # A ? before another would make a trigraph
            out.append("\\?" if c == "?" and out[-1].endswith("?") else c)
        else:
            out.append(f"\\{b:03o}")
    out.append('"')
    return "".join(out)


def sources(en: Lang) -> tuple[str, str]:
    end = launch_end(en)
    h = [
        "// Generated by tools/lang.py from lang/en.ini (`just lang`): don't edit.",
        "// The menu's string ids, for S() (lang.h).",
        "#ifndef TORII_LANG_IDS_H",
        "#define TORII_LANG_IDS_H",
        "",
        f"#define LANG_FORMAT {FORMAT}",
        f"#define LANG_KEY_HASH 0x{key_hash(en):08X}u",
        f"#define LANG_PACK_MAX 0x{PACK_MAX:X}u",
        f"#define LANG_LAUNCH_MAX 0x{LAUNCH_MAX:X}u",
        f"#define LANG_ENGLISH_ID 0x{ENGLISH_ID:04X}u",
        "",
        "enum lang_id {",
    ]
    for key in en.keys:
        h.append(f"\t{ident(key)},")
    h += [
        "\tSTR_COUNT",
        "};",
        "",
        "// The ids below this are the launch's (lang_before_launch)",
        f"#define STR_LAUNCH_END {end}",
        "",
        "#endif",
        "",
    ]
    c = [
        "// Generated by tools/lang.py from lang/en.ini (`just lang`): don't edit.",
        "// English, the language every other falls back to (lang.h).",
        '#include "lang.h"',
        "",
        "const char *const lang_en[STR_COUNT] = {",
    ]
    for key in en.keys:
        c.append(f"\t{c_string(en.text[key])},  // {key}")
    c += ["};", ""]
    return "\n".join(h), "\n".join(c)


def le16(v: int) -> bytes:
    return v.to_bytes(2, "little")


def le32(v: int) -> bytes:
    return v.to_bytes(4, "little")


def pool_of(lang: Lang, en: Lang, keys: list[str]) -> tuple[list[int], bytes]:
    pool = bytearray(b"\0")
    offsets = []
    for key in keys:
        if key in lang.text:
            offsets.append(len(pool))
            pool += lang.text[key].encode("utf-8") + b"\0"
        else:
            offsets.append(0)
    return offsets, bytes(pool)


def pack(lang: Lang, en: Lang) -> bytes:
    offsets, pool = pool_of(lang, en, en.keys)
    count = len(en.keys)
    pool_at = HEADER + 2 * count
    body = bytearray()
    body += lang.meta["code"].encode().ljust(CODE_BYTES, b"\0")
    body += lang.meta["name"].encode().ljust(NAME_BYTES, b"\0")
    body += le32(0) + le32(0)  # the font: none until 1.0
    body += le32(pool_at)
    body += b"".join(le16(o) for o in offsets)
    body += pool
    if len(body) % 2:
        body += b"\0"
    size = 20 + len(body)
    if size > PACK_MAX:
        raise LangError(
            f"{lang.path}: the pack is {size} bytes, over {PACK_MAX} (its room in PSRAM)"
        )
    end = launch_end(en)
    _, launch_pool = pool_of(lang, en, en.keys[:end])
    launch = 2 * end + len(launch_pool)
    if launch > LAUNCH_MAX:
        raise LangError(
            f"{lang.path}: the launch's strings take {launch} bytes, over {LAUNCH_MAX}"
        )
    head = MAGIC + le16(FORMAT) + le16(count) + le32(key_hash(en)) + le32(size)
    return head + le32(zlib.crc32(bytes(body)) & 0xFFFFFFFF) + bytes(body)


# The pseudo-locale: every letter that has one in Latin-1 gets an accent,
# and the formats differ from English's, so a screen still in English, or
# a format still hard-coded, shows
ACCENTS = str.maketrans("aeiouycnAEIOUYCN", "áéíóúýçñÁÉÍÓÚÝÇÑ")


def pseudo(en: Lang) -> str:
    out = [
        "# Generated by tools/lang.py pseudo: accented English, to test packs.",
        "[meta]",
        f"code = {PSEUDO_CODE}",
        "name = Éñglísh (pséúdó)",
    ]
    section = None
    for key in en.keys:
        s, k = key.split(".", 1)
        if s != section:
            out.append(f"[{s}]")
            section = s
        t = en.text[key]
        if key == "format.decimal":
            t = ","
        elif key == "format.thousands":
            t = "."
        elif key == "format.date":
            t = "{month} {day}"
        elif key == "format.date_numeric":
            t = "{day}/{month}"
        else:
            # Placeholders as they are
            parts = re.split(r"(\{[^}]*\})", t)
            t = "".join(p if p.startswith("{") else p.translate(ACCENTS) for p in parts)
        esc = (
            t.replace("\\", "\\\\")
            .replace("\n", "\\n")
            .replace("\t", "\\t")
            .replace('"', '\\"')
        )
        out.append(f'{k} = "{esc}"')
    return "\n".join(out) + "\n"


# The release's torii-lang.zip: unzipped onto the card's root, its packs land
# in /torii/lang. Its entries sorted, dated 1980 and stored, so the same packs
# make the same bytes (`just repro`)
ZIP_DATE = (1980, 1, 1, 0, 0, 0)


def release_zip(packs: dict[str, bytes]) -> bytes:
    """packs: each code's pack, the pseudo-locale left out."""
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_STORED) as z:
        for name in ("torii/", "torii/lang/"):
            info = zipfile.ZipInfo(name, ZIP_DATE)
            info.external_attr = (0o40755 << 16) | 0x10
            info.create_system = 3
            z.writestr(info, b"")
        for code in sorted(packs):
            if code == PSEUDO_CODE:
                continue
            info = zipfile.ZipInfo(f"torii/lang/{code}.lng", ZIP_DATE)
            info.external_attr = 0o100644 << 16
            info.create_system = 3
            z.writestr(info, packs[code])
    return buf.getvalue()


def load(path: Path) -> Lang:
    return parse(path.read_text(encoding="utf-8"), path)


def load_english() -> Lang:
    en = load(ENGLISH)
    check_english(en)
    return en


def write_or_check(path: Path, text: str, check: bool) -> bool:
    old = path.read_text(encoding="utf-8") if path.exists() else None
    if old == text:
        return True
    if check:
        print(f"{path.relative_to(ROOT)} is stale: run `just lang`", file=sys.stderr)
        return False
    path.write_text(text, encoding="utf-8")
    return True


def main(argv: list[str]) -> int:
    if not argv:
        print(__doc__, file=sys.stderr)
        return 2
    try:
        en = load_english()
        cmd = argv[0]
        if cmd == "sources":
            check = "--check" in argv[1:]
            h, c = sources(en)
            ok = write_or_check(IDS_H, h, check) & write_or_check(EN_C, c, check)
            return 0 if ok else 1
        if cmd == "pack":
            src = Path(argv[1])
            out = Path(argv[argv.index("-o") + 1])
            lang = load(src)
            for key in check_pack(lang, en):
                print(f"{src}: {key} isn't translated: English shows", file=sys.stderr)
            out.write_bytes(pack(lang, en))
            return 0
        if cmd == "packs":
            out = (
                Path(argv[argv.index("-o") + 1])
                if "-o" in argv
                else ROOT / "dist" / "lang"
            )
            out.mkdir(parents=True, exist_ok=True)
            langs = [
                load(p) for p in sorted((ROOT / "lang").glob("*.ini")) if p.stem != "en"
            ]
            langs.append(parse(pseudo(en), Path(f"{PSEUDO_CODE}.ini")))
            zip_path = (
                Path(argv[argv.index("--zip") + 1])
                if "--zip" in argv
                else ROOT / "dist" / "torii-lang.zip"
            )
            ids = {ENGLISH_ID: "en"}
            packs = {}
            for lang in langs:
                check_pack(lang, en)
                code = lang.meta["code"]
                i = code_id(code)
                if i in ids:
                    raise LangError(f"{lang.path}: its id {i:04X} is {ids[i]}'s too")
                ids[i] = code
                data = packs[code] = pack(lang, en)
                target = out / f"{code}.lng"
                if not target.exists() or target.read_bytes() != data:
                    target.write_bytes(data)
            data = release_zip(packs)
            if not zip_path.exists() or zip_path.read_bytes() != data:
                zip_path.write_bytes(data)
            return 0
        if cmd == "pseudo":
            Path(argv[argv.index("-o") + 1]).write_text(pseudo(en), encoding="utf-8")
            return 0
    except LangError as e:
        print(f"lang.py: {e}", file=sys.stderr)
        return 1
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
