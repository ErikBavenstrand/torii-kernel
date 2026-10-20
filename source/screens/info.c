// The Info page (info.h): its rows, the reading pages they open, and
// Diagnostics.
//
// A reading page replaces the rows in the page's one place (the shell's
// page): A records which, and the page's poll, after the frame, fades the
// rows out, fills the text in (About reads the card's free space then),
// wraps it and fades the reading page in. B goes back the same way.
#include <stdio.h>
#include <string.h>

#include "info.h"
#include "ff.h"
#include "../build_id.h"
#include "../diag.h"
#include "../ezkernel.h"
#include "../Ezcard_OP.h"
#include "../lang.h"
#include "../library.h"
#include "../namesdb.h"
#include "../settings_model.h"
#include "../tlog.h"
#include "../userdata.h"
#include "../version.h"
#include "../engine/engine.h"
#include "../ui/art.h"
#include "../ui/art_ids.h"
#include "../ui/dialog.h"
#include "../ui/ui.h"
#include "../ui/ui_reading.h"

// --- Look, px (spec.md's grid; mockups #21 and #22) ----------------------------
#define TITLE_H 24       // the title bar's rectangle: whole tiles under its line
#define HINTS_H 16
#define MENU_CARD_Y 16   // the four pages' card (its rectangle; its body 4 px in)
#define MENU_CARD_H 72
#define DIAG_CARD_Y 88   // Diagnostics' card under it, one row
#define DIAG_CARD_H 24
#define MENU_X 8         // the rows: 14 px, across both cards
#define MENU_Y 24
#define MENU_W 224
#define MENU_H \
	88        // 6 rows of 14 px (whole tiles: the 4 px left over stay blank): the
			 // four pages, the gap between the cards, Diagnostics
#define MENU_PAD 4       // the 8x8 icon's inset in its row
#define MENU_DETAIL_W 120  // what a row holds ("hotkeys while playing")
#define MENU_ICON_ROLE UI_ROLE_ACCENT_INK  // the rows' 8x8 icons, in the item's ink
#define READ_CARD_Y 16   // a reading page's card
#define READ_CARD_H 128
#define READ_X 8         // its text: 14 px rows, as the game lists'
#define READ_Y 24
#define READ_W 216       // up to the scroll bar's column
#define READ_H 112       // 8 rows: whole rows (B9's contract) and whole tiles
#define READ_PAD 4
static const struct ui_card_config menu_card = {4, 4, 4, 4, 0};
static const struct ui_card_config diag_card = {4, 3, 4, 1, 0};  // its row 3 px from the body's edges
static const struct ui_card_config read_card = {4, 4, 4, 4, 0};

#define OPEN_MEASURE_FRAMES 3  // frames measured after a reading page is pushed (its hidden first draw)
#define NOR_MB 64              // the game NOR (NORflash_OP.c's NOR_GAME_SIZE)
#define INFO_TEXT_BYTES 2048   // a reading page's text, filled in
#define INFO_ROWS 96           // its rows, wrapped

// The rows, in the list: the gap is a blank row between the two cards that
// the cursor steps over
enum {
	R_CONTROLS,
	R_INGAME,
	R_CART,
	R_ABOUT,
	R_GAP,
	R_DIAG,
	R_COUNT,
	R_ITEMS = R_COUNT - 1,  // rows the user sees (the position, Last place)
	R_PAGES = R_GAP,        // rows that open a reading page
};

char info_version[16] EWRAM_BSS;
char info_build[12] EWRAM_BSS;

extern FATFS EZcardFs;  // ezkernelnew.c

static struct {
	u8 first;  // the view up's first node
	u8 page;   // the reading page up: its row + 1; 0 the rows
	u8 open;   // A on a page's row: its row + 1, after the frame
	u8 close;  // B on a reading page, after the frame
	u8 row;    // the rows' cursor (R_*)
	u8 measure_left;
	char position[12];
	char cart_detail[32];
	char about_detail[32];
	struct ui_hint hints[3];
	u32 icons[R_ITEMS][8];
	u16 start[INFO_ROWS];
	u8 len[INFO_ROWS];
	u8 kind[INFO_ROWS];
	struct ui_reading reading;
	u32 used;  // bytes of text
	char text[INFO_TEXT_BYTES];
} info EWRAM_BSS;

