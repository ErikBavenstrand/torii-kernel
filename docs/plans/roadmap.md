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
- **v0.8.1 Save states across scenes:** LOAD puts back the display registers from the game's own RAM copy, holds the backdrop until the game's VBlank has run, and restores IRQ flags, Supervisor-mode registers, the IRQ vector, timer reloads, WAITCNT and the sound FIFO DMA; cheats run on its first frame; a launch into a state loads it at the game's entry; a state from another game or a newer Torii is refused with a question. `tests/state_trips.sh` and the compat "state trips" column. Cart-tested 2026-10-27 (owner): the menu opens in Pokémon Emerald and Metroid, states save and load across scenes, file on Back to Torii and launch from the popup, and screenshots file and open on a PC, and cheats toggle in game; the card's log has no errors.

## Before v0.9

Everything still open from v0.4.3–v0.8.1, triaged 2026-10-27; what code could fix is fixed (CHANGELOG's Unreleased). None of the rest blocks v0.9's code; the cart checks block the next release tag, with their steps in `docs/release-checklist.md`.

### Cart checks (the owner, on the real cart)

- **v0.8 still to check:** In-game menu Off and custom menu keys; the in-game probe's verdict (`tests/roms/ingame`, Clean boot at Flash 128K, then A: cart SRAM pages `0x20`-`0xEF` in game mode).
- **New since v0.8.1:** a screenshot named by the clock as it was taken, and the game's own clock left right after (Pokémon Emerald); a 32 MB game's menu and cheats with the in-game code in its padding, and with In-game menu Every game and Save states off.
- **Untried since v0.2.2:** DEBUG, NES, the EEPROM probe's verdict, the clock against real time (GB/GBC and GB rumble passed v0.6's cart run). The cart's clock read an hour fast on 2026-10-27: most likely still on summer time (the cart has no DST); the checklist's clock step says to note it and set it.
- **Unmeasured:** the ROM copy rate (`PROGRESS_COPY_RATE`); the walk of a full card; SD folder open time (0.2 s per 300 files emulated) and the 251k-cycle frame on a folder of 1,024+ subfolders; covers from an art folder of thousands of files; Identify (about 1.6 s per MB, estimated) and crc.txt's write steps; About's first free-space read on exFAT; `CHIP_ERASE_FRAMES` from a timed Format all.
- **Hardware unknowns** (each needs a probe of its own, not a checklist step): whether an SD write right after the FPGA's ROM copy is safe; what the real BIOS does on an undefined instruction; where a DMA into PSRAM from cart space lands (`docs/research/omega-de-hardware.md`).

### Known limits (accepted)

