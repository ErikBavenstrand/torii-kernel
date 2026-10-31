# Cheat search: finding a game's values in game

An Action Replay style code finder in the in-game menu: the player remembers
the game's RAM, plays, comes back and narrows the addresses down (the same,
changed, went up, went down, or equal to a number they type) until a few are
left, then keeps one as a cheat. No flash cart we found offers this (GBAtemp,
the EverDrive GBA Pro manual). This is the design; the in-game menu's own is
[v0.8/ingame-menu.md](../v0.8/ingame-menu.md).

## Where things live

The game is frozen while the overlay runs, but everything it can use is
borrowed: 24 KB of EWRAM (the window), the OBJ tiles and cart SRAM pages
past the game's save. PSRAM is no help: **a write to PSRAM doesn't land in
game mode** (the in-game probe on the real cart, 2026-10-27), and the
emulated cart stops with status 78 on one. SD access in game mode wedges the
card. So the search lives in cart SRAM, which has no battery past the save:
a power-off loses the search and anything found, as it loses a state saved
in game.

### The snapshot: the save-state buffer

What is searched: EWRAM (256 KB) and IWRAM from `0x03000000` to
`0x03007E00` (31.5 KB), 294,400 bytes. The last 512 bytes of IWRAM are left
out: the BIOS's words, its IRQ and Supervisor stacks, and the hook's words at
`0x03007FE0`, which change with every opening of the menu.

Cart SRAM has one place that big, the save-state buffer
(`0x40000-0xAFFFF`, 448 KB). Every other free page together is under
200 KB, and the screenshots' room (192 KB) is in use when there are
screenshots. So the search **borrows the save-state buffer**:

| Offsets | Holds |
|---|---|
| `0x40000-0x7FFFF` | EWRAM as last compared (`INGAME_SEARCH_SNAP`) |
| `0x80000-0x87DFF` | IWRAM to `0x03007E00` as last compared |
| `0x88000-0x90FBF` | the candidates: a bit per element (36,800 bytes at 8 bits, half at 16, a quarter at 32) |
| `0x91000` | `struct ingame_search`, its magic last |
| `0x92000-0x9AFBF` | the candidates before the last pass, for Undo |
| `0x9B000-0xA3FBF` | the candidates the first filter left, for Compare with the first scan |
| `0xA4000-0xAFF7F` | their values at the first scan, one after another (49,024 bytes) |

The buffer's tag, mark and info (`0xAFF80-0xAFFFF`) are left alone except
that starting a search zeroes the mark and the tag's magic: Load state then
has nothing to load, and the kernel's next boot neither files the buffer
(nothing "not kept") nor takes it for the slot it preloaded (its
`data_crc` shortcut). The rules:

- **A state saved in game and not kept yet** blocks a search: its row is
  greyed, and the footer says to go back to Torii first (which files it).
- **A kept state** (the one a launch preloads) is on the card: starting a
  search drops it from cart SRAM, and in-game Load state then says to load it
  from Torii. The footer on Start says so.
- **Save state while a search runs** ends the search (the stub's SAVE writes
  the whole buffer): the overlay clears the search's magic before it returns
  SAVE, so a cut-short save never leaves a half search behind.
- **Each launch** has its own id in the session block (`launch_id`), which
  the search's head keeps: a search of an earlier launch is none. The
  kernel can't clear the head itself, as `0x91000` is inside a preloaded
  state's VRAM.

### Found cheats

`struct ingame_found` at `0x2E000`, in the backups' page window after the
screenshot's IWRAM backup (which ends at `0x2DA00`): a magic, a count and up
to 16 records (address, value, size, on now). The overlay writes a record and
then the count. Back to Torii (`ingame_after_game`) files them.

### Turning one on now: the live block