static const char *str(u32 id)
{
	return id < INFO_STRINGS ? gl_info[id] : "";
}

// A row's item number (0-4: the position, Last place) and back
static u32 row_item(u32 row)
{
	return row < R_GAP ? row : row - 1;
}

static u32 item_row(u32 item)
{
	return item < R_GAP ? item : R_DIAG;
}

// --- Texts ----------------------------------------------------------------------

// tmpl into out at at, each "%s" in it the next of a, b, c ("" past them);
// returns the text's new length. Strings come from lang.c, so there is no
// printf format here a translation could get wrong.
static u32 fill(char *out, u32 at, u32 size, const char *tmpl, const char *a, const char *b, const char *c)
{
	const char *args[3] = {a, b, c};
	u32 n = 0, room = size - 1, cut = 0;
	for (const char *s = tmpl; *s; s++) {
		if (s[0] == '%' && s[1] == 's') {
			const char *arg = n < 3 && args[n] ? args[n] : "";
			n++;
			s++;
			for (; *arg; arg++)
				if (at < room)
					out[at++] = *arg;
				else
					cut = 1;
		} else if (at < room) {
			out[at++] = *s;
		} else {
			cut = 1;
		}
	}
	TLOG_ASSERT(!cut);  // it all fitted
	out[at] = 0;
	return at;
}

// Adds a part to the reading page's text
static void put(u32 id, const char *a, const char *b, const char *c)
{
	info.used = fill(info.text, info.used, sizeof(info.text), str(id), a, b, c);
}

static void number(char *out, u32 size, u32 n)
{
	nav_count_words(out, size, n, 0, 0);
}

// Bytes as "29.7 GB" or "512 MB"
static void size_words(char *out, u32 size, unsigned long long bytes)
{
	unsigned long long mb = bytes >> 20;
	if (mb >= 1024) {
		unsigned long long tenths = (bytes * 10) >> 30;
		snprintf(out, size, "%lu.%lu GB", (unsigned long)(tenths / 10), (unsigned long)(tenths % 10));
	} else {
		snprintf(out, size, "%lu MB", (unsigned long)mb);
	}
}

static u32 nor_used_mb(void)
{
	unsigned long long bytes = 0;
	for (u32 i = 0; i < game_total_NOR && i < MAX_NOR; i++)
		bytes += pNorFS[i].filesize;
	return (u32)((bytes + (1u << 20) - 1) >> 20);
}

// In game: the hotkeys as Settings has them, the way the launch patches a
// game for them (GBApatch.c): Save states alone make them save and load
// keys; Save states with anything else (or Cheats, in a game with a cheat
// file) put the in-game menu on the reset keys
static void ingame_text(void)
{
	u16 slots[assress_max];
	for (u32 a = 0; a < assress_max; a++)
		slots[a] = Read_SET_info(a);
	struct settings_values v;
	settings_values_init(&v, slots);
	char reset[24], sleep[24];
	settings_value_text(&v, SET_RESET_KEYS, reset, sizeof(reset));
	settings_value_text(&v, SET_SLEEP_KEYS, sleep, sizeof(sleep));
	u32 rts = settings_get(&v, SET_RTS), cheats = settings_get(&v, SET_CHEATS);
	u32 reset_on = settings_get(&v, SET_RESET_ON), sleep_on = settings_get(&v, SET_SLEEP_ON);
	put(INFO_S_INGAME_START, 0, 0, 0);
	if (rts && !cheats && !reset_on && !sleep_on) {
		put(INFO_S_INGAME_STATES, sleep, reset, 0);
	} else {
		if (rts) {
			put(INFO_S_INGAME_MENU, reset, 0, 0);
		} else {
			put(reset_on ? INFO_S_INGAME_RESET : INFO_S_INGAME_RESET_OFF, reset, 0, 0);
			if (cheats)
				put(INFO_S_INGAME_CHEATS, reset, 0, 0);
		}
		put(sleep_on ? INFO_S_INGAME_SLEEP : INFO_S_INGAME_SLEEP_OFF, sleep, 0, 0);
	}
	put(INFO_S_INGAME_END, 0, 0, 0);
}

