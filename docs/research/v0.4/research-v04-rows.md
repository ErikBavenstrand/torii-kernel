### 14 px list rows at 60 fps on the GBA tile engine (v0.4, ticket B9)

> **Answer:** Use (a), a pixel-pitch canvas, and generalise today's ring to any row height. Row slots sit back to back, 14 px apart, inside one column-major tile canvas on BG1. The period is P = k x 14 px with k a multiple of 4: k = 12 gives 168 px, or 21 tile rows. The canvas is shown through a static 32x64 map (map row m shows canvas tile row m mod 21), and BG1VOFS scrolls it. Draw the selection pill as a few OBJs behind the text, not as BG2 tiles. This uses the same VRAM as today's 16 px ring for the same viewport, reuses `text_blit` unchanged, needs no HBlank machinery, and draws at most `step` rows a frame at about 7-12 k cycles each. Keep (b), 16 px slots squeezed by HBlank DMA to BGxVOFS, as the fallback. Reject (c) sprite text and VCount-IRQ scrolling.
> **Confidence:** 🟡 medium. The hardware facts and the engine's measured costs are solid. The 14 px canvas has not been built, and the cycle estimates for it are extrapolated from `tests/perf/budget.txt`, not measured.

**Findings: the existing engine (code read)**
- 🟢 The ring (`source/ui/ring.h`, `ui_ring.c`) accepts only `rh` 16 or 32 and viewports that are a multiple of 8. Each slot owns its own rectangle of tiles (`tw x rh/8`), and content row r draws into slot `r % k`. Its map rows `(r % (256/rh)) * rh/8` point at the slot, and BGVOFS = scroll - top mod 256. The settings are k = visible + 1 + step and step = 32/rh, so the scroll moves at most 32 px a frame. [1]
- 🟢 The geometry is tied to tile alignment in only a few places: `ring_setup`, `ring_map_row`, `row_entries` (which writes map entries per slot), the `rt = rh/8` window height, and `map_row_live`. The scroll, glide (`ring_glide`, `ring_glide_stop`), spring and prefetch maths is all in px and does not care about rh. [1][2]
- 🟢 The blitter (`text_blit.s`, ARM in IWRAM) already draws at any pixel y into a column-major window of any height (`column_words = height*8`). Each tile word is one pixel row of 8 pixels, written read-modify-write with 32-bit accesses. Rows at 14 px offsets therefore never share a VRAM word with a neighbour, only a tile. [3]
- 🟢 Measured costs (`tests/perf/budget.txt`, WAITCNT_MENU):
  - 38-glyph row: 9,498 cycles, about 250 per glyph.
  - One glyph: 353 cycles.
  - Clearing a full 30x2-tile row: 1,662 cycles.
  - Clearing an unaligned 8 px band: 3,535 cycles. That band goes through the masked `fill_part` path; whole-width bands use `fill_columns`.
  - Today's list-scroll frames: 91-127 k cycles, against a 280,896-cycle frame. [4]
- 🟢 The selection today is a palette-bank switch in the row's map entries, staged for VBlank, with no text redraw. On-screen rows that are refreshed (async names) are drawn into an EWRAM pool and queued as one contiguous VRAM copy per slot (`refresh_row`). [2]
- 🟢 The engine has no HBlank IRQ or HBlank DMA. All register writes go through the shadow plus a single VBlank commit (`gfx.h`). The list is clipped by a hardware window (WIN0/WIN1, `ui_run.c`). DMA3 does bulk copies (diag, PSRAM). [5][6]
- 🟢 The v0.4 spec asks for:
  - lists of 8 rows of 14 px, with a 12 px pill (r3);
  - Settings rows of 12 px inside section cards;
  - BG1 for text, and BG2 for cards and the pill ("palette-swapped row with round end tiles");
  - WIN0 for popups;
  - a theme wipe done as an HBlank palette swap.

  It budgets "font-rendered rows 2 layers x 8 KB". [7]

**Findings: hardware (owning sources)**
- 🟡 The CPU may access VRAM during the drawing period, at "+1 cycle if GBA accesses video memory at the same time". VRAM is a 16-bit bus, so a 32-bit store costs about 2 cycles plus any conflict. Writing a row during display is therefore cheap; the risk is visual tearing, not bandwidth. [8]
- 🟡 A scanline is 1,232 cycles: 960 of drawing and 272 of HBlank. A frame is 228 lines, which gives 280,896 cycles. [8]
- 🟢 DMA timing:
  - The transfer cost is 2N + 2(n-1)S + xI, with x = 2 normally.
  - HBlank start timing (mode 2) with Repeat re-runs the transfer at each HBlank.
  - DMA0 has the highest priority, and lower channels pause for it.

  One halfword per line to BGxVOFS therefore costs about 6-10 cycles of stall per line, or about 1-1.6 k cycles a frame. [9]
