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

## v0.4.2 Before v0.5

Everything the v0.2–v0.4.1 work left open, checked against the code on 2026-10-21. v0.4.0 is tagged after the cart test now under way ([v0.4-manual-test.md](v0.4-manual-test.md), then [the release checklist](../release-checklist.md)); delete the manual test once its findings are logged here. Strike an item out when it's fixed or settled, then drop it at the next cleanup.

### Bugs: data and saves

- `Read_SD_sectors` always returns 0, so FatFs never sees a card read error and every "read failed, don't write back" path above it is unreachable on a real cart (`source/Ezcard_OP.c`, `source/ff15/diskio.c`). Fix before v0.5's save work relies on read checks.
- A power cut during a settings write leaves the whole settings sector erased, so every setting reads its default: `Save_info` erases one sector and programs it with no second copy. Since C3 every confirmed change is a sector write, so it happens more often, and wears the NOR more (`source/Ezcard_OP.c` `Save_info`; `source/screens/settings_page.c`).
- A failed settings write isn't retried and shows no message; the live theme stays (`settings_page.c`).
- ~~An SD rename's `f_rename` cut by power can leave two names on one cluster chain; the rename journal's recovery leaves both (`source/actions.c`). The rollback of a failed move is checked only by reading the code: no test forces a move failure.~~ Fixed by E2 (`a3a746c`, `8fd8817`): recovery drops the second name without freeing the chain (`f_unlink_alias`, `f_unlink_case_aliases` in `ff.c`); `tests/saves.sh rename-cut-split` and `rename-cut-case` sweep every cut; the rollback has a kernel test (`action_rename_fail_file`).
- ~~The shared-save check before an SD rename sees only indexed games, so a game in a hidden folder sharing the save name loses it (`actions.c`).~~ Fixed by E2 (`fa59a3f`, `8fd8817`): a walk of the whole card checks the old and the new save name; a folder it can't read or past 16 levels refuses.
- ~~A hand-edited `/torii/names.txt` over 116 KB is silently ignored by the walk and by Rename (`LIB_NAMES_MAX`, `source/library_scan.c`).~~ Fixed by E2 (`ba2ebcd`): the walk logs it once a boot as an error and Rename says "names.txt is too large to change"; the cap stays (Rename's buffer and the PC tool share it).
- ~~With `/torii` as a file or a write-protected card, the index is never saved, so every boot logs the error and scans again (`library_scan.c`).~~ Fixed by E2 (`a3a746c`, `1edc9f1`): the index stays in RAM, one error a boot, no write tried again that boot; each boot still scans.
- D1's cart logs: after a session that ran Rescan and Identify, the next boot found "nothing in cart RAM" (page `0xF0`'s magic cleared?), and the index loaded from `library.bak` on several boots (maybe just the copies alternating). Not reproduced (`source/cartlog.c`, `library_scan.c`).
- Deleting the running log file with LOGGING on stops SD logging until the next boot (`source/tlog.c`).
- After a power cut drops a half-erased NOR game, a "kernel error: … dropped" line repeats every boot until the next list save (the NOR list reader).
- ~~Non-canonical paths such as `/deep/../x.gba` launch and are stored in Recent as given (only the test hook can make one; `source/userdata.c`).~~ Fixed by E2 (`492cb53`): `userdata_canonical` at launch, Recent and favourites.
- ~~Reset keys and Sleep keys that already clash (written by v0.3 or by hand) are used as they are; only new combinations are checked (`source/settings_model.c`).~~ Fixed by E2 (`0d4e2b2`, `8fd8817`): `settings_hotkeys` falls back to the defaults for Settings and the launch's patches, logged at boot.
- ~~The cheat parser's `Get_KEY_val` and `Get_CHT_val` fill a 50-byte `_paramk` with no NUL and pass it to `strcmp` (`source/showcht.c`).~~ Fixed by E2 (`01ee08b`, `8fd8817`); names keep their inner spaces too.
- ~~E2's findings: with `/torii` a file Recent, the favourites and names.txt logged an error at every write; one damaged folder blocked every rename of a game with a save; the cheat list and a cheat's value split long lines differently; a NOR row took the first SD file of its name whatever its size.~~ Fixed by E2 (`faded2a`). Left: the rename's card walk blocks the keyboard for its length, unmeasured on a cart (about 1 s per 2,000 files estimated).
- The launch copy's VBlank hook sets the handler back to none instead of restoring the previous one; harmless while the engine installs none (`source/progress.c`).

### Bugs: menu

