# v0.8 in-game menu: the sprite overlay

The design for v0.8's in-game menu (roadmap, *v0.8 In-game menu*), from the review of today's
menu in [docs/research/ingame-menu.md](../../research/ingame-menu.md). It is built in phases:
P0 proves what the cart does in game mode, P1 replaces today's Mode 3 menu with the overlay
and its first actions, P2 adds save-state slots and P3 per-cheat toggles.

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
| `0xC0000-0xD8FFF` | the screenshot dump (later) |
| `0xF0000` | the cart log (`source/cartlog.h`) |

### The ABI

`source/ingame_abi.h`, hardware-free so the host tests can include it:

- **In:** a magic, a version, the ROM's CRC, the theme's palette (16 colours from the UI
  theme), the hotkeys, the launch's value of `0x096A0000`, the slot table and the cheat records.
- **Out:** a pending state, a screenshot flag, the cheat mask and the action picked.

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
- Set `WAITCNT` for the SRAM copies, and restore it.

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
