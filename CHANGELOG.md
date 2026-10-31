# Changelog

Changes to the kernel (`source/`) since EZ-Flash's official 1.06 source
(commit `12cb68b`), newest first. Tooling, tests and the emulator are in the
git log, not here.

## v0.9.12

- **Changed:** ROM patches cost nothing in game: a cheat with conditionals
  kept each as a step that does nothing, so many ROM patches and a key test
  were refused as too big; now a conditional's counts leave them out and it
  covers the same ops as on the device (626763b1).
- **Changed:** the picked cheats' ROM patches together go from 256 to 2,048
  halfwords (4 KB), kept in cart SRAM between a launch's passes rather than
  EWRAM, which gains 2.8 KB; a cheat still has 256 at most (f1128143).
- **Changed:** a cheat may have 2,048 ROM patches (4 KB) and a code of up
  to 128 KB, and the picked cheats 4,096 (8 KB): patch lines are decoded
  straight into the cheat's patches, kept in PSRAM, instead of one 32-byte
  op each, so a menu written into free ROM fits one cheat (a2ddcac5).
- **Fixed:** a cheat whose raw line ORs or writes through a pointer without
  a key (`[03005D8C]+137C|80`) no longer reads as unreadable on the cart:
  the line took the last line's key, or stack garbage (790188dd).
- **Changed:** the cheat toggle's frame budget is re-recorded at 176,348
  cycles: folders and the rows' icons took it to 174,730 before this, and
  the ROM-patch split adds about 1,600 (ebca8c3a).

## v0.9.11

- **Added:** cheat folders: a file's `cheatN_folder = "Items"` folds
  adjacent cheats into one row on the Cheats screen, which A opens; choice
  groups fold inside it, and any number of its cheats can be on (6b51eec2).
- **Changed:** the Cheats screen's title names where you are (a folder, a
  choice group, Picked, the Guide or Buttons page), the place one up faint
  after it, so a long game name no longer hides it; picking a choice goes
  back to its row; a folder that is one choice group opens straight to it
  and shows its pick; a folder with cheats on is ticked (dc61506b).
- **Changed:** rows that open a list or page (cheat folders and choices, SD
  card folders, Game saves' games and folders, Info's rows) end in an arrow;
  a cheat folder's title shows a folder icon; a cut title, popup title or
  page heading scrolls to its end, as a list's cut name does (01c3d073).
- **Fixed:** a hint-bar line too long for the bar (a greyed cheat's reason,
  mostly in Swedish) scrolls instead of being cut; the Cheats screen no
  longer starts past a folder whose first cheat is greyed (58987af4).

## v0.9.10

- **Added:** the cheat search reads BCD scores (Stored as: BCD, 1234 kept
  as the digits 12 34), and finds pointers to a value that moves in memory:
  Find pointers to it, then, once it has moved and is found again, Check the
  pointers found; one kept is filed as `[POINTER]+OFFSET:VALUE` and follows
  the value (c7fa7671).

## v0.9.9

- **Added:** the cheat search's filters Up by and Down by an amount, More
  than, Less than and From one number to another; Negative numbers, which
  compares them signed and shows and takes them with a sign; and Compare
  with the first scan, for the values Start remembered (kept when up to
  49,024 bytes of them are left after the first filter) (435c10b8).

## v0.9.8

- **Changed:** no kernel changes beside the version: this release follows a
  fix to the tests, whose checks could fail under load (07a575b9). The
  kernel is v0.9.7's.

## v0.9.7

- **Added:** Screenshots in a game's popup (games pages, Recent,
  Favourites, the NOR page), with how many it has, opens its in-game
  screenshots full screen, newest first: LEFT/RIGHT step, the date and
  "3 of 12" show until A or 2 seconds hide them, START deletes one. Shots
  are matched by their file names' game name, so every shot since v0.8
  shows (5564deb0, b0bb36eb).
- **Added:** the SD card page lists `.bmp` files and opens them in the same
  viewer (A, or View in their popup), stepping through the folder's;
  any uncompressed BMP shows, centred or shrunk to fit, in up to 176
  colours; RLE and other kinds say they can't show (5564deb0).
- **Added:** renaming a game's file on the SD card page moves its
  screenshots to the new name, each keeping its date, unless another
  game shares them (9a7685dc, 6f2af1c8).
- **Changed:** the viewer reads the next picture ahead between frames, so
  keys never wait for it, and photo-like pictures (1,024 or more 4-bit
  colours) are dithered so their gradients don't band (27233a5c, 6f2af1c8).
- **Added:** a cheat search in the in-game menu (its own row, or on the
  Cheats page): it remembers the game's RAM, narrows it down by same,
  changed, went up, went down or equals a typed number (8, 16 or 32 bits),
  lists the last 32 and keeps one as a cheat, on at once, which Back to
  Torii files as "Found ADDRESS" (7b5f6228).
- **Changed:** the in-game menu loads its screenshot, DEBUG and search
  code only when used, so it opens copying less than before (7b5f6228).
  A game whose stack starts at the BIOS's default (0x03007F00) has it 32
  bytes lower for the found cheats' live block; others keep it where it was
  and set a found cheat once, as Street Fighter Alpha 3 crashed with it
  lower (7b5f6228, 977c7823).
- **Fixed:** found cheats the card didn't take wait for the next boot, as
  intended (the boot's answer was cleared, so they were lost); a cheat kept
  on now, then kept again, is filed picked (8d9f3c03).
- **Added:** the cheat search's Undo the last takes back the last filter,
  so one picked by mistake doesn't end the search (it costs a filter about
  10 frames more) (e7e81331).
- **Added:** R in a game list makes the game a favourite or not at once,
  its star shown (on Favourites its row goes); Info's Controls says so
  (d00ed28e).
- **Changed:** a GBA game's Play « default » names how default starts it,
  "default: clean" or "default: addons", from its own boot mode or
  Settings' (d00ed28e).

## v0.9.6

- **Fixed:** a patch write that crosses an 8 MB boundary of PSRAM goes on
  in the next page, not over the first one's start (the official kernel's
  open todo) (de1a12ab).
- **Changed:** language packs name the Torii they were built for, which the
  log shows when it isn't this one, and a pack whose strings fill in other
  values than English's is refused as stale. Packs from v0.9.5 and before
  must be replaced with this release's `torii-lang.zip` (de1a12ab).
- **Fixed:** a soft hyphen (U+00AD) in a name draws nothing, not a hyphen;
  hiding a screen element hides what is inside it too (de1a12ab).
- **Fixed:** crash dumps name only stack words that point into the kernel's
  code, not its read-only data; Diagnostics' clock check measures from a
  second's tick and says "2 seconds passed in 2" (de1a12ab).
- **Changed:** the save question's log line reads `save: "Save to SD card?"`,
  as the question does (de1a12ab).

## v0.9.5

- **Added:** the code keypad types AnyGBA's raw lines: a key between Line
  and Del shows `[ ] + ^ |` in the digits' place, a line holds 48
  characters and scrolls with the caret, and a key's pointer that ends a
  line before another is saved with `+0` (92e11740).
- **Changed:** symbols draw dimmer than digits in the keypad, so `|` reads
  apart from `1`; the function row's Line key is one cell wide (92e11740).
- **Fixed:** raw lines with digits to fill in (`00XX`) can be filled in
  again, and Edit's refusal names the real reason (92e11740).

## v0.9.4

- **Added:** cheat files' raw lines (VBA's `address:value`) read AnyGBA's
  extensions: `[pointer]+offset` targets, `value^key` to write a value
  encrypted with a game's own key, and `target|value` to set bits. Pokémon's
  money and bag counts can be set without a master code (26e18859).
- **Changed:** a cheat's Guide page spaces out `^` and `|` in its code, and
  Edit on a code the keypad can't type says so (26e18859).

