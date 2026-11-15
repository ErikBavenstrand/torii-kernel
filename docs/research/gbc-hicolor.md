# Hi-colour GBC games in Jagoomba Color

Status (2026-11-14, Unreleased, 3c49daae): built. Jagoomba replays
mid-frame BG palette writes line by line. The first half of this note is
what was built and what it costs, the second the original analysis
(2026-10-26, v0.7.1).

## The problem

jagoombacolor#20 and #53. Some GBC games rewrite the background palettes
during the frame, usually from an HBlank handler, to show more than 32
colours at once. The issues name Alone in the Dark, Crystalis (title),
Donkey Kong Country (title; in `roms/`), Tomb Raider, V-Rally '99 and Cannon
Fodder, plus hi-colour demos. In Jagoomba these screens looked patchy:
every line got the palettes as they stood at line 75 (where `timeout.s`
copies `gbc_palette` to `gbc_palette2` for `transfer_palette_`).

## What was built

All in `emulators/patches/jagoombacolor.patch` (`lcd.s`, `timeout.s`,
`equates.h`, `gbz80.s`), each change marked `Torii:`.

- **Recording.** `FF69_W` (BCPD) now compares the byte with the one it
  replaces and, when it changes, falls through to `hc_record` (IWRAM). That
  works out the line the colour first shows on (`scanline`, plus one in
  HBlank, as `newmode` does for the scroll buffers), drops writes from line
  144 on (the next frame's line 0 picks them up), and appends one word:
  `line << 24 | offset << 16 | colour`, where `offset` is the colour's place
  from `0x05000100` (GB BG palette p, colour c: `p * 32 + c * 2`) and
  `colour` is already gamma-converted. The colour's other byte, written
  in the same line, overwrites the same entry rather than adding one.
  Gamma goes through a 32-entry table (`HC_GAMMA`, one converted grey per
  level, made by `gamma_convert_one` when the setting changes), three
  lookups instead of three `gammaconvert` calls.
- **Line 0.** `hc_frame_start` (`timeout.s`'s `line0`) copies the 64 bytes
  of BG palettes into the recording list as its base and rewinds it.
- **Buffers.** Three lists in EWRAM (`HC_BUF1`-`3`, `equates.h`), each the
  list's end, the line 0 palettes (64 bytes), a guard word, 768 entries and
  an end mark: 3,148 bytes each. One records, one is on the screen, and the
  third is ready for the next VBlank or spare. `hc_swap` hands the finished
  list over where `bufferfinish` swaps the scroll buffers (GBA VBlank IRQ
  off); a list that wasn't shown yet is dropped for the newer one, as the
  scroll buffers are. Two buffers aren't enough: when a GB frame ends after
  the GBA's VBlank already came, `bufferfinish` doesn't wait, and the next
  frame records into the list still on the screen (the test ROM showed a
  half-written colour for a few lines that way).
- **Replay.** At each GBA VBlank, `hc_vblank` (from `display_frame`) takes a
  ready list, and if the list on the screen has entries (and the game is in
  GBC mode, unmasked, with the screen on) writes its line 0 palettes as
  `transfer_palette_` does (8 bytes every 32 from `0x05000100`, colour 0 to
  its solid tile at `0x050000F0 + p * 2`, gamma-converted), points
  `hc_cursor` at the first entry and sets `hc_active`. `do_gba_hdma` (the
  VCOUNT IRQ at line 7) then turns on DISPSTAT's HBlank IRQ with the rest,
  and `end_gba_hdma` (line 151) turns it off. `irqhandler` checks HBlank
  first and branches to `hc_hblank`, which applies every entry for line
  `VCOUNT - 7` or earlier (GB line L shows on GBA line L + 8; Jagoomba has
  no scaled modes, so the map is that one offset) and returns to the BIOS
  without the dispatcher's mode switch. IE has the HBlank bit on all the
  time; DISPSTAT is the switch. A frame without entries costs a compare at
  VBlank and nothing else; DMA0-2 stay the scroll buffers', DMA3 stays free.
- **Memory.** EWRAM: 3 x 3,148 + 64 + 48 = 9,556 bytes, plus `gbc_palette`
  and `gbc_palette2` (256 bytes) moved there from IWRAM so the IRQ code fits.
  `END_OF_EXRAM`, the top of the heap `sram.c` borrows for saves, went from
  `0x02029214` to `0x02026BC0`; its largest use, compressing 32 KB of SRAM
  (`ewram_start + 0x1E000` plus a 48-byte header and the worst LZO output
  of `0x8843`), ends at `0x0202693F`, 641 bytes below. IWRAM: none net:
  `CANARY2` and the bss end are where they were (`0x03007E78`), so the user
  stack keeps its room (`hc_record` pushes 12 bytes). VRAM code
  (`.vram1`): `hc_init`, `hc_frame_start`, `hc_swap`, `hc_vblank` and the
  gamma helpers.

## Results

- **Donkey Kong Country** (`roms/`): before, the title's logo was yellow
  instead of red and the octopus's tiles sat in boxes; after, its title and
  attract screens look the same as mGBA's GB core running the ROM
  (`mgba-shot` runs a `.gb`/`.gbc` in mGBA's own core). It writes 38
  colours a frame at most.
- **`tests/gb_hicolor.sh`** (in `just test`): `tests/gb_hicolor_rom.py`
  builds a GBC ROM that, in double speed, rewrites BG palette `LY mod 8`
  with that line's four colours from its STAT HBlank interrupt: 576 colours
  a frame, every one mid-frame. Before, every line showed the line 75
  palettes (2,660 of 2,860 8-pixel pieces differed from mGBA's); now none
  do, with Jagoomba's line timing allowed for (`tests/gb_hicolor.py`: its
  STAT interrupt comes as a line starts, so a colour can show a line or two
  early), at gamma 0 and at gamma 2 (mGBA's colours converted through
  `HC_GAMMA`). It also checks `HC_MAX` (576) and `HC_FULL` (0).
- **None of the other hi-colour games named in the issues is in `roms/`**,
  so Alone in the Dark, Crystalis, Tomb Raider, V-Rally '99 and Cannon
  Fodder are untested.

## Cost

Measured with VSync off (`novblankwait_` poked to 1 after launch, Jagoomba's `frame` read 600 GBA frames apart:
GB frames in 600 GBA frames, 600 = full speed), from 1,300 frames after
launch:

| Game | Before | After |
|---|---|---|
| Pokémon Crystal | 1,209 | 1,201 |
| Link's Awakening DX | 2,262 | 2,240 |
| Super Mario Bros. Deluxe | 1,303 | 1,297 |
| Wario Land 3 | 1,878 | 1,858 |
| Kirby's Dream Land 2 (DMG) | 1,860 | 1,845 |
| Donkey Kong Country (title, 38 colours) | 995 | 943 |
| Test ROM (576 colours, from 600 frames) | 889 | 531 |

Games without mid-frame writes lose about 1%; with VSync on they run at
full speed as before. On a hi-colour frame, recording costs about 90 GBA
cycles a colour and the replay about 150 cycles a line (the BIOS's IRQ
entry and four entries); without the replay the test ROM ran 580. Recording
in VRAM instead of IWRAM ran it at 76% with VSync on; the IWRAM version,
88%.

## Limits

- 768 colours a frame; past that the rest wait for the next frame's line 0
  (`HC_FULL` counts such frames). Rewriting all eight palettes every line
  (2,304 colours, faster than a GBC can) would need about 18 KB more EWRAM.
- Jagoomba's STAT interrupt comes as a line starts, not in the HBlank before
  it, so a colour written from an HBlank handler can show a line or two
  early (Jagoomba's timing, not the replay's).
- The replay writes colours from the HBlank's start; a line with many
  entries can spill into the left border (40 pixels, about 160 cycles more)
  before the GB picture starts. The test ROM's four a line don't.
- OBJ palette writes (`FF6B_W`) mid-frame aren't replayed.

## The original analysis (2026-10-26)

- `FF69_W` in `emulators/jagoombacolor/src/lcd.s` (BCPD writes) stored the
  byte in `gbc_palette` and nothing else.
- `transfer_palette_` runs once per frame from `display_frame`, at VBlank. It
  copies `gbc_palette2`, with the gamma conversion if the setting is on,
  into GBA palette RAM from `0x05000100`: each GB palette's four colours go
  to the first four entries of a 16-colour GBA palette (GB BG palettes 0-7
  to GBA BG palettes 8-15, OBJ palettes to GBA OBJ palettes 0-7). So one GB
  palette set is 8 bytes every 32 bytes, not one block.
- `lcd.s`'s own TODO list has "Palette changes per line using vcount, may
  up to 4 palettes per frame total" and "Real HDMA"; the author's comment on
  #20 (2022-06-02) says the same: palettes reach the GBA only at frame end.
- The per-line effects Jagoomba has (scroll, `BGxCNT`, `DISPCNT`, `WIN0H`)
  go through `do_gba_hdma`, which starts three HBlank DMAs on DMA0, DMA1 and
  DMA2 from buffers filled during the frame. Only DMA3 is left, and one DMA
  can't write eight 8-byte pieces 32 bytes apart: hence the IRQ.
- Full 64-byte snapshots per line would have taken 18 KB double-buffered;
  the change list is smaller for every game seen.
