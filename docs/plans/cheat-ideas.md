# Cheat ideas (research, 2026-11-02)

Research only: nothing here is decided or built. Sources read: `docs/torii-code.md`, `source/cheatcode.h`
and `cheatrun.s` top comments, `docs/research/cheats.md`, `docs/plans/cheat-*.md`, the roadmap's cheat
items, `docs/design/v0.9/cheat-search.md`. Costs: S under a day, M a few days, L a week or more.

## Constraints that shape every idea

- **The interpreter keeps no state of its own** (`cheatrun.s` top): no RAM but the toggle mask, the keys
  (`0x3007FE2`, `FE6`) and the run-once byte. IWRAM above `0x3007FE0` is full (BIOS words at `FF0`, the IRQ
  hook at `FFC`). Anything needing a counter, a timer or a "last value" either keeps it in game RAM at an
  address the author names (a scratch cave, as the golden Emerald counts with `#`), or needs new bytes.
  The one spare place: the live block (`INGAME_LIVE_BYTES` 0x20: check word plus 3 pairs = 28 bytes, so
  4 spare), which sits below the game's stack top; Street Fighter Alpha 3 crashed when 32 bytes were taken
  there (roadmap), so more is risky.
- **Op encoding:** the info word's 4-bit type is full (0-15), but op kinds K 9-15 are free
  (`cheatrun.s`, `CHEAT_PROG_*`), so a new op is a new kind: new interpreter code in the blob (the game's ROM
  tail, ROM budget only), a case in `cheatlower.c`, `cheat_ref.h` for the kernel test.
- **Budget:** 28,000 cycles every frame, 64,000 gated (`CHEAT_LOAD`); table 5 KB, picks 4,096 patches.
  An op's cost goes in `CHEAT_COST_*`; `just size` is not touched by table-side work (EWRAM ~11 KB, IWRAM
  margin ~4 KB over the 2.8 KB stack chain are the kernel's, not the game's). Pure lowering ideas cost none.
- **Testing:** mGBA cannot run Torii-only ops (it reads them as unreadable), so they are tested through
  `tests/host/test_cheatcode.c`, `tests/kernel/test_cheatrun.c` (extend `cheat_ref.h`) and a launch like
  `tests/torii_code.sh`; the fuzzer's and matrix's mGBA comparison skip them.

## Already built (do not propose)

Decoders for every device format; Torii code with tests, blocks nesting 4 deep, ELSE, `@`/`@@` buttons,
`& | # ^`, pointer targets `[P]+O` and keys; per-cheat activation (always, launch, hold, press) and guides;
folders and choice groups; Run once from the in-game list; ROM patches as free runs; the in-game search
(8 filters, BCD, signed, one-level pointer scan, one Undo) with finds filed as cheats; greyed-with-reason
cheats; a file-level ROM guard (`# for crc32 XXXXXXXX`, `for_this_dump` in `cheatstore.c`: a file for another
dump is left out); an encoder (`tools/cheat_encode.py`); the corpus, matrix and fuzz suite; GameHacking
import on the PC (`tools/cheats.py`, mGBA export to libretro). Also expressible today: a copy of one element
(`Y:00^X`, value 0 XOR the bytes at X), a guard on the ROM (`080000AC=...` as a test), clamps (test + write),
counters in a scratch address.

## Ranked proposals

### 1. Value editor: a cheat with a number the player sets (M, risk low)

**What:** `cheatN_param = "Amount:9999:0:999999"` (name, default, min, max) and `$1` in the code, replaced at
decode time; the cheat screen's START menu gets "Set value" using the search's number edit; the file keeps
`cheatN_value`. "Money: [9999]", "Level", "Item quantity", "Wild species" (a param with a name list collapses
the 400-variant choice groups into one row).
**Why:** the most asked-for thing in every cheat tool, and today a player edits hex in the code keypad. Golden
Emerald has 489 cheats, many of them one value each.
**Fits:** `cheatfile.c` (parse the key, substitute before `cheat_decode`), `screens/cheats.c`, `cheatlist.c`;
no interpreter or table change, so no IWRAM/EWRAM/ROM cost beyond a few hundred bytes of code. The in-game
list can show but not edit it (no keyboard in the overlay; left/right stepping is possible later).
**Risk:** picks and the index carry by program hash (`program_hash`), so changing a value must keep the pick
(key it by cheat, not hash); `$` is free in Torii code (`{ }` and `@` are not).
**Test:** host tests for substitution and range; a screen golden (en and sv strings); launch test that the
written value is the set one.

