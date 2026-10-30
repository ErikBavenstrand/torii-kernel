# The screenshot viewer (v0.9.2)

The owner's ask (roadmap v0.9.2, 2026-10-27): a Screenshots entry in each game's popup opens that game's shots from `/torii/screenshots` in a built-in full-screen viewer that steps with LEFT and RIGHT; the SD card page opens any `.bmp` in the same viewer.

## Which shots are a game's

The in-game menu's shots are filed at the next boot (`source/shotstore.c`) as

    /torii/screenshots/<stem> YYYY-MM-DD HH-MM-SS.bmp     (-2, -3 ... within one second)
    /torii/screenshots/<stem>-N.bmp                       (N from 2: no clock)

where `<stem>` is the session's game name: the launched file's name without its folder and extension, cut to 47 bytes on a whole UTF-8 character (`INGAME_NAME_BYTES`), for an SD game and a NOR game alike (a NOR game's name is the file it was written from).

So the names already say whose a shot is, and the viewer matches by them; nothing new goes into the files, and every shot taken since v0.8 shows. A shot is game G's when its name, without `.bmp`, is G's stem followed by exactly one of the two suffixes above (`shotname_parse`, `source/shotname.c`, host-tested). The suffix's form is strict, so `Mario` doesn't take `Mario 2 2026-...` or `Mario-2-2`.

What that means:

- A game's SD file and its NOR copy share their shots, as they share the stem; so do two files of one name in different folders, or with different extensions (`Tetris.gb`, `Tetris.gbc`). That is a feature for the NOR copy and harmless for the rest.
- Renaming a game's file leaves its shots under the old stem: they no longer show in its popup, and still do on the SD card page. A rename could carry them along (a follow-up).
- Names past 47 bytes match on the cut stem, as they were filed.

## Finding them fast

Listing `/torii/screenshots` costs a directory read (an `f_readdir` per name, a sector per three or so long names): fine when the viewer opens, too slow for every popup. So the first popup of a session reads the folder once into a count table, CRC32 of each stem to its count, sorted, in PSRAM page 2 (`shotlist_count`); each popup then looks its game up in it. Shots appear only at boot, so the table stays right for the session, apart from what the menu itself changes in the folder: the viewer's Delete updates it, and the SD card page's deletes, renames and moves forget it (`shotlist_forget`, called from `actions.c`). PSRAM keeps its contents across a reset, so the boot that files new shots forgets it too (`shotstore_file`); a game over 16 MB overwrites it anyway. The popup's count counts every shot (up to 8,192); the viewer lists the first 1,024. A CRC collision only shows a wrong count: the viewer lists by the exact stem.

A popup whose game has shots gets **Screenshots** after Saves, with the count as its value. The games pages (console lists, Recent, Favourites) and the NOR page show it. The first popup of a session reads the folder (4 shots: 76,000 cycles in the emulator).

## The viewer

Its own screen in the menu's session, as the cheat screen is (the page's views popped, `ui_leave(UI_TR_DEEPER)` in and out), then the page again on the same row.

