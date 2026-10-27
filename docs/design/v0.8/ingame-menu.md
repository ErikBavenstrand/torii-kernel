# v0.8 in-game menu: the sprite overlay

The design for v0.8's in-game menu (roadmap, *v0.8 In-game menu*), from the review of today's
menu in [docs/research/ingame-menu.md](../../research/ingame-menu.md). It is built in phases:
P0 proves what the cart does in game mode, P1 replaces today's Mode 3 menu with the overlay
and its first actions, P2 adds per-cheat toggles, P3 save-state slots, P4 screenshots and P5
the In-game menu setting.

## Architecture

The overlay runs from borrowed EWRAM and is stored in cart SRAM, not in the ROM tail.

- **The stub.** Only a small stub, `ingame_stub`, stays in the patch area at the ROM's end
  (`source/gba_rts_patch.s`), in place of `ingamemenu_now`: about `0x300` bytes of its own.
  The hook (`RTS_irq`) calls it on the menu key as it called the old menu.
- **The image.** The kernel writes the overlay's image into cart SRAM at each launch
  (`source/ingame.c`, `ingame_prepare(launch)`, beside `GBApatch_PSRAM` and the NOR launch).
- **Opening.** The stub backs up an EWRAM window (at most 24 KB) to SRAM, copies the overlay
  in, switches to a stack inside the window, runs it, and restores everything.
- **The overlay** is Thumb C, linked at a fixed EWRAM address (`source/overlay/ov.ld`), built
  into `overlay.bin` and embedded in the kernel with `.incbin`, as `source/emulators.s` embeds
  the built-in emulators.

### Cart SRAM layout

Byte offsets in the cart's 1 MB SRAM. The page register (`0x09C00000`) counts 4 KB pages and
`0x0E000000` shows 64 KB from the page set (docs/research/omega-de-hardware.md, *Save and
work RAM*), so page `0x20` is offset `0x20000`.

| Offsets | Holds |
|---|---|
| `0x00000-0x1FFFF` | the game's save |
| `0x20000-0x3FFFF` | the overlay's backups: OBJ VRAM (16 KB), OAM (1 KB), OBJ palette (512 B), the EWRAM window (24 KB), registers |
| `0x40000-0xAFFFF` | the save-state buffer (unchanged) |
| `0xB0000-0xB7FFF` | the overlay's image |
| `0xB8000-0xBFFFF` | the session block, kernel to overlay and back |
| `0xC0000-0xD8FFF` | the screenshot dump (P4): `struct ingame_shot`, the palettes, OAM, VRAM |
| `0xF0000` | the cart log (`source/cartlog.h`) |

### The ABI

`source/ingame_abi.h`, hardware-free so the host tests can include it:

- **In:** a magic, a version, the ROM's CRC, the theme's palette (16 colours from the UI
  theme), the hotkeys, the launch's value of `0x096A0000`, the slot table and the cheat records.
- **Out:** a pending state, a screenshot flag, the cheat mask and the action picked.
- **The lines' widths** (`text_width`) sit in the head the overlay reads at opening, so it
  lays out the menu without reading each line from cart SRAM.

## Drawing

Sprites only, never a BG layer: a BG's scroll and the `BLDY`, `WIN` and `MOSAIC` registers are
write-only, so a menu on a BG can't give them back (today's BG2 affine bug).

- **While open:** back up OAM and hide the game's sprites; `DISPCNT` gets OBJ on, 1D mapping
  and the window bits off. The BGs stay visible but frozen, dimmed by a semi-transparent dark
  sprite through `BLDALPHA` (`BLDCNT` and `BLDALPHA` are readable; restore them).
- **Tiles:** the panel's text goes in OBJ tiles 512-1023 (`0x06014000-0x06017FFF`), valid in
  every mode, bitmap modes included.
- **Text:** rows of 32x16 sprites, drawn by a proportional 1bpp-to-4bpp blitter ported from
  the engine's body font (`source/engine/font*.c`).
- **Look:** the theme's colours, clean and modern: a rounded panel, a selection pill and a
  title, within the sprite budget.

## IRQs, DMA, sound and input

- Run in IRQ mode with IME 0, polling `VCOUNT`, as today.
- Clear and restore the DMA0-3 `CNT_H` registers, as today.
- Mute through `SOUNDCNT_L` and `SOUNDCNT_H` (readable), not the `SOUNDCNT_X` master switch:
  zeroing it resets the PSG, which is today's noise bug.
