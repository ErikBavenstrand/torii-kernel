# GB, GBC and NES cheats

Design (2026-11-02, research only: nothing built or run). Roadmap item "(1.0?) GB, GBC and NES
cheats" (`roadmap.md`). Goal: the cheat screen, folders, picks and `/torii/cheats/<rom path>.cht`
work for GB, GBC and NES as for GBA, with the picks applied at launch. Status marks: 🟢 read in
the code, 🟡 from memory of other sources and to be checked, 🔴 unknown.

## Status (2026-11-02, after stages 0-4)

Everything below stages 0-3 shipped as designed, plus typing codes on the
cart (stage 4). Where the build differs from the design:

- **Poke table in `.rodata`.** `torii_cheats` is `const volatile` in section
  `.rodata.torii_cheats` of each emulator (a first build had it in IWRAM, which
  overflowed). The kernel finds it by its magic once per launch
  (`cheatemu_find_table`); a kernel test asserts exactly one per image, empty.
- **The poke budget** is 63, not 64: a poke takes 80 bytes (20 words) of the
  cheat file's table budget (`CHEATFILE_POKE_WORDS`), so the picks together
  fit the emulator's 64-entry table.
- **GB hook** sits inside `newframe_vblank` (lcd.s), which saves every register
  itself, not after the `bl` in timeout.s. Five games' screens and WRAM were
  identical with the hook and an empty table, over 700 frames.
- **GameShark types:** `01` and `90`-`97` to WRAM (C000-DFFF, the echo) and HRAM
  (FF80-FFFE) run. The type byte follows EnHacklopedia's Game Boy page
  (`9b`: WRAM bank b, GBC only; `8b`: cart RAM bank b) and gambatte
  (`applyVblankCheats` applies `01` only, at the VBlank interrupt); mGBA writes
  any type's address through the bus, ignoring the type.
- **Left greyed, with the reason:**
  - *Cart RAM (`01` at A000-BFFF, `8b`).* Jagoomba keeps the cart's RAM as a copy
    in EWRAM (`XGB_SRAM`) and copies it whole to the cart's SRAM at saves and
    in save states (`sram.c`: `bytecopy(MEM_SRAM, XGB_SRAM, ...)`, `lzo1x_1_compress(XGB_SRAM...)`):
    a poke into the copy reaches the save file, and one made through the game's
    write path (`sram_W2`) writes the cart's SRAM at once. A cheat that must not
    change the save can't be done by writing there. (A real GameShark does change
    the battery save; Torii doesn't.) `8b` has one source only (EnHacklopedia;
    PyBoy's copy of it is commented out), so it isn't implemented either way.
  - Writes to the ROM, VRAM, OAM, I/O and IE, and types other than `01`, `90`-`97`.
  - NES: raw writes outside RAM (0000-1FFF) and PRG-RAM (6000-7FFF) are unreadable;
    the compare form `AAAA?CC:VV` is unread (libretro has none).
- **GBC Game Genie** (roadmap: "left"): nothing was missing in the code, the GB
  and GBC paths are one. It is now tested on a `.gbc` test ROM and, from the
  owned game list, a real GBC game (`tests/emu_cheats.sh`): a code made from the
  ROM's own bytes changes exactly the banks that hold its compare byte in the
  game's PSRAM copy.
- **Typing codes** (`keypad_edit.c`, `screens/keypad.c`): the keypad types GB codes
  with hex digits and `-` (the `:` key becomes `-`), NES codes with the hex digits
  and `:`, the GG key swapping the digits for the 16 genie letters. A line must
  read alone (else "isn't a whole code yet"), then the cheat as cheatemu decodes
  it; what it does is the result line. Buttons stay hidden.

### After the review (2026-11-02)

Fixes from a reviewer's and a tester's findings, each tested:

- **Clean-up and orphaned files.** Settings' clean-up decided a file was a GB, GBC
  or NES game's by whether the game was still on the card, so a renamed or deleted
  game's cheat file was cleaned as a GBA one and lost every code. It now goes by the
  codes' shape (`cheatemu_shape`: a Game Genie, `AAAA:VV`, a `?` wildcard or an
  Xploder `$` line, and nothing a GBA format has), never by the ROM: such a file, or
  such a window of a larger one, is left as it is.
- **NES Game Genie banks.** A 6-letter code for 8000-BFFF no longer patches the
  fixed last bank (mappers 1, 2, 71, 94), nor MMC3's last two (4, 118, 119); C000-DFFF
  on MMC3 is the second to last 8 KB bank, E000-FFFF the last.
- **Rename and delete** move and remove a GB, GBC or NES game's cheat files as a
  GBA's. A name two games share (`Foo.gb` and `Foo.gbc`) is one file, which stays.
