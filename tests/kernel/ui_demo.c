// The UI demo, in the test kernel only: with L held at boot (mgba-shot
// ezkerneltest.gba -k 1:L) after the unit tests, or with L and SELECT
// held on the emulated cart (tests/shots/ui-*.opts) straight away. It shows
// the Paper clean widgets (B9) on three screens, SELECT going to the next:
//   ui-demo     a game list page: the title bar, a card holding a list of
//               1000 synthetic items in 14 px rows with the sliding pill, a
//               scroll bar, the hint bar
//   ui-widgets  path pills and a storage bar on a card, a section label with
//               its 8x8 icon, a card of 12 px Settings-like rows whose
//               explanation shows in the hint bar
//   ui-reading  a reading view (ui/ui_reading.h): headings and key chips,
//               UP and DOWN scrolling a line
//   ui-popup    the popups (B11) over an A-Z game list: START the game
//               popup (Favourite toggles in place, Delete asks over it), A
//               a short NOR-like popup whose Format all asks on its own,
//               LEFT/RIGHT a letter jump with the letter card, B light or
//               dark at once
// Keys on every screen (but ui-popup's above):
//   d-pad, L/R  move and page (wrapping at the ends on a fresh press)
//   A           a dialog for the item (A or B closes it)
//   START       the next theme and item accent, through a fade
//   SELECT      the next screen
// DIM comes on after 20 s idle. It logs "screen ready: ..." for each screen
// and "ui-dialog" (tests/shots ui-*.opts), and frame-cycles figures for
// scrolling, page changes, the dialog and the theme's fades
// (tests/perf/budget.txt).
#ifdef TORII_TEST_KERNEL
#include <gba_interrupt.h>
#include <stdio.h>
#include <string.h>

#include "torii_test.h"
#include "ui_demo.h"
#include "../../source/cartlog.h"
#include "../../source/engine/engine.h"
#include "../../source/lang.h"
#include "../../source/ui/art.h"
#include "../../source/ui/letter.h"
#include "../../source/ui/ui.h"
#include "../../source/ui/ui_reading.h"
#include "../../source/waitcnt.h"

#define ITEMS 1000

static char name_buf[160] EWRAM_BSS;
static char detail_buf[16] EWRAM_BSS;
static char count_buf[24] EWRAM_BSS;
static char dialog_buf[48] EWRAM_BSS;
static struct {
	const char *measure;
	u32 measure_left;
	u32 theme;
	u8 fading;
	u8 screen;     // the screen shown (SCREEN_*)
	u8 next;       // SELECT: the screen to go to, SCREEN_NONE none
	u8 action;     // ui-popup: the popup to open after the frame (ACT_*)
	u32 dialog_item;
} demo EWRAM_BSS;

enum {
	SCREEN_LIST,
	SCREEN_WIDGETS,
	SCREEN_READING,
	SCREEN_POPUP,
	SCREENS,
	SCREEN_NONE = 0xFF,
};

static const char *const words[] = {"Advance", "Castle", "Dragon", "Echo",   "Forest", "Golden",
				    "Harbor",  "Island", "Jungle", "Knight", "Legend", "Moon"};
static const char *const regions[] = {"(Europe)", "(USA)", "(Japan)", "(World)"};

static u32 demo_count(void *ctx)
{
	(void)ctx;
	return ITEMS;
}

// Appends s at *p (a cheap snprintf: newlib's takes thousands of cycles,
// and a real model has its names ready)
static void put(char **p, const char *s)
{
	while (*s)
		*(*p)++ = *s++;
}

static void put_num(char **p, u32 n)
{
	char digits[10];
	u32 count = 0;
	do {
		digits[count++] = (char)('0' + n % 10);
		n /= 10;
	} while (n);
	while (count)
		*(*p)++ = digits[--count];
}

