#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# ///
"""Tests for tools/card.py, run by `just test-host`: what a card must look
like, the games a copy takes and leaves, and the steps it runs. No network,
and no step is run."""

import argparse
import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import card


class Card(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.sd = self.root / "sd"
        (self.sd / "torii").mkdir(parents=True)
        self.src = self.root / "src"
        (self.src / "Hacks" / ".hidden").mkdir(parents=True)
        for name in (
            "Game.gba",
            "Hacks/Prism.gbc",
            "Hacks/readme.txt",
            "Hacks/.hidden/x.gba",
            "Hacks/._Prism.gbc",
        ):
            (self.src / name).write_bytes(b"rom " + name.encode())

    def tearDown(self):
        self.tmp.cleanup()

    def test_card_check(self):
        """A folder with neither /torii nor the kernel isn't taken for a card."""
        card.check_card(self.sd)
        with self.assertRaises(card.CardError):
            card.check_card(self.src)
        with self.assertRaises(card.CardError):
            card.check_card(self.root / "missing")
        with self.assertRaises(card.CardError):
            card.check_card(Path("/"), new_card=True)

    def test_copy(self):
        """Games go with their folder; other files, hidden folders and ._
        files don't; a game the card has is left alone, its bytes kept."""
        (self.sd / "Game.gba").write_bytes(b"mine")
        done = card.copy_games(
            self.sd,
            [
                self.src / "Game.gba",
                self.src / "Hacks",
                self.src / "Hacks" / "readme.txt",
            ],
        )
        self.assertEqual(done.copied, ["/Hacks/Prism.gbc"])
        self.assertEqual(done.kept, ["/Game.gba"])
        self.assertEqual(done.skipped, [str(self.src / "Hacks" / "readme.txt")])
        self.assertEqual((self.sd / "Game.gba").read_bytes(), b"mine")
        self.assertEqual(
            (self.sd / "Hacks" / "Prism.gbc").read_bytes(), b"rom Hacks/Prism.gbc"
        )
        self.assertFalse((self.sd / "Hacks" / "readme.txt").exists())
        self.assertFalse((self.sd / "Hacks" / "._Prism.gbc").exists())
        self.assertFalse(any(self.sd.rglob("*.part")))

    def test_copy_to_folder_and_dry_run(self):
        """--to puts games in a folder on the card; a dry run copies nothing."""
        done = card.copy_games(self.sd, [self.src / "Game.gba"], "/GBA/", dry_run=True)
        self.assertEqual(done.copied, ["/GBA/Game.gba"])
        self.assertFalse((self.sd / "GBA").exists())
        card.copy_games(self.sd, [self.src / "Game.gba"], "GBA")
        self.assertTrue((self.sd / "GBA" / "Game.gba").is_file())
        with self.assertRaises(card.CardError):
            card.copy_games(self.sd, [self.src / "nope.gba"])
        with self.assertRaises(card.CardError):
            card.copy_games(self.sd, [self.src / "Game.gba"], "../out")
        self.assertFalse((self.root / "out").exists())

    def plan(self, *argv):
        args = argparse.Namespace(
            offline="--offline" in argv,
            no_art="--no-art" in argv,
            no_cheats="--no-cheats" in argv,
            fetch_cheats="--fetch-cheats" in argv,
        )
        return {
            s.name: (s.argv[3:] if s.argv else s.why) for s in card.steps(self.sd, args)
        }

    def test_steps(self):
        """Every step by default (your art only with pictures to convert);
        --offline reaches the tools that download; the skips say why."""
        sd = str(self.sd)
        plan = self.plan()
        self.assertEqual(
            list(plan), ["names", "your art", "box art", "cheats", "report"]
        )
        self.assertEqual(plan["names"], [str(card.TOOLS / "names_db.py"), "update", sd])
        self.assertIn("no pictures", plan["your art"])
        self.assertEqual(plan["cheats"], [str(card.TOOLS / "cheats.py"), sd])
        (self.sd / card.ART_SOURCE).mkdir(parents=True)
        (self.sd / card.ART_SOURCE / "Game.png").write_bytes(b"png")
        plan = self.plan("--offline", "--fetch-cheats")
        self.assertEqual(plan["your art"], [str(card.TOOLS / "art.py"), "add", sd])
        self.assertEqual(plan["box art"][-1], "--offline")
        self.assertEqual(plan["names"][-1], "--offline")
        self.assertIn("needs the network", plan["cheats"])
        self.assertEqual(self.plan("--fetch-cheats")["cheats"][-1], "--gamehacking")
        plan = self.plan("--no-art", "--no-cheats")
        self.assertEqual(
            (plan["your art"], plan["box art"], plan["cheats"]),
            ("--no-art", "--no-art", "--no-cheats"),
        )

    def test_dry_run_main(self):
        """A dry run lists the copies and steps and leaves the card as it was."""
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(
                card.main([str(self.sd), str(self.src / "Game.gba"), "--dry-run"]), 0
            )
        self.assertIn("would copy: /Game.gba", out.getvalue())
        self.assertIn("names: would run:", out.getvalue())
        self.assertEqual(sorted(p.name for p in self.sd.iterdir()), ["torii"])
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            self.assertEqual(card.main([str(self.src)]), 1)
        self.assertIn("not a Torii card", err.getvalue())


if __name__ == "__main__":
    unittest.main()
