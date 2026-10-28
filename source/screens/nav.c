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
#include "games.h"
#include "home.h"
#include "info.h"
#include "nor.h"
#include "sdcard.h"
#include "settings_page.h"
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
#define LOG_FLUSH_FULL (LOG_RING_SIZE * 3 / 4)  // a log this full is flushed at once, jobs or not

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
	u8 start_on;                  // Start on's item while home doesn't show it yet, else NAV_ITEMS
	u8 engine;     // the menu's engine session is up, a screen showing
	u8 open;       // home's A: the item to open + 1, after the frame
	u8 back;       // a page's B: home, after the frame
	u8 reshow;     // nav_reshow: the page again, after its action
	u8 announced;  // "nav: ... ready" logged for this place
	u8 h24;        // the clock shows 24 hours
	u8 boot_flush_tried;  // the boot position's idle write was tried this idle spell
	u8 top_rows;          // the tests' hook: pages open on their first row, always (NAV_TEST_HOME)
	u16 rows_kept;        // a bit per item: rows[item] holds the row its page was left on
	u32 rows[NAV_ITEMS];
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
		[NAV_RECENT] = S(STR_ITEM_RECENT), [NAV_FAV] = S(STR_ITEM_FAV), [NAV_GBA] = S(STR_ITEM_GBA),
		[NAV_GBC] = S(STR_ITEM_GBC),       [NAV_GB] = S(STR_ITEM_GB),   [NAV_NES] = S(STR_ITEM_NES),
		[NAV_SD] = S(STR_ITEM_SD),         [NAV_NOR] = S(STR_ITEM_NOR), [NAV_SET] = S(STR_ITEM_SETTINGS),
		[NAV_INFO] = S(STR_ITEM_INFO),
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

u16 nav_start_shown(void)
{
	return registry_start_shown(nav.shown, nav.shown_count);
}

