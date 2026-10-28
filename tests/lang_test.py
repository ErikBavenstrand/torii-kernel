#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# ///
"""Tests for tools/lang.py, run by `just test-host`: the committed
source/lang_ids.h and source/lang_en.c match lang/en.ini; the .ini parser;
the checks a translation must pass against English; and the packs it
builds (their header, offsets and limits)."""

import contextlib
import io
import sys
import unittest
import zipfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import lang

ENGLISH = """\
[meta]
code = en
name = English
launch = format ingame

[format]
decimal = .
thousands = ,
date = {day} {month}
date_numeric = {month}-{day}

[ingame]
resume = Resume
count.one = 1 shot
count.other = {1} shots

[item]
recent = Recent
both = {1} of {2}
spaced = "  kept  "
block = \"\"\"
# Head
A\\tline

\"\"\"
"""


def parse(text: str, name: str = "en") -> lang.Lang:
    return lang.parse(text, Path(f"{name}.ini"))


def english() -> lang.Lang:
    en = parse(ENGLISH)
    lang.check_english(en)
    return en


def translation(body: str, code: str = "sv") -> lang.Lang:
    return parse(f"[meta]\ncode = {code}\nname = Svenska\n{body}", code)


class Committed(unittest.TestCase):
    def test_sources_match_english(self):
        en = lang.load_english()
        h, c = lang.sources(en)
        self.assertEqual(lang.IDS_H.read_text(encoding="utf-8"), h, "run `just lang`")
        self.assertEqual(lang.EN_C.read_text(encoding="utf-8"), c, "run `just lang`")

    def test_check_finds_stale(self):
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(lang.main(["sources", "--check"]), 0)

    def test_every_translation_builds(self):
        en = lang.load_english()
        for path in sorted((ROOT / "lang").glob("*.ini")):
            if path.stem == "en":
                continue
            with self.subTest(path.name):
                pack = lang.pack(lang.load(path), en)
                self.assertLessEqual(len(pack), lang.PACK_MAX)

    def test_swedish_is_whole(self):
        # v0.9 ships Swedish: every string translated, none falling back
        en = lang.load_english()
        self.assertEqual(lang.check_pack(lang.load(ROOT / "lang" / "sv.ini"), en), [])

    def test_pseudo_locale_builds(self):
        en = lang.load_english()
        pseudo = parse(lang.pseudo(en), lang.PSEUDO_CODE)
        self.assertEqual(lang.check_pack(pseudo, en), [])
        self.assertLessEqual(len(lang.pack(pseudo, en)), lang.PACK_MAX)


class Parser(unittest.TestCase):
    def test_values(self):
        en = english()
        self.assertEqual(en.text["ingame.resume"], "Resume")
        self.assertEqual(en.text["item.spaced"], "  kept  ")
        self.assertEqual(en.text["item.block"], "# Head\nA\tline\n")
        self.assertEqual(en.keys[0], "format.decimal")
        self.assertEqual(en.meta["name"], "English")

    def test_escapes(self):
        t = parse(
            "[meta]\ncode = en\nname = E\n[format]\ndecimal = .\nthousands = ,\n"
            'date = {day} {month}\ndate_numeric = {day}\\\\{month}\n[x]\na = 1\\n2 \\" \\\\'
        )
        self.assertEqual(t.text["x.a"], '1\n2 " \\')

    def test_errors(self):
        cases = {
            "not a key = text": "[x]\njust words\n",
            "unknown escape": "[x]\na = \\q\n",
            "again": "[x]\na = 1\na = 2\n",
            "lower-case": "[x]\nA = 1\n",
            "no end": '[x]\na = """\nopen\n',
            "Latin-1": "[x]\na = \u4e00\n",
            "for \\[format\\] only": "[x]\na = {day}\n",
        }
        for why, body in cases.items():
            with self.subTest(why), self.assertRaisesRegex(lang.LangError, why):
                translation(body)

    def test_meta(self):
        with self.assertRaisesRegex(lang.LangError, "key before any"):
            parse("a = b\n[meta]\ncode = en\nname = E\n")
        with self.assertRaisesRegex(lang.LangError, "no name"):
            parse("[meta]\ncode = sv\n", "sv")
        with self.assertRaisesRegex(lang.LangError, "isn't the file's name"):
            parse("[meta]\ncode = sv\nname = S\n", "da")
        with self.assertRaisesRegex(lang.LangError, "language code"):
            parse("[meta]\ncode = Swedish\nname = S\n", "Swedish")

    def test_formats(self):
        with self.assertRaisesRegex(lang.LangError, "needs"):
            translation("[format]\ndate = {day}\n")
        with self.assertRaisesRegex(lang.LangError, "one character"):
            translation("[format]\ndecimal = ,,\n")
        self.assertEqual(
            translation("[format]\nthousands =\n").text["format.thousands"], ""
        )

    def test_english_checks(self):
        with self.assertRaisesRegex(lang.LangError, "come first"):
            lang.check_english(
                parse(ENGLISH.replace("launch = format ingame", "launch = item"))
            )
        with self.assertRaisesRegex(lang.LangError, "needs its .other"):
            lang.check_english(parse(ENGLISH.replace("count.other", "counted")))


