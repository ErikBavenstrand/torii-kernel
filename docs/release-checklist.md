# Release checklist

What to check on a real Omega DE before a release, beyond `just test` and
`just compat`. It lists only what no real-cart test has confirmed yet; what the
cart tests of v0.2.0 and v0.2.1 settled is in
`docs/research/omega-de-hardware.md`. Most of it ends in **Diagnostics** (Info,
Diagnostics, or hold L+R+SELECT while switching the console on), which
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
5. In Settings, turn Save states and Cheats (Games) and Back to menu and
   Sleep (Hotkeys) on, and set Logging (System) to Debug. Each is written
   as A confirms it, so every session below is logged and sleep and cheats
   can be tried.
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
| The in-game menu opens | Boot with addon (the game's menu, START) with Save states on, then L+R+START in the game |
| A save state saves and loads | in the menu, SAVE; play on; open the menu again and LOAD: the game is back where you saved |
| DEBUG reaches the log | with LOGGING on: L+R+START, DEBUG; it says DEBUG SAVED and picks RESET. Choose RESET, not power-off: the snapshot is in cart RAM, which a power-off clears. The kernel's boot logs "debug info" |
| Sleep sleeps and wakes | Sleep on, Boot with addon, L+R+SELECT in the game: the screen goes off; the wake keys bring the game back where it was |
| A cheat works | `hello.gba` Boot with addon (it finds `CHEAT/Hello.cht`), then L+R+START, CHEAT ON, and back to the game. Hello doesn't react to buttons; a green square in its top right corner shows the cheat at work (no square before CHEAT ON) |
| A GB or GBC game runs | from its console's list or the SD card page; note the game's title |
| A NES game runs | from the NES list or the SD card page |
| A game written to NOR starts | SD card page, START on a GBA game, Write to NOR, then start it from the NOR page. Power off and on: the cart must start the kernel, not safe mode |
| Deleting a NOR game | on the NOR page, START on the latest game, Delete. Power off and on: the cart must start the kernel, the game gone from the page |
| Rumble in a game | Drill Dozer, Boot with addon or Clean boot: does the cart rumble? |
| The loading bar during the cart's copy | Clean boot of a big GBA game (16 or 32 MB): the bar moves steadily through "Loading the game" without long stalls or a big jump at the end. See below |
| Opening and going back | A on a home item: its object zooms and fades into the paper, the title bar slides in, the card rises; B plays it backwards. No stutter, no flash of the old screen, the object's first and last frames where the carousel shows it |
| The theme wipe | Settings, Theme, flip it: the new theme comes down from the top behind one clean, straight edge. Look for a ragged or flickering line at the edge (the palette copy running late on the cart, which the emulator can't show) and for the selection pill switching with its row |
| The EEPROM probe | `eeprom.gba`: set its save type to EEPROM 512 in Game info (the game's menu, LEFT/RIGHT on Save type), then Clean boot: A (6-bit addresses), then again with B (14-bit). Then the same two with EEPROM 8K. See below |

### The v0.4 menu

The emulator runs the menu at its frame budget but can't show how it feels
on the cart's screen, how fast the real card is, or what an old card's files
do. With Logging on Debug, each of these leaves lines in the log.

| Check | How |
|---|---|
| The carousel at 60 fps | turn home with LEFT/RIGHT, tapped and held: every object slides smoothly, nothing flickers or drops out (sprites past a line's limit vanish for a line), the far objects' fog reads on the screen |
| The zoom at 60 fps | open and close every item: as "Opening and going back" above, with no frame held at the turn from zoom to card |
| The 14 px rows | hold DOWN through a long game list and the SD card's biggest folder: is the speed right, does the pill keep up, are the rows easy to read at 14 px? Note anything that tears |
| The theme wipe's edge | as "The theme wipe" above, both ways, on the cart's own screen (an IPS kit re-scans the picture and may show its own tearing) |
| A full card's first scan | a card with hundreds of games and no `/torii/library.idx`: time from power-on to the menu, and from there until the consoles' counts settle. The log's `library:` lines give the walk's and the index write's times; compare with the emulator's (about 5x slower than a cart) |
| An old card's recents | a card last used with v0.3 (with `SAVER/Recently play.txt`, NOR games in it too): Recent lists the same games, newest first, and after the first launch `/torii/recent.txt` exists and the old file is gone |
| Covers loading | with art on the card (`just art-scrape` or `art-add`), Settings, Game view, List + art: covers appear soon after the cursor rests, the list never stutters while one loads, and a held scroll shows the placeholder |
| Rename on a real card | rename a game from its menu (the name only), then an SD card file with a save from the SD card page: the file and its `.sav` both renamed, the game still in Recent and Favourites, its save still loading in the game |
| Diagnostics from Info | Info, Diagnostics: its first page comes up, and B there comes back to Info, on its row; run it through once from there too |
| The copy estimate | see "The copy's estimate" below: set `PROGRESS_COPY_RATE` from two or three big games |
| The scroll's brake | let go of a held scroll at top speed: it should stop within about two rows. If it overshoots, raise `SPRING_BRAKE` (`source/ui/ui_list.c`) |
| Format all's time | time a real Format all on the NOR page (it erases every NOR game) and set `CHIP_ERASE_FRAMES` (`source/NORflash_OP.c`) from it; its bar is only an estimate |

**The EEPROM probe** writes blocks 1–4 four different ways, reads them back
a frame later, and shows one line for each way: OK in green, or which bytes
read back wrong in red. A pass is "READS BACK WHAT IT WROTE" in green and
four green OK lines; a 14-bit run also says "1024 BLOCKS: 8 KB" (or "WRAPS
AT 64 BLOCKS: 512 B"). Note what each run's screen says, in particular any
red line, then press A: the kernel's boot logs every step ("eeprom probe:")
and its verdict.

**The copy's estimate:** during a Clean boot the cart copies the game into
PSRAM by itself, and the bar moves on an estimate from the file size
(`PROGRESS_COPY_RATE` in `source/progress.h`, bytes a frame). Each launch
logs the real figure: `launch: the cart copied N KB in F frames, B bytes a
frame (the estimate: E frames)`. If B differs from `PROGRESS_COPY_RATE` by
more than about 10% over two or three big games, set the constant to their
average.

## In Diagnostics

Diagnostics checks the cart by itself (unit tests, clock, PSRAM, NOR, the
boot loader, save chip, timings, SD) and whether the EEPROM probe's runs passed. Then it asks
you (A yes, B no, SELECT skip; START at the first question skips them all):

- **Rumble:** it pulses the motor for a second, as Jagoomba does, and asks
  if the cart rumbled. A no is a failure.
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
