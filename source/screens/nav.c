// The navigation shell (nav.h): home, the pages, where the menu starts and
// the menu's loop.
//
// Event handlers only record what to do (nav.open, nav.back, nav_defer);
// the loop does it between frames, where it may read the card, push and pop
// views, show dialogs, or leave the engine for a launch, whose dialogs run
// sessions of their own (they fill PSRAM and take pReadCache, so the place
// is shown again from scratch when the menu comes back). Everything else
// runs in the menu's one engine session: a screen replaces another with a
// transition (ui_leave), so switching never goes black.
#include <stdio.h>
#include <string.h>

#include "nav.h"
#include "home.h"
#include "info.h"
#include "nor.h"
#include "sdcard.h"
#include "../actions.h"
#include "../ezkernel.h"
#include "../Ezcard_OP.h"
#include "../lang.h"
#include "../library.h"
#include "../logbuf.h"
#include "../settings.h"
#include "../tlog.h"
#include "../userdata.h"
#include "../engine/engine.h"
#include "../ui/art_ids.h"
#include "../ui/dialog.h"
#include "../ui/ui.h"

#define CLOCK_POLL 30              // frames between clock reads
#define SCREEN_MEASURE_FRAMES 3     // frames measured after a place is pushed (its hidden first draw)
#define IDLE_FLUSH_FRAMES 30        // frames with no key held before a flush of a half-full log
#define IDLE_FLUSH_ALL_FRAMES 300   // and before a flush of any log line, or of the boot position

// --- The empty page's look, px (spec.md's grid) ------------------------------
#define PAGE_TITLE_H 24  // the title bar's rectangle: whole tiles under its line
#define PAGE_CARD_Y 16   // the card's rectangle; its body starts 4 px in
#define PAGE_CARD_H 128
#define PAGE_HINTS_H 16
#define EMPTY_X 8        // the card's note: across the card, in its middle
#define EMPTY_Y 72
#define EMPTY_H 16
static const struct ui_card_config page_card = {4, 4, 4, 1, 0};

volatile u32 nav_test_action EWRAM_BSS;
char nav_test_path[USERDATA_PATH_MAX] EWRAM_BSS ALIGN(4);

static struct {
	u32 counts[NAV_ITEMS];
	char count_text[NAV_ITEMS][24];
	u8 shown[NAV_ITEMS];  // the items home lists, in order
	u8 shown_count;
	struct registry_place place;  // where the menu is (place.item: home's facing item, as last shown)
	struct registry_place start;  // where it started
	u8 left_start;                // the user has been elsewhere since (remember)
	u8 engine;     // the menu's engine session is up, a screen showing
	u8 open;       // home's A: the item to open + 1, after the frame
	u8 back;       // a page's B: home, after the frame
	u8 reshow;     // nav_reshow: the page again, after its action
	u8 announced;  // "nav: ... ready" logged for this place
	u8 h24;        // the clock shows 24 hours
	u8 boot_flush_tried;  // the boot position's idle write was tried this idle spell
	u8 page_first;  // the empty page's first node
	struct ui_hint page_hint;
	u32 lib_generation;
	u32 clock_wait;
	char clock[12];
	const char *measure;
	u32 measure_left;
	void (*deferred)(u32 arg);
	u32 deferred_arg;
} nav EWRAM_BSS;

// --- Names and counts --------------------------------------------------------

const char *nav_item_name(u32 item)
{
	const char *const names[NAV_ITEMS] = {
		[NAV_RECENT] = gl_item_recent, [NAV_FAV] = gl_item_fav, [NAV_GBA] = gl_item_gba,
		[NAV_GBC] = gl_item_gbc,       [NAV_GB] = gl_item_gb,   [NAV_NES] = gl_item_nes,
		[NAV_SD] = gl_item_sd,         [NAV_NOR] = gl_item_nor, [NAV_SET] = gl_item_settings,
		[NAV_INFO] = gl_item_info,
	};
	return item < NAV_ITEMS ? names[item] : "";
}

