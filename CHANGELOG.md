# Changelog

Changes to the kernel (`source/`) since EZ-Flash's official 1.06 source
(commit `12cb68b`), newest first. Tooling, tests and the emulator are in the
git log, not here.

## Unreleased

- **Added:** popups, confirmations and the letter card: a popup beside a
  list's selected row that opens from a notch at the row (6 frames, closing
  in 4) and never covers the title bar; a centred confirmation that dims the
  page (towards white in light, black in dark) with Cancel preselected and
  destructive actions in danger ink; a letter card for A-Z jumps that skip
  empty letters. Lists clip with WIN1 while an overlay moves (B11, R9;
  `753c913`, `e577d54`, `d4b9828`).
- **Changed:** lists take any row pitch from 8 to 32 px: rows lie in one
  tile canvas whose map is written once and scrolled by the BG scroll alone
  (14 px rows hold 60 fps; a held scroll's worst frame is 80,864 cycles). The
  selection is a sliding pill of sprites under the text on lists over a card,
  and a drawn pill on lists with paper; the UI builds OAM each frame. Strided
  VRAM copies and DMA fills and copies: a 14 px row clears in 2.6k cycles and
  copies from the cache in 6.6k (B9, R5; `a3200ac`, `49e4b74`, `503a4dd`,
  `206ea0b`).
- **Added:** Paper clean widgets, every size in the theme: cards, the title
  bar, the hint bar of key chips or an explanation, path pills, a storage
  bar, section labels with 8x8 icons, a thinner scroll bar, and a reading
  view for long texts (B9; `49e4b74`, `206ea0b`).
- **Changed:** light ink3 `#8C847B` and dark NES ink `#EF7B84`, for 3:1 hint
  labels and 4.5:1 danger text on floating cards (B9; `49e4b74`).
- **Added:** the library index: boot loads `/torii/library.idx` (every game
  by extension, sorted per console by the name shown, custom names first, up
  to 4,096) into PSRAM, or builds it with a progress dialog once that is
  slow; a background walk rebuilds it only when games, saves, custom names or
  the card change, and it is written alternately to `library.idx` and
  `library.bak`, so a power cut keeps the newer one. The SD list hides
  Torii's folder. FatFs gains `f_readdir_sub`, so wide folders no longer make
  the walk quadratic (B3, R1, R3; `4a8b6a7`, `d64cc77`).
- **Changed:** Recent and favourites move to `/torii/recent.txt` (time
  played, newest first, 32 games, the boot position) and
  `/torii/favorites.txt`, held in PSRAM and written as two copies each ending
  in a CRC line (`safefile.c`, for every user file); a list read with a card
  error is never written back; the v0.3 list and its NOR games are taken over
  at the first write (B5; `32ec4e1`, `8bb170f`, `86c6194`, `298f10a`,
  `1469880`).
- **Removed:** grids (`UI_GRID`): v0.4 has none (B7, T13, E4; `e2d81cd`).
- **Changed:** a palette fade works out only the banks in use (a fade frame
  drops from 58,819 to 27,877 cycles); `frame_measure` keeps several names a
  frame; `ui_list_autoscroll` emits cursor events (B7, E2, E13, E14;
  `5bb9f3c`).
- **Changed:** a list's page change fades the old page out before the new one
  fades in, instead of blanking it; its rows are drawn ahead into a new 24 KB
  row cache keyed by item, within 140k cycles a frame, so page-change frames
  peak at about 150k instead of 249k; rows scrolled back to come from the
  cache (B7, E1, E3, E12; `16a6a3d`, `58efcdd`, `941172b`, `e54976d`,
  `8aa5545`).
- **Added:** menu art as engine assets: icons, carousel objects, ground
  shadows and the placeholder box live in `assets/ui/` and convert with
  `just assets` into `source/ui/art_data.c`, read by id through `art.h`, so
  redrawing art is a PNG edit; unlogged loaders for the carousel's ring
  (B2; `3fee70f`).
- **Fixed:** a UI image refused while the VRAM queue was full left an empty
  cached block that the next push revived; it is now dropped (B2;
  `3fee70f`).
- **Added:** settings as data for the v0.4 page (`settings_model.c`): seven
  section cards of at most six rows, values staged and written to NOR only
  for a real change, the hotkey rules (no combination inside a game's own
  A+B+START+SELECT soft reset), the time and date field editor and the
  12/24-hour clock; new slots 30-33 (24-hour clock, game view, Start on, Show
  all files) (B6; `9ab2c33`, `a49ba5e`, `a6de04e`, `e88ecce`).
- **Changed:** menu colours come from `docs/design/v0.4/palette.json`,
  generated into `source/ui/palette.c` by `just palette`: built-in light and
  dark themes with an accent per carousel item, and the new look's shared
  sizes and motions in one place, `ui_theme.c` (B1; `89ab559`, `092fc22`,
  `de09c11`).
- **Removed:** SD themes: `theme.ini`, wallpapers, layout patches, ACCENT,
  `/torii/theme.txt` and the theme tools. THEME is Dark or Light; an old SD
  theme setting shows Dark (B1, E8; `89ab559`).
- **Added:** ease-out quad and smoothstep motion curves (B1; `14890cb`).
- **Changed:** the loading card shows the game's name and one bar for the
  whole start, with status only for the slow steps (Loading the game,
  Patching the game, Writing to NOR) and no percentages; the quick checks are
  logged instead. The NOR write's card is titled with the game's name too
  (B8, E16-E18; `27eff60`, `cf80986`).
- **Added:** during the cart's own ROM copy, a 124-byte IWRAM VBlank handler
  moves the bar on an estimate from the file size (`PROGRESS_COPY_RATE`);
  each launch logs the copy's real time to tune it (B8, E15; `27eff60`).
- **Added:** names and hidden files: hardware-free `gamename.c` turns file
  names into clean display names, tells colliding games apart by their
  differing tags, reads and writes `/torii/names.txt` (custom names, moved
  with a renamed file), and decides which SD entries stay hidden (B4;
  `818f4e8`, `2ea8907`, `cafc8bf`, `9124ea7`).

## v0.3.0 (2026-10-19)

Tested on a real Omega DE: the new mode 0 menu engine, SD themes, and the
v0.3.2 fixes from two cart tests.

- **Changed:** a held d-pad tops out at 30 rows a second, reached after
  1.15 s, instead of 40 after 1.75 s: 30 moves the lists a steady 8 px every
  frame, where 40 moved them in uneven 11, 11, 10 px steps (`1d28236`).
- **Fixed:** a row redrawn in place (a SETTINGS value, a Diagnostics line)
  and a scroll bar's thumb were drawn into tiles on screen and could tear
  for a frame; they now land in VBlank with the list (`0817cbb`, `c3d13fc`).

- **Fixed:** reads into the shared read buffer used only what they read: a
  short or failed read of a PATCH/ `.pat`, a SAVER/ `.mde` (a big one also
  overran the buffer), `GameID2cht.bin`, a save loaded into cart SRAM or a
  save state file used an earlier owner's bytes (`e1d6d5a`). The tests now
  fill the buffer with 0xFF between the lists' actions, so such reads fail
  there, not on a cart (`6bd0189`).
- **Changed:** a held key on THEME steps through the names at once and loads
  the SD theme it stops on when let go; it loaded every SD theme it passed,
  a few frames each, and lagged behind the key (`83fd215`).
- **Changed:** the log goes to the card as SETTINGS opens and before an SD
  theme loads, so a hang there no longer takes the lines that led to it
  (`ab9378c`).
- **Fixed:** picking an SD theme in SETTINGS after WRITE TO NOR froze the
  cart: the theme's file reader lives in the shared read buffer, which held
  the game's last bytes, and its unset finish hook was called. The reader
  now starts clean (`c11ab97`).
- **Fixed:** a Recently Played NOR game no longer on the game NOR stayed on
  the list; opening it now drops it with the error, and NOR DELETE and
  FORMAT ALL take the games they remove off the list (`5ae634f`).
- **Fixed:** BOOT WITH ADDON and WRITE TO NOR ADDON took an erased switch
  slot's 0xFFFF as its value: the log said "savestate 65535", and a NOR game
  written so made a save state file at every launch. An erased slot is off,
  as SETTINGS shows it (`d9d75ef`).
- **Fixed:** the slow boot's "Loading..." notice cut in, after a frame of
  the background, and cut out; it now fades in from black and out again.
  A new engine session's first frame no longer shows its background before
  its fade from black, at the fast boot too (`52c691e`).
- **Fixed:** Diagnostics took about 80 frames to react to A, making the log
  file first; the file is now made while step 1 shows. Each page's title
  and lines now change in the same frame (the title came about 40 frames
  early, over the old lines or a blank page), and a page's fade or scroll
  ends before a step's work stops the frames, so the save memory page
  (renamed from "save chip and cart RAM", which was cut off) shows what it
  reads and the timings page no half-scrolled row (`93c3ba4`, `aa42e9c`).
- **Fixed:** a Recently Played entry whose file is gone stayed on the list
  after "Can't open the file", to fail again; it now comes off (not on a
  card error, which may pass) (`217f418`).
- **Changed:** B on Diagnostics' first page, started from SETTINGS, goes
  back to SETTINGS page 2 with the cursor on DIAGNOSE, not to the NOR list;
  from the power-on keys it still goes on to the menu (`386e5ed`).
- **Fixed:** with no notice before the list, a big, slow card (or making
  the log file) left the boot black for seconds. "Loading..." now shows once
  the boot has been busy for 20 frames after mounting; a fast boot still
  goes straight to the list (`3d920ed`). The unused "microSD card ready"
  string is gone (`0954c7b`).
- **Fixed:** the background log open (`f38df20`) could meet a delete of the
  file it held open, which FatFs doesn't guard against: a delete now
  finishes it first. A reset or power loss part way lost the new file's
  clusters on the card; the file is now allocated whole first. LOGGING
  turned off meanwhile stops it, so the card is left alone (`9d2680d`).
- **Fixed:** SETTINGS page 2's SAVE edit opened on OK, so A, A saved AUTO
  SAVE unchanged; it opens on the value, as the other rows do, and MODE B's
  opens on the mode it has, not on RUMBLE (`46e1138`).
- **Fixed:** turning LOGGING on in SETTINGS froze the menu for over a
  second and lost the keys pressed then, while it made the 64 KB log file.
  The file is now made a sector at a time in the background, after the
  screen's drawing each frame; the log waits in RAM meanwhile (`f38df20`,
  `053bb2d`).