// Your cart: what the card, the NOR and the lists hold
static void cart_text(void)
{
	char a[24], b[24], c[24];
	number(a, sizeof(a), NOR_MB);
	put(INFO_S_CART_START, a, 0, 0);
	put(INFO_S_CART_GAMES, 0, 0, 0);
	if (library_ready()) {
		static const u8 consoles[] = {NAV_GBA, NAV_GBC, NAV_GB, NAV_NES};
		for (u32 i = 0; i < sizeof(consoles); i++) {
			u32 n = library_count(registry_items[consoles[i]].console);
			nav_count_words(b, sizeof(b), n, gl_count_game, gl_count_games);
			put(INFO_S_CART_CONSOLE, nav_item_name(consoles[i]), b, 0);
		}
		nav_count_words(a, sizeof(a), library_total_files(), gl_count_file, gl_count_files);
		put(INFO_S_CART_FILES, a, 0, 0);
	} else {
		put(INFO_S_CART_COUNTING, 0, 0, 0);  // the index loads again after a launch, or the first scan runs
	}
	if (game_total_NOR) {
		nav_count_words(a, sizeof(a), game_total_NOR, gl_count_game, gl_count_games);
		number(b, sizeof(b), nor_used_mb());
		number(c, sizeof(c), NOR_MB);
		put(INFO_S_CART_NOR, a, b, c);
	} else {
		put(INFO_S_CART_NOR_EMPTY, 0, 0, 0);
	}
	number(a, sizeof(a), userdata_fav_count());
	number(b, sizeof(b), userdata_recent_count());
	number(c, sizeof(c), USERDATA_RECENT_MAX);
	put(INFO_S_CART_LISTS, a, b, c);
	put(INFO_S_CART_END, 0, 0, 0);
}

// About: Torii's version and build, the cart's revision and firmware, the
// card, and the credits. Reads the card (its free space), so it runs
// between frames
static void about_text(void)
{
	put(INFO_S_ABOUT_TORII, info_version, info_build, 0);
	u16 fpga = Read_FPGA_ver();
	u32 lx16 = (fpga & 0xF000) == 0xA000 || (fpga & 0xF000) == 0xB000;  // as Check_FW_update tells them
	char a[24], b[24];
	number(a, sizeof(a), fpga & 0xFF);
	put(INFO_S_ABOUT_CART, str(lx16 ? INFO_S_ABOUT_REV_B : INFO_S_ABOUT_REV_A), a, 0);
	// The free space: exFAT's comes from its bitmap (a few hundred sectors at
	// most), FAT's from FSInfo. A FAT card without a valid FSInfo count
	// would make f_getfree read the whole FAT, seconds on a big card, so its
	// free space isn't shown
	FATFS *fs = &EZcardFs;
	const char *type = fs->fs_type == FS_EXFAT ? "exFAT" : fs->fs_type == FS_FAT32 ? "FAT32" : "FAT";
	unsigned long long cluster = (unsigned long long)fs->csize * 512;
	DWORD clusters;
	if (fs->n_fatent <= 2) {
		tlog_error("info: the card isn't mounted");
		put(INFO_S_ABOUT_CARD_UNKNOWN, 0, 0, 0);
	} else if (fs->fs_type != FS_EXFAT && fs->free_clst > fs->n_fatent - 2) {
		tlog("info: no free cluster count on the card, so no free space");
		size_words(a, sizeof(a), (unsigned long long)(fs->n_fatent - 2) * cluster);
		put(INFO_S_ABOUT_CARD_NO_FREE, a, type, 0);
	} else if (f_getfree("", &clusters, &fs) == FR_OK) {
		size_words(a, sizeof(a), (unsigned long long)(fs->n_fatent - 2) * cluster);
		size_words(b, sizeof(b), clusters * cluster);
		put(INFO_S_ABOUT_CARD, a, b, type);
	} else {
		tlog_error("info: the card's free space didn't read");
		put(INFO_S_ABOUT_CARD_UNKNOWN, 0, 0, 0);
	}
	put(INFO_S_ABOUT_CREDITS, 0, 0, 0);
	FILINFO names;
	if (f_stat(NAMESDB_FILE, &names) == FR_OK)
		put(INFO_S_ABOUT_NAMES, 0, 0, 0);
}

