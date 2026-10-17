# Release checklist

What to check on a real Omega DE before a release, beyond `just test` and
`just compat`. Most of it runs inside **Diagnostics** (settings page 2,
DIAGNOSE, or hold L+R+SELECT while switching the console on), which logs
every answer to `/torii/logs` at Debug level. Bring the card back and run
`just logs /Volumes/OMEGA` (your card's mount point) to read the report in
`dist/logs/torii.log`.

## Before you start

1. Copy the build to the card: `just sd /Volumes/OMEGA`.
2. Set LOGGING to DEBUG in settings page 2 and save, so the sessions
   below are logged too.
3. Have on the card: a GBA game with a battery save, one with an EEPROM
   save, one with a clock (Pokémon Emerald), Drill Dozer or WarioWare:
   Twisted! if you have one, a GB or GBC game, a NES game, and a `.cht`
   file for one game.

## By hand, before Diagnostics

Do these first; Diagnostics asks about them afterwards ("since the last
run"). Answer no to anything you didn't try.

| Check | How |
|---|---|
| A GBA game starts from SD | CLEAN BOOT and BOOT WITH ADDON |
| Its save survives a power cycle | Save in the game, switch off and on, answer A to Save sav?, start it again |
| The in-game menu opens | L+R+START in a game started WITH ADDON (RESET and SAVESTATE on) |
| A save state saves and loads | SAVE, play on, LOAD |
| Sleep sleeps and wakes | SLEEP on, L+R+SELECT in game, then the wake keys |
| A GB or GBC game runs | from the SD list |
| A NES game runs | from the SD list |
| A game written to NOR starts | WRITE TO NOR CLEAN, then from the NOR list |
| The clock kept time while off | the menu's clock after a while switched off |
| DEBUG works in a game | with LOGGING on: L+R+START, DEBUG, then RESET; the next boot logs "debug info" |

## In Diagnostics

Diagnostics runs these by itself and logs each result:

- the kernel's unit tests;
- the clock: a valid date, and that it runs;
- PSRAM: a pattern in its last 64 KB;
- NOR: the kernel reads the same twice, and the FPGA images in the
  upgrade file match their known CRCs (about 800 KB);
- the save chip: read twice, the same both times, and the same after the
  tests (it is only read); a pattern in a free page of cart SRAM; two
  SRAM pages kept apart;
- timings: 64 KB of NOR and of PSRAM at seven WAITCNT settings (the BIOS
  default and faster ones, checked against the default's data), one
  glyph, BIOS LZ77 to WRAM, SD reads, SD writes;
- EEPROM 512 or 8K: reads one block of the save chip through the FPGA's
  EEPROM with 6-bit and 14-bit addresses at save types 0x21 and 0x22,
  and compares each reply with the chip's bytes. It needs a save in the
  chip's first 512 bytes: play an EEPROM game and save first.

Then it asks you (A yes, B no, SELECT skip; START at the first question
skips them all):

- **Rumble:** it pulses the GPIO port's pin 3 with the port register
  (`0x096A0000`) at 1, 0, 2 and 3, and asks after each if the cart
  rumbled. It also logs whether the clock answers at each value. This is
  how we look for a rumble switch separate from the clock.
- **Buttons:** press every button once (START+SELECT for two seconds
  gives up).
- **The screen:** red, green, blue and white bars.
- **The LED:** it breathes red and blue for a moment.
- **The clock:** shows the right date and time.
- The "since the last run" checks from the table above.

At the end, A restarts the kernel. SELECT crashes it on purpose first, to
check the crash dump: the next boot must log `crash: the kernel crashed`
with the registers and the log lines before it. Switch the console off
and on once after that too, and see whether a second crash test's dump
survives a power cycle (it lives in cart RAM; on the emulator it always
does).

## Afterwards

- `just logs /Volumes/OMEGA`, then read `dist/logs/torii.log`: the
  Diagnostics summary, each `diag:` line, and any `E` (error) lines.
- Every result that differs from the emulator goes into the cart model
  (`emu/mgba/src/gba/cart/omega.c`) or
  `docs/research/omega-de-hardware.md`.
