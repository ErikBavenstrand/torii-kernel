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
   `just test-roms && just sd /Volumes/OMEGA tests/roms/eeprom/eeprom.gba tests/roms/ingame/ingame.gba tests/roms/hello/hello.gba`.
   Then make `torii/cheats/Hello.cht` on the card holding these lines:

   ```
   cheats = 1

   cheat0_desc = "Green square"
   cheat0_code = "02001236:58"
   cheat0_enable = false
   ```

4. Delete the probe's save folder (`torii/saves/EEPROM Probe.gba`) from the card, so the probe's 8K runs
   start from a fresh 8 KB save.
5. In Settings, turn Save states (Games) on, In-game menu (Hotkeys) to
   Auto, Sleep (Hotkeys) on, and set Logging (System) to Debug. Each is
   written as A confirms it, so every session below is logged and sleep and
   cheats can be tried.
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
| The in-game menu opens | Boot with addon (the game's menu, START) with Save states on, then L+R+START in the game: the game dims under a panel at once (2 frames), its rows a frame later. B gives the game back with its screen, sound and speed as they were; try it in a game with music, a bitmap-mode game and one with scaled or rotated backgrounds (the old menu lost those) |
| A save state saves and loads | in the menu, Save state, slot 1; play on; open the menu again and Load state, A on slot 1: the game is back where you saved. Load state is greyed until a state is saved, and the Save state page shows the slot in use in the accent |
| Save states reach the card | Save state into slot 2, then Back to Torii: the boot shows "Saving the save state" with a bar and `/torii/states/<rom path>/Main/2.state` is on the card. Launch the game again: Load state at once gives the saved point. The game's popup, Load state, lists the slot with its time and starts the game in it. Power off in game after a Save state: the state is lost (cart RAM past the save has no battery), never half kept |
| Cheats in game | a game with three or four cheats (one picked): L+R+START, Cheats lists them with a box each; A turns one on and it works from the next frame, a greyed one says why at the bottom. Back to Torii: the game's Cheats screen has the new picks |
| A screenshot | in a game's menu, Screenshot: the footer says "Saved when you return to Torii". Back to Torii: the boot shows "Saving the screenshot" ("Saving screenshots" for several, its status "1 of 3" and on) with a bar (a few seconds each), then `/torii/screenshots/<game> <date> <time>.bmp` is on the card. On a PC it shows the game's screen as it was under the menu; note how it compares (a scrolled tile screen may sit shifted, a fade or a Mode 7 road comes out wrong: those registers can't be read back). Try a bitmap game (Doom) and one with sprites over tiles |
| A screenshot's time | in a game that has the cart's clock (Pokémon Emerald, Settings' game clock on), take a screenshot, play two more minutes, then Back to Torii: the file's time in its name is when you took it, two minutes before the filing, and the log says "screenshot: named by the clock in game". Then check the game's own clock (Emerald's, in the player's house): reading the clock from the menu mustn't have upset it. In a game without the clock the name has the filing's time, as before |
| A screenshot survives a power cut | take a screenshot, Back to Torii, and switch off while "Saving the screenshot" and its bar show; power on: no broken `.bmp` is left in `/torii/screenshots` (the boot deletes it), and no `.writing` file |
| A refused menu says why | if L+R+START shows a plain coloured screen instead of the menu, the stub refused it: note the colour (table below), then press B (back to Torii, which keeps the stub's record; A gives the game back, and a power-off loses the record). The boot's dialog says why, and with LOGGING on two "in-game menu:" lines give the details: send both |
| In-game menu Off | Settings, Hotkeys, In-game menu: Off, then Boot with addon a game with a cheat picked: L+R+START does nothing and the cheat still works. Every game: the menu opens in a game with no save states or cheats. Menu keys set to another combination (say L+R+B): that one opens it, and the menu's footer names it |
| A 32 MB game's menu | Boot with addon a 32 MB game (Mother 3, a big romhack) with a cheat picked: the menu opens and the cheat works. The log's "launch: the patch area" line says where the in-game code went: "cut to N bytes at ..." (padding before the ROM's end), or "goes over the game's last bytes" (no padding: note the game; it may misbehave near its end, as with the official kernel). Also with In-game menu on Every game and Save states off: the menu opens (before v0.9 its code was cut short there) |
| Reset game | in the menu, Reset game: the game starts again from its title, without the kernel, and its save is untouched |
| Back to Torii | in the menu, Back to Torii: the kernel starts, as RESET did |
| DEBUG reaches the log | with LOGGING on: L+R+START, Debug snapshot (the last row); the menu comes back saying the snapshot is kept. Choose Back to Torii, not power-off: the snapshot is in cart RAM, which a power-off clears. The kernel's boot logs "debug info" |
| The in-game probe | `ingame.gba` (v0.8 P0): Clean boot it once with its save type at each of SRAM, Flash 64K, Flash 128K, EEPROM 8K (Game info, Save type); for Flash 128K, first give it a save whose two 64 KB banks differ (copy a Pokémon FireRed or Emerald `.sav` into its save folder). Run each save type twice: first press B at "A: TRY THE SD CARD" (skip it), then A. Photograph both result pages (LEFT/RIGHT) of each run, then press A to go back to the kernel; never switch off at the probe. The kernel's boot logs "in-game probe:" lines. See below |
| Sleep sleeps and wakes | Sleep on, Boot with addon, L+R+SELECT in the game: the screen goes off; the wake keys bring the game back where it was |
| A cheat works | `hello.gba`'s popup shows Cheats 1; on the cheat screen, A ticks Green square and B saves. Boot with addon: Hello doesn't react to buttons, and a green square in its top right corner shows the cheat at work from the start. L+R+START, the Cheats row (A) says off and the square stays as it was drawn last; A again says on. Switched off and on, the tick is still there |
| A GB or GBC game runs | from its console's list or the SD card page; note the game's title |
| A GB game keeps its palette | a GB (not GBC) game: L+R, Display, Palette, pick another, leave the menu, power off and on, launch it again: the picked palette is back |
| Pokémon Crystal doesn't stutter | walk around a town a minute at the default gamma: steps are smooth (jagoombacolor#46) |
| GB, GBC and NES games run at full speed | play a busy scene of each a minute (the emulators run ROM at 3,1 wait states with prefetch since v0.6): no stutter or slowdown the game didn't have before, and no crash |
| A GB rumble game rumbles | Pokémon Pinball, or another MBC5 rumble game: the cart rumbles on a bumper hit |
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
| Rename on a real card | rename a game from its menu (the name only), then an SD card file with a save from the SD card page: the file renamed and its save folder under `/torii/saves` moved with it, the game still in Recent and Favourites, its save still loading in the game |
| Diagnostics from Info | Info, Diagnostics: its first page comes up, and B there comes back to Info, on its row; run it through once from there too |
| The copy estimate | see "The copy's estimate" below: set `PROGRESS_COPY_RATE` from two or three big games |
| The scroll's brake | let go of a held scroll at top speed: it should stop within about two rows. If it overshoots, raise `SPRING_BRAKE` (`source/ui/ui_list.c`) |
| A big folder | the SD card page on a folder of 300+ files and one of 1,000+ subfolders: note how long it takes to open (the emulator: 0.2 s per 300 files) and whether the first frames stutter (the log's `frame-cycles` and `sd-folder` lines) |
| Identify's time | a game's popup, Identify (or a new game's background CRC) on a 16 MB game: time it, about 1.6 s per MB estimated; the log's `names:` lines give the crc.txt write steps |
| About's free space | on an exFAT card, Info, About the first time after power-on: how long until the free space shows |
| Format all's time | time a real Format all on the NOR page (it erases every NOR game) and set `CHIP_ERASE_FRAMES` (`source/NORflash_OP.c`) from it; its bar is only an estimate |

**The EEPROM probe** writes blocks 1–4 four different ways, reads them back
a frame later, and shows one line for each way: OK in green, or which bytes
read back wrong in red. A pass is "READS BACK WHAT IT WROTE" in green and
four green OK lines; a 14-bit run also says "1024 BLOCKS: 8 KB" (or "WRAPS
AT 64 BLOCKS: 512 B"). Note what each run's screen says, in particular any
red line, then press A: the kernel's boot logs every step ("eeprom probe:")
and its verdict.

**A refused in-game menu's colours.** When the stub can't open the menu
safely it gives the game back untouched, but first shows one colour for the
whole screen until A or B; the colour names the reason
(`INGAME_REFUSED_COLOURS` in `source/ingame_abi.h`):

| Colour | RGB15 | Why the menu didn't open |
|---|---|---|
| Blue | `7C00` | No overlay image in cart SRAM: its header's magic didn't read (cart SRAM unreadable, or not written at launch) |
| Cyan | `7FE0` | The image's header read, but with sizes the stub can't take (a damaged header) |
| Red | `001F` | The game's EWRAM window (8 KB) didn't read back from cart SRAM pages `0x20`-`0x21`, twice |
| Yellow | `03FF` | OAM didn't read back from cart SRAM (page `0x2C`) |
| Magenta | `7C1F` | The OBJ palette didn't read back from cart SRAM (page `0x2C`) |
| Green | `03E0` | The overlay image loaded, but its check word didn't match (a byte read wrong) |

After B the screen shows the theme's accent for a third of a second (with
the default theme that may also be red) before Torii starts. A white screen
that never changes is not a refusal: report it as a hang.

**The in-game probe** tries, in game mode, what v0.8's in-game menu may
want there and the emulator can't tell: cart SRAM pages 0x20-0xEF, the RAM
page register's readback, a flash 128K save's bank across page changes, a
write to the game's PSRAM, the clock with `0x096A0000` on and off, and
SRAM's four wait-state settings (its SD test is retired: on the real cart it
wedged the card until a power cycle, so the report says "not run"). It puts back what it writes. Send
the log's "in-game probe:" lines from each save type's run: they decide what
the in-game menu's save states and screenshots can rely on (they use cart
SRAM 0x40000-0xD8FFF in game mode, which only the emulator has shown so far).

The probe runs the cart tests by itself, saves its report and shows page
1/2 (CART); LEFT/RIGHT switch to page 2/2 (PAGE 20). Photograph both pages:
they are the report even if the log never gets it. Then press A, the soft
reset back to the kernel: the report waits in cart RAM page 0xF0, which a
power-off clears. If the kernel then says "Can't read the microSD card"
(it retries the mount three times first), note it, and only then switch off
and on: the probe also
keeps the report in its own save (SRAM and flash save types; the screen's
SAVED line says whether), so the next boot still logs it ("the report from
its save"), with how the run ended. Send the photos and the lines.

**The copy's estimate:** during a Clean boot the cart copies the game into
PSRAM by itself, and the bar moves on an estimate from the file size
(`PROGRESS_COPY_RATE` in `source/progress.h`, bytes a frame). Each launch
logs the real figure: `launch: the cart copied N KB in F frames, B bytes a
frame (the estimate: E frames)`. If B differs from `PROGRESS_COPY_RATE` by
more than about 10% over two or three big games, set the constant to their
average.

### The v0.5 saves

The emulator cuts power at every save step, but can't show a real card's
timing or a real game's saves. Do these on the cart with Logging on Debug.

| Check | How |
|---|---|
| A real save, safely copied | play a game that saves (Pokemon, Golden Sun), save in game, Back to menu or power off, power on: "Save sav?" then the copy; `/torii/saves/<rom path>/Main.sav` exists, no `.tmp` or `.bak` beside it, the log shows the verify |
| Power cut during the copy | on the copy's progress bar pull power (a spare card): after the next boot the old save or the new one loads, never a short or blank file, and the retry runs |
| History | save twice with different progress: History (Settings, System) at 5 keeps the older versions under `/torii/backups/<rom path>/Main/`; History Off keeps none |
| A second slot | Select held with A on a game: New slot, name it, Blank; play, save; the first slot's file is untouched; the next launch shows the picker with the last slot preselected |
| The per-game manager | START on the game, Saves: rename, duplicate, delete (the file is in `/torii/trash`), history restore (the old current save is in history), export then import a `.sav` |
| Orphans | move a ROM on a PC, then Settings, Library, Saves: the old path is listed as an orphan; relink it to the moved ROM and the save loads in game |
| Space used | the Saves screen's totals match `du` of `/torii/saves`, `/torii/backups`, `/torii/trash` within a few KB |
| The long-path refusal | a ROM in a folder tree whose save path passes 251 bytes: the launch refuses with the path-too-long message, nothing written |
| Save-type change | change a game's save type from EEPROM 8K to 512 in Game info: the old file goes to history, the notice shows once |

### The v0.9 languages

Copy `sv.lng` from the release's `torii-lang.zip` into the card's
`/torii/lang` first (never let Finder replace the card's `/torii`).

| Check | How |
|---|---|
| Swedish on the cart | Settings, System, Language: pick Svenska with A. Settings comes back in Swedish at once, then home; the log has `lang: sv`. Walk home, a game list and its popup, Saves, Info and the in-game menu of a GBA game started with addons: every text Swedish, none cut in a way the emulator's goldens (`tests/shots/sv-*`) don't show, å, ä and ö drawn |
| After a power cycle | switch off and on: the menu starts in Swedish |
| Back to English | Language, English: Settings comes back in English. Set it back before the Diagnostics run, so its report reads the same as every other |

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
  two give its drift. The menu's clock must match too. The cart doesn't
  change for summer time: an hour fast after the clocks went back (as on
  2026-10-27) is the clock still set to summer time, not drift. Note it,
  then set the time in Settings and answer again.
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
