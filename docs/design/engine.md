# The tile engine

The design rules of the menu's renderer, built in v0.3 (mode 0 replaced the official kernel's mode 3 menu) and extended in v0.4. Each module's header in `source/engine/` and `source/ui/` documents its API; this file keeps the rules that span them. The research behind it is `docs/research/architecture.md`; v0.4's UI on top of it is [v0.4/decisions.md](v0.4/decisions.md).

## Frame

- A fixed 60 fps loop. Each frame wakes from `VBlankIntrWait`, then commits the previous frame's shadow state in VBlank, reads input, updates motion, runs jobs for the rest of the budget, and draws. Draw never touches hardware registers, and commit never calls widget code.
- The engine's IRQ handler runs from IWRAM and counts VBlanks; motion advances by the VBlanks since the last frame, clamped to 3. Any IRQ that can run during an SD transfer runs from IWRAM only.
- When idle (no motion, job, input or dirty state) the loop skips draw and commit and only halts.
- **Jobs:** slow work runs as state-machine jobs within the frame's cycle budget (timed with the TM2/TM3 cascade), at least one step a frame. A step is at most one SD command, an `f_read` of a few KB with the `FIL` kept between frames, or N `f_readdir` entries.
- **Targets:** input reaches the screen within a frame, scrolling never drops one, and a screen change takes under 100 ms, not counting SD reads.

## Text

- Glyphs are drawn straight into VRAM BG tiles by an ARM blitter in IWRAM (word read-modify-write, column-major window tiles), in palette-index colours, so theming is a palette change and nothing is redrawn.
- Lists aren't double-buffered: new rows are drawn off-screen in the wrapping map ahead of the scroll. v0.4's 14 px rows use a pixel-pitch tile canvas on BG1 scrolled by BG1VOFS, with the selection pill as sprites under the text (research override R5).
- Whole-page changes are drawn hidden and shown in VBlank, so half-drawn text never appears.
- Names are cut and measured by UTF-8 character (`source/charset.c`); a name too long for its row ends in an ellipsis. Full names of any length live outside EWRAM (PSRAM), so long names list, open, launch and save.
- Fonts: two proportional 1bpp fonts with width tables, body on a 12 px line and headings at 16 px, covering Latin-1 (`just font`, `docs/research/fonts.md`).

## Views and widgets

- A screen is a `const` layout in ROM: an array of `ui_spec`, one per widget, with fixed rectangles and no layout pass. Each widget kind has an ops table in ROM.
- Nodes come from a static pool with an arena per view; a view stack pushes and pops dialogs and overlays, and the top view is modal.
- Lists draw from a model (`count`, `fetch`, `prefetch`); the widget owns only its scroll position, cursor and row slots, so 10 items and 10,000 cost the same. `fetch` never calls FatFs or switches the ROM page while a commit is pending; slow data comes through jobs. A row cache keyed by item sits in EWRAM.
- Nodes, windows, views and motions have explicit caps; hitting one gives a readable error.
- IWRAM holds only the blitter, the IRQ handlers, the commit path, the launch copy's VBlank hook and the theme wipe's raster split.

## Motion and input

- **Tweens** (fixed duration, a 256-entry easing table) drive fades and fixed transitions; **springs** (critically damped, 16.16) drive anything the user can interrupt, and keep their velocity when retargeted.
- Motion drives BG scroll, sprite position and affine scale, palette fades and blend alpha. Per-line effects use HBlank DMA from a table (the theme wipe), not HBlank IRQs.
- **Input:** repeat state per key at 60 Hz. A tap acts on the frame it's pressed; holding repeats after 15 frames and ramps to 30 a second; paging keys repeat on their own schedule. A move past a list's end stops there; a fresh press at the end wraps, a held key's repeats don't (`input_move`). Timings are theme data (`source/engine/input.h`).

## Resources

- Allocators for BG character and map blocks, sprite tiles, tagged palette banks and the 32 affine matrices; a view owns what it allocates, and freed items are ref-counted with deferred removal.
- Shadow OAM and affine buffers are rebuilt every frame and copied in VBlank. One VBlank commit queue, capped by bytes and by VCOUNT; work left over waits a frame.
- A budget checker checks each layout up front against VRAM, both palettes, sprite tiles, affine matrices and PSRAM.
- Covers are pre-converted tiles cached in PSRAM (`source/engine/cover.h`) and copied into BG tiles.

## The hardware budget

GBATEK and Tonc; every layout is checked against it.

- **VRAM:** 64 KB for BG maps and tiles, 32 KB for sprite tiles.
- **Colours:** 255 BG colours shared by every BG, and another 255 for sprites.
- **Effects:** one blend mode and one coefficient pair per frame, no sprite-to-sprite alpha, one mosaic size, two rectangular windows plus the OBJ window; 128 sprites, 32 affine matrices, 1,210 sprite cycles a scanline.
- **Blending** takes only a first target over a second, so two screens can't cross-fade and planes of one screen don't blend with each other; sprites pop rather than fade where a dialog with sprites fades.
- **The window and blend-target registers are write-only:** the engine keeps their state in its shadow copy and never reads them back.

## FatFs

FatFs runs with `FF_FS_LOCK 0`: nothing stops a delete freeing a file another `FIL` holds open, so code that deletes checks the open files itself (`tlog_open_now` before an SD delete). Every job step and every FatFs call is whole on the one thread, so `FIL`s kept across frames don't interleave mid-call.

The vendored FatFs carries one Torii addition, `f_readdir_sub` (marked "Torii" in `source/ff15/ff.c`), which the library walk needs: keep it across a FatFs upgrade. The SD driver's `Read_SD_sectors` reports no errors (it returns 0), so FatFs never sees a failed card read and the "read failed" branches above it are untested on hardware.

## Testing

- **Goldens:** each scenario is an mgba-shot `-i` file that waits on `screen ready: NAME`; animations are hashed every frame with PNGs for key frames; the clock is pinned. A mismatch writes actual, expected and diff PNGs to `dist/shots-failed/`.
- **Cycle budget:** `frame-cycles: NAME N` lines, counted with cascaded timers, fail over 280,896 cycles a frame or more than 10% over `tests/perf/budget.txt`.
- **On the cart:** Diagnostics' renderer step (`source/diag_render.c`) times the glyph, the scroll step, a cover, and PSRAM copies and writes with the same timer code, and flags figures more than 20% above the emulator's.
- Hardware-free engine code (motion, input, list maths, budget checks) has host tests under ASan and UBSan.
