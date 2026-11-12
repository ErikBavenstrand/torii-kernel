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
                # The strings (a script font follows them) have 64 KB
                strings = int.from_bytes(pack[52:56], "little") or len(pack)
                self.assertLessEqual(strings, lang.PACK_MAX)

    def test_swedish_is_whole(self):
        # v0.9 ships Swedish: every string translated, none falling back
        en = lang.load_english()
        self.assertEqual(lang.check_pack(lang.load(ROOT / "lang" / "sv.ini"), en), [])

    def test_korean(self):
        """Korean: Galmuri9's Hangul, kana and jamo in the pack, every string
        translated but the launch's, a pack no native speaker has reviewed."""
        en = lang.load_english()
        ko = lang.load(ROOT / "lang" / "ko.ini")
        missing = lang.check_pack(ko, en)
        # The sections a launch (and the in-game menu) shows draw Hangul as boxes
        # (script_drop): the pack leaves them to English, Latin-1 where it has any
        # but [hint]'s (lang_keep_launch keeps those in English for a launch)
        left = set(en.meta["launch"].split()) - {"format", "month", "hint"}
        for key in missing:
            self.assertTrue(
                key.split(".", 1)[0] in left
                or key == "diag.title"
                or key.startswith("boot."),
                key,
            )
        # [hint]'s other labels are the menu's only, so they may be Hangul
        menu_hints = {
            "hint.open", "hint.turn", "hint.move", "hint.back", "hint.change",
            "hint.done", "hint.field", "hint.set", "hint.menu", "hint.format", "hint.cancel",
        }  # fmt: skip
        launch = set(en.meta["launch"].split())
        for key in ko.keys:
            if key.split(".", 1)[0] in launch and key not in menu_hints:
                self.assertTrue(lang.latin1(ko.text[key]), key)
        # The font: 11,172 syllables, the compatibility jamo and kana, in the room
        data = lang.pack(ko, en)
        at = int.from_bytes(data[52:56], "little")
        size = int.from_bytes(data[56:60], "little")
        self.assertEqual(len(data), at + size)
        self.assertLessEqual(size, lang.font_tool().SCRIPT_MAX)
        self.assertEqual(data[at : at + 4], b"TSCF")
        self.assertEqual(
            int.from_bytes(data[at + 6 : at + 8], "little"), 1
        )  # the body's only
        cps = lang.script_codepoints(ko)
        self.assertTrue(set(range(0xAC00, 0xD7A4)) <= cps)
        self.assertEqual(ko.meta["review"], "pending")

    def test_pending_packs_stay_out_of_the_release(self):
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            out, z = Path(tmp) / "lang", Path(tmp) / "t.zip"
            self.assertEqual(lang.main(["packs", "-o", str(out), "--zip", str(z)]), 0)
            self.assertTrue((out / "ko.lng").exists())  # the card gets it
            with zipfile.ZipFile(z) as zf:
                names = zf.namelist()
        self.assertIn("sv.lng", names)
        self.assertNotIn("ko.lng", names)
        self.assertNotIn("en-XA.lng", names)

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
        self.assertEqual((u32(52), u32(56)), (0, 0))  # no script font
        self.assertEqual(data[64:80].rstrip(b"\0").decode(), lang.release())
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

    def test_script_font(self):
        """A pack with a font: its strings first, then the blob, which the
        header places and checks with a CRC of its own."""
        en = english()
        body = "font = assets/fonts/test/script-test-body.bdf\n"
        heading = "font_heading = assets/fonts/test/script-test-heading.bdf\n"
        text = "[item]\nrecent = \u0391\u0392\u0393 \u3042\n"
        data = lang.pack(
            translation(body + heading + "font_ranges = 0391-03C9, 3041-3096\n" + text),
            en,
        )
        u32 = lambda at: int.from_bytes(data[at : at + 4], "little")
        at, size = u32(52), u32(56)
        self.assertGreater(size, 0)
        self.assertEqual(at % 4, 0)
        self.assertEqual(u32(12), len(data))
        self.assertEqual(at + size, len(data))
        # The strings' CRC covers the strings only, and their pool ends before the font
        self.assertEqual(u32(16), zlib.crc32(data[20:at]))
        blob = data[at:]
        self.assertEqual(blob[:4], b"TSCF")
        self.assertEqual(int.from_bytes(blob[8:12], "little"), zlib.crc32(blob[12:]))
        self.assertEqual(int.from_bytes(blob[12:16], "little"), len(blob))
        self.assertEqual(int.from_bytes(blob[6:8], "little"), 2)  # body and heading
        # Its text carries the characters the font draws, in UTF-8
        self.assertIn("\u0391\u0392\u0393 \u3042".encode(), data[:at])
        # A character the font's ranges leave out, or no font has, is refused
        with self.assertRaisesRegex(lang.LangError, "script font"):
            translation(body + "font_ranges = 0391-03C9\n" + text)
        with self.assertRaisesRegex(lang.LangError, "isn't Latin-1"):
            translation(text)
        with self.assertRaisesRegex(lang.LangError, "font_ranges"):
            lang.pack(translation(body + "font_ranges = 00A0-00FF\n"), en)
        # One with no heading font borrows the body font's: one record
        data = lang.pack(translation(body + "[item]\nrecent = x\n"), en)
        blob = data[int.from_bytes(data[52:56], "little") :]
        self.assertEqual(int.from_bytes(blob[6:8], "little"), 1)
        self.assertEqual(int.from_bytes(blob[20:24], "little"), 0)

    def test_pseudo_locale_has_the_test_font(self):
        en = lang.load_english()
        data = lang.pack(parse(lang.pseudo(en), lang.PSEUDO_CODE), en)
        self.assertGreater(int.from_bytes(data[56:60], "little"), 0)

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

    def test_stale_placeholders(self):
        # The same keys filling in other values: a pack built before is stale
        en = english()
        swapped = parse(
            ENGLISH.replace("both = {1} of {2}", "both = {1} of {2} in {3}")
        )
        self.assertNotEqual(lang.key_hash(en), lang.key_hash(swapped))
        same = parse(ENGLISH.replace("both = {1} of {2}", "both = {2}: {1}"))
        self.assertEqual(lang.key_hash(en), lang.key_hash(same))

    def test_release_zip(self):
        # The packs alone, no folders; the same packs make the same bytes
        packs = {"sv": b"sv pack", lang.PSEUDO_CODE: b"pseudo", "de": b"de pack"}
        data = lang.release_zip(packs)
        self.assertEqual(data, lang.release_zip(dict(reversed(packs.items()))))
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            self.assertEqual(z.namelist(), ["de.lng", "sv.lng"])
            self.assertEqual(z.read("sv.lng"), b"sv pack")
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
