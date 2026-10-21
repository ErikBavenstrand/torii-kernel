# v0.4 decisions

What v0.4 Browsing and v0.4.1 Art are, as built: the user's decisions from the planning interview, the design rounds and the cart test (2026-10-19 to 2026-10-21), with the research overrides and the changes the cart test asked for. The mockups and their numbers are in this folder ([README](README.md), [spec.md](spec.md)); choices the build made that the user may still want to change are in the roadmap's [v0.4.2 section](../../plans/roadmap.md#v042-before-v05). The ticket reports and the conductor's log were deleted after v0.4.1; `git log` and the commits named in `CHANGELOG.md` have them.

**A first version to tune.** Expect the look, motion and layout to change after cart use, so keep them cheap to change: shared values are fields of the built-in themes in `source/ui/ui_theme.c`, values only one screen uses sit in one `static const struct <screen>_look` at the top of its file, and drawing code has no bare numbers. A tweak is a one-line change, then `just shots-update`.

## Navigation

Nothing is kept from the official kernel's keys or screens.

- **Home is a carousel** of items, in order: Recent, Favourites, one per console the library found games for (GBA, GBC, GB, NES; later systems after NES), SD card, NOR, Settings, Info. Recent and Favourites hide while empty. The ring is a five-slot window over the item list, so it takes any number of items; with fewer than five, only that many slots are drawn and nothing shows twice at rest.
- **Boot** faces the item Settings' "Start on" picks: Last place (the default, the item last faced) or any item home shows. It never opens a page; a hidden choice falls back to Last place, then the first item. `@boot` in `recent.txt` holds only the item. There is no power-on key to launch a game; L+R+SELECT at power-on (Diagnostics) is the only power-on combination.
- **Opening** an item zooms into its page; B plays it back and faces that item. There is no tab bar: each page has a title bar (the item's 16x16 icon and name; the position or the clock on the right). In SD folders B goes up a folder first.
- **Carousel keys:** LEFT/RIGHT turn, A opens.
- **Game lists** (consoles, Recent, Favourites): UP/DOWN move, LEFT/RIGHT jump to the previous or next first letter with a letter card, A plays, START opens the game popup. L and R do nothing, so L+A is a hard reset; SELECT is kept free. No grids and no horizontal game carousels.
- **Starting a GBA game:** A, Play and Launch start with the addons Settings enables, as Settings' "GBA boot mode" says (With addons by default) or the game's own Boot mode in Game info (Default, With addons, Clean). Clean boot is its own popup entry. GB, GBC, NES and NOR games start clean. Holding L while confirming a start is a hard reset.
- **Popups and confirmations** follow one rule set everywhere (`source/ui/ui.h`, popups): a popup sits beside the selected row and never covers the title bar; a destructive action asks in a small centred confirmation with Cancel preselected.
- **In-game hotkeys** (Menu keys, Sleep keys) are set in Settings until v0.7's overlay.

## Pages

- **Consoles, Recent, Favourites:** one list card of 14 px rows. Consoles and Favourites list A–Z, Recent newest first (at most 32). Rows show the display name, a favourite star, a save mark and the size; Recent and Favourites add a console marker, Recent when each game was played (nothing while the clock is unset). "List with art" (a Settings value) shows the cover or the console's placeholder box and two facts.
- **Game popup:** Play, Favourite, Clean boot, Boot with addon, Rename, Game info, Delete (GB, GBC, NES and NOR entries offer fewer). Delete removes the ROM and keeps its save. Game info is a page: console, size, folder, played, save type, boot mode, hard reset.
- **SD card:** the card's folders as they are, raw file names, folders first, sizes, path pills. START opens a file popup (Launch, Clean boot, Write to NOR, with addon, favourites, Rename, Delete, File info). Up to 1,024 entries a folder, with a note past it.
- **NOR:** storage (used and free of 64 MB), the games, and Format all behind a confirmation. Only the latest game can be deleted.
- **Settings:** one scrolling page of section cards (Look, Clock, Games, Cart, Hotkeys, Library, System; the table is `source/settings_model.c`). A edits a value in place; the selected row's explanation shows in the hint bar; Time and Date open a field editor; hotkeys are set by pressing the combination; LED colours are swatches; the theme previews live. **Each confirmed change is written at once** (one kernel NOR sector write). LANGUAGE stays hidden until v0.9.
- **Info:** Controls, In game, Your cart, About (version, build ID, cart revision, FPGA, card size and free space, credits) and Diagnostics. Texts are `en_*` strings.
- **Rename:** an on-screen keyboard (`docs/design/v0.4/keyboard-*.png`). A game's Rename sets its display name in `/torii/names.txt`; the SD page's Rename renames the file and moves its save, save type, save states, patch cache and custom art, and updates the index, recents and favourites. A rename journal recovers a power cut at boot.

## Look

- **Paper clean:** warm paper (light) or graphite (dark), rounded cards with soft shadows, a pill selection, key chips at the bottom. No background patterns. Light and dark are built in; SD themes (`theme.ini`) were dropped. The theme switch is a 16-frame hard-edged wipe (a palette swap per line from a VCOUNT interrupt and HBlank DMA).
- **One accent per item:** GBA #4A39B5, GBC #089C94, GB #6B9C10, NES #CE2129, Recent #EF7B21, Favourites #E7529C, SD #297BDE, NOR #E79C10, Settings #637B9C, Info #DEB510, each with base, tint, ink and on values per theme in `palette.json` (`just palette` generates `source/ui/palette.c`). Row text on a pill stays neutral ink; the base colours of Recent, NOR and Info are never text.
- **Torii:** a red torii glyph (#E74A29) in the carousel's top-left corner.
- **Icons and sprites** live in `assets/ui` (`just assets`): 16x16 and 8x8 icons, the console silhouettes, ground shadows, and one drawn placeholder box per console (Nintendo-style trade dress in original pixels).
- **Carousel:** objects on a ring seen slightly from above, 36° apart; five visible, the front one lowest and largest, the others smaller and fogged toward the backdrop; no spotlight. The name and count sit below.
- **Motion:** a carousel step is 10 frames, ease-out cubic; opening is 12 frames (the object zooms and fades, the card rises, the title slides in), closing 5 + 5; a popup opens from its row in 6 frames; dialogs rise in 6 frames; the loading card shows at once.
- **Performance first:** every screen and motion at 60 fps within `tests/perf/budget.txt`. When a design can't hold it, the design changes, not the frame rate.
- **Starting a game:** the loading card shows the game's name and one bar for the whole start, which moves on an estimate from the file size while the cart copies the ROM (an IWRAM VBlank hook). Its status line names only the slow steps (Patching, Loading, Writing to NOR).

## Library and data

- **Library index:** a background job finds every game on the card by extension (`.gba`, `.gbc`, `.gb`, `.nes`) and keeps `/torii/library.idx` (two alternating files), up to 4,096 games, skipping hidden folders and folders deeper than 8 levels. Boot loads the index and shows the menu at once; a background walk hashes every folder's names, sizes and dates and rebuilds the index only when the hash or the volume serial differs (folder dates don't change on FAT, R1). Settings has Rescan library and Identify library. The format is in `source/library.h`.
- **Names**, the first that exists (`gamename_display`, `source/gamename.h`):
  1. the user's own name (Rename, or `just names-add`, in `/torii/names.txt`);
  2. the No-Intro name for the game's CRC from `/torii/names.db` (`just names-update`, built on the PC and never bundled; CRCs come from the PC tool or Identify library into `/torii/crc.txt`). CRC only: a romhack keeps its own name, never its base game's by game code;
  3. the cleaned file name (no extension or bracket tags, articles moved to the front, scene prefixes dropped; games that clean the same keep their first differing tag).

  The SD card page always shows raw file names.
- **Sort order:** case- and accent-folded, numbers by value ("Mega Man 2" before "Mega Man 10"), by the name shown, so a leading "The" counts (R3).
- **User files** (`favorites.txt`, `recent.txt`, `names.txt`, `crc.txt`) are written as two copies, `NAME.bak` then `NAME`, each ending in a CRC line, never renamed (`f_rename` can leave two names on one cluster chain after a power cut, R2). A hand-edited file without a CRC line is accepted as is. `recent.txt` is `<unix time><TAB><path>` per line, newest first, after its `@boot <item>` line; a time read while the clock is unset (before 2026-10-01) is stored as 0.
- **Hidden on the SD page and in the index:** `/torii`, `SAVER`, `RTS`, `SYSTEM`, `PATCH`, `CHEAT`, `System Volume Information`, dot files, FAT hidden and system entries, and every file the kernel can't start. Settings' "Show all files" shows everything on the SD page.
- **Box art (v0.4.1):** 80x80 covers pre-converted on the PC into the engine's 4bpp tiles (`tools/art.py`), loaded after the cursor rests. Lookup: custom art by file name (`/torii/art/custom/`, `just art-add`), then matched art by CRC (`just art-scrape` from libretro-thumbnails, fetched on the user's machine and never committed), then the console's placeholder box.
- **PSRAM page 0 while the menu runs** is mapped in `source/engine/psram.h`; a launch overwrites all of it. Only the last 384 KB is left for v0.9's language tables; the user was unsure about fixing the map, so revisit it when a need doesn't fit.

## Research overrides

The build's research (`docs/research/v0.4/`) changed these, as the conductor's calls; the roadmap lists them for the user to contest.

- **R1** No folder-date check; a background re-walk notices changes (`research-v04-library.md`).
- **R2** Two-copy safe writes instead of temporary file and rename (`research-v04-library.md`).
- **R3** Sort order as above (`research-v04-library.md`).
- **R4** The nine name-cleaning rules (`research-v04-names-art.md`).
- **R5** 14 px rows on a pixel-pitch tile canvas scrolled by BG1VOFS, with the pill as sprites under the text (`research-v04-rows.md`).
- **R6** Double-size affine sprites only for the object that zooms; ring slots are plain affine sprites (`research-v04-motion.md`).
- **R7** A hard-edged theme wipe; the theme-dependent BG colours are one contiguous run (BG palette 0–63) (`research-v04-motion.md`).
- **R8** The title bar slides during the open (`research-v04-motion.md`).
- **R9** A popup takes WIN0 and list clipping moves to WIN1; the confirmation dims toward white in light and toward black in dark (`research-v04-motion.md`).

## Contracts for code

Rules the build settled that new code must keep:

- Cached list rows outlive their slots: any per-item change (a star, the "played ago" text, a late cover) calls `ui_list_refresh`, even for a row off screen.
- Sprites only through `ui_obj_add` in a widget's update (OAM is rebuilt every frame); a list's plane holds nothing else; a list's rectangle is whole tiles and a whole number of rows (56 or 112 px for 14 px rows, 96 or 120 for 12 px).
- Close a popup before launching or leaving a screen; pages leave plane 0 to overlays. Plane 2 shares BG block 0's tiles with popups and confirmations, so a page keeps large things off plane 2; when the tiles don't fit, `ui_confirm` closes the popup first. A popup holds at most `UI_POPUP_ITEMS` (8) entries, which the SD file popup already fills.
- The theme wipe's raster split needs a commit every VBlank: blocking work calls `ui_wipe_finish` first.
- Big user files (`crc.txt`, the index) are written a step at a time through their own `FIL`: SD writes run at about 200 KB/s, so a one-call write stalls the menu for seconds.
- `.mde` byte 0 is the save type and byte 1 the boot mode (`source/GBApatch.h`); Start on's codes in settings slot 32 are permanent, and a new item takes the next free code (`source/settings_model.h`).
- Screens log `tlog_debug("screen ready: NAME")` when drawn and ship their goldens, `frame-cycles` budgets and key-driven scripts (written against `tests/nav.sh`). The UI walk (`tests/shots/data/walk.txt`) drives every page in both themes.
- Menu-only files go into `NO_COMPAT` in `tools/check.py`; anything on the launch, save, NOR or boot path stays out of it.
- Strings are `en_*` in `source/lang.c`; a change deletes the strings it orphans.