// Game-list-like names: two words, a number and a region; every seventh a
// long one that the row cuts (and the marquee scrolls)
static int demo_fetch(void *ctx, u32 i, struct ui_item *out)
{
	(void)ctx;
	char *p = name_buf;
	put(&p, words[i % 12]);
	put(&p, " ");
	put(&p, words[(i / 12 + 5) % 12]);
	if (i % 7 == 3) {
		put(&p, " - The Long Journey ");
		put(&p, regions[i % 4]);
		put(&p, " (En,Fr,De,Es,It) (Rev 1)");
	} else {
		put(&p, " ");
		put_num(&p, i + 1);
		put(&p, " ");
		put(&p, regions[i % 4]);
	}
	*p = 0;
	p = detail_buf;
	put_num(&p, 4u << (i % 4));
	put(&p, " MB");
	*p = 0;
	out->text = name_buf;
	out->detail = detail_buf;
	out->ink = i % 11 == 5 ? UI_ROLE_DIM : 0;
	return UI_ITEM_READY;
}

static const struct ui_list_model demo_model = {demo_count, demo_fetch, 0, 0, 0};

static void measure(const char *name, u32 frames)
{
	// This frame is the new measurement's, not the one it replaces
	if (demo.measure_left && demo.measure != name)
		frame_measure_drop(demo.measure);
	demo.measure = name;
	demo.measure_left = frames;
	frame_measure(name);  // this frame too
}

// --- What every screen shares ------------------------------------------------
//
// The planes: BG3 the paper's own (title and hint bars, section labels),
// BG2 cards and what sits on them, BG1 a card's list, BG0 dialogs.

#define TITLE {UI_TITLE, 1, UI_NONE, 3, UI_HFILL, 0, 0, 0, 0, 0, 0, 24, &title_config}
#define HINTS(config) {UI_HINTS, 2, UI_NONE, 3, UI_HFILL | UI_BOTTOM, 0, 0, 0, 0, 0, 0, 16, config}
// A card from y 16 (its body from 20) to 144, a pixel of shadow under it
static const struct ui_card_config page_card = {4, 4, 4, 1, 0};
// A list in it, 8 px in from each side but the scroll bar's column
#define LIST_X 8
#define LIST_W 216
// The lists' node indices (their scroll bars name them)
#define N_LIST_BAR_TARGET 3
#define W_LIST_TARGET 9

static int screen_event(struct ui_view *v, const struct ui_event *e);

// --- The game list -------------------------------------------------------------

static const struct ui_title_config title_config = {"Game Boy Advance", "1/1000", ART_ICON16_GBA};
static const struct ui_hint list_hints[] = {{"A", "open"}, {"START", "theme"}, {"SELECT", "more"}, {"B", "back"}};
static const struct ui_hints_config list_hints_config = {list_hints, 4};
static const struct ui_list_config list_config = {&demo_model, UI_ROW_PITCH_LIST, UI_LIST_WRAP | UI_LIST_MARQUEE, 40,
						  4};
static const struct ui_scrollbar_config bar_config = {N_LIST_BAR_TARGET};

enum {
	N_TITLE,
	N_HINTS,
	N_CARD,
	N_LIST,
	N_BAR,
	N_LIST_NODES
};

static const struct ui_spec list_nodes[] = {
	[N_TITLE] = TITLE,
	[N_HINTS] = HINTS(&list_hints_config),
	[N_CARD] = {UI_CARD, 3, UI_NONE, 2, UI_HFILL, 0, 0, 0, 0, 16, 0, 128, &page_card},
	[N_LIST] = {UI_LIST, 4, UI_NONE, 1, UI_LEFT, UI_FOCUSABLE, 0, 0, LIST_X, 24, LIST_W, 112, &list_config},
	[N_BAR] = {UI_SCROLLBAR, 5, UI_NONE, 2, UI_LEFT, 0, 0, UI_ROLE_PANEL, LIST_X + LIST_W, 24, 8, 112, &bar_config},
};
_Static_assert(N_LIST == N_LIST_BAR_TARGET, "the scroll bar's target");

static const struct ui_view_spec list_view = {"ui-demo", list_nodes, N_LIST_NODES, N_LIST, 0, screen_event};

// --- Widgets -----------------------------------------------------------------