// Fills in and wraps row's reading page
static void page_text(u32 row)
{
	info.used = 0;
	info.text[0] = 0;
	switch (row) {
	case R_CONTROLS:
		put(INFO_S_CONTROLS_TEXT, 0, 0, 0);
		break;
	case R_INGAME:
		ingame_text();
		break;
	case R_CART:
		cart_text();
		break;
	default:
		about_text();
		break;
	}
	// A trailing newline would end on a blank row
	while (info.used && info.text[info.used - 1] == '\n')
		info.text[--info.used] = 0;
	info.reading = (struct ui_reading){info.text, info.start, info.len, info.kind, INFO_ROWS, 0};
	u32 rows =
		ui_reading_wrap(&info.reading, ui_theme()->body, READ_W - 2 * READ_PAD, ui_theme()->look.reading_key_w);
	TLOG_ASSERT(rows);  // every row fitted INFO_ROWS
}

// --- The rows -----------------------------------------------------------------

static u32 menu_count(void *ctx)
{
	(void)ctx;
	return R_COUNT;
}

static int menu_fetch(void *ctx, u32 i, struct ui_item *out)
{
	(void)ctx;
	if (i >= R_COUNT)
		return UI_ITEM_PENDING;
	if (i == R_GAP) {
		out->text = " ";  // drawn blank, not waited for
		return UI_ITEM_READY;
	}
	u32 n = row_item(i);
	out->text = str(INFO_S_CONTROLS + n);
	out->detail = i == R_CART    ? info.cart_detail
		      : i == R_ABOUT ? info.about_detail
				     : str(INFO_S_CONTROLS_DETAIL + n);
	out->icon = info.icons[n];
	return UI_ITEM_READY;
}

static const struct ui_list_model menu_model = {menu_count, menu_fetch, 0};

enum {
	M_TITLE,
	M_HINTS,
	M_CARD,
	M_DIAG_CARD,
	M_LIST,
	M_NODES
};

static const struct ui_title_config menu_title = {"", 0, ART_ICON16_INFO};
static const struct ui_hints_config hints_config = {0, 0};  // set as a view is pushed
static const struct ui_list_config menu_list = {&menu_model, UI_ROW_PITCH_LIST,
						UI_LIST_WRAP | UI_LIST_FIXED | UI_LIST_ICON8, MENU_DETAIL_W, MENU_PAD};

static const struct ui_spec menu_nodes[M_NODES] = {
	[M_TITLE] = {UI_TITLE, 1, UI_NONE, 3, UI_HFILL, 0, 0, 0, 0, 0, 0, TITLE_H, &menu_title},
	[M_HINTS] = {UI_HINTS, 2, UI_NONE, 3, UI_HFILL | UI_BOTTOM, 0, 0, 0, 0, 0, 0, HINTS_H, &hints_config},
	[M_CARD] = {UI_CARD, 3, UI_NONE, 2, UI_HFILL, 0, 0, 0, 0, MENU_CARD_Y, 0, MENU_CARD_H, &menu_card},
	[M_DIAG_CARD] = {UI_CARD, 4, UI_NONE, 2, UI_HFILL, 0, 0, 0, 0, DIAG_CARD_Y, 0, DIAG_CARD_H, &diag_card},
	[M_LIST] = {UI_LIST, 5, UI_NONE, 1, UI_LEFT, UI_FOCUSABLE, 0, 0, MENU_X, MENU_Y, MENU_W, MENU_H, &menu_list},
};

static int menu_event(struct ui_view *v, const struct ui_event *e);
static const struct ui_view_spec menu_view = {"info", menu_nodes, M_NODES, 0, M_LIST, 0, menu_event};

static void menu_position(void)
{
	snprintf(info.position, sizeof(info.position), "%lu/%lu", (unsigned long)row_item(info.row) + 1,
		 (unsigned long)R_ITEMS);
	ui_title_set_right((u8)(info.first + M_TITLE), info.position);
}

// Runs Diagnostics after the frame (nav_defer), with no engine session up.
// B on its first page comes back: the shell shows this page again, on
// Diagnostics' row (the row it asked for before running this)
static void run_diag(u32 arg)
{
	(void)arg;
	tlog("info: Diagnostics");
	// A write of library.idx or crc.txt under way finishes now: Diagnostics'
	// unit tests reset the engine, which drops the jobs, and its end restarts
	// the kernel. The menu loads the index again if B brings it back
	library_before_launch();
	if (ui_active()) {
		ui_leave(UI_TR_LEAVE, 0);
		ui_shutdown();
		dialog_blank();
	}
	Diagnostics();
}

