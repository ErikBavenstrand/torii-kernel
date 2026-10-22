# Roadmap

Torii is a public community kernel for the EZ-Flash Omega DE, led by our own taste. It
promises no compatibility with the official kernel or its SD layout. What sets it apart from
Simple DE and DS Style is the foundation under it: tests, the emulated cart, CI, and a fast
renderer that any visual style can sit on. It takes the best features of both, which are
listed in [Prior art](#prior-art).

Each milestone ends in a release that can be shipped. Every pull request has to pass
`just test` and `just compat` in the emulator. Every release also goes through the release
checklist and the on-cart debug mode on a real cart (see [Process](#process)).

Target hardware is the original Omega DE (black PCB marked EZODE). The kernel detects
Rev.A (LX9) and Rev.B (LX16) itself. The blue-PCB EZODE-B needs a kernel and firmware whose
source EZ-Flash hasn't released, so it isn't supported.

## Done

Each release's details are in `CHANGELOG.md`; the planning, ticket reports and reviews behind them are in git history. Design references: [the tile engine](../design/engine.md), [v0.4's decisions](../design/v0.4/decisions.md), [the hardware](../research/omega-de-hardware.md).

- **v0.2 Foundations:** the inherited bugs fixed with tests (save types, the cheat cap, clock and rumble as separate settings, GB/GBC/NES without the clean-boot menu, sorting, Recently Played, clean boot); strict warnings with `-Werror`, clang-format, clangd, host tests under ASan/UBSan, advisory analysis and pinned tooling; the Chinese strings, HZK12 and CP936 removed and the source made UTF-8; SD logging (`tlog`, launch records, crash detection and dumps, `just logs`); the on-cart Diagnostics with its checklist; the emulated cart and `just compat`, with an in-game menu check on every game.
- **v0.2.1–v0.2.3 Real-cart fixes:** from the first two cart tests: log files, the cart model, reproducible builds, save logging, Diagnostics, WAITCNT `0x4018` in the menu, the EEPROM probe, rumble, the game NOR list's safety (two copies, completion marks, rebuild). v0.2.2 bricked the test cart by writing the boot loader; v0.2.3 confines kernel-NOR writes to `Save_info`'s window, the emulator stops any write to the loader, and `tests/loader_safety.sh` runs every writer. **v0.2.1 and v0.2.2 must not be used** (`docs/recovery.md`).
- **v0.3 Renderer, v0.3.1 Themes, v0.3.2 Fixes:** the mode 0 tile engine replaced the official mode 3 menu (a 60 fps frame loop, jobs, an IWRAM glyph blitter, views and widgets, springs and tweens, covers in PSRAM, golden screenshots and a cycle budget per frame); proportional Galmuri fonts, UTF-8 names of any length; SD themes (dropped again in v0.4); then about forty fixes from emulator validation and two cart tests.
- **v0.4 Browsing:** a carousel home with a page per item (Recent, Favourites, each console, SD card, NOR, Settings, Info); the "Paper clean" look in built-in light and dark themes; the library index of the whole card; favourites and recents on SD; clean, custom and database names; hidden system files; Settings on one page with a time editor; rename with an on-screen keyboard; the opening zoom and theme wipe; all 18 engine follow-ups from v0.3.
- **v0.4.1 Art:** box art pre-converted on the PC with a cover cache, the No-Intro names database (`just names-update`), `just art-add` and `just art-scrape`.
- **Cart-test follow-ups (C1–C5, D1–D2, before v0.4.0):** L/R unbound in game lists so L+A hard-resets; a floppy save mark; the GBA boot mode setting with a per-game override; Settings saved on each confirm; Start on faces a carousel item; romhacks keep their own names and art (CRC only); hotkeys renamed and the direct save/load patch removed; one drawn placeholder box per console.
- **v0.4.2 Names on the cart:** new games get their CRC, No-Intro name and box art in the background with no PC step; the full art pack `/torii/art.pak` (`just art-pack`); crc.txt dates kept across a time-zone skew; the E1–E5 fixes (two settings copies, card read errors, rename cuts, the hotkey and cheat-parser fixes, Favourites and crc.txt speed, overlay clipping, dead code).
- **v0.4 cart test (2026-10):** the manual test of v0.4–v0.4.1 on the cart (boot and migration, carousel, lists, rename, SD and NOR pages, Settings, Info, in game, both themes) passed with no findings.

## v0.4.3 Before v0.5

What v0.4.2 left open, checked against the code on 2026-10-22. Strike an item out when it's fixed or settled, then drop it at the next cleanup.

### Background CRCs (F2's findings)

- ~~A launch throws away a partial CRC, so a big game, and every game after it, may never get one.~~ Fixed (`d30126d`): a launch writes the CRC so far to `/torii/crc.part` (a line with its own check) and the next boot goes on from there if the file's size and date still match; the library's new games after Recent's are read smallest first, in five size classes (1, 4, 8 and 16 MB), so a big game no longer holds up the rest. The cut and the resume are tested by hand-stepping in `tests/kernel/test_names.c` (`test_names_partial`), with no frame counts.
- ~~Every 32 names or 10 s the index is rebuilt by a full card walk.~~ Fixed (`9fc6f29`): the names go into the index in place, one per job step (`library_name_found`), and a single walk follows the run. The index is written again; its walk hash is stale until that walk, so a run stopped by a launch costs the next boot one rebuild.
- ~~An unreadable game is read again, with its error logged, after every walk.~~ Fixed (`d30126d`): up to 64 paths are remembered until the next boot.
- ~~The cursor's game follows a new sort on console pages only, and only across one new index.~~ Fixed (`9fc6f29`): the page keeps the cursor game's path as the cursor moves and finds it again in the rebuilt list, on console pages and Favourites (Recent keeps its rows; its order is the clock's).
- The in-place redraw of a new index can show mixed old and new rows for a frame or two. Now it lands twice a run (the names in place, then the walk's index) and was not measured again; settle it on a cart.
- Left, unmeasured: what one name costs in the index (a rename in place moves two runs of the order; one per job step), and how much of the pool a run of thousands of names takes before the walk (each adds its file name, key and display name); a full pool falls back to the walk alone.

### Tools and tests

- ~~IWRAM free is 4,604 bytes on main (CLAUDE.md records 5,980 at v0.4.1).~~ Settled: nothing took it. v0.4.1 had 4,604 free and v0.4 had 4,620; CLAUDE.md's 5,980 was mis-recorded. CLAUDE.md now gives v0.4.2's figures (IWRAM 4,596, EWRAM 18,140 before this work, 16,268 after); the margin over the stack estimate (2,640 bytes) is about 1.9 KB.
- ~~No test drives Identify with a record a few hours off.~~ Added (`d6f22b1`): `test_names_identify` shifts Wars's record by an hour and checks Identify keeps the CRC and takes the file's date.
- ~~The relist after a custom-art rename has no test.~~ Added (`d6f22b1`): `tests/boxart.sh` renames a custom-art game on the SD page and checks the art moves, is listed again and shows under the new name.
- ~~names-update recomputes the CRC of a file dated before 1980 on every run.~~ Fixed (`4bd2627`): such a record, and one the cart dated 0, holds while its file is older than crc.txt.
- ~~art-pack's downloaded counter can read low under threads.~~ Fixed (`4bd2627`): counted under the fetcher's lock.

### Decisions

The user accepted every decision the v0.4 build made on their behalf (2026-10-21); [docs/design/v0.4/decisions.md](../design/v0.4/decisions.md) records them. Change any of them as a normal roadmap item.

F1–F3 (new games named and boxed on the cart) made these; the user confirmed F2's on 2026-10-22:

- F1 (`e04827c`): the kernel keeps a CRC whose date is 1–14 whole hours off the file's, minutes and seconds equal, for every record (nothing marks the tool's); a different same-size file with such a date keeps the old CRC. A kernel before v0.4.2 forgets tool-written dates.
- F2 (confirmed 2026-10-22, built in `d30126d` and `9fc6f29`): a card with names.db (or art) but no crc.txt has every game new, so the background identifies the whole library across boots; the queue is Recent's games, then the rest smallest first, and a big game keeps its partial CRC across launches; a new name sorts the index again, on every list, with the cursor kept on its game.
- F3 (`b3abac5`, `8b4a77f`): the art pack's index takes 128 KB of PSRAM after `PSRAM_NAMES_TEXT`, out of the 384 KB left for v0.9's language tables; NES art is fetched when the card has NES games, else only with `--console NES`; a stopped run replaces a good pack with what came in (a rerun completes it from the cache); fast seek (`FF_USE_FASTSEEK`) is on for all of FatFs (4 bytes a FIL); fixed 3,248-byte records; duplicate covers are found by identical converted bytes; pack covers appear once the link map is built, a few seconds after boot on a small-cluster FAT32 card.

### Left from earlier work

- The rename's card walk blocks the keyboard for its length, unmeasured on a cart (about 1 s per 2,000 files estimated).
- A same-size file swapped in between a names-update and the next boot, with an older date, keeps the old CRC.
- The Settings held-scroll golden hashes every frame, so code-layout changes force a re-record.
- 9 cached art-pack pictures weren't PNGs (libretro aliases served as text?): those games get no cover. The pack's conversion (pure-Python `art.py`, about 29 s on 14 cores) is its limit past 256 requests at a time.

### Open questions and cart checks

- **Untried on a cart since v0.2.2** (the v0.4 cart test covered sleep, cheats, a NOR write and delete, Back to menu and an in-game save): save states, DEBUG, GB/GBC, NES, rumble with a game, the EEPROM probe's verdict, the clock against real time (all in the release checklist).
- **Unmeasured on a cart:** the ROM copy rate (`PROGRESS_COPY_RATE`); the walk of a full card; SD folder open time (0.2 s per 300 files emulated) and the 251k-cycle frame on a folder of 1,024+ subfolders; covers from an art folder of thousands of files; Identify (about 1.6 s per MB, estimated) and crc.txt's write steps; About's first free-space read on exFAT; `CHIP_ERASE_FRAMES` from a timed Format all.
- **Hardware unknowns:** whether an SD write right after the FPGA's ROM copy is safe; what the real BIOS does on an undefined instruction (`docs/research/omega-de-hardware.md`).
- **Licences:** No-Intro's terms for a derived name table (built on the user's machine, never bundled); libretro-thumbnails has no licence grant (fetched locally, never committed); the boxes' trade dress.
- **PSRAM map:** 384 KB is left for v0.9's language tables (`source/engine/psram.h`); the user was unsure about fixing the map.

## v0.5 Saves

### SD layout

Everything the kernel stores lives under `/torii/`. ROMs can be anywhere.

```
/torii/
  torii.ini               settings: commented key = value, with "version ="
  layout.ini              layout version, for migrations
  saves/<rom path>/<slot>.sav
  states/<rom path>/<slot>/<n>.state
  backups/<rom path>/<slot>/<timestamp>.sav
  games/<code>-<crc>.ini  per-game overrides (save type, RTC, cheats)
  art/<system>/{title,box}/<code or name>.lz
  lang/<code>.lng
  plugins/plugins.ini + emulator binaries
  cheats/<code>.cht
  trash/
  recent.txt
  favorites.txt
```

A setting can keep saves next to the ROM instead. Importing old `SAVER/` saves is deferred.

### Integrity

Saves must never be corrupted. A game writes its save to the cart's save chip, and the
kernel copies the chip to SD on the next power-on. The game's own write is out of our hands.
The kernel's copy is made safe like this:

1. **Write and verify:** write `<slot>.sav.tmp`, `f_sync` it, read it back and compare a
   checksum against the chip.
2. **Swap in safely:** keep the old file as `.bak`, then rename the tmp file into place.
   FatFs `f_rename` refuses to overwrite and isn't atomic (it registers the new entry before
   removing the old one), so boot finishes or rolls back any interrupted step.
3. **Keep the flag until verified:** the "unsaved session" flag stays set until the copy
   checks out, so a failed copy is retried on the next boot.
4. **Sanity checks:** a blank chip (all `FF` or `00`), or one identical to the file, is
   skipped without making a backup. A chip that doesn't look like the slot that was loaded
   asks before writing. This also ends the blank 64 KB `.sav` that every game missing from
   the save table gets today (save mode `0x10`, from `Check_saveMODE`): it gets no file at
   launch, and one only once its chip differs from the blank pattern the kernel loaded.
   `tests/saves.sh` checks it with `hello.gba`, which never saves.
5. **Write failures:** a failed write shows a clear error and keeps everything. Today
   `Save_savefile` opens the `.sav` with `FA_CREATE_ALWAYS`, so a copy that runs out of
   room on a full card has already truncated the good file it replaces (`tests/saves.sh
   full` leaves a cut-short `Test.sav`); step 1's tmp file and step 2's rename end that.
6. **Tests:** the emulated cart cuts power at every step, and the test checks that a good
   save always survives.
7. **Save-type changes:** when a game's save type changes, for example EEPROM 8K to 512, its
   existing save is converted or kept untouched in the slot's history. It is never silently
   truncated.

### Slots and history

- **Slots:** named, deliberate branches, such as Main, Nuzlocke or Speedrun. A game with more
  than one slot shows a picker at launch, with the last slot preselected.
- **History:** each slot keeps its last N versions, with N set in settings. You don't launch
  a backup. Restoring one makes it current and pushes the old current save into the history,
  so nothing is lost and no hidden branch appears.
- **Per-game save manager:** new, rename, duplicate and delete slots. Browse a slot's
  history, restore a version, and import or export a `.sav`.
- **Global save manager:** orphaned saves whose ROM moved, relinked in one step, and the
  total space used.
- **Deleting:** a deleted slot goes to `trash/` and is removed after 30 days.

## v0.6 Plugins

- **Handoff:** keep the PogoShell handoff exactly as it is, so existing emulators work
  unchanged. The plugin goes at PSRAM 0 with the ROM after it, the argv block is at
  `0x0203FC00` with magic `0xFAB0BABE`, the ROM address is at `0x0203FBFC`, there is a fake
  romfs header, and `.mb`/`.mbz` plugins run from EWRAM.
- **Plugin table:** replace the guess-by-file-name `/SYSTEM/PLUG/<ext>.bin` lookup with
  `plugins/plugins.ini`, listing extension, binary, save type and size, icon, display name
  and boot method.
- **Saves:** emulator saves use the slot system, which ends today's name collisions between
  folders.
- **Jagoomba from source:** build Jagoomba Color v0.5 from source in the repo, replacing
  the prebuilt `jagoombacolor.h`. Fix the palette not persisting (ez-flash/omega-de-kernel#12)
  there: `GFX_reset` in `lcd.s` overwrites the palette that `readconfig` loaded from the
  game's `.esv` with the GBC BIOS palette on every boot, so apply the automatic palette only
  when the save holds no config yet.
- **Emulators:** Jagoomba Color is built in as a default that the table can override.
  PocketNES and the rest load as plugins: sms, gg, sg, sv, ws, wsc, ngp, pce, z80, col, c8,
  nes, plus the viewer plugins.
- **Extensions:** none for now. File types, launch handlers and menu actions go through
  internal registries, so an extension system can be added once there is something to put in
  it.

## v0.7 In-game menu

The menu that exists today is patched into each game's IRQ handler and runs from the ROM
tail. It offers RESET, SAVE, LOAD (one real-time save), CHEAT ON and CHEAT OFF, with
L+R+Start for reset and L+R+Select for sleep. It becomes a proper overlay:

- **Save states:** slots for each save slot, with timestamps.
- **Cheats:** a toggle for each cheat.
- **Actions:** return to the Torii menu, reset, sleep, and a screenshot to SD.
- **Look:** the theme's colours and font, within its memory budget.
- **Hotkey:** configurable.
- **Fixes it brings:** LOAD with no saved state shows a message instead of crashing (it
  jumps to 0xFFFFFFFE today). Drawing with tiles leaves the game's BG2 affine registers
  alone.
- **Test:** every `just compat` game opens and closes the menu, and the screen and game
  state have to come back unchanged.

- **Review first (user, 2026-10-19):** before building it, review the current in-game menu and prototype the overlay. It can't run the kernel's engine in place (the game owns RAM and VRAM, and the kernel's ROM isn't mapped), so it needs a compact copy of the engine's text and theming. The prototype has to save and restore the game's screen and memory without breaking any `just compat` game. Picking cheats in game is part of it.

## v0.8 Cheats

Today the only cheats are constant byte writes from EZ-format `.cht` files, applied on the
VBlank or keypad IRQ.

1. **Every format, parsed at launch:**
   - **Formats:** GameShark v1/v2, Action Replay v3, CodeBreaker and raw/VBA codes, in both
     libretro `.cht` files and the old EZ format, using mGBA's decoders (MPL-2.0, file-level).
   - **Plain writes, lists and slides:** expanded into the per-frame table, with 16- and
     32-bit writes.
   - **ROM-patch codes:** written once into the PSRAM game image, with no cap.
   - **Master and hook codes:** not needed, because Torii already hooks the IRQ.
2. **Per-frame interpreter:**
   - **What it runs:** a small interpreter in IWRAM for conditionals, ELSE/ENDIF blocks,
     button activators, pointer writes and add/or/and codes.
   - **Speed:** it has a cycle budget measured in the emulator.
   - **Space:** it needs its own size budget. Today's patch area appended to the ROM is
     0x2000 bytes: a 0xD04-byte handler plus 8 bytes per byte-write, which caps cheats at
     600. Plan the new engine's code and code storage separately from that table.
   - **Hook check:** `just compat` reports games the hook misses.
3. **Goal and tests:** every documented code type, each with test vectors. Types that behave
   differently on a kernel than on the original devices, such as slowdown codes, are
   documented, not dropped.
4. **Typing codes on the GBA:**
   - **Entry:** a hex keypad that detects the format as you type.
   - **Storage:** codes are saved under "My codes" in `cheats/<code>.cht`.
   - **Code search:** narrowing RAM between in-game snapshots comes later.
5. **Cheat database:** not bundled. libretro's is CC-BY-SA-4.0, so a `just cheats` tool
   fetches and converts it, with attribution.

- **From v0.3:**
  - Opening a big `.cht` file freezes the cheat screen while `Analyze_KEYVAL` runs: run it as a job with progress (`Open_cht_file`); leaving one is slow too, as `Get_CHT_val` re-reads the file for each pick (`source/showcht.c`).
  - The parser drops spaces from cheat names ("Infinitelives"), more visible in the proportional font.

## v0.9 Languages and firmware

- **Strings:** every UI string has an ID, with English in `lang/en.ini` compiled in as the
  fallback.
- **Language packs:**
  - **Authoring:** translators edit `lang/<code>.ini` as text.
  - **Build:** the build compiles each file into a binary pack (`.lng`, Rockbox-style), whose
    version header rejects stale packs.
  - **Lookup:** an offset table in PSRAM, with English used for any string a pack lacks.
- **Placeholders:** numbered (`{1} of {2}`) and checked against English at build time. Plurals
  are avoided by wording, or written as `.one`/`.other` variants.
- **Scripts:** Latin-1 first. Other scripts come as extra font files.
- **Firmware updater:** kept, behind a confirmation screen and a battery warning. It only
  runs when the embedded firmware is newer than the cart's. It is tested in the emulator, and
  it refuses EZODE-B carts.

Then 1.0.

- **Fonts:**
  - Glyphs outside Latin-1 draw a replacement box today.
  - There is no kerning.
  - Galmuri9's Ç ç ¡ ¿ µ lose a tail pixel at 12 px.
  - Per-glyph row sizes would save about 1.5 KB of ROM (`tools/font.py`).

## Backlog

Small items with no version yet:

- **Logs:**
  - Frame-timing summaries carry the log write's time, not the scroll's.
  - They are DEBUG only.
  - Their table holds 8 names.
  - On a nearly full buffer they can push out older lines (`source/engine/cycles.c`, `tlog_flush`).
- **Crash dumps** name stale stack words that point into read-only data (`t_model+0x2628`) (`source/cartlog.c`, `tools/logs.py`).
- **Diagnostics wording:** "3 seconds passed in about 2.2".
- **Lost at power-off:** the session record in cart RAM page `0xF0` doesn't survive a power cycle (hardware). Keep it somewhere battery-backed if wanted.
- **PSRAM:** where a DMA into PSRAM from cart space lands on the real cart is unknown. Nothing does it any more.
- **Logs:** the save log line still says `"Save sav?"`; change it together with `tests/saves.sh`.
- **Latent risks:**
  - The second half of `Save_savefile` runs frames with SRAM page 0x10 selected.
  - `FRAM_save_op` runs frames with a file open and the working folder at `/SAVER`.
  - Both are safe today and commented.
- **Release file:** EZ-Flash's loader installs only the first 3 MB of `ezkernelnew.bin`; `tools/omega_image.py` enforces it.
- **Emulator:**
  - The kernel install fills the last sector's tail with 0xFF, where the real loader writes stale bytes.
  - `OMEGA_FPGA_VERSION` isn't validated.
- **Engine:** `ui_show` on a parent doesn't hide its children (`source/ui/ui_core.c`); U+00AD draws as a visible hyphen (`font_body.c`).
- **Firmware update:** the screen is blank during its CRC check (`FW_update`, `source/Ezcard_OP.c`).
- **Font tests:**
  - `tools/font_preview.py` needs network for the candidates.
  - `tests/font_test.py` compiles with the host `cc`, not devkitARM.

## Process

- **Per pull request:** `just test` and `just compat` gate every PR.
- **Before each release tag:** run `docs/release-checklist.md` and the on-cart debug mode on
  the real cart. The checklist covers boot and browsing, one game per save type, an in-game
  save plus a power cycle, an in-game menu save state, cheats, and the firmware check screen.
- **Changelog:** every change to `source/` is logged in `CHANGELOG.md` (see CLAUDE.md).

## Prior art

DS Style (FrankieT19, Apache-2.0) descends from Simple DE (Sterophonick), and both are built
on the official kernel. Of what they added, Torii takes:

- the start screen, favourites, recents, view modes, hidden system files and clean names;
- the plugin launcher, boot modes, the quick-start hotkey, the hard-reset toggle and the boot
  progress screen;
- rotating save backups, which become slot history;
- title, box, custom and NOR art, and a scraper;
- light and dark themes with accent colours, one human-editable settings file, Help pages and
  the 12/24-hour clock;
- Jagoomba Color, and PogoShell file types and viewers;
- their bug fixes: the recents overrun, the clean-boot reset and the cheat cap.

It skips Load Style, which runtime themes replace, the Windows customiser and `-ffast-math`.
It may take later: UI sounds, a username, Snake, and a browser-based SD manager.

The EverDrive GBA Pro (December 2025), with 98 save states, an in-game menu, a tunable RTC
and mGBA-format cheats, is the commercial bar to meet.