## v0.9.3

- **Added:** in the in-game Cheats list, SELECT on a cheat that is off runs
  it once as the game resumes (something it gives, say), past its Hold or
  Press buttons (82bcd334).
- **Added:** on the Cheats screen, adjacent cheats that write the same places
  with other values fold into one row named by what their names share; A
  opens its choices, and picking one drops the run's other pick (82bcd334).
- **Changed:** the interpreter's call and skipped cheats cost a little more
  in its budget, so a cheat that only just fitted may now be too big
  (82bcd334).

## v0.9.2

- **Added:** each cheat can have a guide and an activation: always on,
  while buttons are held, toggled by a press, or once at launch. The Cheats
  screen shows them on a Guide page and sets them on a Buttons page, with a
  keyboard for the guide; files keep them as `cheatN_guide` and
  `cheatN_keys` (d59b7bd5).
- **Added:** the in-game Cheats list shows only the picks, each with its
  activation, and scrolls a name too long for its row (d59b7bd5).
- **Changed:** a cheat that patches the ROM can't wait for buttons, so its
  Hold and Press are greyed with the reason, and a keys line its code can't
  follow gives way to what the code does (d59b7bd5).
- **Changed:** every progress bar (launches, NOR writes and erase, saving
  screenshots, the scans, cheat files, the firmware update and its hold)
  shows its percentage right of the status line, drawn by the copy's VBlank
  interrupt too while the cart copies a game, so it never stalls (1f11edc8).

## v0.9.1

- **Added:** cheat files past one read (up to 4 MB, such as Final Fantasy
  Tactics Advance's 14,000 cheats) open in windows of 250 through a `.cdx`
  index beside them, read as the cursor nears them; picks live in the index,
  and a file changed under it is indexed anew with its picks kept (47d5b5f2).
- **Changed:** cheats the kernel can't run, that do nothing, repeat another
  or are master codes are greyed with the reason instead of hidden; any cheat
  can be deleted, and Settings' Clean up cheats removes the greyed ones.
  Conditional cheats may use up to about 23% of a frame (47d5b5f2).
- **Added:** SELECT's menu on any cheat from a cheat database has Rename;
  `just cheats` keeps the name when it writes the file again. A game with a
  name of your own has Original name in its popup, which drops it (6632e0b1).
  A quote in a renamed cheat's name is written as an apostrophe, as an
  edited one's is (1e681486).
- **Added:** in the List + art view, a game's popup has Hide box art, which
  keeps a wrong cover off it (an empty file in `/torii/art/custom`), then
  Show box art; a game with art of its own gets Remove own art (49e9ea39).
- **Changed:** the messages for a full cheat file no longer say 1,024
  cheats or 192 KB; Rename turns a `cheatN_renamed = false` line true
  (49e9ea39).
- **Changed:** the message for a cheat file too big to read no longer says
  192 KB, which windowed files go past (58ac043e).

## v0.9.0

- **Added:** Swedish. The release attaches `torii-lang.zip`; with its
  `sv.lng` copied into `/torii/lang` on the card, Settings, System, Language
  switches to Svenska (983e2842, 19250625, 2cbb0223). Diagnostics' cycle
  budget takes the Swedish screens' longer texts (7f63f16e).
- **Added:** language packs. Every string the menu shows comes from
  `lang/en.ini` by id, and a pack in `/torii/lang` (built from a
  translation's `.ini`) replaces it, with its own date and number formats;
  Settings' Language row, shown now, switches the menu at once. A stale or
  damaged pack, or a string one lacks, shows English (5441fefe, bcc34910, dfa46579, 88b605b5); the firmware
  update's screens too, in English as they run before a pack loads (2da9f788).
- **Fixed:** the FPGA firmware update can't brick an unknown board: it writes
  only to an LX16 (version word `0xA`/`0xB`) on older firmware. Any other word,
  LX9 included until a Rev.A is measured, writes and shows nothing and is logged
  (b72337f5).
- **Changed:** before writing, the update shows a power-cut warning, then asks
  for L+R+A held two seconds; B cancels either. About and Diagnostics show the
  raw version word and name only allowlisted boards; a notice shows during the
  CRC check, which left the screen blank (b72337f5).
- **Fixed:** a question's B now wins when pressed with A or before A is let
  go, and the update's hold screen cancels on B held from before it showed;
  the update also reads the version word twice and skips on a mismatch (56fe3768).

## v0.8.4

- **Changed:** Info has six reading pages: Controls, In game, and new Saves,
  Cheats and Questions pages, with Your cart folded into About. The texts
  cover what v0.5–v0.8 added, and About credits Jagoomba Color, PocketNES
  and mGBA's cheat decoders (c0b88cd4).

## v0.8.3

- **Changed:** nothing in the kernel but its version; this release carries
  faster checks (`just check` runs only the tests a change touches) and
  faster power-cut sweeps in the tooling (c4039c16, 19473b46).

## v0.8.2

- **Changed:** a screenshot is named by when it was taken, not when Back to
  Torii filed it: the menu reads the cart's clock at the shot in games that
  have it (unless the game was mid-way through its own read), and the boot
  uses it if it falls between the launch and now (6aa4e212).
- **Fixed:** full 32 MB ROMs: the in-game code and cheats go in the last
  padding before the ROM's end that holds them, not over its data, for
  menu-only launches too; In-game menu Every game with Save states off cut
  the menu's code short at a 32 MB ROM's end (873d6676).
- **Fixed:** Settings' Game saves listed only the first 256 games with
  saves; it lists them all now, up to the library's 4,096 (9af7a7fa).
- **Changed:** the in-game menu's screenshot count and "Not on card yet"
  footers come from the language strings, ready for v0.9's packs (a9cf916e).
- **Changed:** the menu's pages share one block of EWRAM for their state,
  freeing 7.6 KB for v0.9's language work (8da81fd5).
- **Fixed:** the in-game menu's Debug snapshot could go unlogged: its
  marker was copied to cart memory from an unaligned address, losing its
  first byte; another buffer copied the same way is aligned too (b22693f5, 15d20095).
- **Changed:** the game popup's Load state list cut "Slot N" to "S..." (its
  date filled the row): it is titled Load state now, and each row shows the
  slot's number and how long ago it was saved, as Recent does ("2h ago"),
  or its date when the clock can't tell (6ec9be8b).

## v0.8.1

- **Fixed:** Load state at a game's title or intro gave a black screen:
  the GBA can't read back its scroll, window, mosaic and fade registers, so
  a state couldn't hold them and the earlier scene's stayed (Emerald's
  intro left the windows at 0x0, hiding the overworld). LOAD now finds the
  game's own RAM copy of its display registers, as screenshots do, and
  writes them back (c8a9e669).
- **Fixed:** after Load state the map moved before the sprites (Emerald),
  and loads in other scenes could crash or drift: the screen now stays on the
  backdrop until the game's own VBlank has run on the state; IRQ flags,
  Supervisor-mode registers and the IRQ vector are put back (32249fc2).
- **Fixed:** LOAD runs the hook's cheats and sleep key on its first frame,
  restores the game's WAITCNT and re-arms its sound DMA (7a2e109e, f06f39ef);
  timer reload values are measured at SAVE and restored (e0357553; Need for
  Speed Underground crashed).
- **Fixed:** launching into a state loads it at the game's entry, so games
  that clear IWRAM at start (LEGO Star Wars, GTA) no longer start without it;
  a state from another game or a newer Torii is refused with a question (c9e26bed).

## v0.8.0

- **Added:** a new in-game menu (L+R+START): a sprite overlay in the
  theme's colours that shows in 2 frames and gives the game back byte for
  byte (memory, VRAM, OAM, palettes, registers; checked on all 196 compat
  games), so it no longer breaks rotated or scaled backgrounds. It runs
  from borrowed EWRAM and lives in cart SRAM, leaving the patch area to a
  small stub. Resume, Save state, Load state, Cheats, Screenshot, Reset game
  (restarts the game without Torii), Sleep and Back to Torii; sound is
  muted without the old noise on resume (3c5a7202, e6722286, 10b83f30).
- **Added:** cheats in game: a toggle per cheat (up to 64, unpicked ones
  too), All off and All as launched, with greyed rows saying why; Back to
  Torii keeps the toggles as the game's picks (c708af28).
- **Added:** save states with eight slots per save slot, with times, in
  `/torii/states/<rom path>/<save slot>/`, filed from cart SRAM at the next
  boot power-cut safe and packed (bb9ba50a); the newest is ready in game,
  any slot starts from the game popup's Load state, which asks first if a
  slot can't be read; Load state with nothing saved no longer crashes; old
  `/RTS` states come in as slot 1 (9f579842, eabe5373, 195fb22d, 21a81da4,
  290f4323).
- **Added:** screenshots from the in-game menu, saved as BMP in
  `/torii/screenshots` at the next boot by a software renderer of the
  game's screen (modes 0-5, affine, sprites, windows, blending); scroll and
  other write-only registers can't be read back, so a scrolled tile screen
  comes out shifted (23b72a7e, f576c06f).
- **Changed:** Settings' Back to menu becomes In-game menu: Auto (when the
  game has states or cheats), Every game, or Off (cheats still run)
  (d7707c9b).
