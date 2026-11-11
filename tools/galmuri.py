#!/usr/bin/env python3
"""Cut the script glyphs of Korean's pack (lang/ko.ini) from Galmuri's release.

usage: galmuri.py [--zip FILE] [--out FILE]

Galmuri9's Hangul syllables (U+AC00-U+D7A3), compatibility jamo
(U+3131-U+318E) and kana (U+3041-U+30FF), as a BDF cut like the Latin-1 one
(`tools/font.py --subset-bdf`): the glyphs are unchanged, so lang/ko.ini's
`font` draws what Galmuri draws. The source is Galmuri v2.40.4's release
asset, whose SHA-256 is checked before anything is read from it. The zip is
read in this process only (three members, as bytes): nothing in it is
extracted or run.

--zip FILE   a copy of the release asset (default: fetched into
             dist/galmuri-full/Galmuri-v2.40.4.zip when it isn't there)
--out FILE   the BDF to write (default assets/fonts/galmuri/Galmuri9-hangul.bdf)

The output depends on the release's bytes alone, so it can be made again to
check the committed file. Galmuri11-Bold has Hangul but no kana, and a
syllable set for both fonts would take over 320 KB, so the pack uses
Galmuri9's glyphs for both lines (tools/lang.py's `font`).
"""

import hashlib
import io
import sys
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
import font

VERSION = "v2.40.4"
ASSET = f"Galmuri-{VERSION}.zip"
URL = f"https://github.com/quiple/galmuri/releases/download/{VERSION}/{ASSET}"
SHA256 = "c8b3d9861a62ae73c8b1178091401cd79994812437ef386413f6dd54856e60e7"
MEMBER = "Galmuri9.bdf"
# The code points the pack draws beyond Latin-1
RANGES = [(0x3041, 0x30FF), (0x3131, 0x318E), (0xAC00, 0xD7A3)]
OUT = ROOT / "assets" / "fonts" / "galmuri" / "Galmuri9-hangul.bdf"
CACHE = ROOT / "dist" / "galmuri-full" / ASSET


def release_bytes(path: Path) -> bytes:
    if not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        with urllib.request.urlopen(URL) as r:
            path.write_bytes(r.read())
    data = path.read_bytes()
    got = hashlib.sha256(data).hexdigest()
    if got != SHA256:
        raise SystemExit(f"galmuri.py: {path} has SHA-256 {got}, not {SHA256}")
    return data


def build(zip_path: Path) -> str:
    data = release_bytes(zip_path)
    with zipfile.ZipFile(io.BytesIO(data)) as z:  # the bytes checked
        text = z.read(MEMBER).decode("latin-1")
    keep = {cp for a, b in RANGES for cp in range(a, b + 1)}
    return font.subset_bdf(text, keep)


def main(argv: list[str]) -> int:
    zip_path, out = CACHE, OUT
    if "--zip" in argv:
        zip_path = Path(argv[argv.index("--zip") + 1])
    if "--out" in argv:
        out = Path(argv[argv.index("--out") + 1])
    out.write_bytes(build(zip_path).encode("latin-1"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