struct setting {
	const char *name, *value, *why;
};
static const struct setting settings[] = {
	{"Theme", "Light", "The menu in light or dark."},
	{"Game view", "List", "Lists with or without box art."},
	{"Start on", "Last place", "Where the menu opens at power-on."},
	{"Dim screen", "5 min", "Dim the screen when left alone."},
	{"Time", "14:32", "The cart's clock."},
	{"Date", "2026-10-20", "The cart's calendar."},
	{"24-hour clock", "On", "14:32 or 2:32 PM."},
	{"Clock for games", "On", "Games see the cart's clock."},
};
#define SETTINGS (sizeof(settings) / sizeof(settings[0]))

static u32 settings_count(void *ctx)
{
	(void)ctx;
	return SETTINGS;
}

static int settings_fetch(void *ctx, u32 i, struct ui_item *out)
{
	(void)ctx;
	out->text = settings[i].name;
	out->detail = settings[i].value;
	return UI_ITEM_READY;
}

static const struct ui_list_model settings_model = {settings_count, settings_fetch, 0, 0, 0};
static const struct ui_title_config widgets_title = {"Widgets", "12:34", ART_ICON16_SET};
static const struct ui_hints_config widgets_hints = {0, 0};
static const struct ui_list_config settings_list = {&settings_model, UI_ROW_PITCH_SETTINGS, UI_LIST_WRAP, 80, 4};
static const struct ui_label_config look_label = {"Look", UI_FONT_BODY, UI_ALIGN_LEFT, 0, ART_ICON8_LOOK + 1};
static const struct ui_label_config used_label = {"40 MB used", UI_FONT_BODY, UI_ALIGN_LEFT, 0, 0};
static const struct ui_label_config free_label = {"24 MB free of 64", UI_FONT_BODY, UI_ALIGN_RIGHT, 0, 0};
static const struct ui_card_config top_card = {4, 4, 4, 1, 0};
static const struct ui_card_config low_card = {4, 4, 4, 1, 0};
static const struct ui_scrollbar_config settings_bar = {W_LIST_TARGET};
static const u32 nor_games[] = {16, 8, 16};  // MB

enum {
	W_TITLE,
	W_HINTS,
	W_TOP,
	W_PATH,
	W_USED,
	W_FREE,
	W_STORAGE,
	W_LOOK,
	W_LOW,
	W_LIST,
	W_BAR,
	W_NODES
};

static const struct ui_spec widget_nodes[] = {
	[W_TITLE] = {UI_TITLE, 1, UI_NONE, 3, UI_HFILL, 0, 0, 0, 0, 0, 0, 24, &widgets_title},
	[W_HINTS] = HINTS(&widgets_hints),
	// The top card (y 16 to 72): path pills, then the storage bar's texts and bar
	[W_TOP] = {UI_CARD, 3, UI_NONE, 2, UI_HFILL, 0, 0, 0, 0, 16, 0, 56, &top_card},
	[W_PATH] = {UI_PATH, 4, UI_NONE, 2, UI_HFILL, 0, 0, UI_ROLE_PANEL, 8, 24, 8, 16, 0},
	[W_USED] = {UI_LABEL, 5, UI_NONE, 2, UI_LEFT, 0, 0, UI_ROLE_PANEL, 8, 40, 112, 16, &used_label},
	[W_FREE] = {UI_LABEL, 6, UI_NONE, 2, UI_RIGHT, 0, UI_ROLE_FAINT, UI_ROLE_PANEL, 8, 40, 112, 16, &free_label},
	[W_STORAGE] = {UI_STORAGE, 7, UI_NONE, 2, UI_HFILL, 0, 0, UI_ROLE_PANEL, 8, 56, 8, 8, 0},
	// A section label on the paper, its icon in the item's ink
	[W_LOOK] = {UI_LABEL, 8, UI_NONE, 3, UI_HFILL, 0, UI_ROLE_ACCENT_INK, 0, 0, 72, 0, 16, &look_label},
	// The lower card (y 88 to 128): two whole 12 px rows (B9's contract)
	[W_LOW] = {UI_CARD, 9, UI_NONE, 2, UI_HFILL, 0, 0, 0, 0, 88, 0, 40, &low_card},
	[W_LIST] = {UI_LIST, 10, UI_NONE, 1, UI_LEFT, UI_FOCUSABLE, 0, 0, LIST_X, 96, LIST_W, 24, &settings_list},
	[W_BAR] = {UI_SCROLLBAR, 11, UI_NONE, 2, UI_LEFT, 0, 0, UI_ROLE_PANEL, LIST_X + LIST_W, 96, 8, 24,
		   &settings_bar},
};
_Static_assert(W_LIST == W_LIST_TARGET, "the scroll bar's target");