void nav_count_words(char *out, u32 size, u32 n, u32 id)
{
	if (id)
		SF(out, size, STR_N(id, n), LG(n));
	else
		lang_number(out, size, n);
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
			nav_count_words(t, sizeof(nav.count_text[i]), c[i], STR_COUNT_FILES);
		else if (i == NAV_SET)
			snprintf(t, sizeof(nav.count_text[i]), "%s", S(STR_COUNT_PAGE));
		else if (i == NAV_INFO)
			t[0] = 0;
		else
			nav_count_words(t, sizeof(nav.count_text[i]), c[i], STR_COUNT_GAMES);
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

// Each item's page
static const struct nav_page *const pages[NAV_ITEMS] = {
	[NAV_RECENT] = &games_page, [NAV_FAV] = &games_page, [NAV_GBA] = &games_page, [NAV_GBC] = &games_page,
	[NAV_GB] = &games_page,     [NAV_NES] = &games_page, [NAV_SD] = &sdcard_page, [NAV_NOR] = &nor_page,
	[NAV_SET] = &settings_page, [NAV_INFO] = &info_page,
};

// The page up is about to be left (nav_page.leave)
static void page_leave(void)
{
	if (nav.place.open && pages[nav.place.item]->leave)
		pages[nav.place.item]->leave();
}

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

// Keeps the carousel item for Last place (home's facing item, or the open
// page's), written with the next write of recent.txt or by the idle flush,
// once the user has moved from where the menu started (another item, or
// its page opened or left): a boot where nothing moves writes nothing to
// the card, and an item already kept isn't written again
static void remember(void)
{
	current_place(&nav.place);  // home's facing item, the page's row, as they are now
	if (!nav.left_start) {
		if (nav.place.item == nav.start.item && nav.place.open == nav.start.open)
			return;
		nav.left_start = 1;
	}
	userdata_boot_set(registry_items[nav.place.item].word);
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
	// From home (the open) the reads go first: the zoom runs while the page
	// is pushed, and with them that would pass a frame (a stalled sprite).
	// Else after the screen has gone, so a key answers at once, and after
	// the page has kept what it holds (Settings: a NOR write, which the
	// clock's hours may follow)
	int early = kind == UI_TR_OPEN;
	if (early) {
		read_settings();
		count_items();
	}
	if (nav.engine)
		ui_leave(kind, way);
	page_leave();  // the page going keeps what it holds
	if (!early) {
		read_settings();  // the clock's hours, as Settings may have changed them
		count_items();
	}
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
		dialog_message(NULL, S(STR_NAV_LIST_FAILED), DIALOG_ERROR);
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

// Home's item zooms into its page (B19): its object, carried over, zooms
// and fogs out while the page draws; a plain fade if it doesn't show
static void open_item(u32 item)
{
	// On the row it was left on, earlier in this session; the first time,
	// the page picks (a game list: the game played last)
	u32 row = nav.top_rows ? 0 : (nav.rows_kept & 1u << item) ? nav.rows[item] : NAV_ROW_FIRST;
	struct registry_place p = {(u8)item, 1, row};
	struct ui_zoom zoom;
	u32 kind = UI_TR_FADE;
	if (!nav.place.open && home_zoom_open(item, &zoom)) {
		ui_zoom_set(&zoom);
		kind = UI_TR_OPEN;
	}
	tlog_debug("nav: open %s", registry_items[item].word);
	show(&p, kind, 1);
}

// The page goes, then its item's object comes back out of the fog as home
// draws (the open backwards); home faces the item, or a fade brings it
// facing the nearest when the item went (its last game deleted)
static void go_back(void)
{
	remember();  // its item, for Last place; its row, for the next open
	nav.rows[nav.place.item] = nav.place.row;
	nav.rows_kept |= (u16)(1u << nav.place.item);
	struct registry_place p = {nav.place.item, 0, 0};
	struct ui_zoom zoom;
	u32 kind = UI_TR_FADE;
	if (registry_index(nav.shown, nav.shown_count, p.item) < nav.shown_count) {
		home_zoom_close(p.item, &zoom);
		ui_zoom_set(&zoom);
		kind = UI_TR_CLOSE;
	}
	tlog_debug("nav: back from %s", registry_items[p.item].word);
	show(&p, kind, -1);
}

// Start on's item once a new index shows it (no index at power-on, or one
// from before its console's first game): home turns to it if the user is
// still where the menu started; NAV_ITEMS for no change
static u32 start_late(void)
{
	u32 item = nav.start_on;
	if (item >= NAV_ITEMS || registry_index(nav.shown, nav.shown_count, item) >= nav.shown_count)
		return NAV_ITEMS;
	nav.start_on = NAV_ITEMS;
	if (nav.left_start || nav.place.open || home_facing() != nav.start.item)
		return NAV_ITEMS;
	tlog_debug("nav: start on %s, now the index shows it", registry_items[item].word);
	nav.start.item = (u8)item;
	nav.place.item = (u8)item;
	return item;
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
		home_items(nav.shown, nav.shown_count, start_late());
}

static void run_deferred(void)
{
	void (*fn)(u32) = nav.deferred;
	nav.deferred = 0;
	remember();  // a launch writes recent.txt, with the place
	readcache_poison();  // nothing is in it between two actions
	ui_wipe_finish();    // an action may block: the theme wipe ends first
	fn(nav.deferred_arg);
	lang_poll();  // a launch that came back: its language again, before the page draws
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
	page_leave();  // a launch may leave the engine
	remember();
	readcache_poison();
	switch (action) {
	case NAV_TEST_CLEAN:
	case NAV_TEST_ADDON:
	case NAV_TEST_NOR:
	case NAV_TEST_NOR_ADDON:
		action_launch(nav_test_path, action - NAV_TEST_CLEAN, save ? save - 1 : ACTION_SAVE_KEEP, hard);
		break;
	case NAV_TEST_STATE:
		action_state_slot_set(save);
		action_slot_chosen(1);
		action_launch(nav_test_path, ACTION_ADDON, ACTION_SAVE_KEEP, hard);
		action_slot_chosen(0);  // returned: it did not start
		action_state_slot_set(0);
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
	case NAV_TEST_HOME:
		nav.top_rows = 1;  // the routes count rows from each page's first
		if (nav.shown_count)
			nav.place = (struct registry_place){nav.shown[0], 0, 0};
		nav.place.open = 0;
		nav.reshow = 1;  // "nav: home ready" again, once it faces the first item
		after_action();
		return;
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

// Writes the boot position to the card (Last place) after
// IDLE_FLUSH_ALL_FRAMES with no key held, from the menu's own loop only:
// once a spell, so a card that fails isn't hammered
static void idle_remember(void)
{
	u32 idle = ui_idle_frames();
	if (idle < IDLE_FLUSH_ALL_FRAMES)
		nav.boot_flush_tried = 0;
	if (nav.open || nav.back || nav.deferred || jobs_pending() || gfx_pending() || ui_transitioning())
		return;
	if (idle >= IDLE_FLUSH_ALL_FRAMES && !nav.boot_flush_tried && ui_settled()) {
		nav.boot_flush_tried = 1;
		remember();
		userdata_flush();  // only if the place changed since recent.txt was written
	}
}

// Writes the log to the card while a screen waits for a key, after every
// frame of every screen's loop (ui_set_frame_hook), Game info's and the
// keypad's too: once it fills half its RAM ring, so a long stay loses no
// lines, and after IDLE_FLUSH_ALL_FRAMES with any line waiting, so a
// power-off loses none. A flush's SD write never stalls a scroll or a
// transition, nor a job, unless the ring is LOG_FLUSH_FULL full: then it
// goes now, before lines are lost. Never under a progress dialog (a copy or
// a launch). Once the card failed the log stops trying, and while the card
// is full it waits for a file to be deleted (tlog_card_freed)
static void idle_flush(void)
{
	u32 pending = tlog_pending();
	if (!pending || tlog_card_failed() || dialog_progress_active())
		return;
	if (pending >= LOG_FLUSH_FULL) {
		tlog_console("nav: full flush");  // tests/logging.sh counts them
		tlog_flush();
		return;
	}
	if (nav.open || nav.back || nav.deferred || jobs_pending() || gfx_pending() || ui_transitioning())
		return;
	u32 idle = ui_idle_frames();
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
	ui_set_frame_hook(idle_flush);
	u32 start_on = read_settings();
	clock_read();
	userdata_load();  // both lists now, so no screen reads the card while it draws
	count_items();
	struct registry_place p;
	registry_start(start_on, userdata_boot_get(), nav.shown, nav.shown_count, &p);
	tlog_debug("nav: start on %lu: %s%s", (unsigned long)start_on, p.open ? "" : "home-",
		   registry_items[p.item].word);
	nav.start = p;
	// A console's item with no games in the index yet: faced once one shows
	u32 wanted = registry_of_start(start_on);
	nav.start_on = (u8)(wanted < NAV_ITEMS && wanted != p.item ? wanted : NAV_ITEMS);
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
		lang_poll();
		idle_remember();
	}
}
