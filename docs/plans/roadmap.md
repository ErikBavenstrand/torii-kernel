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

- **Save types:** Doom, Doom II and Gauntlet are now EEPROM 512, and the Classic NES Series
  are EEPROM (ez-flash/omega-de-kernel#3).
- **Cheat table:** capped at 600 writes, which is what the 0x2000-byte patch area holds.
- **Clock and rumble:** they are now separate settings, shown as one row, `CART: [x] CLOCK
  [x] RUMBLE` (ez-flash/omega-de-kernel#5).
- **GB, GBC and NES games:** they start without the Clean Boot menu. The Goomba palette
  waits for v0.6 (ez-flash/omega-de-kernel#12).
- **Sorting:** the SD listing uses a case-insensitive merge sort.
- **Recently played:** it works at its 10-entry limit.
- **Clean boot:** EWRAM is fully cleared when a game starts.

Still open from the bug work:

- **EEPROM sizes:** settled, no change needed. FlashGBX's cart-dump database (`db_AGB.json`
  in `res/config.zip`) lists 941 table entries marked 0x22 (EEPROM 8K) as 512-byte EEPROM,
  plus 9 marked 0x23 and 6 marked 0x32 (Flash 64K). The real cart decides the address width
  per request, so 8K mode takes these games' 6-bit requests and their saves stay in the first
  512 bytes (`docs/research/cart-test-2026-10-18b.md`). The 0x23 and 0x32 entries are
  unchecked, and Doom and Gauntlet stay EEPROM 512.
- **Hook coverage:** done. `just compat` has an in-game menu column; all 118 commercial
  GBA games in the library pass (`docs/research/hook-coverage.md`). Four games get exact-ROM
  patches for the EU dumps tested; other regions may need their own. The menu keeps the game's
  brightness, but still resets the BG2 affine registers, which are write-only.
- **Game NOR:** WRITE TO NOR now runs in the emulator (`tests/nor.sh`), which showed these
  kernel bugs. Each one can lose or overwrite a NOR game:
  - Fixed: `Read_S98NOR_ID` (`Ezcard_OP.c`) left the chip in ID mode, so the NOR list could
    read empty and the next write landed over the first game. It now writes `0xF0`.
  - Fixed in v0.2.1 (`tests/nor_safety.sh`, the list format in `omega-de-hardware.md`):
    `GetFileListFromNor` returned without `REG_IME = 1` when its scan reached the end of the
    64 MB. "Delete latest" erased only the game's first 128 KB and kept its list entry, so a
    write with the same header cut off after one block was listed. Losing power inside
    `Save_NOR_info` emptied the list, and the next write went over the first game. A ROM
    without 0x96 at 0xB2 was written but never listed. The list now has two copies, games a
    completion mark, and the boot rebuilds a lost list from the marks.
- **Non-ASCII names:** the list draws names by byte count, so CJK names start scrolling too
  early, and a character split at the edge of the list shows as `?`. Names over 99 bytes
  (about 34 CJK characters) show cut with `*` and "Name too long", and can't be opened. The
  v0.3 file list fixes both.
- **Clean boot:** IWRAM is still not fully cleared at launch, because the `SoftReset_now`
  code stays there. No game is known to need it. Clean-boot tests use `tests/roms/idle`
  (no crt0), since devkitARM's crt0 clears EWRAM itself.
- **From the logging work:**
  - `Save_savefile` ignores short writes, and `Check_save_flag` ignores its result, so a
    nearly full card silently truncates a new save, e.g. 64 KB written as 32 KB (v0.5's
    crash-safe saving replaces it).
  - The in-game menu's LOAD with no saved state jumps to 0xFFFFFFFE.
  - Fixed in v0.2.1: the kernel erased the NOR game table whenever it read the list as empty
    (a chip left in a command mode or a read glitch), and the next WRITE TO NOR overwrote the
    games. Reading the list now writes nothing, and a write reads it again first.
  - The `/torii` folder shows in the SD root list and moves games down a row (key scripts
    in compat may need an extra DOWN). v0.4 hides system folders.
  - The log file sequence number doesn't handle overflow past 99999999 (only reachable with
    crafted files).
  - Needs the real cart: whether cart RAM page 0xF0 survives a power cycle, which WAITCNT
    settings read correctly, how the FPGA answers EEPROM requests of each width, rumble at
    GPIO values 2 and 3 without the clock, whether an SD write right after the FPGA copies
    a game into PSRAM is safe, and what the BIOS does on an undefined instruction.
- **Settings default:** done. The emulated kernel NOR already read 0xFFFF for settings that
  were never written. The 0x0000 seen came from saves, which zero slots 65–255
  (`tests/settings.sh`), and erased hotkey slots are now range-checked. Known slots: 13 is
  CLOCK and 25 is RUMBLE; 23–24 were DS Style's reset and backup toggles, so leave them
  unused.

### Tooling (done)

- **Warnings:** add `-Wextra -Wshadow -Wundef -Wformat=2 -Wstrict-prototypes
  -Wmissing-prototypes`, exempting the vendored FatFs. Fix everything, then turn on `-Werror`.
- **Stack and size:** add `-fstack-usage`, and try `-ffunction-sections -fdata-sections
  -Wl,--gc-sections`, comparing `just size` before and after. Tried and rejected: gc-sections
  cost 568 bytes of ROM and 24 of IWRAM.
- **Editor config:** add `.editorconfig` and `.gitattributes`.
- **Formatting:** run a pinned clang-format through `uvx` in `just lint` and CI, and add a
  short formatting note to CLAUDE.md. There is no Claude Code hook.
- **clangd:** add `.clangd` with `--target=arm-none-eabi`, built on
  `just compile-commands`.
- **Host tests:** build pure code (parsers, sorting, cheat decoders) for the host with
  ASan/UBSan, reusing `torii_test.h`.
- **Advisory analysis:** run cppcheck, clang-tidy (`bugprone-*`, `clang-analyzer-*`) and
  GCC 10's `-fanalyzer` in report-only mode, with a baseline. GCC 10's analyzer gives up on
  the kernel's big functions (`-Wanalyzer-too-complex`) and finds nothing so far.
- **Pinning:** Docker images by digest plus a Debian snapshot, Actions by SHA, just and uv
  by version. Bump the snapshot and the image digest together.

Notes from the tooling work:

- Making ROM functions `static` grew the static deepest-stack estimate from about 1.8 KB to
  2.4 KB (`main`, `Process_savefile`, `Open_cht_file`). It fits in IWRAM's free space, and
  `noinline` on the save writer and cheat readers would win it back if needed.

### Source cleanup (done)

1. Port the Simple DE and DS Style fixes we want while their code still lines up with ours.
2. Remove the Chinese strings, the HZK12 font and FatFs's CP936 tables (about 87 KB each).
3. Convert to UTF-8 with LF line endings, and run clang-format once in its own commit,
   listed in `.git-blame-ignore-revs`.

Then update CLAUDE.md: the GBK, CRLF and `zh_*` rules go.

### Logging and on-cart debug mode (done)

#### SD logging

Logging to SD is built first, because the debug mode writes its report through it. It is
a persistent setting, `Logging: Off / Errors / Info / Debug`, off by default and free when
off. Until `torii.ini` arrives in v0.5, it lives in a free kernel settings slot, like CLOCK
and RUMBLE. Kernel crash dumps are captured even when logging is off.

- **Kernel log:** `tlog()` writes into a small ring buffer in RAM (2–4 KB of the free
  EWRAM). The buffer is flushed to `/torii/logs/` at safe points: before a launch, on
  errors, and when settings close. Files are append-only and rotate, keeping the last N,
  so the card never fills. Every boot starts with a session marker. Under the emulator,
  `tlog()` still goes to mGBA's console.
- **Launch record:** before each launch, the log records exactly what the kernel did:
  game code and revision, save type, the patches and hooks applied (including
  `rom_patches` and `reset_table.h` matches), cheats, CART settings and the plugin.
- **Crash detection:** if a game ran and didn't come back cleanly, the next boot logs that
  the previous session ended abnormally, next to its launch record.
- **In-game snapshot:** a "Save debug info" entry in the in-game menu writes registers,
  the hook state and a small memory sample to SD.
- **Kernel crash dump:** an exception handler stores the registers and call stack in cart
  RAM that survives a soft reset, and the next boot writes them to the log. Whether the
  dump also survives a power cycle is checked on the real cart.
- **Tools:** `just logs PATH` pulls the logs from an SD card and turns code addresses into
  function names from the matching `ezkernelnew.sym`, in the same format as the emulator's
  crash reports.

#### On-cart debug mode

Not a setting: you run it when you want it, from a Diagnostics entry in settings or by
holding a key combination at power-on (in case the menu itself is broken). It logs at
Debug level for its own run only. It:

- runs the unit tests;
- probes the hardware: RTC, PSRAM, NOR reads, and a save-chip test that doesn't touch the
  user's saves;
- measures real timings: NOR wait states (the kernel never sets `WAITCNT`), glyph
  rendering, LZ77 decompression and SD throughput;
- settles the EEPROM 512 versus 8K question for games in the save table;
- looks for a separate rumble control. The Omega DE has its own rumble motor on the cart
  GPIO port, and `0x096A0000` gates the whole port. The kernel now enables it per game,
  from the GBA game code or the GB cart type at 0x147 (`a854cdc`). GBA romhacks that add
  rumble only get the port while CLOCK is on;
- walks you through the manual checklist with pass/fail buttons.

It writes its report to SD as a log stream that we bring back to the repo. When the real
cart disagrees with the emulator, the fix goes into the cart model.

## v0.2.1 Real-cart fixes (done)

Every known bug, found in our work or inherited from the official kernel, is fixed here.
From the first real-cart test of v0.2.0 (`docs/research/cart-test-2026-10-18.md`):

- **Log files:** fill new log files with zeros from RAM, not a ROM array. On the real cart,
  ROM reads while SD mode is on return the SD buffer's leftover data. Make `disk_write`
  refuse or copy any buffer in ROM. Recover log files already full of junk (start a new file
  when the space after the text isn't zero), and make `log_find_end` robust.
- **Cart model:** while SD mode is on, ROM reads don't return NOR contents. Cart RAM beyond the
  save clears on a power cycle. The clock gate is `0x096A0000` bit 0. The EEPROM doesn't
  answer without a game loaded. Add a test that log files are zero after their text.
- **Reproducible builds:** sort the Makefile's `$(wildcard)` lists, so CI and macOS link the
  same binary. Attach `ezkernelnew.sym` to each release, and put a real build ID (a CRC or the
  git hash) in the boot line. `just logs` warns when the symbols don't match.
- **Save logging:** log every "Save sav?" decision, save copy (with size and result), NOR
  write and settings save, including `f_write` and `f_close` results. Done: a short copy is
  logged as an error with the bytes written (`tests/saves.sh`), but still cuts the save short;
  handling the error and keeping the old save is v0.5's crash-safe saving. On the real cart,
  check a power-on copy's log lines.
- **Diagnostics:**
  - name the failed checklist item in the summary;
  - show the clock live on the clock item;
  - log probe answers as info, not errors;
  - after a crash, say the session crashed;
  - show a GB game's title instead of `(????)`;
  - in the checklist, say to use RESET, not power-off, before checking the DEBUG snapshot.
- **Clock display:** the checklist clock looked stuck about 10 minutes behind. Make it live,
  and check the RTC against real time (Diagnostics could log both).
- **In-game DEBUG:** give feedback when the snapshot is taken, and survive power-off. Either
  write it to SD from the menu, or tell the user to use RESET. On the real cart, choosing it
  seemed to do nothing.
- **Checklist answers:** a yes needs proof where the log can give it. For example, flag a
  "sleep works" yes when no launch had sleep on, and an NES yes with no NES launch.
- **EEPROM probe:** run it in game mode, from a small test ROM, so it can settle 512 vs 8K.
- **WAITCNT:** the cart reads correctly at 2/1 with prefetch, 20% faster than the default.
  The menu now runs at `0x4018`, and every launch restores `0x0000` (`source/waitcnt.h`);
  launching (PSRAM writes), the cart's control registers, SD transfers, NOR writes and the
  clock stay at `0x0000`. In mGBA a 300-file folder opens
  in 23 frames instead of 40. **The real cart must confirm it:** browse, open a large
  folder, launch from SD and NOR, write a game to NOR, set the clock and run Diagnostics,
  then check the log for errors. Games that don't set WAITCNT still start at `0x0000`.
- **Rumble:** no GPIO register value drove the motor. Test with Drill Dozer: WarioWare:
  Twisted! needs a motion sensor the Omega DE lacks. Then find out how the cart's rumble is
  driven, or whether this cart has a motor at all.

Also every bug still open from v0.2 that belongs here:

- **Game NOR:** interrupts left off after a full scan, half-written games listed after
  "delete latest", the list lost on a power cut in `Save_NOR_info`, homebrew without 0x96 at
  0xB2 never listed, the table erased whenever the list reads empty, and NOR launches not
  updating Recently Played.
- **Launch:** IWRAM isn't fully cleared (the `SoftReset_now` code stays).
- **Logging:** the log sequence number overflow.

Kept in their own milestones:
- hiding `/torii`, in v0.4;
- UTF-8 name drawing and names over 99 bytes, in v0.3;
- save write errors, in v0.5;
- Jagoomba from source and the palette fix, in v0.6;
- the in-game LOAD crash and the BG2 affine registers, in v0.7.

EEPROM sizes and rumble wait for the next real-cart test.

## v0.2.2 Second real-cart fixes (done)

From the real-cart test of v0.2.1 (`docs/research/cart-test-2026-10-18b.md`):

- **EEPROM probe:** every write lost its last byte, polled right after the stop bit. The
  probe now waits before polling, writes four blocks four different ways and shows which
  bytes differ; the kernel accepts a match on bytes 0–6 and logs the lost byte.
- **Diagnostics:** the EEPROM item checks the probe's verdict; a button step missing L or R
  says the hotkeys need them; the boot after the crash test names it.
- **Save size:** a `.sav` smaller than its save type (512 bytes under EEPROM 8K) is grown
  with `FF`, so blocks past its end survive power-off.
- **Hotkeys:** match the combo's keys, so an extra or stuck key doesn't block the in-game
  menu or sleep.
- **NOR list:** an older kernel's list is saved in the new format once, and list reads are
  logged only when they change.
- **`just logs`:** junk bytes show as a count, not verbatim.
- **Hello** shows a green square while its test cheat runs, for the checklist.

Still to confirm on the real cart (`docs/release-checklist.md`): the in-game menu, save
states, DEBUG, sleep and cheats (the console's L button was laggy), GB/GBC, NES, a NOR write,
rumble, the probe's verdict, and the clock against real time.

## v0.2.3 Boot loader safety (done)

v0.2.2 bricked the real cart at its first boot: the NOR list's copy B went to kernel NOR
`0x7C0000`, which wraps onto EZ-Flash's boot loader, and the cart started in safe mode
(`No_Loader_found`). v0.2.1 wrote the same place on WRITE TO NOR and delete. **v0.2.1 and
v0.2.2 must not be used**; both are marked unsafe on GitHub, and `docs/recovery.md` has the
recovery used on 2026-10-18.

- Copy B moves to `0x780000`; `Save_info`, the only kernel-NOR writer, refuses anything
  outside `0x780000–0x7BFFFF` (`source/bootloader.h`).
- The emulated cart models the real chip: the loader at chip 0, the kernel window at
  `0x40000` wrapping at 8 MB, 8 KB boot sectors, programming failures. Any write to the
  loader stops the run, and `tests/loader_safety.sh` runs every kernel-NOR writer, power cuts
  in them and the other chip; it fails on v0.2.1 and v0.2.2.
- Diagnostics reads the loader (read-only) and logs its CRC and state.
- The release checklist keeps `DE.BIN` on the test card and tests NOR write and delete.

## v0.3 Renderer

The menu is currently mode 3 (`SetMode(MODE_3 | BG2_ENABLE)`), drawn pixel by pixel in C.
v0.3 replaces it with a mode 0 engine built to carry v0.4's UI. The design, the tickets,
and every decision made along the way are in [v0.3-decisions.md](v0.3-decisions.md).

- **Engine:** a fixed 60 fps loop with time-sliced jobs, shadow state committed in VBlank,
  allocators for VRAM, palettes, sprites and affine matrices, and a layout budget checker.
- **Text:** proportional glyphs drawn by an ARM blitter in IWRAM straight into VRAM tiles,
  in palette-index colours. Lists scroll through the wrapping map with 16 px rows.
- **Views:** a widget tree with a view stack, a focus model, and lists fed by a data source.
  Layouts are data.
- **Motion:** eased tweens for transitions, and springs for anything the user can interrupt.
- **Input:** per-key repeat with a stepped speed-up.
- **Covers:** decoded covers cached in PSRAM and streamed into sprites.
- **Names:** cut and measured by UTF-8 character. Full names of any length are kept
  outside EWRAM (for example a PSRAM index), so names over 99 bytes list, open, launch
  and save.
- **Look:** today's screens and keys, with two proportional fonts, light and dark themes,
  and accent colours.
- **Testing:** golden screenshots, a cycle budget per frame, and a renderer step in
  Diagnostics that times the real cart.

The budget each layout is checked against (GBATEK, Tonc):

- **VRAM:** 64 KB for BG maps and tiles, 32 KB for sprite tiles. One layout fits exactly:
  4bpp full-screen text (19.2 KB), a 4bpp wallpaper (19.2 KB), 8bpp 120x80 art (9.6 KB),
  9.3 KB of panels and 4 maps (8 KB).
- **Colours:** 255 BG colours shared by every BG, and another 255 for sprites.
- **Covers in sprite VRAM:** about 5 at 80x80 in 8bpp, or 10 in 4bpp.
- **Effects:** one blend mode and one coefficient pair per frame, no sprite-to-sprite alpha,
  one mosaic size, and 2 rectangular windows plus the OBJ window.
- **Free memory (2026-10-18):** 660 KB of ROM, 4.0 KB of IWRAM and 7.0 KB of EWRAM.

## v0.3.1 Themes

- **Theme format:** `themes/<name>/theme.ini` plus LZ77 assets. Its keys override the
  compiled-in defaults one by one (palette, fonts, spacing, view layouts, icons) through a
  bounded patch table. A missing key or asset falls back to the default theme.
- **Tools:** a PNG converter for assets, and `just theme-check`, which validates a theme
  against the budget and previews it in the emulator.
- **Budget check:** the kernel rejects a theme over budget with a readable error.

## v0.3.2 Renderer fixes

Bugs found while building v0.3 and v0.3.1. Each is struck out here once a ticket fixes it.

- A scrolling long file name runs into the size column in the SD list (T3; port T9).
- ~~The game menu shows "CHEAT(0)" before the cheat list has been opened (T3; port T11).~~ Fixed by T11 (`5e44a3b`).
- ~~The cheat view counts sections plus entries: "[002/004]" for 2 cheats (T3; port T11).~~ Fixed by T11 (`5e44a3b`).
- Diagnostics' loader check can log a garbled loader title: `say()` clears the start of `pReadCache`, where the image is (T16 review; port T12).
- The cheat parser drops spaces from cheat names ("Never money" shows as "Nevermoney"), more visible in the proportional font (T11 review; v0.8 cheats).
- The cheat parser's `Get_KEY_val` and `Get_CHT_val` copy 50-byte fields that may lack a NUL (T16; v0.8 cheats).

## v0.4 Browsing

- **Start screen:** SD, NOR, Settings, last played and favourites.
- **Favourites:** no cap.
- **Recents:** shown as a virtual folder.
- **Boot to:** the screen of your choice, and the SD position is remembered.
- **View modes:** list, list plus art, grid and carousel, as theme layouts.
- **Names:** system files and kernel folders (`/torii`, `SAVER`, `RTS`, dot files) hidden,
  and names cleaned of extensions and bracket tags.
- **Small touches:** a message for empty folders, per-extension icons, and NOR games in the
  same views.
- **Folder index:** 512 files per folder through an index in PSRAM, keeping 256 folders.
- **Art:**
  - title (120x80) and box (80x80) art;
  - custom art for any file or folder, with no cap;
  - art for NOR games;
  - art for console folders;
  - a scraper CLI in `tools/`.
- **Settings:** a 12/24-hour clock and time editor, settings in categories, and Help pages.

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
  themes/<name>/theme.ini + assets
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
5. **Write failures:** a failed write shows a clear error and keeps everything.
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