- 🟡 Tonc says HBlank DMA "only occurs on the visible HBlanks, not the ones in the VBlank". The value for line 0 must be written during VBlank and the table started at line 1. [10]
- 🟢 Sprite limits:
  - A line has 1,210 OBJ cycles, or 954 with H-Blank Interval Free.
  - A normal OBJ costs its width in cycles; an affine one costs 10 + 2 x width.
  - OBJ tiles get 32 KB in modes 0-2.
  - Wide shapes are 16x8, 32x8, 32x16 and 64x32; there is no 64x16.

  A 7-sprite 32x16 pill costs 224 cycles a line. [11]
- 🟢 Butano's `sprite_text_generator` packs variable-width glyphs into 32-px-wide sprites (32x8 or 32x16). It moves to the next sprite past 32 columns, plots straight into the sprite's tile VRAM, and can be set to one sprite per glyph. This is the reference design for (c). [12]
- 🟡 Tonc's TTE offers chr4c (column-major) and chr4r renderers. Column-major drops the loop that steps from one tile row to the next, and the ARM version is "much better" than C even from ROM. Torii's blitter is derived from `chr4c_drawg_b1cts_fast`, and earlier research measured it 3x faster than Thumb C from ROM. [3][13][14]

**Option comparison (8 rows x 14 px, 112 px viewport, text about 216-224 px = 27-28 tile columns)**

| | (a) pixel canvas | (b) 16 px slots + HDMA squeeze | (c) text on sprites | (d) VCount IRQ per row |
|---|---|---|---|---|
| Mechanism | slots 14 px apart in one column-major canvas; static map; one BG1VOFS a frame | today's ring unchanged; per-line BGVOFS table removes 2 lines from each 16 px slot | each row is 7 32x16 OBJs | IRQ at each row boundary rewrites VOFS |
| Tiles | 21 rows x 28 = 588 (18.4 KB) at k = 12 | 22 rows x 28 = 616 (19.3 KB) at k = 11, 12.5 % of it blank | about 9-10 rows x 7 x 8 = 504-560 OBJ tiles (16-17.5 KB of 32 KB OBJ) | as (b) |
| Per-frame overhead | 0 | DMA about 1-1.6 k + table build about 1.5-4 k when the scroll moves | OAM rebuild of about 70 entries | 9-10 IRQs x about 100-200 = 1-2 k, with jitter |
| Row redraw | layout + blit about 250/glyph, plus clearing 14 lines (about 1-2 k) | the same, 16 lines | the same into OBJ VRAM | the same |
| Pill | OBJs (cheap, moves by the pixel, can slide) | BG2 palette swap still works if BG2 is squeezed too (4-halfword HDMA, BG1HOFS..BG2VOFS) | OBJs | as (b) |
| Risks | ring generalisation; refresh copy is no longer contiguous; Settings cards at 12 px | a new HBlank subsystem; the theme wipe needs a second HDMA channel; everything on BG1/BG2 inside the list lines is squeezed; DMA re-armed every VBlank | OBJ VRAM and OAM clash with the carousel zoom transition (spec OBJ ~16 KB); line budget about 450-650/954 at row overlaps; BLDALPHA becomes per-OBJ | IME is off during FatFs and SD work, and late IRQs glitch visibly; rejected |

- 🟢 (a) costs no more VRAM than today. For a 112 px viewport, today's ring needs visible = 8, step = 2, k = 11, which is 22 tile rows. The canvas with k = 12 needs 21. The spec's "8 KB per text layer" (256 tiles, about 9 tile rows at 28 wide) can't hold a 112 px scrolling list under either design. [1][7] (my arithmetic)
- 🟢 Under (a), rows do not straddle the canvas wrap as long as P is a multiple of both 8 and rh. For 14 px rows that means k is a multiple of 4 (P = 56, 112, 168...). For 12 px rows k must be even, and for 16 px any k works. With P = k x rh, content y mod P equals canvas y, so content tile row t always lands on canvas tile row t mod P/8. (derived)
- 🟢 Under (a), a 32x64 map (512 px) can stay static: map row m shows canvas tile row m mod (P/8). Set VOFS = (scroll mod P) + P - top, which always lies in [P - top, 2P - top). Visible BG lines then stay below 2P + h, which is 456 for P = 168 and h = 120, under 512, so the 512 px BG wrap never cuts into the 21-row period. Scrolling then writes no map entries at all. The extra cost is one 2 KB screenblock. The alternative is a 32x32 map whose rows are rewritten when they enter view; each entry depends only on t, so re-staging a shared row is harmless. [1] (derived)
- 🟡 Rows to redraw per scroll step: exactly one new row per 14 px scrolled, and at most `step` rows a frame (2 at a 28 px/frame cap). At rest `fill_spare` pre-draws the next row above and below, so a single step from rest draws nothing that frame. Worst case is 2 x (about 40 glyphs x 250 + about 2 k clear), about 24 k cycles; a typical game name of 20-25 glyphs is about 7-8 k per row. [2][4] (extrapolated)
- 🟡 A pixel-row cache (4bpp rows kept in EWRAM) doesn't pay. One row is 14 x 28 words = 1,568 B, so 32 rows would take 50 KB of the 80 KB free. Copying a row back (about 3-4 k cycles by DMA or ldm/stm from EWRAM) saves only about half of a redraw. The k = 12 resident slots already act as the cache. If anything, cache the layout (glyph indices) per resident row. [4] (estimate)