const char *nav_item_count(u32 item)
{
	return item < NAV_ITEMS ? nav.count_text[item] : "";
}

const char *nav_clock(void)
{
	return nav.clock;
}

void nav_count_words(char *out, u32 size, u32 n, const char *one, const char *many)
{
	if (n == 1 && one) {
		snprintf(out, size, "%s", one);
		return;
	}
	char digits[16];
	u32 len = (u32)snprintf(digits, sizeof(digits), "%lu", (unsigned long)n), o = 0;
	for (u32 i = 0; i < len && o + 2 < size; i++) {
		if (i && (len - i) % 3 == 0)
			out[o++] = ',';
		out[o++] = digits[i];
	}
	snprintf(out + o, size - o, "%s%s", many ? " " : "", many ? many : "");
}

// What each item holds, and which items show (registry_shown)
static void count_items(void)
{
	u32 *c = nav.counts;
	c[NAV_RECENT] = userdata_recent_count();
	c[NAV_FAV] = userdata_fav_count();
	// After a launch that didn't start, the index loads again in the
	// background: the counts from before stand meanwhile, so the consoles
	// don't leave home for a moment
	if (library_ready() || !library_busy()) {
		for (u32 i = 0; i < NAV_ITEMS; i++)
			if (registry_items[i].console != NAV_NO_CONSOLE)
				c[i] = library_count(registry_items[i].console);
		c[NAV_SD] = library_total_files();
	}
	c[NAV_NOR] = game_total_NOR;
	c[NAV_SET] = c[NAV_INFO] = 0;
	for (u32 i = 0; i < NAV_ITEMS; i++) {
		char *t = nav.count_text[i];
		if (i == NAV_SD)
			nav_count_words(t, sizeof(nav.count_text[i]), c[i], gl_count_file, gl_count_files);
		else if (i == NAV_SET)
			snprintf(t, sizeof(nav.count_text[i]), "%s", gl_count_page);
		else if (i == NAV_INFO)
			t[0] = 0;
		else
			nav_count_words(t, sizeof(nav.count_text[i]), c[i], gl_count_game, gl_count_games);
	}
	nav.shown_count = (u8)registry_shown(c, nav.shown);
	nav.lib_generation = library_generation();
}

// The settings the shell reads: the clock's hours and Start on
static u32 read_settings(void)
{
	u16 slots[assress_max];
	for (u32 a = 0; a < assress_max; a++)
		slots[a] = Read_SET_info(a);
	struct settings_values v;
	settings_values_init(&v, slots);
	nav.h24 = (u8)settings_clock_24h(&v);
	return settings_get(&v, SET_START_ON);
}

// --- The empty page -------------------------------------------------------------
// The frame every page shows until its ticket lands: the item's title bar,
// an empty card saying so, and the hint bar.

enum {
	P_TITLE,
	P_HINTS,
	P_CARD,
	P_NOTE,
	P_NODES
};

#define PAGE_TITLE(icon) {"", 0, (icon)}
static const struct ui_title_config page_titles[NAV_ITEMS] = {
	[NAV_RECENT] = PAGE_TITLE(ART_ICON16_RECENT), [NAV_FAV] = PAGE_TITLE(ART_ICON16_FAV),
	[NAV_GBA] = PAGE_TITLE(ART_ICON16_GBA),       [NAV_GBC] = PAGE_TITLE(ART_ICON16_GBC),
	[NAV_GB] = PAGE_TITLE(ART_ICON16_GB),         [NAV_NES] = PAGE_TITLE(ART_ICON16_NES),
	[NAV_SD] = PAGE_TITLE(ART_ICON16_SD),         [NAV_NOR] = PAGE_TITLE(ART_ICON16_NOR),
	[NAV_SET] = PAGE_TITLE(ART_ICON16_SET),       [NAV_INFO] = PAGE_TITLE(ART_ICON16_INFO),
};
static const struct ui_hints_config page_hints_config = {0, 0};  // empty_enter's
static const struct ui_label_config note_label = {"", UI_FONT_BODY, UI_ALIGN_CENTER, 0, 0};

