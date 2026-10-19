// Diagnostics' renderer step (docs/plans/v0.3-decisions.md, Testing: "On the
// cart"): times the engine's drawing and asset paths on the cart with the
// engine's cycle counter (engine/cycles.h), logs each figure as
// "frame-cycles: NAME N", and flags any more than 20% above the figure the
// emulator recorded for it (tests/perf/budget.txt, compiled in as
// source/perf_budget.h). Then, among the questions, a ten-second
// scroll-and-fade demo to watch for tearing.
//
// The figures, each its own NAME in the budget (the emulator's come from the
// diag-renderer golden scenario, which runs this step):
//   diag-glyph          one body-font glyph (text_blit), best of 4
//   diag-row38          a 38-glyph list row's text, best of 4
//   diag-cover-decode   a 64x64 4bpp cover's tiles (2 KB) through the BIOS's
//                       LZ77 and into PSRAM at WAITCNT_SLOW, as a cover job's step
//   diag-cover-upload   the same 2 KB from PSRAM to VRAM by DMA at WAITCNT_MENU
//   diag-psram-slow     8 KB written to PSRAM by DMA at WAITCNT_SLOW (psram_write's way)
//   diag-psram-menu     the same at WAITCNT_MENU, a probe: writes at the
//                       menu's speed aren't confirmed on the cart, so it reads
//                       them back (at WAITCNT_SLOW) and says if any went wrong
//   diag-scroll         the worst frame of a list gliding a row a frame (the
//                       fastest a theme's top_speed allows)
//   diag-page           the worst frame of a list's page change
// The timed parts run with interrupts off, so a VBlank doesn't land in them.
// The PSRAM tests use the last 64 KB of the 32 MB PSRAM (page 0x3000, as the
// PSRAM step does), which only a 32 MB game reaches and every launch fills
// again; nothing here writes either NOR.
#include <gba_compression.h>
#include <gba_interrupt.h>
#include <stdio.h>
#include <string.h>

#include "diag_priv.h"
#include "Ezcard_OP.h"
#include "ez_define.h"
#include "lz77.h"
#include "perf_budget.h"
#include "tlog.h"
#include "waitcnt.h"
#include "engine/cycles.h"
#include "engine/frame.h"
#include "engine/motion.h"
#include "engine/psram.h"
#include "engine/res.h"
#include "engine/text.h"
#include "ui/ui.h"

#define DMA3SAD (*(vu32 *)0x040000D4)
#define DMA3DAD (*(vu32 *)0x040000D8)
#define DMA3CNT (*(vu32 *)0x040000DC)
#define DMA_ON_16 0x80000000u
#define DMA_ON_32 0x84000000u

#define PSRAM_SCRATCH_PAGE 0x3000
#define PSRAM_SCRATCH (PSRAM_WINDOW + 0x7F0000u)  // the last 64 KB of the 32 MB
#define COVER_BYTES 0x800                         // 64x64 4bpp tiles
#define PROBE_BYTES 0x2000

extern u8 pReadCache[];

// --- Figures -------------------------------------------------------------

static u32 budget_of(const char *name)
{
	for (u32 i = 0; i < sizeof(perf_budget) / sizeof(perf_budget[0]); i++)
		if (!strcmp(perf_budget[i].name, name))
			return perf_budget[i].cycles;
	return 0;
}

// Logs a figure and shows it against the emulator's; what is a literal (the
// summary keeps it)
static void figure(const char *name, const char *what, u32 cycles)
{
	cycles_report(name, cycles);
	u32 emu = budget_of(name);
	if (!emu) {
		diag_say(WHITE, "%s: %lu (no emulator figure)", what, (unsigned long)cycles);
	} else if ((unsigned long long)cycles * 5 > (unsigned long long)emu * 6) {
		diag_say(YELLOW, "%s: %lu, %lu%% of the emulator's %lu", what, (unsigned long)cycles,
			 (unsigned long)((unsigned long long)cycles * 100 / emu), (unsigned long)emu);
		tlog("diag: renderer: %s is more than 20%% above the emulator's figure", name);
		diag_add_slow(what);
	} else {
		diag_say(WHITE, "%s: %lu, emulator %lu", what, (unsigned long)cycles, (unsigned long)emu);
	}
}

