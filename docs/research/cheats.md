# Cheats: formats, database and the kernel

Research for v0.7 (2026-10-25), before the design in `docs/plans/roadmap.md`.

## Findings

> **Answer:** Today Torii reads only EZ-Flash's `.cht` format. It applies at most 600 constant byte writes from the IRQ hook, under one global on/off flag, and forgets the picks after each launch. mGBA's decoders for GameShark v1/v2, Action Replay v3 and CodeBreaker can be ported: they are MPL-2.0 per file, which works with Torii's Apache-2.0. libretro's GBA database holds 7,024 cheats in 514 files, almost all plain CodeBreaker. 72% of its real cheats are constant writes or ROM patches that v0.7 can run without v0.8's interpreter; 80% if fills are counted.
> **Confidence:** 🟢 high — the code and the database were read directly; the matching heuristics and the community sources are 🟡 or unread.

**Findings**
- 🟢 The only format read today is EZ's: `[Section]` is a cheat, its `key=` lines are options (one per section), and a value is `ADDR,VV,VV;…` byte writes. The address is folded into IWRAM or EWRAM by a mask, so ROM and IO writes can't be written [1].
- 🟢 Lookup: `/CHEAT/<rom name>.cht`, else `/CHEAT/GameID2cht.bin` → `/CHEAT/Eng/<bucket>/<id>.cht` (EZ's pack). It needs the header's game code [1].
- 🟢 Picks are made on the cheat screen at each launch and never stored. Cheats is one global setting [1].
- 🟢 The limit is 600 writes of 8 bytes each in a 0x2000-byte patch area (handler 0xD04). An overflow is dropped silently [1].
- 🟢 The table `pCHEAT` takes 4.8 KB of EWRAM, and the screen borrows `pReadCache` (about 2,000 lines). `docs/research/architecture.md` still says there is no cap [1].
- 🟢 The v0.3 freeze is real: each pick re-reads the whole file a byte at a time [1].
- 🟡 The "dropped spaces" bug looks fixed already: `tests/cheats.sh` checks inner spaces. Not run [1].
- 🟢 GB, GBC and NES games get no cheats: `Game_pick` returns before the lookup. PocketNES has its own cheat finder [1].
- 🟢 mGBA's GBA cheat files are MPL-2.0 per file [2][3].
- 🟢 Formats, as mGBA decodes them [2]:
  - GSAv1 (GameShark v1/v2, AR v1/v2): TEA-style cipher; 8/16/32-bit writes, a list, a ROM patch, a button, if-eq, a hook.
  - PARv3 (AR v3, GameShark SP): writes with fill or slide, pointer, add, conditionals with ELSE/ENDIF, buttons, four ROM patches, IO writes, slowdown (not implemented).
  - CodeBreaker: `8hex 4hex`, encrypted only after a type-9 key line; writes, or/and/add, 16-bit conditionals, fill, key test, hook.
- 🟢 Format detection is heuristic: CodeBreaker by its shape, VBA by its `:`, GSAv1 against PARv3 by a plausibility score. Directives can force a version [2].
- 🟢 The ciphers, seed tables, CodeBreaker's LCG and the scoring port as plain C. The cheat lists, memory access and hook breakpoint are mGBA's own. `src/gba/test/cheats.c` gives test vectors. mGBA also reads EZ `.cht` files [2].
- 🟢 libretro-database is CC-BY-SA-4.0, with codes "collected from any available source on the web" [4].
- 🟢 Its cheat files per system: GBA 514, GB 1,496, GBC 960, NES 2,265 [4].
- 🟢 Each file is named `<No-Intro name> (<code type>).cht`, with no CRC or game code inside. The format is `cheats = N` with `cheatN_desc`, `_code` and `_enable`; `+` joins lines [4].
- 🟢 GBA: 7,024 cheats; 6,795 plain CodeBreaker, 37 encrypted CodeBreaker, 176 GameShark/PARv3. The "(Code Breaker)" tag isn't reliable: 29 files lack it, and some tagged ones hold GameShark codes [5].
- 🟡 Of the 6,456 cheats that aren't master codes [5]:
  - 72% are constant writes or ROM patches, and 80% with fills expanded.
  - 19% need conditionals, 10% fills, 8% button activators, 2% or/and/add and 0.1% pointers.
  - 30 are ROM patches.
  - 500 master codes ("Must Be On") aren't needed, since Torii hooks the IRQ itself.
- 🟡 No CodeBreaker-tagged game's plain cheats pass 600 writes together; FF6 Advance is the largest at 237. Fills expanded to plain writes would pass the cap in 35 cheats (up to 2,000 bytes), so a fill should stay a loop [5].
- 🟡 Matching to the No-Intro dat that `names_db.py` already caches: 181 GBA names match exactly, 37 more without language tags, 245 by title only (the region differs), and 44 fail. The ROM header's game code against the dat's serial could settle the ambiguous ones [6].
- 🟢 Prior art, EverDrive GBA PRO: 4 pages of 11 groups of up to 8 codes (352); Select toggles a group; codes typed by hand or imported in mGBA's format; kept per game. It warns that "every code steals a small amount of CPU time" [7].
- 🔴 Omega users (gbatemp search snippets only): codes have to be converted to raw by hand, and files in the wrong folder silently do nothing [8].

**Open questions**
- ❔ DeadSkullzJr's Omega-targeted GBA database (gbatemp) and GameHacking.org: coverage and terms unread (403).
- ❔ Jagoomba's cheat support, and whether GB/NES Game Genie or GameShark codes could reach the emulators.
- ❔ The 31 CodeBreaker type-D codes other than the key test, and how RetroArch scopes a type-9 key across cheats.
- ❔ EnHacklopedia's device notes: real ROM-patch slot limits, and slowdown codes on a flash cart.

---

**Sources**
1. [source/cheat.c, showcht.c, GBApatch.c, gba_rts_patch.s, ezkernelnew.c, tests/](../../source/showcht.c) · HEAD 2e6f038c · primary
2. [emu/mgba/src/gba/cheats/*.c, src/core/cheats.c](../../emu/mgba/src/gba/cheats/codebreaker.c) · local checkout · primary
3. [LICENSE](../../LICENSE) (Apache-2.0) · primary
4. [libretro-database](https://github.com/libretro/libretro-database) (tree, README, LICENSE) · 2026-10-25 · primary
5. All 514 GBA files classified: a script over all 514 files (not kept) · 2026-10-25 · secondary (own computation)
6. libretro cht names against `dist/dats/Nintendo - Game Boy Advance.dat` (2026.10.07) · 2026-10-25 · secondary (own computation)
7. EverDrive GBA PRO user manual (PDF) · 2026-04-20 · vendor
8. gbatemp threads 509684, posts/10891810 (search snippets) · undated · forum

*deep · 2 rounds · ~40 sources read · sonnet ×6*