#define PAGE_NODES(item) \
	{ \
		[P_TITLE] = {UI_TITLE, 1, UI_NONE, 3, UI_HFILL, 0, 0, 0, 0, 0, 0, PAGE_TITLE_H, &page_titles[item]}, \
		[P_HINTS] = {UI_HINTS, 2, UI_NONE, 3, UI_HFILL | UI_BOTTOM, 0, 0, 0, 0, 0, 0, PAGE_HINTS_H, \
			     &page_hints_config}, \
		[P_CARD] = {UI_CARD, 3, UI_NONE, 2, UI_HFILL, 0, 0, 0, 0, PAGE_CARD_Y, 0, PAGE_CARD_H, &page_card}, \
		[P_NOTE] = {UI_LABEL, 4, UI_NONE, 2, UI_HFILL, 0, UI_ROLE_FAINT, UI_ROLE_PANEL, EMPTY_X, EMPTY_Y, \
			    EMPTY_X, EMPTY_H, &note_label}, \
	}
static const struct ui_spec page_nodes[NAV_ITEMS][P_NODES] = {
	PAGE_NODES(NAV_RECENT), PAGE_NODES(NAV_FAV), PAGE_NODES(NAV_GBA), PAGE_NODES(NAV_GBC), PAGE_NODES(NAV_GB),
	PAGE_NODES(NAV_NES),    PAGE_NODES(NAV_SD),  PAGE_NODES(NAV_NOR), PAGE_NODES(NAV_SET), PAGE_NODES(NAV_INFO),
};

static int page_event(struct ui_view *v, const struct ui_event *e);
#define PAGE_VIEW(item) {"page-empty", page_nodes[item], P_NODES, 0, UI_NONE, 0, page_event}
static const struct ui_view_spec page_views[NAV_ITEMS] = {
	PAGE_VIEW(NAV_RECENT), PAGE_VIEW(NAV_FAV), PAGE_VIEW(NAV_GBA), PAGE_VIEW(NAV_GBC), PAGE_VIEW(NAV_GB),
	PAGE_VIEW(NAV_NES),    PAGE_VIEW(NAV_SD),  PAGE_VIEW(NAV_NOR), PAGE_VIEW(NAV_SET), PAGE_VIEW(NAV_INFO),
};

static int page_event(struct ui_view *v, const struct ui_event *e)
{
	(void)v;
	if (e->type != UI_EV_KEYS)
		return 0;
	if (e->keys & ui_input()->pressed & INPUT_B)
		nav_back();
	return 1;
}

static int empty_up(void)
{
	return ui_depth() && ui_view_at(0)->spec == &page_views[nav.place.item];
}

static struct ui_view *empty_enter(u32 item, u32 row)
{
	(void)row;
	struct ui_view *v = ui_push(&page_views[item], &nav);
	if (!v)
		return 0;
	nav.page_first = v->first;
	ui_title_set((u8)(v->first + P_TITLE), nav_item_name(item));
	ui_title_set_right((u8)(v->first + P_TITLE), nav.clock);
	ui_label_set((u8)(v->first + P_NOTE), gl_page_empty);
	nav.page_hint = (struct ui_hint){"B", gl_hint_back};
	ui_hints_set((u8)(v->first + P_HINTS), &nav.page_hint, 1);
	return v;
}

static void empty_clock(const char *text)
{
	if (empty_up())
		ui_title_set_right((u8)(nav.page_first + P_TITLE), text);
}

static const struct nav_page empty_page = {empty_enter, 0, empty_clock, 0};

// Each item's page: a page ticket puts its own here
static const struct nav_page *const pages[NAV_ITEMS] = {
	[NAV_RECENT] = &empty_page, [NAV_FAV] = &empty_page, [NAV_GBA] = &empty_page, [NAV_GBC] = &empty_page,
	[NAV_GB] = &empty_page,     [NAV_NES] = &empty_page, [NAV_SD] = &sdcard_page, [NAV_NOR] = &nor_page,
	[NAV_SET] = &empty_page,    [NAV_INFO] = &info_page,
};

