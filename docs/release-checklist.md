# Release checklist

What to check on a real Omega DE before a release, beyond `just test` and
`just compat`. It lists only what no real-cart test has confirmed yet; the
rest is in `docs/research/cart-test-2026-10-18.md` and
`cart-test-2026-10-18b.md`. Most of it ends in **Diagnostics** (settings
page 2, DIAGNOSE, or hold L+R+SELECT while switching the console on), which
logs every answer to `/torii/logs` at Debug level. Bring the card back and
run `just logs /Volumes/OMEGA` (your card's mount point) to read the report
in `dist/logs/torii.log`.

## Before you start

1. **Check the buttons first.** The in-game menu and sleep need L and R
   (the hotkeys are L+R+START and L+R+SELECT by default), and v0.2.1's test
   failed on a laggy L. Run Diagnostics once, press A at its questions and
   go through the button step: it must end with every button seen. Fix the
   console before going on if it doesn't.
2. **Keep EZ-Flash's loader on the test card.** Write `DE.BIN` at byte
   `0x200` of the card once (`docs/recovery.md`, steps 1–4). If a build
   erases the cart's boot loader, safe mode's "Update loader" then works at
   once, without a computer.
3. Copy the build, the EEPROM probe and the cheat test to the card:
   `just test-roms && just sd /Volumes/OMEGA tests/roms/eeprom/eeprom.gba tests/roms/hello/hello.gba`.
   Then make `CHEAT/Hello.cht` on the card holding these lines:

   ```
   [Plain]
   ON=1236,58;

   [GameInfo]
   Name=Hello
   ```

4. Delete `SAVER/EEPROM Probe.sav` from the card, so the probe's 8K runs
   start from a fresh 8 KB save.
5. In settings page 1, turn RESET, SAVESTATE, SLEEP and CHEAT on (under
   ADDON); in page 2, set LOGGING to DEBUG. Save, so every session below
   is logged and sleep and cheats can be tried.
6. Have on the card: Pokémon Emerald (or another game with a save), a GB
   or GBC game, a NES game, and Drill Dozer for rumble if you have it.

## By hand, before Diagnostics

Do these first; Diagnostics asks about them afterwards ("since the last
run"). Answer **SELECT** ("didn't try") to anything you didn't try, not B:
B means it failed. Diagnostics checks each yes against the log where it can,
and lists a yes with no matching launch (sleep with no launch that had SLEEP
on, NES with no NES launch) as "unconfirmed" in its summary.

| Check | How |
|---|---|
| The in-game menu opens | BOOT WITH ADDON with RESET and SAVESTATE on, then L+R+START in the game |
| A save state saves and loads | in the menu, SAVE; play on; open the menu again and LOAD: the game is back where you saved |
| DEBUG reaches the log | with LOGGING on: L+R+START, DEBUG; it says DEBUG SAVED and picks RESET. Choose RESET, not power-off: the snapshot is in cart RAM, which a power-off clears. The kernel's boot logs "debug info" |
| Sleep sleeps and wakes | SLEEP on, BOOT WITH ADDON, L+R+SELECT in the game: the screen goes off; the wake keys bring the game back where it was |
| A cheat works | `hello.gba` BOOT WITH ADDON (it finds `CHEAT/Hello.cht`), then L+R+START, CHEAT ON, and back to the game. Hello doesn't react to buttons; a green square in its top right corner shows the cheat at work (no square before CHEAT ON) |
| A GB or GBC game runs | from the SD list; note the game's title |
| A NES game runs | from the SD list |
| A game written to NOR starts | WRITE TO NOR CLEAN, then start it from the NOR list (R). Power off and on: the cart must start the kernel, not safe mode |
| Deleting a NOR game | in the NOR list, delete the latest game. Power off and on: the cart must start the kernel, the game gone from the list |
| Rumble in a game | Drill Dozer, BOOT WITH ADDON or CLEAN BOOT: does the cart rumble? |
| The EEPROM probe | `eeprom.gba`, CLEAN BOOT with SAVE TYPE EEPROM 512: A (6-bit addresses), then again with B (14-bit). Then the same two with EEPROM 8K. See below |

**The EEPROM probe** writes blocks 1–4 four different ways, reads them back
a frame later, and shows one line for each way: OK in green, or which bytes
read back wrong in red. A pass is "READS BACK WHAT IT WROTE" in green and
four green OK lines; a 14-bit run also says "1024 BLOCKS: 8 KB" (or "WRAPS
AT 64 BLOCKS: 512 B"). Note what each run's screen says, in particular any
red line, then press A: the kernel's boot logs every step ("eeprom probe:")
and its verdict.

## In Diagnostics

Diagnostics checks the cart by itself (unit tests, clock, PSRAM, NOR, the
boot loader, save chip, timings, SD) and whether the EEPROM probe's runs passed. Then it asks
you (A yes, B no, SELECT skip; START at the first question skips them all):

- **Rumble:** it pulses the GPIO port at four register values and asks
  after each if the cart rumbled.
- **Buttons:** press every button once. If L or R never registers, the
  in-game menu and sleep can't work, and it says so.
- **The screen** and **the LED:** confirmed in v0.2.1; answer them.
- **The clock:** look at a real clock (a phone) when you answer, and note
  the real time: the log records what the cart's clock read then, so the
  two give its drift. The menu's clock must match too.
- The "since the last run" checks from the table above.

At the end, A restarts the kernel. SELECT crashes it on purpose first, to
check the crash dump: the next boot must log `crash: the kernel crashed`
and say it was "Diagnostics' crash test".

**The "unconfirmed" path:** run Diagnostics once more straight after, with
nothing tried in between, and answer yes to sleep. The summary must list it
as "unconfirmed: sleep" rather than pass it.

## Afterwards

- `just logs /Volumes/OMEGA`, then read `dist/logs/torii.log`: the
  Diagnostics summary, each `diag:` and `eeprom probe:` line, and any `E`
  (error) lines.
- Note the `diag: boot loader CRC` line and whether it found a GBA header:
  the first real reading of EZ-Flash's loader (`omega-de-hardware.md`).
- Every result that differs from the emulator goes into the cart model
  (`emu/mgba/src/gba/cart/omega.c`) or
  `docs/research/omega-de-hardware.md`.
