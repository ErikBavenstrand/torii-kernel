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
- **v0.4.3 Before v0.5:** the background CRCs (a launch keeps a big game's partial CRC; new games read smallest first; names put in the index in place; the cursor follows its game), a new index redraws only the rows that changed, the rename's card walk shows a progress dialog, `art_scrape` follows libretro-thumbnails links, and the measured pool headroom. Accepted limits: a same-size file swapped in with an older date keeps its CRC (delete `crc.txt`). Cart-checked 2026-10-22.
- **v0.4 cart test (2026-10):** the manual test of v0.4–v0.4.1 on the cart (boot and migration, carousel, lists, rename, SD and NOR pages, Settings, Info, in game, both themes) passed with no findings.

## v0.4.3 Before v0.5

Released (2026-10-22); its details are in `CHANGELOG.md`. What stays open for later cart runs:

### Open questions and cart checks

v0.4.3's cart run passed (the user's word, 2026-10-22). What it didn't cover or measure stays here for the next one; none blocks code.

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
  states/<rom path>/<slot>/<n>.state   reserved; written from v0.7
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

A setting (Settings > Games > Save location) can keep saves next to the ROM instead, as `<rom dir>/<rom stem>.<slot>.sav`. Old `SAVER/` saves are not imported: v0.5 keeps no backwards compatibility (decided 2026-10-22).

What v0.5 built of this (`source/gamesave.c`, `gamerec.c`): a game's folder `/torii/saves/<rom path>/` holds its slot files and `game.ini`, its record (`save_type = 0-5`, `boot_mode = 0-2`, `slot = <current slot>`; plain text, missing keys are defaults). The record is there whatever the setting says, so changing it loses nothing. Folders are made when the first file is written (a game of unknown save type gets none until its chip differs from blank). A NOR game's saves are kept as for a ROM at `/torii/nor/<name>`, whatever the setting. A save path over 251 bytes (the pending-save record's room) is refused at launch with a message. The per-game overrides listed above as `games/<code>-<crc>.ini` (RTC, cheats) stay for v0.7; the save type and boot mode are by path, in `game.ini`.

v0.5 ships all of this at once: integrity, slots, history and both save managers are built and tested locally, then released together (decided 2026-10-22). Save states are not part of it; they come with v0.7's in-game menu.

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
   save always survives. Each step logs a numbered `tlog` marker; the emulator cuts power at marker N, and a test loops N over all markers, checking that the save and its `.bak` survive and boot recovers.
7. **Save-type changes:** when a game's save type changes, for example EEPROM 8K to 512, its
   existing save is converted or kept untouched in the slot's history. It is never silently
   truncated.

### Slots and history

Decided 2026-10-22: a version is kept only when the chip differs from the current file, at the copy-to-SD after a play session; N defaults to 5 (Settings). Backups are named `<n>-<YYYYMMDD-HHMMSS>.sav`, where the rising counter `n` fixes the order, so a wrong clock can't reorder or overwrite history. Saves live in `/torii/saves/` by default, next to the ROM by setting. A save-type change is never converted: the old file stays untouched in the slot's history, the new type starts from the chip's blank state, and a notice is shown.

- **First slot:** every game's first slot is `Main` (`Main.sav`). It can be renamed, and deleted once another slot exists.
- **Creating a slot:** a game with one slot launches straight in. Hold Select while pressing A to open the picker, which lists the slots and a "New slot…" row; the per-game manager has the same action. A new slot asks for a name (on-screen keyboard), then "Blank" (default) or "Copy of <current slot>". A game with two or more slots shows the picker on every launch, the last slot preselected.
- **Where things live:** the per-game manager is a "Saves" entry in the game's Start menu; the global manager is a "Saves" row in Settings. The picker stays a chooser (slots, New, Rename on START: the GBA has no X key); the rest is in the manager.
- **Slot names:** UTF-8, at most 24 characters, unique per game ignoring case; `\ / : * ? " < > |` stripped, trailing dots and spaces trimmed. A rename writes a journal entry first (old and new name) and boot recovery finishes or undoes it, like the `.bak` swap; the power-cut test sweeps rename too.
- **Slots:** named, deliberate branches, such as Main, Nuzlocke or Speedrun. A game with more
  than one slot shows a picker at launch, with the last slot preselected.
- **History:** each slot keeps its last N versions, with N set in settings. You don't launch
  a backup. Restoring one makes it current and pushes the old current save into the history,
  so nothing is lost and no hidden branch appears.
- **Per-game save manager:** new, rename, duplicate and delete slots. Browse a slot's
  history, restore a version, and import or export a `.sav`. Built (`source/screens/game_saves.c`): "Saves" in a GBA game's Start menu (Games lists, SD card page, game NOR page) opens the slots with the current one marked; A on a slot lists Launch with this slot (no picker follows), Set as current, Rename, Duplicate, Delete (asks; the slot goes to `/torii/trash` for 30 days; the last slot is refused), History (A on a version asks, then restores) and Export (`/torii/export/<game> - <slot>.sav`, numbered if it exists); "New slot..." and "Import .sav..." (a folder browser from the ROM's folder showing folders and `.sav` files, then the slot to replace, then a question; a file of the wrong size is refused) close the list. Limits: a GBA game only; on the SD page the entry is left out when the popup is already full (nine rows: addons on); a folder lists 28 entries at most; tests `tests/game_saves.sh`.
- **Global save manager:** orphaned saves whose ROM moved, relinked in one step, and the
  total space used.
- **Deleting:** a deleted slot goes to `trash/` and is removed after 30 days.

What v0.5 built of slots and history (`source/slotstore.c`, API in its header; tests `tests/kernel/test_slotstore.c`, `tests/slotstore.sh`): the Settings row **History** (System section: Games' card is full at 6 rows; Off/1/3/5/10/20, default 5) sets the versions kept. The save copy pushes the version it replaces into the history before the swap (the record holds the history folder from halfword `0x82`); a repeat after a cut adds none (the newest version is compared first), and a history that can't take the version fails the copy, the file staying as it is. Restore, import and the type change always push, whatever the setting (pruning to at least 1). Restore moves the version out of the history (it is the file now). Renames, deletes and installs (restore, import) are journaled in `/torii/slotop.txt` (CRC) and `slotstore_recover()` finishes them at boot; a history folder is never moved to another parent (a cut leaves its `..` wrong): the trash takes its files one by one. A launch whose slot file is bigger than the save type needs (EEPROM 8K to 512) pushes it untouched into the history, starts the new type blank, and shows a one-time notice; a smaller file is still grown with FF, which keeps its bytes.

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
  - `FRAM_save_op` runs frames with a file open.
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