// --- The clock ------------------------------------------------------------------

static void clock_read(void)
{
	struct settings_clock c;
	settings_read_clock(&c);
	char text[sizeof(nav.clock)] = "";
	// Before USERDATA_CLOCK_FROM the clock isn't set: it shows nothing (T4)
	if (userdata_clock_time(userdata_unix_time(c.year, c.month, c.day, c.hour, c.minute, c.second)))
		settings_format_time(text, sizeof(text), c.hour, c.minute, nav.h24);
	if (!strcmp(text, nav.clock))
		return;
	memcpy(nav.clock, text, sizeof(nav.clock));
	if (!nav.engine)
		return;
	if (nav.place.open) {
		if (pages[nav.place.item]->clock)
			pages[nav.place.item]->clock(nav.clock);
	} else {
		home_clock(nav.clock);
	}
}

// --- Places ---------------------------------------------------------------------

static void measure(const char *name, u32 frames)
{
	// This frame is the new measurement's, not the one it replaces
	if (nav.measure_left && nav.measure != name)
		frame_measure_drop(nav.measure);
	nav.measure = name;
	nav.measure_left = frames;
	frame_measure(name);  // this frame too
}

// Where the menu is now: home's facing item, or the page and its row
static void current_place(struct registry_place *p)
{
	*p = nav.place;
	if (!nav.engine)
		return;
	if (p->open) {
		if (pages[p->item]->row)
			p->row = pages[p->item]->row();  // else the row it opened on
	} else if (ui_depth()) {
		p->item = (u8)home_facing();
	}
}

// Keeps the place for Last place (written with the next write of
// recent.txt, or by the idle flush), once the user has left the one the
// menu started on: a boot where nothing moves writes nothing to the card
static void remember(void)
{
	current_place(&nav.place);  // home's facing item, the page's row, as they are now
	struct registry_place p = nav.place;
	if (!nav.left_start) {
		const struct registry_place *s = &nav.start;
		if (p.item == s->item && p.open == s->open && (!p.open || p.row == s->row))
			return;
		nav.left_start = 1;
	}
	char word[USERDATA_ITEM_MAX];
	registry_boot_word(&p, word, sizeof(word));
	userdata_boot_set(word, p.open ? p.row : 0);
}

static void engine_enter(void)
{
	if (nav.engine)
		return;
	if (!ui_active()) {
		// Black until the first commit (dialog_blank: mode 0, no layer on),
		// not a forced blank, which shows white; the screen comes in from it
		dialog_blank();
		engine_init();
		ui_init(NULL);
	}
	ui_enter(UI_TR_LEAVE, 0);  // in from black (the boot's, handed over)
	nav.engine = 1;
}

static void engine_leave(void)
{
	if (!nav.engine)
		return;
	ui_leave(UI_TR_LEAVE, 0);
	ui_shutdown();
	dialog_blank();
	nav.engine = 0;
}

static struct ui_view *push_place(const struct registry_place *p)
{
	if (p->open)
		return pages[p->item]->enter(p->item, p->row);
	return home_enter(nav.shown, nav.shown_count, registry_nearest(nav.shown, nav.shown_count, p->item));
}