### 2. Torii-code operands from memory, and repeat (M, risk medium)

**What:** (a) `AAAAAAAA#[BBBBBBBB]`, `AAAAAAAA=[BBBBBBBB]`, `<` `>` with a memory right side, and `:[B]` for a
copy; (b) a repeat suffix, `LINE x6+0064`, writing, adding, OR-ing or testing six party slots 100 bytes apart.
**Why:** (a) makes "heal: set HP to max HP", "restore PP", "on change" triggers (test against a shadow byte,
then copy to it) and "keep the larger" expressible; today each needs a constant per value. (b) the
interpreter already has elements for WRITE, PTR, ADD, OR, AND (`CHEAT_COST_GEN`, PARv3 slides); only Torii's
syntax lacks them (verify in `torii_line`), so party-wide cheats cost one line and little table.
**Fits:** `torii_line` in `cheatcode.c`, a new op kind `K 9` (operand flags as `CHEAT_KEYED_*`), `cheatlower.c`,
`cheatrun.s`, `torii-code.md`, `cheat_encode.py`. (b) is S and mostly lexer; (a) is M with new assembly.
**Cost in cycles:** like a keyed op, `CHEAT_COST_KEYED` 520; no RAM.
**Risk:** assembly in IRQ context; wrong widths and odd addresses (reuse the CPU-load rule). Test through
`cheat_ref.h` randoms and `test_cheatrun_torii`.

### 3. Toggle activation: press a chord to switch a cheat on and off (M, risk medium)