static const struct ui_view_spec widgets_view = {"ui-widgets", widget_nodes, W_NODES, W_LIST, 0, screen_event};

// --- Reading -------------------------------------------------------------------

static const char controls_text[] =
	"# Home\n"
	"\xC2\xAB\xC2\xBB\tturn the carousel\n"
	"A\topen the item\n"
	"# Every page\n"
	"B\tback to the carousel, facing the item you came from\n"
	"L R\tpage up, page down\n"
	"# Game lists\n"
	"^v\tmove\n"
	"\xC2\xAB\xC2\xBB\tjump by letter\n"
	"START\tthe game's menu: play, favourite, clean boot, rename, delete\n"
	"\n"
	"Held keys keep going: a held UP or DOWN glides through the list, a held L or R pages "
	"through it.\n"
	"# In game\n"
	"L R START\treset to the menu\n"
	"L R SELECT\tsleep";
#define READING_ROWS 48
static u16 reading_start[READING_ROWS] EWRAM_BSS;
static u8 reading_len[READING_ROWS] EWRAM_BSS;
static u8 reading_kind[READING_ROWS] EWRAM_BSS;
static struct ui_reading reading EWRAM_BSS;

static const struct ui_title_config reading_title = {"Controls", "Info", ART_ICON16_INFO};
static const struct ui_hint reading_hints[] = {{"^v", "scroll"}, {"SELECT", "more"}, {"B", "back"}};
static const struct ui_hints_config reading_hints_config = {reading_hints, 3};
static const struct ui_list_config reading_list = {&ui_reading_model, UI_ROW_PITCH_SETTINGS,
						   UI_LIST_PLAIN | UI_LIST_SCROLL, 0, 4};
static const struct ui_scrollbar_config reading_bar = {N_LIST_BAR_TARGET};

static const struct ui_spec reading_nodes[] = {
	[N_TITLE] = {UI_TITLE, 1, UI_NONE, 3, UI_HFILL, 0, 0, 0, 0, 0, 0, 24, &reading_title},
	[N_HINTS] = HINTS(&reading_hints_config),
	[N_CARD] = {UI_CARD, 3, UI_NONE, 2, UI_HFILL, 0, 0, 0, 0, 16, 0, 128, &page_card},
	[N_LIST] = {UI_LIST, 4, UI_NONE, 1, UI_LEFT, UI_FOCUSABLE, 0, 0, LIST_X, 24, LIST_W, 112, &reading_list},
	[N_BAR] = {UI_SCROLLBAR, 5, UI_NONE, 2, UI_LEFT, 0, 0, UI_ROLE_PANEL, LIST_X + LIST_W, 24, 8, 112,
		   &reading_bar},
};

static const struct ui_view_spec reading_view = {"ui-reading", reading_nodes, N_LIST_NODES, N_LIST, 0, screen_event};

// --- Popups ------------------------------------------------------------------