// Shows place p: the screen up goes out (kind's way, way as ui_leave's
// dir), p's comes in
static void show(const struct registry_place *p, u32 kind, s32 way)
{
	if (nav.engine)
		ui_leave(kind, way);  // before the slow reads, so a key answers at once
	read_settings();  // the clock's hours, as Settings may have changed them
	count_items();
	engine_enter();
	// What the last screen's handlers recorded (keys while it went) was for
	// it: keys held for this one come with it (ui_leave)
	nav.open = nav.back = 0;
	nav.deferred = 0;
	while (ui_depth())
		ui_pop();
	nav.place = *p;
	nav.announced = 0;
	if (p->open)
		ui_item_set(registry_items[p->item].accent);
	// No room for it (VRAM, a layout over budget) would leave the screen
	// black in the transition's gap, with nothing to take keys: the error
	// comes in from black, then the session starts over, freeing
	// everything, and the place is tried again
	for (u32 tries = 0; !push_place(p); tries++) {
		tlog_error("nav: %s", ui_error());
		TLOG_ASSERT(tries == 0);  // a fresh session should have room
		ui_enter(UI_TR_LEAVE, 0);
		dialog_message(NULL, gl_list_failed, DIALOG_ERROR);
		engine_leave();
		engine_enter();
	}
	remember();
	measure("nav-screen", SCREEN_MEASURE_FRAMES);
}

void nav_open(u32 item)
{
	nav.open = (u8)(item + 1);
}

void nav_back(void)
{
	nav.back = 1;
}

void nav_reshow(void)
{
	nav.reshow = 1;
}

void nav_defer(void (*fn)(u32 arg), u32 arg)
{
	nav.deferred = fn;
	nav.deferred_arg = arg;
}

static void open_item(u32 item)
{
	struct registry_place p = {(u8)item, 1, 0};
	tlog_debug("nav: open %s", registry_items[item].word);
	show(&p, UI_TR_FADE, 1);
}

static void go_back(void)
{
	remember();  // the page's row, for Last place
	struct registry_place p = {nav.place.item, 0, 0};
	tlog_debug("nav: back from %s", registry_items[p.item].word);
	show(&p, UI_TR_FADE, -1);
}

// After something that may have ended the engine session (a launch that
// didn't start) or changed what the items hold: the place comes back
static void after_action(void)
{
	if (!ui_active())
		nav.engine = 0;
	if (!nav.engine || nav.reshow) {
		// In from black, or faded (nav_reshow); a page whose item the
		// action hid (the last game of a console deleted) gives home facing
		// the nearest item
		u32 kind = nav.engine ? UI_TR_FADE : UI_TR_LEAVE;
		struct registry_place p = nav.place;
		nav.reshow = 0;
		count_items();
		if (p.open && registry_index(nav.shown, nav.shown_count, p.item) >= nav.shown_count)
			p.open = 0;
		show(&p, kind, 0);
		return;
	}
	count_items();
	if (!nav.place.open)
		home_items(nav.shown, nav.shown_count);
}

static void run_deferred(void)
{
	void (*fn)(u32) = nav.deferred;
	nav.deferred = 0;
	remember();  // a launch writes recent.txt, with the place
	readcache_poison();  // nothing is in it between two actions
	fn(nav.deferred_arg);
	after_action();
}

// --- The tests' hook (nav.h) ------------------------------------------------------

static void test_hook(void)
{
	u32 word = nav_test_action;
	nav_test_action = 0;
	if (word >> 24 != NAV_TEST_MAGIC || !tlog_in_emulator_now())
		return;
	u32 action = word & 0xFF, save = (word >> 8) & 0xFF, hard = (word >> 16) & 1;
	nav_test_path[USERDATA_PATH_MAX - 1] = 0;
	tlog("nav: test hook %lu %s", (unsigned long)action, nav_test_path);
	remember();
	readcache_poison();
	switch (action) {
	case NAV_TEST_CLEAN:
	case NAV_TEST_ADDON:
	case NAV_TEST_NOR:
	case NAV_TEST_NOR_ADDON:
		action_launch(nav_test_path, action - NAV_TEST_CLEAN, save ? save - 1 : ACTION_SAVE_KEEP, hard);
		break;
	case NAV_TEST_DELETE:
		action_delete_file(nav_test_path);
		break;
	case NAV_TEST_NOR_LAUNCH:
		action_nor_read();
		if (save < game_total_NOR)
			action_nor_launch(save, hard);
		break;
	case NAV_TEST_NOR_DELETE:
		action_nor_delete();
		break;
	case NAV_TEST_NOR_FORMAT:
		action_nor_format();
		break;
	default:
		tlog_error("nav: no test action %lu", (unsigned long)action);
		break;
	}
	nav.reshow = nav.place.open;  // a page shows what the action changed
	after_action();
}

