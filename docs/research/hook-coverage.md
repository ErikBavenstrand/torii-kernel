# In-game menu and IRQ hook coverage

Which games the kernel's in-game menu (and the cheat, reset and sleep hooks
that share its IRQ hook) misses, measured on the emulated cart with every
GBA game in `roms/games.toml` (`just compat --all gba`, 2026-10-17), and
what the kernel fixes made of it. Results come from the emulated cart; the
fixes are worth confirming on a real cart.

## How the hook works

With any of RESET, SAVESTATE, SLEEP or CHEAT on, BOOT WITH ADDON patches the
ROM as it loads (`source/GBApatch.c`):

1. It finds the words where the game keeps the address of its IRQ handler
   store, `0x3007FFC` or its mirror `0x3FFFFFC`, and changes them to
   `0x3007FF4`. So the game installs its handler one slot lower.
   - With the fast patch engine (the default, English only), the offsets
     come from a built-in table keyed by game code (`source/reset_table.h`,
     2,700+ entries). Unknown games fall back to the scan, and so (since
     `b736015`) do games whose listed words aren't IRQ handler literals in
     this ROM, and blank game codes.
   - Otherwise the kernel scans every aligned ROM word (`PatchInternal`),
     keeps at most 32 (`EMax`), and caches the result in `/PATCH/<game>.pat`.
   - `Patch_SpecialROM_sleepmode` adds code patches for a few games that
     compute the address instead of loading it, or that clear IWRAM over
     the hook.
2. If it patched anything, the ROM's first instruction becomes a branch to
   the patch area after the ROM's end. That code stores the hook (`RTS_irq`)
   at `0x3007FFC` and jumps to the game's real entry.
3. On an IRQ with the VBlank (or keypad) flag set, the hook checks the keys.
   L+R+START opens the menu (RESET / SAVE / LOAD, and cheats). Then it jumps
   to the game's handler at `0x3007FF4`.

SAVESTATE on its own used to work differently, with an `RTS_only` patch
where L+R+START loaded a state at once and L+R+SELECT saved one; loading with
no saved state crashed Advance Wars. v0.4 removed it: SAVESTATE now always
gets the menu.

So the menu is missed when:

- the kernel patches nothing, so the entry branch isn't written;
- the game still writes `0x3007FFC`, through a store the kernel didn't find
  or by clearing IWRAM after the entry code ran;
- the game never enables the VBlank IRQ.

## How it is measured

`just compat` has an **in-game menu** column. For every GBA game that boots,
it makes a second cart, writes settings into its kernel NOR (SAVESTATE on
alone, menu key L+R+START), launches with BOOT WITH ADDON, and runs
`tests/hook_watch.lua`. That script reads only memory and the CPU state, so
it checks the hook from outside the kernel:

- the game's start, and the patch area the entry branch points to;
- every change of `0x3007FFC` (the IRQ vector) and `0x3007FF4` (the game's
  handler);
- how many frames had the VBlank IRQ fully enabled (IME, IE bit 0, DISPSTAT
  bit 3);
- whether the menu is on screen: mode 3 with BG2 only, and the CPU in the
  menu's code.
- that the patch area starts with the kernel's entry code, so a game whose
  own entry was left alone isn't counted as hooked. The game's start is
  when the ROM header changes; many games share the kernel's first word.

The script presses START and A to get past title screens. 600 frames into
the game it holds L+R+START, then B a second later, then DOWN, A, START and A.
The check passes when the menu opened and the screen changed after it closed.
The metrics show how the kernel found the stores. That is the built-in table
or the scan (read from `/PATCH/*.pat`), with the code patches, plus the
number of matching ROM words.

`tests/menu.sh` runs the same flow on `tests/roms/hello`, which now takes a
VBlank IRQ, as part of `just test`.

## Results

119 GBA games ran (118 commercial, 1 homebrew; the `gba` filter also matched
3 GB games, which are n/a). All 118 commercial games are in the built-in
table. Every game that boots enables the VBlank IRQ, except the homebrew one.

Before the kernel fixes (kernel at `f219ed6`), 107 opened the menu and went
on afterwards, and 12 failed. After them, 118 pass, and the
homebrew RTC Checker is n/a: it never enables the VBlank IRQ, which the menu
needs.