- **Fixed:** B on Diagnostics' first page restarted the kernel, which
  landed at the SD root; it now goes back to the menu, as its hint says
  (`c449f44`).
- **Fixed:** Diagnostics' pages cut long lines off ("boot loader CRC ...,
  present, with a..."); a long line now goes on in the next. The clock page
  says it is measuring during its 2-second wait, and the EEPROM page no
  longer names a folder of the source (`c449f44`).
- **Fixed:** the boot showed "microSD card ready / Loading..." for a few
  frames before fading to black and the list; it now goes straight to the
  list (`fdd9fe2`).
- **Fixed:** NOR DELETE erased the latest NOR game at once; it now asks
  "Delete this game from NOR?" over the game's name first, as the SD list's
  delete and FORMAT ALL do (`c211261`).
- **Changed:** messages close with A as well as B, and their hint reads
  "[A]OK". A pressed while one is still opening does nothing, so a quick
  double A can't close it unseen (`c211261`).
- **Fixed:** the NOR list showed "0M" for games under 1 MB; it shows sizes
  as the SD list does (128K, 4M) (`c211261`).
- **Fixed:** the empty NOR list was blank; it says "No games in NOR", and
  Recently Played's title reads "RECENTLY PLAYED" (`c211261`).
- **Fixed:** a file deleted from the SD list stayed on Recently Played; it
  comes off. A launch from Recently Played that doesn't start, its errors
  and B on its game menu go back to Recently Played, not the SD list
  (`c211261`).
- **Changed:** the cycle budget compiled in for Diagnostics' renderer step
  is re-recorded, with a page change in a 302-file folder (`99fdea6`).
- **Fixed:** a quick double A on a game opened its menu and the second A
  picked CLEAN BOOT. A menu or dialog still opening now ignores A and START;
  the d-pad and B act at once (`96d6515`).
- **Fixed:** LOAD SAVE's question about a save still waiting to be copied
  said "Launching loses it"; it now says "Loading the save loses it. Load
  anyway?". The save's name in it is cut at a whole character (`3d9fecf`).
- **Fixed:** a key pressed while LOAD SAVE's dialogs closed opened the NOR
  menu unasked after B on its question (`09c385c`).