// --- The loop ---------------------------------------------------------------------

// The clock under a dialog (dialog_set_tick), while the place is still up:
// not in a session of the dialogs' own (a launch's), whose views have no
// ctx
static void clock_tick(void)
{
	if (nav.engine && ui_depth() && ui_view_at(0)->ctx)
		clock_read();
}

// Writes the log to the card while the menu waits for a key: once it fills
// half its RAM ring, so a long stay in the menu loses no lines, and after
// IDLE_FLUSH_ALL_FRAMES with any line waiting, so a power-off loses none.
// The boot position goes to the card then too (Last place). A flush's SD
// write never stalls a scroll or a transition. Once the card failed the
// log stops trying, and while the card is full it waits for a file to be
// deleted (tlog_card_freed)
static void idle_flush(void)
{
	u32 idle = ui_idle_frames();
	if (idle < IDLE_FLUSH_ALL_FRAMES)
		nav.boot_flush_tried = 0;
	if (nav.open || nav.back || nav.deferred || jobs_pending() || gfx_pending() || ui_transitioning())
		return;
	if (idle >= IDLE_FLUSH_ALL_FRAMES && !nav.boot_flush_tried && ui_settled()) {
		nav.boot_flush_tried = 1;  // once a spell: a card that fails isn't hammered
		remember();
		userdata_flush();  // only if the place changed since recent.txt was written
	}
	u32 pending = tlog_pending();
	if (!pending || tlog_card_failed())
		return;
	if ((pending >= LOG_RING_SIZE / 2 && idle >= IDLE_FLUSH_FRAMES) || idle >= IDLE_FLUSH_ALL_FRAMES) {
		tlog_console("nav: idle flush");  // tests/logging.sh counts them
		tlog_flush();
	}
}

// "nav: ... ready" once the place's first view is on screen: tests/nav.sh
// waits for it, whatever the page's own view is called
static void announce(void)
{
	if (nav.announced || !ui_depth() || !(ui_view_at(0)->flags & UI_V_READY))
		return;
	nav.announced = 1;
	if (nav.place.open)
		tlog_debug("nav: page %s ready", registry_items[nav.place.item].word);
	else
		tlog_debug("nav: home ready");
}

void nav_run(void)
{
	memset(&nav, 0, sizeof(nav));
	dialog_set_tick(clock_tick);
	u32 start_on = read_settings();
	clock_read();
	userdata_load();  // both lists now, so no screen reads the card while it draws
	count_items();
	struct registry_place p;
	registry_start(start_on, userdata_boot_get(), nav.shown, nav.shown_count, &p);
	tlog_debug("nav: start on %lu: %s%s, row %lu", (unsigned long)start_on, p.open ? "" : REGISTRY_HOME_PREFIX,
		   registry_items[p.item].word, (unsigned long)p.row);
	nav.start = p;
	show(&p, UI_TR_LEAVE, 0);  // in from the boot's black screen
	for (;;) {
		if (nav.measure_left) {
			frame_measure(nav.measure);
			nav.measure_left--;
		}
		ui_frame();
		announce();
		if (nav.clock_wait)
			nav.clock_wait--;
		else {
			nav.clock_wait = CLOCK_POLL;
			clock_read();
		}
		if (nav.deferred)
			run_deferred();
		else if (nav.back)
			go_back();
		else if (nav.open)
			open_item(nav.open - 1u);
		else if (nav_test_action && ui_settled())
			test_hook();
		else if (nav.lib_generation != library_generation())
			after_action();  // the walk found a new index: the items' counts
		if (nav.place.open && pages[nav.place.item]->poll)
			pages[nav.place.item]->poll();
		library_poll();
		idle_flush();
	}
}
