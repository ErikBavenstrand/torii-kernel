# In-game menu and IRQ hook coverage

Which games the kernel's in-game menu (and the cheat, reset and sleep hooks
that share its IRQ hook) misses, measured on the emulated cart with every
GBA game in `roms/games.toml` (`just compat --all gba`, 2026-10-17, kernel
`f219ed6`). Results come from the emulated cart. Check the failures on a
real cart before changing the kernel for them.

## How the hook works

With any of RESET, SAVESTATE, SLEEP or CHEAT on, BOOT WITH ADDON patches the
ROM as it loads (`source/GBApatch.c`):

1. It finds the words where the game keeps the address of its IRQ handler
   store, `0x3007FFC` or its mirror `0x3FFFFFC`, and changes them to
   `0x3007FF4`. So the game installs its handler one slot lower.
   - With the fast patch engine (the default, English only), the offsets
     come from a built-in table keyed by game code (`source/reset_table.h`,
     2,700+ entries). Unknown games fall back to the scan.
   - Otherwise the kernel scans every aligned ROM word (`PatchInternal`),
     keeps at most 32 (`EMax`), and caches the result in `/PATCH/<game>.pat`.
   - `Patch_SpecialROM_sleepmode` adds code patches for a few games that
     compute the address instead of loading it.
2. If it patched anything, the ROM's first instruction becomes a branch to
   the patch area after the ROM's end. That code stores the hook (`RTS_irq`)
   at `0x3007FFC` and jumps to the game's real entry.
3. On an IRQ with the VBlank (or keypad) flag set, the hook checks the keys.
   L+R+START opens the menu (RESET / SAVE / LOAD, and cheats). Then it jumps
   to the game's handler at `0x3007FF4`.

SAVESTATE on its own works differently: it uses the `RTS_only` patch, where
L+R+START loads a state at once and L+R+SELECT saves one. Loading with no
saved state crashed Advance Wars. The menu needs SAVESTATE with RESET, SLEEP
or cheats.

So the menu is missed when:

- the kernel patches nothing, so the entry branch isn't written;
- the game still writes `0x3007FFC`, through a store the kernel didn't find
  or by clearing IWRAM after the entry code ran;
- the game never enables the VBlank IRQ.

## How it is measured

`just compat` has an **in-game menu** column. For every GBA game that boots,
it makes a second cart, writes settings into its kernel NOR (SAVESTATE and
RESET on, menu key L+R+START), launches with BOOT WITH ADDON, and runs
`tests/hook_watch.lua`. That script reads only memory and the CPU state, so
it checks the hook from outside the kernel:

- the game's start, and the patch area the entry branch points to;
- every change of `0x3007FFC` (the IRQ vector) and `0x3007FF4` (the game's
  handler);
- how many frames had the VBlank IRQ fully enabled (IME, IE bit 0, DISPSTAT
  bit 3);
- whether the menu is on screen: mode 3 with BG2 only, and the CPU inside
  the patch area.

The script presses START and A to get past title screens. 600 frames into
the game it holds L+R+START, then B a second later, then DOWN, A, START and A.
The check passes when the menu opened and the screen changed after it closed.
The metrics show how the kernel found the stores. That is the built-in table
or the scan (read from `/PATCH/*.pat`), with the code patches, plus the
number of matching ROM words.

`tests/menu.sh` runs the same flow on `tests/roms/hello`, which now takes a
VBlank IRQ, as part of `just test`.

## Results

122 GBA games ran: 107 open the menu and go on afterwards, 12 fail, and 3 GB
games are n/a. All 119 commercial GBA games are in the built-in table, so the
ROM scan was never used. Every game that boots enables the VBlank IRQ, except
one homebrew ROM.

| Game | What happens | Cause |
|---|---|---|
| Pokémon FireRed (BPRE rev 1), WaterBlue (FireRed hack) | crash before the menu key | table offsets are for rev 0. The kernel writes `0x3007FF4` over 2 code words and misses 2 of the 3 real stores. |
| Pokémon LeafGreen (BPGE rev 1, two dumps) | crash before the menu key | same: offsets `0x1BB`, `0x1C0C8E` are rev 0's |
| Crash Bandicoot Purple (BD4E rev 1) | crash before the menu key | all 3 table offsets miss this revision |
| SimCity 2000 (A5CP rev 1) | no menu; the game sets its own handler 1 frame in | 1 of 2 table offsets misses; the real store stays |
| Grand Theft Auto Advance (BGTP) | crash at the first IRQ (`0xEFEFEFEF`) | the table is right, but the game fills IWRAM with `0xEF` after the hook is in, and its handler now goes to `0x3007FF4` |
| Super Monkey Ball Jr. (ALUP) | crash at the first IRQ (`0xD5BEADDE`) | same: the game fills IWRAM over the hook |
| Lara Croft Tomb Raider: Legend (BL8P) | no menu; the game sets its own handler (`03000F00`) 1 frame in | the one table entry matches, but the game also writes the vector another way |
| RTC Checker (homebrew, no game code) | no menu | never enables the VBlank IRQ. Also, its blank code matches the table's all-zero entry ("GBADev 2004Mbit Competition"), so the kernel patches 10 arbitrary words |
| Crash Nitro Kart (BCNP) | crashes 18 frames after the menu closes | not investigated |
| LEGO Star Wars II (BL7P) | after the menu, a black screen that ignores keys | not investigated |

Super Mario Advance 4 rev 1 (AX4P) passes, but 5 of its 7 table offsets are
rev 0's, so the kernel overwrites 5 unrelated words with `0x3007FF4`.

Kirby: Nightmare in Dream Land has 33 matching words, more than the scan's
32 slots. That only matters if it falls back to the scan.

Other things seen:

- **The menu changes the screen state.** It zeroes BLDY and the BG2 affine
  registers and doesn't restore them; they are write-only, so it can't. A
  game that set them once looks wrong until it writes them again. Pokémon
  Ruby's darkened title menu comes back at full brightness.
- **The menu's "IME = 0" doesn't disable IME.** `strh r3,[r7,#208]` in
  `gba_rts_patch.s` writes `0x040000D0` (DMA2's count), not IME at
  `0x04000208`. The menu runs inside the IRQ handler with IRQs masked, so
  this is mostly harmless.

## Recommended kernel changes (not made here)

1. **Check the table against the ROM.** In `use_internal_engine`, apply an
   entry only if every offset holds `0x3007FFC` or `0x3FFFFFC`, and
   otherwise use the scan. Skip the table for blank or non-alphanumeric game
   codes. This fixes FireRed, LeafGreen, WaterBlue, Crash Purple and SimCity
   2000 rev 1, and stops the stray writes into SMA4 rev 1 and homebrew.
2. **Games that clear IWRAM** (GTA Advance, Super Monkey Ball Jr.) need the
   hook to come back after the clear. One option is to install it when the
   game first enables IRQs instead of at entry. Another is a per-game patch
   in `Patch_SpecialROM_sleepmode`. Until then, an addon launch crashes these
   games.
3. Fix the IME store in `ingamemenu_now` (`#0x208` needs a base register).
4. Look into Crash Nitro Kart and LEGO Star Wars II on a real cart first.