static u32 blit_cycles(const struct text_run *run)
{
	u32 best = 0xFFFFFFFF;
	for (u32 i = 0; i < 4; i++) {
		u16 ime = REG_IME;
		REG_IME = 0;
		u32 t0 = cycles_now();
		text_blit(run);
		u32 t = cycles_since(t0);
		REG_IME = ime;
		if (t < best)
			best = t;
	}
	return best;
}

// Writes bytes to PSRAM at dst with DMA3 in 1 KB pieces (as psram_write) at
// waitcnt; returns the cycles
static u32 psram_write_at(u16 waitcnt, u32 dst, const void *src, u32 bytes)
{
	if (!psram_holds((const void *)dst, bytes))
		return 0;  // never: the scratch area is inside the window
	u16 ime = REG_IME, old = REG_WAITCNT_CART;
	REG_IME = 0;
	REG_WAITCNT_CART = waitcnt;
	u32 t0 = cycles_now();
	for (u32 done = 0; done < bytes; done += 1024) {
		DMA3SAD = (u32)src + done;
		DMA3DAD = dst + done;
		DMA3CNT = DMA_ON_16 | 512;
	}
	u32 t = cycles_since(t0);
	REG_WAITCNT_CART = old;
	REG_IME = ime;
	return t;
}

// Halfwords of PSRAM at p that differ from want, read at the slow WAITCNT
static u32 psram_differ(u32 p, const u16 *want, u32 bytes)
{
	u16 waitcnt = WAITCNT_SLOW();
	u32 bad = 0;
	for (u32 i = 0; i < bytes / 2; i++)
		bad += ((const vu16 *)p)[i] != want[i];
	WAITCNT_RESTORE(waitcnt);
	return bad;
}

// --- The demo list -----------------------------------------------------------

#define DEMO_ITEMS 1000

static char demo_name[64] EWRAM_BSS;
static char demo_detail[12] EWRAM_BSS;

static const char *const words[] = {"Advance", "Castle", "Dragon", "Echo",   "Forest", "Golden",
				    "Harbor",  "Island", "Jungle", "Knight", "Legend", "Moon"};

static u32 demo_count(void *ctx)
{
	(void)ctx;
	return DEMO_ITEMS;
}

static int demo_fetch(void *ctx, u32 i, struct ui_item *out)
{
	(void)ctx;
	snprintf(demo_name, sizeof(demo_name), "%s %s %lu%s.gba", words[i % 12], words[(i / 12 + 5) % 12],
		 (unsigned long)i + 1, i % 7 == 3 ? " - The Long Journey (Rev 1)" : "");
	snprintf(demo_detail, sizeof(demo_detail), "%lu MB", 4ul << (i % 4));
	out->text = demo_name;
	out->detail = demo_detail;
	return UI_ITEM_READY;
}

static const struct ui_list_model demo_model = {demo_count, demo_fetch, 0};
static const struct ui_label_config demo_title = {"Renderer demo", UI_FONT_HEADING, UI_ALIGN_LEFT, 0};
static const struct ui_list_config demo_list = {&demo_model, 16, 0, 0, 40};

enum {
	D_BAR,
	D_TITLE,
	D_LIST,
	D_COUNT
};

// In place of Diagnostics' own view (both lists don't fit in BG VRAM):
// chrome on plane 2, the list on plane 1
static const struct ui_spec demo_nodes[D_COUNT] = {
	[D_BAR] = {.kind = UI_PANEL,
		   .id = 1,
		   .parent = UI_NONE,
		   .plane = 2,
		   .anchor = UI_HFILL,
		   .paper = UI_ROLE_PANEL,
		   .h = 16},
	[D_TITLE] = {.kind = UI_LABEL,
		     .id = 2,
		     .parent = D_BAR,
		     .plane = 2,
		     .anchor = UI_FILL,
		     .paper = UI_ROLE_PANEL,
		     .data = &demo_title},
	[D_LIST] = {.kind = UI_LIST,
		    .id = 3,
		    .parent = UI_NONE,
		    .plane = 1,
		    .anchor = UI_FILL,
		    .paper = UI_ROLE_BG,
		    .y = 16,
		    .data = &demo_list},
};