**Conflicts**
- ⚖️ Pill placement: the spec puts the pill on BG2 as palette-swapped tiles with round end tiles [7]. At a 14 px pitch, BG2 rows are no longer tile-aligned, so a map-entry palette swap can't outline a 12 px pill. Drawing it as pixels into a BG2 canvas works, but every selection change then writes visible tiles (they must go through VBlank) and a sliding pill needs a redraw every frame. Leaning towards OBJs: a left cap, middle pieces that share one solid 32x16 tile set, and a right cap. That is 24 OBJ tiles and 224 line cycles. Set OBJ priority equal to BG2's and lower than BG1's, so the pill draws above the card and below the text. Row text on pills is always neutral ink, so no text palette swap is needed. ring_scroll_for and ring_keep_on_screen keep the selected row wholly inside the viewport, so the pill never needs window clipping; hide it during a page change.
- ⚖️ HBlank DMA during VBlank: GBATEK's DMA page doesn't say [9]; Tonc says it doesn't fire there [10]. Leaning towards Tonc, which matches common practice. This matters only for (b).
- ⚖️ OAM access during HBlank: GBATEK says OAM and OBJ VRAM need H-Blank Interval Free for HBlank access [8][9]. The engine writes OAM only in VBlank, so it doesn't matter here.

**Open questions**
- ❔ Settings uses 12 px rows inside section cards, and the cards on BG2 scroll with the rows. The text canvas works with even k, but card edges and headers at 12 px offsets on BG2 need their own answer: a BG2 canvas, or cards that pin to section boundaries.
- ❔ The refresh path (`refresh_row`) assumes a slot is one contiguous run of tiles. In a column-major canvas, a 14-line slot is 28 runs of 14 words, which overflows `GFX_QUEUE_CAP` 16. It needs a strided queue op such as `gfx_queue_copy2d(dst, dst_stride, src, src_stride, run, count)`, or the refresh must wait for VBlank.
- ❔ The spec's window use: WIN0 serves popups and WIN1 clips the list. The confirm dim, zoom and theme wipe together may need both windows at once; this hasn't been checked.
- ❔ Galmuri9's 12 px line inside a 14 px row leaves 1 px of padding top and bottom, and descenders reach the next slot's first line. That's fine for the blitter, but check the 12 px pill (r3) centring against the ink.
- ❔ No source was found for a GBA menu that uses non-8-multiple text rows (Goomba, PocketNES, EZ-Flash and the jam menus were not checked within the budget).
- ❔ The cycle figures for (a) are extrapolated, not measured. Measure them first (see below).

**Recommendation for ticket B9**
- **Approach:** (a), the pixel-pitch canvas, with an OBJ pill. Keep the ring's px maths and change only the geometry.
- **Data structures:**
  - `struct ui_ring` takes any `rh` from 8 to 32.
  - `ring_setup` rounds `k = visible + 1 + step` up so that `k * rh % 8 == 0`. For rh 14 and h 112 that gives visible 9, step 2 (cap 28 px/frame) and k 12, so P is 168 px (21 tile rows). For rh 12 and step 2, k must be even.
  - Replace `ring_rows` with `period = k * rh`. Slot s owns canvas lines `[s*rh, s*rh + rh)`, and `ring_slot` stays `r % k`.
  - The list's tiles become one `text_window {canvas, tw, P/8}`. `draw_row` clears lines `[s*rh, +rh)` and draws the name at `y = s*rh + (rh - line_height)/2`; `text_clear` already handles any y and h.
  - BG1 uses a 32x64 map written once at list init (row m shows canvas row m % (P/8)), with `vofs = (scroll % P) + P - top`. `row_entries`, `ring_map_row` and the selection restaging go away.
  - Rewrite `map_row_live` as `line_live(y0, y1)`, which checks canvas lines against the live VOFS and the window. It backs the existing asserts that a fresh slot's lines are off screen.
  - The pill becomes a `ui_pill` widget: up to 8 OAM entries (32x16, 12 px used) on one OBJ palette bank (tint), whose y = row_top - scroll + top is written in the same `gfx` shadow commit as BG1VOFS. Optionally ease its y when the cursor moves.
  - Add `gfx_queue_copy2d` for `refresh_row`; the pool is about 1.6 KB per 14 px row.