- **In-game menu:** the game's name shows above the rows only when they leave room (with Save states and cheats they don't). By design (owner, 2026-10-27): the game is on screen behind the menu. A 32 MB ROM with no padding at all before its end still gets the in-game code over its last bytes, as the official kernel did (a 32 MB ROM fills the address space; the log says so).
- **States:** write-only display registers can't be put back in games that keep no RAM copy of them (44 compat games stay blank until the game redraws); the sound DMA's position drifts after a load (its source counter can't be read; research for 1.0: games on Nintendo's MP2000 sound engine keep the PCM buffer's position in RAM, which could re-sync DMA1 at LOAD); Sonic Advance and Moto Racer replace the IRQ hook, and HP Quidditch blocks the menu.
- **Screenshots:** the scroll comes from the game's own RAM copy of its registers where it keeps one (pokeemerald's `gGpuRegBuffer`, Nintendo's library); without one a scrolled tile screen sits shifted, and the BG matrices, BLDY, MOSAIC and window bounds stay write-only, so fades and Mode 7 roads come out wrong. A screenshot in a game without the cart's clock, or taken while the game was reading it, is named by the filing's time.
- **Emulators:** hi-colour GBC games lose mid-frame palette writes (jagoombacolor#20, #53): a per-line palette path in Jagoomba's renderer needs about 18 KB it doesn't have and its last DMA channel; a 1.0 project (`docs/research/gbc-hicolor.md`). Left from the trackers: jagoombacolor#42 accuracy tests, #43 a GBC-compatible ROM run as DMG, #34 the default palette; PocketNES#14 multiboot.

- **From the review (2026-10-28), minor:** a 32 MB launch whose extras were dropped to fit rescans the ROM every launch (the `.pat` cache's room fits only the picks); the screenshot's clock read can't see a game's write-only clock command when the port is unreadable (Pokémon-style libraries mask IRQs around clock access); "chip select high" as mid-read assumes the port reads back its output pins, true in mGBA, unverified on the cart.

### Decisions for v0.9

- **PSRAM map:** 384 KB of page 0 (7.625–8 MB, `source/engine/psram.h`) is reserved for the language tables: 64 KB for the active pack, and 320 KB held for a 1.0 script font (decided 2026-10-28, see v0.9).
- **Memory:** EWRAM has 11,388 bytes free in the release kernel and 10,312 in the test kernel since the pages' states share one block (2026-10-27); ROM has about 215 KB free, IWRAM 10 KB.
- **In-game strings:** ready: every string the overlay shows comes from `gl_ingame`, drawn by the kernel at launch, with v0.9's numbered placeholders (`lang_fill`) and one/other forms where a count shows. The kernel's own dialogs about the menu (`EN_INGAME_REFUSED*`) are printf formats like the rest of `lang.h`'s `EN_*`, for v0.9's string IDs.
- **Licences:** No-Intro's terms for a derived name table (built on the user's machine, never bundled); libretro-thumbnails has no licence grant (fetched locally, never committed); the boxes' trade dress.

## v0.9 Languages and firmware

Decided with the owner on 2026-10-28. The work is split three ways: **strings and packs**, then **Swedish and the release** once that has merged, and alongside both, **firmware safety**.

- **Languages:** English and Swedish. The owner checks Swedish.
- **Strings** (done on `claude/v09-strings-c52085`; Swedish and the release zip done on `claude/v09-swedish-fc5217`):
  - Every UI string has an ID. `lang/en.ini` is the source of truth.
  - `just lang` generates `source/lang_ids.h` and the English table from it. Both are committed and never edited by hand, and a test fails when they are stale, the same way the fonts work.
  - Code reads a string with `S(STR_...)`.
  - The `gl_*` globals, the per-screen structs and the `EN_*` printf formats all go.
  - Diagnostics' checklist and the SD logs stay in English and get no IDs, so bug reports read the same in any language. Diagnostics' title and the row that opens it are translated.
- **Language packs:**
  - **Authoring:** translators edit `lang/<code>.ini` as text.
  - **Build:** the build compiles each file into a binary pack (`.lng`, Rockbox-style), whose version header rejects stale packs. Each pack has a font field that stays empty until 1.0.
  - **Location:** packs live on the card in `/torii/lang/`. English is compiled in as the fallback. The release attaches `torii-lang.zip`.
  - **Lookup:** the active pack's header, offset table and strings sit in PSRAM: 64 KB at the start of page 0's language reserve (`source/engine/psram.h`). A pack over that size fails the build. English is read from ROM and uses none of the reserve.
  - **Formats:** each pack carries its own date and number formats in `[format]` (`date = {day} {month}` with `[month]`'s names, `date_numeric` for the saves page's `10-23`, the decimal and thousands separators). The 12/24-hour clock stays a setting, and Settings' date and File info's change date stay `2026-10-28`.
  - **Launches:** a game fills PSRAM, the pack too, before the in-game menu's texts are drawn. The launch's sections of en.ini (`[meta]`'s `launch`, 8 KB at most per pack) go to the list row cache's room first, which no list uses during a launch; the menu loads the pack again if the launch comes back.
- **Switching:** Settings' Language row lists English plus the packs on the card (up to 7, by their header's name). Picking a language with A takes effect at once: the setting is written, the pack loads, the row cache clears, and Settings comes back drawn in it, then the carousel (`tests/shots/lang-switch.opts`, on the pseudo-locale).
- **Pack to pack:** switching from one pack straight to another rewrites the pack in place in PSRAM, which the strings' review (2026-10-28) left open, as a kept text pointer could show the new pack's bytes. Settled: `tests/shots/lang-packs.opts` goes English -> en-XA -> Swedish -> English with every frame of the fade hashed; every fourth frame was looked at and is whole. It covers Settings' own reshow only: a pointer another screen keeps across a switch is untested.
- **Swedish:** `lang/sv.ini`, drafted by Claude and checked by the owner: day-first dates (`28 okt`, `28/10`), a decimal comma, a no-break space for thousands. One term each: sparfil, snabbsparning, plats, backup, fusk, favorit, skärmbild. Every screen was run in Swedish and the texts that were cut shortened; the `tests/shots/sv-*` goldens keep 19 scenarios in Swedish.
- **Release:** `just build` writes `dist/torii-lang.zip` (the packs but en-XA, as `torii/lang/<code>.lng`, sorted and dated 1980; `just repro` compares it), CI uploads it beside `ezkernelnew.bin`, and the release attaches it, with an install step in its notes.
- **From the Swedish review (2026-10-28), open:**
  - A pack's stale check hashes English's key names only: a later release that changes a text's meaning or placeholders under the same key still loads an old pack, and a `{2}` the code doesn't fill prints as is. Either hash each key's placeholders too, or rename a key whenever its placeholders change.
  - `torii-lang.zip` holds `torii/lang/<code>.lng` so it unzips onto the root, but Finder's drag offers to replace the card's `/torii`; the docs say to copy the `.lng` files. A flat zip would avoid it. It has no version marker either.
  - The letter jump («» in game lists) knows A-Z only: a game starting with Å, Ä or Ö files under "#".
  - Swedish texts shortened for width, for the owner's check: `rename.key_space` "Blank", `gsaves.keys` "Backup" (singular), `games.fact_size`/`fact_played` "Strl"/"Sist", `ingame.unsaved_both` "1 sparning, {1} bild", `gsaves.act_copy` "Kopiera".
- **Placeholders:** numbered (`{1} of {2}`) and checked against English at build time. Plurals are avoided by wording, or written as `.one`/`.other` variants.
- **Scripts:** Latin-1 only. Other scripts come in 1.0 as extra font files, in the remaining 320 KB of the reserve. PSRAM page 1 stays untouched.
- **Firmware updater:**
  - **Images:** the official images (LX16 V07, LX9 V05), unchanged.
  - **Allowlist:** an update is written only to a cart whose FPGA version word is on the allowlist (LX16: high nibble `0xA`/`0xB`) and whose firmware is older than ours. Today the kernel treats any word it doesn't recognise as LX9, so an EZODE-B or any other unknown board could be offered the wrong image and bricked. With the allowlist, any other word writes nothing and logs the raw value.
  - **LX9:** the kernel never writes LX9 firmware until a real Rev.A word has been measured. About and Diagnostics show the raw version word instead of guessing "Rev.A".
  - **Starting an update:** two screens. The first shows the versions and a warning: a power cut can leave the cart unbootable, so use fresh batteries or a plugged-in console. The second asks you to hold L+R+A for two seconds. B cancels on either screen.
  - **Tests:** `tests/fw_update.sh` covers every case, including that every word outside the allowlist writes nothing.

## After v0.9

- **Screenshots per game (owner, 2026-10-27):** a "Screenshots" entry in the game's popup, next to Saves, opening that game's screenshots from `/torii/screenshots`; a viewer built into the kernel that shows a BMP full screen and steps through the game's shots with LEFT and RIGHT. The SD card page opens a `.bmp` in the same viewer.

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
