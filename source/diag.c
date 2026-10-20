// Diagnostics (see diag.h). Every result goes to the log as a "diag:" line;
// the screen shows the same lines on the engine (source/ui), in a list that
// scrolls to the newest. The steps that need the user come last: rumble,
// the checklist from docs/release-checklist.md, checked against the log
// where it can be, the renderer demo, and a crash test.
//
// It never writes to the game saves: it reads the FRAM only, writes test
// patterns to a free SRAM page (CARTLOG_TEST_PAGE), and checks the FRAM's
// checksum is the same afterwards.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <gba_base.h>
#include <gba_compression.h>
#include <gba_dma.h>
#include <gba_input.h>
#include <gba_interrupt.h>
#include <gba_systemcalls.h>
#include <gba_video.h>

#include "ff.h"
#include "ez_define.h"
#include "ezkernel.h"
#include "Ezcard_OP.h"
#include "bootloader.h"
#include "build_id.h"
#include "RTC.h"
#include "cartlog.h"
#include "diag.h"
#include "diag_priv.h"
#include "logbuf.h"
#include "logcheck.h"
#include "lz77.h"
#include "tlog.h"
#include "waitcnt.h"
#include "engine/cycles.h"
#include "engine/engine.h"
#include "engine/motion.h"
#include "engine/text.h"
#include "ui/dialog.h"
#include "ui/ui.h"
#include "../tests/kernel/torii_test.h"

#define REG_WAITCNT_IO (*(vu16 *)0x04000204)

extern const u8 __rom_end__;
extern void SoftReset_now(void);
extern u32 gl_currentpage;

#define say diag_say
#define step diag_step
#define ask diag_ask
#define pause_frames diag_pause

// --- Screen and keys ---------------------------------------------------------
//
// One engine view: a title bar, and the step's lines in a list that scrolls
// to the newest, with no row selected. The list's count is fixed: rows past
// the last line are blank, and a new line redraws its row. The colour bars
// for the checklist are an image with its own palette.

#define DIAG_LINES 32       // lines kept: a long summary scrolls back over them
#define DIAG_LINE_BYTES 48  // about what a line holds in the body font
#define DIAG_ROWS 512       // the list's rows: more lines than a page ever has

enum {
	N_TITLE_BAR,
	N_TITLE,
	N_LIST,
	N_BARS,
	N_COUNT
};

static struct {
	char lines[DIAG_LINES][DIAG_LINE_BYTES];
	u8 ink[DIAG_LINES];
	u32 count;  // lines on this page
	char title[48];
	struct ui_view *view;
	u16 keys;  // fired since wait_key started listening
	u32 pumped;  // torii_frames when say last ran a frame
	u8 turning;  // a page change waits for page_turn
} scr EWRAM_BSS;

static u32 diag_step_no;

static u32 lines_count(void *ctx)
{
	(void)ctx;
	return DIAG_ROWS;
}

static int lines_fetch(void *ctx, u32 index, struct ui_item *out)
{
	(void)ctx;
	if (index < scr.count && index + DIAG_LINES >= scr.count) {
		out->text = scr.lines[index % DIAG_LINES];
		out->ink = scr.ink[index % DIAG_LINES];
	} else {
		out->text = "";
	}
	return UI_ITEM_READY;
}

static const struct ui_list_model lines_model = {lines_count, lines_fetch, 0, 0, 0};

// The checklist's colour bars, 240x24 px: red, green, blue and white bars 56
// px wide, as 30x3 tiles in a palette of their own (colours 1-4)
#define SOLID(c) (0x11111111u * (c))
#define EDGE(c) (0x00111111u * (c))  // its right 2 px transparent: the gap
#define T8(w) w, w, w, w, w, w, w, w
#define BAR(c) T8(SOLID(c)), T8(SOLID(c)), T8(SOLID(c)), T8(SOLID(c)), T8(SOLID(c)), T8(SOLID(c)), T8(EDGE(c))
#define BARS_ROW T8(0), BAR(1), BAR(2), BAR(3), BAR(4), T8(0)
static const u32 bars_tiles[30 * 3 * 8] = {BARS_ROW, BARS_ROW, BARS_ROW};
static const u16 bars_palette[16] = {0, RGB(31, 0, 0), RGB(0, 31, 0), RGB(0, 0, 31), RGB(31, 31, 31)};

// Two tile sets: a new title flips in through the VRAM queue, in the same
// VBlank as the new page's rows (page_turn)
static const struct ui_label_config title_config = {"", UI_FONT_HEADING, UI_ALIGN_LEFT, UI_LABEL_SWAP, 0};
static const struct ui_list_config list_config = {&lines_model, 16, UI_LIST_PLAIN, 0, 0};
static const struct ui_image_config bars_config = {bars_tiles, bars_palette, 0xD1A6};

static const struct ui_spec diag_nodes[N_COUNT] = {
	[N_TITLE_BAR] = {.kind = UI_PANEL,
			 .id = 1,
			 .parent = UI_NONE,
			 .plane = 2,
			 .anchor = UI_HFILL,
			 .paper = UI_ROLE_PANEL,
			 .h = 16},
	[N_TITLE] = {.kind = UI_LABEL,
		     .id = 2,
		     .parent = N_TITLE_BAR,
		     .plane = 2,
		     .anchor = UI_FILL,
		     .paper = UI_ROLE_PANEL,
		     .data = &title_config},
	[N_LIST] = {.kind = UI_LIST,
		    .id = 3,
		    .parent = UI_NONE,
		    .plane = 1,
		    .anchor = UI_FILL,
		    .paper = UI_ROLE_BG,
		    .y = 16,
		    .data = &list_config},
	[N_BARS] = {.kind = UI_IMAGE,
		    .id = 4,
		    .parent = UI_NONE,
		    .plane = 0,
		    .flags = UI_HIDDEN,
		    .y = 120,
		    .w = 240,
		    .h = 24,
		    .data = &bars_config},
};

static int diag_event(struct ui_view *view, const struct ui_event *e)
{
	(void)view;
	if (e->type != UI_EV_KEYS)
		return 0;
	scr.keys |= e->keys;
	return 1;
}