- **Estimated cycles** (budget 280,896):
  - 0 extra fixed cost a frame, against 1-5 k for (b).
  - One row: about 7-8 k typical, about 12 k for a full 40-glyph line.
  - Glide at the 28 px/frame cap: about 25 k worst case, against today's lists-scroll at 91 k.
  - Page change (9 rows while hidden): about 70-110 k, spread over frames by `ui_draw_room`.
  - Pill: an OAM update only.
- **Risks:**
  - The ring generalisation touches the core of a well-tested list, including the asserts and host tests (`tests/host` for ring.h).
  - The strided refresh copy is needed.
  - Settings' 12 px BG2 cards are unsolved.
  - OBJ priority and blending must be included in the BLDALPHA zoom and BLDY dim (set the OBJ target bits).
  - The extra 2 KB screenblock must fit the VRAM map; re-plan the spec's 28 KB figure, since text alone is about 18 KB per list.
- **Measure first:**
  1. A `tests/kernel` benchmark: clear plus draw a 25- and a 40-glyph name into a 14-line slot of a 168-line column-major canvas in VRAM, as `text-row14` and `text-clear-row14`. This confirms or replaces the 250/glyph and about 1.5 k clear figures.
  2. Host tests for the generalised `ring_setup`, `ring_resident` and `ring_glide` with rh 12, 14 and 16 and k rounding. Check that no fresh slot's lines are visible at any scroll for a step of at most 28 px.
  3. A demo list at 14 px under `just shot` and the tear watch (`-T`). Record frame-cycles for rest, a single step, a held-key glide and a page change, plus the pill's line budget with the 8 row icons on the same lines.
  4. Only if (1) or (3) blows the budget: prototype (b) with a DMA0 HBlank-repeat table in a branch, and compare.

---

**Sources**
1. [source/ui/ring.h, source/ui/ui_ring.c](/Users/erik.bavenstrand/Code/torii-kernel/source/ui/ring.h) · 2026-10 · primary (code)
2. [source/ui/ui_list.c](/Users/erik.bavenstrand/Code/torii-kernel/source/ui/ui_list.c) · 2026-10 · primary (code)
3. [source/engine/text.h, text.c, text_blit.s](/Users/erik.bavenstrand/Code/torii-kernel/source/engine/text_blit.s) · 2026-10 · primary (code)
4. [tests/perf/budget.txt; tests/kernel/test_text.c](/Users/erik.bavenstrand/Code/torii-kernel/tests/perf/budget.txt) · 2026-10 · primary (measurements)
5. [source/engine/gfx.h](/Users/erik.bavenstrand/Code/torii-kernel/source/engine/gfx.h) · 2026-10 · primary (code)
6. [source/ui/ui_run.c, source/ui/ui_core.c (window clip)](/Users/erik.bavenstrand/Code/torii-kernel/source/ui/ui_run.c) · 2026-10 · primary (code)
7. [docs/design/v0.4/spec.md; docs/design/v0.4/decisions.md (R5)](/Users/erik.bavenstrand/Code/torii-kernel/docs/design/v0.4/spec.md) · 2026-10-20 · primary (design)
8. [GBATEK: memory map / LCD timing](https://problemkaputt.de/gbatek.htm) · n.d. · primary (read through a fetch summary)
9. [GBATEK: DMA transfers](https://problemkaputt.de/gbatek-gba-dma-transfers.htm) · n.d. · primary
10. [Tonc: DMA](https://gbadev.net/tonc/dma.html) · n.d. · secondary
11. [GBATEK: LCD OBJ overview](https://problemkaputt.de/gbatek-lcd-obj-overview.htm) · n.d. · primary
12. [butano bn_sprite_text_generator.h / .cpp](https://github.com/GValiente/butano/blob/master/butano/src/bn_sprite_text_generator.cpp) · master · primary (code)
13. [Tonc: TTE](https://gbadev.net/tonc/tte.html) · n.d. · secondary
14. [docs/research/architecture.md, fonts.md](/Users/erik.bavenstrand/Code/torii-kernel/docs/research/architecture.md) · 2026 · secondary (earlier research)

*deep · 2 rounds · 14 sources read · 1 sonnet researcher*