- **Fixed:** coming back to the SD list from Recently Played (a launch that
  didn't start, an error, B on the game menu) put the cursor on the first
  row; it is where it was, as after B (`09c385c`).
- **Fixed:** a full card stopped the log for the rest of the boot, as a card
  failure; the log now waits in RAM and goes on once a file is deleted or at
  the next flush with room (`1355085`).
- **Fixed:** a failed folder-up showed "error?"; it shows error 0, "Can't
  open the folder" (`f51e614`).
- **Fixed:** SETTINGS wrote the kernel NOR on every OK (five erases in
  25 s on a cart). A setting still takes effect at once on OK, but it is
  kept (written to the NOR) when you leave SETTINGS, once, and not at all if
  nothing changed; turning the power off inside SETTINGS loses its changes
  (`e5d3e8b`, `f51e614`).
- **Fixed:** the lists' clock stopped under a dialog, such as FORMAT ALL's
  4-minute erase; it now keeps going (`a36ec16`).
- **Fixed:** BACK UP SAVE's progress dialog never showed when the card had
  no save file yet: it found no VRAM over the NOR menu, which now closes
  first (`ed09af3`).
- **Fixed:** Diagnostics' page went black for 2 frames as it came back
  after the unit tests; it now stays up (`3e8243e`).
- **Fixed:** a save copy the card couldn't take (a full card) filled its
  bar to "Done"; the power-on copy and BACK UP SAVE now show "Can't write
  the save", and the power-on copy waits: the next power-on offers it
  again, and a launch or LOAD SAVE first tries it again and, if the card
  still can't take it, asks before losing it (`384c622`, `f51e614`,
  `e38f84d`).
- **Fixed:** BOOT WITH ADDON said "Loading the game" while it still checked
  save states and patches (`95b5f8d`).
- **Changed:** one wrap rule for every list and menu: a held key stops at
  the end and stays there, and a fresh press at the end wraps. The menus and
  the UI demo's list used to wrap after a 15-frame rest; the theme key
  `wrap_pause` is gone (`6844884`).
- **Fixed:** L from the NOR list and B from Recently Played bring the SD list
  back with the cursor where it was, not on its first row (`691fc00`).
- **Fixed:** after a card failure the lists' idle flush no longer calls
  `tlog_flush` every idle frame (`tlog_card_failed`) (`40bf58f`).
- **Fixed:** if the list's view can't be pushed, an error comes in from black
  and the session starts over, instead of a black screen that takes no key
  (`046cfdf`).
- **Fixed:** two presses of one key while a screen goes out are two moves on
  the next screen, not one, and keys queued for the next screen are no
  longer dropped while DIM is on: they wake it (`bf7f65d`). Only the d-pad,
  L and R count twice, so a double A can't act on a screen not seen yet, and
  a second press the screen left before using is dropped rather than moving
  the screen after it (`5cb9525`, `e7d2d4f`).
- **Known issue:** a cart whose kernel NOR is a uniform-sector S29GL064A
  (NOR ID `0x220C`; Diagnostics shows it as "kernel NOR id") can't take a
  kernel update: EZ-Flash's loader stops with "Bad NOR ID" when R is held,
  and installs neither Torii nor the official kernel (`docs/recovery.md`).
- **Changed:** scrolling. A held d-pad speeds up smoothly from a row every
  4 frames to 40 rows a second, its top speed at 1.75 s held, and the list
  glides with the cursor at its speed, so the cursor never leaves the
  screen; let go, it brakes to a whole row. Held LEFT/RIGHT page every 6
  frames and scroll through instead of blanking the list (the
  fastest-scroll flicker and its 200k-cycle frames on the cart); every
  scroll frame is now under 75k cycles in the emulator. A fresh press at a
  list's end wraps to the other end, as in the menus; a held key stops
  there. A, B and START pressed during a held scroll act on the row shown
  selected. Fades cost half as much, and Diagnostics' demo scrolls
  smoothly (`c11f265`, `f73b9ff`).
- **Changed:** the whole menu runs in one engine session: switching between
  the SD list, the NOR list, Recently Played, folders, SETTINGS, help and
  the cheat screen never goes black. A screen fades to its background and
  slides away in 3 frames, the next is drawn while nothing shows and comes in
  over 8 (sideways for L/R, up and down for folders, Recently Played, cheats);
  a launch fades to black. Dialogs and menus fade in, then dim the screen,
  while sliding up over 12 frames, and close the same way backwards; the dim
  is darker. All of it is theme data (`dialog_in`, `dialog_out`,
  `tab_*`, `deeper_*`, `fade_*`, `leave_*` in theme.ini). A delete, FORMAT
  ALL or overwrite question answered no leaves the list as it was (`98955be`,
  `e707f85`, `504cbf2`, `e9bb7af`).
- **Fixed:** Diagnostics' unit tests failed on the cart (`asset_psram_round_trip`):
  a DMA from the kernel's ROM into PSRAM writes nothing on the real cart.
  `psram_write` now refuses sources in cart space; the test copies its data to
  RAM first (`b058a21`).
- **Fixed:** a scrolling list logged a `frame-cycles:` line every frame and
  overflowed the log (107 KB lost in one cart test). Measured frames go to
  mGBA's console only, and the card gets one summary per name at DEBUG when the
  log is written; the lists also write the log while no key is held, so
  a long stay in the menu or a power-off loses no lines (`b08c3b9`, `29c35d2`).
- **Fixed:** Diagnostics no longer goes black for about 5 s while its unit
  tests run: the page stays on screen (`32b6f96`).
- **Fixed:** log lines: a refused NOR write now says it wasn't written, the
  boot loader's blank title says DE.BIN has none, and the crash test's boot
  line is complete (`42db653`).
- **Fixed:** progress bars move and fill. Each step of a launch (save,
  loading, patching), WRITE TO NOR, the power-on save copy, BACK UP SAVE
  and FORMAT ALL fills its share of the bar, with the step's percentage,
  and the bar shows full ("Done") before the dialog closes. It used to sit
  at 0% everywhere but the NOR write and stop a block short. The FORMAT ALL
  bar is an estimate based on the official "about 4 mins", with the time so
  far. During a CLEAN BOOT the cart copies the game itself, so the bar holds
  until the copy ends (`a865d94`).