static const struct ui_view_spec demo_view = {
	.name = "diag-demo", .nodes = demo_nodes, .count = D_COUNT, .id = 33, .focus = UI_NONE};

static struct ui_view *demo_push(void)
{
	diag_screen_hide();
	struct ui_view *v = ui_push(&demo_view, 0);
	if (!v) {
		diag_screen_show();
		tlog_error("diag: renderer: %s", ui_error());
		return 0;
	}
	for (u32 i = 0; i < 60 && !(v->flags & UI_V_SHOWN); i++)
		ui_frame();
	diag_pause(10);
	return v;
}

static void demo_pop(void)
{
	ui_pop();
	diag_screen_show();
}

// The worst frame of the next frames frames
static u32 worst_frame(u32 frames)
{
	u32 worst = 0;
	while (frames--) {
		ui_frame();
		if (frame_last_cycles() > worst)
			worst = frame_last_cycles();
	}
	return worst;
}

// --- The step ------------------------------------------------------------------

// A list row's worth of text: 38 glyphs, none wider than 8 px
static const char row38[] = "Super Mario Advance 4 - Mario Bros. 3!";

static void time_text(u32 *vram)
{
	struct text_window win = {vram, 30, 2};
	struct text_line line;
	text_clear(&win, 0, 0, 240, 16, 0);
	text_layout(&font_body, row38, sizeof(row38) - 1, 240, &line);
	struct text_run row = {vram, 16, &font_body, line.glyphs, line.count, 1, 2, 1};
	struct text_run one = {vram, 16, &font_body, line.glyphs + 1, 1, 3, 2, 1};  // 'u' at x 3
	u32 glyph = blit_cycles(&one);
	u32 row_cycles = blit_cycles(&row);
	figure("diag-glyph", "glyph", glyph);
	figure("diag-row38", "38-glyph row", row_cycles);
	diag_say(GREY, "(in the row, %lu cycles a glyph)", (unsigned long)(row_cycles / line.count));
}

static void time_cover(u32 *vram)
{
	// 2 KB of tiles: the body font's bitmaps, which pack like small pictures
	const u8 *tiles = (const u8 *)font_body.bits;
	u8 *packed = pReadCache, *stage = pReadCache + 0x8000;
	u32 packed_bytes = lz77_compress(tiles, COVER_BYTES, packed);
	SetPSRampage(PSRAM_SCRATCH_PAGE);
	u16 ime = REG_IME;
	REG_IME = 0;
	u32 t0 = cycles_now();
	LZ77UnCompWram(packed, stage);
	int written = psram_write((void *)PSRAM_SCRATCH, stage, COVER_BYTES);
	u32 decode = cycles_since(t0);
	t0 = cycles_now();
	DMA3SAD = PSRAM_SCRATCH;
	DMA3DAD = (u32)vram;
	DMA3CNT = DMA_ON_32 | COVER_BYTES / 4;
	u32 upload = cycles_since(t0);
	REG_IME = ime;
	int ok = written && !memcmp(stage, tiles, COVER_BYTES) && !memcmp(vram, tiles, COVER_BYTES);
	SetPSRampage(0);
	figure("diag-cover-decode", "cover LZ77 to PSRAM", decode);
	figure("diag-cover-upload", "cover PSRAM to VRAM", upload);
	diag_say(ok ? GREY : RED, "(%lu bytes packed to %lu%s)", (unsigned long)COVER_BYTES,
		 (unsigned long)packed_bytes, ok ? "" : ", BAD DATA");
	if (!ok)
		diag_add_failure("the cover path gives back the tiles");
}

