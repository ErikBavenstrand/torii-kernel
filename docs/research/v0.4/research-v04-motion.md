### v0.4 motion on GBA hardware: carousel, open zoom, theme wipe, popups

> **Answer:** Most of the design fits 60 fps with little CPU. The limits are in the hardware. (1) Five double-size 64x64 affine OBJs cost 5 x 266 = 1330 OBJ cycles on every line their 128-px boxes share, over the 1210 budget, so OBJs vanish. Use double size only for the zooming object; the ring at scale <= 1.0 then costs 6 x 138 = 828. (2) Fog must use per-object palette banks, because there is one BLDALPHA pair and OBJs never blend with each other. (3) A palette swap per scanline cannot make a per-pixel 4 px dither, so the wipe's dither edge becomes a few interleaved lines or a hard edge. (4) A title bar that slides sideways on the same plane as a card that rises needs a scanline split or a plane of its own. The popup and dim mostly reuse what `ui_run.c` already does, but WIN0 is currently the list's clip window.
> **Confidence:** 🟡 medium. The cycle formulas come from GBATEK and match the mGBA renderer the tests run on (🟢). The DMA-per-HBlank figures are derived from timing tables, and mid-line palette behaviour can only be checked on a real cart, because mGBA draws each line whole at HBlank start.