- **Changed:** dialog, progress and message texts are clear sentence-case
  English ("Save to SD card?", "Writing to NOR 50%", "No save file found",
  "Delete this file?", the firmware update's), with no "Sav" (`a865d94`).
- **Changed:** BREATHING and SD LED on SETTINGS page 2 are rows UP and DOWN
  stop on, edited like any other (R, G, B, OK). The settings are stored as
  before (`a865d94`).
- **Changed:** HELP shows "Torii 0.3.0 · FW N" instead of the official
  "K:1.06", and every boot logs "Torii 0.3.0, build id …"
  (`source/version.h`) (`a865d94`).

- **Changed:** `ezkernelnew.bin` is 1.79 MB instead of 2.44 MB: it no longer
  carries EZ-Flash's 16bpp menu pictures, which nothing draws since v0.3. The
  FPGA firmware moved down to fill the gap (`source/image_layout.h`); the
  firmware update writes the same bytes as before (`546fd17`).
- **Removed:** HELP's "SELECT: Thumbnail toggle" line; the lists no longer
  show thumbnails (`3250448`).
- **Fixed:** with an SD theme that has a wallpaper, the game menu no longer
  fails to open and drops the menu to the Dark theme for the session. The
  wallpaper now sits in BG VRAM that only planes 0 and 2 use, so it never
  pushes down the maps the menus' lists need room beside; a dialog that
  needs that room for its text moves it among the maps until it closes,
  without a one-frame flash of its tiles (`bc4a255`, `6956a8a`).
- **Changed:** no screen draws in mode 3 any more: the boot's card message,
  the SD theme error and the firmware update's screens are engine dialogs
  with the official texts (the update's checks and writes are unchanged),
  and the lists no longer start on a white frame. The mode 3 drawing code,
  its font and the 16bpp pictures' use are gone (`4c15d3c`).
- **Changed:** messages, questions and progress are dialogs on the v0.3 tile
  engine (`source/ui/dialog.h`): the launch's progress (with the game's name),
  WRITE TO NOR's and the PSRAM load's progress with a bar, "Save sav?", the
  NOR menu's format, save load and backup, delete latest, L+START delete,
  and every error message, the lists' too. Texts, keys and flow are as
  before; a message now shows "[B]OK", a question returns once its key is
  let go, and the delete question shows the whole name. The screen doesn't
  dim while a dialog is up. The format erase and the save backup show
  their progress over the NOR list (`6067e1b`, `adf21e9`, `ba02013`,
  `7b2d44d`, `8107dfe`).
- **Changed:** Diagnostics runs on the engine: its lines scroll in a list,
  colours are theme roles, the summary scrolls with UP/DOWN. A renderer
  step times a glyph, a 38-glyph row, an LZ77 cover into PSRAM, a PSRAM to
  VRAM copy, PSRAM writes at the slow and the menu's WAITCNT (read back),
  a scroll frame and a page change, and flags any figure more than 20%
  above the emulator's; a ten-second scroll-and-fade demo asks about
  tearing. The boot loader's title is no longer garbled in the log. An SD
  theme's wallpaper is read from PSRAM page 0 even when a launch or a NOR
  write left another page selected (`6067e1b`, `adf21e9`, `ba02013`,
  `6db6bfa`).
- **Changed:** the SD list, the NOR list and Recently Played, with the game
  menu, the NOR game menu and their questions, run on the v0.3 tile engine:
  a title bar with the folder, clock and count, 16 px rows with file-type
  icons and sizes, smooth scrolling, and a cut name that scrolls inside its
  column, no longer into the size. Keys and flow are as before; SELECT no
  longer toggles thumbnails, which v0.4's covers replace (`5e1981c`,
  `cfd3cc8`, `f641acd`).
- **Changed:** file names of up to 255 bytes list, open, launch and save
  whole: the list keeps them in PSRAM instead of 100 bytes each in EWRAM,
  which frees 70 KB of EWRAM. A name whose save name won't fit the pending
  save record (over 251 bytes) says Name too long, and one over 99 bytes
  can't be written to the game NOR, whose list keeps 99 (`5e1981c`).
- **Fixed:** L+START deleted a file in the current directory, which could
  be /SAVER after a failed launch, rather than in the list's folder
  (`f641acd`).
- **Changed:** SETTINGS (both pages) and HELP are drawn by the v0.3 tile
  engine: a header and a list with each setting's name and value, the
  official keys and edit flow, and the same settings writers. In edit mode
  the row shows the option under the cursor (`« RESET: ON »`). Page 2 gains
  THEME (Dark, Light or a theme from `/torii/themes`, which OK records in
  `/torii/theme.txt`), ACCENT and DIM, previewed at once and saved like the
  other settings; DIAGNOSE is now its last row. Held L and R turn one page.
  HELP credits the Galmuri fonts (OFL-1.1) and gives its QR code a quiet
  zone (`783114e`, `54b83e8`, `7075e23`, `cdaf71c`, `e3b624a`, `8057d8b`).
- **Fixed:** February days past 29 in leap years, hot keys stored as
  invalid codes, a dead fifth MODE B position, and AUTO SAVE and MODE B
  changes not reaching the next launch until a reboot (`783114e`). A saved
  ACCENT showed black, and a screen without a list could fail to appear
  after another closed (`2a67c3f`).

- **Added:** theme tools (not part of the kernel, listed as they are for
  theme authors): `just theme-new` starts a theme from Dark or Light,
  `just theme-wallpaper` converts a PNG into a wallpaper, reducing it to
  15 colours, and `just theme-check` checks a theme with the kernel's own
  parser and previews it on the emulated cart. `docs/themes.md` explains
  themes and every theme.ini key.
- **Fixed:** the cheat viewer shared theme view id 11 with the NOR list,
  so a theme's `nor-list` patch moved its nodes too; it is now `cheats`,
  id 34 (`38b60ec`).
- **Changed:** the SD theme loader's wallpaper check and log messages
  moved into the hardware-free parser (`theme_wallpaper_check`), so `just
  theme-check` prints exactly what the kernel logs. No behaviour change
  (`0c3cec8`).
- **Added:** themes from the SD card. `/torii/themes/<name>/theme.ini`
  (version 1) overrides the dark or light theme key by key: colours, fonts,
  row height, spacing, fade, slide, marquee and key repeat timings, up to
  16 layout patches, and a 4bpp LZ77 wallpaper repeated behind the menu.
  THEME = 2 picks the theme `/torii/theme.txt` names, loaded at boot; one
  that fails to load says why on the boot screen and the menu uses Dark,
  and a screen the theme breaks falls back to its base theme. Nothing new
  is written to kernel NOR. A stored ACCENT no longer draws as a
  near-black colour (`c84f0df`, `0f7ac38`, `b7aa517`).