**What:** `cheatN_keys = "toggle L+R"`: each press flips the cheat's own bit in the toggle mask
(`0x3007FE8/FEC`), so it works without the in-game menu and without holding buttons (fly, no-clip, speed).
**Why:** hold and press are the only chord modes; "hold L to walk through walls" is a thumb cramp, and run
once is single shot. This is the GameShark habit players know.
**Fits:** a gate pair like Press's two conditionals (`cheatfile.c`) plus a new "flip my bit" op (kind K 10)
in `cheatrun.s`; the in-game list already reads the mask, so its rows tick live. Needs a rule for the start
state (starts on, or a launch-time bit set by the kernel in the table's first header; open question).
**Risk:** the mask is zeroed by SoftReset (fine); the keys' "before" snapshot must not double-fire on the
frame a state loads. **Test:** kernel test with scripted `CHEAT_KEYS_*`; `tests/cheats_ingame.sh`.

### 4. Cheat lint and run harness for authors (S-M, risk none)

**What:** `just cheat-lint FILE`: runs the host decoder (`tools/cheat_corpus.c` exists) and reports unreadable
lines, load against both caps, duplicate and overlapping writes, test-less loops that write every frame,
missing guide or keys, guards missing the `# for crc32` line, folder and choice mistakes; and `just cheat-run
FILE CHEAT --ram A=V --keys L+R --frames N` on `tests/roms/cheattest` with `mgba-shot -p/-k` printing the
memory afterwards. A disassembler is `cheat_corpus.c` printing the Details page's text.
**Why:** curation of 489 golden cheats is by hand; new gen3 sets will be too. Catches what players hit later.
**Fits:** `tools/`, `tests/cheat_codes.sh` (the harness), no kernel code. **Risk:** none.

### 5. Dump-mismatch visible, and lookup by CRC (S-M, risk low)

**What:** (a) when `for_this_dump` leaves a file out, show a note on the Cheats page ("these cheats are for
another dump: CRC 1F1C08FB") instead of a log line only; offer "Use anyway". (b) if no file is at
`/torii/cheats/<rom path>.cht`, look for `/torii/cheats/by-crc/<CRC>.cht` using the CRC cache
(`names_crc_now`), so renamed or moved ROMs keep their cheats and one file serves identical dumps.
(c) `cheatN_rom = "080000AC=45565841"` style per-cheat guard lowered to a first test, plus a header check
(game code + version byte, `0x080000AC`/`0xBC`) written beside the CRC line when Torii creates a file.
**Why:** libretro files carry no CRC and revision differences (Emerald rev 1) silently write wrong addresses
(research: "files in the wrong folder silently do nothing" for Omega users).
**Fits:** `cheatstore.c`, `cheatfile.c`, `screens/cheats.c`; no interpreter work.

### 6. Gen 3 Pokémon golden sets (Ruby, Sapphire, FireRed, LeafGreen) (L per game, risk medium)

**What:** reuse `tools/golden/emerald.py` as a library fed by each pret symbol file (addresses, species and
item tables, the encryption key's offset) and emit the same Menu/Money/Pokémon/Battle/Wild/Overworld sections.
**Why:** the one family where the XOR-key helpers matter and where players ask most; Emerald proves the
design. **Fits:** `tools/golden/`, `docs/research/cheats-emerald*.md`. The compiled blobs (`field.c`,
`menu.c`) need per-ROM addresses, the biggest cost. **Test:** `just compat`-style launch with a user-supplied
ROM in `roms/`; commercial ROMs are the owner's, so CI cannot run these.

### 7. Cheat search: what is left of its list (each S-M)

- **Two-level pointer chains** (`[[P]+O1]+O2`, also Torii syntax; M, also an interpreter kind): the search's
  pointer scan stays one level; games that nest (Emerald's bag through `SaveBlock1` then a pocket pointer) need it.
- **"On now" through a pointer** (S-M): a live-block pair kind with a pointer flag; the block has 4 spare
  bytes and no room for more pairs, so one pointer pair replaces plain ones.
- **Auto-name finds in game** (S): "Found 02024284", editable later; no keyboard needed.
- **Memory view around a candidate** (M): 16 bytes before and after, the digits the overlay already draws.
- VRAM, palette, OAM searches and a second Undo: low value, the 288 KB buffer has no room; skip.

### 8. Frame counter and random pseudo-registers (S-M, risk medium)

**What:** a word the interpreter bumps each run (and an LFSR), read as `@F` / `@R` in tests: "every 64 frames",
"a 1-in-8 chance", timers such as "on for 5 seconds after a press" (with a scratch byte).
**Why:** timed and random cheats today need an author-picked scratch address and a bump line; most games
have no safe one. **Cost:** the live block's 4 spare bytes, or a game-RAM scratch declared by the file
(`cheatN_scratch`); the first needs a stack-top margin test across `just compat` games. Rank low until the
byte is found; most use can be had with proposal 2 plus a scratch address.

### 9. Macros and shared routines in a cheat file (M, risk low)

**What:** `[define NAME]` lines in a file, `call NAME(args)` in a cheat, expanded at decode (no interpreter
change), so 489 cheats can share "set flag", "give item" and "write string" bodies.
**Why:** shrinks files, keeps golden sets maintainable. Not a runtime call: a true `CALL` into native ROM
code (how the golden menu runs) already works through ROM patches and a hook, and a general CALL op would run
arbitrary code in the IRQ with a 48-byte stack: reject.
**Fits:** `cheatfile.c`/`cheatcode.c` text expansion; the budget counts the expanded ops.

### 10. Importers and exporters (S each)

- **On-cart mGBA `.cheats` and EZ-Flash `.cht` read** (M): a user drops a file from mGBA; the PC tool already
  converts GameHacking exports, so a plain `just cheats --import FILE` for the user's own mGBA file is S.
- **Export for other emulators** (S): `cheat_encode.py` could write the device-only subset and name what
  Torii code lost. Low value; RetroArch already reads libretro `.cht`, ignoring Torii's keys.
- **Cheat packs as a release asset** (S): `torii-cheats-golden.zip` as the language packs, one file per
  game, so a player copies one folder.

### 11. Smaller Torii-code additions (S each, mostly lowering sugar)

Masked equal `A&M=V` (one op type bit, or two tests: bit-field compares are common in gen 3 data); masked
write `A:V/M` (lowers to AND then OR); a clamp `A<>LO..HI` (test + write, lowering); multiply and shift
(BCD and percent maths, rare); block copy `DST:copy SRC,N` (snapshots need scratch RAM, rare); 64-bit
adds (rare). None needs RAM; put sugar in the lowering so the table does not grow a kind.

### Not recommended

- A general CALL into native code from the table (stack, IRQ safety).
- A restore-on-off undo for cheats: needs a snapshot area the budget lacks.
- GB, GBC and NES cheats are on the roadmap already (libretro has 4,721 GB/GBC/NES files against 514 for GBA,
  so the reach is the largest of anything here, but it is an emulator-patch project of size L, not a
  Torii-code one).

## Recommended top 3

1. **Value editor (1):** biggest player-facing win, no interpreter risk, no memory budget.
2. **Memory operands and repeat (2):** unlocks heal-to-max, party-wide and on-change cheats for authors;
   split it: repeat first (S), memory operands second (M).
3. **Toggle chord (3):** fixes the hold-only ergonomics; pair it with the lint and harness (4) so the new
   ops arrive with the tooling to check them, and with the dump-mismatch note (5) as a quick S win.
