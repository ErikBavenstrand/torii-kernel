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
- **v0.8.2–v0.8.4:** a screenshot named by when it was taken; full 32 MB ROMs with the in-game code and cheats in their padding; Game saves past 256 games; the pages' states in one EWRAM block; Info's six reading pages (Controls, In game, Saves, Cheats, Questions, About).
- **v0.9 Languages and firmware (v0.9.0, 2026-10-28):**
  - **Strings:** every UI string has an ID from `lang/en.ini` (`S(STR_...)`, `{1}` placeholders, `.one`/`.other` forms).
  - **Packs:** `.lng` packs from `/torii/lang` load into 64 KB of PSRAM, with English in ROM as the fallback. Settings' Language row switches live.
  - **Swedish:** `lang/sv.ini`, with its own date and number formats. The release attaches `torii-lang.zip`, which holds the bare `.lng` files.
  - **Firmware updater:** writes only to an allowlisted LX16 cart on older firmware, after a power-cut warning and an L+R+A hold. LX9 and unknown boards are never written, and About shows the raw version word.
  - **Design notes:** the decisions are in git history up to 3edfc929 (PSRAM split: 64 KB for the active pack, 320 KB held for a script font; page 1 untouched).
- **v0.9.1–v0.9.6 Cheat files:** cheat files up to 4 MB; each cheat's guide and activation (always, on launch, held or pressed buttons); the in-game Cheats list shows the picks, and SELECT runs one once; AnyGBA's raw lines and their keypad; cheats that write the same places marked; a PSRAM patch write across an 8 MB boundary fixed; packs name the Torii they were built for.
- **v0.9.7–v0.9.10 Screenshots and the cheat search:** screenshots in a game's popup and `.bmp` files on the SD card page, moved with a renamed game; the in-game cheat search (same, changed, up, down, equals, Up/Down by, More/Less than, From A to B, signed, BCD, a one-level pointer scan, Undo), its finds filed as cheats on Back to Torii; R toggles a favourite; Play « default » names its way on the games pages.
- **v0.9.11–v0.9.15 The cheat screen:** cheat folders (`cheatN_folder`) and choice groups; ROM patches free in game, 2,048 a launch; one flat list with scroll bars; any cheat's code editable on the cart; launches decode only the picks; the game popup trimmed (Hide box art, Load state and starting straight into a state removed); the in-game menu no longer lets a bright game show through.
- **v0.9.16 Torii code and golden cheats (2026-11-02):** Torii code, a superset of VBA's lines with tests, blocks, buttons and bit operations (`docs/torii-code.md`); the menu reads the cart at WS0 3/1; a launch keeps ROM patches as runs in 48 KB of cart SRAM (about 23,000 patches, 4,096 a cheat). Pokémon Emerald's golden set (`tools/golden/emerald.py`, curation in `docs/research/cheats-emerald-curation.md`): 489 cheats, all Torii code, in Menu, Money, Pokémon, Battle, Wild, Overworld and Safari & Contests; an L+R+B menu (Heal party, PC, Shop, Warp, Gift Pokémon, Trade evolution, Legendaries again, Event tickets, Unlock); every folder reviewed against pokeemerald and on the emulated cart, with the bugs found fixed.
- **v0.9.17 Fixes (2026-11-02):** In-game menu Off reaches NOR games (the overlay gives the game back); a 32 MB launch that left its cheats out keeps its `.pat` and isn't scanned again; Boot mode is set in Game info only (a game list's Play names the way, no stepper) and the SD page's Play names default's way; `tests/saves.sh rom-read-error`; the flaky `cheats.sh` step fixed (a SIGPIPE); Emerald's Free Day Care, Any Pokémon in the Frontier (Mew) and Forget HMs' summary-screen swap run on the emulated cart.
- **v0.9.18 Fonts, tests and tooling (2026-11-02):** Ç ç ¡ ¿ µ keep their tail at 12 px, and font bitmaps take 1 byte a row for glyphs up to 8 px (6.8 KB to 3.7 KB of ROM); `tests/saves.sh rom-read-error` also fails a read at a ROM's third block; the `lang-live` golden (a pack switched in Settings reaches home and its pages, frame for frame as booting in Swedish); Swedish "Mellanr.", "Kopiera ny plats" and "1 snabbspar."; CI's static analysis gates on its baseline; `just check` plays only the games that ran a changed function (the `NO_COMPAT` list is gone); the debug frame summary counts a ninth name under "other"; `OMEGA_FPGA_VERSION` is validated; `font_preview.py --offline`; Forget HMs' level-up path run in battle on the emulated cart (a Mew at Lv 19 with Cut learned Mega Punch over Cut with the cheat, and "HM moves can't be forgotten now." without it).
- **v0.9.19 Kerning and script fonts (2026-11-02):** kerning (13 left glyphs, 224 and 174 pairs in the body and heading fonts, picked by their ink: a variant of the left glyph a pixel narrower, one rule in `text_layout`, `text_width`, the labels, rename's wrapping, the copy's percentage and the in-game menu's lines); a language pack's script font (`font` in `[meta]`, `source/engine/script.h`; glyph indices are 16-bit; the pseudo-locale en-XA has numbered boxes for Greek, Cyrillic and kana, golden `lang-script`); frame figures leave out the log flush and the timing summary no longer pushes older lines out; the Swedish terms reviewed; `tests/cheats.sh` passed 5 runs in a row after the SIGPIPE fix.
- **v0.9.20 Licences (2026-11-02):** `THIRD_PARTY_NOTICES.md` (each part's copyright and licence) and `licenses/`, attached to each release as `torii-licenses.zip`; the release binary is under the GNU GPL 3 or later (it contains minilzo); Info's Credits names the kernel base, FatFs, libgba and minilzo; the roadmap's baseline, slot and power-off items closed; Emerald's sleep confirmed on the real cart.
- **v0.9.21 Analysis and slots (2026-11-02):** the static-analysis baseline from 97 findings to none (fixed in the code, or a false alarm marked beside it with its reason); Settings, Saves, Ask for a slot (off plays the active slot; SELECT with A still asks); a test for it.
- **Tracker items (2026-11-02):** `just gb-accuracy` runs Blargg's tests and the acid2 ROMs through Jagoomba (15 of 54 pass, all `cpu_instrs`; the rest is Jagoomba's accuracy, not fixed), `tests/gb_mode.sh` checks GBC/SGB/DMG mode and the default palette (jagoombacolor#42, #34); PocketNES#14 multiboot closed as nothing to do.
- **v0.9.22 Extended Latin (2026-11-12):** the kernel draws file names beyond Latin-1 itself, with no language pack: Galmuri9's and Galmuri11's extended Latin (Polish, Czech, Turkish, Romanian, Vietnamese...), Greek, Cyrillic, punctuation and symbols, 1,675 glyphs in 33 KB of ROM (`font_extra.c`; headings get Galmuri11's made bold), in every screen and a launch's dialogs; the in-game menu folds them to Latin-1. Translations stay English and Swedish (a Korean pack was drafted and dropped: the owner wants none for now).
- **v0.9.23 Cheat search's 1.0 review (2026-11-12):** pointer finds picked only after a Check, which marks rather than drops (the list kept when none pass, the list before the Check shown again, Find asks before it replaces one); the interpreter writes through a pointer only into EWRAM and IWRAM; Undo puts the snapshot's values back; the IWRAM a pass borrows and the EWRAM under the search's code read back from cart SRAM first; the hook's words out of the searched run (a stack top in EWRAM); New search and Save state ask first; a found code filed once, the boot says what it filed or why it couldn't, and records the card refused wait with their game; the numbers converted with Negative numbers and BCD, From A to B in order; All off stops the found cheats on now; the menu's row counts the candidates; `tests/search_more.sh` (Undo, 16 finds at 32 bits, the live block's three, Sleep and Reset mid-search, cart SRAM and card faults, a stack in EWRAM, a pointer to VRAM, a cut mid-pass). The overlay's extension is in two parts in cart SRAM page `0xF0`, so the menu still opens in 4 frames.
- **Tests (2026-11-13):** every test pipeline ends in `drain_grep` or `drain_head` (`tests/pipes.sh`), never `grep -q` or `head`, whose early exit could SIGPIPE the writer under `pipefail` and fail a test now and then (`search_more midgame`, 1 run in 24; 72 of 72 after); `just lint` refuses the bare forms (`tools/pipe_check.py`).

- **Unreleased: sound after the in-game menu (38020788):** games on Nintendo's MP2000 (m4a) engine got the wrong samples for up to `pcmDmaPeriod` - 1 frames after every Resume, Save state and Load state (all 49 m4a games in `roms/`: the stub turned the sound DMAs on at the buffer's start, mid-period); the stub now leaves them off for the engine's next re-arm (`m4a_hold`). `tests/m4a_sound.py` measures it from mGBA's DMA and FIFO state (`mgba-shot -a`), on a homebrew engine in `just test` and in every game's state trips in `just compat`.
## Open work, before 1.0

**Everything below is not done.** When an item ships, move it to [Done](#done) (one line under its release) and delete it here; an item partly done keeps only what is left. Swept on 2026-10-28 from the code's TODOs, `docs/design`, `docs/research`, the release checklist, `tools/analyze-baseline.txt` and hand-over notes; checked again against git history on 2026-11-02, after v0.9.16, and on 2026-11-13, after v0.9.23. The groups were first planned as patches v0.9.1–v0.9.5, but the releases went to cheats instead, so they are themes now, not version numbers.

1.0 is tagged when every item below is done or deferred and a full pass of `docs/release-checklist.md` on the real cart finds nothing new. Items marked **(1.0?)** were planned as 1.0 projects; the owner decides whether each one must ship before 1.0 or can come after it.

### Fixes

Code fixes that the emulator can test, plus the leftovers from v0.9.

- None open.

### In game

- **Cheat search (shipped in v0.9.7–v0.9.10; [design](../design/v0.9/cheat-search.md)):** the 1.0 review's fixes are done (Done); what stays is a cart check, and the rest is in Known limits.
  - **Cart check:** a pointer kept in Pokémon Emerald (money through gSaveBlock1Ptr) across a door; the menu's opening and the search pages on the real cart with the two-part extension in cart SRAM page `0xF0`.

### Emulators and cheats

- **(1.0?) Hi-colour GBC:** these games lose mid-frame palette writes (jagoombacolor#20, #53). A per-line palette path in Jagoomba needs about 18 KB and its last DMA channel (`docs/research/gbc-hicolor.md`).
- **(1.0?) GB, GBC and NES cheats** (owner, 2026-10-25): libretro's GameShark and Game Genie files, with the codes passed to Jagoomba and PocketNES. **Built but not released** (owner, 2026-11-02: the quality can't be guaranteed yet): Game Genie as ROM patches, GameShark `01` and `90`-`97` and NES raw pokes as per-frame writes, typing codes on the cart; emulator-tested, with 84% of GB, 87% of GBC and 99.5% of NES cheats in libretro's files running, and a first real-cart pass (Pokémon Red's HP poke, Crystal's Game Genie). The work stays unpublished until a fuller real-cart check (Zelda II and other NES codes, a banked game, a save left clean, speed and sound with many pokes) passes and the owner says to ship it.
- **(maybe) Live values in game** (owner, 2026-10-30: less useful than the rest): each cheat in the in-game list shows the value now at the address it writes ("Money 3,421"), with digits the kernel draws for the overlay. Encrypted values (Pokémon's money) would read as nonsense.
- From the trackers (checked 2026-11-02, `docs/research/gba-emulators.md`): jagoombacolor#43 stays open until someone has the issue's ROM (Super Connard, patched): Torii's mode logic is tested and right for `$143 = $80`/`$C0`. #34's remaining part is a taste call: a global default palette for games the GBC table doesn't know (grey today).

### Known limits (accepted, no work planned)

- **Cheat search:**
  - It borrows the save-state buffer (no other cart SRAM has 288 KB), so a state not yet on the card blocks it and saving a state ends it (Save state asks first).
  - A game whose stack top is the BIOS's default (`0x03007F00`) gets its stack 32 bytes lower for the live block, others none (Street Fighter Alpha 3, top `0x03007EC0`, crashed with it): there "on now" is "set now", set once and held from the next launch. Emerald, Mario Kart, GTA, Metroid and F-Zero log stack literal 0 and have no live block.
  - One Undo; when a pass writes over more snapshot values than its 28 KB of Undo room holds, the filters against the last scan wait, after an Undo, for one against a number.
  - Pointer chains (one level only); "on now" through a pointer (set once, and only after a Check); naming a found cheat in game (no font or keyboard: rename it on the Cheats page); a memory view around a candidate; VRAM, palette and OAM searches.
  - A found code is looked for in a larger cheat file's last window only.
  - The Cheat search row is on the Cheats page when the game has a cheat list (the menu has room for nine rows); the menu's lines past the seventh draw a frame later, with the footer.
- **In-game menu:**
  - The game's name shows above the rows only when they leave room. That's by design: the game is on screen behind the menu.
  - A 32 MB ROM with no padding gets the in-game code over its last bytes, as the official kernel did.
  - The screenshot's clock read can't see a game's write-only clock command when the port is unreadable.
- **States:**
  - After Resume, Save state and Load state, a game on the m4a engine is silent until the engine re-arms its sound DMAs: up to `pcmDmaPeriod` - 1 frames (6 in Pokémon's, 11 in Wolfenstein 3D's). Starting them where the engine has them needs the PCM buffer's address, which only the write-only DMA source holds; SoundInfo's `pcmBuffer` is it in most games but not in Camelot's (Golden Sun: The Lost Age, Mario Golf, Mario Power Tennis). Other engines (Metroid's, Mario & Luigi's, Nintendo EAD's in Super Mario Advance 3 and 4) still get their sound DMAs back at their source's start, as before.
  - Write-only display registers can't be put back in games that keep no RAM copy of them; 44 compat games stay blank until the game redraws.
  - The exact-ROM hook patches cover only the EU builds of GTA Advance, Super Monkey Ball Jr., LEGO Star Wars II and Tomb Raider: Legend; their other regions get no in-game menu (`docs/research/hook-coverage.md`).
  - Sonic Advance and Moto Racer replace the IRQ hook, and HP Quidditch blocks the menu. These are compat's three known failures.
- **Screenshots:** without the game's RAM copy of its registers, a scrolled tile screen sits shifted. The BG matrices, BLDY, MOSAIC and window bounds can't be read back, so fades and Mode 7 roads come out wrong. A shot taken without the cart's clock is named by the filing's time.
- **Static analysis:** CI fails on any finding; `tools/analyze-baseline.txt` is empty. A finding that is a false alarm gets an inline `cppcheck-suppress` or `NOLINT` with its reason beside the code.
- **Hardware:**
  - The session record in cart RAM page `0xF0` doesn't survive a power cycle.
  - EZ-Flash's loader installs only the first 3 MB of `ezkernelnew.bin` (`tools/omega_image.py` enforces it).
  - `Save_savefile`'s second half runs frames with SRAM page 0x10 selected, and `FRAM_save_op` runs frames with a file open. Both are safe today and commented.

## After 1.0

Taken off the 1.0 list (owner, 2026-11-11).

### Licences

- **Ask Jaga (jagoombacolor) and Dwedit (Goomba Color, PocketNES) to put a licence in their repositories**, or to say in writing that Torii may build and release their code with our patches. Neither repository has one, and the release's binary includes both (`THIRD_PARTY_NOTICES.md`, "Open points"). Nothing else in the licence audit is open.

### Research

- **Hardware probes** (`docs/research/omega-de-hardware.md`):
  - Is an SD write right after the FPGA's ROM copy safe?
  - What does the real BIOS do on an undefined instruction? The crash handler can't catch a crash that turns IRQs off or loops inside the kernel.
  - Where does a DMA into PSRAM from cart space land?
  - WAITCNT `0x4018` for control-register writes, SD, NOR and the RTC.
  - HBlank DMA into palette RAM at the theme wipe's edge, and mid-frame mode switching.
  - The open register questions: the `0x09E00000` status values, `0x096C0000` auto-save, Mode B's slot, the frames at minor `0x17`, what `0x09700000` changes in the ROM map, and how the unlock sequence is checked.
  - A blue EZODE-B's raw version word and NOR IDs, if an owner of one can read them.
- **Unverified:** "chip select high" as mid-read assumes the RTC port reads back its output pins. That holds in mGBA but hasn't been checked on the cart.

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