- **Changed:** the cheat screen runs on the v0.3 tile engine: cheats as
  headings, their options indented, a picked one in the accent colour with
  "[x]"; the cursor skips headings, LEFT/RIGHT and L/R page, long names
  scroll. Picking and applying are unchanged. The counter counts options
  only ("[001/002]", not "[002/004]"), the game menu shows CHEAT until the
  screen is opened and then CHEAT(n) for the cheats picked, and opening the
  game menu drops cheats a previous menu left picked (`5e44a3b`).
- **Added:** views, widgets and themes for the v0.3 tile menu, not yet used
  by any screen (`source/ui/`). Screens are const layouts of labels,
  panels, lists, grids, images, scroll and progress bars with anchors, on a
  view stack with modal dialogs that slide in over the dimmed screen. Lists
  scroll pixel by pixel on a spring through a ring of 16 px row slots,
  select by palette bank, redraw a whole page hidden, and scroll a cut name
  on the selected row. Dark and light themes with seven accents are palette
  swaps, with palette fades and layout patches for theme files to come.
  A DIM setting (slot 27, off by default) darkens the screen after idle
  minutes; nothing new writes kernel NOR (`40c91a0`, `2b8037a`, `a38043a`).
- **Changed:** the v0.3 UI is smoother. A page change or wrap draws its rows
  in one hidden frame and fades the list in; lists scroll 32 px a frame, held
  L/R pages every 4 frames, and a cut name slides in and out at both edges at
  a pixel a frame. Keys pressed during a page change are no longer dropped.
  Rows draw faster, caps rise to 48 nodes and a 1 KB arena, and the scroll
  and progress bars are restyled (`a02944f`, `8df0d4c`, `9992a54`).
- **Changed:** motion checks a value's range before taking a slot and its
  spring settles in 64 bits; palette fades round symmetrically; releasing a
  direction restarts the repeat of one still held (`bec26d8`, `472a177`).
  `RGB()` parenthesises its arguments (`0567902`).
- **Added:** cover streaming for the v0.3 tile menu, not yet used by any
  screen. Files are read from SD a few KB per job step; LZ77 assets (TAS1,
  written by `tools/asset.py`) are decoded into a cache in PSRAM page 0
  (from 0x08C00000) and copied into sprite slots in VBlank, nearest the
  cursor first; affine sprites can scale and rotate them. Every game launch
  empties the cache before PSRAM is overwritten. `RGB()` now parenthesizes
  its arguments, for the static analyzer (`fb0614e`, `170e13a`, `0665879`).
- **Added:** text for the v0.3 tile menu, not yet used by any screen. The
  Galmuri9 and Galmuri11 Bold fonts are converted into `source/engine/`
  with 2-byte rows (`23efcc2`). `charset_next_latin1()` reads UTF-8 names
  for these Latin-1 fonts, drawing anything else as the replacement glyph
  (`7f5bed7`, `997f45c`). An ARM blitter in IWRAM (556 bytes) draws glyphs straight
  into mode 0 window tiles, and a text API measures strings, cuts them
  with "..." by character and clears rectangles: about 250 cycles a glyph
  (`c50ccf8`, `53bc7e1`). Trailing spaces no longer cut a line that fits
  (`fd242f0`).
- **Changed:** about 17.4 KB of EWRAM freed for the v0.3 engine (2.3 KB free
  before, 19.7 KB after), with no visible change. The cheat screen's line and
  value buffers and the file list sort's work buffer now borrow `pReadCache`,
  which holds nothing between menu passes (its owners are listed in
  `ez_define.h`). Recently Played rows shrink from 512 to 356 bytes, the
  longest entry the kernel keeps (`b9d6f5e`, `8224c72`).
- **Fixed:** a cheat file over 2275 lines ran its list off `pReadCache` and
  overwrote the NOR game list in RAM. The list now stops at 2053 lines and
  logs that the rest were left out (`b9d6f5e`, `664670e`).
- **Added:** the v0.3 menu engine's motion and input layers, not yet used by
  the menu. Motion has eased tweens and critically damped springs (retargets
  keep their velocity) for scroll, sprites, blend and palette fades. Input
  repeats per key (15 frames, then every 4, every 2 after 1 s), pages with
  L/R (20, then 8), and wraps a list only on a fresh press or after a pause
  at its end (`8b073d0`, `ff18199`).
- **Added:** each menu screen logs `screen ready: NAME` at debug level once
  drawn, so the golden-screen tests (`tests/shots`) wait for screens rather
  than fixed frames (`65acf23`).
- **Added:** the v0.3 tile engine's core in `source/engine/`, not yet used by
  any screen: a 60 fps frame loop with idle detection, state-machine jobs
  within a cycle budget, shadow video registers, palettes and OAM committed
  in VBlank through a capped VRAM queue, allocators for VRAM, palette banks
  and affine matrices with per-view arenas, and a layout budget checker
  (`a5d6392`, `09a5d58`).
- **Changed:** Diagnostics' timings read a free-running TM2/TM3 cycle counter
  shared with the engine, instead of restarting the timers each time
  (`a5d6392`).

## v0.2.4 (2026-10-18)

Tested on a real Omega DE (FPGA firmware 7): everything in v0.2.3, plus the
rumble fix below.

- **Fixed:** Diagnostics' rumble step never rumbled. The Omega DE's motor
  isn't driven through GPIO pin 3 in kernel mode: as Jagoomba does it, 0xF1
  at 0x09E20000, then 2 and 0 at 0x08001000, which reach the motor only
  while the GPIO port is on and the ROM page isn't the kernel's (real cart).
  Diagnostics pulses it so under the PSRAM page, and a no is a failure
  (`a6c76ee`, `6adf948`).
- **Added:** the boot logs the answers of the rumble probe ROM
  (`tests/roms/rumble`), which tries the motor four ways in game mode
  (`1fab0fc`, `c2faa0e`).

## v0.2.3 (2026-10-18)

**Don't use v0.2.1 or v0.2.2:** both can erase EZ-Flash's boot loader, and the
cart then starts in safe mode (`No_Loader_found`). `docs/recovery.md` says
how to recover.

- **Fixed:** v0.2.2 bricked a cart at boot, and v0.2.1 on WRITE TO NOR and
  delete. Kernel NOR from 0x7C0000 wraps onto EZ-Flash's boot loader, and the
  NOR list's copy B was written there. Copy B now sits at 0x780000, and
  Save_info writes nothing outside 0x780000-0x7BFFFF (`79cdf75`), nor more
  than 8 KB in the settings sector, which is 8 KB boot sectors on a PL064J
  (`36931c8`). Save_info maps the kernel's ROM page itself before writing,
  so no other page can turn its offsets into the loader's (`b2ad489`).
