### Which consoles can Torii run through GBA emulators, and which are worth it?

> **Answer:** GB, GBC and NES are the only systems with mature, fast emulators and big, famous libraries. Master System/Game Gear (S8GBA) is the one other system that is solid and actively maintained. PC Engine, Neo Geo Pocket, WonderSwan, Supervision, Lynx, Coleco and MSX run but with real limits (speed, saves, mostly Japanese libraries). SNES only works through per-game ROM builders, and there is no Mega Drive emulator. None of the emulators states a licence, so Torii can't safely ship any third-party emulator binary.
> **Confidence:** 🟡 medium. Versions, licences and the loader ABI come from the owners' repos; per-game playability is thin (forum sites returned 403).

**Findings: GB / GBC**
- 🟢 Jagoomba Color v0.5 (EvilJagaGenius, 2022-06-08) is the newest release of the Goomba Color line, forked from Dwedit's 2019-05-04 source; last push 2024-04-27 [1].
- 🟢 v0.5 ships an `_ezode` build for the Omega DE (rumble) and an `_ezo` build (Omega RTC) [1].
- 🟢 No licence: GitHub's licence field is empty for Jagoomba, Dwedit/goombacolor and lesserkuma's fork; Dwedit's page carries only a copyright line [1][2].
- 🟡 Mappers MBC1/2/3/5/7 and HuC1/3; MMM01/MBC4/MBC6 stubbed; MBC3 day carry not implemented; no Game Boy Camera [1].
- 🟡 Jagoomba's fixes over Goomba Color: Pokémon Crystal graphics, Wario Land DX boots, MGS Ghost Babel, Super Mario Land DX SRAM, Konami Collections, DK Land colours [1].
- 🟡 Palette (omega-de-kernel#12, open): the emulator keeps a config record with the palette bank in SRAM; v0.5 saves only the GB type per game and defaults to greyscale [1][3].
- ❔ Famous games (Pokémon R/B/Y/G/S/C, Link's Awakening DX, Oracles, Tetris, Kirby, Mario Land 1/2, Wario Land 1-3, Metroid II, Mario Golf/Tennis, DW Monsters): no third-party compatibility list exists. `roms/` holds about 80 of them, so Torii's emulated cart can test each first-hand.

**Findings: NES**
- 🟢 Dwedit/PocketNES is still maintained: `config.h` says 2025-4-16, with commits to 2025-07-19 (MMC5, VRC3/4/6 IRQs, mapper 118, NES 2.0 headers). The last tagged build is 1-4-2020-B, and dwedit.org still offers 2013-07-01, the build Torii embeds [4][5].
- 🟢 No licence (GitHub field empty, two-line README) [5].
- 🟡 Mappers are overlays, including 1, 4, 5, 24/26 and 85. There is no expansion audio and no FDS, and an unknown mapper falls back to MMC3 [5].
- 🟡 A 2025 AI-assisted fork (GamingFanboy365/pocketnes) adds about 20 mappers and $4011 PCM; it is unreviewed [6].
- ❔ No compatibility list exists for the famous games. The big titles use MMC1/MMC3 (SMB3, Zelda 1/2, Metroid, Mega Man 2-6, Kirby, FF, DQ), which PocketNES has supported for years. There are no NES ROMs in `roms/`.

**Findings: other systems**

| System | Best emulator | State | Famous games and how they run |
|---|---|---|---|
| Master System / Game Gear / SG-1000 (+ Coleco) | S8GBA 1.1.9 (FluBBa), replaces SMSAdvance 2.5 (2009) | 🟡 active; gaps: sprite collision, speech, GG EEPROM saves [7] | Sonic, Alex Kidd, Phantasy Star, Wonder Boy III, Shinobi: no per-game reports |
| PC Engine (HuCard) | PCEAdvance 7.5 (2005) | 🟢 "mostly slow"; CD games need PSRAM writes in game, which the Omega can't do [8] | R-Type good, Bomberman '93 perfect, Alien Crush, Darius, Neutopia ok; US ROMs encrypted [8] |
| Neo Geo Pocket / Color | NGPGBA 0.6.0 (2026-08-19) | 🟢 HLE BIOS; "games that require saving will not work correctly" [9] | Sonic: boss glitch; Card Fighters Clash, Metal Slug, KOF R-2: none listed |
| WonderSwan / Color | SwanGBA 0.7.7 (2026-08-12) | 🟢 NVRAM saves; no states for colour games [10] | FF I/II: sprites in dialogs; Digimon: missing gradient; library mostly Japanese |
| Supervision | WasabiGBA 0.2.6 (2026-08-20) | 🟢 "All games should work" [11] | Small, obscure library |
| Atari Lynx | LodjurGBA 0.1.5 (2026-08-11) | 🟡 early, needs BIOS [12] | — |
| ColecoVision | Cologne 0.8 (2009), or S8GBA | 🟡 needs BIOS; some games freeze [13] | — |
| MSX | MSXAdvance 0.4 (2009) | 🟡 MSX1 only, incomplete; C-BIOS [13] | Metal Gear is MSX2, so it won't run 🔴 |
| ZX Spectrum | ZXAdvance 1.0.1a (2003) | 🟡 bakes games into the binary; not a runtime plugin [13] | — |
| SNES | Snezziboy 0.26 (GPLv2), SNESAdvance | 🟡 ROM builders, per-game tweaks [14] | BoF 1&2 "full speed" per the notes; most games slow |
| Mega Drive | none | 🔴 none found | — |
| Arcade | FluBBa single-game emulators (Ghosts'n Goblins, Yie Ar, Green Beret…) | 🟡 one .gba per game | — |
| CHIP-8, Odyssey2, Arcadia, Atari 2600 | Chip8Adv (GPL-3.0), Advyssey2, AdvCadia, PocketStella (Sterophonick) | 🟢 exist; quality unknown [15] | — |

- 🟢 FluBBa's emulators (S8GBA, NGPGBA, SwanGBA, WasabiGBA) and their CPU cores declare no licence; only GBA_Shared is MIT [16].
- 🟡 Simple DE's author says only MSXAdvance is safe to redistribute ("uses the C-BIOS") [17].
- 🔴 gametechwiki rates the GBA-hosted emulators as low accuracy and mostly inactive (seen only in search snippets).

**Upstream tracker items (checked 2026-11-02 against Torii's build)**

- 🟢 **jagoombacolor#42, accuracy tests** (a user ran Blargg's ROMs on NanoBoyAdvance: the memory-timing ones fail). `just gb-accuracy` (`tools/gb_tests_fetch.sh`, `tests/gb_accuracy.sh`) fetches Blargg's suite (retrio/gb-test-roms at c240dd7d), dmg-acid2 v1.0 and cgb-acid2 v1.1 into the git-ignored `dist/gb-tests`, runs each through Jagoomba on the emulated cart and reads the result off the game's tile map (`XGB_VRAM + 0x1800`, one tile per character); it reports and never fails. The ROMs have no licence we could ship, so they're only downloaded.
  - **Passing (15 of 54):** all 11 `cpu_instrs` singles, `oam_bug` 3 and 6, and the `dmg_sound`/`cgb_sound` overflow-on-trigger test (06) in both sets. Everything else that gives a verdict fails: `instr_timing`, `interrupt_time`, `halt_bug`, `mem_timing` 01 and 02 (the same in `mem_timing-2`), `oam_bug` 1, 2, 4, 5, 7, 8, and sound tests 01-05, 07, 08, 09 and 11. Eight print more than the screen holds (`mem_timing` 03, sound 10 and 12, the acid2 pictures) and have no verdict here.
  - **dmg-acid2 and cgb-acid2** both draw the face wrong (eyes, hair and mouth), looked at in `dist/gb-tests/out/*.png`; the same sort of result the issue's author saw.
  - Jagoomba isn't cycle-accurate (it counts a whole instruction's cycles at once, has no OAM bug and approximates the sound), so these failures are its design, not a Torii bug, and not trivial to fix. The test is the baseline to compare against if someone improves it. Mooneye's suite wasn't run: it reports through the Z80's registers (and `ld b,b`), not the screen, and would need a hook into Jagoomba's CPU state.
- 🟡 **jagoombacolor#43, a GBC-compatible ROM run as DMG.** Jagoomba reads header byte `$143` in `cart.s` (`$80` or `$C0` is GBC; main.c's name code takes `>= $80`) and the "Game Boy" setting (L+R menu, kept per game; default "Prefer GBC over SGB") picks between GBC and SGB for a game with both. A game that has `$143 = $80` and `$146 = 3, $14B = $33` starts as SGB for a moment, to fetch the SGB border (autoborder, on by default), and resets as GBC after the border is loaded or at frame 255 (`sgb.s`, `timeout.s`). `tests/gb_mode.sh` checks it on the emulated cart, with generated ROMs (`tests/gb_rom.py`) and Link's Awakening DX: a dual-mode ROM ends in GBC mode, a GBC-only ROM in GBC, an SGB-only ROM in SGB, a plain one in DMG. Nothing to fix on that path. The issue's ROM (Super Connard, patched) isn't in the repository and its attachment wasn't downloaded, so its cause is open: guesses are a `$143` value other than `$80`/`$C0` (real hardware reads bit 7, and `$84`/`$88` give the "PGB" mode with no palettes, so a plain `>= $80` isn't right either) or the border pass under NanoBoyAdvance. No ROM in `roms/` or `tests/roms/` has such a byte, so a change would be untested. A per-game override already exists (Jagoomba's own menu, kept per game).
- 🟢 **jagoombacolor#34, the default palette.** Already how Jagoomba v0.5 behaves, and `tests/gb_mode.sh` now checks it: a DMG game gets the palette its title has in the GBC's table (`gbcgamedetect.c`: a title hash, as the real boot ROM does, so a few unrelated titles share one), and a game with none gets grey (bank 1), where earlier versions left yellow-green (bank 0, "pea soup"). A palette the player picks is kept per game. What is left is taste: a global "default palette for games the table doesn't know" (grey now; pea soup or a theme colour are the alternatives) or a switch for the GBC table's colours. Neither is needed for correctness, so neither is built; the owner can decide.
- 🔴→none **PocketNES#14, multiboot** (a Dolphin user asks for it; Dwedit asks what happens without a link cable). `MULTIBOOT`/`GOMULTIBOOT` send the emulator and a game over the link cable to another GBA, which needs a second 60 KB copy of the emulator linked in. Torii runs from the Omega's PSRAM, has no link-cable path in the menu and no cable to test with, and Jagoomba's build leaves its own multiboot off for the same reason. Nothing to do; the switches stay off in `emulators/pocketnes/src/config.h`.

**Conflicts**
- ⚖️ SMS quality: GameBrew says "near-perfect compatibility" for SMSAdvance, gametechwiki says "not recommended". Leaning to S8GBA, the 2026 successor, which supersedes both.

**Open questions**
- ❔ Per-game speed for SMS/GG, NGP and WonderSwan famous titles: only real-hardware videos would settle it.
- ❔ Whether FluBBa or Dwedit would grant redistribution: ask them.

---

**Sources**
1. [EvilJagaGenius/jagoombacolor](https://github.com/EvilJagaGenius/jagoombacolor) · 2022-06-08 / 2024-04-27 · primary
2. [Goomba Color, dwedit.org](https://www.dwedit.org/gba/goombacolor.php) · 2019-05-04 · primary
3. [ez-flash/omega-de-kernel#12](https://github.com/ez-flash/omega-de-kernel/issues/12) · 2025-07-18 · primary
4. [PocketNES, dwedit.org](https://www.dwedit.org/gba/pocketnes.php) · 2013-07-01 · primary
5. [Dwedit/PocketNES](https://github.com/Dwedit/PocketNES) · 2025-07-19 · primary
6. [GamingFanboy365/pocketnes](https://github.com/GamingFanboy365/pocketnes) · 2025 · primary (fork)
7. [FluBBaOfWard/S8GBA](https://github.com/FluBBaOfWard/S8GBA) · 2026-08-19 · primary
8. [patters-match/pceadvance README](https://github.com/patters-match/pceadvance) · 2022 · primary
9. [FluBBaOfWard/NGPGBA](https://github.com/FluBBaOfWard/NGPGBA) · 2026-08-19 · primary
10. [FluBBaOfWard/SwanGBA](https://github.com/FluBBaOfWard/SwanGBA) · 2026-08-12 · primary
11. [FluBBaOfWard/WasabiGBA](https://github.com/FluBBaOfWard/WasabiGBA) · 2026-08-20 · primary
12. [FluBBaOfWard/LodjurGBA](https://github.com/FluBBaOfWard/LodjurGBA) · 2026-08-11 · primary
13. [GameBrew: List of GBA homebrew emulators and emulator pages](https://www.gamebrew.org/wiki/List_of_GBA_homebrew_emulators) · 2009-2024 · aggregator
14. [Snezziboy, SourceForge](https://sourceforge.net/projects/snezziboy/) · 2015 · vendor; [Sterophonick/Emulators-On-GBA](https://github.com/Sterophonick/Emulators-On-GBA) · secondary
15. [Sterophonick/Chip8Adv and siblings](https://github.com/Sterophonick/Chip8Adv) · 2019-2020 · primary
16. [FluBBaOfWard/GBA_Shared](https://github.com/FluBBaOfWard/GBA_Shared) · 2026 · primary
17. [Sterophonick/omega-de-kernel README](https://github.com/Sterophonick/omega-de-kernel) · 2026-03 · primary

*deep · 2 rounds · ~60 sources read · 7 researchers on sonnet*