- **The launch enforces the budget** (63 pokes; the ROM patches kept) after a file
  edit or in a windowed file, and says what it left out: a dialog after the load
  ("N cheats or lines weren't applied: no room, or no match in this game"), which counts
  a cheat left out whole, a poke with no room, and a Game Genie line that patched nothing
  (another game version, a checksum byte, the patch limit). The cheat screen already says
  why a pick is refused (the hint bar's reason).
- **8 MB ROMs.** The loader (`LoadEMU2PSRAM`) pages by the file offset and writes each
  block at the emulator's size plus its offset in the page; a block of the last 80 KB
  of an 8 MB page ends past the page's window. The patcher addresses the same linear
  place the game runs from, so it is right wherever the loader was; but it now leaves a
  byte there alone (`cheatemu_rom.base`, `done.unmapped`), as it isn't known to be in
  the copy. Only a ROM of about 8 MB is affected. The loader itself isn't changed here.
- **NES raw code outside RAM** (4000-5FFF, 8000+) and the compare form are "can't run"
  with a reason ("Writes the ROM or a register"), not unreadable.
- **Reasons** name the line that doesn't run: cart RAM ("Writes cart RAM, which a save
  keeps"), video RAM, the ROM or I/O, an unknown GameShark type.
- **Checksum bytes.** A GB Game Genie byte on the emulators' sampled words (4 of every
  128 in the first 16 KB, `cheatemu_sampled`) is skipped, as designed; the cheat's
  details and the hint bar now say "Checksum bytes stay unpatched" for it, the Info
  text explains it, and the launch counts it in the dialog. NES can't say before the
  game loads (the offset depends on the mapper's banks): the launch log and dialog do.
  `cheatstore_launch_none` also resets the kept patches.
- **`jagoombacolor.patch`** rewrote five whole files (gbz80.s, lcd.s, main.c, sram.c,
  ui.c): an edit tool had turned upstream's CRLF into LF. They are CRLF again, the
  patch is 580 lines, and the binary is the same.

### Speed of the hooks, measured on the emulated cart (2026-11-02)

TM3 read around the call (1 cycle a tick), 600-700 frames of real games and the
tiny test ROM, mean cycles per call (a GB frame is 280,896 cycles):

| Hook | Table | Cycles per frame | Share of a frame |
| --- | --- | --- | --- |
| Jagoomba, Dr. Mario (GB) | empty | 63 | 0.02% |
| Jagoomba, Donkey Kong Country (GBC) | empty | 63 | 0.02% |
| Jagoomba, Dr. Mario | 63 pokes | 9,630 | 3.4% |
| Jagoomba, Donkey Kong Country | 63 pokes | 9,710 | 3.5% |
| PocketNES, test ROM | empty | 90 | 0.03% |
| PocketNES, test ROM | 63 pokes | 3,780 | 1.3% |

About 150 cycles a poke in Jagoomba and 58 in PocketNES: the hook runs from ROM
(PSRAM, wait states), and IWRAM has no room for it. Since 2026-11-15 it runs from VRAM
(below), at about 106. A typical cheat of 5 pokes
costs 0.27% of a GB frame. The first version, reading each field of an entry
separately, cost 88 cycles empty and 9,960 with 63 pokes; two word loads an
entry brought it to the above. The mean excludes the IRQs that land inside the
call (the maximum, up to 32,000, is a sound or VBlank IRQ). Not measured on a
real cart, whose PSRAM timing may differ.

### With hi-colour, and the hook in VRAM (2026-11-15)

- **Merged with main's hi-colour GBC** (3c49daae). The two Jagoomba patches touch
  different code (the hook in `newframe_vblank`; hi-colour's `FF69_W` recording,
  `hc_frame_start` and the HBlank replay) and were merged on the source, upstream's
  CRLF kept, then saved with `just emulators-save-patch`. The cheats use no EWRAM
  or IWRAM (table and code outside both), so hi-colour's figures stand: the save
  heap's worst case (32 KB of SRAM compressed, `ewram_start` `0x020000CC` +
  `0x1E000` + 48 + `0x8843`) ends at `0x0202693F`, 641 bytes below `END_OF_EXRAM`
  `0x02026BC0`; `CANARY2` stays at `0x03007E78`. Hi-colour moved `XGB_RAM` from
  `0x03005890` to `0x03005990` (`gbc_palette` left IWRAM); the tests read it from
  the emulator's symbols.
- **The hook runs from VRAM** (`.vram1`, 200 bytes; 368 of its 3,584 left): from
  PSRAM its code fetches made a poke cost about 190 cycles; now about 106. Measured
  with `tests/emu_cheat_speed.py --measure`: Jagoomba's `frame` 600 GBA frames apart
  from 1,300 frames after the launch, 63 pokes (type `97` to DF00-DF3E, a bank these
  games leave alone):

| Game | VSync on, none | VSync on, 63 pokes | VSync off, none | VSync off, 63 pokes |
|---|---|---|---|---|
| Hi-colour test ROM (576 colours a frame) | 530 | 521 | 530 | 521 |
| Pokémon Crystal | 600 | 600 | 1,222 | 1,175 |
| Link's Awakening DX | 600 | 600 | 2,264 | 2,098 |
| Donkey Kong Country | 600 | 600 | 1,142 | 1,099 |
| Wario Land 3 | 600 | 600 | 1,884 | 1,769 |
| Dr. Mario | 600 | 600 | 2,776 | 2,500 |

  From ROM the same 63 pokes gave 511 (test ROM) and 1,134, 1,966, 1,061, 1,668
  and 2,315 with VSync off. Every game stays at full speed with VSync on; the
  test ROM, which can't reach full speed with its 576 colours, loses 1.7%.