The cheat interpreter's table is in the patch area at the ROM's end, in
PSRAM, which can't be written in game. A found cheat runs every frame this
session through a **live block** of RAM the hook owns: 28 bytes after the
hook's spend area. Where the game's stack top is the BIOS's default,
`0x03007F00`, `Get_spend_address` lowers it by `0xA0` instead of `0x80`:
the spend area keeps the first `0x80`, the live block takes `0x80-0x9B`.
Other tops keep v0.9.6's `0x80` and get no live block (set once instead):
the 32 bytes more crashed Street Fighter Alpha 3 (top `0x03007EC0`) in
`compat --all`, and the spend area's words are all the stub's during the
menu (it reads its header copy after the overlay returns), so there is no
other room. `test_spend_lowering` (tests/kernel) keeps both cases. It holds a check word and three (address, value)
pairs; an address's top four bits are its width as the table's ops have it
(0 byte, 1 halfword, 2 word), 0 is an empty pair.

The interpreter (`cheatrun.s`) gets the block's address in a literal the
kernel patches in (`Cheat_live`, 0 for none) and, before the table, writes
each pair whose address is in EWRAM or IWRAM, if the check word matches
(`ingame_live_check`: a magic XOR the six words rotated): stack garbage over
the block (a game that resets its stack to the old top) writes nothing. The
pairs are constant writes, as the cheat filed for them is. The BIOS's
SoftReset (Reset game) clears the block if it is in IWRAM's last 512 bytes;
a game that clears its RAM at its start clears it too, else it runs on, as
the filed cheat would.

The live block needs the interpreter, which a launch puts in only with cheats
on and a table (`Patch_RTS_Cheat`); the session's `live_at` is 0 otherwise,
and for a NOR game written before (whose patch is burned in). Then "on now"
becomes **set now**: the overlay writes the value once as the game resumes.
Either way the cheat is filed, and picked when "on now" was chosen, so it
runs from the next launch.

## The search's speed

A pass reads the RAM, the snapshot and the candidates, compares, and writes
the snapshot and candidates back, in chunks of 512 bytes through the stub's
`sram_copy` (cart SRAM is 8 bits wide at 8 wait states). The first pass
touches all 294,400 bytes twice; later ones skip a chunk whose candidates are
all gone without reading its snapshot, so they speed up as the search
narrows. The game is frozen meanwhile; the footer says "Searching..." first.

The compare runs as ARM from the game's IWRAM, which the pass borrows as the
screenshot's packing does (kept at `INGAME_BACKUP_SHOT` meanwhile, and read
from there as searched RAM), with its chunk buffers: in Thumb from the
window the first pass took 190 frames, most of it the compare's registers
spilling to the window's slow stack; it is one copy per size so they stay
in registers. Equals compares against a chunk filled with its value, so it
reads no snapshot, and a chunk's snapshot is written back only while it
has candidates.
The EWRAM the overlay sits in is read from the stub's backup of it
(`INGAME_BACKUP_EWRAM`), so the game's values are searched there too.
Measured on the emulated cart: see the end.

## More filters (v0.9.7)

Cheat Engine's, where the GBA has use for them:

- **Up by N, Down by N:** the value went up or down by an amount since the
  scan compared with (lives down by 1). They wrap at the size, so they work
  for signed values either way.
- **More than N, Less than N, From A to B:** against numbers alone; From
  edits its two numbers one after the other.
- **Negative numbers** (a toggle): the compares are signed, two's
  complement (0 to -1 is down), the numbers are shown and typed with a sign
  (the editor's first place is plus or minus), and so are the list's values.
- **Compare with the first scan** (a toggle): Same, Changed, Up, Down and
  By compare with the values Start remembered rather than the last pass's.
  Cart SRAM has no room for a second snapshot (the first is 288 KB), so the
  first filter keeps its candidates' first-scan values as it goes (from the
  snapshot it reads), one after another in the candidates' order, with a
  copy of its candidates' bits; a later pass reads a chunk's values back by
  counting the kept candidates before it. They fit for 49,024 bytes of
  values (49,024 candidates at 8 bits, 24,512 at 16, 12,256 at 32): with
  more left after the first filter, or once the first filter is undone, the
  toggle is greyed and its footer says the first scan wasn't kept. Keeping
  them makes the first filter read the snapshot (about 22 frames more for an
  Equals).

The compare is one function a chunk (`cheatsearch_chunk`): the relative
filters against the earlier values (By against them shifted), the others
against a chunk filled with the number, all through one narrow loop that
keeps an outcome mask (equal, less, greater) with the sign bit flipped for
a signed compare.

## BCD and the pointer scan (v0.9.7)

