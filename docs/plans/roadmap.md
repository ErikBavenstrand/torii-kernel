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
  backups/<rom path>/<slot>/<n>-<YYYYMMDD-HHMMSS>.sav
  games/<code>-<crc>.ini  per-game overrides (save type, RTC, cheats)
  art/<system>/{title,box}/<code or name>.lz
  lang/<code>.lng
  cheats/<code>.cht
  trash/
  recent.txt
  favorites.txt
```

Saves live only there (decided 2026-10-23: no setting for next-to-the-ROM saves). Old `SAVER/` saves are not imported: v0.5 keeps no backwards compatibility (decided 2026-10-22).

What v0.5 built of this (`source/gamesave.c`, `gamerec.c`): a game's folder `/torii/saves/<rom path>/` holds its slot files and `game.ini`, its record (`save_type = 0-5`, `boot_mode = 0-2`, `slot = <current slot>`; plain text, missing keys are defaults). Folders are made when the first file is written (a game of unknown save type gets none until its chip differs from blank). A NOR game's saves are kept as for a ROM at `/torii/nor/<name>`. A save path over 251 bytes (the pending-save record's room) is refused at launch with a message. The per-game overrides listed above as `games/<code>-<crc>.ini` (RTC, cheats) stay for v0.7; the save type and boot mode are by path, in `game.ini`.

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
   existing save is kept untouched in the slot's history and the new type starts blank, with
   a notice. It is never converted or silently truncated.

### Slots and history

Decided 2026-10-22: a version is kept only when the chip differs from the current file, at the copy-to-SD after a play session; N defaults to 5 (Settings). Backups are named `<n>-<YYYYMMDD-HHMMSS>.sav`, where the rising counter `n` fixes the order, so a wrong clock can't reorder or overwrite history. Saves live only in `/torii/saves/`. A save-type change is never converted: the old file stays untouched in the slot's history, the new type starts from the chip's blank state, and a notice is shown.

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
  total space used. Built (`source/screens/saves*`, `source/saveorphan.c`; tests `tests/saves_manager.sh`): Settings, Library card, "Saves".
- **Deleting:** a deleted slot goes to `trash/` and is removed after 30 days.

What v0.5 built of slots and history (`source/slotstore.c`, API in its header; tests `tests/kernel/test_slotstore.c`, `tests/slotstore.sh`): the Settings row **History** (System section: Games' card is full at 6 rows; Off/1/3/5/10/20, default 5) sets the versions kept. The save copy pushes the version it replaces into the history before the swap (the record holds the history folder from halfword `0x82`); a repeat after a cut adds none (the newest version is compared first), and a history that can't take the version fails the copy, the file staying as it is. Restore, import and the type change always push, whatever the setting (pruning to at least 1). Restore moves the version out of the history (it is the file now). Renames, deletes and installs (restore, import) are journaled in `/torii/slotop.txt` (CRC) and `slotstore_recover()` finishes them at boot; a history folder is never moved to another parent (a cut leaves its `..` wrong): the trash takes its files one by one. A launch whose slot file is bigger than the save type needs (EEPROM 8K to 512) pushes it untouched into the history, starts the new type blank, and shows a one-time notice; a smaller file is still grown with FF, which keeps its bytes.

### Review of the first build (2026-10-23)

The cart test and one review round (a code reviewer and an adversarial tester) after the first build. Fixed in place unless marked open.

- **START did nothing on the GBA list with art** (the cart's log: `popup: BG VRAM: can't allocate 224 units`): Saves made the GBA popup 8 items, whose titled card no longer fit block 0's free tiles beside the art list. The popup now opens without its title when the titled card doesn't fit (`tests/shots/games-art-popup.opts`).
- **The save copy took 7.4 s for 128 KB on the cart**, 3.7 s of it copying the old save into the history. The old save now becomes the `.bak` and is renamed into the history after the swap (`Save_bak_retire`, `slotstore_move_file`); restore and the type change rename too, and import copies only the file it brings in. A cut leaves the old save as the file, the `.bak` or a version at every step; recovery renames a `.bak` into the history (keeping at least one version) instead of removing it, and an install's journal goes forward. A history that can't take the `.bak` no longer fails the copy.
- **Opening a GBA game's popup read the card** (the cheat lookup, 64 KB of `GameID2cht.bin`, the ROM header and the record): Saves now shows for every GBA game, and the facts are read once it is picked.
- **Deleting an orphan whose "ROM" is a folder took every save beneath it** (a stray `.sav` in `/torii/saves/GBA/`): a save folder that holds folders is never an orphan, and relink and delete refuse one.
- **Slot work under a save still on the chip** (a failed boot copy): rename, delete, restore or import could leave the chip's data in the wrong slot or over a restored file. Every save screen copies a pending save first (`pending_save_flush`).
- **A delete switched the current slot before its journal**: the record now changes once the file has moved, inside the journaled steps.
- **A rename or delete cut short by TAKEN or PATH cleared its journal after moving the save**: the journal stays unless nothing moved.
- **A rename to a name the card knows as another slot's 8.3 alias** trashed that slot's history: the new save path must be free, and an "orphan" history folder must hold the name itself.
- **A launch could read the record while a slot rename was half done** (a card error, the journal waiting for the next slot operation) and make a blank slot under the old name: a launch now finishes the slot and orphan journals before it reads the record.
- **Trash expiry overflowed a 32-bit `long`** (seconds since year 0): days are compared first.
- **The library counted every slot file as a save twice**, filling `LIB_SAVES_MAX` early.
- **Stack on the launch path**: the record's `FIL` and path moved to EWRAM, `Retire_oversized_save` is `noinline`; the record's folders are made only when its open says there are none, and the save copy's likewise.
- **EWRAM was down to 5.8 KB free** (18 KB at v0.4.2): the save screens' state (a game's saves, Play with, the slot flows) now lives in `pReadCache` (`READCACHE_GSAVES`, `READCACHE_FLOW`) while they run; 12 KB free.
- **The save screens' UX** was rebuilt as direction B of `docs/design/v0.5/saves-variants.md` (the user's choice, 2026-10-23): a list and detail page (`screens/save_page.c`) for a game's slots, a slot's backups (RIGHT) and the launch's Play with; a slot's actions in a popup (START); New slot (Empty, a copy, or a .sav file, which makes a new slot and overwrites nothing); Settings > Saves as the NOR page (a storage bar, then the saves without a game and Empty trash). The START popups merge the boot items into Play « default / clean / addons » with Saves second, and popups scroll past 7 rows (`UI_POPUP_ROWS`), so their tiles stay the same.
- **A save retired by a type change was pruned like any backup**: it is a kept backup now (`-kept` in its name), which no pruning counts or removes.
- **The trash purge trusted the clock**: a clock outside 2026-2060 touches nothing, and a folder dated by an unset clock or one ahead takes today's date (it waits its 30 days from now, never less).
- **Settings > Saves walked the card again after every action**: its figures change in place now (a walk only when saves past the listed ones wait), and the orphans' history bytes come from the backups' walk itself.