static const struct ui_view_spec diag_view_spec = {
	.name = "diag", .nodes = diag_nodes, .count = N_COUNT, .id = 32, .focus = UI_NONE, .event = diag_event};

static u8 node_of(u32 n)
{
	return (u8)(scr.view->first + n);
}

// Pushes the screen's view, showing the page as it was
static void screen_push(void)
{
	scr.view = ui_push(&diag_view_spec, 0);
	if (!scr.view) {
		tlog_error("diag: %s", ui_error());
		return;
	}
	scr.turning = 0;  // a new view: drawn whole before it shows
	ui_label_set(node_of(N_TITLE), scr.title);
	ui_list_set_cursor(node_of(N_LIST), scr.count ? scr.count - 1 : 0);
	for (u32 i = 0; i < 30 && !(scr.view->flags & UI_V_SHOWN); i++)
		ui_frame();
}

void diag_screen_hide(void)
{
	if (!scr.view)
		return;
	ui_pop();
	scr.view = 0;
}

void diag_screen_show(void)
{
	if (!scr.view)
		screen_push();
}

// Sets the engine and the screen up (again after the unit tests, which reset
// the engine), showing the page as it was. Again, the page held on screen
// (screen_hold) stays there while the new session draws it, in the same
// VRAM (the same view in a new engine takes the same blocks): blanked, it
// went black for 2 frames
static void screen_open(int again)
{
	if (!again) {
		VBlankIntrWait();
		*(vu16 *)0x05000000 = 0;  // a black backdrop until the theme's
		*(vu16 *)0x04000000 = 0;  // mode 0, nothing on
	}
	engine_init();
	gfx_freeze(again);
	motion_reset();  // the unit tests leave motions behind
	ui_init(NULL);
	ui_dim_set_frames(0);  // no dimming in the middle of a question
	screen_push();
	for (u32 i = 0; i < 30 && gfx_queue_len(); i++)
		ui_frame();
	if (gfx_queue_len())
		tlog_debug("diag: page redrawn with %lu VRAM copies still queued", (unsigned long)gfx_queue_len());
	gfx_freeze(0);  // the next commit writes the page's registers and palettes
	if (again)
		tlog_debug("diag: page back after the unit tests");
}

// Starts a page: the list's page change hides the old lines, and page_turn,
// at the next frame Diagnostics runs, brings the title in with the new ones
static void screen_title(const char *title)
{
	snprintf(scr.title, sizeof(scr.title), "DIAGNOSTICS: %s", title);
	scr.count = 0;
	if (!scr.view)
		return;
	TLOG_ASSERT(text_width(&font_heading, scr.title, strlen(scr.title)) + 2u * ui_theme()->pad <= SCREEN_WIDTH);
	ui_show(node_of(N_BARS), 0);
	ui_list_reset(node_of(N_LIST), 0);
	ui_list_page_label(node_of(N_LIST), node_of(N_TITLE), scr.title);
	scr.turning = 1;
}

// Runs frames until the list's scroll is at rest and VRAM is up to date, and
// at least min frames: the work after it may stop the frames for a while
// (reading the save chip, the timings), and the screen then holds no half-
// scrolled row
static void settle(u32 min)
{
	u8 list = node_of(N_LIST);
	for (u32 i = 0; i < 60 && (i < min || !ui_list_still(list) || gfx_queue_len()); i++)
		ui_frame();
	ui_frame();  // its commit puts up the registers the last frame set (the scroll)
	scr.pumped = torii_frames;
}

// Finishes the page change screen_title started: the list draws the new
// page with the lines said so far, the title changes in the same frame
// (ui_list_page_label), and both show in the same VBlank; then its fade-in
// ends
static void page_turn(void)
{
	if (!scr.turning || !scr.view)
		return;
	scr.turning = 0;
	u8 list = node_of(N_LIST);
	for (u32 i = 0; i < 60 && ui_list_paging(list); i++)
		ui_frame();
	settle(ui_theme()->page_fade + 1u);
}

// Adds a line to the page; it shows in the next frame
static void line_add(u8 colour, const char *text)
{
	u32 i = scr.count++;
	snprintf(scr.lines[i % DIAG_LINES], DIAG_LINE_BYTES, "%s", text);
	scr.ink[i % DIAG_LINES] = colour;
	if (!scr.view)
		return;
	ui_list_refresh(node_of(N_LIST), i, i);
	// The line it replaces in the ring leaves the window: its row, cached
	// or drawn, shows blank now (lines_fetch), not the old text
	if (i >= DIAG_LINES)
		ui_list_refresh(node_of(N_LIST), i - DIAG_LINES, i - DIAG_LINES);
	ui_list_set_cursor(node_of(N_LIST), i);
}

// Replaces line i (a live line: the clock, the buttons not pressed yet)
static void line_set(u32 i, u8 colour, const char *text)
{
	if (i >= scr.count || i + DIAG_LINES < scr.count)
		return;
	snprintf(scr.lines[i % DIAG_LINES], DIAG_LINE_BYTES, "%s", text);
	scr.ink[i % DIAG_LINES] = colour;
	if (scr.view)
		ui_list_refresh(node_of(N_LIST), i, i);
}

void diag_pause(u32 frames)
{
	page_turn();
	while (frames--)
		ui_frame();
}

u32 diag_line_cut(const char *text, u32 clip)
{
	u32 len = strlen(text);
	if (text_width(&font_body, text, len) <= clip)
		return len;
	u32 cut = 0;
	for (u32 i = 1; i < len; i++)
		if (text[i] == ' ' && text_width(&font_body, text, i) <= clip)
			cut = i;
	return cut ? cut : len;  // no space to cut at: the list ends it in "..."
}