- **Stored as: BCD (scores):** elements are two decimal digits a byte (a
  score of 1234 as `0x1234`), as many games keep scores. An element with a
  nibble past 9 isn't one and every filter drops it (`cheatsearch_bcd_drop`,
  a word at a time, from IWRAM with the compare). BCD's digits sort as its
  bytes do, so the filters compare the bytes against the numbers as BCD;
  only By moves the earlier numbers, as numbers (none past 99, 9999, ... or
  below 0). Numbers are typed in decimal (2, 4 or 8 digits), the list shows
  the digits, and a kept cheat writes the BCD. Negative numbers is greyed
  while BCD is on. A new search in the same launch keeps the last one's
  size, sign and format.
- **The pointer scan:** for a value that moves in memory (Pokémon Emerald
  moves its save blocks), Find pointers to it on a candidate's page lists
  every word of EWRAM and IWRAM that points at most 4 KB
  (`INGAME_POINTER_REACH`) below it, with the offset, up to 960
  (`struct ingame_pointers`, cart SRAM `0x2E100`). Once the value has moved
  (a door, a reload), the player finds it again with a new search, and
  Check the pointers found keeps those that point the same way at the new
  address. A pointer from the page is kept as `[POINTER]+OFFSET:VALUE`, a
  pointer code the cheat interpreter already runs (AnyGBA's raw extension),
  picked; in game it is set once, as the live block takes plain addresses.
  One level: a chain of pointers isn't followed. `tests/search.sh ptr` moves
  a block of BCD coins and keeps them through the pointer to it.
- **Room:** the glyphs moved to `0x2DA00` and the search's lines start at
  `0x3C100`, so the 58 lines fit before the state buffer.
- **Speed:** a 16-bit BCD Equals first pass takes 89 frames; the first
  filter reads the snapshot only for chunks that keep candidates.

## The compare

`source/cheatsearch.c`, hardware-free (host tests in
`tests/host/test_cheatsearch.c`, and the overlay builds it): elements are
aligned units of 1, 2 or 4 bytes, little-endian, compared unsigned. Each
pass keeps a candidate when:

| Filter | Now against before |
|---|---|
| Same | equal |
| Changed | not equal |
| Went up | greater |
| Went down | less |
| Equals N | now equals N (N cut to the size) |

and stores now as the next pass's before. Starting a search sets every
candidate; a filter can be applied at once (Equals what the screen shows).

## The pages

The menu has room for nine rows (160 px), which a game with save states,
a cheat list and the DEBUG row fills. So **Cheat search** is a fixed row on
the Cheats page (after All off and Back to my picks) when the game has a
cheat list, and the menu's own row (where Cheats would be) when it has none.
Its page:

- **No search:** Size (8, 16 or 32 bits; A cycles it) and Start search. The
  footer: what Start does, or why it can't.
- **A search:** Same as before, Changed, Went up, Went down, Equals N, Show
  them (greyed until 1-32 are left), Undo the last, New search. Each filter
  first copies the candidates to `INGAME_SRAM_SEARCH_UNDO` (`0x92000`, at
  most 36,800 bytes; about 10 frames at 8 bits), so a filter picked by
  mistake doesn't end the search: Undo puts them back (once). The snapshot
  keeps the undone pass's values, so the next filter compares against those
  (in a chunk the pass emptied, against the values before it). The footer shows how many are
  left ("1,204 left"), or "None left" or "Searching...".
- **Equals N** edits N in place: A starts the edit with the cursor on the
  last digit, LEFT and RIGHT pick a digit, UP and DOWN change it, A searches,
  B leaves N as it was. N has the size's digits (3, 5 or 10) and can't pass
  its maximum.
- **The list** (`Show them`): each candidate as `02030100  5`, its address in
  hex and its value now in decimal. A opens it.
- **A candidate:** its address as the title; Value N (edited as Equals'),
  Keep it, and Keep it, on now (or set it now); the cursor starts on Keep it. Keeping writes a found record;
  the footer then says it is filed on Back to Torii. Three on now at most.

Numbers are drawn from glyphs the kernel renders at launch (`0-9`, `A-F`,
the thousands separator, the body font): the overlay
has no font, and every other line is drawn whole by the kernel. A composed
line is a kernel line (a label like "Equals") with glyphs after it.

Back to Torii's row says it saves while found cheats wait, as for states and
screenshots.

## Filing

`ingame_after_game`, after the states and the picks: the game's cheat file
is opened as the cheat screen opens it (made if the game has none), and each
record is added with `cheatstore_add` as a code of the user's
(`cheatN_user = true`), format VBA raw (`02030100:05`, `:0005` or
`:00000005` by size), named "Found 02030100"; one turned on now is picked.
One cheat an address: a record kept again later for the same address and
size wins (the same code twice would be greyed as doing what the first
does). Then one `cheatstore_write`, power-cut safe as every cheat file write is. The
records are cleared after the write; a write the card refuses keeps them for
the next boot. A larger file (read in windows) gets them in its last window.
Rename one on the Cheats page.

## Memory

- **The overlay's extension.** The stub copies the overlay's window at
  every opening (backup, load, check), so every byte there costs the menu's
  opening: with the search in it, the panel showed a frame late (5 frames,
  `tests/menu.sh` allows 4). So the search, the screenshot and the DEBUG
  snapshot are an extension (`EXT` in `ov.c`, `.ext` in `ov.ld`), linked
  after the window and stored after the image in cart SRAM, which the
  overlay loads itself when one of them is used: the game's EWRAM there
  goes to the window's backup first (one run with it), the kernel's check
  word (`ext_check` in the session) has to match, and the overlay puts the
  game's bytes back before it returns. The ARM code that runs from borrowed
  IWRAM (the screenshot's packer and the search's compare, `.fastext`)
  comes after it in the image and is read straight into IWRAM. The core
  image is now 9,604 bytes and the window 11,472 (v0.9.6: 10,300 and
  12,080); the extension is 6.6 KB. A screenshot scans EWRAM for the game's
  display registers before the extension loads over it.
- **The menu's rows.** A game with save states and no cheat list now has
  eight rows; the lines past the seventh are drawn with the footer, a frame
  after the rest, which keeps the text within its 5 frames.
- The search's 31 lines don't fit the session block's 40 KB: they go in
  cart SRAM from `0x3C400`, after the marquee's whole names, and the glyphs
  at `0x3F000`, which the overlay reads as the search page opens. The
  overlay names every line by its place in cart SRAM.
- The kernel: a few hundred bytes for filing; EWRAM untouched.

## Tests

- `tests/host/test_cheatsearch.c`: every filter at every size, alignment,
  values at the size's edges, counting, a chunk with no candidates.
- `tests/search.sh` (a new `tests/roms/searchtest`, whose lives go down on
  A): start an 8-bit search, Equals 5, lose a life, Equals 4, one candidate
  left: the lives' address; keep it on now with 9; A in game no longer lowers
  it; Back to Torii files `Found ADDRESS` with `ADDRESS:09`, picked. It logs
  each pass's time.
- Golden shots `ingame-search` (and `sv-ingame-search`): the pages, the
  number edit, the list and a candidate.

## Not done

- Searching VRAM, palette or OAM (values are rarely kept there).
- More than one level of Undo; pointer chains (a pointer to a pointer);
  a memory view around a candidate; "on now" through a pointer (the live
  block writes plain addresses: it is set once, and runs from the next
  launch).
- Naming a found cheat in game (no keyboard in the overlay): rename it in
  Torii.

## Measured

On the emulated cart, `tests/search.sh` (searchtest, 8 bits), each pass's
frames as the overlay logs them:

| Pass | Frames |
|---|---|
| Start (remember 294,400 bytes) | 43 |
| First Equals (every element a candidate) | 68 |
| Equals with one candidate left | 9 |

A relative filter on the first pass also reads the snapshot (about 22
frames more). Not measured on the real cart: its SRAM copies run at the
same 8 wait states the stub sets, so the numbers should hold.

- **The overlay's stack:** its deepest chain is about 850 of its 1,024
  bytes (the lines' buffers kept to one a call).
- **Not tested on the real cart:** the whole feature; in particular that
  the live block's 32 bytes below the old stack top stay the hook's in every
  game with the default top, and that IWRAM borrowed for the compare goes back (it does for the
  screenshots, the same code path).