- The picture fills the screen; a smaller picture sits centred on black.
- An overlay strip at the bottom: the shot's date and time (from its name, "28 Oct 2026 14:03"; a file not named as a shot shows its file name) at the left, "3 of 12" at the right. It shows on opening and after each step, and goes after 2 seconds (120 VBlanks); A shows or hides it.
- LEFT and RIGHT step to the older and newer shot (game shots newest first; an SD folder's `.bmp` files in the SD page's order), held keys repeat on the paging schedule, and a fresh press at the end wraps (`input_move`'s rule).
- START asks "Delete this screenshot?" (the confirmation, danger ink); yes deletes the file and shows the next one, or leaves when none is left.
- B goes back.

The SD card page: `.bmp` files list without Show all files (they are files the menu opens now); A on one opens the viewer on it, stepping through that folder's `.bmp` files; its popup gets **View** first.

## Showing a full-colour picture on a tile engine

The engine has no bitmap mode, and mode 3 would need the whole of BG VRAM plus the sprite tiles' lower half, with the overlay and the confirmation drawn some other way. So the picture is an **8bpp tile layer**, and the rule that every screen draws through the tile engine holds:

- A new widget, `UI_PICTURE`: 30 x 20 8bpp tiles (600 tiles, 38,400 bytes) on its own plane, every tile its own, in row order. A view with one sets its plane's 8bpp bit in BGCNT (`ui_view.bpp8`, as lists set `tall`). The viewer puts it on plane 1, whose character block 1 reaches 48 KB; the overlay goes in front of it on plane 0, which the delete confirmation takes while it is up.
- Colours: an 8bpp tile's pixel n is BG colour n. The UI owns banks 0-2 (colours 0-47), and a confirmation over the picture takes two banks of its own (`confirm_init`), so the picture gets banks 3-13: **176 colours**, colours 48-223.
- **Quantising** (`source/picture.c`, host-tested). The file is read once: each line is converted to RGB15, kept in PSRAM page 2, and written as tiles in the colours met so far (a 32K-entry table, RGB15 to index + 1). A GBA screen is RGB15, and most frames use fewer than 176 colours, so that is usually all. A picture with more (blending, a fade, a photo) is counted again from the kept lines at 4 bits a channel (at most 4,096 colours, listed where the tiles go until they are written), cut into 176 boxes by median cut (the box with the most pixels times its longest side is split at its colours' median, along that side), each box's colour its pixels' mean, and its kept lines written again. Counting at 4 bits keeps the cut quick (it measured 5-6 M cycles on 2,900 colours at 5 bits); it costs a 5-bit step only where a picture has more than 176 colours. No dithering: GBA art is flat colour, where dithering only adds noise.
- RGB15 from 8 bits is the top 5 bits, which inverts `ppu_bmp_row`'s expansion: a Torii shot's colours come back exactly as the game drew them.

## BMP files

`picture.c` reads the header and the rows through a read callback (the kernel's is FatFs; the host tests' a buffer):

- Taken: `BITMAPINFOHEADER` and its later versions (40, 52, 56, 108, 124 bytes), 1, 4, 8, 16, 24 and 32 bits a pixel, uncompressed (`BI_RGB`) or with bit masks (`BI_BITFIELDS`, any contiguous masks), top-down or bottom-up.
- Refused, the picture black and the overlay saying why in place of the date (it stays up): "Can't show this kind of picture" (`STR_SHOTS_UNSUPPORTED`) for RLE and JPEG/PNG-in-BMP, OS/2 headers, more than 8,192 pixels a side, rows over the read buffer (16 KB: 5,461 pixels at 24 bits); "Not a picture, or damaged" (`STR_SHOTS_BROKEN`) for a file that isn't a BMP or ends early.
- Sizes: 240 x 160 shows as it is; a smaller picture is centred on black; a larger one is shrunk to fit, keeping its shape, by nearest sampling (a 480 x 320 mGBA capture shows whole at half size, a 256 x 192 one at 213 x 160). Only the rows it samples are read (row by row when it skips rows, in 16 KB runs otherwise).

## Memory

- `pReadCache`, `READCACHE_SHOTS` (all 128 KB within one decode, the rest while the viewer runs): within a decode, the colour table (64 KB, from 0, over the stage: no job step runs inside the call); while the viewer runs, from `0x10000`, its state: the tiles (38,400 bytes, read by the VRAM queue after the call), the palette, the list's order and sort keys, the read buffer and the `FIL`. The viewer starts no job and runs no listing.
- PSRAM page 2, selected only inside the viewer's and the count table's calls, never across a FatFs call (page 0 stays the window's otherwise): the cache of 32 decoded pictures (tiles and palette) from 0, the picture being decoded as RGB15 lines (75 KB) from `0x140000`, the list's names (256 bytes each, up to 1,024: a game with more lists its first 1,024 in the folder's order) from `0x200000`, and the count table (up to 2,048 games, 8,192 shots) from `0x300000`.
- EWRAM: 16 bytes more for the pages' shared state (the popups' count texts and their one more item).

## Stepping

A step decodes inside one call, with no frame drawn, so the old picture stays until the new one is ready; then the picture's banks go black, its tiles go through the VRAM queue (its cap raised to 40 KB a VBlank while they land, so one or two frames), and the new colours come in once they have landed. The step's time is logged (`shots: NAME: ... in N frames`).

Decoded pictures stay in a cache of 32 in PSRAM page 2. Resting on a picture for 12 frames (no key held) decodes the next one the way the last step went into it (`shots: ahead:`), so the usual look-then-step shows at once: a picture from the cache costs a PSRAM copy (about 150,000 cycles, half a frame). A key pressed during that decode waits for it.

The hot loops (a 24-bit row to RGB15, counting, mapping to tiles, the median cut's passes) are ARM code in IWRAM (`PIC_HOT`, 1.3 KB: 6,628 bytes stay free in the test kernel); the rest runs from ROM.

## Strings

`[shots]` in `lang/en.ini` (and `sv.ini`): the overlay's position ("{1} of {2}"), the date and time ("{1} {2}", the date as `lang_date` writes it with the year after it), the delete question, its text, button and failure, the two refusals, and an empty list's message. The popup's item is `STR_GAMES_POP_SHOTS` (`LANG_POP_SHOTS`), and the SD popup's View `STR_SD_VIEW`.

## Tests

- `tests/host/test_picture.c`: headers (taken and refused), every bit depth and both row orders against a reference, fitting and centring, the exact path and median cut at 5 and 4 bits (at most 176 colours, every pixel mapped to a near colour), a Torii shot round-trip (`ppu_bmp_row` then decode gives the RGB15 back).
- The same file's `test_shotname`: stems, matching, splitting a name and the order (`source/shotname.c`).
- Goldens (`tests/shots/shots-*.opts` and `sv-shots-*`), on fixtures in `tests/shots/data/`: the popup's entry with its count, the viewer with and without the overlay, a step, a read-ahead, the delete question and a delete, a small centred BMP from the SD card page, an unsupported one's message, the SD popup's View.
- `frame-cycles:` lines for the viewer's frames (`shots-show`, `shots-overlay`) recorded in `tests/perf/budget.txt`. The decode is not one frame and logs its frames instead.
- `tests/shots/data/bmp_fixtures.py` writes the fixtures.

## Measurements

The emulator, on the goldens' card (`shots-game`, `shots-sd`), each picture's first decode:

| Picture | Colours | Cycles | Frames |
| --- | --- | --- | --- |
| 240 x 160, 24 bits (a Torii shot) | 16, exact | 2.57 M | 10 |
| 240 x 160, 24 bits, smooth gradients in every channel (the worst case) | 2,881 at 4 bits, cut to 176 | 8.54 M | 31 |
| 64 x 48, 4 bits, centred | 16, exact | 1.48 M | 6 |
| any, from the cache | | 0.15 M | 1 |

Of a Torii shot's 2.57 M: the SD reads about 0.7 M, the rows' conversion 0.7 M, the tiles 0.8 M, keeping the lines in PSRAM 0.23 M, clearing the table 0.1 M. On the cart the SD reads 115 KB at 1,710 KB/s (67 ms, about 1.1 M cycles), so a new shot takes about 11 frames there, and a cached or read-ahead one about one. The worst case is a photo-like picture; a GBA frame with fades and blending has far fewer 4-bit colours.

Frames while the viewer runs: `shots-show` (a picture landing) 90,291 cycles, `shots-overlay` (the strip in or out) 98,971.

## Follow-ups

- A rename of a game's file could rename its shots along (their stem).
- The read-ahead blocks keys while it runs (up to its picture's decode); a decode split into job steps would not, at the cost of keeping the table and the stage apart across frames.
- Pictures past 176 colours band where a picture has smooth gradients; ordered dithering would hide it for photos, at a cost per pixel.