- **Added:** Diagnostics reads the boot loader, without writing it, and
  logs its CRC32, whether it's blank, erased or overwritten (no GBA header)
  and whether it is EZ-Flash's DE.BIN; a damaged one is an error with a
  warning on screen (`36931c8`, `b2ad489`).

## v0.2.2 (2026-10-18)

- **Fixed:** the EEPROM probe's report counts a read that matches a write
  in bytes 0-6 (the block and the run's nonce) as that write, logs "except
  byte 7: wrote XX, read FF" and flags the run's last-byte loss, so its
  size checks still work; it logs how each write waited (`a517691`,
  `906a468`).
- **Fixed:** Diagnostics' EEPROM item passed on any probe line, even a
  failed run's; it now checks the verdict. A button step missing L or R
  says the hotkeys need them, and the boot after the deliberate crash test
  says it was that test, not "crashed in the menu" (`ce9e84d`).
- **Fixed:** a `.sav` smaller than its save type needs (512 bytes for
  EEPROM 8K) is grown with 0xFF and logged; blocks past its end were never
  loaded or written back, so they were lost at power-off. A grow cut short
  by a full card keeps the old size (`a1d6598`, `906a468`).
- **Fixed:** the in-game menu, sleep and RTS-only hotkeys match when the
  combo's keys are held, whatever else is; an extra or stuck key used to
  block them. The handler is 4 bytes smaller (`0c2ccf2`).
- **Changed:** an older kernel's NOR list is saved in Torii's two-copy
  format once, crash-safely, and the list and game count are logged only
  when they change; v0.2.1 logged the older list on every read (`7a7e916`,
  `906a468`).

## v0.2.1 (2026-10-18)

- **Added:** a build ID, the CRC32 of the kernel's ROM, written after
  linking without moving any code. The boot log line, crash dumps (the
  crashed kernel's ID) and Diagnostics show it, and a unit test checks it
  against the ROM, so `just logs` can tell which symbols fit a log (`d7d0b85`,
  `18502bc`). A crash dump in v0.2.0's layout is logged as skipped and cleared.
- **Changed:** the kernel builds byte for byte the same on CI and macOS:
  the Makefile sorts its file lists, which set the link order, and asserts
  name files relative to the project (`82d6615`).
- **Fixed:** log files no longer fill with junk on the real cart. A new
  file was zero-filled from a ROM array, and ROM reads while SD mode is on
  return the FPGA's SD buffer; it is now filled from RAM, and `disk_write`
  refuses a source in ROM (`059e795`).
- **Fixed:** a log file with junk after its text (from v0.2.0) is left as
  it is, and logging goes on in the next file; the end of a file's text is
  found by reading the whole file, so junk can't hide it (`059e795`), even
  junk with no zero byte in it, such as 0xFF (`7887069`).
- **Fixed:** a flush cut off by power loss left part of a line, and the
  next session's first line ran on from it; the next boot now drops the
  part (`7887069`).
- **Fixed:** the log's sequence number wraps from 99999999 to 1 instead
  of overflowing the header's 8 digits, which lost a file (`7887069`).
- **Added:** the save flow is logged: every "Save sav?" answer, save copy
  and load (name, size, save type, and the `f_write`, `f_read` and
  `f_close` results), NOR game write, and write of the settings, NOR game
  list or pending-save record, read back to check it (`b2dac40`). A save
  copy the card cuts short is now an error in the log, not silent.
- **Changed:** the menu runs at WAITCNT `0x4018` (WS0 2/1 with prefetch),
  which the real cart read correctly, instead of the BIOS's `0x0000`; in
  mGBA a folder of 300 files opens in 23 frames instead of 40. Writes never
  measured at that speed (control registers, SD transfers, NOR, the clock,
  and launching with its PSRAM writes) keep `0x0000`, and games start at
  `0x0000` (`b2dac40`). Still to be confirmed on the real cart.
- **Fixed:** about 100 bytes of the kernel's reset code stayed in IWRAM
  when a game started. The last step now runs from the top of IWRAM, which
  the BIOS clears, and clears all the rest (`b8ff2e0`).
- **Fixed:** the in-game menu's DEBUG gave no sign it worked, and a
  power-off then lost the snapshot. It now says DEBUG SAVED and picks
  RESET, whose boot logs it (`5dab00c`).
- **Fixed:** after a crash dump, the boot report says the kernel crashed
  and where, not that the session ended in the menu; GB and GBC games are
  named by their title, not `(????)` (`f0c4dab`).
- **Added:** the EEPROM probe ROM (`tests/roms/eeprom`) asks the cart's
  EEPROM in game mode, where it answers, with 6-bit or 14-bit addresses;
  the kernel's next boot logs each step and a verdict, to settle 512 against
  8K on the real cart (`f0c4dab`). Each run's data carries a nonce, so an
  earlier run's can't pass for its own (`08ab386`).
- **Changed:** Diagnostics shows the clock live and logs it at the answer,
  names each failed check in the summary, logs rumble answers as info,
  and checks the checklist's yes answers against the log, listing those it
  can't back as unconfirmed. Its EEPROM step points to the probe ROM; the
  kernel-mode probe, which the real cart never answered, is gone
  (`f2e94c5`). A launch counts as evidence only once it reached the game,
  and file names can't fake it (`08ab386`). The WAITCNT line says it is the
  menu's (`3dbd34e`).
- **Fixed:** the game NOR list survives a power cut: it is kept in two copies
  (kernel NOR `0x7A0000` and `0x7C0000`) with a sequence number and CRC, and a
  save always leaves one whole copy (`c14ddbf`). The official kernel's list
  still reads, the official kernel reads Torii's, and games it adds after a
  downgrade are kept on the way back (`811a126`).
- **Fixed:** each game written to NOR ends with a completion mark, written
  last, so a game cut off while written is never listed, and a lost list is
  rebuilt from the marks (`c14ddbf`). It takes the area's last 512 bytes, or
  one more 128 KB block when the ROM fills its last block; a game with no
  room left for that block is written without a mark (`811a126`).
- **Fixed:** the boot no longer erases the NOR list when it reads no games,
  which let the next write go over them; a write reads the list again first
  (`c14ddbf`), and a failed write shows File error (`811a126`).
- **Fixed:** delete latest drops the game from the list and erases its mark
  and header, so a later write with the same header that loses power isn't
  listed with the old size (`c14ddbf`).
- **Fixed:** a ROM without gbafix's `0x96` at `0xB2` is listed after it is
  written to NOR, instead of being overwritten by the next write (`c14ddbf`).
- **Fixed:** the NOR scan turned interrupts back on when it reached the end of
  the 64 MB (`c14ddbf`).
- **Added:** a game started from NOR goes on Recently Played as `NOR:NAME`
  and starts from NOR there (`c14ddbf`).

## v0.2.0 (2026-10-17)

- **Added:** LOGGING in settings page 2 (Off, Errors, Info, Debug; off by
  default) writes the kernel's log to four rotating 64 KB files in
  `/torii/logs`, so a real cart can bring back reports (`63f50a5`). Off,
  it touches neither the card nor cart RAM. Each launch logs what the
  kernel did (header, save type, IRQ hook source, patched words, CART and
  GPIO), and the next boot says how the session ended. A new file needs
  1 MB free besides (`4c831be`), and lines never split across files (`0167c1a`,
  `0bfd2bf`).
- **Added:** a kernel crash (an IRQ arriving outside the kernel's code) is
  kept in cart RAM page 0xF0 with the registers, stack and last log lines,
  and logged at the next boot even with LOGGING off (`63f50a5`); a dump
  stays until it reaches the card, and the largest arrives whole
  (`0167c1a`, `0bfd2bf`).
- **Added:** with LOGGING on, the in-game menu has DEBUG, which saves the
  game's registers, hook state and memory for the next boot's log
  (`63f50a5`, `ed77614`). Its code goes in only then, and never into a NOR
  copy (`0167c1a`). The menu's font keeps only the rows and letters it
  draws, so the menu is smaller than before and still takes 600 cheats.
- **Added:** Diagnostics (DIAGNOSE in settings page 2, or L+R+SELECT at
  power-on) runs the unit tests and probes the clock, PSRAM, NOR, the save
  chip (read only) and cart SRAM, times WAITCNT settings, glyphs, BIOS
  LZ77 and the SD card, tries EEPROM 512 against 8K, looks for a separate
  rumble control, and asks the checklist in `docs/release-checklist.md`
  (`63f50a5`, `7aca5da`, `0167c1a`).
- **Fixed:** reading the game NOR's ID left the chip in ID mode, so after
  a soft reset the NOR list read as empty and the kernel erased its table;
  the read now resets the chip, and so does boot (`0167c1a`).

- **Fixed:** Recently Played no longer overflows a 256-byte buffer on a
  long path (`6b868b7`), and skips a line too long for the kernel to have
  written (`db08c34`), whose folder overran the path buffer.
- **Fixed:** a name shorter than three bytes no longer makes the type check
  read before it (`72a69fc`).
- **Changed:** the list's name copies have explicit bounds and the charset
  switch a default (`8831f34`), for `just analyze`; no change in behaviour.
- **Fixed:** the SD list shows files without the archive bit and read-only
  folders (`371b98b`); it leaves out only hidden and system entries and
  volume labels (`6b868b7`). An 8.3-only name with accented letters lists
  instead of showing `?` (`6b868b7`). A name longer than 99 bytes, which a list entry can't hold, used
  to be cut and fail to open; it now shows cut with a `*` and says Name too
  long, and can't open or delete a file whose name starts the same
  (`c324466`). A folder path past 255 bytes says Path too long, and a folder
  that fails to open leaves the path as it was.
- **Fixed:** a game in more pieces on SD than the cart's table of cluster
  runs holds (61) shows File fragmented instead of overwriting the launch
  parameters and EWRAM (`906c429`), before its save is made (`6b868b7`).
  FAT12 and FAT16 cards whose chains end in any end mark no longer hang the
  launch; an empty game file or a failed card read shows File error
  (`6b868b7`, `db08c34`).
- **Fixed:** writing a 65th game to NOR reports NOR full instead of writing
  past the 64-entry list in EWRAM (`bdc832d`).
- **Fixed:** a Recently Played game whose file is gone shows File error
  (`df1119c`). It made a save and started Goomba or PocketNES with no ROM.
  After a failed Recently Played launch, the list opens its own games again
  (`ff9aa71`); they opened from the entry's folder, or reopened the entry.
- **Fixed:** a 99-byte `.gb` name no longer writes its save name one byte
  past the buffer (`8a5cc6c`).
- **Changed:** all source is UTF-8 with LF line endings and formatted by
  clang-format (`27835ea`); the kernel builds the same.
- **Fixed:** A and L+START do nothing in an empty SD folder (`d7dda3e`). They
  acted on a stale entry from the folder listed before: A opened its launch
  menu or started a GB, GBC or NES game. Ported from DS Style v7.4.
- **Changed:** names are drawn in ASCII (`a47a7a7`, `eff3a93`, `93092e0`,
  `6711497`). The 12x12 GBK font and FatFs's code page 936 go, freeing
  about 350 KB of ROM. FatFs hands names over as UTF-8, so every file lists,
  opens and saves under its own name on FAT32 and exFAT; accented Latin
  letters show without their accents, other scripts as `?`. Cheat files and
  Recently Played are read byte for byte, so `.cht` files in Latin-1 or GBK
  still parse. Upgrading: the save the old kernel still has to write back
  and the Recently Played list keep names in GBK, which no longer match. If
  the last game you played has a non-ASCII name, switch on once with the old
  kernel first so it writes the save back.
- **Removed:** Chinese (`c05855f`). English is the only language until language
  packs arrive; a saved Chinese setting loads English. Cheats come from
  `/CHEAT/Eng`, and the fast patch engine is no longer off for Chinese.
- **Fixed:** a game's brightness survives the in-game menu (`5b10b34`). The
  menu zeroed BLDY, which is write-only, so a game that dims the screen
  once (Pokémon Ruby's title menu) came back at full brightness. It now
  turns blending off through BLDCNT and restores it on exit. The BG2
  rotation registers are still reset, as the menu draws on BG2.
- **Fixed:** the in-game menu, cheats, reset and sleep work in rev 1 of
  Pokémon FireRed and LeafGreen, Crash Bandicoot Purple and SimCity 2000
  (`b736015`). The fast patch engine's built-in table is keyed by game code
  alone and has rev 0's offsets, so these games got unrelated words
  overwritten, and most crashed with an addon on. A table entry is now used
  only if every listed word in the ROM is an IRQ handler address; otherwise
  the kernel scans the ROM. Blank game codes (homebrew) no longer match the
  table's all-zero entry.
- **Fixed:** an unset in-game hotkey (erased settings) counts as the key the
  settings page shows, L R SELECT or L R START (`b736015`). It made the key
  read as "no key held", so reset or the menu would fire on every frame.
- **Fixed:** with an addon on, Grand Theft Auto Advance, Super Monkey Ball
  Jr. and LEGO Star Wars II (EU) no longer crash or hang (`c57c3b6`,
  `dcf4c5b`). Each clears the top of IWRAM after the kernel's hook is in;
  their clear now stops just below it.
- **Fixed:** the in-game menu works in Lara Croft Tomb Raider: Legend (EU),
  which computes its IRQ handler address instead of loading it
  (`dcf4c5b`).
- **Changed:** these four per-game patches live in one table in `irqpatch.c`
  (`35613bf`). Each applies only to the exact ROM (game code, revision and
  header check) and only if the patch site holds the expected word.
- **Fixed:** Crash Nitro Kart no longer crashes after the in-game menu
  (`dcf4c5b`). The kernel moves a game's stack down to make room for the
  menu's saved registers; this game reloads its stack from a second
  literal, which is now moved too.
- **Fixed:** the in-game menu turns IME off and on as intended (`37c7d60`).
  It wrote DMA2's count register instead, leaving it at 1 after every menu.
  Loading a save state turns IME back on too (`110277b`), since the state
  is saved while the menu has it off.
- **Fixed:** the IRQ patch keeps up to 64 stores, not 32 (`bd61eec`).
  Kirby: Nightmare in Dream Land has 42 listed, and the rest stayed
  unpatched. Cached `/PATCH/*.pat` files of the old layout are scanned
  again, and a `.pat` is no longer read whatever its size.
- **Changed:** of several built-in table entries for one game code, the
  first that matches the ROM is used (`c69aa23`), so FireRed rev 1 gets its
  own entry; lowercase game codes count. The exact-ROM patches run only on
  SD launches and check the header against the game code.

- **Fixed:** cheat lines no longer make bogus writes (`2c274c8`): an entry
  without a value is dropped instead of reusing the last one, empty `;` and
  trailing `,` add nothing (a trailing `,` wrote 0 to the next address), and
  only hex digits count, so tabs, CRs and stray bytes are skipped.
- **Changed:** the cheat line parser moves unchanged from `showcht.c` to
  `cheat.c`, with the cheat table in `cheat.h` (`a0a46a4`), so it builds
  without FatFs or the hardware and the host tests run it under sanitizers.
- **Changed:** `sort.c` follows the new `.clang-format` (`14e1d5b`).
- **Changed:** the kernel builds with `-Wextra`, `-Wshadow`, `-Wundef`,
  `-Wformat=2`, `-Wstrict-prototypes`, `-Wmissing-prototypes` and `-Werror`
  (`d65cdb4`). The fixes keep behaviour: `(void)` prototypes, renamed
  shadowing locals, casts that keep the unsigned comparisons, `static` for
  file-local functions; `IWRAM_CODE` functions stay extern.

## v0.1.1 (2026-10-17)

- **Fixed:** opening an SD folder no longer builds the path with `sprintf`
  onto itself (`5193849`), which is undefined behaviour and could write past
  the 256-byte path; it appends with bounded `strncat`.
- **Changed:** the kernel builds without compiler warnings (`1e8dcee`):
  `Patch_somegame` gets a prototype in place of an implicit declaration, and
  the NOR erase poll drops an unused variable.
- **Fixed:** save types for Doom, Doom II, Gauntlet - Dark Legacy and the
  Classic NES Series and Famicom Mini games (`b9c9245`). The first three are
  EEPROM 512, not 8K (ez-flash/omega-de-kernel#3); the NES games are EEPROM
  (512, or 8K for the Zelda games and a few others), not SRAM.
- **Fixed:** too many cheats no longer overrun the patch area (`d57c89f`).
  The table is capped at 600 entries, which fit after the in-game handler in
  the 0x2000 bytes appended to the ROM; it also frees 19 KB of EWRAM.
- **Changed:** the GAME RTC setting becomes a CART row with separate CLOCK
  and RUMBLE checkboxes (`6d39f85`, `e7aaa36`, `a854cdc`). Turning the clock off
  no longer turns off rumble (ez-flash/omega-de-kernel#5). Each game gets
  what it uses: rumble for Drill Dozer, WarioWare: Twisted! and GB/GBC
  rumble carts in Goomba; the clock for other GBA games, GB/GBC clock carts
  and PocketNES.
- **Changed:** GB, GBC and NES games start as soon as you open them
  (`7489a1b`). Their boot menu only offered CLEAN BOOT
  (ez-flash/omega-de-kernel#12); hold L for a hard reset as before.
- **Changed:** the SD listing sorts names ignoring case (`792f94e`), with a
  merge sort in place of a bubble sort that copied whole entries. Listing a
  folder past its 256 folders or 512 files no longer writes past the list,
  and a full list of one kind no longer hides the other (`f16036c`).
- **Fixed:** the recently played list at its 10-entry limit (`aa88d7c`).
  Relaunching a listed game or adding to a full list wrote a stray 11th row,
  and the file was never truncated, so old rows lingered.
- **Fixed:** cheat files with LF line endings (`2ff3845`). Each value lost
  its last digit, so cheats wrote wrong values, and an empty value hung the
  kernel. Long values and section names can no longer overflow their buffers.
- **Fixed:** games start with EWRAM fully clear (`b6045f7`). The launch
  reset already cleared it, but the kernel then ran its last reset step from
  EWRAM and left 64 bytes there; that step now runs from IWRAM, with
  interrupts off before IWRAM is cleared (`972be9d`).
- **Changed:** the launch log names the clock and rumble a game may use
  (`63061c9`).

## v0.1.0 (2026-10-17)

- **Fixed:** the kernel builds on Linux (`c04ca5e`). `draw.c` included
  `hzk12.h`, but the file is `HZK12.h`; only case-insensitive file systems
  found it.
- **Fixed:** booting no longer overwrites settings and cheat memory
  (`b029fad`). `Read_NOR_info` copied twice the NOR game list's size, and
  the EWRAM data was 616 bytes over 256 KB. Both wrapped onto
  `SET_info_buffer` and the cheat buffers at the start of EWRAM. The copy now
  stops at the end of the list, and `SAV_info_buffer` moved to IWRAM.
- **Added:** `torii_run_tests()` call at boot (`2912147`). It does nothing in
  the normal kernel; the test kernel runs `tests/kernel` there.
- **Added:** `tlog` debug log (`3827cf7`). It logs boot and each game launch
  to mGBA's debug console and does nothing on real hardware.