static int menu_event(struct ui_view *v, const struct ui_event *e)
{
	(void)v;
	switch (e->type) {
	case UI_EV_CURSOR: {
		u32 to = e->index < R_COUNT ? e->index : 0;
		if (to == R_GAP) {
			// Over the gap, the way the cursor was going
			to = info.row < R_GAP ? R_DIAG : R_ABOUT;
			ui_list_set_cursor((u8)(info.first + M_LIST), to);
		}
		info.row = (u8)to;
		menu_position();
		return 1;
	}
	case UI_EV_ACTIVATE:
		if (e->index == R_DIAG)
			nav_defer(run_diag, 0);
		else if (e->index < R_PAGES)
			info.open = (u8)(e->index + 1);
		return 1;
	case UI_EV_KEYS:
		if (!(e->keys & ui_input()->pressed & INPUT_B))
			return 0;
		nav_back();
		return 1;
	default:
		return 0;
	}
}

static struct ui_view *push_menu(u32 row)
{
	struct ui_view *v = ui_push(&menu_view, &info);
	if (!v)
		return 0;
	info.first = v->first;
	info.row = (u8)(row < R_COUNT && row != R_GAP ? row : R_CONTROLS);
	ui_title_set((u8)(v->first + M_TITLE), nav_item_name(NAV_INFO));
	info.hints[0] = (struct ui_hint){"A", gl_hint_open};
	info.hints[1] = (struct ui_hint){"^v", gl_hint_move};
	info.hints[2] = (struct ui_hint){"B", gl_hint_back};
	ui_hints_set((u8)(v->first + M_HINTS), info.hints, 3);
	ui_list_reset((u8)(v->first + M_LIST), info.row);
	menu_position();
	return v;
}

// --- The reading pages ----------------------------------------------------------

enum {
	P_TITLE,
	P_HINTS,
	P_CARD,
	P_LIST,
	P_BAR,
	P_NODES
};

static const struct ui_title_config read_title = {"", 0, ART_ICON16_INFO};
static const struct ui_list_config read_list = {&ui_reading_model, UI_ROW_PITCH_LIST, UI_LIST_PLAIN | UI_LIST_SCROLL, 0,
						READ_PAD};
static const struct ui_scrollbar_config read_bar = {P_LIST};

static const struct ui_spec read_nodes[P_NODES] = {
	[P_TITLE] = {UI_TITLE, 1, UI_NONE, 3, UI_HFILL, 0, 0, 0, 0, 0, 0, TITLE_H, &read_title},
	[P_HINTS] = {UI_HINTS, 2, UI_NONE, 3, UI_HFILL | UI_BOTTOM, 0, 0, 0, 0, 0, 0, HINTS_H, &hints_config},
	[P_CARD] = {UI_CARD, 3, UI_NONE, 2, UI_HFILL, 0, 0, 0, 0, READ_CARD_Y, 0, READ_CARD_H, &read_card},
	[P_LIST] = {UI_LIST, 4, UI_NONE, 1, UI_LEFT, UI_FOCUSABLE, 0, 0, READ_X, READ_Y, READ_W, READ_H, &read_list},
	[P_BAR] = {UI_SCROLLBAR, 5, UI_NONE, 2, UI_LEFT, 0, 0, UI_ROLE_PANEL, READ_X + READ_W, READ_Y, 8, READ_H,
		   &read_bar},
};

static int read_event(struct ui_view *v, const struct ui_event *e);
// One view a page, so each logs its own "screen ready: info-..."
#define READ_VIEW(name) {(name), read_nodes, P_NODES, 0, P_LIST, 0, read_event}
static const struct ui_view_spec read_views[R_PAGES] = {
	READ_VIEW("info-controls"),
	READ_VIEW("info-ingame"),
	READ_VIEW("info-cart"),
	READ_VIEW("info-about"),
};

