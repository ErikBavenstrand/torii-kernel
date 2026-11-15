#!/usr/bin/env python3
"""Cut the kernel's glyphs beyond Latin-1 from Galmuri's release.

usage: galmuri.py [--zip FILE] [--out FILE]

Galmuri9's and Galmuri11's glyphs in tools/font.py's EXTRA_RANGES (extended Latin, Greek,
Cyrillic, punctuation and symbols), as a BDF cut like the Latin-1 one
(`tools/font.py --subset-bdf`): the glyphs are unchanged, and `just font`
turns them into source/engine/font_extra.c. The source is Galmuri v2.40.4's release
asset, whose SHA-256 is checked before anything is read from it. The zip is
read in this process only (two members, as bytes): nothing in it is
extracted or run.

--zip FILE   a copy of the release asset (default: fetched into
             dist/galmuri-full/Galmuri-v2.40.4.zip when it isn't there)
--out DIR    where to write Galmuri9-extra.bdf and Galmuri11-extra.bdf
             (default assets/fonts/galmuri)

The output depends on the release's bytes alone, so it can be made again to
check the committed files. Galmuri11 Bold has none of these glyphs, so
headings draw Galmuri11's regular ones made bold (tools/font.py --extra-heading).
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
# Each font read and the BDF it is cut to: Galmuri9's for the body line, and
# Galmuri11's (regular: Galmuri11 Bold has none of these) for headings, which
# tools/font.py's --extra-heading smears to bold
MEMBERS = {"Galmuri9.bdf": "Galmuri9-extra.bdf", "Galmuri11.bdf": "Galmuri11-extra.bdf"}
RANGES = font.parse_ranges(font.EXTRA_RANGES)
OUT = ROOT / "assets" / "fonts" / "galmuri"
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


def build(zip_path: Path) -> dict[str, str]:
    """Each member's subset BDF, by the member's name."""
    data = release_bytes(zip_path)
    keep = {cp for a, b in RANGES for cp in range(a, b + 1)}
    with zipfile.ZipFile(io.BytesIO(data)) as z:  # the bytes checked
        return {m: font.subset_bdf(z.read(m).decode("latin-1"), keep) for m in MEMBERS}


def main(argv: list[str]) -> int:
    zip_path, out = CACHE, OUT
    if "--zip" in argv:
        zip_path = Path(argv[argv.index("--zip") + 1])
    if "--out" in argv:
        out = Path(argv[argv.index("--out") + 1])
    for member, text in build(zip_path).items():
        (out / MEMBERS[member]).write_bytes(text.encode("latin-1"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
