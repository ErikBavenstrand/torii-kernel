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

Each release's details are in `CHANGELOG.md`; the planning, ticket reports and reviews behind them are in git history (the roadmap's own sections for v0.5–v0.8 are in it up to aa95173a). Design references: [the tile engine](../design/engine.md), [v0.4's decisions](../design/v0.4/decisions.md), [the in-game menu](../design/v0.8/ingame-menu.md), [the hardware](../research/omega-de-hardware.md).

- **v0.2 Foundations:** the inherited bugs fixed with tests (save types, the cheat cap, clock and rumble as separate settings, GB/GBC/NES without the clean-boot menu, sorting, Recently Played, clean boot); strict warnings with `-Werror`, clang-format, clangd, host tests under ASan/UBSan, advisory analysis and pinned tooling; the Chinese strings, HZK12 and CP936 removed and the source made UTF-8; SD logging (`tlog`, launch records, crash detection and dumps, `just logs`); the on-cart Diagnostics with its checklist; the emulated cart and `just compat`, with an in-game menu check on every game.
- **v0.2.1–v0.2.3 Real-cart fixes:** from the first two cart tests: log files, the cart model, reproducible builds, save logging, Diagnostics, WAITCNT `0x4018` in the menu, the EEPROM probe, rumble, the game NOR list's safety (two copies, completion marks, rebuild). v0.2.2 bricked the test cart by writing the boot loader; v0.2.3 confines kernel-NOR writes to `Save_info`'s window, the emulator stops any write to the loader, and `tests/loader_safety.sh` runs every writer. **v0.2.1 and v0.2.2 must not be used** (`docs/recovery.md`).
- **v0.3 Renderer, v0.3.1 Themes, v0.3.2 Fixes:** the mode 0 tile engine replaced the official mode 3 menu (a 60 fps frame loop, jobs, an IWRAM glyph blitter, views and widgets, springs and tweens, covers in PSRAM, golden screenshots and a cycle budget per frame); proportional Galmuri fonts, UTF-8 names of any length; SD themes (dropped again in v0.4); then about forty fixes from emulator validation and two cart tests.
- **v0.4 Browsing:** a carousel home with a page per item (Recent, Favourites, each console, SD card, NOR, Settings, Info); the "Paper clean" look in built-in light and dark themes; the library index of the whole card; favourites and recents on SD; clean, custom and database names; hidden system files; Settings on one page with a time editor; rename with an on-screen keyboard; the opening zoom and theme wipe; all 18 engine follow-ups from v0.3. Cart-tested (2026-10) with no findings.
- **v0.4.1 Art:** box art pre-converted on the PC with a cover cache, the No-Intro names database (`just names-update`), `just art-add` and `just art-scrape`.
- **Cart-test follow-ups (C1–C5, D1–D2, before v0.4.0):** L/R unbound in game lists so L+A hard-resets; a floppy save mark; the GBA boot mode setting with a per-game override; Settings saved on each confirm; Start on faces a carousel item; romhacks keep their own names and art (CRC only); hotkeys renamed and the direct save/load patch removed; one drawn placeholder box per console.
- **v0.4.2 Names on the cart:** new games get their CRC, No-Intro name and box art in the background with no PC step; the full art pack `/torii/art.pak` (`just art-pack`); crc.txt dates kept across a time-zone skew; the E1–E5 fixes (two settings copies, card read errors, rename cuts, the hotkey and cheat-parser fixes, Favourites and crc.txt speed, overlay clipping, dead code).
- **v0.4.3 Before v0.5:** the background CRCs (a launch keeps a big game's partial CRC; new games read smallest first; names put in the index in place; the cursor follows its game), a new index redraws only the rows that changed, the rename's card walk shows a progress dialog, `art_scrape` follows libretro-thumbnails links, and the measured pool headroom. Accepted limit: a same-size file swapped in with an older date keeps its CRC (delete `crc.txt`). Cart-checked 2026-10-22.
- **v0.5 Saves (v0.5.0–v0.5.3):** everything under `/torii/` (saves only in `/torii/saves/<rom path>/`, with `game.ini`; no `SAVER/` import); the save copy written, verified and swapped in power-cut safe, with every step swept by `cutpoint`; named slots with a picker, a per-game Saves page and Settings > Saves (orphans, Game saves, Clean up, trash kept 30 days); backups kept per slot (Settings' Backups kept); a save type that shrinks keeps the old save as a kept backup. Then speed (a FAT free-cluster hint, a sector cache, fewer settle delays, the pending-save record programmed in place), the kernel's own printf, and two UX reviews (`docs/design/v0.5/`).
- **v0.6 Emulators (v0.6.0–v0.6.3):** Jagoomba Color and PocketNES built from pinned upstream source with Omega DE rumble and clock, reproducibly; GB palettes kept per game; 32 KB GB saves written through and saved a second after the game's last write; save slots for GB, GBC and NES; NES CRCs without the iNES header; LOAD with no state shows a message. Torii plays GBA, GB, GBC and NES only: no plugins (owner, 2026-10-25).
- **v0.7 Cheats (v0.7.0–v0.7.1):** GameShark, Action Replay and CodeBreaker decoders (ported from mGBA) and a per-frame interpreter for every code type; one libretro-format file per game in `/torii/cheats/<rom path>.cht` with power-cut-safe picks; the cheat screen with a code keypad and Details; `just cheats` from GameHacking.org; device-accurate rules checked against libretro's 7,017 cheats, a type matrix and a fuzzer. Also Jagoomba's VSync and PocketNES's region kept, and `just compat` running one GB/GBC game per mapper.
- **v0.8 In-game menu:** a sprite overlay from borrowed EWRAM that gives the game back byte for byte (Resume, Save state, Load state, Cheats, Screenshot, Reset game, Sleep, Back to Torii); eight state slots per save slot, filed from cart SRAM to `/torii/states` on Back to Torii's boot; per-cheat toggles in game; screenshots rendered to BMP at the next boot by a software PPU; Settings' In-game menu (Auto, Every game, Off). States and screenshots stay in unbattered cart SRAM until Back to Torii (owner, 2026-10-27); SD access in game mode wedges the card.
- **v0.8.1 Save states across scenes:** LOAD puts back the display registers from the game's own RAM copy, holds the backdrop until the game's VBlank has run, and restores IRQ flags, Supervisor-mode registers, the IRQ vector, timer reloads, WAITCNT and the sound FIFO DMA; cheats run on its first frame; a launch into a state loads it at the game's entry; a state from another game or a newer Torii is refused with a question. `tests/state_trips.sh` and the compat "state trips" column.

## Before v0.9

Everything still open from v0.4.3–v0.8.1, in one place. None of it blocks v0.9's code; the cart checks block the next release tag (`docs/release-checklist.md`).

### Cart checks

- **v0.8 and v0.8.1 on the real cart (never run):** the in-game menu opening in Pokémon Emerald (it went black before v0.8.0's stub fixes; if it stays shut while the game goes on, the stub refused, and its record survives only a soft reset); a state saved, filed on Back to Torii, and loaded in the same room, in another building and from the title screen; a screenshot opened on a PC, and one cut by a power-off; cheats toggled in game; In-game menu Off and custom menu keys; the in-game probe's verdict (`tests/roms/ingame`, Clean boot at Flash 128K, then A: cart SRAM pages `0x20`-`0xEF` in game mode).
- **Untried since v0.2.2:** DEBUG, GB/GBC, NES, rumble with a game, the EEPROM probe's verdict, the clock against real time.
- **Unmeasured:** the ROM copy rate (`PROGRESS_COPY_RATE`); the walk of a full card; SD folder open time (0.2 s per 300 files emulated) and the 251k-cycle frame on a folder of 1,024+ subfolders; covers from an art folder of thousands of files; Identify (about 1.6 s per MB, estimated) and crc.txt's write steps; About's first free-space read on exFAT; `CHIP_ERASE_FRAMES` from a timed Format all.
- **Hardware unknowns:** whether an SD write right after the FPGA's ROM copy is safe; what the real BIOS does on an undefined instruction; where a DMA into PSRAM from cart space lands (`docs/research/omega-de-hardware.md`).

### Known limits

- **In-game menu:** a screenshot's time is the filing's, not the moment it was taken (the clock in game mode waits on the probe); one screenshot a trip back to Torii (a second replaces the first); a menu with Save states and cheats has no room for the game's name; full 32 MB ROMs keep the old trim.
- **States:** write-only display registers can't be put back in games that keep no RAM copy of them (44 compat games stay blank until the game redraws); the sound DMA's position drifts after a load; Sonic Advance and Moto Racer replace the IRQ hook, and HP Quidditch blocks the menu.
- **Screenshots:** write-only registers (scroll, BG matrices, BLDY, window bounds) can't be dumped, so scrolled tile screens may sit shifted and fades and Mode 7 roads come out wrong.
- **Saves:** Game saves past 256 games with saves aren't listed (logged).
- **Emulators:** hi-colour GBC games lose mid-frame palette writes (jagoombacolor#20, #53; `docs/research/gbc-hicolor.md`). Left from the trackers: jagoombacolor#42 accuracy tests, #43 a GBC-compatible ROM run as DMG, #34 the default palette; PocketNES#14 multiboot.

### Decisions for v0.9

- **Memory:** EWRAM has 3,756 bytes free in the release kernel and 2,680 in the test kernel (2026-10-27), so v0.9's tables can't go there. The screens' EWRAM states (`games` 3.7 KB, `sd` 3.6 KB, `info` 2.7 KB, `nor` 1.4 KB, Diagnostics' `scr` 1.6 KB) could share a union for 5 to 8 KB if needed. ROM has 217 KB free, IWRAM 10 KB.
- **PSRAM map:** 384 KB is left for the language tables (`source/engine/psram.h`); the owner was unsure about fixing the map.
- **In-game strings:** the menu's strings are drawn by the kernel at launch (`lang.c`'s `gl_ingame`), so a language pack has to reach them there.
- **Licences:** No-Intro's terms for a derived name table (built on the user's machine, never bundled); libretro-thumbnails has no licence grant (fetched locally, never committed); the boxes' trade dress.

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

- **In-game states and screenshots that survive a power-off** (research, owner, 2026-10-27): v0.8 files them only on Back to Torii, as SD access in game mode wedges the card. The FPGA writes cart SRAM's state buffer back to a file on its own (the `/RTS` path, `docs/research/omega-de-hardware.md`); pointing that write-back at a per-game file could keep a state saved in game through a power-off.
- **One place for how a game boots** (owner, 2026-10-27): the game popup's play mode (Play « addons ») and Game info's Boot mode set the same thing in two places, which is confusing. Keep only one; it also decides whether picked cheats run, so the cheat screen's "needs Boot with addon" note should point to it.
- **GB, GBC and NES cheats** (owner, 2026-10-25, out of v0.7): libretro has cheat files for them (GameShark, Game Genie), but the codes would have to reach Jagoomba and PocketNES (PocketNES has its own cheat finder).
- **Logs:**
  - Frame-timing summaries carry the log write's time, not the scroll's.
  - They are DEBUG only.
  - Their table holds 8 names.
  - On a nearly full buffer they can push out older lines (`source/engine/cycles.c`, `tlog_flush`).
- **Crash dumps** name stale stack words that point into read-only data (`t_model+0x2628`) (`source/cartlog.c`, `tools/logs.py`).
- **Diagnostics wording:** "3 seconds passed in about 2.2".
- **Lost at power-off:** the session record in cart RAM page `0xF0` doesn't survive a power cycle (hardware). Keep it somewhere battery-backed if wanted.
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
