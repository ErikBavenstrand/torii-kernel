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
- ❔ How RetroArch scopes a type-9 key across cheats. (The other `D` lines in libretro's files are encrypted lines, not other `D` types; see v0.7.1 below.)
- ❔ EnHacklopedia's device notes: real ROM-patch slot limits, and slowdown codes on a flash cart.

## What the kernel does where devices and mGBA differ (v0.7)

The interpreter (`source/cheatrun.s`) runs each picked cheat's ops as mGBA's
`mCheatRefresh` runs one cheat set, once a frame on the VBlank IRQ; the
kernel test `cheatrun_matches_mgba` checks it against a line-for-line port
(`tests/kernel/cheat_ref.h`) on random cheats. Where mGBA leaves a choice to
its frontend, or a flash cart can't do what the device did:

- **The device's button** (GameShark's `8` codes, Action Replay v3's
  `IF_BUTTON` writes): mGBA has one host button for them. Torii reads R and
  SELECT held together (`CHEAT_BUTTON_KEYS`) on KEYINPUT, as CodeBreaker's
  `D0000020` key test does with the keys in its code. Neither chord is a
  hotkey's (L+R+START, L+R+SELECT by default).
- **Hook codes** (GameShark `F`, Action Replay `C4`, CodeBreaker `1`) are
  dropped: every cheat runs on the VBlank IRQ instead, as mGBA runs a cheat
  without a hook at the frame's end. A game that never enables the VBlank
  IRQ runs no cheats.
- **Slowdown codes** (Action Replay v3 `00000000 0800xx00`, GameShark
  v1/v2 `80F00000 0000xxxx`) are read, so their cheat reads, and do nothing,
  as in mGBA (a TODO there): the devices loop in their handler, and in the
  VBlank IRQ that would cost far more than the cheats' 10% of a frame.
- **Odd addresses:** a halfword or word op at an odd address is the CPU's
  own load and store, as mGBA's bus calls are: the load rotated, the store
  aligned down. A byte to palette or VRAM is the hardware's (both halves of
  the halfword).
- **ROM patches** are written once into the game's PSRAM image at launch,
  whatever a condition covering them says (mGBA applies its ROM patches the
  same way). A write op to the ROM becomes a patch too.
