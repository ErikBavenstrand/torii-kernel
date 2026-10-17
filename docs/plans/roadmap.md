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

### Bugs

Each fix comes with a test:

- **Save types:** correct the `saveMODE.h` table. Gauntlet and Doom are EEPROM 512, and the
  Classic NES Series are EEPROM, not SRAM (ez-flash/omega-de-kernel#3).
- **Cheat table:** bounds-check it. `Analyze_KEYVAL` doesn't check `pCHEAT`, and cheats past
  the 0x2000-byte patch area spill over (about 607 one-byte entries).
- **RTC and rumble:** make them separate toggles. Turning GAME RTC off also turns rumble off
  (ez-flash/omega-de-kernel#5).
- **Goomba:** persist its settings, and drop the needless Clean Boot prompt
  (ez-flash/omega-de-kernel#12).
- **Sorting:** replace the bubble sort, which swaps 100-byte entries with case-sensitive
  `strcmp`, with a case-insensitive merge sort over an index.
- **Recently played:** at its 10-entry limit it writes 11 rows and never truncates the file
  (DS Style fixed this).
- **Clean boot:** check that `RESET_EWRAM` is part of the boot reset for games that need a
  clean memory state.
- **Investigate with `just compat`:** games the in-game menu and cheat hook miss (the
  literal-pool scan for `0x3007FFC`, or games that never enable the VBlank IRQ), and games
  that need a clean boot state.

### Tooling

- **Warnings:** add `-Wextra -Wshadow -Wundef -Wformat=2 -Wstrict-prototypes
  -Wmissing-prototypes`, exempting the vendored FatFs. Fix everything, then turn on `-Werror`.
- **Stack and size:** add `-fstack-usage`, and try `-ffunction-sections -fdata-sections
  -Wl,--gc-sections`, comparing `just size` before and after.
- **Editor config:** add `.editorconfig` and `.gitattributes`.
- **Formatting:** run a pinned clang-format through `uvx` in `just lint` and CI, and add a
  short formatting note to CLAUDE.md. There is no Claude Code hook.
- **clangd:** add `.clangd` with `--target=arm-none-eabi`, built on
  `just compile-commands`.
- **Host tests:** build pure code (parsers, sorting, cheat decoders) for the host with
  ASan/UBSan, reusing `torii_test.h`.
- **Advisory analysis:** run cppcheck, clang-tidy (`bugprone-*`, `clang-analyzer-*`) and
  GCC 10's `-fanalyzer` in report-only mode, with a baseline.

### Source cleanup

1. Port the Simple DE and DS Style fixes we want while their code still lines up with ours.
2. Remove the Chinese strings, the HZK12 font and FatFs's CP936 tables (about 87 KB each).
3. Convert to UTF-8 with LF line endings, and run clang-format once in its own commit,
   listed in `.git-blame-ignore-revs`.

Then update CLAUDE.md: the GBK, CRLF and `zh_*` rules go.

### On-cart debug mode

A mode you launch on the real cart that:

- runs the unit tests;
- probes the hardware: RTC, PSRAM, NOR reads, and a save-chip test that doesn't touch the
  user's saves;
- measures real timings: NOR wait states (the kernel never sets `WAITCNT`), glyph
  rendering, LZ77 decompression and SD throughput;
- walks you through the manual checklist with pass/fail buttons.

It writes a report to SD that we bring back to the repo. When the real cart disagrees with
the emulator, the fix goes into the cart model.

## v0.3 Renderer

The menu is currently mode 3 (`SetMode(MODE_3 | BG2_ENABLE)`), drawn pixel by pixel in C.

### Engine

Torii gets its own mode 0 engine in C. It uses libtonc where libtonc fits, and our own code
wherever that measures faster or safer. Its design comes from pokeemerald, Butano and Tonc:

- **Windows:** a window is a rectangle of tiles on one BG, with its own tiles, a palette bank
  and a RAM buffer.
- **Text:** a proportional font drawn into the window tiles in palette-index colours, so
  changing the theme is a palette change, with no redraw. The glyph blitter is ARM code in
  IWRAM.
- **VBlank commit:** shadow copies of the hardware state, and one queue that copies changes
  during VBlank, capped by bytes and by VCOUNT. Palettes keep unfaded and faded buffers for
  fades.
- **Lists:** scroll through the wrapping 32x32 map and draw only the newly exposed row.
- **Cover art:** 8bpp sprites on the separate sprite palette, LZ77-compressed and
  decompressed by BIOS SWI 12h straight into VRAM.
- **Layouts:** described as data, not code.

### Budget

The budget each layout is checked against comes from GBATEK and Tonc:

- **VRAM:** 64 KB for BG maps and tiles, 32 KB for sprite tiles. One layout fits exactly:
  4bpp full-screen text (19.2 KB), a 4bpp wallpaper (19.2 KB), 8bpp 120x80 art (9.6 KB),
  9.3 KB of panels and 4 maps (8 KB). An 8bpp full-screen wallpaper plus full-screen text plus
  the maps is 256 B over.
- **Colours:** 255 BG colours shared by every BG, and another 255 for sprites.
- **Covers in sprite VRAM:** about 5 at 80x80 in 8bpp, or 10 in 4bpp.
- **Effects:** one blend mode and one coefficient pair per frame, no sprite-to-sprite alpha,
  one mosaic size, and 2 rectangular windows plus the OBJ window.
- **Text speed:** about 631 cycles per glyph in ARM from IWRAM, against 2044 in Thumb from
  ROM. A 40-glyph row is 30% of VBlank in ARM and 98% in Thumb.
- **Free memory now:** about 345 KB of ROM, 5.7 KB of IWRAM and 400 B of EWRAM.
- **Mode switching:** switching mode halfway down the screen is undocumented, so we don't
  rely on it.

### Fonts and themes

- **Fonts:** two proportional pixel fonts, body text on a 12 px line and headings at 16 px.
  They are 1bpp glyphs with a width table, covering Latin-1 and common punctuation, made from
  Cozette (MIT) or Spleen (BSD-2) and credited in About. A repo tool converts BDF or PNG
  fonts.
- **Theme format:** a theme is `themes/<name>/theme.ini` plus LZ77 assets, which a repo tool
  converts from PNG. Its keys override compiled-in defaults one by one: palette, fonts,
  spacing, view layouts, icons. A missing key or asset falls back to the default theme.
- **Built-in themes:** a light theme, a dark theme, and accent colours as palette swaps.
- **Budget check:** the kernel checks each layout against the budget and rejects it with a
  readable error. `just theme-check` validates a theme and previews it in the emulator.
- **Existing screens:** ported to the engine.

## v0.4 Browsing

- **Start screen:** SD, NOR, Settings, last played and favourites.
- **Favourites:** no cap.
- **Recents:** shown as a virtual folder.
- **Boot to:** the screen of your choice, and the SD position is remembered.
- **View modes:** list, list plus art, grid and carousel, as theme layouts.
- **Names:** system files hidden, and names cleaned of extensions and bracket tags.
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
   asks before writing.
5. **Write failures:** a failed write shows a clear error and keeps everything.
6. **Tests:** the emulated cart cuts power at every step, and the test checks that a good
   save always survives.

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