- **Fixed (before release, from the first cart test):** the in-game menu
  blacked out Pokémon Emerald on the real cart. Its stub now keeps games'
  ROM wait states (it had set the menu's faster ones in game mode), checks
  its backups and the overlay's image against a check word before touching
  the game, and on any failure leaves the game running and logs why at the
  next boot; it opens in 4 frames (36802315, eafa7159). The in-game probe
  shows its report on screen and keeps it in its own save, runs its SD test
  last and only on A, and the boot retries a card that doesn't mount three
  times before its error (ff9d5aed).
- **Fixed (before release, data safety):** the cart's FPGA writes the
  in-game state buffer back to the card through the launch table's runs at
  0x300; v0.8 left them empty, and on the second cart test the walk wrapped
  onto the launched ROM's sectors and overwrote it. Every launch now points
  the write-back at `/torii/rts.bin`, a 448 KB file of its own whose chain is
  checked to cover exactly that much, sent before any state-buffer write;
  the table is checked before it is sent and a launch refuses rather than
  send a bad one, with its own message on a card short of room; the state
  flag is cleared before its tag; FatFs writes are fenced to the volume; a
  ROM whose header reads blank isn't launched (ad9da2e7..cf01e697, 50c5d75a,
  92a59bb2, 82270ca7, 9c042797).
- **Fixed (before release):** after waking from sleep or a refused menu,
  the game no longer sees the keys (Pokémon opened its START menu); a
  refused menu shows a white screen while its keys are held instead of
  stuttering, and the menu's Sleep row says START+SELECT wakes (e1714e99).
- **Fixed (before release, third cart test):** Emerald's menu refused
  because a preloaded state carried the RTS flag the FPGA watches, so the
  cart wrote the buffer back while the menu read it. Torii never sets that
  flag now: a state is marked "TORII-STATE-OK1." of its own. A refused menu
  no longer strands you: A gives the game back, B or holding the keys 2 s
  goes back to Torii, which says why the menu didn't open (4f524a84,
  73aaeccf).
- **Fixed (before release, fourth cart test):** a refused menu's reason
  was lost: the real BIOS runs its reset on the game's stack, over the
  record's IWRAM copy; the stub moves the stack first. Each refusal reason
  shows its own screen colour, and the record keeps the first mismatching
  word for the log; DMA is stopped twice and a backup checked twice
  (0e35d4e5, 374ab054).
- **Fixed (before release, fifth cart test):** the real cart's SRAM page
  register ignores its low bit (8 KB steps), so odd pages landed on even
  ones and the menu's image overwrote itself; every user of cart SRAM now
  addresses the 64 KB window's even page, and the emulated cart does the
  same. The menu then opened in Emerald, with Save state, Load state and
  Screenshot working on the cart (cfebcbeb).
- **Changed:** in-game states and screenshots stay in cart memory until
  Back to Torii saves them to the card (SD access while a game runs is
  impossible on this cart): the menu's footer says what isn't on the card
  yet and the row reads "Back to Torii and save" (a0cbc2fd). Up to five
  screenshots a session, packed, each its own BMP; the scroll is taken
  from the game's copy of its display registers, so a scrolled screen
  comes out right; the boot saves them under a progress bar (9d6dcaa0,
  2cd4261c). Back to Torii goes straight to black (bdd73fec). A card that
  can't be read after a mount says so instead of showing an empty list,
  and the in-game probe's SD test is retired (12a9274c).
- **Changed:** the in-game code no longer takes 4 KB of the kernel's IWRAM
  (a311071d); the DEBUG snapshot is taken by the overlay, freeing the
  stub (d79be1ca); LOAD copies through the faster loop (92c4c5d6); a
  relaunch after Back to Torii skips putting the state back when cart
  memory holds it (fa6e4363); the in-game cheat list uses the kernel's ✓
  and clearer wording (5f80dd9e); dead code removed (979c71de).
- **Fixed (security):** a `/PATCH` cache file is checked before use: its
  sizes and offsets must fit the ROM, and it must be for this ROM (size and
  header CRC), so a corrupt or stale one can't make the launch copy out of
  bounds (de1e1413, 8ce6e4c6).
- **Fixed:** log lines were lost while a screen ran its own loop (keypad,
  cheats, Game info); every screen's loop now flushes, and the keypad and
  frame summaries log less (ebbf28b7). Box art shows soon after boot:
  crc.txt loads before names.db (2309357c, 05748617). The box-art listing
  no longer takes 3 frames in one on a slow card (bc0aa59e). An `/RTS`
  state is imported once and not unpacked twice (4daee59a). A save of
  another chip type goes to History instead of being padded into the new
  one, and the notice names the game (4524c3bf). The copy bar's estimate
  matches the cart (7ad9f062); favorites.txt edited by hand is no longer
  reported on every boot (a4ed0720).
- **Added:** the keypad shows how many digits a line has toward a whole
  one, and L+B clears the code (4e362a26).
- **Added (tests):** an in-game probe ROM for the real cart
  (`tests/roms/ingame`) and emulator stops for game-mode PSRAM and SD
  writes (a6f5e5fe, ab93fc31).

## v0.7.1

- **Fixed:** cheat codes follow the real devices where mGBA doesn't (GBATEK,
  VBA-M): Action Replay v3 C6/C7 IO writes are 16 and 32 bits; an IF with an
  empty THEN and an ELSE no longer runs the ELSE every frame; its padding
  and end lines count and stop as on the device; signed compares work at
  the code's width; GameShark D/E conditionals read their not-equal and
  at-most/at-least kinds and 3010-3060 are adds; E0zz counts lines; a second
  CodeBreaker key and a DEADFACE reseed carry to later cheats (13204692,
  d9fa0cf9, b2e7d29b); a DEADFACE inside a cheat with its own format
  reseeds the cheats after it, and deleting an emptied file deletes the
  copy a reader takes last, so a cut leaves no broken copy (979432e5,
  df029857). A halfword or word ROM write to an unaligned address is
  patched aligned down, as a CPU store is (6213f491).
- **Added (tests):** every cheat is checked three ways: `just cheat-corpus`
  decodes libretro's 7,017 GBA cheats with Torii's and mGBA's decoders and
  fails on any difference no documented device rule explains; a type matrix
  runs every op, width, format and comparator through both engines on the
  emulated cart, frame by frame; `just cheat-fuzz` runs random programs the
  same way and keeps failing seeds as regression cases.