- **The budget:** the picked cheats together get 5 KB of table and a model
  of 28,000 cycles (10% of a frame at the BIOS's wait states) for what runs
  every frame, and 64,000 (about 23%, inside VBlank's 30%) counting what a
  conditional without an ELSE guards, which runs only while it holds (a
  button, say). Both caps are one number per cheat, its load
  (`CHEAT_LOAD`, `cheatcode.h`); a cheat over either alone is "too big". Each of the 6,161 libretro GBA cheats that read
  and lower fits alone (`just cheat-corpus`, 2026-10-26). Their ROM patches
  together are 256 at most (a cheat's own limit, so any one fits), kept in
  EWRAM across the game's load; the cheat screen refuses a pick past that
  rather than a launch applying part of a cheat. A pick whose enable line
  would take a window of the file past 192 KB is refused too, as it
  couldn't be saved. A false conditional's walk past the ops it guards
  counts as always run (`CHEAT_COST_WALK` an op).
- **Greyed, never hidden (v0.9.x):** the cheat screen lists every cheat. One
  that can't be picked shows greyed with its reason: unreadable, too big,
  can't run here, a master code (its key or seed sets up the codes after
  it), one that changes nothing (only conditions, `CHEATFILE_NOTHING`), or
  one whose lowered table part and patches an earlier cheat of the file
  already has (`CHEATFILE_SAME`, by hash). Any cheat can be deleted. Settings'
  Clean up cheats removes the greyed ones from every cheat file
  (`cheatstore_clean`), master codes too, but only where every cheat that
  stays reads as before; codes typed on the cart stay.
- **Larger files (v0.9.x):** a file over 192 KB or 1,024 cheats is read in
  windows of about 250 cheats or 40 KB (`cheatindex.h`), through its
  index, `<game>.cdx` beside it: each window's place, CRC, cheat count and
  the decoder at its first cheat, and the picks with what each takes of the
  budget. An open reads the index and the first window; the cheat screen
  shows a window at once and decodes it a slice a frame, and reads the next
  and previous windows ahead into PSRAM page 1 between frames, so crossing
  is at once. A pick saves the index alone; an edit or Settings' clean-up
  writes the file whole. An index that doesn't fit (the file's size, or a
  window's CRC) makes the kernel read the file whole once and index it,
  picks carried by hash. In the emulator, GameHacking's FFTA list (14,343
  cheats, 1.46 MB, 58 windows) opens by its index in 34 frames.
- **Decrypted codes (v0.9.x):** `tools/cheats.py` writes every encrypted
  code decrypted (GameShark and Action Replay codes raw, CodeBreaker codes
  plain, key and seed lines left out; `CHEAT_RAW_LINES` in `cheatcode.c`
  records them on the host), checks each decodes alone as before, and drops
  the master codes: Torii hooks the game itself, so once nothing is
  encrypted it has no use for them. Of GameHacking's 53,238 sets for the
  card's games, 52,125 were rewritten and 2 lost (they needed a key their
  format can't carry). It writes a larger file's index too
  (`tools/cheat_patches.c --index`), and carries picks over by program hash,
  so a pick outlives its code being written anew.

## AnyGBA's raw extensions (v0.9.4)

No device can write a value that depends on memory other than its target:
the Action Replay's pointer write (`[P]+O = V`), the GameShark's add, or and
and, and CodeBreaker's conditionals all use constants. Pokémon's third
generation keeps money, coins and bag counts XORed with a key in its save,
which it moves about in memory, so device codes either zero the key (which
scrambles every other encrypted value until the game rewrites it) or miss.
AnyGBA (a GBA static recompiler, 2026) reads VBA's raw lines with three
extensions, and its curated Emerald list uses them; Torii reads the same
syntax, in the VBA format:

| Line | Op | As a device code |
| --- | --- | --- |
| `AAAAAAAA:V` | WRITE (ROM_PATCH in the ROM) | any device's write |
| `[PPPPPPPP]+O:V` | WRITE_PTR | Action Replay v3's pointer write |
| `AAAAAAAA\|V` | OR | GameShark's or |
| `[PPPPPPPP]+O\|V` | KEYED, OR through a pointer | none |
| `TARGET:V^KEY` | KEYED, V XORed with the key's bytes | none |

A target or key is an address (8 digits) or `[P]` with an optional `+O` (1
to 8 digits; the key's at most FFFF). V has 2, 4 or 8 digits, its width.
A '+' after a `]` belongs to the offset (a key's only when its digits end
the line), so lines still join with '+' in libretro's files;
`cheat_plus_joins` decides it for the decoder and the cheat's detail page
alike. Lines that map onto a device's op decode to that op;
only what no device has becomes `CHEAT_OP_KEYED`, one interpreter op
(`cheatrun.s` type 6, five words) whose flags say which of its target and
key are pointers. Such codes run only in Torii and AnyGBA; mGBA and
RetroArch read them as unreadable lines. The code keypad types them too
(v0.9.5): the key between Line and Del shows a row of `[ ] + ^ |` in the
digits' place (typing `[`, `+` or `|` turns it back), a line holds 48
characters and the caret's scrolls, and a key's pointer that ends a line
before another is saved with `+0`. `tools/cheat_encode.py`'s `VBA`
writes them.

## Code types where the devices and mGBA differ (v0.7.1)

Torii's decoders started as mGBA's. Where mGBA and the devices disagree,
Torii follows the devices. The sources are GBATEK's "GBA Cheat Devices" [9],
EnHacklopedia's GBA page [10] and VBA-M's `gbaCheats.cpp` [11]. VBA-M is
an emulator, so a reading only it gives is 🟡. `tests/host/test_cheatcode.c`
(`cheatcode_device`) checks each one.

- 🟢 **Action Replay v3 IO writes:** `C6aaaaaa 0000yyyy` writes 16 bits and
  `C7aaaaaa yyyyyyyy` writes 32 [9][10][11]; mGBA writes 8 and 16. Another
  `C` code (`C0`, `C2`, `C5`) is unreadable, not an IO write.
- 🟢 **Action Replay v3 "turn off all codes"** (action bits `C0`, e.g.
  `C8`-`FA`): codes after it are off while the condition is false [9][10].
  GBATEK says the device spins in its handler until the condition holds;
  Torii skips the rest of the cheat that frame instead (the other picked
  cheats run), as a hang in the IRQ would freeze the game. `CE` ("always
  turn off") is never true. mGBA rejects these.
- 🟢 **Action Replay v3 end line** `00000000 00000000`: no code after it
  runs [9][10]; mGBA ignores it. No libretro cheat has one mid-code.
- 🟢 **Action Replay v3 empty THEN:** in mGBA's loop a conditional covering
  no ops never tests, so `IF … ELSE … ENDIF` with nothing before the ELSE
  ran the ELSE every frame. Torii puts a step that does nothing (a write of
  count 0) in the empty THEN.
- 🟢 **Action Replay v3 padding** `80000000 00000000` (needed after a 32-bit
  slide [9]) is a code on the device, so a next-one or next-two conditional
  counts it; it writes nothing.
- 🟢 **Signed comparisons** (Action Replay v3 `18`-`22`, CodeBreaker `B`,
  `C`) compare signed at the code's width [9][10][11]; mGBA zero-extends 8-
  and 16-bit values, so its signed compare is unsigned there. VBA-M compares
  CodeBreaker's unsigned; GBATEK says signed, and Torii follows GBATEK.
  `cheatrun.s` shifts both sides to the top bits; `tests/kernel/cheat_ref.h`
  does the same.
- 🟡 **GameShark v1/v2 `E0zzxxxx`** covers the next zz *lines*: the device
  steps through its list a line at a time, as VBA-M models it (`i += zz`)
  [11]. GBATEK and EnHacklopedia say "codes" [9][10], which is the same for
  every type but the type-3 list, whose address lines are lines of their
  own. `D` (the next code) still covers a whole code.
- 🟡 **GameShark v1/v2 conditional kinds:** `Daaaaaaa 00z0xxxx` and
  `E0zzxxxx zaaaaaaa` test equal (0), not equal (1), at most (2) and at least
  (3), unsigned, in VBA-M [11]; the others are unreadable. GBATEK and
  EnHacklopedia list only kind 0; mGBA reads every kind as equal.
- 🟡 **GameShark v1/v2 type 3** beyond the list (`3000cccc`): `3010xxyy`,
  `3020xxyy` add or subtract a byte, `3030xxxx`, `3040xxxx` a halfword, and
  `3050`, `3060` the next line's first word, to the word at the line's
  second half, as VBA-M reads them [11]; others are unreadable. mGBA read
  them all as lists (garbage).
- 🟡 **GameShark v1/v2 button:** `80F00000 0000xxxx` (slowdown) reads and
  does nothing [9][10]; `8a4aaaaa` writes 32 bits of 0 whatever its value,
  which VBA-M calls the device's bug [11]. mGBA rejects both.
- 🟡 **CodeBreaker `D` codes:** `D0000020 yyyy` runs the next code while
  every key in yyyy is held (GBATEK: `IF [joypad] AND yyyy = 0`; VBA-M
  reads "any key", which agrees for the one-key codes libretro has).
  `D0000010 yyyy`, from VBA-M only, runs it unless they all are. VBA-M's
  `D0000000` (run unless KEYINPUT equals yyyy) is too odd to trust and stays
  unreadable, as do others. libretro's 182 `D` lines are all `D0000020`.
- 🟢 **A second CodeBreaker key:** on the device a key line changes the
  seeds for every code after it [9]. A libretro file may join codes made
  under two keys, the second set starting with its own key line, raw. mGBA
  decrypts that line with the first key, which reads it as garbage. Torii
  takes a raw key line other than the file's as the new key when the cheat
  has more lines, the old key reads it as nonsense and the new one doesn't.
  The file's later cheats keep the new key. A key line alone is no
  evidence: an encrypted code can start with 9. No libretro file has a real
  second key, so the corpus didn't change.
- 🟢 **DEADFACE and typed cheats:** a DEADFACE line reseeds the device for
  every code after it. A cheat with its own `cheatN_format` (typed on the
  keypad) now keeps the file's GameShark seeds when its format is the
  file's; before, it got the default seeds.

`just cheat-corpus` (2026-10-26) shows that only four libretro cheats
changed, all garbage before and after. Three from FF6 and one from Pokemon
Emerald were unreadable, and now decode to `C`-prefixed "turn off"
conditionals at bad addresses.

## The validation suite (v0.7.1)

Three layers check Torii's cheats against mGBA's, and where mGBA reads a
code unlike the device, against the device. `tests/cheat_expected.toml`
lists every difference that is meant ("allow") or a known bug ("xfail",
`[[bug]]`), with the reason; anything else fails.

- **Decoding** (`tools/cheat_diff.py`): mGBA's GBA cheat decoders are built
  for the host from `emu/mgba` (`tools/mgba_cheats.sh`, stubs and flags in
  `tools/mgba_cheats/`) and decode the same files as Torii's. `just
  cheat-corpus` runs libretro's database (fetched into `dist/cheat-corpus`,
  never committed; report in `dist/cheat-corpus/report.txt`), test-host the
  committed `tests/cheats/sample` (seeded codes from `tools/cheat_sample.py`,
  and hand-made edges with the device's reading in `expect.tsv`), and `just
  cheat-fuzz-decode` any number of random codes.
- **Running** (`tests/cheat_matrix.py`, in `just test`): every code type of
  every format at every width, into every watched region aligned and not,
  every comparator at every width with each action, nesting, buttons, ROM
  patches, on the cheat test ROM in both engines. Where mGBA reads a code
  unlike the device (C6/C7, an empty THEN, signed tests narrower than a
  word, slowdown, a second key), mGBA runs codes it reads as the device
  reads the case's (`NAME.equiv.cht`).
- **Fuzzing** (`just cheat-fuzz SEED COUNT`): seeded random programs of
  valid codes, minus the shapes where Torii means to differ, run in both
  engines with random keys; failing ones are minimized and saved to
  `tests/cheat_fuzz_cases.json`, which `just test` runs.

What it found beyond the known bugs: an unaligned VBA (or CodeBreaker)
halfword or word write to the ROM is patched byte for byte where a CPU store
and mGBA align it down; a DEADFACE line in a cheat with its own
`cheatN_format` doesn't reseed the file's later cheats; mGBA's D0000020
leaves its ELSE count uninitialized, its GameShark list reads freed memory,
its PARv3 decoder shifts by a negative count for width-8 conditionals, and
its libretro reader fails a whole file on a line past 511 characters.

---

**Sources**
1. source/cheat.c, showcht.c, GBApatch.c, gba_rts_patch.s, ezkernelnew.c, tests/ (cheat.c and showcht.c went in v0.7; see them at 2e6f038c) · HEAD 2e6f038c · primary
2. [emu/mgba/src/gba/cheats/*.c, src/core/cheats.c](../../emu/mgba/src/gba/cheats/codebreaker.c) · local checkout · primary
3. [LICENSE](../../LICENSE) (Apache-2.0) · primary
4. [libretro-database](https://github.com/libretro/libretro-database) (tree, README, LICENSE) · 2026-10-25 · primary
5. All 514 GBA files classified: a script over all 514 files (not kept) · 2026-10-25 · secondary (own computation)
6. libretro cht names against `dist/dats/Nintendo - Game Boy Advance.dat` (2026.10.07) · 2026-10-25 · secondary (own computation)
7. EverDrive GBA PRO user manual (PDF) · 2026-04-20 · vendor
8. gbatemp threads 509684, posts/10891810 (search snippets) · undated · forum
9. [GBATEK, GBA Cheat Devices](https://problemkaputt.de/gbatek.htm#gbacheatdevices) · 2026-10-26 · secondary (reverse-engineered)
10. [EnHacklopedia, Hacking GBA](https://doc.kodewerx.org/hacking_gba.html) · 2026-10-26 · secondary (community)
11. [VBA-M, src/core/gba/gbaCheats.cpp](https://github.com/visualboyadvance-m/visualboyadvance-m/blob/master/src/core/gba/gbaCheats.cpp) · master, 2026-10-26 · secondary (emulator)

*deep · 2 rounds · ~40 sources read · sonnet ×6*