- ~~Opening Favourites with thousands of entries blocks for up to about 1.8 s (3,367 favourites), and again after un-favouriting or a new index (`source/screens/games.c`).~~ Fixed by E3 (`b4bf219`): 3,400 favourites over 4,096 games sort in 26 frames (198 before), and the order is kept between visits and changed in place.
- ~~Slow work on the menu's thread: parsing a big `names.txt` is one walk step (0.26 s per 1,000 sorted lines, 0.82 s unsorted; `gamename_custom_parse`); a large `crc.txt` takes about 6 s of background work after each boot (3,000 records, emulated); before a launch, unwritten CRCs are written synchronously, about 0.5 s per 1,000 records (`names_before_launch`, `source/names_store.c`). A read-only `crc.txt` is never updated, so its work repeats every boot.~~ Fixed by E3 (`8839032`): `names.txt` parses in steps; crc.txt loads in one pass with its hot loops in IWRAM (3,008 records: 6.2 s of background frames, 9.3 s before); a launch writes only the new CRCs, to `/torii/crc.new`; a read-only crc.txt is detected and skipped.
- ~~A popup's first draw (143k cycles) and a confirmation's (170k) land a frame late while the library's background scan runs (`source/ui/ui_popup.c`).~~ Fixed by E3 (`09dff4f`): a frame with a view's first draw runs one job step; a confirmation's first draw is 146k now.
- ~~While an overlay moves only one list clips, so a page with two lists shows the second's spare rows for 4–6 frames; over a popup, the confirmation's corners show the dimmed page for 1–2 px; behind NOR's Delete question the popup's edge and Delete row show; a popup over row 0 lets the row's size peek out above it (`ui_popup.c`).~~ Fixed by E3 (`09dff4f`): two lists clip to the box round both; a confirmation takes its popup's place; a popup opens from its row's lines.
- ~~A key pressed while START waits for an SD page change reaches the page after the popup closes; START and B mashed 3–4 frames apart over a popup can drop the START and pass B to the list (`source/screens/sdcard.c`, `ui_popup.c`).~~ Fixed by E3 (`7065c18`).
- B pressed on the frame a hotkey combination is released counts as part of the hold (`settings_page.c`).
- While the Time/Date card is open only `ui_frame` runs: the library walk and a Rescan pause until it closes (`settings_page.c`).
- ~~The selected row's marquee keeps scrolling under an open popup, about 13k cycles a frame (`source/ui/ui_list.c`).~~ Fixed by E3 (`7065c18`).
- ~~A first visit to a game page right after a failed launch briefly shows an empty card until the index reloads (`games.c`).~~ Fixed by E3 (`37ea646`): the page loads the index first.
- On a boot with no index yet the consoles count 0, so a Start on console falls back to Last place (`source/screens/nav.c`).
- ~~After an SD rename of a game with a custom name, the row shows the cleaned name for a few seconds until the walk restores it.~~ Fixed by E2 (`84fc3ef`).
- ~~Two games that clean to the same name in different folders with the same tags show the same name: rows pass file names, not paths, to the dedupe (`games.c`, `source/gamename.c`).~~ Fixed by E2 (`84fc3ef`).
- ~~NOR rows show cleaned file names, never custom or database names (`source/screens/nor.c`).~~ Fixed by E2 (`84fc3ef`): the library record with the same file name gives the name.
- The theme wipe needs a commit every VBlank: a frame that overruns during the 16-frame wipe shows the old theme whole-screen (`source/ui/ui_run.c`).
- A launch shows about 8 black frames between home's fade-out and the loading card.
- A cover landing during a palette fade could show wrong colours (not seen; `source/engine/cover.c`).
- A failed `job_start` for the art listing is retried every frame and logs each time (`source/boxart.c`).
- ~~The SD page's item count job costs up to 194k cycles a frame while it counts a folder (`sdcard.c`).~~ Fixed by E3 (`09dff4f`): one step a frame, 92k at most.
- Diagnostics' first page draws its prompt lines a frame after `screen ready: diag-start` (`source/diag.c`); passes show in plain text, as the palette has no green (`source/diag_priv.h`).
- `ui_hints_get` doesn't give back a hint note's ink, so a popup over a danger or countdown line restores it as plain text (can't happen today; `source/ui/`).
- ~~BG block 0's tiles on list pages are nearly full with a popup and a confirmation up (9–20 spare); a taller popup falls back to closing the popup. The popup's 8 entries (`UI_POPUP_ITEMS`) are full on the SD file popup.~~ Fixed by E3 (`09dff4f`): a confirmation borrows the popup's tiles when its own don't fit; popups hold 9 items (9 without a title), and the SD popup offers both.

### Dead code and cleanup

- ~~Eight icons are drawn by nothing (8x8 Favourites, Recent, SD, Settings, torii and up; 16x16 file and folder; `assets/ui`).~~ Fixed by E5 (`dd4414a`): deleted.
- ~~`action_launch`'s result (`ACTION_NOR_MAYBE`, `ACTION_NOR_WRITTEN`) is ignored by every caller (`source/actions.c`); `library_games_seen` has no caller (`library_scan.c`); `art_drop` and B2's unlogged loaders are dead (`source/ui/art.c`); `struct gamename_io` and `gamename_custom_load`/`_store`/`_store_move` run only in host tests (`gamename.c`).~~ Fixed by E5 (`dd4414a`, `c57ceb8`): all dropped; `action_launch` already reads the NOR list again after a write.
- ~~Settings' Mode B is only stored, as in v0.3: confirm on a cart in a DS that something reads it, or drop the row (`settings_model.c`).~~ Settled by E5 (`73807f9`): the cart reads slot 15 itself in mode B (its A/B switch), so the row stays; `docs/research/omega-de-hardware.md` says so, from secondary sources (not checked on a cart).
- ~~`crc.txt` still writes the GBA game-code column and `names.db` keeps an empty code table, unused since C4 (`tools/names_db.py`, `source/namesdb.h`).~~ Fixed by E5 (`6a028ec`): names.db version 2, crc.txt without codes; v0.4.1's files still read.
- ~~`SET_LABELS` and `SET_LABEL_RESET_*` now index explanations only (`source/lang.c`); `tools/mockup/settings.py` uses the old hotkey row names; `docs/design/v0.4/palette.png` predates B9's contrast change.~~ Fixed by E5 (`d4fc1ea`, `ae9fbd2`, `f13f7de`): `SET_EXPLAIN_*`; `tools/mockup/palette_sheet.py` draws palette.png.

### Tools and tests

- ~~`just art-scrape` and `just names-update` print a traceback on a read-only or full card instead of a message (catch `OSError` in `main`); a 403 or other 4xx isn't remembered as a miss; `Pokemon Trading Card Game 1.gbc` is "not fetched" on every run; a run right after `names-update` recomputes CRCs; names differing only in case share a cache file on macOS (`tools/art_scrape.py`, `tools/names_db.py`).~~ Fixed by E5 (`bb677fb`, `7221cf0`): the TCG's No-Intro name gets a 403 from libretro-thumbnails every time, now a remembered miss; names-update writes each file's FAT date, so records hold whoever wrote them.
- ~~The `diag-tests` golden failed once while the colima VM was busy, then passed unchanged;~~ (E5, `691815d`: not reproduced in 16 parallel runs under load, and the emulation reads no host time; `tests/shots.py` now fails a run whose kernel or emulator changed under it, the likely cause) the Settings held-scroll golden hashes every frame, so code-layout changes force a re-record.
- ~~`tests/nav.sh` routes assume the card starts on home facing its first item, and its log lines are cut at 240 characters, so routes to paths of about 225 bytes never match.~~ Fixed by E5 (`8e799d1`): a test hook turns home to its first item; waits are cut at 240 bytes as the kernel cuts them.
- ~~The UI walk (`tests/shots/data/walk.txt`) uses 54 of mgba-shot's 64 waits (`MAX_WAITS`, `emu/mgba-shot.c`): raise it or split the walk.~~ Fixed by E5 (`8c3e7ae`): 256.
- ~~A failing animation frame writes only its actual PNG, no expected or diff (`tests/shots.py`); mGBA zero-fills RAM where the cart doesn't, so goldens can't catch uninitialised reads (only `pReadCache` is poisoned).~~ Fixed by E5 (`8c3e7ae`, `691815d`): frames kept by hash in `dist/shots-frames` give expected and diff PNGs; mgba-shot fills EWRAM and IWRAM with 0xA5 at power-on (the suite passes).
- ~~No golden covers About's "free space isn't recorded" branch, Your cart's "still counting", File info's date, No-Intro names with odd characters, or a box switching between consoles with no cover between.~~ Fixed by E5 (`691815d`): info-no-free, info-counting, sd-root/file-info, games-names, games-box-switch.

### Decisions to review

Made during the build without the user; each is cheap to change. The user's answers so far: settings save on confirm and boot faces a carousel item (C3, D1), romhacks keep their own names and art (C4), per-console placeholder boxes (C2, D2), the hotkey review (C5), and the cart findings (C1).

- **Research overrides R1–R9** ([decisions](../design/v0.4/decisions.md#research-overrides)): a background re-walk instead of folder dates, two-copy safe writes, the sort order, the name-cleaning rules, 14 px rows with a sprite pill, double-size sprites only when zooming, a hard-edged theme wipe, the title slide, the confirmation's dim toward white in light.
- **Favourites are capped at 4,096** although the decisions said no cap (`source/userdata.h`).
- **Starting games (C1):** Play with every addon off boots clean; the SD popup offers both Clean boot and Boot with addon (9 items, so no title; E3), and its Boot with addon skips the cheat screen; Game info's Default doesn't say what Settings' mode is; NOR games ignore the boot mode.
- **Settings:** Back to menu can't be edited while Save states is on (C5); a value is lost if the power goes off in the 30 frames before its write; keys during the write are lost; Settings opens on Theme; L/R jump sections; Dim screen has no live preview; Time and Date set the clock at once.
- **Start on (D1):** its choices are counted when Settings opens (a Rescan inside doesn't refresh them); a hidden stored choice keeps its name.
- **Hotkey rules:** 2–3 keys including L, R, START or SELECT, no overlap with the other hotkey even while it's off, nothing inside A+B+START+SELECT.
- **Hidden files:** PATCH and CHEAT hidden too, and every file the kernel can't start on the SD page; `.sgb` and dot-less files no longer launch.
- **Name cleaning:** 4–5 digit scene prefixes dropped except 1900–2099; the article table can misfire ("Rock, Paper, A"); colliding games show their first differing tag, preferring Rev, Beta and translation tags. Names over 255 bytes use their 8.3 name for the save, so two such files can share one (v0.3).
- **Names and art (C4, v0.4.1):** CRC-only lookup; CRCs computed on the PC; games in hidden folders never get names; played games get theirs after the next boot; art-scrape deletes stale romhack art by default; a dump renamed with its own tag loses its art; existing art is never refreshed; one timeout stops a scrape; custom art is shared by same-named files in different folders; covers load after 8 frames of rest with no fade; Game info shows no cover; at most 8,192 art files.
- **Placeholder boxes (D2):** Nintendo-style trade dress in original pixels (a conscious yes before release); the NES box's torii reads like a skull; the three Game Boy boxes share the silver band.
- **Look:** palette changed for contrast (light ink3 `#8C847B`, dark danger `#EF7B84`); 16x16 icons keep their PNG colours in both themes; look values read off the mockups where spec.md is silent; the Settings and keyboard mockups were approved by the conductor, not the user.
- **Motion:** a page change fades out, then in (about 9 frames, not 6); the zoom's pieces and timing; the hard-edged 16-frame wipe; dialogs rise in 6 frames, the loading card shows at once; in light, text looks lighter than the paper for a frame or two of the card fade.
- **Carousel:** neighbours at 0.89 scale and the far pair at 0.60; held-turn speed; with 2–5 items one item shows at both ends mid-turn; far objects nearly vanish in dark; a fast held turn can miss a label for a frame.
- **Loading card:** titled with the file name; a pending save shows a separate "Saving…" card first, an unsaved index "Saving the game list".
- **Pages:** Favourites shows no stars; list with art shows two facts; SD Delete reaches `/torii` and `/SAVER` with Show all files on; a folder over 1,024 shows the first 1,024 the card lists; SD folder icons blue; LEFT/RIGHT page the SD page and B climbs only on a fresh press; NOR is one card, only its latest game deletable, Format all offered on an empty NOR, tinted amber, not danger; no coloured dots before NOR games (14 px rows can draw them now).
- **Popups:** keys pressed while one closes: moves and START reach the page (START opens it again), A, B and SELECT are dropped; keys pressed while START waits for the list go to the popup; a confirmation hides the popup it asks over (E3); the dim toward white also fades the hint labels in light.
- **Rename:** B never cancels the keyboard; Rename only for indexed SD games; SD rename moves files, not folders.
- **Info:** all wording; card sizes in binary units labelled GB/MB.
- **The boot position is written after 5 s idle,** an SD write in the menu.
- **Dropped v0.3 features:** the NOR menu's load and back-up of a save; the help QR code.

### Open questions and cart checks

- **Untried on any cart since v0.2.2:** the in-game menu, save states, DEBUG, sleep, cheats, GB/GBC, NES, a NOR write and delete, rumble with a game, the EEPROM probe's verdict, the clock against real time (all in the release checklist).
- **Unmeasured on a cart:** the ROM copy rate (`PROGRESS_COPY_RATE`); the first scan and the walk of a full card; SD folder open time (0.2 s per 300 files emulated) and the 251k-cycle frame on a folder of 1,024+ subfolders; covers from an art folder of thousands of files; Identify (about 1.6 s per MB, estimated) and crc.txt's write steps; About's first free-space read on exFAT; how long Settings' "Saving..." takes.
- **How it looks and feels on the cart's screen:** the carousel's scaled pixel art at 0.6x and 1.9x; the 14 px rows; the scroll's brake (`SPRING_BRAKE`); tearing at 30 rows/s (an IPS kit re-scans by itself); the wipe's edge (a late VCOUNT interrupt could make it ragged; mGBA can't show it); `CHIP_ERASE_FRAMES` from a timed Format all.
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