- **Added:** cheat types that were unreadable: GameShark slowdown (read, not
  run, so the rest of the cheat works), Action Replay v3 "turn off codes"
  conditionals (the rest of the cheat waits on them) and CodeBreaker
  D0000010 (13204692).
- **Added:** the cheat screen's START menu: Details (the full name, code
  lines, format, budget share and why a row is greyed), Turn all off, Show
  picked only, and Edit and Delete on your own codes. The title shows how
  many are on, the list wraps, greyed rows say how much they need and keep
  their key hints, a full file greys "+ Add code" up front, new codes are
  named "Code N", and picking cheats for a game that boots clean says they
  need Boot with addon (cd7caa0f, 51c93fef).
- **Changed:** the popup's cheat count survives a power-off
  (`/torii/cheats/counts.txt`, still without opening the cheat file);
  deleting a file's last code deletes the file (d61c12dc). €, …, bullets,
  curly primes and Unicode spaces draw Latin-1 stand-ins instead of a box
  (1275ede2).
- **Fixed:** Jagoomba keeps its VSync setting over a power-off
  (jagoombacolor#36, 7d47e5e7); PocketNES takes the region from a NES 2.0
  header and keeps one picked in its menu (PocketNES#3, 51b6aa9f).

## v0.7.0

- **Added:** cheats in GameShark v1/v2 (Action Replay v1/v2), Action Replay
  v3 (GameShark SP), CodeBreaker (plain and keyed) and VBA raw codes, decoded
  by a port of mGBA's decoders (MPL-2.0, `source/cheatcode.c`) from one
  libretro-format file per game, `/torii/cheats/<rom path>.cht`, which moves
  and renames with its game (6e4d8e1b, e3d7dcf1, 6fe4231c, 51d987bb, a4596285).
- **Added:** a per-frame cheat interpreter (`source/cheatrun.s`) in the
  game's patch area: every code type the decoders read runs each VBlank with
  mGBA's semantics (writes at their width, fills and slides, pointer writes,
  add/or/and, conditionals with ELSE blocks, button activators on R+SELECT),
  within a cycle budget of 10% of a frame; the patch area grows to 0x3000
  bytes (025f8ca5, 650b03e8).
- **Fixed (before release):** cheats run exactly once per VBlank: the hook
  calls the game's IRQ handler and runs them again only after VBlank is
  acknowledged, so a game that serves HBlank first can't livelock with a
  heavy cheat set (8a0e5d6f). Cheat ROM patches never land on the in-game
  code or the kernel's own patches, and on a ROM without room the patch area
  shrinks to what the picks need, or the cheats are left out (3a216dff,
  174d27d6). A cheat file over 56 KB is refused rather than cut short when a
  pick is saved; a ROM write inside a conditional and add/or/and on ROM show
  as "can't run on this cart"; `enable = 1` counts as a pick (5b51d980).
- **Added:** a cheat screen from "Cheats" in a GBA game's popup: a tick per
  cheat, picks saved power-cut safe in the file and applied at every launch,
  cheats on from the game's start, and a big file read once behind a progress
  dialog instead of freezing the screen; the popup's count leaves out master
  codes (a4596285, 917111b7, 37c6827f).
- **Added:** a code keypad: "+ Add code" ends every GBA game's cheat list,
  and a hex keypad types GameShark, Action Replay, CodeBreaker or VBA raw
  codes, shows the format it detected (SELECT overrides it, kept as
  `cheatN_format`) and what the code decodes to, then names it on the rename
  keyboard and saves it picked; placeholders (`00??`) open in it to fill in,
  and the user's own codes can be edited or deleted (731d2d38, 088ea321,
  8c225b3e, e8ae242d). A card error opening a cheat file now leaves the file
  alone instead of starting a new one over it (f4a42078).
- **Added:** a cheat file names the dump it was made for (`# for crc32 X`,
  first in the files `just cheats` writes): the popup and a launch leave it
  out when the CRC cache, or `crc.txt` read straight from the card before
  the cache loads, knows the ROM as another dump, so a swapped ROM never
  runs another build's codes; a file without the line is for any dump
  (7c7a9bb0, 5b6a1bec).
- **Changed:** `just cheats` takes cheats from GameHacking.org only, by exact
  ROM CRC32: a harvester pasted into your own browser's console exports the
  mGBA cheats of your card's dumps at the site's robots.txt pace, and the tool
  imports its bundles from your downloads, keeps each code's device as
  `cheatN_format`, drops unnamed and oversized sets and fits the file under
  the kernel's cap. A light romhack whose base dump is on the card gets the
  base's cheats, "(base game)", less ROM patches over bytes it changed
  (b8997362, 1dc2d41c, 868f2b2c, a8d2e692).
- **Changed:** cheat writes keep their real addresses (no longer folded into
  IWRAM or EWRAM), and ROM-patch codes are written once into the game's PSRAM
  image, with no cap (a4596285).
- **Changed:** cheat files up to 192 KB (from 56 KB): the text sits in PSRAM,
  read in 1 KB parts and edited in place, and a launch keeps its picked ROM
  patches in EWRAM instead of reading the file again over the loaded game
  (efd00fe9). `just cheats` writes compact files (no enable line for a cheat
  that isn't picked) that fit under the cap with room for picks (9701948f,
  8afa1f77).
- **Fixed (before release):** the game popup no longer reads the cheat file
  (87 frames on a 190 KB file, now one): its count comes from a cache filled
  when the file is decoded. A cheat whose ROM patches don't all fit is left
  out whole, and the screen refuses such a pick, or one the file has no room
  to save; a too-big file, another dump's file and a failed PSRAM write each
  get their own message instead of a card error (8541f133).
- **Removed:** the Cheats setting, Boot with addon's cheat step, `/CHEAT`,
  `GameID2cht.bin` and EZ-Flash's `.cht` format, whose cheat files are poor
  (a4596285).

## v0.6.2

- **Added:** GB, GBC and NES games have save slots, as GBA games do: Saves
  in their START menu and in Settings > Saves > Game saves, New slot (Empty
  or a copy of one), backups, rename, delete, and Play with. A slot holds the
  emulator's 64 KB of cart SRAM (the game's save, the emulator's settings and
  save states), so New slot has no "from a .sav file" for them: a .sav from
  elsewhere is the game's save alone (6e904573).
- **Fixed:** the emulators' builds list their source files sorted, so they
  link in the same order everywhere: v0.6.1's release, built on CI, has a
  Jagoomba laid out differently from a local build of the same commit (both
  work). `just repro` now rebuilds both emulators from a shuffled copy (4f2cf2b2).
- **Fixed:** NES games got no cover and no No-Intro name: the kernel took a
  NES game's CRC over the whole file, its 16-byte iNES header too, where
  No-Intro's CRCs (names.db, the art pack) leave it out. They leave it out
  now, here and in `just names-update`; crc.txt's first line says so, and a
  crc.txt without that line has its NES CRCs computed again (df975858).

## v0.6.1

- **Changed:** a GB or GBC game with 32 KB of save RAM keeps it in the
  cart's SRAM as it writes it, as a real cartridge does: Jagoomba writes each
  byte through to SRAM 8000-FFFF, its own records (settings, save states)
  move below, and the next launch packs the copy into its record. v0.6.0's
  autosave paused the game 0.12-0.15 s a second after save writes, some
  20 times a minute in Pokémon Picross and Puzzle Challenge (measured in the
  emulated cart); now nothing pauses and a power-off right after a save
  keeps it. The autosave stays for a save whose records already fill more
  than 32 KB, skips RAM written with the same bytes, and waits five quiet
  seconds instead of one within 30 s of its last save. Saving Jagoomba's
  records clears only what they free, not all of SRAM: 0.13 s down to
  0.08 s. Save states have 32 KB instead of 56 (88eebc2).