| Game | Before | Cause | Fix |
|---|---|---|---|
| Pokémon FireRed (BPRE rev 1), WaterBlue (FireRed hack) | crash before the menu key | table offsets are for rev 0. The kernel wrote `0x3007FF4` over 2 unrelated words (a pointer and a code word) and missed 2 of the 3 real literals | `b736015`, `c69aa23`: entries are checked against the ROM; the table's rev 1 entry matches |
| Pokémon LeafGreen (BPGE rev 1, two dumps) | crash before the menu key | same: offsets `0x1BB`, `0x1C0C8E` are rev 0's | same: the rev 1 entry matches |
| Crash Bandicoot Purple (BD4E rev 1) | crash before the menu key | all 3 table offsets miss this revision | `b736015`: no entry matches, so the kernel scans |
| SimCity 2000 (A5CP rev 1) | no menu; the game set its own handler 1 frame in | 1 of 2 table offsets missed; the real store stayed | same: the kernel scans |
| Grand Theft Auto Advance (BGTP) | crash at the first IRQ (`0xEFEFEFEF`) | a DMA fills all of IWRAM with `0xEF` after the hook is in | `c57c3b6`: the fill stops at `0x3007FF0` |
| Super Monkey Ball Jr. (ALUP) | crash at the first IRQ (`0xD5BEADDE`) | a DMA fills `0x3007600` to the end of IWRAM | same |
| LEGO Star Wars II (BL7P) | hung, a black screen after the menu | crt0 clears IWRAM up to `0x3008000`, then the first IRQ jumps to 0 | `dcf4c5b`: the clear stops at `0x3007FF0` |
| Lara Croft Tomb Raider: Legend (BL8P) | no menu; its handler replaced the hook 1 frame in | it stores at `0x3008000 - 4`, computed | `dcf4c5b`: `subs r0,#4` becomes `subs r0,#12` |
| Crash Nitro Kart (BCNP) | crash 18 frames after the menu | after `RegisterRamReset` it reloads its stack from a second literal the kernel didn't move, so the stack sat on the menu's register save area at `0x3007E80` | `dcf4c5b`: `Get_spend_address` moves that literal too |
| RTC Checker (homebrew, no game code) | no menu | never enables the VBlank IRQ. Its blank code also matched the table's all-zero entry | `b736015`: blank codes skip the table. The menu stays n/a |

### Exact-ROM patches

Most fixes are general rules: the table check, the blank-code rule, and
moving every reload of the stack top. Four games overwrite the hook in ways
no safe rule can find from the ROM alone. A rule on crt0's `0x03008000` end
address would also change 9 working games; one on DMA control words would
also hit the sound DMAs of about 60; GTA's fill target is in a register;
and Lara's address is computed. They get code patches from the
`rom_patches` table in `source/irqpatch.c`. Each entry applies only to the
ROM with its game code, revision and header complement check, and only if
the patch site holds the expected word.

| Game (EU) | Code, revision, check | Patch | Compat case |
|---|---|---|---|
| Grand Theft Auto Advance | BGTP, 0, 0xAC | DMA control `0x584`: `0x85002000` → `0x85001FFC` | `just compat --all BGTP` |
| Super Monkey Ball Jr. | ALUP, 0, 0x15 | DMA control `0x36070`: `0x85000280` → `0x8500027C` | `just compat --all ALUP` |
| LEGO Star Wars II | BL7P, 0, 0xE0 | crt0 clear end `0x14C`: `0x03008000` → `0x03007FF0` | `just compat --all BL7P` |
| Lara Croft Tomb Raider: Legend | BL8P, 0, 0xB8 | word `0x14A0`: `subs r0,#4` → `subs r0,#12` | `just compat --all BL8P` |

The compat report's "hook from" metric says "exact-ROM patch" for these.
Other regions of the same games don't match and still miss the menu.

Super Mario Advance 4 rev 1 (AX4P) passed before, but 5 of its 7 table
offsets are rev 0's, so the kernel overwrote 5 unrelated words; it now
uses the table's rev 1 entry.

Kirby: Nightmare in Dream Land has 44 matching words (42 of `0x3007FFC`,
2 of the mirror), more than the scan's 32 slots; with the scan engine 12
stayed unpatched. The kernel now has 64 slots (`EMax`), and `.pat` files
of the old layout are scanned again.

Other things seen:

- **The menu changes the screen state.** EZ-Flash's 1.06 menu zeroed BLDY
  and the BG2 affine registers and never restored them; they are
  write-only. A game that set them once looked wrong until it wrote them
  again: Pokémon Ruby's darkened title menu came back at full brightness.
  Since `5b10b34` the menu turns blending off through BLDCNT, which can be
  read, and restores it, leaving BLDY alone. The BG2 affine registers are
  still reset, as the menu draws on BG2; avoiding that would mean a
  tile-based menu. No one online reports this; the closest is EZ-Flash's
  note that sound registers are write-only, so sound can turn to noise
  after loading a save state.
- **The menu's IME stores missed IME** until `37c7d60`: `strh r3,[r7,#208]`
  wrote `0x040000D0` (DMA2's count), leaving it at 1 after every menu.