- Edge-detect the keys with auto-repeat; on opening, wait for the menu key's release; on
  closing, wait until every key is released.
- Set `WAITCNT` for the SRAM copies, and restore it: `0x4017`, SRAM 8 cycles and ROM WS0 3/1
  with prefetch, never faster than games run in game mode (the stub's own code runs from the
  game's PSRAM).
- **Fail safe** (after the real cart's black screen in Pokémon Emerald): read every backup
  back before changing anything of the game's, check the loaded image against the check word
  the kernel writes in its header, and treat an answer out of range as Resume. On a failure
  the game goes on untouched and a record in page `0xF0` (`struct ingame_refused`) tells the
  kernel's next boot why.

## P1: the menu

- **Resume.**
- **Reset game** (new): SWI 0 with the ROM page left as it is, so the patched game restarts
  without the kernel. Verify it on the emulated cart.
- **Sleep.**
- **Back to Torii:** today's `reset_now`.
- Today's SAVE, LOAD and CHEAT ON/OFF keep working until P2 and P3 replace them, as entries
  backed by today's code: no regression for users.

## P2: cheats in game (built)

- **The list.** The menu's Cheats row opens a list of the game's cheats, each with a check
  box, under "All off" and "All as launched"; LEFT and RIGHT page through it. Its footer
  says why the row under the cursor can't change, else that the toggles are kept.
- **What the kernel writes.** `cheatstore_launch` lists every pick, then other cheats in
  file order, up to 64 (`INGAME_CHEATS`), each with its name drawn as a line, its cycles,
  its number and index in the file and a reason it can't change (`enum ingame_why`): it
  patches the ROM (off: pick it in Torii; on: on until Back to Torii), Torii can't run it,
  or the table had no room. It writes them to cart SRAM before the game loads, since the
  names are in PSRAM.
- **The table.** The listed cheats that weren't picked go in the interpreter's table after
  the picks, off: the "extras". They share the table's 0x1400 bytes with the picks (EWRAM
  has no room for more), take one of the 64 toggles each, and each costs
  `CHEAT_COST_SKIP` (100 cycles) a frame while off; extras are left out, the last first,
  until the picks all on and the extras all off fit `CHEAT_CYCLES_MAX`. When the patch
  area can't take them (a 32 MB ROM's cut area), the extras go first, the picks stay.
- **The switches.** `cheatrun.s` runs cheat n unless bit n of (the launch's off mask XOR
  the toggles at IWRAM `0x03007FE8` and `0x03007FEC`) is set. The off mask (8 bytes) sits
  between the interpreter and the table. The toggles are past the BIOS's SVC stack and
  before its words at `0x03007FF0`; SoftReset zeroes them, so Reset game (and a game's own
  soft reset) brings back the cheats as launched. A cheat past the 64th always runs.
- **Greying.** The overlay adds up the cycles of what is on (the call, every table cheat
  at its skip cost, and the on ones at their own) and greys a cheat that would pass the cap.
- **Back to Torii keeps the toggles as picks**, with no question: the cheat screen in
  Torii saves on B without asking too, and a question in game would be a step between the
  player and the game on every quit. The overlay writes the entries on to the session
  block's answer as it closes; the kernel's next boot (`ingame_after_game`) reads the
  cheat file again, checks each entry's number, and toggles the picks through
  `cheatfile_toggle` (drops first), then `cheatstore_write` (power-cut safe). After Reset
  game the answer is the launch's picks. A power-off in game loses the toggles, as cart
  SRAM past the save does.
- **Tests:** `tests/cheats_ingame.sh` (toggles take effect from the next frame, greyed
  rows stay off, the file after Back to Torii, the write cut at each cutpoint),
  `tests/kernel/test_cheatrun.c` (`test_cheatrun_off`), golden shots `ingame-cheats`.

## P3: save states with slots, tier A (built)

Tier A: the kernel files states; the FPGA's write-through of the old `/RTS` file (tier B)
and paging spare PSRAM from the menu (tier C) stay off until the cart probe says what works.