## v0.6.0

- **Changed:** the GB/GBC and NES emulators are built from source with the
  kernel (`emulators/`, pinned and patched): Jagoomba Color v0.5 as before,
  and PocketNES moves from the 2013 build to Dwedit's 2025 source (MMC5,
  VRC3/4/6 IRQs, mapper 118, NES 2.0 headers). The launch copies them in
  128 KB parts and writes PocketNES's whole 48-byte header, flags at 0 where
  stale PSRAM was (998abab). PocketNES leaves out Go Multiboot, whose 60 KB
  multiboot copy of itself Torii has no use for: it is 83 KB, smaller than
  the 2013 build, and the kernel area is 70% full (193df78).
- **Fixed:** a GB palette picked in Jagoomba's menu stays the game's
  (ez-flash/omega-de-kernel#12): its settings are saved on the Omega, and
  the reset no longer puts the automatic palette back. Autosleep's setting
  comes back with its time (998abab).
- **Fixed:** a GB or GBC game with 32 KB of save RAM (Pokémon Yellow, Gold,
  Silver and Crystal, among others) lost an in-game save at power-off unless
  Jagoomba's L+R menu was opened first: Jagoomba kept that RAM in EWRAM and
  copied it to the cart only from its menu. It now does the same a second
  after the game's last write. 8 KB games and NES games already wrote
  through to the cart (b661254).
- **Fixed:** Diagnostics' unit tests failed `asset_sd_read` on a real card,
  whose clusters are bigger than the emulated card's: the test now expects
  the seek steps the card's cluster size gives (b661254).
- **Fixed:** NES games start on a 256-byte boundary in PSRAM: PocketNES
  times a branch's page crossing by its ROM address, so a game off a
  boundary ran some branches a cycle off (Dwedit/PocketNES#5) (d333227).
- **Fixed:** Pokémon Crystal and other games stuttered in Jagoomba at the
  default gamma: it now waits for line 164 before drawing the background
  then, as a hardware-tested fix in EvilJagaGenius/jagoombacolor#46 does.
  PocketNES's autosleep is off by default too, and its setting comes back
  with its time (d333227).
- **Changed:** both emulators run ROM at 3,1 wait states with prefetch, as
  GBA games do from PSRAM (they ran at the power-on 4,2 without); GBC games
  see a GBA, as on a real GBA (Shantae, the Oracles' Advance Shop); autosleep
  is off by default (998abab).
- **Removed:** the unused `goomba.h` and EZ-Flash's `emusrc/` (998abab).

## v0.5.3

- **Added:** Settings > Saves > Game saves lists the library's games that
  have saves, each opening its Saves page, where a slot played starts the
  game: saves can be found from Settings, not only from a game's START menu.
  The game NOR's games aren't listed (their page has Saves) (093a345).
- **Fixed:** a key tapped while a write blocks the menu (Settings' NOR write,
  a save copy step) is no longer lost: the VBlank interrupt keeps every key
  held since the last poll, and the next frame sees the tap (f1b6504).
- **Changed:** Play with names the game as the list it was started from does
  (its display name, not its file name) (c62c810). Clean up's bar shows two
  shades, the saves and the backups its line names; the trash's size stays
  on Empty trash's row (093a345). New slot's pane heading no longer peeks
  above its popup (99ddea8).
- **Changed:** the kernel parses numbers itself (`fmt_strtoul`), so newlib's
  errno block (`impure_data`) is no longer linked: 776 bytes more IWRAM free
  (5,764) (ebaffe8).

## v0.5.2

- **Changed:** a launch no longer erases a NOR sector for the pending-save
  record (about 0.5 s): the sector holds 128 records in slots, each launch
  programs the next erased one in place (its flag last, so a cut never leaves
  half a record pending), and the sector is erased once every 128 launches;
  in-place writes are bounded by `kernel_nor_program_ok` (a6b5e1d). A clear
  also uses up any stray slot past the record, and the sector's erase writes
  slot 0 flag last too. Going back to an older kernel loses a save still
  waiting to be copied in a slot past 0: copy it first (power on once)
  (a9b366c).
- **Changed:** the card works with fewer, larger writes: each SD write call
  ends with the driver's 8.6 ms settle delay, so the save copy writes 8 KB a
  call, slot copies 4 KB and a new save 8 KB, and a new folder's cluster is
  cleared in one call (64 calls for a 32 KB cluster before: about 0.55 s a
  folder). The save copy runs its recovery check once, not three times
  (ae05694, 10db684).
- **Changed:** a 12-sector write-through cache keeps the folder and FAT
  sectors FatFs reads again and again (a slot rename read 56, now none; on a
  cart each costs about 2 ms); each save copy, launch's save and slot
  operation logs its card reads, writes and time, and the library walk logs
  its own time and sectors, without names.db's load or the pauses
  (ae05694, c3e03ca).
- **Changed:** the kernel's own printf replaces newlib's, whose stdio, heap and
  float code took 39 KB of ROM and 5.8 KB of IWRAM; a log line formats in
  21,000 cycles, not 54,000. The language tables and the NES patch are
  `const`: 1.5 KB more out of IWRAM (9adf9e5, c3e03ca, e860e93).
- **Fixed:** the trash purge read a folder again for each file it removed, and
  pruning saw at most 64 backups of a slot: it reaches every one (10db684).
- **Changed:** B held on the SD page climbs a folder every third of a second
  up to the card's root; Game info's Played says Not lately or Recently,
  never blank (a91a367, 8013ed2).
- **Removed:** the slot store's import and export, which no screen uses since
  New slot makes a slot from a .sav (7b61033).
- **Changed:** Settings has a Saves card (Save without asking, was Auto save;
  Backups kept; Clean up saves, was Library's Saves); a slot's popup no longer
  has Export .sav; Empty trash with nothing in it does nothing; the save
  pages' words are plainer (Move to a game, Made, Backup N, what A does)
  (`docs/design/v0.5/settings-ux-review.md`, bb89ccd, 48b5e95).
- **Changed:** a game list opens on the game played last that it holds (after
  a play session, A, A plays it again), and every page opens on the row it was
  left on earlier in the session; Recent's game popup has Remove, which takes
  a game off Recent and keeps its file (8d92e83).
- **Changed:** one word per thing: the NOR page says Play like the others,
  Settings' History is Backups kept, the save pages say START menu
  (`docs/design/v0.5/ux-review.md`, 8d92e83).
- **Changed:** a new slot, a rename or a copy in a game that has a save no
  longer makes the next boot rebuild and rewrite the library index: its
  walk counts a game's save folder once (73df309).
- **Changed:** the first file or folder made after power-on no longer
  stalls: a background job finds the FAT's first free cluster from the
  mount on (on a 32 GB card with games ahead of the free space, FatFs read
  ~600 FAT sectors first: a second or more before a save copy's first write,
  and seconds of a frozen menu inside the library's index write) (fb1cba6).
- **Changed:** a save copy no longer erases a NOR sector to clear the
  pending-save record (0.7 s): the record is programmed to zeros in place
  (4a32b27); the move into the backups compares no bytes, and slot copies
  verify by CRC with one read-back (2310fed).
- **Changed:** copying a save (a slot copy, a slot from a file) shows a
  notice; the saves page says when it can't open; Play's « » boot
  carries through Saves, and both START popups offer it on one rule (30f7f1f).
- **Changed:** the save screens, rebuilt (design direction B,
  `docs/design/v0.5/saves-variants.md`): a game's Saves is a list and detail
  page (`source/screens/save_page.c`): its slots with the active one marked
  and when it was saved, RIGHT for a slot's backups (A restores), START for
  its popup (Play, Make active, Backups, Rename, Copy to new slot,
  Delete); New slot offers Empty, a copy or a .sav file, which makes a new
  slot and overwrites none. The launch's Play with is the same page. Settings >
  Saves shows a storage bar, then the saves without a game and Empty trash,
  and changes its figures in place after an action (b713e2a).
- **Changed:** the START popups: Play (or the SD page's Play) steps its boot
  with LEFT/RIGHT (default, clean, addons) in place of Clean boot and Boot
  with addon, and Saves comes second, never left out; popups scroll past 7
  rows, so a long one costs no more tiles (7902bad, b713e2a).
- **Fixed:** a save set aside by a save type change was pruned after a few
  play sessions: it is a kept backup now; the trash purge ignores a clock
  outside 2026-2060 and re-dates folders an unset clock dated (ca027da).
- **Changed:** EWRAM free goes from 5.8 KB to 12 KB: the save screens' state
  lives in `pReadCache` while they run (b713e2a).
- **Fixed:** START did nothing on a GBA list in the list-with-art view: Saves
  made its popup too tall for the free tiles. A popup that doesn't fit with its
  title now opens without it (7e63c81).
- **Changed:** the save copy no longer copies the old save into the history: it
  becomes the `.bak` and is renamed in after the swap, so a 128 KB copy loses
  the 3.7 s it spent there; restore and a save-type change rename too, and
  recovery keeps a `.bak` as a version instead of removing it (dc64061).
- **Changed:** opening a GBA game's popup reads nothing from the card (it read
  the cheat table, the ROM header and the record for Saves); a save still on
  the chip is copied before any slot is renamed, deleted, restored or imported
  (dc64061).
- **Fixed:** deleting a save "without a game" whose path was a folder took
  every save beneath it; a delete changed the current slot before its journal;
  a rename or delete stopped halfway cleared its journal; a rename onto another
  slot's 8.3 name trashed its history; a launch now finishes slot journals
  first; trash expiry overflowed; the library counted saves twice (dc64061).
- **Added:** the per-game save manager (`source/screens/game_saves.c`): "Saves"
  in a GBA game's Start menu (Games lists, SD card page, game NOR page) lists
  its slots with the current one marked. A slot's actions are Launch with this
  slot (no picker follows), Set as current, Rename, Duplicate, Delete (asks;
  to `/torii/trash`, never the last slot), History (restore a version; the old
  current goes into the history) and Export (`/torii/export`); New slot and
  Import .sav (a folder browser, then the slot to replace) close the list
  (e0fa5e7).
- **Added:** the global save manager: Settings > Library > Saves
  (`source/screens/saves.c`, card side `source/saveorphan.c`). It shows the
  space the saves, their history and the trash use, and the orphans, save
  folders whose ROM is no longer at their path. Relink picks a library game
  (same file name first, then the same console) and moves the save and history
  folders there; a slot whose name the target has already comes as `<name> 2`,
  `3`..., the target's own slots and record never change. Delete sends an orphan
  to the trash for 30 days, Keep leaves it, Empty trash now asks first.
  Journaled in `/torii/orphanop.txt` and finished at boot; folders move a file
  at a time, as a folder renamed into another parent can leave a wrong `..`
  entry after a cut (144c567).
- **Added:** the launch's save slot picker and the New slot and Rename flows
  (`source/screens/slot_picker.c`, `slot_flow.c`, `slot_list.c`). A game with
  two or more slots asks which to play on every launch (the last used chosen,
  A plays, START renames, B starts nothing, a "New slot..." row names a slot
  and makes it blank or a copy of the current one); a game with one starts
  straight in, and Select held with A shows the picker anyway. The chosen slot
  becomes the record's, so the save loaded and the pending-save record are its
  (197b36c).
- **Added:** the slot store (`source/slotstore.c`): a game's slots listed,
  created blank or as a copy, renamed (also by case only), duplicated and
  deleted to `/torii/trash` (purged after 30 days at boot, never on an unset
  clock); each slot's history of versions (listed, pushed, pruned, restored),
  imports and exports of a `.sav`. Renames, deletes and installs are journaled
  in `/torii/slotop.txt` and finished at boot. New Settings row *History*
  (versions kept: Off/1/3/5/10/20, default 5). The save copy now puts the
  version it replaces into the slot's history before the swap, so the old save
  is in the history or the file at every power cut (8f7cc4d).
- **Changed:** a save file bigger than the game's save type (EEPROM 8K changed
  to 512) is no longer used as it is: it goes untouched into the slot's
  history, the slot starts the new type blank, and a notice says so once. A
  smaller file is still grown with FF (8f7cc4d).

- **Changed:** saves leave `/SAVER` (no import: v0.5 keeps no backwards
  compatibility). A game's saves are in `/torii/saves/<rom path>/<slot>.sav`,
  its first slot `Main`, and its record (save type, boot mode, current slot) is
  `game.ini` beside them; a NOR game keeps its saves as for a ROM at
  `/torii/nor/<name>`. Folders are made when a file is first written. The
  pending-save record now holds the full path, so a save path over 251 bytes
  is refused at launch with a message. The library marks a game saved from the
  `.sav` files in its folder, Rename moves the game's folders (saves, history)
  with it through the same journal, and the FatFs alias drop now also handles
  folders. There is no setting for saves next to the ROM (b0e8b49, e98a46c).

- **Added:** `cutpoint(name)` (`source/cutpoint.h`), a debug log line
  `cutpoint N: name` that marks a step boundary for the power-cut tests
  (`mgba-shot -X N`, `tests/cutsweep.sh`); nothing else happens at runtime
  (798691f)
- **Added:** `source/saveslot.c`, the hardware-free logic for v0.5's save
  slots: name cleaning and case-insensitive uniqueness, save, backup and
  trash paths, the backup history (counter order, pruning), the rename
  journal with boot recovery, and blank/identical checks. Nothing calls it
  yet (6ddae27).
- **Changed:** the copy of cart SRAM to the card can no longer cost a good
  save. It writes `<save>.tmp`, syncs, reads it back and compares it with the
  chip, then renames the old save to `.bak`, the tmp file into place, and
  removes the `.bak`; `Save_recover` finishes or undoes a cut swap at boot and
  before each copy and launch (it drops a second name left by a cut inside
  `f_rename` by its entry alone). A full card or a bad readback shows the write
  error and keeps the old save; the swap needs room for the file twice. The
  pending-save record stays until the copy checks out (ec2076e).
- **Changed:** a blank chip (all FF or all 00) or one identical to the file
  copies nothing; one that shares under 1/8 of the file's data bytes asks
  before it is written over it. A game missing from the save table (type 10)
  gets no blank 64 KB `.sav` at launch: the file is made when its chip differs
  from blank (ec2076e).

## v0.4.3

- **Changed:** a game list redraws only the rows whose text, marks or detail
  changed when the index changes under it, instead of every visible row
  through the refresh pool, so a launch or a name for an off-screen game
  redraws nothing and a found name no longer shows old and new rows together
  (`fc5b3af`, `3380b2b`).
- **Added:** a rename's check of the whole card for another game with the save
  shows a "Checking saves" progress dialog once it passes a third of a second,
  so a big card doesn't look frozen (`fc5b3af`, `3380b2b`).
- **Added:** a launch keeps the big game the background was reading: its CRC so
  far goes to `/torii/crc.part` and the next boot goes on from there, so a 32 MB
  game isn't started over at every short visit. New games after Recent's are
  read smallest first, by size class, and a game the card can't read is tried
  once a boot, not after every walk (F2; `d30126d`).
- **Changed:** the names the background finds go into the index in place,
  32 at a time or after 10 s, one per job step, instead of a walk of the card
  each time; one walk follows the run. The cursor stays on its game on every
  list (Favourites too) across any number of new indexes, as the page keeps its
  game's path, and `library_old_path` is gone (F2; `9fc6f29`).

## v0.4.2 (2026-10-21)

New games get their names and box art on the cart: CRCs in the background,
the full art pack, and names-update's dates kept across a time-zone skew.

- **Fixed:** the walk keeps a `crc.txt` date up to 14 whole hours off its
  file's, as names-update writes it from a PC in another local time (DST),
  and takes the cart's date; any other date still forgets the CRC (F1; `e04827c`).
- **Added:** games added to the card get their CRC, and with it their No-Intro
  name and box art, in the background after the boot's walk (Recent's first),
  with no PC tool or launch; a launch stops it and the next boot goes on. The
  game lists keep the cursor on its game when new names sort them again
  (F2; `6ab146f`, `e86624d`).
- **Added:** the full art pack, `/torii/art.pak` (`just art-pack`, once):
  covers for every No-Intro game libretro-thumbnails has, so a new official
  game gets its box with no PC. Looked up after custom art and
  `art/<CRC32>.bin`; its index is read into PSRAM and its cluster link map
  built a few clusters a frame, so a cover reads at its offset with a quick
  seek (F3; `b3abac5`, review fixes `8b4a77f`).
- **Fixed:** a split art pack never stalls a frame: with no link map, a seek
  walks 16 clusters a step, and the map holds 254 pieces, not 62 (`3e6f3dd`).

## v0.4.1 (2026-10-21)

The new menu: a carousel home, game lists, SD card, NOR, Settings and Info
pages, rename, light and dark themes, a library index, No-Intro names and box
art, and the fixes from the first cart tests.

- **Fixed:** SD read errors reach FatFs: a save or ROM the card can't read
  stops the launch with an error, so nothing is copied over the good save,
  and a NOR write stops instead of burning bad data. Settings are kept as
  two copies with a sequence number and CRC, both written each save, so a
  power cut can't wipe them and older kernels read the current ones; a failed
  write says so and is retried. Smaller: the hotkey capture's B, the Time
  card pausing the library, a deleted log reopened, a dropped NOR entry
  logged once, the launch hook restoring the VBlank handler (E1; `a71b042`).
- **Fixed:** an SD rename survives a power cut and rolls back a failed move;
  the shared-save check sees games outside the index; large `names.txt` files
  are read; a write-protected card doesn't rescan every boot; paths are
  canonical; clashing hotkeys from older kernels fall back to defaults; the
  cheat parser keeps spaces and terminates its keys; same-named games in
  different folders are told apart; NOR rows show custom and database names
  (E2; `1d880bd`).
- **Fixed:** Favourites open fast with thousands of entries; names.txt and
  crc.txt work no longer stalls the menu; popups and confirmations open on
  time during a library scan, clip cleanly and keep the selected row; keys
  mashed around popups and page changes act as pressed; the SD popup offers
  both Clean boot and Boot with addon; the SD item count stays within a
  frame (E3; `6d07d0c`).
- **Fixed:** the theme wipe survives a slow frame; a launch goes to the
  loading card after 3 black frames instead of 8; covers keep their colours
  through fades; Start on waits for the library on a first boot; Diagnostics
  passes show in green; the NES placeholder's torii is redrawn (E4;
  `4befcfb`).
- **Removed:** dead code (unused icons, loaders, return values, the GBA
  game-code column of `names.db` and `crc.txt`); the tools report a
  read-only or full card clearly (E5; `0d6f91e`).
- **Docs:** source comments point to the new design docs
  (`docs/design/engine.md`, `docs/design/v0.4/decisions.md`) and the
  hardware notes instead of the deleted planning logs; no code change
  (`2441c25`).

- **Changed:** Start on only picks which carousel item home faces at
  power-on; it never opens a page. Its choices are Last place and the items
  home shows (NES only with NES games, Recent and Favourites only while
  they hold games); a chosen item that isn't shown falls back to Last
  place, then the first item. Turning away from the boot item and back no
  longer rewrites `recent.txt` (D1).
- **Changed:** one placeholder box per console, drawn by the kernel and
  redesigned (a light left band with the system's name, a soft torii);
  `just art-scrape` no longer builds card placeholders and deletes the
  `box-*.bin` files it made (D2).
- **Changed:** a game without box art shows a placeholder box per console
  (GBA's left band, GB and GBC's top band and badge, NES's black box) with a
  muted torii skeleton where the art would be (redrawn in D2). The
  Settings, Info, NOR, SD card, torii and 8x8 GBA icons are redrawn (C2;
  `cd8ddf0`).
- **Changed:** Settings writes a value when it is confirmed (A on an edit,
  OK on the time card, an accepted hotkey), once the page is still, with
  "Saving..." in the hint bar; leaving never pauses on black. Last place is
  now the carousel item last faced: boot starts on home facing it, never
  inside a page (C3; `55c2c88`, `6ebdeb6`, `61767bd`).
- **Changed:** L and R no longer page game lists or the SD card page: hold
  L and press A to start the selected game with a hard reset (C1).
- **Changed:** a game with a save shows a small floppy mark beside its size
  instead of an unexplained dot (C1).
- **Added:** Settings' GBA boot mode (With addons or Clean) sets how A, Play
  and Launch start GBA games; Game info's Boot mode overrides it per game,
  kept in byte 1 of the game's `/SAVER` `.mde` file, which v0.3 ignores (C1;
  `f6a6e66`, `ba54472`, `44ed091`).
- **Changed:** romhacks keep their own names and art: a game is named and
  boxed by its No-Intro CRC only, never by the GBA game code a romhack shares
  with its base game; the library index is rebuilt once. Info's Controls says
  Rename gives any game, a romhack too, a name of your own (C4; `a44cb04`,
  `87386be`, `93434a6`).
- **Removed:** the direct save/load patch: Save states always opens the
  in-game menu (Save, Load, Reset, Cheats). A game written to NOR by an older
  kernel with Save states alone keeps its old patch until written again
  (C5; `448bb16`).
- **Changed:** the Hotkeys rows are Back to menu, Menu keys, Sleep and Sleep
  keys, with accurate explanations and Info text (C5; `448bb16`, `9209f0d`).
- **Changed:** A, START and SELECT pressed while a screen goes out, in the
  gap, or during the open zoom's sprite phase are dropped, so a quick A, A on
  home no longer starts a game unseen; once the page shows they act (B20;
  `665de8b`, `b5db4bf`).
- **Fixed:** an empty SD folder shows only B in the hint bar; past the
  library's 4,096 games a console list's last row says "Showing the first
  4,096 games" (B20; `1d1146b`, `26b8886`).
- **Changed:** Info's Controls and In game texts follow every page's final
  keys (B20; `171ab18`, `7c1207c`).
- **Removed:** dead code from before v0.4: unused functions, the tab
  transition, list icons, view spec ids, look fields, the thumbnail and
  Mode B globals, and comments naming old screens (B20; `a6d8513`).
- **Added:** Rename in the game and SD file popups, with an on-screen
  keyboard (10-column QWERTY grid with digits, Latin-1 page, L/R caret,
  SELECT shift, START OK). A game's Rename sets its name in
  `/torii/names.txt` and re-sorts its list at once; an SD file's renames the
  file with its save, save type, save states, cheats, patch cache and custom
  art, and Recent, the favourites, names.txt and the index follow. A failed
  move is undone, and a journal (`/torii/rename.txt`) finishes or undoes a
  rename a power cut interrupted at the next boot. With the extra popup item,
  a Delete confirmation that doesn't fit beside a tall popup closes the popup
  first (B18; `0a0d254`, `38389d6`, `7593f09`, `7aa020f`, `8f69930`,
  `dc0840c`, `f81f161`).
- **Added:** box art: List + art shows a game's cover in place of the
  placeholder: custom art by file name (`/torii/art/custom/<file name>.bin`),
  then matched art by CRC (`/torii/art/<CRC32>.bin`) or GBA game code from
  crc.txt; the card's art is listed once into PSRAM so lookups never read
  the card; covers load after the cursor rests and stay cached in PSRAM;
  Settings' Look gains Box art (On/Off). `just art-add ROM_OR_FILE_NAME
  IMAGE SD_PATH` makes custom art from any picture. Covers go to BG tiles
  from a raw asset format the GBA copies as is (B22; `19dc20f`, `e877511`,
  `7b0dabb`, `739c7e5`, `b768626`).
- **Added:** opening an item zooms its carousel object into the page: the
  object grows to 1.9x and fades into the paper as the backdrop's tint goes,
  the title's heading slides in from the left, then the card rises 12 px and
  fades in; B plays it backwards (B19, E7; `ed91130`, `2304e03`, `52d12f8`).
- **Added:** the theme switch is a top-down wipe behind a hard edge: the old
  colours are copied back in at the edge's line by HBlank DMA armed from a
  VCOUNT interrupt (164 bytes of IWRAM); Settings' Theme row previews with it
  (B19, R7; `ed91130`, `2304e03`, `52d12f8`, `5c45fef`, `e3e833c`).
- **Changed:** questions, errors and notices in a session of their own rise
  in from black over 6 frames (the loading card shows at once); a dialog up
  when its screen changes goes to black with it; the unused mosaic
  transitions are gone (B19, E6, E9-E11; `ed91130`, `2304e03`, `e3e833c`).
- **Fixed:** a register hold left over from an earlier page no longer stalls
  the screen and its sprites behind later uploads (B19; `ed91130`).
- **Added:** game lists: one page for Recent, Favourites and each console,
  8 rows of 14 px with the name shown, a favourite's star, a save dot and the
  size (Recent: when it was played); A-Z letter jumps with the letter card;
  A plays with the enabled addons (L held: a hard reset of the row shown);
  START opens the game popup: Play, Favourite in place, Clean boot, Boot
  with addon (the cheat screen when Cheats is on and the game has a cheat
  file; greyed out with every addon off), Game info (LEFT/RIGHT change the
  save type, kept in its `.mde`) and Delete (Cancel preselected; the save
  stays). List with art shows the placeholder box. The cheat screen comes
  back in the page look. The library index keeps each game's display name
  (LIBRARY_VERSION 3: every card is indexed again once) (B13; `4787903`,
  `6f94faf`, `9d8ea25`, `a433d64`, `aa2932d`, `b9cb2b5`, `ff4a788`).
- **Added:** the Settings page: one scrolling page of section cards whose
  edges and labels scroll with the rows, the selected row's explanation in
  the hint bar; A edits a value in place as « value », Time and Date in a
  field card, hotkeys by pressing the combination (refusals say why), LED
  colours as swatches; the theme and the LED preview live; Rescan library
  and Identify library run from it; L/R jump a section. Values are written to
  the kernel NOR once, as the page is left, and only for a change; the log is
  flushed as it opens (B16; `10be693`, `ccda3b2`, `4dc755c`, `11b37d2`,
  `302cfd5`, `96fee8a`).
- **Added:** the SD card page: the card's folders as they are, raw names,
  folders first then files A-Z, sizes and item counts, path pills in the
  title bar, up to 1,024 files (folders past it stay reachable), hidden
  entries unless Show all files is on, the cursor kept per folder. B goes up
  a folder, then home; A opens a folder or starts a game with the enabled
  addons (L held: a hard reset); START opens Launch, Clean boot (while an
  addon is on), Write to NOR (and with addon), Add to favourites, Delete
  (asks, Cancel preselected; the save stays; never a folder) and File info.
  The listing goes to PSRAM through `sdlist.c`; `nameindex.c` and `sort.c`
  go (B14; `085922f`, `62b3b1a`, `ee4a522`, `b1d8d78`, `fabb55f`,
  `665034c`).
- **Added:** the NOR page: one card with the storage line (used, and free
  of 64 MB) over a bar of each game's share, and the games in written order;
  Format all in a card of its own. A launches (L held: a hard reset, as in
  v0.3); START opens Launch, Game info and Delete. Delete takes only the
  latest game, asks first with Cancel preselected and checks the NOR still
  holds that game; Format all asks first, with the game count and its 4
  minutes. A write to NOR now updates home's NOR count; an older kernel's NOR
  list entries are cut at their 100th byte (B15; `1d7ff84`, `4cca8fe`,
  `bd09e1e`, `65fe71f`, `d8f6d4b`, `f40afb0`, `16f632e`).
- **Added:** home is the carousel: the items' objects on a ring of affine
  sprites seen from above, the front one lowest and largest, the others
  smaller and fogged toward the backdrop, over soft ground shadows; the
  backdrop takes the facing item's tint over a low horizon; under the ring
  the item's icon, name and count, and a dot per item. LEFT/RIGHT turn in 10
  frames, a held key glides, the ring wraps, and four or fewer items each
  show once at rest; A opens the facing item. The red torii glyph sits top
  left and the clock top right (B12; `e6fdd7c`, `0a5afe8`, `db631eb`,
  `d080e82`).
- **Added:** the Info page: Controls, In game, Your cart and About open
  reading pages (UP/DOWN scroll, L/R page, B back); In game follows the
  hotkey settings as games are patched; Your cart shows what the card, NOR
  and lists hold; About shows the version, build ID, cart revision, FPGA
  firmware, the card's size and free space, and the credits; Diagnostics runs
  from its row (after any library write finishes) and B comes back (B17;
  `b5b99ad`, `e8d765e`, `89445e5`, `15ae5e2`).
- **Fixed:** a reading view at its last scroll no longer reports a move on
  DOWN; Diagnostics' log blanks a line leaving its 32-line window, so a
  cached row can't show old text (B17; `b5b99ad`).
- **Added:** names database: `just names-update SD_PATH` downloads
  libretro-database's No-Intro dats (CC BY-SA 4.0) on the PC and writes
  `/torii/names.db` and the card's CRCs to `/torii/crc.txt`; the library walk
  names games by CRC, then GBA game code, and sorts by those names; Identify
  library and the games on Recent get CRCs in the background after boot
  (only on cards with names.db, crc.txt or art) (B21; `81b80a7`, `ff4968d`,
  `1d1f80a`, `0fecaec`, `7d60271`, `ac754b9`, `4359e99`).
- **Changed:** CRC32 runs from an EWRAM table a word at a time: 17 cycles a
  byte instead of 39 (B21; `81b80a7`).
- **Fixed:** a safe file over its size cap counts as a failed read, so
  nothing writes over it (B21; `4359e99`).
- **Changed:** the new menu replaces the v0.3 screens (T1): home lists the
  items (Recent, Favourites and each console while they hold games, then SD
  card, NOR, Settings, Info) until the carousel, each opening a page frame
  (title bar with the item's icon, name and clock, a card, the hint bar); B
  goes back facing the item; the menu starts where Start on says, Last place
  from `recent.txt`'s `@boot` line; the clock shows nothing while it isn't
  set; the library walk keeps running (B10; `671db53`, `5f46d9c`, `4e3b7cf`).
- **Changed:** launches, write to NOR, deleting a file and the NOR's launch,
  delete and format move into a screen-free `actions.c`; a deleted file
  leaves Recent, the favourites and the library index at once, and a folder
  is refused. The SD list, the NOR list, Recently Played, SETTINGS, HELP and
  its QR code are gone, with the NOR menu's load and back-up of a save
  (B10; `671db53`, `4f7406d`, `8552a32`, `a59065a`).
- **Changed:** dialogs and the loading card are floating cards: a hairline
  and shadow, the title in the heading font, key chips, and a progress bar
  on the card (B10; `671db53`).
- **Changed:** a screen's hidden first draw stops at 230,000 cycles, leaving
  entrances 38,000 cycles of margin (B10; `07a4a64`).
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