// A-Z as the lists sort (library_sort_key), with no game under E, I, J, L,
// O, Q, U, V or X, and one under "#"
static const char *const games[] = {
	"007: Everything or Nothing",
	"Advance Wars",
	"Advance Wars 2",
	"Astro Boy",
	"Banjo-Kazooie",
	"Boktai",
	"Castlevania",
	"Columns Crown",
	"Donkey Kong Country",
	"Dragon Quest",
	"F-Zero",
	"Final Fantasy VI Advance",
	"Fire Emblem",
	"Golden Sun",
	"Golden Sun: The Lost Age",
	"Harvest Moon",
	"Kirby & the Amazing Mirror",
	"Kuru Kuru Kururin",
	"Mario & Luigi: Superstar Saga",
	"Mario Kart: Super Circuit",
	"Mega Man Zero 3",
	"Metroid Fusion",
	"Metroid: Zero Mission",
	"Minish Cap",
	"Mother 3",
	"Ninja Five-0",
	"Pokemon Emerald",
	"Pokemon Pinball",
	"Rhythm Tengoku",
	"Riviera",
	"Sonic Advance",
	"Sword of Mana",
	"Tactics Ogre",
	"Tony Hawk's Pro Skater 2",
	"Wario Ware",
	"Yoshi's Island",
	"Zelda: Four Swords",
};
#define GAMES (sizeof(games) / sizeof(games[0]))
static const char *const game_sizes[] = {"4 MB", "8 MB", "16 MB", "32 MB"};
static u8 game_fav[GAMES] EWRAM_BSS;
static char ask_buf[64] EWRAM_BSS;
static struct ui_pop_item menu_items[12] EWRAM_BSS;
static struct ui_popup menu EWRAM_BSS;

enum {
	ACT_NONE,
	ACT_MENU,   // START: the game popup
	ACT_SHORT,  // A: the NOR-like popup
	ACT_LONG,   // START with R held: a popup past UI_POPUP_ROWS, which scrolls
};

static u32 games_count(void *ctx)
{
	(void)ctx;
	return GAMES;
}

static int games_fetch(void *ctx, u32 i, struct ui_item *out)
{
	(void)ctx;
	out->text = games[i];
	out->detail = game_sizes[i % 4];
	return UI_ITEM_READY;
}

static u32 game_letter(void *ctx, u32 i)
{
	(void)ctx;
	return letter_of(games[i]);
}

static const struct ui_list_model games_model = {games_count, games_fetch, 0, 0, 0};
static const struct ui_title_config popup_title = {"Game Boy Advance", "A-Z", ART_ICON16_GBA};
static const struct ui_hint popup_hints[] = {
	{"START", "menu"}, {"A", "more"}, {"\xC2\xAB\xC2\xBB", "letter"}, {"B", "theme"}};
static const struct ui_hints_config popup_hints_config = {popup_hints, 4};
static const struct ui_list_config games_list = {&games_model, UI_ROW_PITCH_LIST, UI_LIST_WRAP, 40, 4};

enum {
	P_TITLE,
	P_HINTS,
	P_CARD,
	P_LIST,
	P_BAR,
	P_LETTER,
	P_NODES
};

static const struct ui_spec popup_nodes[] = {
	[P_TITLE] = {UI_TITLE, 1, UI_NONE, 3, UI_HFILL, 0, 0, 0, 0, 0, 0, 24, &popup_title},
	[P_HINTS] = HINTS(&popup_hints_config),
	[P_CARD] = {UI_CARD, 3, UI_NONE, 2, UI_HFILL, 0, 0, 0, 0, 16, 0, 128, &page_card},
	[P_LIST] = {UI_LIST, 4, UI_NONE, 1, UI_LEFT, UI_FOCUSABLE, 0, 0, LIST_X, 24, LIST_W, 112, &games_list},
	[P_BAR] = {UI_SCROLLBAR, 5, UI_NONE, 2, UI_LEFT, 0, 0, UI_ROLE_PANEL, LIST_X + LIST_W, 24, 8, 112, &bar_config},
	[P_LETTER] = UI_LETTER_NODE(6),
};
_Static_assert(P_LIST == N_LIST_BAR_TARGET, "the scroll bar's target");

static const struct ui_view_spec popup_view = {"ui-popup", popup_nodes,      P_NODES,
					       P_LIST,     UI_VIEW_LR_PAGES, screen_event};