void diag_say(u8 colour, const char *format, ...)
{
	char text[160];
	va_list args;
	va_start(args, format);
	vsnprintf(text, sizeof(text), format, args);
	va_end(args);
	if (colour == RED)
		tlog_error("diag: %s", text);
	else
		tlog("diag: %s", text);
	// A line too wide for the page goes on in the next, cut at a space
	u32 clip = SCREEN_WIDTH - 2u * ui_theme()->pad;
	const char *rest = text;
	for (;;) {
		u32 cut = diag_line_cut(rest, clip);
		char part[DIAG_LINE_BYTES];
		snprintf(part, sizeof(part), "%.*s", (int)cut, rest);
		line_add(colour, part);
		rest += cut;
		while (*rest == ' ')
			rest++;
		if (!*rest)
			break;
	}
	// The screen keeps up with the log: the new page with its first line, or
	// a frame, unless one ran since the last VBlank (a burst of lines shows at
	// the next); a line that scrolled the list waits for the scroll to end
	if (scr.turning) {
		page_turn();
	} else if (torii_frames != scr.pumped) {
		ui_frame();
		scr.pumped = torii_frames;
	}
	if (scr.view && !ui_list_still(node_of(N_LIST)))
		settle(0);
}

void diag_step(const char *name)
{
	diag_step_no++;
	cartlog_phase(PHASE_DIAG, diag_step_no, NULL);
	tlog("diag: step %lu: %s", (unsigned long)diag_step_no, name);
	tlog_flush();
	screen_title(name);
}

static void wait_release(void)
{
	page_turn();
	do {
		ui_frame();
	} while (ui_input()->held);
}

// Called every frame while wait_key waits, if set: the live clock.
static void (*wait_tick)(void);

// Waits for every key to be let go, then for a press of one in mask.
static u16 wait_key(u16 mask)
{
	wait_release();
	scr.keys = 0;
	for (;;) {
		ui_frame();
		if (wait_tick)
			wait_tick();
		u16 keys = scr.keys & mask;
		if (keys)
			return keys;
	}
}

int diag_ask(const char *topic, const char *question, int kind)
{
	say(WHITE, "%s", question);
	say(GREY, kind == ASK_TRIED ? "A: yes   B: no   SELECT: didn't try" : "A: yes   B: no   SELECT: skip");
	u16 key = wait_key(KEY_A | KEY_B | KEY_SELECT);
	int answer = (key & KEY_A) ? ANSWER_YES : (key & KEY_B) ? ANSWER_NO : ANSWER_SKIP;
	u8 colour = GREY;
	const char *said = kind == ASK_TRIED ? "not tried" : "skipped";
	if (answer == ANSWER_YES) {
		colour = GREEN;
		said = "yes";
	} else if (answer == ANSWER_NO) {
		colour = kind == ASK_PROBE ? WHITE : RED;
		said = "no";
	}
	say(colour, "%s: %s", topic, said);
	tlog_flush();
	return answer;
}

// --- Timing ------------------------------------------------------------------

// The engine's cycle counter (engine/cycles.h), which keeps running: the
// start is a time stamp, so Diagnostics' timings don't disturb the frame's.
static u32 diag_timer_t0;

void IWRAM_CODE diag_timer_start(void)
{
	cycles_init();
	diag_timer_t0 = cycles_now();
}

u32 IWRAM_CODE diag_timer_stop(void)
{
	return cycles_now() - diag_timer_t0;
}

// KB per second from bytes moved in cycles.
static u32 kb_per_s(u32 bytes, u32 cycles)
{
	if (!cycles)
		return 0;
	return (u32)((unsigned long long)bytes * 16777216ULL / cycles / 1024);
}

// Sums words of ROM-space memory with WAITCNT set to waitcnt, from IWRAM, so
// the kernel's own code isn't read at the wait states under test. Returns
// the cycles taken.
u32 IWRAM_CODE diag_read_timed(const vu32 *src, u32 words, u16 waitcnt, u32 *sum)
{
	u16 ime = REG_IME;
	u16 old = REG_WAITCNT_IO;
	u32 s = 0;
	REG_IME = 0;
	REG_WAITCNT_IO = waitcnt;
	diag_timer_start();
	for (u32 i = 0; i < words; i++)
		s += src[i];
	u32 cycles = diag_timer_stop();
	REG_WAITCNT_IO = old;
	REG_IME = ime;
	*sum = s;
	return cycles;
}

// Copies halfwords of EZ-Flash's boot loader from OFFSET into DST, reading
// only: ROM page 0x8000 shows the loader where the kernel's code was, so this
// runs from IWRAM with interrupts off, copies with a plain loop (no ROM
// helper), and puts the kernel's page back before anything else runs.
void IWRAM_CODE diag_loader_read(u32 offset, u16 *dst, u32 halfwords)
{
	u16 ime = REG_IME;
	REG_IME = 0;
	u16 waitcnt = WAITCNT_SLOW();
	SetRompage(ROM_PAGE_LOADER);
	const vu16 *src = (const vu16 *)(0x08000000 + offset);
	for (u32 i = 0; i < halfwords; i++)
		dst[i] = src[i];
	SetRompage(gl_currentpage ? gl_currentpage : 0x8002);
	WAITCNT_RESTORE(waitcnt);
	REG_IME = ime;
}

// --- Steps -------------------------------------------------------------------

// The summary's lines: each failed check, each yes the log can't back, and
// each renderer figure slower than the emulator's.
static const char *summary[24];
static u32 summary_count;
static const char *unconfirmed[16];
static u32 unconfirmed_count;
static const char *slow[12];
static u32 slow_count;

void diag_add_failure(const char *what)
{
	if (summary_count < sizeof(summary) / sizeof(*summary))
		summary[summary_count++] = what;
}
#define add_failure diag_add_failure

void diag_add_slow(const char *what)
{
	if (slow_count < sizeof(slow) / sizeof(*slow))
		slow[slow_count++] = what;
}

// Shows a check's result; failures go in the summary. what is a literal.
static void result(int ok, const char *what)
{
	say(ok ? GREEN : RED, "%s: %s", what, ok ? "pass" : "FAIL");
	if (!ok)
		add_failure(what);
}