static void time_psram_writes(void)
{
	u16 *slow_pattern = (u16 *)(pReadCache + 0x10000), *menu_pattern = (u16 *)(pReadCache + 0x14000);
	for (u32 i = 0; i < PROBE_BYTES / 2; i++) {
		slow_pattern[i] = (u16)(i * 0x9E37 + 0x5A5A);
		menu_pattern[i] = (u16) ~(i * 0x7F4B + 0x1234);
	}
	SetPSRampage(PSRAM_SCRATCH_PAGE);
	u32 slow = psram_write_at(WAITCNT_BIOS, PSRAM_SCRATCH, slow_pattern, PROBE_BYTES);
	u32 slow_bad = psram_differ(PSRAM_SCRATCH, slow_pattern, PROBE_BYTES);
	u32 menu = psram_write_at(WAITCNT_MENU, PSRAM_SCRATCH, menu_pattern, PROBE_BYTES);
	u32 menu_bad = psram_differ(PSRAM_SCRATCH, menu_pattern, PROBE_BYTES);
	SetPSRampage(0);
	figure("diag-psram-slow", "PSRAM 8 KB slow", slow);
	figure("diag-psram-menu", "PSRAM 8 KB fast", menu);
	if (slow_bad) {
		diag_say(RED, "%lu halfwords wrong at the slow WAITCNT", (unsigned long)slow_bad);
		diag_add_failure("PSRAM writes at the slow WAITCNT");
	}
	// A finding, not a failure: the kernel writes PSRAM slowly until this
	// reads back right on carts
	diag_say(menu_bad ? YELLOW : GREEN, "PSRAM writes at %04X: %s", WAITCNT_MENU,
		 menu_bad ? "BAD DATA, keep them slow" : "read back right");
	if (menu_bad)
		tlog("diag: renderer: %lu of %u halfwords written at WAITCNT %04X read back wrong",
		     (unsigned long)menu_bad, PROBE_BYTES / 2, WAITCNT_MENU);
}

static void time_list(void)
{
	struct ui_view *v = demo_push();
	if (!v)
		return;
	u8 list = (u8)(v->first + D_LIST);
	ui_list_set_cursor(list, 40);  // too far to scroll: a page change
	u32 page = worst_frame(12);
	ui_list_autoscroll(list, INPUT_ONE);  // a row a frame, a held d-pad's fastest
	u32 scroll = worst_frame(40);
	ui_list_autoscroll(list, 0);
	demo_pop();
	figure("diag-scroll", "scroll frame", scroll);
	figure("diag-page", "page change frame", page);
}

void diag_renderer_step(void)
{
	diag_step("renderer");
	// Tiles nothing shows (sprite tiles, and no sprite is on), for the text
	// and the cover's copy
	u8 tiles = res_obj_tiles(COVER_BYTES / 32, 0, 0);
	if (tiles == RES_NONE) {
		diag_say(RED, "no VRAM for the renderer step: %s", res_error());
		diag_add_failure("the renderer step");
		return;
	}
	u32 *vram = res_obj_addr(tiles);
	time_text(vram);
	time_cover(vram);
	res_release(RES_OBJ_VRAM, tiles);
	time_psram_writes();
	time_list();
	tlog_flush();
	diag_pause(60);
}

// --- The demo --------------------------------------------------------------

void diag_renderer_demo(void)
{
	diag_step("renderer demo");
	diag_say(WHITE, "Watch the list scroll and fade for ten");
	diag_say(WHITE, "seconds, for tearing or stutter.");
	diag_pause(90);
	struct ui_view *v = demo_push();
	if (v) {
		u8 list = (u8)(v->first + D_LIST);
		for (u32 f = 0; f < 600; f++) {
			ui_frame();
			// Down, speeding up from 10 rows a second to 40 as a held
			// d-pad does, a fade at full speed, a brake; a page change; up
			// at 30 rows a second through a fade, and a brake
			if (f < 150)
				ui_list_autoscroll(
					list, (s32)(INPUT_ONE / 6 + (INPUT_ONE * 2 / 3 - INPUT_ONE / 6) * f / 150));
			if (f == 250 || f == 540)
				ui_list_autoscroll(list, 0);
			if (f == 280)
				ui_list_set_cursor(list,
						   ui_list_cursor(list) + 100);  // too far to scroll: a page change
			if (f == 330)
				ui_list_autoscroll(list, -(s32)(INPUT_ONE / 2));
			if (f == 160 || f == 420)
				ui_fade(FADE_BLACK, FADE_MAX, 30);
			if (f == 200 || f == 470)
				ui_fade(FADE_BLACK, 0, 30);
		}
		while (ui_fading())
			ui_frame();
		demo_pop();
	}
	if (diag_ask("renderer demo", "Did the scroll look smooth with no tearing?", ASK_CHECK) == ANSWER_NO)
		diag_add_failure("the renderer demo");
}