static int popup_screen_keys(struct ui_view *v, u16 keys)
{
	if (keys & INPUT_START) {
		demo.action = (ui_input()->held & INPUT_R) ? ACT_LONG : ACT_MENU;
		return 1;
	}
	if (keys & INPUT_B) {
		ui_theme_switch(ui_theme() == &ui_theme_light ? &ui_theme_dark : &ui_theme_light);
		return 1;
	}
	if (keys & (INPUT_LEFT | INPUT_RIGHT)) {
		u8 list = (u8)(v->first + P_LIST);
		u32 to = letter_jump(GAMES, ui_list_cursor(list), keys & INPUT_LEFT ? -1 : 1, game_letter, 0), prev,
		    next;
		ui_list_set_cursor(list, to);
		letter_around(GAMES, to, game_letter, 0, &prev, &next);
		ui_letter_show((u8)(v->first + P_LETTER), game_letter(0, to), prev, next);
		return 1;
	}
	return 0;
}

// The popups, after a frame (they run frames of their own)
static void popup_action(void)
{
	struct ui_view *v = ui_top();
	u32 action = demo.action;
	demo.action = ACT_NONE;
	if (!v || v->spec != &popup_view)
		return;
	u8 list = (u8)(v->first + P_LIST), hints = (u8)(v->first + P_HINTS);
	u32 i = ui_list_cursor(list);
	if (action == ACT_MENU) {
		static const char *const texts[7] = {"Play",   "Favourite", "Clean boot", "Boot with addon",
						     "Rename", "Game info", "Delete"};
		for (u32 k = 0; k < 7; k++)
			menu_items[k] = (struct ui_pop_item){texts[k], 0, k == 6 ? UI_POP_DANGER : 0};
		menu_items[1].value = game_fav[i] ? "on" : "off";
		menu = (struct ui_popup){games[i], ART_ICON8_GBA + 1, menu_items, 7, 0, 0};
		if (!ui_popup_open(list, hints, &menu))
			return;
		// Favourite toggles in place; Delete asks, and Cancel goes back to
		// the popup
		s32 pick;
		for (;;) {
			pick = ui_popup_choose();
			if (pick == 1) {
				game_fav[i] ^= 1;
				menu_items[1].value = game_fav[i] ? "on" : "off";
				ui_popup_changed(1);
				continue;
			}
			if (pick == 6) {
				snprintf(ask_buf, sizeof(ask_buf), "Delete %s?", games[i]);
				struct ui_confirm ask = {ask_buf, "The save stays.", "Delete", 1};
				int yes = ui_confirm(hints, &ask);
				tlog("ui-demo: delete %s", yes ? "yes" : "no");
				if (!yes)
					continue;
			}
			break;
		}
		ui_popup_close();
		tlog("ui-demo: popup picked %ld", (long)pick);
	} else if (action == ACT_LONG) {
		// Twelve items: seven show, the rest scroll; the first steps its
		// value with LEFT/RIGHT
		static const char *const texts[12] = {"Play",  "Saves",  "Favourite", "Rename",  "Game info", "Cheats",
						      "Patch", "Export", "Import",    "Backups", "Copy",      "Delete"};
		static const char *const boots[3] = {"default", "clean", "addons"};
		u32 boot = 0;
		for (u32 k = 0; k < 12; k++)
			menu_items[k] = (struct ui_pop_item){texts[k], 0, k == 11 ? UI_POP_DANGER : 0};
		menu_items[0].value = boots[0];
		menu_items[0].flags = UI_POP_STEP;
		menu = (struct ui_popup){games[i], ART_ICON8_GBA + 1, menu_items, 12, 0, "boot"};
		if (!ui_popup_open(list, hints, &menu))
			return;
		s32 pick;
		while ((pick = ui_popup_choose()) >= 0 && (pick & (UI_POP_LEFT | UI_POP_RIGHT))) {
			boot = (boot + (pick & UI_POP_LEFT ? 2u : 1u)) % 3u;
			menu_items[0].value = boots[boot];
			ui_popup_changed(0);
			tlog("ui-demo: boot %s", boots[boot]);
		}
		ui_popup_close();
		tlog("ui-demo: long popup picked %ld", (long)pick);
	} else if (action == ACT_SHORT) {
		menu_items[0] = (struct ui_pop_item){"Launch", 0, 0};
		menu_items[1] = (struct ui_pop_item){"Game info", 0, 0};
		menu_items[2] = (struct ui_pop_item){"Format all", 0, UI_POP_DANGER};
		menu = (struct ui_popup){games[i], ART_ICON8_NOR + 1, menu_items, 3, 0, 0};
		if (!ui_popup_open(list, hints, &menu))
			return;
		s32 pick = ui_popup_choose();
		ui_popup_close();
		if (pick == 2) {
			struct ui_confirm ask = {"Format all of NOR?", "3 games go. Saves on SD stay.", "Format", 1};
			tlog("ui-demo: format %s", ui_confirm(hints, &ask) ? "yes" : "no");
		}
	}
}

