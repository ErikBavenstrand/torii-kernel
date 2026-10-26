#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# ///
"""Tests for tools/cheat_encode.py, run by `just test-host`: the ciphers
against published codes and mGBA's decoding of them, encrypting then
decrypting gives the code back, and each format's lines are laid out as mGBA
reads them. tests/cheat_engine.sh checks every case's codes against mGBA's
and Torii's own decoders too."""

import random
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import cheat_encode as ce


class Ciphers(unittest.TestCase):
    def test_gameshark_published(self):
        # Mario Kart - Super Circuit, "Have 99 Coins" (tests/cheat_codes.toml): 8-bit 0x63 at 0x03003D10
        self.assertEqual(
            ce.tea_encrypt(0x03003D10, 0x63, ce.GSA_SEEDS), (0x8C1A761B, 0x31E2378B)
        )
        self.assertEqual(
            ce.tea_decrypt(0x8C1A761B, 0x31E2378B, ce.GSA_SEEDS), (0x03003D10, 0x63)
        )

    def test_action_replay_published(self):
        # An Action Replay MAX code (dist's real-codes corpus): 16-bit 0xFFFF at 0x030012E5
        self.assertEqual(
            ce.tea_decrypt(0x7FF23BFC, 0x077B26D1, ce.PAR_SEEDS), (0x023012E5, 0xFFFF)
        )
        self.assertEqual(
            ce.tea_encrypt(0x023012E5, 0xFFFF, ce.PAR_SEEDS), (0x7FF23BFC, 0x077B26D1)
        )

    def test_codebreaker_published(self):
        # libretro's encrypted CodeBreaker "92151D12 E4CE+6F043086 E2E4+720DFB2A 24CE",
        # which mGBA decodes to a type 5 and "A241776C C473"
        key = ce.CBKey(0x92151D12, 0xE4CE)
        self.assertEqual(key.decrypt(0x720DFB2A, 0x24CE), (0xA241776C, 0xC473))
        self.assertEqual(key.encrypt(0xA241776C, 0xC473), (0x720DFB2A, 0x24CE))

    def test_round_trips(self):
        rng = random.Random(7)
        seeds = [
            ce.GSA_SEEDS,
            ce.PAR_SEEDS,
            ce.gs_reseed(0x1234, ce.TABLES["gsa1_t1"], ce.TABLES["gsa1_t2"]),
        ]
        for _ in range(200):
            op1, op2 = rng.getrandbits(32), rng.getrandbits(32)
            for s in seeds:
                self.assertEqual(
                    ce.tea_decrypt(*ce.tea_encrypt(op1, op2, s), s), (op1, op2)
                )
        keys = 0
        while keys < 8:
            try:
                key = ce.CBKey(0x90000000 | rng.getrandbits(28), rng.getrandbits(16))
            except ValueError:
                continue
            keys += 1
            for _ in range(50):
                op1, op2 = rng.getrandbits(32), rng.getrandbits(16)
                self.assertEqual(key.decrypt(*key.encrypt(op1, op2)), (op1, op2))

    def test_deadface_reseeds(self):
        code = ce.Code(ce.GSA)
        code += ce.GSA.deadface(0x1234)
        code += ce.GSA.write(2, 0x02030000, 0xBEEF)
        first, second = code.lines
        self.assertEqual(
            ce.tea_decrypt(*(int(x, 16) for x in first.split()), ce.GSA_SEEDS),
            (0xDEADFACE, 0x1234),
        )
        seeds = ce.gs_reseed(0x1234, ce.TABLES["gsa1_t1"], ce.TABLES["gsa1_t2"])
        self.assertEqual(
            ce.tea_decrypt(*(int(x, 16) for x in second.split()), seeds),
            (0x12030000, 0xBEEF),
        )


class Layouts(unittest.TestCase):
    def test_gameshark(self):
        self.assertEqual(ce.GSA.write(1, 0x03004011, 0x77), [(0x03004011, 0x77)])
        self.assertEqual(
            ce.GSA.write(4, 0x02030034, 0xDEADBEEF), [(0x22030034, 0xDEADBEEF)]
        )
        self.assertEqual(ce.GSA.rom_patch(0x0800088C, 0x20A5), [(0x60000446, 0x20A5)])
        self.assertEqual(ce.GSA.button(2, 0x02030024, 0x4242), [(0x82230024, 0x4242)])
        self.assertEqual(
            ce.GSA.if_eq_range(0x02030100, 7, 2), [(0xE0020007, 0x02030100)]
        )
        self.assertEqual(
            ce.GSA.write_list(1, [2, 3, 4]), [(0x30000003, 1), (2, 3), (4, 0)]
        )

    def test_action_replay(self):
        # mGBA's own test vectors (src/gba/test/cheats.c): "02300002 00005678" and a slide
        self.assertEqual(ce.PAR.write(2, 0x03000002, 0x5678), [(0x02300002, 0x5678)])
        self.assertEqual(
            ce.PAR.slide(1, 0x03000000, 1, 2, 2, 1), [(0, 0x80300000), (1, 0x01020002)]
        )
        self.assertEqual(
            ce.PAR.cond("EQ", 1, 0x03000000, 0, "block"), [(0x88300000, 0)]
        )
        self.assertEqual(ce.PAR.else_(), [(0, 0x60000000)])
        self.assertEqual(
            ce.PAR.write(1, 0x02030510, 0xF1, count=8), [(0x00230510, 0x7F1)]
        )
        self.assertEqual(ce.PAR.io(2, 0x48, 0x1B2D), [(0xC7000048, 0x1B2D)])
        self.assertEqual(
            ce.PAR.rom_patch(4, 0x08000894, 0xF00D), [(0, 0x1E00044A), (0xF00D, 0)]
        )

    def test_codebreaker(self):
        self.assertEqual(ce.CB.write16(0x02030022, 0xBEEF), [(0x82030022, 0xBEEF)])
        self.assertEqual(
            ce.CB.slide(0x02030510, 0x1000, 5, 2, 0x0101),
            [(0x42030510, 0x1000), (0x01010005, 2)],
        )
        self.assertEqual(
            ce.CB.super(0x02030580, bytes(range(1, 9))),
            [(0x52030580, 4), (0x01020304, 0x0506), (0x07080000, 0)],
        )
        self.assertEqual(ce.CB.keys(0x0001), [(0xD0000020, 1)])

    def test_text(self):
        code = ce.Code(ce.CB)
        code += ce.CB.write8(0x02030011, 0x5A)
        code += ce.CB.write16(0x02030022, 0xBEEF)
        self.assertEqual(code.text(), "32030011 005A+82030022 BEEF")
        vba = ce.Code(ce.VBA)
        vba += ce.VBA.write(2, 0x02030022, 0xBEEF)
        self.assertEqual(vba.text(), "02030022:BEEF")
        self.assertIn("cheat0_enable = true\n", ce.cht([("x", "02030022:BEEF")]))


if __name__ == "__main__":
    unittest.main(verbosity=0)