- **Slots.** Eight a save slot, `/torii/states/<rom path>/<save slot>/<n>.state`
  (`source/statestore.c`): a 64-byte head (two CRCs, a sequence, an order across the
  slots, the clock's stamp at filing), then the 448 KB buffer in 32 KB chunks packed in
  the BIOS's LZ77 (`lz77_compress_fast`, a hashed greedy packer). Power-cut safe as
  safefile's text files are, with no rename: each slot has `<n>.state` and `.bak`, a
  write goes to the copy a reader wouldn't take, its head last; the reader takes the valid
  copy with the higher sequence.
- **The buffer** (cart SRAM `0x40000-0xAFFFF`, the old layout) gets a tag at `0xAFFE0`
  (`struct ingame_state_tag`): its slot, whether the card has it, an autoload byte, and
  the CRC of the ROM path and save slot it belongs to. SAVE writes the flag last; LOAD
  checks it first (the old code's check was commented out).
- **In game.** Save state and Load state open a page of the eight slots, each with its
  time ("Slot 2 · 27 Oct 14:03"), "empty", or "not kept yet" for a state saved in game
  that the card hasn't. Only the slot in the buffer loads in game; the others say to load
  them from Torii. Saving into another slot replaces a state not kept yet, and the footer
  says so first.
- **Filing.** The kernel's next boot after Back to Torii files a state not kept yet under
  a progress dialog, then marks it kept. A power-off loses it, as cart SRAM past the save
  does (the menu says "not kept yet" until then).
- **Before a launch** with Save states on, the kernel puts the newest slot in the buffer
  (`ingame_states_launch`), so Load state works at once. An old `/RTS/<name>.rts` with a
  state's flag becomes slot 1, once (when the save slot has no states).
- **Load state in the game popup** (GBA list pages, Save states on) lists the slots,
  newest first, and starts the game with addons in the one picked: the kernel sets the
  tag's autoload byte, the patch's entry code puts a mark at IWRAM `0x3007FE4` from it,
  and the hook loads the state at the first VBlank, clearing both (Reset game starts
  clean). A game whose start code clears that word starts normally, the state in the
  buffer for Load state.
- **Speed** (emulated cart, hello): SAVE 18 frames and LOAD 24 (the stub's copies run
  in Thumb now), filing 180 frames for 55 KB packed, putting it back before a launch 57.
- **Tests:** `tests/states.sh` (round trip frame for frame, filing and the next launch,
  the popup's autoload, the filing cut at each cutpoint, the `/RTS` import); compat's
  "save state" column saves, plays and loads in every game; golden shots `ingame-states`.

## P4: screenshots (built)

- **The row.** Screenshot, after Cheats. A dumps the game's screen to cart SRAM
  `0xC0000-0xD8FFF` (`struct ingame_shot`, `source/ingame_abi.h`): the readable display
  registers as the overlay found them at opening (DISPCNT, DISPSTAT, VCOUNT, BG0-3CNT,
  WININ, WINOUT, BLDCNT, BLDALPHA), the BG palette and VRAM as they are, and the OBJ
  palette, OAM and the OBJ tiles the menu drew over from the stub's backups; the magic
  last. About 4 frames; the footer then says "Saved when you return to Torii". Another
  replaces it. The answer's `screenshot` flag tells the kernel.
- **At the next boot** (`ingame_after_game`, `source/shotstore.c`): under a notice, the
  kernel copies the dump into `pReadCache` (`READCACHE_SHOT`, all of it), draws it a line at
  a time with `source/ppu_render.c` and writes `/torii/screenshots/<game> <date> <time>.bmp`
  (240x160, 24 bits, top-down; the clock's time at filing, a number when there is no
  clock). About 200 frames for hello (75 drawing, the rest the card).
- **Power cuts.** No rename (FatFs's can leave two names on one chain): a journal
  (`/torii/screenshots/.writing`) names the file, which is written with a blank BMP head,
  its lines, then its head; the journal goes last. A cut loses the dump (cart RAM past the
  save has no battery), so the next boot's `shotstore_recover` deletes a file the journal
  names that isn't whole: the card has the screenshot whole or not at all. A write the card
  refuses keeps the dump and its name in cart SRAM, and the answer's flag, for the next boot.
- **The renderer** (`source/ppu_render.c`, host-built too): modes 0-5, text BGs at 4 and 8
  bits with every map size, flips and scroll, affine BGs wrapping or not, the bitmap modes
  through BG2's matrix (both frames), sprites plain, affine and double-size in 1D and 2D
  mapping, OBJ priorities as the hardware's (the first in OAM at the best priority), the
  windows (WIN0, WIN1, the OBJ window), alpha blending, semi-transparent sprites and the
  brightness fades. Left out: mosaic and anything changed mid-frame.
- **What can't be read back.** The BG scroll and matrices, BLDY, MOSAIC and the windows'
  bounds are write-only, and no shadow of them is general to games (each keeps its own, or
  none), so the dump has them as 0: a scrolled tile screen comes out shifted, an all-zero BG
  matrix is taken as the identity (bitmap games come out whole), a fade is left out (hello's
  dimming), and a window whose bounds read 0 covers the screen. Mode 7 roads (F-Zero, Mario
  Kart) set their matrix each line and come out wrong whatever is dumped.
- **Tests:** `tests/ppu_render.sh` dumps frames from mGBA (every register) and needs every
  pixel of the renderer's BMP to match mGBA's screen at 5 bits a channel: hello (mode 3, a
  fade), the kernel's home and SD card pages (mode 0, sprites, a window), and the games in
  `roms/` when there (Doom and Wolfenstein 3D in mode 4, Mario Kart's title with an affine
  BG, windows in Advance Wars and Mario Kart's menu, blending in Golden Sun, Kirby, Metroid
  Fusion, Emerald); `tests/host/test_ppu_render.c` (flips, priorities, affine and
  double-size sprites, blending, the BMP head); `tests/screenshot.sh shot` (the row, the
  file, nothing written twice) and `cut` (each cutpoint).

## P5: the In-game menu setting (built)

- **Settings, Hotkeys, In-game menu** (Back to menu's slot, so its values carry over):
  Auto (Back to menu's Off: the menu in a game launched with Save states or cheats on, as
  before), Every game (its On: the menu in every game started with addons; before, the keys
  only went back to the kernel) and Off (the menu keys do nothing; cheats still run, the
  sleep key still works). Save states no longer force the row to show On.
- **Menu keys** as Settings sets them reach the hook (`RTS_Reset_key`) and the overlay's
  footer ("L+R+START or B: resume"), drawn by the kernel at launch; `tests/menu_keys.sh`
  opens the menu on L+R+B with only Every game on, and checks Off.
- **NOR games** keep the In-game menu setting and keys they were written with (the patch is
  burned in, as the old hotkeys were; Info's Hotkeys page says so): a game written under
  Auto still opens the menu after Off is picked, until it is written again.

## Polish (from use in the emulator)

- The menu's footer names its keys; the panel is 176 px wide for it. The title (the game's
  name) shows when the rows leave room: with Save states and cheats on they don't.
- The panel shows 4 frames after the key and its rows 5 in hello (2 and 3 before the stub
  slowed to `0x4017` and read its backups back): the stub's SRAM copies move four words a
  pass, the rows' sprites go in after the panel shows, and the footer's 40 tiles a frame
  after the rows.
- The Save state page shows the buffer's slot, the one Load state gives back, in the accent.
- The game popup's Load state on a slot whose data doesn't read asks first ("This state
  can't be read"): B goes back to the list, A starts the game from its start.

## P0: the cart probe

A probe ROM for the real cart (precedents: `source/eepromprobe.h`, `source/rumbleprobe.h`),
reporting to SRAM page `0xF0` and the logs. It checks, in game mode:

- SRAM pages `0x20-0xEF` read and write, for each save type, flash 128K's bank 1 included;
- PSRAM writes;
- the page register's readback;
- SD sector reads and writes;
- the RTC through the `0x096A0000` toggle;
- the SRAM wait states.

It goes on `docs/release-checklist.md`. The emulator gets stop statuses (like status 76) for
whatever is unproven in game mode (PSRAM writes, SD access), in the cart model's patch
(`just emu-save-patch`).

## Tests

- **`tests/menu.sh`:** dump EWRAM, IWRAM (less the IRQ stack and `0x3007FE0-0x3007FFF`),
  VRAM, OAM, the palette and the readable I/O registers at opening and at closing: they must be
  byte-identical. The screen's hash before opening must equal the hash after closing.
- **`tests/hook_watch.lua`** detects the sprite menu.
- **Compat:** the column's open and close must leave every game identical; `just compat --all`
  at the end must stay at 196 of 196, or the difference is explained.
- **Golden shots** of the overlay.
- **Perf:** frames from the menu key to the first drawn frame; the target is the menu visible
  within 2 frames.

## Full 32 MB ROMs

They keep today's behaviour (the patch area at the cap, over the ROM's last bytes) unless the
stub makes it trivially better; the build notes which.