**Findings: hardware numbers**
- 🟢 OBJ budget per line: 1210 cycles, or 954 with DISPCNT bit 5 (H-Blank Interval Free). A normal OBJ costs its width n in cycles; an affine OBJ costs 10 + 2n, where n is the doubled width when double size is on [1][5]. mGBA implements exactly this (`common.c`: `cycles = width`, or `10 + width * 2` with the width doubled for double size; budget `OBJ_LENGTH = 1210`), charges OBJs in OAM order, and stops drawing once the budget is spent [5]. So **lower OAM index wins, and the emulator shows overflow**: test it in goldens.
- 🟢 Torii leaves bit 5 off (`DISP_MODE0_OBJ1D 0x0040` in `ui_run.c`), so its budget is 1210. Keep it off: OAM is only written in VBlank anyway (gfx_commit) [6].
- 🟡 Off-screen OBJs earlier in OAM still cost cycles. Hidden entries (attr0 bit 9 without affine) cost nothing; `gfx_oam_end` hides unused entries, which is correct [1][5].
- 🟢 A line is 1232 cycles, and a frame 280,896 cycles (the perf budget's figure). The HBlank flag stays 0 for 1006 cycles, so roughly **224-226 cycles of HBlank are usable**; mGBA uses HDRAW 1008 / HBLANK 224 [1][2][5].
- 🟢 OAM has 32 affine matrices; their 8.8 values are stored in the entries' fourth halfwords, as `gfx.h` mirrors (`GFX_AFFINE_MATS 32`). Tile-mode OBJ VRAM is 32 KB (1024 4bpp tiles) [1][3][6].
- 🟢 DMA time is 2N + 2(n-1)S + 2I. HBlank start timing is 2; DMA0 has the highest priority [1].
- 🟡 Palette RAM has a 16-bit bus: a 32-bit write costs 2 cycles, plus 1 when it collides with the PPU. IWRAM reads cost 1 cycle; EWRAM 32-bit reads cost 6. So a 32-bit DMA runs at about **3 cycles a word (1.5 a colour) from IWRAM, or about 8 a word from EWRAM**. In ~220 cycles that is at most ~70 words (~140 colours) from IWRAM, or ~27 words (~54 colours) from EWRAM, less the DMA's own start. This is derived from the GBATEK timing table, not measured [1].
- 🟡 The CPU can write palette RAM during HDraw, at the cost of a wait. The change takes effect from the pixel being drawn at that moment, so a write that lands mid-line shows as a split line. This is widely reported but not quoted from an owning source this round. **mGBA cannot show it**: it renders the whole line at HBlank start (`video.c` `_startHblank` calls `drawScanline`, then runs HBlank DMA). Goldens therefore never catch tearing; only a real cart does [5].
- 🟢 Semi-transparent OBJs (OBJ mode 1) always alpha-blend, with the one global BLDALPHA, against the BG pixel below them. OBJs are merged into one layer first, so they never blend with each other. BLDY scales only the layers set as first targets. BLDCNT holds one first-target set and one second-target set, and a layer may be in both. With windows on, each window region's bit 5 must enable effects [1][4].
- 🟢 Butano's practice matches: a pool of 32 affine matrices; double size turned on automatically only when the transformed corners leave the box; pa/pd taken from a 1025-entry reciprocal LUT indexed by an 8.8-quantized scale (so no division, and scale steps of 1/256); a 2049-entry sine LUT [3].
- 🟢 Butano's scanline effects are an IWRAM ARM HBlank ISR that writes `src[vcount+1]`, one halfword per effect, at most 6 effects. It also switches its own VRAM commits from DMA to CPU while they run. Butano does not swap whole palettes per line [3].
- 🟡 Tonc: affine OBJs have about 1 px of positional uncertainty from the hardware's half-pixel offset from the mathematical centre, and double size moves the origin to the centre of the doubled box [4].

**Findings: what the engine already has**
- 🟢 `affine.c`: a quarter-wave sine table in 4.12 (65 entries, interpolated), plus `affine_rotscale` (a 0x10000 / zoom division per axis) and `affine_set` into shadow OAM. It is enough for uniform scale. A reciprocal LUT would remove the 2 divides per object (about 12 a frame, cheap either way) and match butano's quantization [6].
- 🟢 `gfx.c`: shadow registers including windows, BLDCNT/BLDALPHA/BLDY and mosaic; two 512-colour palette buffers (unfaded and faded) with per-bank dirty bits; OAM rebuilt between `gfx_oam_begin`/`gfx_oam_end`; and a VRAM queue of at most 16 KB a frame, in 1 KB chunks, starting no copy after line 225. The commit DMA runs in IWRAM (`gfx_commit_queue`) [6].
- 🟢 `ui_run.c` `regs_update`: WIN0/WIN1 already clip up to `UI_SCROLLERS = 2` lists, with WIN0 going to the top view's list. The blend logic is one effect at a time: BLDY darken for DIM, transitions and dims under modals (first targets = the lower planes + BD), and BLDALPHA for dialog fade-ins and page reveals. `BLEND_BRIGHTEN` is defined but not used [6].
- 🟢 The IRQ path: `torii_irq` (`crash.s`) counts VBlanks, then hands off to libgba's dispatcher. Only `IRQ_VBLANK` is enabled, and there is no HBlank or VCOUNT handler and no HBlank DMA anywhere in `source/` [6].
- 🟢 The perf budget shows page draws are heavy: `lists-page` 196k, `lists-screen` 214k and `lists-menu-open` 204k cycles in a frame. A zoom that draws its page during the animation has to spread that work out [6].

**Conflicts**
- ⚖️ Spec vs hardware, double size: the spec says "5 affine OBJs (64x64 4bpp, double-size)". Five double-size boxes, each 128 lines tall and centred near y 70, all cover about lines 10-138: 1330 cycles > 1210 there, more with the 6th object entering during a step and with shadows. Leaning: double size off for ring slots (scale <= 1.0 fits the 64x64 box when scaled about its centre), on only for the zooming front object.
- ⚖️ Spec vs hardware, the "4 px dither edge": a top-down palette swap changes whole lines, so per-pixel checker dither is impossible without pixel-timed mid-line writes (4 cycles per pixel, impractical). Leaning: a "line dither" (2-4 interleaved lines of old and new near the edge) or a hard edge.
- ⚖️ The spec's dim is "50 % toward bg", but BLDY only goes to white or black. In light, white is within about 1.5 levels (of 31) of paper #F7EFE7 at EVY 8, and in dark, black is close to shadow #101010. Leaning: BLDY brighten in light, darken in dark; the error isn't visible.

**Open questions**
- ❔ The real-cart behaviour of HBlank DMA into palette RAM at the wipe's edge (start latency, PPU stalls, whether ~32 words land before HDraw). It needs a cart test, because mGBA can't show tearing.
- ❔ Whether GBATEK states the +1 wait and mid-line palette visibility verbatim (the fetches didn't return that section).
- ❔ How the 1.9x upscale and the 0.6x/0.89x downscales of 52 px pixel art read on the cart's screen (dropped outline rows at 0.6). This is a look judgment to make after the first build (the v0.4 design is provisional).

---

### Details by question

**1. Carousel (B12)**
- OBJ cycles per line, worst lines (y about 42-96, where all object boxes overlap):
  - spec as written: 5 x 266 = 1330 (+266 for the entering 6th, + shadows), **over 1210 → OBJs drop**;
  - ring without double size: 5 x 138 = 690, 6 x 138 = 828 during a step; + 2 overlapping shadows as normal 64-wide OBJs (64 each) = 956; + 16x16 icons on other lines. This fits 1210 with ~250 to spare, but not 954, so keep bit 5 off;
  - affine shadows sharing the item matrices would cost 138 each (~1100-1240 on shadow lines). Use normal (non-affine) shadow OBJs in 3-4 pre-drawn widths, or affine shadows on the three front items only.
- OBJ VRAM: 6 slots x 2 KB = 12 KB (5 visible + 1 entering during a step), shadows ~1 KB (4 widths of 64x8 at 256 B), 16x16 icons ~2 KB, box art 2 KB: ~17 KB of 32 KB. Items are any number (T10, more in v0.6), so use a slot ring: on a press, queue the entering item's 2 KB into the slot just freed. It is ~512 words, well inside the 16 KB commit cap and a few lines of VBlank. The entering object starts at -108° (hidden), so a 1-frame upload delay never shows. Swapping the front object's tiles isn't needed: the front changes by moving OAM entries, not tiles.
- Matrices: one per visible object (≤ 6) + the zoom reuses the front's: ≤ 7 of 32. Shadows don't need matrices if they're normal OBJs.
- OAM order: front object first (lowest index = on top among equal priorities and charged first, so overflow would cut the far ones), then by depth, shadows after the objects at a lower priority.
- Fog: per-object OBJ bank (6) + shadows (1) + label icon/dots (1-2) ≈ 9 of 16 OBJ banks. Lerping 6 x 15 colours a frame costs ~3-4k cycles (motion.h has the palette fade maths) plus a 192-byte commit. The alpha alternative fails: one EVA/EVB for all OBJs, no OBJ-on-OBJ blending, and it would take the one blend the label fade wants. The label fade itself can also be a palette lerp toward the backdrop, which leaves BLDCNT free.
- Fixed point: x = 120 + 98 sin a and feet y = 100 - 30(1 - cos a) through `affine_sin` (4.12) are fine. Scale 0.42 + 0.58 cos a → pa = pd = 256/scale; quantize scale to 8.8 and read the inverse from a reciprocal LUT (butano), or keep the divide.
- Shimmer and jitter. At rest the three scales are constant, so there is no shimmer, but nearest-neighbour downscaling to 0.6 drops ~40 % of rows and columns: 1-px outlines break. During a step the scale changes every frame and the sampled texels "crawl"; that can't be fully avoided over 10 frames. Mitigations:
  - anchor each object's feet: compute the screen row of the feet texel from the quantized pa (centre + (feet_row - 32) x 256 / pa), and round that once, so the feet don't bob 1 px against the shadow;
  - use even box sizes and a consistent rounding of the 0.5 px affine centre offset [4];
  - optional mip: a hand- or tool-downsampled 32x32 copy per item, used below a scale threshold (~0.7) as a 32x32 affine OBJ (74 cycles, 512 B a slot). It keeps outlines at the far slots, at the cost of a visible switch when an item crosses the threshold. Decide after seeing the first build.
- CPU per step frame: 6 objects' positions and matrices (~1-2k), fog lerps (~4k), backdrop tint lerp, and the label: well under 20k of 280k cycles. CPU is not the constraint; OBJ line cycles are.

**2. Open zoom and close (B19)**
- Turn double size on at f0 while the scale is still 1.0 (it then looks identical, just costs 266) and move x,y by -32 in the same frame, so there's no one-frame jump. Scale to 1.9 keeps 52 x 1.9 ≈ 99 px inside the 128 box. Hide the other ring objects (or fog them out) at f0: OBJ cycles are then 266 + shadow, trivial.
- Fog-out of the front object is the same per-bank lerp toward the page surface, while the backdrop tint lerps to the surface. There's no blend in f0-5.
- f5-12: BLDALPHA with first = BG1|BG2, second = BG2|BG3|BD (BG2 in both), EVA 0→16 and EVB = 16 - EVA. Text blends with the card under it, and the card with the surface. This is not a true group fade (text fades toward card colour, not the surface) but reads as one; `ui_run.c` already builds this kind of BLDCNT for reveals. Turn the front OBJ off at f5, because a semi-transparent or blended OBJ would take part in the same blend.
- **Title bar vs the card rise:** the rise is a BG VOFS change of 12 px on BG1/BG2, and the slide is an HOFS change. If the title bar lives on BG1/BG2, either both move together (wrong) or the planes need a split at the bar's bottom line. Options: (a) draw the title bar on BG0 during the open (BG0 is free unless a popup is up); (b) a VCOUNT IRQ at the bar's last line rewrites BG1/BG2 HOFS/VOFS (~4 register writes, a few hundred cycles a frame, a small IWRAM handler); (c) slide the bar by redrawing its map columns (8-px steps only, too coarse). Leaning (a).
- The page's content must be drawn before f5: a page draw costs ~196-214k cycles (perf budget), and its tiles are up to 16 KB, the whole per-frame commit cap. Draw it into the hidden planes in f0-4 (it fits beside the zoom's ~5k cycles), and hold the reveal until its queue ticket is done (`gfx_queue_done`). Measure `frame-cycles` of f0-f5.
- Close (10 f): play it in reverse. The page planes fade out with BLDALPHA, then the front object comes back from 1.9 to 1.0 out of the fog; turn double size off on the frame it reaches 1.0.

**3. Theme wipe (B19)**
- Mechanism for a hard edge at line E, once a frame: VBlank commits the **new** palette (lines 0..E-1 show it). A VCOUNT-match IRQ at line E-1 (or E-2, for slack) arms DMA0 in HBlank mode, non-repeating, copying the **old** colours. It fires in that line's HBlank, so line E onward shows the old theme. The next VBlank re-commits the new palette. Cost: one IRQ (~100-200 cycles including BIOS and libgba dispatch) and one DMA of k words a frame.
- **Size the copy to the HBlank:** pack every colour that differs between themes into one contiguous run per palette (B1's `tools/palette.py` owns the layout). BG banks 0-3 are ≤ 64 colours = 32 words ≈ 100 cycles from IWRAM, which fits in ~220. Adding the OBJ banks (fogged items, shadows, icons, ~9 banks, ~140 colours) does not fit one HBlank: do BG on line E and OBJ on line E+1 (invisible at 1 line), or swap the OBJ banks at VBlank only on the Settings page, where OBJs are just icons. B19 triggers the wipe from `settings_page.c`, even though motion.png draws it over the carousel.
- Source location: an IWRAM block of the old theme's run (≤ 128 B, or 256 B for both themes when dithering) plus a handler of ~64-128 B ARM: ~200-400 B against the ~2.4 KB IWRAM margin. From EWRAM only a run of ≤ ~24 words fits.
- "Dither": interleave 2-4 lines around E (new/old/new/old), with one DMA per line. That needs re-arming each line: from an HBlank IRQ enabled for those lines only, or a repeating DMA0 over a small table (dest reload, source incrementing) stopped by a second VCOUNT IRQ. Each extra line costs another ~100 cycles of DMA and IWRAM for the table. Per-pixel 4 px dither is not possible.
- Risks: (1) the CPU is stalled during a long DMA3, such as a 1 KB VRAM chunk (~2 lines) or an SD/PSRAM copy, so the VCOUNT IRQ can run late and miss the line; then the edge jumps for a frame. Arm 2 lines early, and keep big DMA3 copies out of the wipe's frames. (2) The IRQ handler must be IWRAM-resident and disabled when the wipe ends: menu ROM can be paged away during launches. (3) None of this shows in mGBA (whole-line render), so the edge's cleanliness is a cart checklist item.
- Alternatives: windows with two BG sets don't work: mode 0 has only 4 BGs and the pages use all 4, and each theme would need duplicate maps with other palette banks. **A cheap fallback with no HBlank work** is a top-down wipe through black or white: WIN0 grows from the top over 8 f with BLDY = 16 inside it (per-window effect bit), the full palette swaps in VBlank at mid-point, then WIN0 shrinks from the top over 8 f. It keeps the top-down motion, with a hard edge and a dip. It collides with the list's WIN0 clip while a list is on screen; see question 4. The last resort, per T18, is an instant switch.

**4. Popup and dim (B11)**
- WIN0 open: WIN0V grows from the notch row up and down over 6 f (ease-out quad), and WIN0H follows the card as BG0 HOFS slides 8 px. Inside WIN0: BG0 + page layers + OBJ + effects; outside: page layers without BG0. These are all shadowed registers, one commit a frame, costing ~0 cycles.
- **Window conflict:** `regs_update` gives WIN0 to the top view's list and WIN1 to a second list. With a popup, WIN0 must be the popup's (it has priority over WIN1). Move the list clip to WIN1, and allow only one clipped list while a popup is open. With two scrollers (list + art) and a popup, drop the lower list's clip.
- Dim: BLDY with first targets BG1|BG2|BG3|OBJ|BD (BG0 excluded), EVY 0→8 over the open. Use BRIGHTEN in light and DARKEN in dark. BG0's transparent pixels (round corners, shadow) then show correctly dimmed page. `ui_run.c` already dims modals with BLDY on `lower | LAYER_BD`; it only lacks the light theme's brighten. Any OBJ belonging to the confirmation would also be dimmed: keep confirmation graphics in BG0 tiles.
- Blend exclusivity: the dim (brightness mode) can't coexist with an alpha fade. The popup opens by window (no alpha), so this holds. Don't add an alpha fade-in to the confirmation.

---

### Recommendations for tickets B11, B12, B19

**B12 Home carousel**
- Approach: affine 64x64 OBJs **without double size** for ring slots (scale ≤ 1.0); front object first in OAM; 6 tile slots (2 KB each) as a ring, with the entering item's tiles queued on the press; one matrix per object; shadows as **normal** OBJs in 3-4 pre-drawn widths (or affine on the front three only); fog and label fade as palette lerps; pa from a quantized scale (a reciprocal LUT is optional); feet anchored from the quantized pa.
- Budgets: worst OBJ line ≤ ~960 of 1210 cycles; OBJ VRAM ≤ ~17 KB of 32; ≤ 7 matrices; ≤ ~9 OBJ banks; CPU < 20k cycles per step frame; one 2 KB upload per step.
- Measure first: build a 6-object + shadow test scene at the worst step frame. Check in mGBA, which models the 1210 budget, that nothing drops: hash the frame against the same scene with the far objects removed. Measure `frame-cycles` of a step and a held turn. Look at the 0.6x slot on screen to decide on a 32x32 mip.
- Risks: OBJ line overflow if double size stays on (the spec's text must change); broken outlines at 0.6x; 1 px feet bob without anchoring.
- **Design change needed:** spec line 21 "double-size" → "double size only while zooming"; ground shadows not affine (or only the front three).

**B11 Popups, confirmations and the letter card**
- Approach: the popup owns WIN0 (the list clip moves to WIN1), WIN0V eased from the notch, BG0 HOFS + WIN0H for the 8 px slide; the confirmation dim is BLDY with BG0 excluded, BRIGHTEN in light and DARKEN in dark, EVY ramped to 8.
- Budgets: no CPU or VRAM cost beyond drawing the card (BG0 tiles ~8 KB, already planned); 6 and 4 frames are register writes only.
- Measure first: a golden with a popup over a scrolled list, to check the WIN1 clip still holds; the dim colour in light against the paper bg.
- Risks: two clipped lists plus a popup need 3 windows (only 2 exist); OBJs inside a confirmation get dimmed; a popup on BG0 at the same time as anything else using BG0 (the title bar trick in B19).

**B19 Motions**
- Open/close: double size on at f0 at scale 1.0 (with x,y -32); fog-out by palette; other objects hidden; the page drawn into hidden planes in f0-4 and revealed when its queue ticket is done; f5-12 BLDALPHA, first BG1|BG2, second BG2|BG3|BD; the title bar on BG0 during the open (or a VCOUNT split). Measure `frame-cycles` for each open frame: the page draw (~200k) must not land in the same frame as anything else heavy.
- Theme wipe, in order of preference:
  1. hard-edge split: VCOUNT IRQ at E-2 → HBlank DMA0 of one contiguous run of theme-dependent colours (≤ 32 words BG, from IWRAM), the OBJ run on the next line;
  2. "line dither" of 2-4 interleaved lines if (1) is clean on the cart;
  3. the window + BLDY dip wipe (no HBlank work);
  4. an instant switch (T18).
  Budgets: ~200-400 B IWRAM (handler + table) of the ~2.4 KB margin; ~300-500 cycles a frame.
- Measure first: `just size` for IWRAM after adding the handler; the DMA's cycles (`-r` registers, or a cycles mark around a test copy); a **real-cart check** of the edge for tearing, since mGBA can't show it (add to `docs/release-checklist.md`).
- Risks: a late IRQ during DMA3 or SD transfers; the window conflict for the dip fallback while a list is clipped; the IRQ handler must not run while the kernel ROM is paged out.
- **Design changes needed:** "4 px dither edge" → "line-dithered or hard edge"; "title bar slides in" needs the bar on BG0 (or a split) during the open; palette layout must put theme-dependent colours in one contiguous run (affects B1's `palette.py`).

---

**Sources**
1. [GBATEK: LCD OBJ overview, OBJ affine parameters, LCD timings, LCD I/O interrupts and status, DMA transfers, colour special effects](https://problemkaputt.de/gbatek.htm) · 3.06 · primary (fetched through a summarizer; quotes slightly paraphrase-prone)
2. [Tonc: video, DMA](https://www.coranac.com/tonc/text/video.htm), [dma](https://www.coranac.com/tonc/text/dma.htm) · undated · primary tutorial
3. butano source, master 2026: `hw/include/bn_hw_sprite_affine_mats_constants.h`, `src/bn_sprite_affine_mats_manager.cpp`, `include/bn_affine_mat_attributes.h`, `include/bn_reciprocal_lut.h`, `include/bn_sin_lut.h`, `hw/src/bn_hw_hblank_effects.s`, `src/bn_core.cpp` · primary
4. [Tonc: affine sprites](https://www.coranac.com/tonc/text/affobj.htm), [graphic effects](https://gbadev.net/tonc/gfx.html) · undated · primary tutorial
5. mGBA as checked out in the repo: `emu/mgba/src/gba/renderers/common.c`, `video-software.c`, `src/gba/video.c`, `include/mgba/internal/gba/video.h` · primary (code)
6. Torii: `source/engine/affine.c/.h`, `gfx.c/.h`, `frame.c`, `motion.h`, `source/ui/ui_run.c`, `source/crash.s`, `tests/perf/budget.txt`, `docs/design/v0.4/spec.md`, `motion.png`, `docs/design/v0.4/decisions.md` · primary (code and design)

*deep · 2 rounds · ~20 sources read · 2 sonnet researchers + own reading of repo and mGBA*