// --- The dialog ------------------------------------------------------------

static const struct ui_label_config ask_label = {"Open this game?", UI_FONT_HEADING, UI_ALIGN_CENTER, 0, 0};
static const struct ui_label_config name_label = {"", UI_FONT_BODY, UI_ALIGN_CENTER, 0, 0};
static const struct ui_label_config yes_label = {"A open    B cancel", UI_FONT_BODY, UI_ALIGN_CENTER, 0, 0};
static const struct ui_panel_config border_panel = {UI_PANEL_BORDER};

static const struct ui_spec dialog_nodes[] = {
	{UI_PANEL, 1, UI_NONE, 0, UI_HCENTER | UI_VCENTER, 0, 0, UI_ROLE_PANEL, 0, 0, 192, 80, &border_panel},
	{UI_LABEL, 2, 0, 0, UI_HFILL, 0, 0, UI_ROLE_PANEL, 8, 8, 8, 16, &ask_label},
	{UI_LABEL, 3, 0, 0, UI_HFILL, 0, 0, UI_ROLE_PANEL, 8, 32, 8, 16, &name_label},
	{UI_LABEL, 4, 0, 0, UI_HFILL, 0, UI_ROLE_DIM, UI_ROLE_PANEL, 8, 56, 8, 16, &yes_label},
};

static int dialog_event(struct ui_view *v, const struct ui_event *e)
{
	(void)v;
	if (e->type == UI_EV_KEYS && (e->keys & (INPUT_A | INPUT_B))) {
		ui_pop();
		return 1;
	}
	return 0;
}

static const struct ui_view_spec dialog_view = {"ui-dialog", dialog_nodes, 4, UI_NONE, UI_VIEW_MODAL, dialog_event};

// --- Events ------------------------------------------------------------------

static const struct {
	const struct ui_theme *theme;
	u32 item;  // whose accent (enum pal_item)
} looks[] = {
	{&ui_theme_dark, PAL_GBA},
	{&ui_theme_light, PAL_GBA},
	{&ui_theme_dark, PAL_RECENT},
	{&ui_theme_light, PAL_GBC},
};