- **Sound.** The hook writes no sound register. GBA sound registers sampled every
  20 frames for 600 frames matched with 63 pokes and without a cheat file in Dr.
  Mario, Pokémon Crystal and Link's Awakening DX; Donkey Kong Country's differed, as
  did two of its WRAM bytes, and so did Dr. Mario's against a cheat file with every
  pick off: the launch's own time (reading the file) moves the game's phase, so an
  exact comparison isn't a test. The cart's check is by ear.

### The proof (2026-11-15)

- **GB and GBC** (`tests/gb_cheat_proof.sh`, `gb_cheat_rom.py`, `gb_cheat_proof.py`):
  a test ROM of 8 banks on MBC1, MBC3 or MBC5 (GB, or GBC with WRAM bank 3 mapped)
  records, a little after each VBlank, three bytes of every bank, two of bank 0 and
  ten watched RAM bytes (a write found is recorded and counted, then cleared). 34
  cases run in mGBA's GB core with mGBA's cheat device (`mgba-shot -E`) and through
  Torii; both must match a model of the device on the bus. Torii matches it in every
  case but the checksum byte it skips on purpose. mGBA matches it but where its
  shortcuts differ, each predicted and checked: a compare patches the first bank that
  holds it (and never the last: its search stops one short), a code without a compare
  the bank mapped when it applies, and `9x` codes the mapped WRAM bank. mGBA's ROM
  patch also re-keyed a compare patch every other frame and then patched the mapped
  bank too; our mGBA patch fixes that (`src/core/cheats.c`, `Torii:`).
- **Real GB and GBC games** (`tests/gb_cheat_games.sh`): libretro's runnable codes
  (up to 40 pokes and 8 Game Genie cheats a game) on 16 owned games. Every poke that
  holds in mGBA at frames 900-903 holds in Torii (Pokémon Red 39 of 40 in both, Crystal
  39 of 40, Wario Land 3 17 of 40: the rest are bytes the game itself rewrites between
  the VBlank and the read, in both). Every Game Genie patch the device would make is
  counted in Torii's log and up to 48 patched and 48 untouched bytes read back from
  the game's copy in PSRAM (Yellow 259 patches, Super Mario Bros. Deluxe 252).
- **NES** (`tests/nes_cheat_proof.sh`, `nes_cheat_rom.py`): no reference emulator,
  so a model of the device; 21 cases on NROM, UxROM, MMC1 and MMC3 test ROMs (6 and 8
  letters on switchable, fixed and second-to-last banks; raw pokes to RAM, a mirror,
  PRG-RAM; a raw write to the ROM refused), read from PocketNES's `NES_RAM`. Torii's
  static patch matches the bus except on MMC3, where a bank shows a patch in both
  8 KB windows (8000-9FFF and A000-BFFF): the bus patches the read's window alone.
  No NES game is in `roms/`.
- **Saves** (`tests/emu_cheat_saves.sh`): GB 32 KB and 8 KB and NES save games run
  with and without Game Genie patches and pokes picked; the saves are the same bytes.
- **A pick turned off** (`tests/emu_cheats.sh`): the next launch on the same cart
  writes no poke and makes no patch.
- **Typing** (`tests/emu_cheat_typing.sh`): a code typed key by key on the keypad
  for a GB, a GBC (`93` bank code) and a NES (raw) game is saved picked and runs at
  the next launch.
- **Coverage** (`tools/cheat_emu_coverage.py`, 2026-11-15): GB 84.4% (11,189 of
  13,259 distinct cheats), GBC 87.4% (6,221 of 7,116), NES 99.5% (31,301 of 31,448),
  unchanged.

## What the code does today

- 🟢 Only GBA reaches cheats. `action_launch` reads the picks only `!game.is_emu` (`source/actions.c:188`);
  `Game_pick` gives GB/GBC/NES `save_num 0xF` and no addon menu (`source/ezkernelnew.c:1735-1746`); the
  game popup's Cheats row needs `gba` (`source/screens/games.c:1498-1513`).
- 🟢 The emulator launch (`Game_boot`, `source/ezkernelnew.c:1887-1913`) sends the FAT parameters, then
  `LoadEMU2PSRAM` (`:915-1010`): the emulator image from the kernel's ROM (`source/emulators.s`,
  `.incbin`) goes through `pReadCache` to PSRAM offset 0 in 128 KB parts; the game file follows in 128 KB
  parts at `rom_start_address` (right after the emulator for GB/GBC; after a 48-byte PocketNES header on a
  256-byte boundary for NES), raw, with the iNES header kept. Then `Launch_now`. **Nothing but the ROM
  bytes passes to the emulators**: no table, no settings block; they keep their own settings in cart SRAM.
- 🟢 The whole GBA cheat path is GBA-specific: `cheatfile_decode` (`source/cheatfile.c:508`) calls
  `cheat_decode_split` then `cheat_lower_onto` (GBA address map, interpreter words, budgets); ROM patches
  are halfword + mask (`struct cheat_patch`, `source/cheatcode.h`) written into PSRAM by `rom_patch`
  (`source/cheatstore.c:1893`: byte stores aren't possible, so a halfword read-modify-write, offset from
  `PSRAMBase_S98`, page `offset>>23`). Picks, the screen, folders, `.cdx`/`.cdc` caches, Details are
  format-agnostic.