### v0.5.1 (2026-10-24)

From the cart's logs after the v0.5 save screens and a review round (a bug and performance reviewer; an independent UX agent works on navigation in its own branch).

- **The first file or folder made after power-on scanned the FAT for a free cluster** (with FSINFO's next-free hint unknown or stale: on the cart's 32 GB card, ~600 FAT sectors, a second or more; the 1.3 s before a save copy's first write, and a background index write that froze the menu for seconds inside one job step). A job (`source/fathint.c`) reads the FAT from the hint (else its start), 8 sectors a step, from the mount on, and sets the hint at the first free cluster.
- **Clearing the pending-save record erased a NOR sector** (0.7 s each save copy): `Save_info` programs in place when the new data only clears bits of what is there (NOR turns 1s to 0s without an erase), so the clear is a few halfword programs. The erase path is unchanged for everything else.
- **The move into the history compared the whole file with the newest version** (a 128 KB save read twice): a move compares nothing; only a second name a cut left (same size, same chain) is dropped. A folder with versions isn't made again.
- **Slot copies verified by reading both files again**: the copy's CRC is taken as it writes, and the new file is read once to check it.
- **Slot copies, a slot from a file and an export showed nothing for seconds**: a "Copying the save" notice shows meanwhile.
- **The slots page counted every slot's backups again after Make active**: only after changes that touch them.
- **Play's « » boot was lost when playing from Saves**, and the two START popups offered Clean on different rules: Saves plays as Play said, and both offer default and clean for a GBA game (addons while one is on), nothing for GB, GBC and NES (they start clean).
- **The saves page failed silently once on the cart** (BG VRAM, from the Recent page with art; not reproduced in the emulator): it says so now, its seldom-changing labels take one tile set (about 110 tiles less on plane 3), and a failed BG allocation logs the pool's blocks, so the next one can be read.
- **Any new .sav made the next boot rebuild the whole library index** (the walk's hash covered every slot file): a game's save folder counts once, so only a game's first save (which marks it) rebuilds.
- **UX review** (an independent agent, `docs/design/v0.5/ux-review.md`, merged in eac14cd): game lists open on the game played last that they hold, then on the row they were left on; Recent's popup has Remove (off Recent only, no question); one word per thing (Play on the NOR page, Backups kept, START menu).
- Open items moved to v0.5.2.

### v0.5.2 (2026-10-24)

Everything open after v0.5.1, a survey of the build's memory and speed, and a settings and save pages UX review (an independent agent, `docs/design/v0.5/settings-ux-review.md`). Fixed in place unless marked open; the timings are the emulator's on a card of 4 KB clusters (`sd:` log lines), where a sector read costs almost nothing, so the read savings show on the cart only.

**The owner's decisions (2026-10-24):** Export .sav removed (with the slot store's import and export, which nothing called); SELECT stays free (no quick favourite key); Play « default » stays as it is.

**Speed**

- **Every SD write call ends with the driver's settle delay** (`delay(3000)` in `Write_SD_sectors`, about 8.6 ms, two thirds of a save copy's time): the save copy writes 8 KB a call, slot copies 4 KB, a new save 8 KB, and a new folder's cluster is cleared in one call (`Write_SD_fill`; FatFs's `dir_clear` made 64 calls for a 32 KB cluster, about 0.55 s a folder, three for a game's first backup). The save copy: 1.45 s to 0.96 s; a game's first launch: 1.08 s to 0.69 s.
- **A 12-sector write-through cache** (`source/sdcache.c`, 6 KB of IWRAM) of the folder and FAT sectors read into FatFs's window: a slot rename read 56 sectors (9 different), now none; a save copy 94 to 35 reads. File data read into a caller's buffer isn't kept. Random replacement: a loop over a folder bigger than the cache keeps part of it.
- **Each operation's card work is logged** (`source/diskstat.c`): `sd: save copy: 35 reads (259 sectors, 59 more from the cache), 55 writes (300 sectors) in 961 ms`, for the save copy, the launch's save and each slot operation; the library walk logs its own time and sectors, not names.db's load or the pauses between its steps.
- **The pending-save record's erase at each launch** (about 0.5 s): 128 slots in its sector, a launch's record programmed into the next erased one in place (flag last), the sector erased once every 128 launches (`tests/saves.sh record-slots`); in-place writes are bounded by `kernel_nor_program_ok`.
- **The save copy ran `Save_recover` three times**: once. **The trash purge read a folder again per file**: once. **Pruning saw 64 backups at most**: it keeps the newest 64 listed and removes the older ones beyond.
- **Done in v0.5.3: keys tapped during a blocking write were lost** (the settings write, a save copy step): input is polled once a frame. A keypad interrupt could latch presses.