class Translation(unittest.TestCase):
    def test_missing_falls_back(self):
        en = english()
        missing = lang.check_pack(translation("[item]\nrecent = Senaste\n"), en)
        self.assertIn("ingame.resume", missing)
        self.assertNotIn("item.recent", missing)

    def test_placeholders_match(self):
        en = english()
        lang.check_pack(translation("[item]\nboth = {2} av {1}\n"), en)  # any order
        for body in (
            "[item]\nboth = {1} av\n",
            "[item]\nrecent = {1}\n",
            "[item]\nboth = {1} {2} {3}\n",
        ):
            with (
                self.subTest(body),
                self.assertRaisesRegex(lang.LangError, "placeholders"),
            ):
                lang.check_pack(translation(body), en)

    def test_count_forms(self):
        en = english()
        lang.check_pack(
            translation("[ingame]\ncount.one = 1 bild\ncount.other = {1} bilder\n"), en
        )
        with self.assertRaisesRegex(lang.LangError, "without"):
            lang.check_pack(translation("[ingame]\ncount.other = {1} bilder\n"), en)

    def test_unknown_key(self):
        with self.assertRaisesRegex(lang.LangError, "isn't in English"):
            lang.check_pack(translation("[item]\nnew = Ny\n"), english())


class Pack(unittest.TestCase):
    def test_layout(self):
        en = english()
        sv = translation("[item]\nrecent = Senaste\n[ingame]\nresume = Fortsätt\n")
        data = lang.pack(sv, en)
        self.assertEqual(data[:4], b"TLNG")
        u16 = lambda at: int.from_bytes(data[at : at + 2], "little")
        u32 = lambda at: int.from_bytes(data[at : at + 4], "little")
        self.assertEqual(u16(4), lang.FORMAT)
        self.assertEqual(u16(6), len(en.keys))
        self.assertEqual(u32(8), lang.key_hash(en))
        self.assertEqual(u32(12), len(data))
        self.assertEqual(u32(16), zlib.crc32(data[20:]))
        self.assertEqual(data[20:28].rstrip(b"\0"), b"sv")
        self.assertEqual(data[28:52].rstrip(b"\0"), b"Svenska")
        self.assertEqual((u32(52), u32(56)), (0, 0))  # no font until 1.0
        pool = u32(60)
        self.assertEqual(pool, lang.HEADER + 2 * len(en.keys))
        self.assertEqual(data[pool], 0)

        def text(key: str) -> bytes | None:
            at = u16(lang.HEADER + 2 * en.keys.index(key))
            return data[pool + at : data.index(b"\0", pool + at)] if at else None

        self.assertEqual(text("item.recent"), b"Senaste")
        self.assertEqual(text("ingame.resume"), "Fortsätt".encode())
        self.assertIsNone(text("item.both"))
        self.assertEqual(len(data) % 2, 0)

    def test_too_big(self):
        en = english()
        big = translation('[item]\nblock = """\n' + ("x" * 200 + "\n") * 400 + '"""\n')
        with self.assertRaisesRegex(lang.LangError, "over 65536"):
            lang.pack(big, en)

    def test_launch_room(self):
        en = english()
        big = translation(
            '[ingame]\nresume = """\n' + ("x" * 200 + "\n") * 50 + '"""\n'
        )
        with self.assertRaisesRegex(lang.LangError, "launch"):
            lang.pack(big, en)

    def test_stale_key_set(self):
        en = english()
        more = parse(ENGLISH + "new = New\n")
        self.assertNotEqual(lang.key_hash(en), lang.key_hash(more))

    def test_release_zip(self):
        # Unzips onto the card's root; the same packs make the same bytes
        packs = {"sv": b"sv pack", lang.PSEUDO_CODE: b"pseudo", "de": b"de pack"}
        data = lang.release_zip(packs)
        self.assertEqual(data, lang.release_zip(dict(reversed(packs.items()))))
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            self.assertEqual(
                z.namelist(),
                ["torii/", "torii/lang/", "torii/lang/de.lng", "torii/lang/sv.lng"],
            )
            self.assertEqual(z.read("torii/lang/sv.lng"), b"sv pack")
            for info in z.infolist():
                self.assertEqual(info.date_time, lang.ZIP_DATE)

    def test_code_ids(self):
        # source/lang.c's lang_code_id gives the same (tests/host/test_lang.c)
        self.assertEqual(lang.code_id("sv"), 0x564E)
        self.assertEqual(lang.code_id("en-XA"), 0x53C4)
        for code in ("sv", "en-XA", "de", "pt-BR"):
            self.assertNotIn(lang.code_id(code), (0, 0xFFFF, lang.ENGLISH_ID))


if __name__ == "__main__":
    stream = io.StringIO()
    result = unittest.TextTestRunner(stream=stream).run(
        unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    )
    if result.wasSuccessful():
        print(f"PASS: language strings ({result.testsRun} tests)")
    else:
        print(stream.getvalue(), end="")
        print("FAIL: language strings")
        sys.exit(1)