- 🟢 In game, GB/GBC/NES have no Torii overlay: `ingame_prepare` (the IRQ-hook menu, `source/ingame.c:331`)
  is called only in the GBA addon launch (`ezkernelnew.c:~2040`), and the emulators run their own L+R menu
  (`docs/research/ingame-menu.md:35`). **Picks are launch-time only**; no toggling in game.
- 🟢 libretro code strings, read from the database (2026-11-02; the repo's corpus holds only GBA):
  - GB: `FA0-999-4C1` (Game Genie, 9 digits), `09C-56B-E6E+09C-74A-E6E` (two lines joined by `+`),
    `01995EDB` (GameShark, 8 hex). Names carry the device in the file name, as GBA's do.
  - NES: `0754:00+0756:01` and `079E:07` (raw `AAAA:VV` RAM pokes), `AAZUAG`, `ZZZSSO` (Game Genie, 6
    letters), `NAPKIN`, and 8-letter ones with a compare. No `cheatN_format`.
  - 🔴 Counts per kind, 6-digit GB genies, `AAAA?CC:VV` compare forms and which GameShark type bytes
    occur: unmeasured. Stage 0 below is a corpus tool (`just cheat-corpus` for GB/GBC/NES) that fetches
    the three folders (CC-BY-SA-4.0, git-ignored in `dist/`, like GBA's) and counts them.

## 1. Code kinds

| Kind | Form | Meaning | Kind of work |
| --- | --- | --- | --- |
| GB GameShark | `TTVVLLHH` (8 hex) | write VV at HHLL every frame; TT is the type/bank byte | RAM poke |
| GB Game Genie | `XXX-YYY-ZZZ`, or `XXX-YYY` | ROM byte patch at an address, 9-digit form with a compare byte | ROM patch |
| NES raw / Rocky | `AAAA:VV` | write VV at CPU address AAAA every frame | RAM poke |
| NES Game Genie | 6 or 8 letters of `APZLGITYEOXUKSVN` | patch the byte at 8000-FFFF; 8 letters add a compare | ROM patch |

- 🟢 GB GG decode, ported from mGBA (`emu/mgba/src/gb/cheats.c:67-100`, MPL-2.0 like `cheatcode.c`):
  address = `((op1&F)<<8) | ((op2>>4)&FF) | (((op2&F)^F)<<12)`; value = `op1>>4`; compare: third group
  `op3`, `((op3&F00)<<20 | op3&F)` rotated right 2, folded and XORed with `BA`. 6-digit has no compare.
  GB GameShark: mGBA reads `(op&FF)<<8 | op>>8&FF` as the address and byte 2 as the value, and ignores the
  type byte (`:55-64`).
- 🟡 GameShark's type byte: `01` is a plain write; `90`-`97` select a CGB WRAM bank for D000-DFFF; other
  values touch cartridge RAM banks. Check against EnHacklopedia and the corpus before deciding what to
  refuse. Proposal: accept `01` (and `00`), `90`-`97` with the address in D000-DFFF; anything else is
  "unreadable" until the corpus says it matters.
- 🟡 NES GG letters (A P Z L G I T Y E O X U K S V N = 0..F) and bit shuffle, as FCEUX and nesdev document:
  address = `0x8000 | (n3&7)<<12 | (n5&7)<<8 | (n4&8)<<8 | (n2&7)<<4 | (n1&8)<<4 | (n4&7) | (n3&8)`;
  value = `(n1&7)<<4 | (n0&8)<<4 | (n0&7) | (n5&8)` (8 letters: `| (n7&8)` instead of `n5&8`, compare =
  `(n7&7)<<4 | (n6&8)<<4 | (n6&7) | (n5&8)`). Written from memory: the host test must check every
  shuffle against a second implementation (fceumm or nestopia source, as `tools/cheat_oracle.c` checks
  GBA against mGBA) and published pairs, before any of it ships.
- NES raw: addresses 0000-07FF (RAM, mirrors fold to 0000-07FF) and 6000-7FFF (PRG-RAM); others refused.
  🟡 `AAAA?CC:VV` (compare form) if the corpus has any: decode it as a poke that runs only while the byte
  equals CC (stage 2 can refuse it).
- Refused, shown greyed as "can't run here" (existing reason): GB GameShark to ROM or I/O; Game Genie
  and raw codes mixed across consoles; Torii code (`cheatN_format = "Torii"`) and every GBA format: their
  addresses are the GBA's. Format lines (`cheatN_format`) are ignored for these consoles, the shape of
  each line says its kind.

## 2. Where each applies

### 2a. ROM patches: in the kernel, before launch, no emulator change

- 🟢 The game sits unmodified in PSRAM after `LoadEMU2PSRAM`; the kernel can write it with the same
  halfword read-modify-write as `rom_patch`. 🟢 Both emulators read it from there: Jagoomba's
  `make_instant_pages` points every bank at `romstart + 16384*i` and copies bank 0 to VRAM at start
  (`emulators/jagoombacolor/src/cache.c:30-60`; its cache path is compiled out, `MOVIEPLAYER 0`,
  `config.h:22`); PocketNES copies its first 128 KB into EWRAM and caches the rest from PSRAM
  (`USE_ACCELERATION`, `loadcart.c`), both after the kernel's patch. So a patch made before `Launch_now` is
  what runs. The kernel's PSRAM write path (`SetPSRampage`, `PSRAMBase_S98`, 16-bit stores) is the one in
  `rom_patch`; it needs a base offset argument (the game's `rom_start_address`, plus 16 for NES's iNES header).
- Byte patches: `struct cheat_patch` already has a mask, so one byte is `value<<8|v, mask 0xFF00` or the
  low half. 🟢
- **GB bank semantics.** A real Game Genie patches the bus: address 0000-3FFF hits bank 0; 4000-7FFF hits
  whichever bank is mapped, and the compare byte exists to tell banks apart. A static patch makes the
  same choice at launch: for 4000-7FFF, patch the byte in every bank `b >= 1` (offset
  `b*0x4000 + addr - 0x4000`) where the ROM byte equals the compare (every bank for a 6-digit code, as the
  device would hit whichever is mapped). Bank count from the ROM size (`0x148`) or the file size. Cost:
  up to 511 patches a line; cap 2,048 patches a launch (a cheat over it is "too big").
- **NES bank semantics.** The same, by mapper: the CPU address picks the byte inside a PRG slot, so a
  patch goes to every PRG offset `p` whose low bits equal the address's under the mapper's slot size, with
  `byte == compare` when one is given. Slot sizes: mapper 0/3 one fixed map (16 KB mirrored or 32 KB), 1
  and 2 16 KB (the fixed last bank only for C000+), 4 8 KB, 7 and 66 32 KB. Unknown mapper: refuse 6-letter
  codes, accept 8-letter ones (the compare picks the byte) at 8 KB slots. The mapper is in iNES byte 6/7
  (and NES 2.0's bits). 🔴 Which mappers PocketNES runs that this table lacks: list them from
  `emulators/pocketnes/src/mappers`.
- 🟡 The two emulators' `checksum()` (Jagoomba `sram.c:262-335`, PocketNES `sram.c`) samples 128 words at
  a 128-byte stride from the ROM start to key its states, settings and palette. A patch in the first 16 KB
  on a sampled word changes the key: with that cheat on, the game's state and palette are "another
  game's". About 3% of patches there. Mitigation: the kernel doesn't patch those four bytes when
  it finds a sampled word (refuse the code with a reason), or Torii-patches the emulators to checksum a
  fixed copy. Decide with the first test of it.
- GBC games cached by Jagoomba in VRAM (`bank_1`) hold the patched bank 0 for the same reason. 🟢

### 2b. RAM writes: every frame, by the emulator

Both need the emulator to carry a table. **The kernel places it in the emulator image**: a 1 KB const
block in each emulator's `.rodata`, which the kernel finds in its own copy and writes in PSRAM after
`LoadEMU2PSRAM`'s copy. Reasons: 🟢 the emulators run in place from PSRAM (`.text` at `0x08000000`), so the
table is readable at once with no EWRAM, VRAM or SRAM; IWRAM is full in both (Jagoomba `.iwram`+`.bss`
end `0x03007E9C`, PocketNES `0x03007CA4`, of `0x03008000` less the stacks), EWRAM is carved from the
emulators' own layout (Jagoomba `END_OF_EXRAM 0x02029214`, PocketNES `0x0203B98C`, both used as caches
and buffers); and no kernel-to-emulator call convention exists to hang it on.

```c
// emulators/*/src/torii_cheats.c, EWRAM_BSS-free, in .rodata
struct torii_cheats {
    char magic[8];    // "TORIICHT"; the kernel scans the image for it (4-byte aligned), 0 or 2 matches = a bug
    u32  version;     // 1
    u16  count;       // entries used
    u16  max;         // 64: the emulator never reads past it
    struct { u8 kind; u8 bank; u16 addr; u8 value; u8 pad[3]; } e[64];   // 8 bytes each: 4 + 8 + 512 bytes
};
```

- `kind` 1 = poke. GB: `addr` is the CPU address, `bank` the WRAM bank (0 = whatever D000 maps to, 1-7).
  NES: `addr` is the CPU address already folded (0000-07FF, 6000-7FFF), `bank` 0.
- Kernel side: `emulator_cheat_slot(is_EMU)` (new, `source/emulators.h`) scans the embedded image for the
  magic once and returns its offset, 0 for an image without one (tests built with an older emulator
  keep working); then 16-bit stores into PSRAM (`PSRAMBase_S98 + off`), the emulator already in place.
  The kernel's copy in ROM stays zero (`count 0`).
- 🟢 **PocketNES.** `do_cheats` already runs once a frame, in `timeout.s:247-256`, but only when
  `num_cheats != 0`, and it writes `NES_RAM[addr]` for `addr < 10240` (RAM at 0, PRG-RAM at 0x800 =
  `$6000`; `real_address` in `cheat.c`). The user's own list sits in `cheatfinder_cheats` (50 cheats,
  3 bytes + 15 of name, loaded from a CHEATLIST state in cart SRAM by `cheatload`; `loadcart.c:408-414`),
  carved from the end of the cache and dropped when it doesn't fit (`setup_cheatfinder`). Do not feed
  Torii's picks into it: it is saved back to SRAM by `cheatsave` and editable in the emulator's menu.
  Patch instead (`Torii:` marks, `just emulators-save-patch pocketnes`): a second loop over the ROM table
  in `do_cheats` (the same writes), and the `timeout.s` test becomes `num_cheats | torii_count`.
  Mapping: `addr & 0x7FF` for `< 0x2000`; `0x800 + (addr - 0x6000)` for 6000-7FFF.
- 🟡 **Jagoomba.** (The plan; built otherwise: the hook is in lcd.s's `newframe_vblank` and, since 2026-11-15, in `.vram1`, as the Status says.) No cheat code exists. Hook: the GB frame's vertical blank in `timeout.s`, right after
  `bl newframe_vblank` (`timeout.s:~218`, once a GB frame: `FF41_R_vblank`, `line145_to_end` follow), a
  call into a new C function `torii_run_cheats()` in `.text` (ROM, not IWRAM: at most 64 entries of about
  20 cycles is 1,300, under 0.5% of the 280,896-cycle frame; `.vram1` has 1.3 KB free if it ever needs to
  be fast). Writes:
  - C000-CFFF: `XGB_RAM[addr - 0xC000]` (`XGB_RAM` `0x03005990` since hi-colour, 8 KB, `gbz80.s:2858`);
  - D000-DFFF: bank 0 or 1 → `XGB_RAM + 0x1000 + (addr-0xD000)`; bank n ≥ 2 → `GBC_EXRAM +
    (n-2)*0x1000 + (addr-0xD000)` (`GBC_EXRAM 0x0202E000`; `io.s:676-707`, `_FF70W`);
  - E000-FDFF: the echo, folded to C000-DDFF; FF80-FFFE: `XGB_HRAM + (addr-0xFF80)` (`0x03007890`);
  - A000-BFFF (cart RAM) refused at decode: the writes go through the SRAM mirror (`sram_W_torii`,
    `memory.s:115`) and the autosave bookkeeping; revisit with a real code that needs it.
  - 🔴 Whether the call is safe from that point in the asm loop: the C ABI keeps r4-r11, but r0-r3, r12 and
    lr, and the CPU flags that the loop keeps live (`gb_flg`, `cycles`), have to be checked in
    `timeout.s:200-260` before writing it (the call is wrapped in a `stmfd/ldmfd` of what the loop owns).
    This is the stage's biggest unknown. A GB frame runs per emulated VBlank, so with VSync off it runs
    faster than 60 Hz and the cheats follow it, as a real GameShark's do.
- EWRAM: 0 bytes in both (table in ROM). Image growth: about 0.5 KB table + 0.2 KB code each; the
  kernel's area is 70% full (`CHANGELOG` v0.6.0).
- Saved states in the emulators' own menu don't touch the table; a LOAD STATE puts back RAM and the
  next frame's poke overwrites it, as a cheat device does.

## 3. The kernel side

New files (all hardware-free, host-testable like `cheatcode.c`):

| File | Job |
| --- | --- |
| `source/cheatemu.h`, `cheatemu.c` | decoders `cheatemu_decode(console, code, len, out)`: GB GameShark, GB Game Genie, NES GG, NES raw; output `struct emucheat {u8 kind; u8 bank; u16 addr; u8 value; u8 compare; u8 has_compare;}` list. Ports mGBA's GB lines (MPL-2.0 header as `cheatcode.c`). Letters, hex, `+` joins via the existing `cheat_plus_joins`-style line split. |
| `source/cheatemu_apply.c` | `emucheat_plan(list, console, rom_info, out_patches, out_pokes)`: GG → byte patches (the bank/mapper rules in 2a, given the ROM's size, GB `0x148`, NES mapper), poke table of ≤ 64. Host test with a synthetic ROM image. |
| `tests/host/test_cheatemu.c` | decoders, shuffles, banked patches, caps, folded addresses |
| `tools/cheat_emu_corpus.c` (or an extension of `tools/cheat_corpus.c`) | decode all of libretro's GB/GBC/NES files and report counts per kind and refusals: stage 0 |

Wiring:

1. `struct cheatfile` gets `console` (2 bits, set by `cheatstore_read(rom_path)` from
   `library_console_of(path)`); `cheatfile_decode` branches on it: for GB/GBC/NES it calls
   `cheatemu_decode` and fills `w->prog` with the 8-byte records as words (so `bytes` and the `5 KB`
   picked budget count them, `seen_before`/`SAME` hashes still work), `w->patches` is not used. The
   status mapping reuses `CHEATFILE_OK/UNREADABLE/CANT_RUN/BIG`. 🟢 `cheatfile.format` has 3 bits
   (`cheatfile.h:130`); nothing new is needed there since format lines are ignored.
2. A launch pass for emulators: `cheatstore_launch_emu(rom_path, console)` (new, beside `launch_pass`,
   `cheatstore.c:2099`) decodes the picks only (`launch_wants`) and fills `static struct emu_launch
   {u16 n_pokes; u16 n_gg; struct emucheat e[..]} EWRAM_BSS` (1 KB of EWRAM, which has about 11 KB left;
   if the 6 KB `kept` store is easier, page `0xF0` is free in an emulator launch: no `ingame_prepare`).
   It is called from `action_launch` (`actions.c:188`) for `game.is_emu` (no "clean" for them: a pick is
   the opt-in; there is no global cheats switch to honour, `gl_cheat_on` is "any pick").
3. `Game_boot`'s emulator branch (`ezkernelnew.c:1887`), after `LoadEMU2PSRAM` returns 0: `emu_cheats_write(
   is_EMU, rom_start, filesize)`: apply `emucheat_plan` (needs the ROM header bytes it already read: GB
   `0x147/0x148`, NES iNES flags at the game's start), do the byte patches with a `rom_patch` taking a base,
   write the poke table into the slot. Log `launch: cheats: N pokes, M ROM patches written` (`tlog`).
   `LoadEMU2PSRAM` must return `rom_start_address` (now local).
4. Compare bytes read from PSRAM before patching: PSRAM reads need the page set as `rom_patch` does.
5. `cutpoint`/power-cut: nothing new is written to the card; the pick save path is unchanged.
6. `tests/loader_safety.sh`: no kernel-NOR writes are added. 🟢 (PSRAM only.)

## 4. UI changes

- 🟢 Popup: `gba &&` at `games.c:1498-1513` becomes "GBA, GB, GBC or NES, not in NOR". `cheatstore_peek`
  counts the file's cheats (`counts.txt`); it is console-agnostic once `cheatstore_read` takes the console.
- `cheats_run(rom_path, title, icon)` derives the console from the path; the screen, folders, picks,
  guides, Buttons and rename are format-agnostic. What changes inside it:
  - **Buttons (Hold/Press, `cheatN_keys`)**: GBA keys; for emulators it's meaningless (the poke runs
    every frame). Hide the Buttons row and the tag for GB/GBC/NES.
  - **Details / Guide**: the code lines use `cheatcode`'s formatters; add a formatter for emucheat lines
    ("Write 99 at C507", "Patch 4C1: ROM 0x1234 = FA if 0x99"), and don't show the GBA "cycles/ROM
    patches" budget; show "N pokes" and "M ROM patches" instead.
  - **Budget text**: picked total of 64 pokes and 2,048 patches (the existing "too big" greyed reason).
  - **Code keypad** (`source/screens/keypad.c`, `keypad_edit.c:341`: `cheat_detect` shapes and the
    `[ ] + ^ |` row): uses GBA shapes; `+ Add code` and "Edit code" are **hidden for emulator consoles in
    stages 1-3** (codes come from files). Stage 4: a console-aware alphabet (hex and `-` for GB; hex and `:`
    or the 16 genie letters for NES) and `cheatemu_detect()`. Typed codes are written as `cheatN_code`
    with no format line.
  - Info's Cheats page and `lang/en.ini` line 612 say "A GBA game's cheats"; reword (and the Swedish).
  - New strings for refusal reasons only if the existing ones don't fit (`en.ini` and `sv.ini` both).
- Torii code: GBA-only; picking it in a GB file is "can't run here".

## 5. In-game menu

Confirmed above: no Torii overlay in GB/GBC/NES games (`ingame_prepare` is GBA-only; the emulators' own
L+R menu is the in-game menu). Picks are launch-time only; Back to Torii from the emulators' menu doesn't
file cheat changes; "Cheat search" doesn't exist for them. State this in Info's Cheats page.

## 6. Testing

- **Host** (`just test-host`): `tests/host/test_cheatemu.c`: every GB GG vector (mGBA's decode as the
  oracle through `tools/mgba_cheats.sh`, which already builds mGBA's GBA decoders; extend for
  `src/gb/cheats.c`), NES GG against a second implementation, the plan's bank and mapper rules on
  synthetic ROMs (8 banks, compare matching one, 6-digit all), cap and refusal cases, fuzz as
  `cheat_fuzz.py`.
- **Test ROMs** (written for Torii like `tests/gb_rom.py` and `nes_rom.py`; no third-party code):
  - `tests/gb_rom.py cheat`: MBC5, 4 banks, no RAM. Proof of a **RAM poke**: each frame the program
    loads `[C100]` and sets the background palette to red if it is `$77`, else black; the cheat
    `017700C1` (type 01, value 77, address C100 low byte first) turns it red. Proof of a **ROM patch**:
    bank 1 at 4000 holds `LD A,$11; RET`; the program calls it and goes red when A is `$22`; the Game
    Genie changes the immediate (with compare `$11`); bank 2 holds a different byte at the same offset
    to show the compare leaves it alone.
  - `tests/nes_rom.py cheat`: NROM-256 (and a UxROM variant for banks). RAM: a loop reads `$0300`, red
    backdrop if `$55` else black; the cheat `0300:55` (raw) proves PocketNES's table; ROM: a `LDA #$10`
    byte in PRG changed to `$20` by an 8-letter genie, compare `$10` (letters computed by the host test).
  - The screen colour is checked as `tests/nes.sh` does (`png_lines`, count red pixels).
- **New** `tests/emu_cheats.sh` (add to `tests/all.sh` beside `nes.sh`/`gb_save.sh`, lines 103-107): per
  console builds the SD card with the ROM and its `.cht` (`/torii/cheats/<path>.cht`) with the pick as
  an enable line (`cheatN_enable = true`, as `tests/cheats.sh` writes), launches as `nes.sh` does
  (`nav_launch`), screenshots, checks red and the log lines. Cases: poke on, poke off (black), ROM patch
  with and without a matching compare, a refused code (log says why, game starts anyway), a banked
  code, 8 picks.
- **Golden shots** (`tests/shots/*.opts`): the cheat screen for a GB file and a NES file (popup row,
  list, Details with the new formatter); Swedish variants as the rule in `CLAUDE.md` asks. Perf
  budget unchanged (no menu change).
- **compat** (`tests/cheat_codes.sh`, `cheat_codes.toml`): it reads real codes for owned GBA games. Add
  `[[code]]` entries with `console = "gb"`/`"nes"` for owned ROMs (read-back of a ROM patch from PSRAM,
  and a RAM value after N frames via `-m`), skipped when the ROM is missing. A Pokémon Red/Yellow GB
  `D` bank-switched code and a banked NES (MMC3) Game Genie are the useful two. `just check` already
  plays only games that ran a changed function; the new launch code is in `Game_boot`, so it plays them.
- **Emulator patches**: `just emulators` builds; `just repro` must still pass (no timestamps in the new
  tables); a kernel test (`tests/kernel`) asserts exactly one `TORIICHT` in each image and `count == 0`
  in the shipped one.
- **Risks to test explicitly**: the sampled-checksum collision (2a), a patch in Jagoomba's VRAM bank
  copy, a poke while the emulator's menu is open (the frame doesn't run), a state load.

## 7. Cost and plan

| Stage | Ships | Files and functions | Emulator patch |
| --- | --- | --- | --- |
| 0 | counts only | `tools/cheat_emu_corpus.c`, `just cheat-emu-corpus`; settles GameShark type bytes, 6-digit genies, compare forms and mapper mix | none |
| 1 | **ROM-patch Game Genie for GB, GBC and NES**, picked in the existing screen | `cheatemu.[ch]`, `cheatemu_apply.c`, `cheatfile.c` (console branch, ~60 lines), `cheatstore.c` (`cheatstore_launch_emu`, `rom_patch` with a base), `actions.c`, `ezkernelnew.c` (return `rom_start`, write after load), `games.c` (popup gate), `cheats.c` (hide Buttons, Add/Edit code; details formatter), `lang/en.ini`+`sv.ini`, host test, `tests/emu_cheats.sh`, two test ROM modes, goldens, CHANGELOG, docs | none |
| 2 | NES raw pokes | `cheatemu` NES raw; `emulators/pocketnes/src/torii_cheats.c` (table), `cheat.c` (`do_cheats` second loop), `timeout.s` test; `emulator_cheat_slot()`; `emulators-save-patch pocketnes` | small (about 40 lines) |
| 3 | GB GameShark pokes | `emulators/jagoombacolor/src/torii_cheats.c`, `timeout.s` hook, `gbz80.s` globals; `emulator_cheat_slot()` shared with stage 2 | medium (the asm call) |
| 4 | typing codes on the cart | `keypad.c`, `keypad_edit.c` (console alphabets, `cheatemu_detect`) | none |

Size: stage 1 about 5-6 files new or changed with ~900 lines of C and tests; stage 2 about 150; stage 3
about 200; stage 4 about 300. The kernel area (1 MB, 70% full) takes it; EWRAM +1 KB.

**Biggest risks**
1. 🔴 Jagoomba's frame hook (stage 3): register and flag use around `timeout.s:218`; wrong, it corrupts
   timing or crashes. Spike it with a one-poke build and `just compat`'s per-mapper GB games first.
2. 🟡 Banked ROM patches differ from a device's per-bus patch: 6-digit codes on banked mappers patch
   every bank (over-patching code that isn't the game's), and mapper slot sizes are my table. Mitigate
   with the compare byte and by refusing unknown mappers.
3. 🟡 The emulators' sampled checksum changes with a patch in the first 16 KB (states and palette keyed
   on it).
4. 🟡 NES and GB GameShark/GG decode details are from memory of FCEUX, nesdev and EnHacklopedia: do not
   ship before the oracle test passes. mGBA's GB decoders cover GB only.
5. 🔴 Nothing is measured about libretro's GB/NES files (kind counts, odd forms): stage 0 first, so the
   decoders' scope follows the data and not this document.
6. The emulators run unlicensed upstream code (`roadmap.md`: ask Jaga and Dwedit for a licence): more
   patches to them add to that open point. Keep each marked `Torii:`.
7. Hardware: none of this has met a real cart. PSRAM writes by the kernel of a ROM it just wrote are the
   same path GBA patches use; the emulator's table read is ordinary ROM. Cart-test stage 1 first.