static int screen_event(struct ui_view *v, const struct ui_event *e)
{
	switch (e->type) {
	case UI_EV_CURSOR: {
		static u32 last;
		u32 jump = e->index > last ? e->index - last : last - e->index;
		last = e->index;
		if (demo.screen == SCREEN_WIDGETS) {
			ui_hints_text((u8)(v->first + W_HINTS), settings[e->index].why);
			return 1;
		}
		if (demo.screen != SCREEN_LIST)
			return 1;
		// A row (a tap, or a held key's glide), a screenful (L, R), or a
		// jump too far to scroll (a wrap): a page change
		if (jump > 9)
			measure("ui-page-change", 12);
		else if (jump > 1)
			measure("ui-list-scroll", 16);
		else
			measure("ui-list-step", 16);
		char *p = count_buf;
		put_num(&p, e->index + 1);
		put(&p, "/");
		put_num(&p, ITEMS);
		*p = 0;
		ui_list_page_label((u8)(v->first + N_LIST), (u8)(v->first + N_TITLE), count_buf);
		return 1;
	}
	case UI_EV_ACTIVATE: {
		if (demo.screen == SCREEN_POPUP)
			demo.action = ACT_SHORT;
		if (demo.screen != SCREEN_LIST)
			return 1;
		struct ui_item item = {0};
		demo_fetch(0, e->index, &item);
		snprintf(dialog_buf, sizeof(dialog_buf), "%s", item.text);
		struct ui_view *d = ui_push(&dialog_view, 0);
		if (d)
			ui_label_set((u8)(d->first + 2), dialog_buf);
		measure("ui-dialog-open", 3);
		return 1;
	}
	case UI_EV_KEYS:
		if (demo.screen == SCREEN_POPUP && popup_screen_keys(v, e->keys))
			return 1;
		if (e->keys & INPUT_START) {
			if (!demo.fading) {
				ui_fade(FADE_BLACK, FADE_MAX, 6);
				demo.fading = 1;
			}
			return 1;
		}
		if (e->keys & INPUT_SELECT) {
			demo.next = (u8)((demo.screen + 1) % SCREENS);
			return 1;
		}
		return 0;
	default:
		return 0;
	}
}

// Fades out, switches the look, fades back in
static void theme_step(void)
{
	if (!demo.fading || ui_fading())
		return;
	demo.theme = (demo.theme + 1) % (sizeof(looks) / sizeof(looks[0]));
	ui_theme_set(looks[demo.theme].theme);
	ui_item_set(looks[demo.theme].item);
	ui_fade(FADE_BLACK, 0, 6);
	demo.fading = 0;
	tlog("ui-demo: theme %s, item %lu", ui_theme()->name, (unsigned long)ui_item());
}

// Shows screen s (the first, or after SELECT: the last goes out first)
static void show(u32 s)
{
	static const struct ui_view_spec *const views[SCREENS] = {&list_view, &widgets_view, &reading_view,
								  &popup_view};
	if (ui_depth()) {
		ui_leave(UI_TR_DEEPER, 1);
		while (ui_depth())
			ui_pop();
	}
	demo.screen = (u8)s;
	struct ui_view *v = ui_push(views[s], s == SCREEN_READING ? &reading : 0);
	if (!v) {
		tlog_error("ui-demo: %s", ui_error());
		return;
	}
	if (s == SCREEN_WIDGETS) {
		ui_path_set((u8)(v->first + W_PATH), "SD/GBA/RPG");
		ui_storage_set((u8)(v->first + W_STORAGE), nor_games, 3, 64);
		ui_hints_text((u8)(v->first + W_HINTS), settings[0].why);
	}
}

void ui_demo(void)
{
	REG_WAITCNT_CART = WAITCNT_MENU;  // as the menu runs
	LoadEnglish();                    // the popups' strings
	irqInit();
	cartlog_install();
	engine_init();
	ui_init(&ui_theme_dark);
	ui_item_set(PAL_GBA);
	memset(&demo, 0, sizeof(demo));
	demo.next = SCREEN_NONE;
	ui_dim_set_frames(20 * 60);
	// The reading view's text, wrapped once for its list
	reading = (struct ui_reading){controls_text, reading_start, reading_len, reading_kind, READING_ROWS, 0};
	if (!ui_reading_wrap(&reading, ui_theme()->body, LIST_W - 2 * 4, ui_theme()->look.reading_key_w))
		tlog_error("ui-demo: the reading text has more than %d rows", READING_ROWS);
	show(SCREEN_LIST);
	for (;;) {
		if (demo.measure_left) {
			frame_measure(demo.measure);
			demo.measure_left--;
		}
		if (ui_fading())
			frame_measure("ui-fade");
		ui_frame();
		theme_step();
		if (demo.action)
			popup_action();
		if (demo.next != SCREEN_NONE && ui_depth() == 1) {
			u32 next = demo.next;
			demo.next = SCREEN_NONE;
			show(next);
		}
	}
}
#endif