static void step_info(void)
{
	step("the cart");
	u16 fpga = Read_FPGA_ver();
	say(WHITE, "FPGA firmware %u (%s)", fpga & 0xFF,
	    ((fpga & 0xF000) == 0xA000 || (fpga & 0xF000) == 0xB000) ? "LX16, Rev.B" : "LX9, Rev.A");
	tlog("diag: FPGA version register %04X", fpga);
	say(WHITE, "kernel NOR id %04X, game NOR id %04X", Read_S71NOR_ID(), Read_S98NOR_ID());
	say(WHITE, "WAITCNT %04X in the menu, 0000 in games", REG_WAITCNT_IO);
	say(WHITE, "kernel ROM %08lX-%08lX", 0x08000000ul, (unsigned long)&__rom_end__);
	u32 id = build_id_compute();
	say(id == torii_build_id ? WHITE : RED, "build id %08lX%s", (unsigned long)torii_build_id,
	    id == torii_build_id ? ", matches the ROM" : ", NOT the ROM's");
	for (u32 slot = 0; slot < 32; slot += 8) {
		u16 v[8];
		for (u32 i = 0; i < 8; i++)
			v[i] = Read_SET_info(slot + i);
		tlog_debug("diag: settings %2lu-%2lu: %04X %04X %04X %04X %04X %04X %04X %04X", (unsigned long)slot,
			   (unsigned long)slot + 7, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
	}
	pause_frames(60);
}

// Leaves the page on screen while the unit tests run, for the seconds they
// take: they reset the engine (pools, shadow state, motions) but write only
// sprite VRAM from 0x06014000, OAM and the sprite palette (tests/kernel),
// never BG VRAM, the BG palette or DISPCNT. So with sprites off the page's
// backgrounds stay as they are. The engine's state is dropped, not shut down
// (ui_shutdown would queue changes to the page's VRAM); screen_open starts
// it again.
static void screen_hold(void)
{
	VBlankIntrWait();
	*(vu16 *)0x04000000 &= ~0x1000;  // DISPCNT: sprites off
	scr.view = 0;
}

// The unit tests reset the engine, so the page holds still while they run
// (screen_hold) and comes back as it was.
static void step_unit_tests(void)
{
	step("unit tests");
	say(WHITE, "running the kernel's unit tests...");
	say(WHITE, "(the screen holds still for about 5 s)");
	pause_frames(30);  // shown, faded in, before the engine stops
	screen_hold();
	// The log's open, if its job hasn't finished, ends now while the page
	// holds still: the tests reset the engine and its jobs
	tlog_open_now();
	tlog_flush();
	int failed = torii_unit_tests();
	screen_open(1);
	say(failed ? RED : GREEN, "%d unit tests failed (see the log)", failed);
	result(!failed, "unit tests");
	pause_frames(60);
}

static int bcd_ok(u8 v, u8 max)
{
	return (v & 0xF) <= 9 && (v >> 4) <= 9 && UNBCD(v) <= max;
}

static void step_rtc(void)
{
	step("clock");
	u8 a[7], b[7];
	rtc_enable();
	rtc_get(a);
	rtc_disenable();
	say(WHITE, "measuring the clock for two seconds...");
	pause_frames(130);  // a bit over two seconds
	rtc_enable();
	rtc_get(b);
	rtc_disenable();
	say(WHITE, "20%02X-%02X-%02X %02X:%02X:%02X", a[0], a[1], a[2], a[4] & 0x3F, a[5], a[6]);
	say(WHITE, "then %02X:%02X:%02X", b[4] & 0x3F, b[5], b[6]);
	tlog_debug("diag: clock raw %02X %02X %02X %02X %02X %02X %02X", a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
	int valid = bcd_ok(a[0], 99) && bcd_ok(a[1], 12) && a[1] && bcd_ok(a[2], 31) && a[2] &&
		    bcd_ok(a[4] & 0x3F, 23) && bcd_ok(a[5], 59) && bcd_ok(a[6], 59);
	u32 s1 = UNBCD(a[6]) + 60 * UNBCD(a[5]), s2 = UNBCD(b[6]) + 60 * UNBCD(b[5]);
	u32 passed = (s2 + 3600 - s1) % 3600;
	say(WHITE, "%lu seconds passed in about 2.2", (unsigned long)passed);
	result(valid, "clock reads a valid date");
	if (passed == 0 && tlog_in_emulator())
		say(GREY, "clock runs: the emulator can't tell");
	else
		result(passed >= 1 && passed <= 4, "clock runs");
	pause_frames(60);
}

// The last 64 KB of PSRAM, a part only a 32 MB game uses: the kernel fills
// PSRAM again at every launch.
#define PSRAM_TEST_PAGE 0x3000
#define PSRAM_TEST_OFFSET 0x7F0000
static void step_psram(void)
{
	step("PSRAM");
	vu16 *p = (vu16 *)(PSRAMBase_S98 + PSRAM_TEST_OFFSET);
	u32 errors = 0, first = 0, write_cycles = 0, read_cycles = 0;
	SetPSRampage(PSRAM_TEST_PAGE);
	for (u32 pass = 0; pass < 2; pass++) {
		u16 flip = pass ? 0xFFFF : 0;
		diag_timer_start();
		for (u32 i = 0; i < 0x8000; i++)
			p[i] = (u16)(i * 0x9E37 + 0x1234) ^ flip;
		u32 w = diag_timer_stop();
		diag_timer_start();
		for (u32 i = 0; i < 0x8000; i++) {
			if (p[i] != ((u16)(i * 0x9E37 + 0x1234) ^ flip)) {
				if (!errors)
					first = i;
				errors++;
			}
		}
		u32 r = diag_timer_stop();
		if (!pass) {
			write_cycles = w;
			read_cycles = r;
		}
	}
	SetPSRampage(0);
	say(WHITE, "64 KB: write %lu KB/s, check %lu KB/s", (unsigned long)kb_per_s(0x10000, write_cycles),
	    (unsigned long)kb_per_s(0x10000, read_cycles));
	if (errors)
		say(RED, "%lu halfwords wrong, the first at +%lX", (unsigned long)errors, (unsigned long)first * 2);
	result(!errors, "PSRAM holds a pattern");
	pause_frames(60);
}

static void step_nor(void)
{
	step("NOR reads");
	u32 size = (u32)&__rom_end__ - 0x08000000;
	u32 a = crc32((u8 *)0x08000000, size), b = crc32((u8 *)0x08000000, size);
	say(WHITE, "kernel CRC %08lX twice: %s", (unsigned long)a, a == b ? "same" : "DIFFERENT");
	result(a == b, "kernel NOR reads the same twice");
	// The FPGA images in the upgrade file have known CRCs (image_layout.h).
	u32 lx9 = crc32((u8 *)LX9_newomega_top_bin_address, LX9_newomega_top_bin_size);
	u32 lx16 = crc32((u8 *)LX16_newomega_top_bin_address, LX16_newomega_top_bin_size);
	say(WHITE, "FPGA image CRCs %08lX %08lX", (unsigned long)lx9, (unsigned long)lx16);
	result(lx9 == LX9_FW_crc32 && lx16 == LX16_FW_crc32, "800 KB of NOR read correctly");
	pause_frames(60);
}

// Reads EZ-Flash's boot loader (never writes it) and logs its CRC and
// whether it looks whole. A blank or damaged loader means the cart starts in
// safe mode at the next power-on: docs/recovery.md.
static void step_loader(void)
{
	step("boot loader");
	u16 *image = (u16 *)pReadCache;
	for (u32 at = 0; at < LOADER_SIZE; at += 0x1000)
		diag_loader_read(at, image + at / 2, 0x1000 / 2);
	enum loader_state state = loader_check((const u8 *)image);
	u32 crc = crc32(image, LOADER_SIZE);
	int whole = state == LOADER_OK;
	int de_bin = whole && crc32(image, DE_BIN_SIZE) == DE_BIN_CRC32;
	// The header's title and code, copied before anything else runs: the
	// image is in pReadCache, which the mode 3 screen's say() used to clear
	// the start of (v0.3.2's garbled title)
	char title[17];
	memcpy(title, (const char *)image + 0xA0, 16);
	title[16] = 0;
	say(whole ? WHITE : RED, "boot loader CRC %08lX", (unsigned long)crc);
	say(whole ? WHITE : RED, "it is %s", loader_state_name(state));
	if (whole) {
		// DE.BIN's header has no title or code: zeros
		if (title[0] || title[12])
			tlog("diag: boot loader title %.12s, code %.4s, %s", title, title + 12,
			     de_bin ? "EZ-Flash's DE.BIN" : "not DE.BIN (another loader version?)");
		else
			tlog("diag: boot loader with a blank title and code, %s",
			     de_bin ? "EZ-Flash's DE.BIN (it has none)" : "not DE.BIN (another loader version?)");
	} else {
		tlog_error("diag: the boot loader is %s: the cart will start in EZ-Flash's safe mode at the next "
			   "power-on (docs/recovery.md)",
			   loader_state_name(state));
		say(RED, "DON'T POWER OFF before reading");
		say(RED, "docs/recovery.md on the Torii site");
	}
	// Whether EZ-Flash's loader has a GBA header isn't known yet: only a blank
	// or erased one fails
	result(whole, "boot loader present");
	pause_frames(whole ? 60 : 600);
}

static u32 fram_crc(void)
{
	u32 crc;
	SetRampage(0x00);
	ReadSram(SRAMSaver, pReadCache, 0x10000);
	SetRampage(0x10);
	ReadSram(SRAMSaver, pReadCache + 0x10000, 0x10000);
	SetRampage(0x00);
	crc = crc32(pReadCache, 0x20000);
	return crc;
}

static void step_save_chip(void)
{
	step("save memory");  // the save chip and cart SRAM, in a title that fits
	say(WHITE, "reading the 128 KB save chip twice...");
	u32 before = fram_crc();
	u32 again = fram_crc();
	say(WHITE, "save chip CRC %08lX, then %08lX", (unsigned long)before, (unsigned long)again);
	result(before == again, "save chip reads the same twice");

	// Patterns in a free 64 KB page of cart SRAM, never the save chip.
	u32 errors = 0;
	u8 *buf = pReadCache, *back = pReadCache + 0x10000;
	for (u32 pass = 0; pass < 2; pass++) {
		for (u32 i = 0; i < 0x10000; i++)
			buf[i] = (u8)(i * 37 + (i >> 8)) ^ (pass ? 0xFF : 0);
		SetRampage(CARTLOG_TEST_PAGE);
		WriteSram(SRAMSaver, buf, 0x10000);
		ReadSram(SRAMSaver, back, 0x10000);
		SetRampage(0);
		for (u32 i = 0; i < 0x10000; i++)
			errors += back[i] != buf[i];
	}
	say(errors ? RED : WHITE, "SRAM page %02X: %lu bytes wrong", CARTLOG_TEST_PAGE, (unsigned long)errors);
	result(!errors, "cart SRAM holds a pattern");

	// Two pages hold different bytes: paging works.
	u8 x = 0x5A, y = 0xA5, rx = 0, ry = 0;
	SetRampage(CARTLOG_TEST_PAGE);
	WriteSram(SRAMSaver, &x, 1);
	SetRampage(CARTLOG_TEST_PAGE + 0x10);
	WriteSram(SRAMSaver, &y, 1);
	SetRampage(CARTLOG_TEST_PAGE);
	ReadSram(SRAMSaver, &rx, 1);
	SetRampage(CARTLOG_TEST_PAGE + 0x10);
	ReadSram(SRAMSaver, &ry, 1);
	SetRampage(0);
	result(rx == x && ry == y, "cart SRAM pages are separate");

	u32 after = fram_crc();
	say(after == before ? WHITE : RED, "save chip CRC after: %08lX", (unsigned long)after);
	result(after == before, "save chip untouched by the tests");
	pause_frames(60);
}

// WS0 settings to try: first access 4, 3 or 2 cycles, then 2 or 1, with the
// prefetch buffer on, and the BIOS default.
static const u16 waitcnts[] = {0x0000, 0x4000, 0x4010, 0x4004, 0x4014, 0x4008, 0x4018};
static const char *const waitcnt_names[] = {"4/2", "4/2+pf", "4/1+pf", "3/2+pf", "3/1+pf", "2/2+pf", "2/1+pf"};

static void timed_reads(const char *what, const vu32 *src)
{
	u32 sum0 = 0;
	for (u32 i = 0; i < sizeof(waitcnts) / sizeof(*waitcnts); i++) {
		u32 sum;
		u32 cycles = diag_read_timed(src, 0x4000, waitcnts[i], &sum);
		if (i == 0)
			sum0 = sum;
		say(sum == sum0 ? WHITE : RED, "%s %04X %s: %lu KB/s%s", what, waitcnts[i], waitcnt_names[i],
		    (unsigned long)kb_per_s(0x10000, cycles), sum == sum0 ? "" : ", BAD DATA");
	}
}

static void step_timing(void)
{
	step("timings");
	say(WHITE, "64 KB read at each WAITCNT (WS0 N/S):");
	timed_reads("NOR", (const vu32 *)0x08000000);
	SetPSRampage(0);
	timed_reads("PSRAM", (const vu32 *)PSRAMBase_S98);
	tlog_flush();

	// The glyph's time is the renderer step's now (diag_render.c)

	// BIOS LZ77: 16 KB of the official SD list's background picture, which
	// the upgrade file keeps for this (image_layout.h): the same data as
	// before, so the figure compares with older reports.
	u8 *packed = pReadCache, *out = pReadCache + 0x8000;
	const u8 *picture = (const u8 *)LZ77_SAMPLE_address;
	u32 packed_size = lz77_compress(picture, LZ77_SAMPLE_size, packed);
	diag_timer_start();
	LZ77UnCompWram(packed, out);
	u32 lz_cycles = diag_timer_stop();
	int lz_ok = memcmp(out, picture, LZ77_SAMPLE_size) == 0;
	say(lz_ok ? WHITE : RED, "LZ77 to WRAM: %lu KB/s (from %lu B)",
	    (unsigned long)kb_per_s(LZ77_SAMPLE_size, lz_cycles), (unsigned long)packed_size);
	result(lz_ok, "BIOS LZ77 gives back the data");

	// SD reads: 256 KB of raw sectors from the card's start.
	diag_timer_start();
	for (u32 lba = 0; lba < 512; lba += 4)
		Read_SD_sectors(lba, 4, pReadCache);
	u32 sd_cycles = diag_timer_stop();
	say(WHITE, "SD reads: %lu KB/s", (unsigned long)kb_per_s(0x40000, sd_cycles));

	// SD writes: 128 KB rewritten in a file of Diagnostics' own, made once.
	FIL f;
	UINT done;
	int ok = f_open(&f, "/torii/diag.bin", FA_READ | FA_WRITE | FA_OPEN_ALWAYS) == FR_OK;
	memset(pReadCache, 0, 0x800);
	if (ok && f_size(&f) != 0x20000) {
		for (u32 i = 0; ok && i < 0x20000; i += 0x800)
			ok = f_write(&f, pReadCache, 0x800, &done) == FR_OK && done == 0x800;
		ok = ok && f_sync(&f) == FR_OK;
	}
	if (ok) {
		ok = f_lseek(&f, 0) == FR_OK;
		diag_timer_start();
		for (u32 i = 0; ok && i < 0x20000; i += 0x800)
			ok = f_write(&f, pReadCache, 0x800, &done) == FR_OK && done == 0x800;
		ok = ok && f_sync(&f) == FR_OK;
		u32 cycles = diag_timer_stop();
		if (ok)
			say(WHITE, "SD writes: %lu KB/s", (unsigned long)kb_per_s(0x20000, cycles));
	}
	f_close(&f);
	result(ok, "SD write test");
	pause_frames(60);
}

// --- The log on the card ------------------------------------------------------

// What the log shows happened since the last Diagnostics run (logcheck.h).
static u32 log_seen;
static struct logcheck log_check EWRAM_BSS;

// Reads the log files, oldest first, into pReadCache one at a time.
static void read_log(void)
{
	char path[40];
	u32 seqs[LOG_FILES];
	FIL f;
	UINT got;
	memset(&log_check, 0, sizeof(log_check));
	tlog_flush();
	for (u32 i = 0; i < LOG_FILES; i++) {
		seqs[i] = 0;
		snprintf(path, sizeof(path), "/torii/logs/torii-%lu.log", (unsigned long)i);
		if (f_open(&f, path, FA_READ) != FR_OK)
			continue;
		if (f_read(&f, pReadCache, LOG_HEADER_LEN, &got) == FR_OK)
			seqs[i] = log_header_seq((const char *)pReadCache, got);
		f_close(&f);
	}
	for (;;) {
		int next = -1;
		for (u32 i = 0; i < LOG_FILES; i++)
			if (seqs[i] && (next < 0 || seqs[i] < seqs[next]))
				next = (int)i;
		if (next < 0)
			break;
		seqs[next] = 0;
		snprintf(path, sizeof(path), "/torii/logs/torii-%lu.log", (unsigned long)next);
		if (f_open(&f, path, FA_READ) != FR_OK)
			continue;
		if (f_read(&f, pReadCache, LOG_FILE_SIZE, &got) != FR_OK)
			got = 0;
		f_close(&f);
		// Line by line, to the first zero byte: the end of the text.
		pReadCache[got] = 0;  // got <= LOG_FILE_SIZE, half the buffer
		for (char *text = (char *)pReadCache; *text;) {
			char *nl = strchr(text, '\n');
			if (nl)
				*nl = 0;
			logcheck_line(&log_check, text);
			if (!nl)
				break;
			text = nl + 1;
		}
	}
	log_seen = log_check.seen;
	tlog("diag: the log since the last run shows %03lX (see source/logcheck.h)", (unsigned long)log_seen);
}

// --- EEPROM 512 or 8K ----------------------------------------------------------

// The cart's EEPROM answers only in game mode: tests/roms/eeprom asks it.
static void step_eeprom(void)
{
	step("EEPROM 512 or 8K");
	if (log_seen & SEEN_EEPROM_PROBE_FAILED) {
		say(RED, "An EEPROM probe run failed: its");
		say(RED, "verdict and steps are in the log.");
		add_failure("the EEPROM probe");
	} else if (log_seen & SEEN_EEPROM_PROBE) {
		say(GREEN, "EEPROM probe runs since the last run");
		say(GREEN, "read back what they wrote: pass");
	} else {
		say(WHITE, "Not tested since the last run: only a");
		say(WHITE, "game can test EEPROM. With Torii's");
		say(WHITE, "EEPROM probe (eeprom.gba) on the card,");
		say(WHITE, "launch it with CLEAN BOOT, SAVE TYPE");
		say(WHITE, "EEPROM 512, then 8K, and press A, then");
		say(WHITE, "B, each time: four runs, each logged.");
	}
	pause_frames(180);
}

// --- Rumble ------------------------------------------------------------------

#define ROM_PAGE_PSRAM 0x200  // the page games from PSRAM, Jagoomba among them, run under

// Pulses the motor for a second as Jagoomba does (Set_rumble, on and off at
// once, each frame). The motor takes 0x08001000 only while the ROM page isn't
// the kernel NOR's, whose window it otherwise is (real cart, 2026-10-18), so
// this sets the PSRAM page meanwhile: from IWRAM with interrupts off, as the
// kernel's code is gone, waiting on VCOUNT, with the kernel's page back
// before it returns. The writes don't reach the PSRAM.
void IWRAM_CODE diag_rumble_pulses(void)
{
	u16 ime = REG_IME;
	REG_IME = 0;
	SetRompage(ROM_PAGE_PSRAM);
	for (u32 f = 0; f < 60; f++) {
		Set_rumble(1);
		Set_rumble(0);
		while (REG_VCOUNT >= 160)
			;
		while (REG_VCOUNT < 160)
			;
	}
	SetRompage(gl_currentpage ? gl_currentpage : 0x8002);
	REG_IME = ime;
}

// The motor also needs the GPIO port on (0x096A0000 bit 0), as for a game.
static void step_rumble(void)
{
	step("rumble");
	say(WHITE, "Hold the console: the motor runs for a");
	say(WHITE, "second.");
	Set_RTC_status(1);
	pause_frames(30);
	diag_rumble_pulses();
	if (ask("rumble", "Did it rumble just now?", ASK_CHECK) == ANSWER_NO)
		add_failure("rumble");
}

// --- Checklist ---------------------------------------------------------------

// docs/release-checklist.md, "In Diagnostics": things only a person can see.
// Each question fits the screen; name is a literal for the summary. A yes to
// something the log should show (needs) is "unconfirmed" without it.
struct check {
	const char *question;
	const char *name;
	u32 needs;
	const char *proof;  // what the log lacks, for the unconfirmed line
};
#define CHECK_PREFIX "checklist: "
#define ITEM(question, name, needs, proof) {question, CHECK_PREFIX name, needs, proof}

static const struct check checklist[] = {
	ITEM("The bars are red, green, blue, white.", "colour bars", 0, NULL),
	ITEM("The LED breathes red and blue now.", "LED", 0, NULL),
	ITEM("The clock above shows the right time.", "clock", 0, NULL),
	ITEM("Since the last run, a GBA game started.", "a GBA game started", SEEN_GBA, "GBA launch"),
	ITEM("...its save survived a power cycle.", "a save survived a power cycle", SEEN_GBA, "GBA launch"),
	ITEM("...the in-game menu opened.", "the in-game menu", SEEN_MENU,
	     "BOOT WITH ADDON launch with RESET or SAVESTATE on"),
	ITEM("...a save state saved and loaded.", "save states", SEEN_SAVESTATE, "launch with SAVESTATE on"),
	ITEM("...sleep slept and woke.", "sleep", SEEN_SLEEP, "launch with SLEEP on"),
	ITEM("...a GB or GBC game ran.", "a GB or GBC game", SEEN_GB, "GB or GBC launch"),
	ITEM("...a NES game ran.", "a NES game", SEEN_NES, "NES launch"),
	ITEM("...a game written to NOR started.", "a game from NOR", SEEN_NOR, "launch from NOR"),
	ITEM("...the clock kept time while off.", "the clock while off", 0, NULL),
	ITEM("...DEBUG in a game reached the log.", "DEBUG", SEEN_DEBUG, "debug info"),
};

#define CLOCK_ITEM 2

static u8 clock_shown;   // the second on screen
static u32 clock_line;   // the page's line that shows it

static void clock_read(u8 t[7])
{
	rtc_enable();
	rtc_get(t);
	rtc_disenable();
}

static void clock_draw(void)
{
	u8 t[7];
	clock_read(t);
	if (t[6] == clock_shown)
		return;
	clock_shown = t[6];
	char line[24];
	snprintf(line, sizeof(line), "20%02X-%02X-%02X %02X:%02X:%02X", t[0], t[1], t[2], t[4] & 0x3F, t[5], t[6]);
	line_set(clock_line, YELLOW, line);
}

// The live clock on the clock item: read four times a second, drawn when
// the second changes.
static void clock_tick(void)
{
	if (torii_frames % 15 == 0)
		clock_draw();
}

static void step_keys(void)
{
	step("buttons");
	say(WHITE, "Press every button once. Hold START and");
	say(WHITE, "SELECT for two seconds to give up.");
	u16 seen = 0, shown = 0xFFFF;
	u32 held = 0;
	wait_release();
	line_add(YELLOW, "");  // the buttons not pressed yet
	u32 keys_line = scr.count - 1;
	while (seen != 0x3FF && held < 120) {
		ui_frame();
		const InputState *in = ui_input();
		seen |= in->pressed;
		held = ((in->held & (KEY_START | KEY_SELECT)) == (KEY_START | KEY_SELECT)) ? held + 1 : 0;
		static const char *const names[] = {"A", "B", "SEL", "START", "RIGHT", "LEFT", "UP", "DOWN", "R", "L"};
		if (seen != shown) {
			shown = seen;
			char line[64];
			u32 len = 0;
			line[0] = 0;
			for (u32 k = 0; k < 10; k++) {
				if (!(seen & (1 << k)))
					len += snprintf(line + len, sizeof(line) - len, "%s ", names[k]);
			}
			line_set(keys_line, YELLOW, line[0] ? line : "all seen");
		}
	}
	if (seen != 0x3FF)
		tlog("diag: keys never pressed: %03X", (unsigned)(~seen & 0x3FF));
	result(seen == 0x3FF, "every button");
	if (~seen & (KEY_L | KEY_R)) {
		say(YELLOW, "The in-game menu and sleep hotkeys use");
		say(YELLOW, "L and R by default: they need both.");
	}
	wait_release();
}

static void step_checklist(void)
{
	const u32 count = sizeof(checklist) / sizeof(*checklist);
	for (u32 i = 0; i < count; i++) {
		const struct check *c = &checklist[i];
		step("checklist");
		say(GREY, "item %lu of %lu", (unsigned long)i + 1, (unsigned long)count);
		if (i == 0 && scr.view)
			ui_show(node_of(N_BARS), 1);
		if (i == 1)
			Set_LED_control(0x80 | 0x20 | 0x08);  // breathing red and blue
		if (i == CLOCK_ITEM) {
			line_add(YELLOW, "");
			clock_line = scr.count - 1;
			clock_shown = 0xFF;  // not a BCD second: drawn at once
			clock_draw();
			wait_tick = clock_tick;
		}
		char topic[48];
		snprintf(topic, sizeof(topic), "checklist %lu (%s)", (unsigned long)i + 1,
			 c->name + sizeof(CHECK_PREFIX) - 1);
		int answer = ask(topic, c->question, i > CLOCK_ITEM ? ASK_TRIED : ASK_CHECK);
		wait_tick = NULL;
		if (i == 1)
			CheckSwitch();  // the LED as the settings have it
		if (i == CLOCK_ITEM) {
			// To compare with the real time the user notes: the clock's drift.
			u8 t[7];
			clock_read(t);
			tlog("diag: %s: the clock read 20%02X-%02X-%02X %02X:%02X:%02X at the answer", topic, t[0],
			     t[1], t[2], t[4] & 0x3F, t[5], t[6]);
		}
		if (answer == ANSWER_NO) {
			add_failure(c->name);
		} else if (answer == ANSWER_YES && c->needs) {
			if (log_seen & c->needs) {
				tlog("diag: %s: the log shows it", topic);
			} else {
				say(YELLOW, "unconfirmed: the log doesn't show it");
				tlog("diag: %s: unconfirmed: the log has no %s since the last run", topic, c->proof);
				if (unconfirmed_count < sizeof(unconfirmed) / sizeof(*unconfirmed))
					unconfirmed[unconfirmed_count++] = c->name;
				pause_frames(90);
			}
		}
	}
}

// --- The run -----------------------------------------------------------------

// Waits for A or SELECT on the summary; UP and DOWN scroll back over its
// lines, which may be more than the screen shows
static u16 summary_keys(void)
{
	u32 first = scr.count > DIAG_LINES ? scr.count - DIAG_LINES : 0;
	u32 cursor = scr.count ? scr.count - 1 : 0;
	wait_release();
	scr.keys = 0;
	for (;;) {
		ui_frame();
		u16 keys = scr.keys;
		scr.keys = 0;
		if (keys & (KEY_A | KEY_SELECT))
			return keys;
		if ((keys & KEY_UP) && cursor > first)
			cursor--;
		if ((keys & KEY_DOWN) && cursor + 1 < scr.count)
			cursor++;
		if (scr.view && (keys & (KEY_UP | KEY_DOWN)))
			ui_list_set_cursor(node_of(N_LIST), cursor);
	}
}

static u32 crash_loop[1] EWRAM_BSS;

void Diagnostics(void)
{
	memset(&scr, 0, sizeof(scr));
	screen_title("start?");
	screen_open(0);
	say(WHITE, "Diagnostics tests the cart and writes");
	say(WHITE, "a report to /torii/logs. It never");
	say(WHITE, "writes your saves. It takes a minute,");
	say(WHITE, "then restarts the kernel.");
	say(WHITE, "A: start   B: back to the menu");
	diag_pause(3);  // drawn before the screen counts as ready
	tlog_debug("screen ready: diag-start");
	if (wait_key(KEY_A | KEY_B) & KEY_B) {
		// Nothing changed yet: back to the caller, which starts the menu
		// again (B still held counts as held there)
		tlog("diag: not started");
		ui_shutdown();
		dialog_blank();
		return;
	}
	if (tlog_level == TLOG_OFF) {  // this session has no line on the card yet
		tlog_set_level(TLOG_DEBUG);
		tlog_marker("Diagnostics", 0);
	}
	tlog_set_level(TLOG_DEBUG);
	diag_step_no = 0;
	summary_count = 0;
	unconfirmed_count = 0;
	slow_count = 0;
	tlog("diag: Diagnostics started, logging at debug for this run");
	// A new log file takes about 80 frames to make: a job makes it while the
	// first page shows, and the unit tests' step finishes it (v0.3.2: A took
	// that long to show anything)
	tlog_flush_soon();

	step_info();
	step_unit_tests();
	step_rtc();
	step_psram();
	step_nor();
	step_loader();
	step_save_chip();
	step_timing();
	diag_renderer_step();
	read_log();
	step_eeprom();

	step("questions");
	say(WHITE, "The rest needs you: rumble, buttons, a");
	say(WHITE, "short checklist and a renderer demo.");
	say(WHITE, "A: go on   START: skip them");
	if (wait_key(KEY_A | KEY_START) & KEY_A) {
		step_rumble();
		step_keys();
		step_checklist();
		diag_renderer_demo();
	} else {
		tlog("diag: questions skipped");
	}

	step("summary");
	if (summary_count == 0)
		say(GREEN, unconfirmed_count || slow_count ? "no check failed" : "everything passed");
	for (u32 i = 0; i < summary_count; i++)
		say(RED, "failed: %s", summary[i]);
	for (u32 i = 0; i < unconfirmed_count; i++)
		say(YELLOW, "unconfirmed: %s", unconfirmed[i]);
	for (u32 i = 0; i < slow_count; i++)
		say(YELLOW, "slower than the emulator: %s", slow[i]);
	say(WHITE, "The report is in /torii/logs.");
	say(WHITE, "A: restart the kernel");
	say(WHITE, "SELECT: crash it on purpose, to test");
	say(WHITE, "the crash dump, then restart");
	say(GREY, "UP/DOWN: scroll the summary");
	tlog("diag: done");
	cartlog_phase(PHASE_MENU, 0, NULL);
	tlog_flush();
	u16 key = summary_keys();
	if (key & KEY_SELECT) {
		cartlog_phase(PHASE_CRASH_TEST, 0, NULL);
		tlog("diag: crash test: running from EWRAM, outside the kernel's code");
		tlog_flush();
		crash_loop[0] = 0xEAFFFFFE;  // ARM: b .
		((void (*)(void))crash_loop)();
	}
	SoftReset_now();
	for (;;)
		;
}
