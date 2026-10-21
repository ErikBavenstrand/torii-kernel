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

## v0.2 Foundations

### Bugs (done)

Fixed with tests on main, from `b9c9245` to `b6687ea`. `CHANGELOG.md` has the details.

- ~~**Save types:** Doom, Doom II and Gauntlet are now EEPROM 512, and the Classic NES Series are EEPROM (ez-flash/omega-de-kernel#3).~~
- ~~**Cheat table:** capped at 600 writes, which is what the 0x2000-byte patch area holds.~~
- ~~**Clock and rumble:** they are now separate settings, shown as one row, `CART: [x] CLOCK [x] RUMBLE` (ez-flash/omega-de-kernel#5).~~
- ~~**GB, GBC and NES games:** they start without the Clean Boot menu. The Goomba palette waits for v0.6 (ez-flash/omega-de-kernel#12).~~
- ~~**Sorting:** the SD listing uses a case-insensitive merge sort.~~
- ~~**Recently played:** it works at its 10-entry limit.~~
- ~~**Clean boot:** EWRAM is fully cleared when a game starts.~~

Still open from the bug work:

- ~~**EEPROM sizes:** settled, no change needed. FlashGBX's cart-dump database (`db_AGB.json` in `res/config.zip`) lists 941 table entries marked 0x22 (EEPROM 8K) as 512-byte EEPROM, plus 9 marked 0x23 and 6 marked 0x32 (Flash 64K). The real cart decides the address width per request, so 8K mode takes these games' 6-bit requests and their saves stay in the first 512 bytes (`docs/research/cart-test-2026-10-18b.md`). The 0x23 and 0x32 entries are unchecked, and Doom and Gauntlet stay EEPROM 512.~~
- ~~**Hook coverage:** done. `just compat` has an in-game menu column; all 118 commercial GBA games in the library pass (`docs/research/hook-coverage.md`). Four games get exact-ROM patches for the EU dumps tested; other regions may need their own. The menu keeps the game's brightness, but still resets the BG2 affine registers, which are write-only.~~
- ~~**Game NOR:** WRITE TO NOR now runs in the emulator (`tests/nor.sh`), which showed these kernel bugs. Each one can lose or overwrite a NOR game:~~
  - ~~Fixed: `Read_S98NOR_ID` (`Ezcard_OP.c`) left the chip in ID mode, so the NOR list could read empty and the next write landed over the first game. It now writes `0xF0`.~~
  - ~~Fixed in v0.2.1 (`tests/nor_safety.sh`, the list format in `omega-de-hardware.md`): `GetFileListFromNor` returned without `REG_IME = 1` when its scan reached the end of the 64 MB. "Delete latest" erased only the game's first 128 KB and kept its list entry, so a write with the same header cut off after one block was listed. Losing power inside `Save_NOR_info` emptied the list, and the next write went over the first game. A ROM without 0x96 at 0xB2 was written but never listed. The list now has two copies, games a completion mark, and the boot rebuilds a lost list from the marks.~~
- ~~**Non-ASCII names:** the list draws names by byte count, so CJK names start scrolling too early, and a character split at the edge of the list shows as `?`. Names over 99 bytes (about 34 CJK characters) show cut with `*` and "Name too long", and can't be opened. The v0.3 file list fixes both.~~ Fixed by v0.3 (`5e1981c`).
- ~~**Clean boot:** IWRAM is still not fully cleared at launch, because the `SoftReset_now` code stays there. No game is known to need it. Clean-boot tests use `tests/roms/idle` (no crt0), since devkitARM's crt0 clears EWRAM itself.~~ Fixed by v0.2.1.
- ~~**From the logging work:**~~
  - `Save_savefile` ignores short writes, and `Check_save_flag` ignores its result, so a
    nearly full card silently truncates a new save, e.g. 64 KB written as 32 KB (v0.5's
    crash-safe saving replaces it).
  - The in-game menu's LOAD with no saved state jumps to 0xFFFFFFFE.
  - ~~Fixed in v0.2.1: the kernel erased the NOR game table whenever it read the list as empty (a chip left in a command mode or a read glitch), and the next WRITE TO NOR overwrote the games. Reading the list now writes nothing, and a write reads it again first.~~
  - The `/torii` folder shows in the SD root list and moves games down a row (key scripts
    in compat may need an extra DOWN). v0.4 hides system folders.
  - ~~The log file sequence number doesn't handle overflow past 99999999 (only reachable with crafted files).~~ Fixed by v0.2.1 (`7887069`).
  - Needs the real cart: whether cart RAM page 0xF0 survives a power cycle, which WAITCNT
    settings read correctly, how the FPGA answers EEPROM requests of each width, rumble at
    GPIO values 2 and 3 without the clock, whether an SD write right after the FPGA copies
    a game into PSRAM is safe, and what the BIOS does on an undefined instruction.
- ~~**Settings default:** done. The emulated kernel NOR already read 0xFFFF for settings that were never written. The 0x0000 seen came from saves, which zero slots 65–255 (`tests/settings.sh`), and erased hotkey slots are now range-checked. Known slots: 13 is CLOCK and 25 is RUMBLE; 23–24 were DS Style's reset and backup toggles, so leave them unused.~~

### Tooling (done)

- ~~**Warnings:** add `-Wextra -Wshadow -Wundef -Wformat=2 -Wstrict-prototypes -Wmissing-prototypes`, exempting the vendored FatFs. Fix everything, then turn on `-Werror`.~~
- ~~**Stack and size:** add `-fstack-usage`, and try `-ffunction-sections -fdata-sections -Wl,--gc-sections`, comparing `just size` before and after. Tried and rejected: gc-sections cost 568 bytes of ROM and 24 of IWRAM.~~
- ~~**Editor config:** add `.editorconfig` and `.gitattributes`.~~
- ~~**Formatting:** run a pinned clang-format through `uvx` in `just lint` and CI, and add a short formatting note to CLAUDE.md. There is no Claude Code hook.~~
- ~~**clangd:** add `.clangd` with `--target=arm-none-eabi`, built on `just compile-commands`.~~
- ~~**Host tests:** build pure code (parsers, sorting, cheat decoders) for the host with ASan/UBSan, reusing `torii_test.h`.~~
- ~~**Advisory analysis:** run cppcheck, clang-tidy (`bugprone-*`, `clang-analyzer-*`) and GCC 10's `-fanalyzer` in report-only mode, with a baseline. GCC 10's analyzer gives up on the kernel's big functions (`-Wanalyzer-too-complex`) and finds nothing so far.~~
- ~~**Pinning:** Docker images by digest plus a Debian snapshot, Actions by SHA, just and uv by version. Bump the snapshot and the image digest together.~~

Notes from the tooling work:

- Making ROM functions `static` grew the static deepest-stack estimate from about 1.8 KB to
  2.4 KB (`main`, `Process_savefile`, `Open_cht_file`). It fits in IWRAM's free space, and
  `noinline` on the save writer and cheat readers would win it back if needed.

### Source cleanup (done)

1. ~~Port the Simple DE and DS Style fixes we want while their code still lines up with ours.~~
2. ~~Remove the Chinese strings, the HZK12 font and FatFs's CP936 tables (about 87 KB each).~~
3. ~~Convert to UTF-8 with LF line endings, and run clang-format once in its own commit, listed in `.git-blame-ignore-revs`.~~

Then update CLAUDE.md: the GBK, CRLF and `zh_*` rules go.

### Logging and on-cart debug mode (done)

#### SD logging

Logging to SD is built first, because the debug mode writes its report through it. It is
a persistent setting, `Logging: Off / Errors / Info / Debug`, off by default and free when
off. Until `torii.ini` arrives in v0.5, it lives in a free kernel settings slot, like CLOCK
and RUMBLE. Kernel crash dumps are captured even when logging is off.

- ~~**Kernel log:** `tlog()` writes into a small ring buffer in RAM (2–4 KB of the free EWRAM). The buffer is flushed to `/torii/logs/` at safe points: before a launch, on errors, and when settings close. Files are append-only and rotate, keeping the last N, so the card never fills. Every boot starts with a session marker. Under the emulator, `tlog()` still goes to mGBA's console.~~
- ~~**Launch record:** before each launch, the log records exactly what the kernel did: game code and revision, save type, the patches and hooks applied (including `rom_patches` and `reset_table.h` matches), cheats, CART settings and the plugin.~~
- ~~**Crash detection:** if a game ran and didn't come back cleanly, the next boot logs that the previous session ended abnormally, next to its launch record.~~
- ~~**In-game snapshot:** a "Save debug info" entry in the in-game menu writes registers, the hook state and a small memory sample to SD.~~
- ~~**Kernel crash dump:** an exception handler stores the registers and call stack in cart RAM that survives a soft reset, and the next boot writes them to the log. Whether the dump also survives a power cycle is checked on the real cart.~~
- ~~**Tools:** `just logs PATH` pulls the logs from an SD card and turns code addresses into function names from the matching `ezkernelnew.sym`, in the same format as the emulator's crash reports.~~

#### On-cart debug mode

Not a setting: you run it when you want it, from a Diagnostics entry in settings or by
holding a key combination at power-on (in case the menu itself is broken). It logs at
Debug level for its own run only. It:

- ~~runs the unit tests;~~
- ~~probes the hardware: RTC, PSRAM, NOR reads, and a save-chip test that doesn't touch the user's saves;~~
- ~~measures real timings: NOR wait states (the kernel never sets `WAITCNT`), glyph rendering, LZ77 decompression and SD throughput;~~
- ~~settles the EEPROM 512 versus 8K question for games in the save table;~~
- ~~looks for a separate rumble control. The Omega DE has its own rumble motor on the cart GPIO port, and `0x096A0000` gates the whole port. The kernel now enables it per game, from the GBA game code or the GB cart type at 0x147 (`a854cdc`). GBA romhacks that add rumble only get the port while CLOCK is on;~~
- ~~walks you through the manual checklist with pass/fail buttons.~~

It writes its report to SD as a log stream that we bring back to the repo. When the real
cart disagrees with the emulator, the fix goes into the cart model.

## v0.2.1 Real-cart fixes (done)

Every known bug, found in our work or inherited from the official kernel, is fixed here.
From the first real-cart test of v0.2.0 (`docs/research/cart-test-2026-10-18.md`):

- ~~**Log files:** fill new log files with zeros from RAM, not a ROM array. On the real cart, ROM reads while SD mode is on return the SD buffer's leftover data. Make `disk_write` refuse or copy any buffer in ROM. Recover log files already full of junk (start a new file when the space after the text isn't zero), and make `log_find_end` robust.~~
- ~~**Cart model:** while SD mode is on, ROM reads don't return NOR contents. Cart RAM beyond the save clears on a power cycle. The clock gate is `0x096A0000` bit 0. The EEPROM doesn't answer without a game loaded. Add a test that log files are zero after their text.~~
- ~~**Reproducible builds:** sort the Makefile's `$(wildcard)` lists, so CI and macOS link the same binary. Attach `ezkernelnew.sym` to each release, and put a real build ID (a CRC or the git hash) in the boot line. `just logs` warns when the symbols don't match.~~
- ~~**Save logging:** log every "Save sav?" decision, save copy (with size and result), NOR write and settings save, including `f_write` and `f_close` results. Done: a short copy is logged as an error with the bytes written (`tests/saves.sh`), but still cuts the save short; handling the error and keeping the old save is v0.5's crash-safe saving. On the real cart, check a power-on copy's log lines.~~
- ~~**Diagnostics:**~~
  - ~~name the failed checklist item in the summary;~~
  - ~~show the clock live on the clock item;~~
  - ~~log probe answers as info, not errors;~~
  - ~~after a crash, say the session crashed;~~
  - ~~show a GB game's title instead of `(????)`;~~
  - ~~in the checklist, say to use RESET, not power-off, before checking the DEBUG snapshot.~~
- ~~**Clock display:** the checklist clock looked stuck about 10 minutes behind. Make it live, and check the RTC against real time (Diagnostics could log both).~~
- ~~**In-game DEBUG:** give feedback when the snapshot is taken, and survive power-off. Either write it to SD from the menu, or tell the user to use RESET. On the real cart, choosing it seemed to do nothing.~~
- ~~**Checklist answers:** a yes needs proof where the log can give it. For example, flag a "sleep works" yes when no launch had sleep on, and an NES yes with no NES launch.~~
- ~~**EEPROM probe:** run it in game mode, from a small test ROM, so it can settle 512 vs 8K.~~
- ~~**WAITCNT:** the cart reads correctly at 2/1 with prefetch, 20% faster than the default. The menu now runs at `0x4018`, and every launch restores `0x0000` (`source/waitcnt.h`); launching (PSRAM writes), the cart's control registers, SD transfers, NOR writes and the clock stay at `0x0000`. In mGBA a 300-file folder opens in 23 frames instead of 40. **The real cart must confirm it:** browse, open a large folder, launch from SD and NOR, write a game to NOR, set the clock and run Diagnostics, then check the log for errors. Games that don't set WAITCNT still start at `0x0000`.~~
- ~~**Rumble:** no GPIO register value drove the motor. Test with Drill Dozer: WarioWare: Twisted! needs a motion sensor the Omega DE lacks. Then find out how the cart's rumble is driven, or whether this cart has a motor at all.~~

Also every bug still open from v0.2 that belongs here:

- ~~**Game NOR:** interrupts left off after a full scan, half-written games listed after "delete latest", the list lost on a power cut in `Save_NOR_info`, homebrew without 0x96 at 0xB2 never listed, the table erased whenever the list reads empty, and NOR launches not updating Recently Played.~~
- ~~**Launch:** IWRAM isn't fully cleared (the `SoftReset_now` code stays).~~
- ~~**Logging:** the log sequence number overflow.~~

Kept in their own milestones:
- hiding `/torii`, in v0.4;
- ~~UTF-8 name drawing and names over 99 bytes, in v0.3;~~
- save write errors, in v0.5;
- Jagoomba from source and the palette fix, in v0.6;
- the in-game LOAD crash and the BG2 affine registers, in v0.7.

EEPROM sizes and rumble wait for the next real-cart test.

## v0.2.2 Second real-cart fixes (done)

From the real-cart test of v0.2.1 (`docs/research/cart-test-2026-10-18b.md`):

- ~~**EEPROM probe:** every write lost its last byte, polled right after the stop bit. The probe now waits before polling, writes four blocks four different ways and shows which bytes differ; the kernel accepts a match on bytes 0–6 and logs the lost byte.~~
- ~~**Diagnostics:** the EEPROM item checks the probe's verdict; a button step missing L or R says the hotkeys need them; the boot after the crash test names it.~~
- ~~**Save size:** a `.sav` smaller than its save type (512 bytes under EEPROM 8K) is grown with `FF`, so blocks past its end survive power-off.~~
- ~~**Hotkeys:** match the combo's keys, so an extra or stuck key doesn't block the in-game menu or sleep.~~
- ~~**NOR list:** an older kernel's list is saved in the new format once, and list reads are logged only when they change.~~
- ~~**`just logs`:** junk bytes show as a count, not verbatim.~~
- ~~**Hello** shows a green square while its test cheat runs, for the checklist.~~

Still to confirm on the real cart (`docs/release-checklist.md`): the in-game menu, save
states, DEBUG, sleep and cheats (the console's L button was laggy), GB/GBC, NES, a NOR write,
rumble, the probe's verdict, and the clock against real time.

## v0.2.3 Boot loader safety (done)

v0.2.2 bricked the real cart at its first boot: the NOR list's copy B went to kernel NOR
`0x7C0000`, which wraps onto EZ-Flash's boot loader, and the cart started in safe mode
(`No_Loader_found`). v0.2.1 wrote the same place on WRITE TO NOR and delete. **v0.2.1 and
v0.2.2 must not be used**; both are marked unsafe on GitHub, and `docs/recovery.md` has the
recovery used on 2026-10-18.

- ~~Copy B moves to `0x780000`; `Save_info`, the only kernel-NOR writer, refuses anything outside `0x780000–0x7BFFFF` (`source/bootloader.h`).~~
- ~~The emulated cart models the real chip: the loader at chip 0, the kernel window at `0x40000` wrapping at 8 MB, 8 KB boot sectors, programming failures. Any write to the loader stops the run, and `tests/loader_safety.sh` runs every kernel-NOR writer, power cuts in them and the other chip; it fails on v0.2.1 and v0.2.2.~~
- ~~Diagnostics reads the loader (read-only) and logs its CRC and state.~~
- ~~The release checklist keeps `DE.BIN` on the test card and tests NOR write and delete.~~

## v0.3 Renderer (done)

Released in v0.3.0, together with v0.3.1 and v0.3.2.

The official menu was mode 3 (`SetMode(MODE_3 | BG2_ENABLE)`), drawn pixel by pixel in C.
v0.3 replaces it with a mode 0 engine built to carry v0.4's UI; mode 3 and `draw.c` are gone. The design, the tickets,
and every decision made along the way are in [v0.3-decisions.md](v0.3-decisions.md).

- ~~**Engine:** a fixed 60 fps loop with time-sliced jobs, shadow state committed in VBlank, allocators for VRAM, palettes, sprites and affine matrices, and a layout budget checker.~~
- ~~**Text:** proportional glyphs drawn by an ARM blitter in IWRAM straight into VRAM tiles, in palette-index colours. Lists scroll through the wrapping map with 16 px rows.~~
- ~~**Views:** a widget tree with a view stack, a focus model, and lists fed by a data source. Layouts are data.~~
- ~~**Motion:** eased tweens for transitions, and springs for anything the user can interrupt.~~
- ~~**Input:** per-key repeat with a stepped speed-up.~~
- ~~**Covers:** decoded covers cached in PSRAM and streamed into sprites.~~
- ~~**Names:** cut and measured by UTF-8 character. Full names of any length are kept outside EWRAM (for example a PSRAM index), so names over 99 bytes list, open, launch and save.~~
- ~~**Look:** today's screens and keys, with two proportional fonts, light and dark themes, and accent colours.~~
- ~~**Testing:** golden screenshots, a cycle budget per frame, and a renderer step in Diagnostics that times the real cart.~~

The budget each layout is checked against (GBATEK, Tonc):

- **VRAM:** 64 KB for BG maps and tiles, 32 KB for sprite tiles. One layout fits exactly:
  4bpp full-screen text (19.2 KB), a 4bpp wallpaper (19.2 KB), 8bpp 120x80 art (9.6 KB),
  9.3 KB of panels and 4 maps (8 KB).
- **Colours:** 255 BG colours shared by every BG, and another 255 for sprites.
- **Covers in sprite VRAM:** about 5 at 80x80 in 8bpp, or 10 in 4bpp.
- **Effects:** one blend mode and one coefficient pair per frame, no sprite-to-sprite alpha,
  one mosaic size, and 2 rectangular windows plus the OBJ window.
- **Free memory (2026-10-18):** 660 KB of ROM, 4.0 KB of IWRAM and 7.0 KB of EWRAM.

## v0.3.1 Themes (done)

- ~~**Theme format:** `themes/<name>/theme.ini` plus LZ77 assets. Its keys override the compiled-in defaults one by one (palette, fonts, spacing, view layouts, icons) through a bounded patch table. A missing key or asset falls back to the default theme.~~
- ~~**Tools:** a PNG converter for assets, and `just theme-check`, which validates a theme against the budget and previews it in the emulator.~~
- ~~**Budget check:** the kernel rejects a theme over budget with a readable error.~~

## v0.3.2 Renderer fixes

Bugs found while building v0.3 and v0.3.1. Each is struck out here once a ticket fixes it.

- ~~A scrolling long file name runs into the size column in the SD list (T3; port T9).~~ Fixed by T9: the marquee is clipped to the name column.
- ~~The game menu shows "CHEAT(0)" before the cheat list has been opened (T3; port T11).~~ Fixed by T11 (`5e44a3b`).
- ~~The cheat view counts sections plus entries: "[002/004]" for 2 cheats (T3; port T11).~~ Fixed by T11 (`5e44a3b`).
- ~~Diagnostics' loader check can log a garbled loader title: `say()` clears the start of `pReadCache`, where the image is (T16 review; port T12).~~ Fixed by T12 (`6067e1b`).
- ~~After a failed launch from the NOR list's game menu, the menu stays open on a black screen until a key redraws its rows (pre-existing; T12 review; port T9).~~ Fixed by v0.3.2 (`14f3626`): it no longer happens since the menus moved to the engine; the golden nor-launch-fail keeps it so.
- ~~The NOR format dialog counts the erase in binary (`itoa(count, msg, 2)`), and the list's clock stops during the erase (T12 review).~~ Fixed by v0.3.2 (`a36ec16`): F4's progress dialog had already replaced the count; the clock now runs under dialogs.
- ~~With an SD theme's wallpaper, BACK UP SAVE's progress dialog fails to get VRAM and doesn't show (F4 review).~~ Fixed by v0.3.2 (`ed09af3`): it was pushed over the NOR menu, with or without a wallpaper; the menu now closes first.
- ~~Leaving the NOR list with L starts the SD list at its first row, not where the cursor was (`ACT_TO_SD` calls `show_sd(0, ...)` in `source/screens/lists.c`) (F1).~~ Fixed by v0.3.2 (`691fc00`).
- ~~SETTINGS page 2 writes kernel NOR on every change, not once when leaving: five settings writes in 25 s on the cart, which wears the NOR (`source/setwindow2.c`) (F3).~~ Fixed by v0.3.2 (`e5d3e8b`), page 1 too.
- ~~Two presses of one key in the 3 frames a screen goes out count as one, because the held keys are a bitmask (`ui_frame` in `source/ui/ui_run.c`). Queue up to two presses (F2).~~ Fixed by v0.3.2 (`bf7f65d`).
- ~~If `show()`'s `ui_push` fails, the menu stays in the transition gap with a black screen. Fall back to `ui_enter` and an error dialog (`source/screens/lists.c`) (F2).~~ Fixed by v0.3.2 (`046cfdf`).
- ~~While DIM is on, `ui_frame` drops keys queued for the next screen (F2).~~ Fixed by v0.3.2 (`bf7f65d`).
- ~~The empty-folder label doesn't fade with its list in the alpha fade, because pixels over another plane of the same screen don't blend (`regs_update`) (F2).~~ Not reproduced in v0.3.2 (`1bc8567`): the empty list leaves its plane clear under the label, so it blends with the backdrop; the golden `recent-empty` hashes the fade.
- ~~The Diagnostics page blanks for 2 frames when it comes back after the unit tests (`screen_open` in `source/diag.c`) (F3).~~ Fixed by v0.3.2 (`3e8243e`).
- ~~After a card failure, the lists' idle flush calls `tlog_flush` every idle frame. Each call returns at once, but it could stop trying (`idle_flush`) (F3).~~ Fixed by v0.3.2 (`40bf58f`).
- ~~BOOT WITH ADDON's first progress step says "Loading the game" while it still checks save states and patches (`source/ezkernelnew.c`), and `source/ui/dialog.h`'s usage example still names `gl_save_sav` (F4).~~ Fixed by v0.3.2 (`95b5f8d`).
- ~~Held-key wrapping differs between lists: the main lists stop at the end, while menus and the demo list wrap after a 15-frame rest (`input_move`, `wrap_pause`). Settle on one rule (F1).~~ Fixed by v0.3.2 (`6844884`).
- ~~`tests/themes.sh` still copies the example theme with `mcopy -s`, in the host's file order (F1).~~ Fixed by v0.3.2 (`0a1413d`).
- ~~No golden covers the two longest dialog texts (`en_file_overflow`, `en_error_7`). Untested paths: a NES launch, BOOT WITH ADDON with the scan engine forced, and a nearly full card during a save copy (F4).~~ Fixed by v0.3.2 (`e4eacc7`: goldens `dialog-overflow`, `dialog-fragmented`; `24ec875`: `tests/nes.sh`, `tests/scan_engine.sh`, `tests/saves.sh nearly-full`).
- ~~`docs/recovery.md` and the release notes should say that a cart whose kernel NOR is a uniform-sector S29GL064A (ID `0x220C`) can't take a kernel update: EZ-Flash's loader stops with "Bad NOR ID" (F5).~~ Fixed by v0.3.2 (`1a7fd3a`).
- ~~When the power-on save copy runs out of room, its progress dialog still fills and says "Done", and no error shows: only the log has it. `Check_save_flag` calls `progress_done(gl_done)` whatever `Save_savefile` gives, and `Save_savefile` returns 1 after a short write (`source/ezkernelnew.c`). Seen on the emulated cart in `tests/saves.sh full`'s case: the bar goes from 43% to a full "Done". Show an error dialog instead (v0.3.2 tests; v0.5's crash-safe saving replaces the copy).~~ Fixed by v0.3.2 (`384c622`): error 3 "Can't write the save" shows instead, also for BACK UP SAVE; `tests/saves.sh full` checks it.
- ~~NOR DELETE erases the latest NOR game at once, without asking; the SD list's delete and FORMAT ALL ask first (`nor_menu_a` in `source/screens/lists.c`) (v0.3 emulator validation).~~ Fixed by v0.3.2 (`c211261`): it asks "Delete this game from NOR?"; golden `nor-delete`, `tests/nor_safety.sh` answers B, then A.
- ~~Messages ("[B]OK") ignore A (`dialog_message` in `source/ui/dialog.c`) (v0.3 emulator validation).~~ Fixed by v0.3.2 (`c211261`): A or B closes them, the hint reads "[A]OK", and a message still opening drops A; golden `dialog-double-a`.
- ~~The NOR list shows "0M" for games under 1 MB (`list_fetch` uses `>> 20`) (v0.3 emulator validation).~~ Fixed by v0.3.2 (`c211261`): it uses the SD list's `size_text`; goldens `nor-delete`, `nor-launch-fail`.
- ~~Recently Played keeps a file deleted from the SD list, and B after a failed CLEAN BOOT of a missing Recently Played file returns to the SD list (v0.3 emulator validation).~~ Fixed by v0.3.2 (`c211261`): the delete calls `Recently_play_forget`, and every way back from a Recently Played entry returns there; `tests/stale_recent.sh`, unit test `recently_played_remove`.
- ~~The empty NOR list is blank, while Recently Played says "No game played yet" (v0.3 emulator validation).~~ Fixed by v0.3.2 (`c211261`): "No games in NOR"; goldens `nor-list`, `nor-delete`.
- ~~Recently Played's title reads "RECENT PLAYED" (`en_recently_play`) (v0.3 emulator validation).~~ Fixed by v0.3.2 (`c211261`): "RECENTLY PLAYED", which fits the title bar.
- ~~A DOWN wrap from the end of the 302-file `/Long` folder costs 195,784 cycles, 14% over `lists-page`'s 171,561, and no golden covers it (v0.3 emulator validation).~~ Fixed by v0.3.2 (`99fdea6`): golden `sd-wrap` wraps both ways in `/Long`; the budget is re-recorded, as the frame is bounded by design (v0.3-decisions, "v0.3.2 validation fixes").
- ~~SETTINGS page 2's SAVE row opens its edit on OK, not on the value, so A, A saves it unchanged (`page2_event` in `source/setwindow2.c` resets every row's edit position but `auto_save_pos`); MODE B's edit starts at RUMBLE, not at its mode (v0.3 emulator validation).~~ Fixed by v0.3.2 (`46e1138`): both open on their value; golden `settings-edit`.
- ~~Turning LOGGING on freezes the menu for about 80 frames and drops the keys pressed then: `start_file` zero-fills the 64 KB log file after `f_getfree`, inside the OK press (`source/tlog.c`) (v0.3 emulator validation).~~ Fixed by v0.3.2 (`f38df20`): `tlog_flush_soon` opens the log a step at a time in an engine job that lets the screen draw first (`ui_settled`, `JOB_WAIT`); `tests/logging.sh settings`, kernel test `engine_jobs`.
- ~~Diagnostics' start page says "B: back", but B soft-resets the kernel, which lands at the SD root (`source/diag.c`) (v0.3 emulator validation).~~ Fixed by v0.3.2 (`c449f44`): B ends Diagnostics' session and returns to the menu (the NOR list from SETTINGS); golden `diag-back`.
- ~~Diagnostics' pages clip long lines ("boot loader CRC 395276F5, present, with a…", "WAITCNT 4018 in the menu (games start at…"), the clock page shows only its title for 2.2 s, and the EEPROM page names `tests/roms/eeprom` (v0.3 emulator validation).~~ Fixed by v0.3.2 (`c449f44`): `diag_say` wraps a long line at a space and the two lines are shorter, the clock page says it is measuring first, and the EEPROM page speaks to users; golden `diag-pages`, kernel test `diag_line_cut`.
- ~~The boot shows "microSD card ready / Loading..." for about 3 frames, then fades to black before the list (`source/ezkernelnew.c`) (v0.3 emulator validation).~~ Fixed by v0.3.2 (`fdd9fe2`): a card that mounts shows nothing until the list; golden `boot` hashes the frames from the hand-over, `tests/smoke.sh` checks no notice shows.
- ~~Review of `f38df20`'s background log open: it holds `log_file` across frames and FatFs keeps no lock (`FF_FS_LOCK 0`), so a delete from the SD list could free the file under it; cut off part way (a reset, power loss) after other FatFs calls had written the FAT out, the new file's clusters were lost (fsck.fat: 5 clusters); turned LOGGING off, it went on writing the card (v0.3.2 review).~~ Fixed by v0.3.2 (`9d2680d`): a delete calls `tlog_open_now`, `start_begin` allocates the file whole and syncs its size, and the job stops with LOGGING off; `tests/logging.sh open`, held part way by the hook `tlog_open_steps_test`. Not confirmed: interleaving with folder listing, covers, the theme, a launch, the settings commit or the pending-save copy (separate FILs, absolute paths, its own `bounce`, no `pReadCache`; every job step and every FatFs call is whole on the one thread), and a launch (`engine_leave`, then `jobs_init` drops the job and the launch's `tlog_flush` finishes the open at once).
- ~~Since `fdd9fe2` the boot shows nothing until the list, so a big, slow card could stay black for seconds (v0.3.2 review).~~ Fixed by v0.3.2 (`3d920ed`): "Loading..." once the boot has been busy 20 frames after mounting (`Boot_notice_check`, also between the log open's steps); golden `boot-slow`, golden `boot` stays flash-free. `gl_init_ok` went with it (`0954c7b`).
- ~~B on Diagnostics' first page from SETTINGS lands on the NOR list, not where DIAGNOSE was picked (v0.3.2 review).~~ Fixed by v0.3.2 (`386e5ed`): SETTINGS page 2 again, the cursor on DIAGNOSE (`SETTINGS_AT_DIAGNOSE`); golden `diag-back`.
- ~~No golden covers B on a Recently Played game's menu returning to Recently Played (v0.3.2 review).~~ Fixed by v0.3.2 (`51ebdf1`): golden `recent-menu-back`.
- ~~A Recently Played entry whose file is missing stays on the list after a failed launch ("Can't open the file") (v0.3.2 review).~~ Fixed by v0.3.2 (`217f418`): `Game_launch` calls `Recently_play_forget` on `FR_NO_FILE` or `FR_NO_PATH`; `tests/stale_recent.sh`.
- Found by the final emulator validation (067006a), cosmetic, after the cart test:
  - ~~Diagnostics takes about 80 frames to react to A on "start?" (the log file is made synchronously before step 1, unlike in SETTINGS), and its title changes about 40 frames before its body.~~ Fixed (`93c3ba4`): `tlog_flush_soon` makes the file while step 1 shows, and the unit tests' still page finishes it (`tlog_open_now`); the title changes with the list's new page (`ui_list_page_label`); golden `diag-turn` hashes every frame from A.
  - ~~Diagnostics' save chip page shows its title over the previous page's lines, then a blank body while it reads the chip; "reading the 128 KB save chip twice..." goes only to the log, and the title is cut to "save chip and car…".~~ Fixed (`93c3ba4`): `page_turn` brings the title in with the page's first line and lets the fade end before the reads; the page is "save memory", and a title too wide fails an assert; golden `diag-turn`.
  - ~~Diagnostics' pages change title and body at different frames (the timings page shows a half-scrolled row under its title).~~ Fixed (`93c3ba4`, `aa42e9c`): title and lines land in the same VBlank (a `UI_LABEL_SWAP` title, flipped through the queue the list's reveal waits for), and a line that scrolls the list waits for the scroll to end (`settle`); golden `diag-turn`.
  - ~~The slow-boot "Loading..." notice cuts in and out without fading (`tests/shots/boot-slow.opts` says it fades).~~ Fixed (`52c691e`): a notice over nothing comes in from black (`ui_enter`), `dialog_hand_over` runs a frame before its fade-out, and `ui_enter` sets the black blend at once (the first frame showed the background, at the fast boot too); golden `boot-slow` hashes both fades, golden `boot` stays flash-free.
  - ~~BOOT WITH ADDON with SAVESTATE logs "savestates 65535" while it creates the state file (log wording).~~ Fixed (`d9d75ef`): the ADDON launch and NOR write read an erased switch as off (it went into the patch and the NOR entry, whose launch then made a state file); golden `boot-addon` waits for the line, `tests/stale_recent.sh` checks the NOR launch.
  - ~~A Recently Played NOR entry whose game is no longer on NOR stays on the list.~~ Fixed (`5ae634f`): its failed launch, NOR DELETE and FORMAT ALL drop it (`Recently_play_forget_nor`); `tests/stale_recent.sh`.
- Found by the cart test of build 6DF7E865:
  - ~~Switching THEME to an SD theme after WRITE TO NOR froze the cart.~~ Fixed (`c11ab97`): the theme's SD reader in `pReadCache` kept the game's bytes as its `finish` hook; `tests/themes.sh`.
  - ~~Other reads of stale `pReadCache` data.~~ Fixed (`e1d6d5a`): a short or failed read of a `.pat`, `.mde`, `GameID2cht.bin`, a `.sav` into SRAM or a state file used an earlier owner's bytes. Tests now catch the class (`6bd0189`): the lists log `scratch free` before each action and mgba-shot fills `pReadCache` with 0xFF, in no emulated time.
  - ~~A held key on THEME loaded every SD theme it passed (a few frames each).~~ Fixed (`83fd215`): the name steps at once and the theme loads when the key is let go; golden `settings-theme-held`, `tests/themes.sh` counts the loads.
  - ~~The hang left no log: nothing flushed between WRITE TO NOR and SETTINGS.~~ Fixed (`ab9378c`): the log goes to the card as SETTINGS opens and before an SD theme loads; `tests/logging.sh hang`. Not done: the session state in cart RAM page 0xF0 at screen changes, since that page didn't survive the power-off (boot 3 found "nothing in cart RAM").
- **Cart checks for the next test:**
  - Is the brake after letting go at top speed (about two rows) too far? If so, raise `SPRING_BRAKE` (F1).
  - Does the held scroll at 30 rows/s (8 px a frame) still tear? If so, which screen is the cart's: an IPS kit that re-scans the picture tears by itself (v0.3.2 scroll feel).
  - Time a real FORMAT ALL erase and set `CHIP_ERASE_FRAMES` from it; its bar is only an estimate (F4).
- The cheat parser drops spaces from cheat names ("Never money" shows as "Nevermoney"), more visible in the proportional font (T11 review; v0.8 cheats).
- The cheat parser's `Get_KEY_val` and `Get_CHT_val` copy 50-byte fields that may lack a NUL (T16; v0.8 cheats).

## v0.4 Browsing

Planned in the interview and design rounds of 2026-10-19 and 2026-10-20. [v0.4-decisions.md](v0.4-decisions.md) has every decision, and [docs/design/v0.4](../design/v0.4/README.md) has the mockups in light and dark. They are a first version: expect to tune much of the look and motion once it runs on a cart.

Built in tickets B1–B23 (reports in [v0.4-reports](v0.4-reports/)); the cart checks in [docs/release-checklist.md](../release-checklist.md) come before v0.4.0. What the build left open is under v0.4.2.

- ~~**Navigation, all new:** a carousel home (Recent, Favourites, each console with games, SD card, NOR, Settings, Info) on a ring of five visible objects; A zooms into the item's page, B comes back. No tab bar, and none of the official keys.~~ Done (B10, B12, B19).
- ~~**Pages:** game lists per console, Recent and Favourites (compact or with a placeholder box, letter jump, a game popup), an SD card browser, NOR with storage and Format all, Settings on one page of section cards, and Info (controls, help, about, Diagnostics).~~ Done (B11, B13–B18); Info holds Controls, In game, Your cart, About and Diagnostics.
- ~~**Look:** "Paper clean" in built-in light and dark themes (SD themes dropped), one accent colour per item, a new icon set, a red torii glyph in the corner, and the carousel, opening and popup motions.~~ Done (B1, B2, B9, B11, B12, B19).
- ~~**Library index:** the whole card scanned by extension into `/torii/library.idx`, up to 4,096 games, rescanned when the card changes.~~ Done (B3; a background re-walk notices the change, R1).
- ~~**Names:** custom names, then cleaned file names (no extension or bracket tags); system folders and dot files hidden, with a "Show all files" setting.~~ Done (B4, B18).
- ~~**Favourites and recents** on SD (`/torii/favorites.txt`, `/torii/recent.txt`), favourites with no cap; a "Start on" setting picks where boot starts (where you left off by default).~~ Done (B5, B10); favourites are capped at 4,096.
- ~~**Settings:** a 12/24-hour clock and a time editor.~~ Done (B6, B16).
- ~~**Limits:** 1,024 entries per SD folder.~~ Done (B14).

- **Engine follow-ups from v0.3, all in v0.4 (session reports, 2026-10-19):**
  - **Smoothness:**
    - ~~A page change still blanks the list for a frame before it fades in. Fade out first (F1).~~ Done (E1, B7).
    - ~~A whole-palette fade costs about 45k cycles a frame. Fade only the banks in use, or use the brightness registers for fades to black (`palette_fade`) (F1).~~ Done (E2, B7).
    - ~~A page change's hidden draw peaks at about 248k cycles of a 281k frame (`UI_DRAW_BUDGET`) (F1).~~ Done (E3, B7).
    - ~~Grids don't glide on held LEFT/RIGHT (F1).~~ Closed: grids were removed (E4, B7).
  - **Transitions:**
    - ~~A true cross-fade between screens needs both resident; today they fade through the background (F2).~~ Closed by the zoom, which carries home's object across; two full screens don't fit at once (E5, B19).
    - ~~Mosaic transitions exist but no theme or golden uses them (F2).~~ Removed (E6, B19).
    - ~~Parent/child transitions could scale, with affine BGs or sprites (F2).~~ Done: the open zoom (E7, B19).
    - ~~The `KIND_effects` theme key could take names (`fade+slide_x`) instead of a bitmask (F2).~~ Closed in v0.4: SD themes and `theme.ini` were dropped (E8, B1).
    - ~~Dialogs in a session of their own (launch, firmware) appear without motion, and dialogs popped by a screen change pop without motion (F2).~~ Done (E9, E10, B19).
    - ~~A menu that comes up with its screen shows at full brightness over a list fading in (F2).~~ Done (E11, B19).
  - **Data:**
    - ~~There is no row cache keyed by item (VRAM has no room for more slots; EWRAM could hold one) (F1).~~ Done (E12, B7).
    - ~~`ui_list_autoscroll` sends no cursor events (F1).~~ Done (E13, B7).
    - ~~`frame_measure` keeps one name per frame (F2).~~ Done (E14, B7).
  - **Progress:**
    - ~~Show progress during the cart's own ROM copy (`Send_FATbuffer`'s IWRAM busy loop) with an IWRAM VBlank hook; it needs a cart test (F4).~~ Done (E15, B8); its rate is still to be measured on a cart.
    - ~~The bar shows overall progress and the status the step's percentage: pick one.~~ Done: the bar (E16, B8).
    - ~~The NOR write's dialog title.~~ Done (E17, B8).
    - ~~Trim the launch's 0.3 s of step texts if users notice it.~~ Done: only the slow steps show (E18, B8).

## v0.4.1 Art

- ~~**Box art** (about square for GBA and GB), pre-converted on the PC, loaded after the cursor rests.~~ Done (B22).
- ~~**Lookup:** custom art by file name first (romhacks keep their own art), then CRC or GBA game code, then the placeholder.~~ Done (B22).
- ~~**Names** from a No-Intro CRC database, built by a `just` tool and not bundled.~~ Done (B21: `just names-update`).
- ~~**Tools:** `just art-add` for custom art and `just art-scrape` from libretro-thumbnails (check their terms first).~~ Done (B22, B23).

## v0.4.2 Fixes and review from the v0.4 build

What the v0.4 ticket sessions, the conductor's reviews and the research left open: bugs to fix, the choices the build made that the user may want to change, and the assumptions that still need a real cart or a decision. [v0.4-decisions.md](v0.4-decisions.md)'s ticket log has the detail. Each item is struck out once fixed or settled.

### Bugs

- The launch copy's VBlank hook sets the VBlank handler back to none afterwards instead of restoring the previous one; harmless while the engine installs none (`source/progress.c`; B8 review).
- The `diag-tests` golden failed once at frame 4 while the colima VM was busy, then passed unchanged: possibly flaky under load (B2).
- ~~Diagnostics' line list (`source/diag.c` `lines_fetch`) doesn't refresh a line that leaves its window (B7).~~ Fixed by B17 (`b5b99ad`).
- Parsing a large `/torii/names.txt` runs as one step of the library rebuild: 0.26 s for 1,000 sorted lines, 0.82 s unsorted, a one-off menu stall (`gamename_custom_parse`; B3).
- `tr-tab`'s `settings2-in` and `settings2-back` goldens catch a plain backdrop frame since B9 (v0.3 Settings page 2 comes in a frame later); B10 deletes those screens (B9).
- ~~A list already at its last scroll sends a cursor event with an unchanged index on every DOWN (B9 review).~~ Fixed by B17 (`b5b99ad`).
- A popup's first draw (143k cycles) and a confirmation's (170k) land a frame late while the library's background scan runs (B11).
- While an overlay moves, only one list clips, so a page with two lists shows the second's spare rows for 4–6 frames; over a popup, the confirmation's rounded corners show the dimmed page for 1–2 px (B11).
- Deleting the running log file with LOGGING on stops SD logging until the next boot (pre-existing; B10).
- Non-canonical paths such as `/deep/../x.gba` launch and are stored in Recent as given (only the test hook can make one today; B10).
- After a power cut drops a half-erased NOR game, a "kernel error: … dropped" line repeats every boot until the next list save (pre-existing; B15).
- Behind the NOR Delete question, the popup's edge and Delete row show beside the confirmation (B11's design; B15).
- `tests/nav.sh` page routes assume the menu starts on home; scripts set Start on = Home first (B14, B15).
- A key pressed while START waits for an SD page change still reaches the page after the popup closes (B14).
- A power cut in the middle of a settings write leaves the whole settings sector erased, so every setting reads its default: `Save_info` erases one sector and programs it with no second copy (pre-existing; B16's tester).
- A failed settings write isn't retried within the visit and shows no message; the live theme stays (B16).
- `ui_hints_get` doesn't give back a hint note's ink, so a popup over a danger or countdown line restores it as plain text (can't happen today; B16).
- While the Time/Date field card is open only `ui_frame` runs: the library walk and a Rescan pause until it closes (B16 review).
- Opening Favourites with thousands of entries blocks for up to about 1.8 s (3,367 favourites), and again after un-favouriting or a new index; cache across visits or make it a job (B13).
- The cheat parser drops spaces from cheat names ("Infinitelives"; pre-existing in v0.3).
- The selected row's marquee keeps scrolling under an open popup (about 13k cycles a frame; B13).
- A first visit to a game page right after a failed launch briefly shows an empty card until the index reloads (B13).
- The game popup over row 0 lets the row's size peek out clipped above the popup's top (B13).
- The theme wipe needs a commit every VBlank: a frame that overruns during the 16-frame wipe shows the old theme whole-screen for that frame (B19).
- A launch shows about 8 black frames between home's fade-out and the loading card (pre-existing; B19).
- ~~`gfx_hold_regs` keeps stale bits after a hold ends (B12).~~ Fixed by B19 (`ed91130`).
- A cover landing during a palette fade could show wrong colours; one frame of new palette on old tiles is possible if the VRAM queue is ever overfull (B22, not seen).
- `just art-scrape` and `just names-update` print a Python traceback on a read-only or full card instead of a clear message (catch `OSError` in `main`); a 403 or other 4xx isn't remembered as a miss (B23 review).
- Block 0's BG tiles on list pages are nearly full with a popup and a confirmation up (9–20 tiles spare); a taller popup or new widget falls back to closing the popup (B18).
- After an SD rename of a game with a custom name, the row shows the cleaned name until the walk puts the custom or database name back (a few seconds; B18).
- The rename's rollback on a failed move is checked only by reading the code; no test forces a move failure (B18).
- The shared-save check before an SD rename sees only indexed games: a game in a hidden folder sharing the save name loses it (B18 review).
- A large crc.txt loads slowly after each boot: about 6 s of background work for 3,000 records (emulated), mostly hashing paths (B21).
- A read-only crc.txt is never updated: dates are re-adopted and Recent games re-identified every boot (B21).
- ~~Past the library's 4,096 games no page says so: the decisions' "showing the first N" note exists only for SD folders; `library_truncated` and `library_games_seen` have no caller (B20).~~ Fixed by B20 (`26b8886`): a console's last row shows the note (`library_games_seen` still has no caller).
- Eight icons are drawn by nothing: the 8x8 Favourites, Recent, SD, Settings, torii and up icons and the 16x16 file and folder icons (`assets/ui`; only `tests/kernel/test_assets.c` reads them) (B20).
- `action_launch`'s result (`ACTION_NOR_MAYBE`, `ACTION_NOR_WRITTEN`) is ignored by all three callers: use it or drop it (B20).
- The SD card page's item count job costs up to 194k cycles a frame while it counts a folder's entries (within a frame; seen in the UI walk, `sd-folder`) (B20).
- ~~Keys pressed during the open zoom reach the page before it shows: A, A on home within about 4–9 frames starts Recent's first game without the list ever showing, and START opens the game popup over the page still sliding in; a 1-frame B during the zoom is lost while the A after it isn't (B20's tester).~~ Fixed by B20 (`665de8b`): A, START and SELECT are dropped until the page has come in.
- START and B mashed 3–4 frames apart over a game popup can drop a START and pass the next B to the list, which goes home (B20's tester; not reachable by hand).
- ~~An empty SD folder still shows the hints "A open · START menu" (B20's tester).~~ Fixed by B20 (`1d1146b`).
- The UI walk (`tests/shots/data/walk.txt`) uses 55 of mgba-shot's 64 waits (`MAX_WAITS`): raise it or split the walk before it grows (B20).
- Settings' Mode B is only stored: the kernel never reads it (as in v0.3); confirm on a cart in a DS that something does, or drop the row (B20).

### Decisions to review

Made during the build without the user; each is cheap to change.

- **Research overrides R1–R9** (v0.4-decisions.md): no folder-date check (a background re-walk instead), two-copy safe writes, sort order (case and accents folded, numbers by value, "The" counts), the name-cleaning rules, the 14 px row canvas with a sprite pill, double-size sprites only for the zooming object, a hard-edged theme wipe, the title bar on its own layer during the open, the confirmation's dim toward white in light.
- **Favourites are capped at 4,096** although the decisions said no cap (B5).
- **Palette changed for contrast:** light ink3 `#8C847B`, dark danger ink `#EF7B84` (B9).
- **16x16 icons keep their PNG colours** instead of the item's base colour, the same in light and dark (B2).
- **A page change fades out, then in:** it lands about 9 frames after the key instead of 6 (B7).
- **Loading card:** titled with the file name; the copy estimate is a guess (64 KB a frame) until measured; a launch with a pending save still shows a separate "Saving…" card first; a launch with an unsaved library index shows "Saving the game list" first (B8, B3).
- **Hotkeys:** 2–3 keys including L, R, START or SELECT, no overlap with the other hotkey even while it's off, nothing inside A+B+START+SELECT; with Save states on, the reset keys are armed whatever "Reset hotkey" says (B6).
- **Settings rows, labels and defaults** (Game view List, Show all files Off, 24-hour clock On, Start on Last place; Start on can't pick NOR, Settings or Info) (B6).
- **Hidden files:** PATCH and CHEAT hidden too, and every file the kernel can't start (`.sav`, `.txt`, `.cht`) on the SD card page; `.sgb` and dot-less files no longer launch (B4, B3).
- **Name cleaning:** 4–5 digit scene prefixes dropped except 1900–2099; the article table can misfire ("Rock, Paper, A"); colliding games show their first differing tag, preferring Rev/Beta/translation tags (B4).
- **The "played ago" words** and the unset clock counting as before 2026-10-01 (B5).
- **Mockups for Settings and the rename keyboard,** approved by the conductor for the build (`docs/design/v0.4/settings-*.png`, `keyboard-*.png`).
- **Dropped v0.3 features:** the NOR menu's load and back-up of a save; the help QR code; the cheat screen, per-game save type, hard reset and write to NOR with addon come back in B13/B14 (B10).
- **A starts games with the enabled addons** (Play, Launch); Clean boot is a separate entry; v0.3's A, A was a clean boot (conductor, B13/B14).
- **Rename:** B never cancels the keyboard; Rename only for indexed SD games; SD rename moves files, not folders; with an addon on, the SD Delete confirmation closes the popup first (B18).
- **Art scraper:** one timeout stops fetching for the run; existing art never refreshed; romhacks get base-game art (B23).
- **Box art:** custom art shared by same-named files in different folders; covers load after 8 frames of rest with no fade (B22).
- **Game lists:** Favourites shows no stars; list with art shows two facts; Game info is a page; GB/GBC/NES offer only Play (B13).
- **SD card page:** Delete reaches `/torii` and `/SAVER` with Show all files on; the first 1,024 files the card lists are shown in a bigger folder; folder icons blue (B14).
- **Settings:** a black pause while the NOR is written on leaving; Time/Date write the clock at once; edits lost on power-off before leaving; the pill jumps rather than slides (B16).
- **NOR page:** one card with four rows, only the latest game deletable, no hold-A for Format all, L/R don't page (B15).
- **Motion:** the opening zoom's pieces and timing; a hard-edged 16-frame theme wipe; dialogs rise in 6 frames, the loading card shows at once (B19, conductor).
- **Carousel:** neighbours at 0.89 and far items at 0.60 scale read close to the front; held-turn speed; one item showing at both ends mid-turn with 2–5 items; far dark objects nearly invisible in dark (B12).
- **Info texts:** all wording, card sizes in binary GB/MB, Your cart's contents (B17); Controls must be re-checked once every page has landed (B20).
- **Names database:** names-update computes CRCs on the PC; a romhack missing from the dat shows its base game's name via the GBA game code; no CRC at launch (B21).
- **The boot position is written after 5 s idle,** a new SD write in the menu (B10).
- **Popups and confirmations:** keys pressed while one closes: moves reach the page, A/B/START/SELECT are dropped; the confirmation's dim toward white also fades the hint bar's labels in light; L/R page by 8 rows (B11).
- **Look values read off the mockups** where spec.md is silent: shadows, scroll bar, hint chips, pill size and spring, path pill colour (B1, B9).

### Open questions and assumptions

- **Unmeasured on a real cart:** the cart's ROM copy rate (B8's bar estimate); the library walk and first-scan times (emulated SD is about 5x slower than the cart); the theme wipe's HBlank palette DMA (mGBA can't show tearing); how the carousel's scaled pixel art reads at 0.6x and 1.9x; the 14 px rows' feel.
- **SD folder open time** on a cart (0.2 s per 300 files emulated) and the 251k-cycle frame on a folder of 1,024+ subfolders (B14).
- **Raster split on a cart:** a late VCOUNT interrupt could make the wipe's edge ragged; the open at 233k cycles a frame (B19).
- **Opening a cover in an art folder of thousands of files** on a real cart (B22).
- **Names database timings on a cart:** Identify about 1.6 s per MB in the foreground (estimate); crc.txt write steps about 10 ms each (B21).
- **Licences:** No-Intro's terms for a derived name table (the names database is built on the user's machine and never bundled); libretro-thumbnails art has no licence grant (fetched locally, never committed).
- **Design questions for later tickets:** Settings' 12 px rows inside section cards that scroll (B16); whether WIN0 and WIN1 suffice when the popup, the list clip, the dim, the zoom and the wipe overlap (B11, B19); the 8x8 GBA and Info icons need a redraw.
- **PSRAM map:** the user was unsure about fixing it; it now holds SD names (832 KB), two library index slots (1–3.5 MB), recents and favourites (3.5–4 MB), the cover cache (4 MB) and reserves.

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

- **From v0.3:** opening a big .cht file freezes the cheat screen while `Analyze_KEYVAL` runs. Run it as a job with progress (`Open_cht_file`) (F2).

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
  - Per-glyph row sizes would save about 1.5 KB of ROM (`tools/font.py`) (T1).

## Backlog

Small items from the v0.3 sessions with no version yet:

- **Logs:**
  - Frame-timing summaries carry the log write's time, not the scroll's.
  - They are DEBUG only.
  - Their table holds 8 names.
  - On a nearly full buffer they can push out older lines (`source/engine/cycles.c`, `tlog_flush`) (F3).
- **Crash dumps** name stale stack words that point into read-only data (`t_model+0x2628`) (`source/cartlog.c`, `tools/logs.py`) (F3).
- **Diagnostics wording:** "3 seconds passed in about 2.2" (F3).
- **Lost at power-off:** the session record in cart RAM page `0xF0` doesn't survive a power cycle (hardware). Keep it somewhere battery-backed if wanted (F3).
- **PSRAM:** where a DMA into PSRAM from cart space lands on the real cart is unknown. Nothing does it any more (F3).
- **Logs:** the save log line still says `"Save sav?"`; change it together with `tests/saves.sh` (F4).
- **Latent risks:**
  - The second half of `Save_savefile` runs frames with SRAM page 0x10 selected.
  - `FRAM_save_op` runs frames with a file open and the working folder at `/SAVER`.
  - Both are safe today and commented (F4).
- **Release file:** EZ-Flash's loader installs only the first 3 MB of `ezkernelnew.bin`; `tools/omega_image.py` enforces it (F5).
- **Emulator:**
  - The kernel install fills the last sector's tail with 0xFF, where the real loader writes stale bytes.
  - `OMEGA_FPGA_VERSION` isn't validated (F5).
- **Font tests:**
  - `tools/font_preview.py` needs network for the candidates.
  - `tests/font_test.py` compiles with the host `cc`, not devkitARM (T1).

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