**Memory**

- **newlib's printf** (stdio, a heap, float code: 39 KB of ROM, 5.8 KB of IWRAM data) replaced by the kernel's own (`source/fmt.c`, host-tested against the host's printf): a log line formats in 21,000 cycles, newlib's took 54,000. The language tables and the NES patch are `const`. IWRAM free: 4,268 to 5,032 bytes with the sector cache's 6 KB in it (the deepest stack measured 1.9 KB).
- **Done in v0.5.3: `impure_data` (1.1 KB of IWRAM)** was still linked (newlib's `errno`, from `strtol` and others).

**UX**

- **Settings has a Saves card** (Save without asking, Backups kept, Clean up saves ›); the save pages' words are plainer; Empty trash with nothing in it is inert; the backups page says Made and Backup N (the review, bb89ccd).
- **Game info's Played** says Not lately (off Recent) or Recently (no time), never blank. **B held on the SD page** climbs a folder every 20 frames up to the card's root.
- **Kept:** Settings' hint bar shows the row's explanation (the keys show once editing starts).
- **Settled in v0.5.3 (owner):** Clean up listing every game with saves, each opening its Saves page; A on a slot plays at once (direction B's choice) or opens its popup; Play with's title is the file name, not the display name; a legend for the storage bar's three shades; New slot's pane heading peeks above its popup; the kept backup's note assumes save types are known; the Saves card after Games or after Library.

**Tooling**

- **Done in v0.5.3: a deleted source file's object stayed in `build/` and was linked** (`tools/fresh.py` rebuilds changed files only): moving `libc_printf.c` aside still linked it. Clean objects with no source.
- **`tests/logging.sh open` held B for 4 frames**, inside the settings write's blocked frames once the code got faster: it holds B for 30.

### v0.5.3 (2026-10-24)

v0.5.2's open items, the owner's answers to the UX review's questions (2026-10-24), and one code review round.

- **Game saves** (Settings > Saves, above Clean up saves): the library's games with saves, in its order, by display name; A opens a game's Saves page, and a slot played there starts the game. The card walk (`saveorphan_scan`) keeps a key per game folder whose ROM is there (256 at most, `ORPHAN_HAVE`); the list opens the game's page in its own memory's place, so it walks the card again on the way back. Games on the game NOR aren't listed (not in the library; their page has Saves in START's menu). `tests/shots/saves-games.opts`, `tests/saves_manager.sh` case 6.
- **Key taps during a blocking write**: the VBlank interrupt (`crash.s`) ORs the held keys into `input_keys_seen`, which `input_poll` takes: a tap inside Settings' NOR write is seen (`tests/logging.sh` settings and open tap B for 4 frames).
- **Play with's title** is the list's display name (`action_launch_title`; `tests/shots/slot-picker-list.opts`); from the SD card and NOR pages, the file name they list.
- **Clean up's bar**: two shades, saves and backups, as its line; the trash's size on Empty trash's row.
- **New slot's pane heading** is blank while its popup is up.
- **`impure_data`**: `fmt_strtoul` replaces newlib's `strtoul` and `atoi`; IWRAM free 5,032 to 5,764 bytes.
- **Stale objects**: `fresh.py track` counts a deleted source as changed, so its object and the linked kernel go.
- **Kept as they are (owner):** A on a slot plays it; the Saves card after Games; the kept backup's note.
- **Kept (owner, 2026-10-24): the kept backup.** A save type that shrinks moves the slot's save into its history as a backup no pruning removes (the slot starts blank, so it is the only copy); making it a slot of its own was the alternative.
- **Open: Game saves past 256 games** with saves aren't listed (logged).

## v0.6 Emulators

Torii plays GBA, GB, GBC and NES, and nothing else (owner, 2026-10-25): the research
(`docs/research/gba-emulators.md`) found no other system with a fast, mature emulator and a
library worth it, and no emulator with a licence to bundle. So there is no plugin system, no
PogoShell handoff, no `/SYSTEM/PLUG` and no viewers: v0.6 makes the two built-in emulators
as good as they get on the Omega DE.

**Released as v0.6.0 (2026-10-25).** Custom GB borders were looked at and dropped (owner,
2026-10-25): none exist per game for the games without an SGB border, which Jagoomba
already draws for SGB games. The fixes carried below wait for a cart run.

**Released as v0.6.1 (2026-10-25):** 32 KB GB saves written through to the cart, as a real cart keeps them (88eebc2): v0.6.0's autosave paused games 0.12-0.15 s, often in Pokémon Picross and Puzzle Challenge (the owner: "games will stutter a lot"). Save states get 32 KB instead of 56. Checked in the emulated cart: every 32 KB game in `roms/` (24) uses the copy, a v0.6.0 save (Pokémon Yellow, record only) moves into it exactly, `tests/gb_save.sh` cuts the power 30 frames after the writes.

**Released as v0.6.2 (2026-10-25):** save slots for GB, GBC and NES games (no .sav import: a slot holds the emulator's 64 KB), NES CRCs without the iNES header so NES games get names and covers, NES saves shown to write through (`tests/nes_save.sh`), and the emulators' builds reproducible across machines (sorted file lists, `just repro` rebuilds them shuffled).

**Fixes carried from v0.5.2 (moved 2026-10-24, open after v0.6.2):**

- **Cart check: path walks from the root** (every open under `/torii/saves/<rom path>/` scans `GBA/` again): the cache now answers most of these; relative paths (`f_chdir`) would remove the rest, but the SD page keeps its own current folder. Measure on the cart with the `sd:` lines first.
- **Cart check: whether the 8.6 ms settle delay is needed** after every SD write (EZ-Flash's code; the commands inside one call run back to back without it). Measure with the `sd:` lines; drop the delay, or shorten it, only if the card keeps up.
- **Screens' EWRAM states**: each screen's state is its own EWRAM static (`games` 3.7 KB, `sd` 3.6 KB, `info` 2.7 KB, `nor` 1.4 KB, Diagnostics' `scr` 1.6 KB): a union of page states or a shared path scratch would free 5 to 8 KB. Do it if the emulator builds or the launch path need the room.

**Emulators**

- **Done (998abab, 2026-10-25):** the items below, except the per-mapper compat entries. Every GB/GBC game in `roms/` (77) passes `just compat`, the NES homebrew test runs, and the emulated cart showed a picked palette kept over a power cycle. The emulators build reproducibly, Jagoomba with devkitARM r58 and PocketNES with r66; PocketNES leaves out Go Multiboot (193df78), and the kernel's ROM is 70% full (304,244 bytes free). Cart checks are in `docs/release-checklist.md` (palette, speed at 3,1 waits, GB rumble).
- **Saves (cart run, 2026-10-25):** a 32 KB GB game's in-game save was lost at power-off unless Jagoomba's L+R menu ran first; Jagoomba now saves a second after the game's last write (b661254, `tests/gb_save.sh`). Diagnostics' `asset_sd_read` failed on the cart's bigger clusters (test fixed, b661254). "Save without asking" stays off by default: the prompt stays (owner, 2026-10-25).
- **From the emulators' issue trackers (2026-10-25, d333227):** jagoombacolor#29, #51 and #30 are the 32 KB save loss (fixed in b661254); #32 and ez-flash#12 the palette (fixed); #46 and #35 Crystal's stutter (fixed, not reproducible in mGBA: check on the cart); PocketNES#5 the ROM alignment (fixed). Left: jagoombacolor#53 and #20 hi-color GBC games, #42 accuracy tests, #43 a GBC-compatible ROM run as DMG, #36 the frame-wait setting not saved, #34 the default palette choice; PocketNES#3 region not saved (taken from the NES 2.0 header in 2025 builds), #14 multiboot.
- **Built from source:** Jagoomba Color (GB/GBC) and PocketNES (NES) build from their
  upstream source at pinned commits, fetched like mGBA and patched from files in the repo,
  with the kernel's devkitARM, reproducibly. They replace the prebuilt `jagoombacolor.h`
  and `pocketnes.h`. Each is the newest working version: Jagoomba Color's master, and
  Dwedit's PocketNES master (2025: MMC5, VRC3/4/6 IRQs, mapper 118, NES 2.0 headers), not
  the 2013 build.
- **Omega DE options:** the GB emulator gets the DE's rumble and clock, as Jagoomba's own
  `_ezode` build does. PocketNES's clock is a build option, not a word the kernel writes
  into the binary at a fixed offset.
- **GB palette (ez-flash/omega-de-kernel#12):** a palette picked in Jagoomba's menu is kept
  per game.
- **Removed:** `source/goomba.h` (unused) and `emusrc/`, EZ-Flash's older Goomba Color, which
  Jagoomba replaces.
- **Tests:** `just compat` launches one GB/GBC game per mapper (MBC1, MBC2, MBC3+clock, MBC5,
  MBC5+rumble), and every GB/GBC game in `roms/` boots once into a compatibility list. NES
  is covered by homebrew test ROMs until NES games are in `roms/`.
- **Licences:** neither emulator states one; they ship inside `ezkernelnew.bin` as they do
  in EZ-Flash's kernel (owner, 2026-10-25).

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
- boot modes, the quick-start hotkey, the hard-reset toggle and the boot
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