// "1/3": the screenful the top row is in, the last at the last scroll
static void read_position(u32 top)
{
	u32 rows = info.reading.count, shown = READ_H / ui_theme()->look.list_pitch;
	u32 pages = rows > shown ? (rows + shown - 1) / shown : 1;
	u32 at = rows <= shown || top + shown >= rows ? pages : top / shown + 1;
	snprintf(info.position, sizeof(info.position), "%lu/%lu", (unsigned long)at, (unsigned long)pages);
	ui_title_set_right((u8)(info.first + P_TITLE), info.position);
}

static int read_event(struct ui_view *v, const struct ui_event *e)
{
	(void)v;
	switch (e->type) {
	case UI_EV_CURSOR:
		read_position(e->index);
		return 1;
	case UI_EV_KEYS:
		if (!(e->keys & ui_input()->pressed & INPUT_B))
			return 0;
		info.close = 1;
		return 1;
	default:
		return 0;
	}
}

// Measures the frames that draw a view just pushed, from the next one
static void measure(void)
{
	frame_measure("info-page");
	info.measure_left = OPEN_MEASURE_FRAMES - 1;
}

// Fades the rows out and row's reading page in (after the frame)
static void open_page(u32 row)
{
	ui_leave(UI_TR_FADE, 1);  // before the card is read, so the key answers at once
	page_text(row);
	while (ui_depth())
		ui_pop();
	struct ui_view *v = ui_push(&read_views[row], &info.reading);
	if (!v) {
		tlog_error("info: %s", ui_error());
		if (!push_menu(row))
			nav_back();
		return;
	}
	info.first = v->first;
	info.page = (u8)(row + 1);
	ui_title_set((u8)(v->first + P_TITLE), str(INFO_S_CONTROLS + row));
	ui_title_set_sub((u8)(v->first + P_TITLE), nav_item_name(NAV_INFO));
	info.hints[0] = (struct ui_hint){"^v", str(INFO_S_SCROLL)};
	info.hints[1] = (struct ui_hint){"B", gl_hint_back};
	ui_hints_set((u8)(v->first + P_HINTS), info.hints, 2);
	read_position(0);
	tlog_debug("info: %s, %lu rows", read_views[row].name, (unsigned long)info.reading.count);
	measure();
}

// Fades the reading page out and the rows in, on its row
static void close_page(void)
{
	u32 row = info.page - 1u;
	ui_leave(UI_TR_FADE, -1);
	while (ui_depth())
		ui_pop();
	info.page = 0;
	if (!push_menu(row)) {
		tlog_error("info: %s", ui_error());
		nav_back();
		return;
	}
	measure();
}

// --- The page (nav.h) -------------------------------------------------------------

static struct ui_view *info_enter(u32 item, u32 row)
{
	(void)item;
	if (!info_version[0])
		snprintf(info_version, sizeof(info_version), "%s", TORII_VERSION);
	if (!info_build[0])
		snprintf(info_build, sizeof(info_build), "%08lX", (unsigned long)torii_build_id);
	char mb[16];
	number(mb, sizeof(mb), NOR_MB);
	fill(info.cart_detail, 0, sizeof(info.cart_detail), str(INFO_S_CART_DETAIL), mb, 0, 0);
	fill(info.about_detail, 0, sizeof(info.about_detail), str(INFO_S_ABOUT_DETAIL), info_version, 0, 0);
	static const u8 icons[R_ITEMS] = {ART_ICON8_KEYS, ART_ICON8_GAMES, ART_ICON8_CART, ART_ICON8_INFO,
					  ART_ICON8_SYSTEM};
	for (u32 i = 0; i < R_ITEMS; i++)
		art_icon8_tile(icons[i], MENU_ICON_ROLE, info.icons[i]);
	info.page = info.open = info.close = 0;
	info.measure_left = 0;
	// recent.txt may hold any row: past the last, the first
	return push_menu(item_row(row < R_ITEMS ? row : 0));
}

static u32 info_row(void)
{
	return row_item(info.page ? info.page - 1u : info.row);
}

static void info_poll(void)
{
	if (info.measure_left) {
		frame_measure("info-page");
		info.measure_left--;
	}
	if (info.open) {
		u32 row = info.open - 1u;
		info.open = 0;
		open_page(row);
	} else if (info.close) {
		info.close = 0;
		close_page();
	}
}

const struct nav_page info_page = {info_enter, info_row, 0, info_poll};
