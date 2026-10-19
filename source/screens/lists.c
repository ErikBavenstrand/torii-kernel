// The SD list, the NOR list and Recently Played on the engine (lists.h).
//
// Screens and keys are today's (docs/plans/v0.3-decisions.md, Port guide):
//   SD list   d-pad move (LEFT/RIGHT a page), A opens a folder or a game,
//             B the folder above, R the NOR list, START Recently Played,
//             L+START deletes the file under the cursor
//   NOR list  A the NOR game menu, L the SD list, R SETTINGS
//   Recently  UP/DOWN, A opens the game, B the SD list
//   menus     UP/DOWN (wrapping on a fresh press), A, B; the game menu's
//             LEFT/RIGHT pick the save type, L held with A starts with a
//             hard reset
//
// The list's names are in PSRAM (nameindex.h). Event handlers only record
// what to do (lists.act); the loop does it between frames, where it may read
// the card, pop and push views, or leave the engine (launches and NOR writes,
// whose dialogs run a session of their own: they fill PSRAM and take
// pReadCache, so the folder is listed again when the lists come back).
// Everything else, SETTINGS, help and cheats included, runs in the lists'
// one engine session: a screen replaces another with a transition
// (ui_leave), so switching never goes black.
#include <gba_input.h>
#include <gba_video.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "lists.h"
#include "../cartlog.h"
#include "../diag.h"
#include "../ezkernel.h"
#include "../lang.h"
#include "../logbuf.h"
#include "../nameindex.h"
#include "../NORflash_OP.h"
#include "../RTC.h"
#include "../settings.h"
#include "../showcht.h"
#include "../sort.h"
#include "../tlog.h"
#include "../engine/engine.h"
#include "../engine/text.h"
#include "../ui/dialog.h"
#include "../ui/ui.h"

// The SD list's folder (ezkernelnew.c): the launch path opens games there
extern TCHAR currentpath[MAX_path_len];
extern TCHAR currentpath_temp[MAX_path_len];
extern FILINFO fileinfo;
extern DIR dir;

#define LIST_ROWS 9       // rows a page of the list shows: (160 - 16) / 16
#define FOLDER_DEPTH 100   // folders deep whose cursor is remembered
#define CLOCK_POLL 15      // frames between clock reads
#define MEASURE_FRAMES 16     // frames measured after a cursor move
#define IDLE_FLUSH_FRAMES 30       // frames with no key held before a flush of a half-full log
#define IDLE_FLUSH_ALL_FRAMES 300  // and before a flush of any log line

enum screen {
	SCR_SD,
	SCR_NOR,
	SCR_RECENT,
	SCREENS,
};

enum action {
	ACT_NONE,
	ACT_FOLDER_IN,    // arg: the folder's index
	ACT_FOLDER_UP,
	ACT_OPEN,         // arg: the file's index in the list
	ACT_TO_SD,
	ACT_TO_NOR,
	ACT_SETTINGS,
	ACT_RECENT,
	ACT_RECENT_OPEN,  // arg: the row
	ACT_DELETE_ASK,   // arg: the file's index in the list
	ACT_NOR_MENU,     // arg: the NOR game
	ACT_MENU_A,       // arg: the menu line; key_l
	ACT_MENU_B,
};

// What a message or confirm dialog (ui/dialog.h) does once answered
enum after {
	AFTER_POP,     // back to the list under it (the dialog has gone)
	AFTER_SD,      // the SD list, listed again
	AFTER_NOR,     // the NOR list, read again
	AFTER_DELETE,  // confirm: delete current_filename
	AFTER_FORMAT,  // confirm: FORMAT ALL
	AFTER_BACKUP,  // confirm: overwrite the NOR game's save file
};

enum icon {
	ICON_FOLDER,
	ICON_GBA,
	ICON_GB,
	ICON_NES,
	ICON_OTHER,
	ICON_NOR,
	ICONS,
};

struct sd_file {
	u32 name;  // nameindex handle
	u32 size;
};

static u32 sd_folders[MAX_folder] EWRAM_BSS;  // nameindex handles
static struct sd_file sd_files[MAX_files] EWRAM_BSS;
static u16 folder_cursor[FOLDER_DEPTH] EWRAM_BSS;
static u32 icons[ICONS][32] EWRAM_BSS ALIGN(4);

static struct {
	u8 screen;        // enum screen the list view shows
	u8 engine;        // the engine is running
	u8 act;           // enum action, done after the frame
	u8 key_l;         // L held with the action's key
	u32 arg;
	u8 depth;         // folders below the root, + 1
	u8 menu;          // the menu shown: 0 none, 1 game menu, 2 NOR game menu
	u8 menu_rows;     // rows it shows
	u8 menu_max;      // the last row the cursor reaches
	u8 menu_line;
	u8 after;         // enum after, for the dialog shown
	u8 from_recent;   // the game menu is for a Recently Played entry
	u8 queued_a;      // A during a page change, for once the page is up
	u8 icons_made;
	u32 nor_game;     // the NOR game menu's game
	u32 recent_count;
	u32 list_first;   // pool index of the list view's first node
	u32 dialog_first;
	u32 clock_wait;
	u8 clock_hms[3];
	const char *measure;
	u32 measure_left;
	InputWrap wrap;       // the menus' cursor
	InputWrap list_wrap;  // the list's
	struct game_pick game;
	char title[MAX_path_len];
	char clock[12];
	char count[24];
	char detail[16];
	char row[32];
	char save_row[40];
} lists EWRAM_BSS;

// --- Icons ----------------------------------------------------------------

// 16x16, in the theme's icon colours: A folder, B GBA, C GB, D NES, E light,
// F dark (ui.h)
static const char *const icon_art[ICONS] = {
	"................"
	".AAAAA.........."
	"AAAAAAA........."
	"AAAAAAAAAAAAAAA."
	"AAAAAAAAAAAAAAA."
	"AAAAAAAAAAAAAAA."
	"AAAAAAAAAAAAAAA."
	"AAAAAAAAAAAAAAA."
	"AAAAAAAAAAAAAAA."
	"AAAAAAAAAAAAAAA."
	"AAAAAAAAAAAAAAA."
	"AAAAAAAAAAAAAAA."
	"AAAAAAAAAAAAAAA."
	".AAAAAAAAAAAAA.."
	"................"
	"................",
	"................"
	"..BBBBBBBBBBBB.."
	"..BBBBBBBBBBBB.."
	"..BEEEEEEEEEEB.."
	"..BEBBBBBBBBEB.."
	"..BEBBBBBBBBEB.."
	"..BEBBBBBBBBEB.."
	"..BEEEEEEEEEEB.."
	"..BBBBBBBBBBBB.."
	"..BBBBBBBBBBBB.."
	"..BBFBFBFBFBBB.."
	"..BBFBFBFBFBBB.."
	"..BBBBBBBBBBBB.."
	"................"
	"................"
	"................",
	"................"
	"...CCCCCCCCCC..."
	"...CEEEEEEEEC..."
	"...CEFFFFFFEC..."
	"...CEFFFFFFEC..."
	"...CEFFFFFFEC..."
	"...CEEEEEEEEC..."
	"...CCCCCCCCCC..."
	"...CCFCCCCDCC..."
	"...CFFFCCDCDC..."
	"...CCFCCCCDCC..."
	"...CCCCCCCCCC..."
	"...CCCCCCCCCC..."
	"....CCCCCCCC...."
	"................"
	"................",
	"................"
	"................"
	".DDDDDDDDDDDDDD."
	".DDDDDDDDDDDDDD."
	".DDEEEEEEEEEEDD."
	".DDEFFFFFFFFEDD."
	".DDEFFFFFFFFEDD."
	".DDEFFFFFFFFEDD."
	".DDEEEEEEEEEEDD."
	".DDDDDDDDDDDDDD."
	".DDDDDDDDDDDDDD."
	"..DDDDDDDDDDDD.."
	"..DFDFDFDFDFDD.."
	"..DDDDDDDDDDDD.."
	"................"
	"................",
	"................"
	"...FFFFFFF......"
	"...FEEEEEFF....."
	"...FEEEEEFEF...."
	"...FEEEEEFFFF..."
	"...FEFFFFEEEF..."
	"...FEEEEEEEEF..."
	"...FEFFFFFFEF..."
	"...FEEEEEEEEF..."
	"...FEFFFFFFEF..."
	"...FEEEEEEEEF..."
	"...FEFFFFEEEF..."
	"...FEEEEEEEEF..."
	"...FFFFFFFFFF..."
	"................"
	"................",
	"................"
	"..E.E.E.E.E.E..."
	"..E.E.E.E.E.E..."
	".BBBBBBBBBBBBB.."
	".BBBBBBBBBBBBB.."
	".BEBBBBBBBBBBB.."
	".BBBBBBBBBBBBB.."
	".BBBBBBBBBBBBB.."
	".BBBBBBBBBBBBB.."
	".BBBBBBBBBBBBB.."
	".BBBBBBBBBBBBB.."
	".BBBBBBBBBBBBB.."
	"..E.E.E.E.E.E..."
	"..E.E.E.E.E.E..."
	"................"
	"................",
};

static void make_icons(void)
{
	for (u32 k = 0; k < ICONS; k++) {
		for (u32 t = 0; t < 4; t++) {
			u32 tx = t & 1, ty = t >> 1;
			for (u32 y = 0; y < 8; y++) {
				u32 row = 0;
				for (u32 x = 0; x < 8; x++) {
					char c = icon_art[k][(ty * 8 + y) * 16 + tx * 8 + x];
					u32 index = c == '.' ? 0 : (u32)(UI_ROLE_ICON + (c - 'A'));
					row |= index << (4 * x);
				}
				icons[k][t * 8 + y] = row;
			}
		}
	}
	lists.icons_made = 1;
}

static const u32 *type_icon(const char *name)
{
	switch (Check_file_type((TCHAR *)name)) {
	case 0:
		return icons[ICON_GBA];
	case 1:
	case 2:
		return icons[ICON_GB];
	case 3:
		return icons[ICON_NES];
	default:
		return icons[ICON_OTHER];
	}
}

// --- Text helpers -----------------------------------------------------------

// Appends s at *p (newlib's printf takes thousands of cycles a row)
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

// A file size as the list shows it: 16M, 512K, 300B
static void size_text(char *out, u32 size)
{
	char *p = out;
	if (size >> 20) {
		put_num(&p, size >> 20);
		put(&p, "M");
	} else if (size >> 10) {
		put_num(&p, size >> 10);
		put(&p, "K");
	} else {
		put_num(&p, size);
		put(&p, "B");
	}
	*p = 0;
}

// --- The list model -----------------------------------------------------------

static u32 list_count(void *ctx)
{
	(void)ctx;
	switch (lists.screen) {
	case SCR_SD:
		return folder_total + game_total_SD;
	case SCR_NOR:
		return game_total_NOR;
	default:
		return lists.recent_count;
	}
}

static int list_fetch(void *ctx, u32 i, struct ui_item *out)
{
	(void)ctx;
	if (i >= list_count(ctx))
		return UI_ITEM_PENDING;
	if (lists.screen == SCR_SD) {
		if (i < folder_total) {
			out->text = names_get(sd_folders[i]);
			out->detail = "DIR";
			out->icon = icons[ICON_FOLDER];
		} else {
			const struct sd_file *f = &sd_files[i - folder_total];
			out->text = names_get(f->name);
			size_text(lists.detail, f->size);
			out->detail = lists.detail;
			out->icon = type_icon(out->text);
		}
	} else if (lists.screen == SCR_NOR) {
		char *p = lists.detail;
		put_num(&p, pNorFS[i].filesize >> 20);
		put(&p, "M");
		*p = 0;
		out->text = pNorFS[i].filename;
		out->detail = lists.detail;
		out->icon = icons[ICON_NOR];
	} else {
		const char *entry = p_recently_play[i];
		out->text = entry;
		out->icon = Recently_play_nor(entry) ? icons[ICON_NOR] : type_icon(entry);
	}
	return UI_ITEM_READY;
}

static const struct ui_list_model list_model = {list_count, list_fetch, 0};

// --- The menus' model -----------------------------------------------------------

// The CHEAT row: "CHEAT" until the cheat screen has been opened, then
// "CHEAT(n)" (showcht.c)
static const char *cheat_row(void)
{
	cheat_menu_label(lists.row, sizeof(lists.row), gl_rom_menu[5]);
	return lists.row;
}

static int menu_fetch(void *ctx, u32 i, struct ui_item *out)
{
	(void)ctx;
	if (i >= lists.menu_rows)
		return UI_ITEM_PENDING;
	if (lists.menu == 2) {
		out->text = gl_nor_op[i];
		return UI_ITEM_READY;
	}
	out->text = gl_rom_menu[i];
	if (i == 1 && (gl_reset_on | gl_rts_on | gl_sleep_on | gl_cheat_on) == 0)
		out->ink = UI_ROLE_DIM;  // BOOT WITH ADDON with no addon on: nothing to do
	if (i == 4) {
		static const char *const types[] = {"AUTO", "SRAM", "EEPROM8K", "EEPROM512", "FLASH64", "FLASH128"};
		snprintf(lists.save_row, sizeof(lists.save_row), "%s <%s>", gl_rom_menu[4],
			 types[lists.game.save_num < 6 ? lists.game.save_num : 0]);
		out->text = lists.save_row;
	}
	if (i == 5) {
		out->text = cheat_row();
		if (lists.game.havecht && gl_cheat_on == 0)
			out->ink = UI_ROLE_DIM;  // the CHEAT addon is off
		else if (gl_cheat_count)
			out->ink = UI_ROLE_ACCENT;
	}
	return UI_ITEM_READY;
}

static u32 menu_count(void *ctx)
{
	(void)ctx;
	return lists.menu_rows;
}

static const struct ui_list_model menu_model = {menu_count, menu_fetch, 0};

// --- Layouts -----------------------------------------------------------------

enum {
	N_BAR,
	N_TITLE,
	N_CLOCK,
	N_COUNT,
	N_LIST,
	N_EMPTY,
	N_NODES,
};

static const struct ui_label_config title_label = {"", UI_FONT_BODY, UI_ALIGN_LEFT, 0};
static const struct ui_label_config clock_label = {"", UI_FONT_BODY, UI_ALIGN_CENTER, UI_LABEL_SWAP};
static const struct ui_label_config count_label = {"", UI_FONT_BODY, UI_ALIGN_RIGHT, UI_LABEL_SWAP};
static const struct ui_label_config empty_label = {"", UI_FONT_BODY, UI_ALIGN_LEFT, 0};
static const struct ui_list_config list_config = {&list_model, 16, 0, UI_LIST_MARQUEE | UI_LIST_ICONS, 40};

// The list isn't focusable: the view's handler takes every key, so L and R
// switch screens as they always did instead of paging the list
static const struct ui_spec list_nodes[N_NODES] = {
	[N_BAR] = {.kind = UI_PANEL,
		   .id = 1,
		   .parent = UI_NONE,
		   .plane = 2,
		   .anchor = UI_HFILL,
		   .paper = UI_ROLE_PANEL,
		   .h = 16},
	[N_TITLE] = {.kind = UI_LABEL,
		     .id = 2,
		     .parent = N_BAR,
		     .plane = 2,
		     .anchor = UI_LEFT | UI_VFILL,
		     .paper = UI_ROLE_PANEL,
		     .w = 96,
		     .data = &title_label},
	[N_CLOCK] = {.kind = UI_LABEL,
		     .id = 3,
		     .parent = N_BAR,
		     .plane = 2,
		     .anchor = UI_LEFT | UI_VFILL,
		     .ink = UI_ROLE_DIM,
		     .paper = UI_ROLE_PANEL,
		     .x = 96,
		     .w = 64,
		     .data = &clock_label},
	[N_COUNT] = {.kind = UI_LABEL,
		     .id = 4,
		     .parent = N_BAR,
		     .plane = 2,
		     .anchor = UI_RIGHT | UI_VFILL,
		     .ink = UI_ROLE_DIM,
		     .paper = UI_ROLE_PANEL,
		     .w = 80,
		     .data = &count_label},
	[N_LIST] = {.kind = UI_LIST,
		    .id = 5,
		    .parent = UI_NONE,
		    .plane = 1,
		    .anchor = UI_FILL,
		    .paper = UI_ROLE_BG,
		    .y = 16,
		    .data = &list_config},
	[N_EMPTY] = {.kind = UI_LABEL,
		     .id = 6,
		     .parent = UI_NONE,
		     .plane = 2,
		     .anchor = UI_HFILL,
		     .flags = UI_HIDDEN,
		     .ink = UI_ROLE_DIM,
		     .y = 16,
		     .h = 16,
		     .data = &empty_label},
};

static int list_event(struct ui_view *v, const struct ui_event *e);
static const struct ui_view_spec list_views[SCREENS] = {
	[SCR_SD] = {"sd-list", list_nodes, N_NODES, 10, UI_NONE, UI_VIEW_LR_PAGES, list_event},
	[SCR_NOR] = {"nor-list", list_nodes, N_NODES, 11, UI_NONE, UI_VIEW_LR_PAGES, list_event},
	[SCR_RECENT] = {"recent", list_nodes, N_NODES, 12, UI_NONE, 0, list_event},
};

// A menu: a list of rows on plane 3 between two panel strips on plane 0, the
// lower one with the keys' hint. The list fills its rows exactly, so nothing
// of the view under it shows through.
#define MENU_W 128  // six rows on plane 3 must fit beside the main list's tiles
#define MENU_H(rows) (8 + (rows) * 16 + 16)
#define MENU_Y(rows) (((160 - MENU_H(rows)) / 2) & ~7)
#define M_LIST 1
#define M_HINT 3

static const struct ui_list_config menu_config = {&menu_model, 16, 0, UI_LIST_FIXED, 0};
static const struct ui_label_config hint_label = {"", UI_FONT_BODY, UI_ALIGN_CENTER, 0};

#define MENU_NODES(rows) \
	{ \
		{.kind = UI_PANEL, \
		 .id = 1, \
		 .parent = UI_NONE, \
		 .plane = 0, \
		 .anchor = UI_HCENTER, \
		 .paper = UI_ROLE_PANEL, \
		 .y = MENU_Y(rows), \
		 .w = MENU_W, \
		 .h = 8}, \
		{.kind = UI_LIST, \
		 .id = 2, \
		 .parent = UI_NONE, \
		 .plane = 3, \
		 .anchor = UI_HCENTER, \
		 .paper = UI_ROLE_PANEL, \
		 .y = MENU_Y(rows) + 8, \
		 .w = MENU_W, \
		 .h = (rows) * 16, \
		 .data = &menu_config}, \
		{.kind = UI_PANEL, \
		 .id = 3, \
		 .parent = UI_NONE, \
		 .plane = 0, \
		 .anchor = UI_HCENTER, \
		 .paper = UI_ROLE_PANEL, \
		 .y = MENU_Y(rows) + 8 + (rows) * 16, \
		 .w = MENU_W, \
		 .h = 16}, \
		{.kind = UI_LABEL, \
		 .id = 4, \
		 .parent = 2, \
		 .plane = 0, \
		 .anchor = UI_FILL, \
		 .ink = UI_ROLE_DIM, \
		 .paper = UI_ROLE_PANEL, \
		 .data = &hint_label}, \
	}

static const struct ui_spec menu3_nodes[] = MENU_NODES(3);
static const struct ui_spec menu5_nodes[] = MENU_NODES(5);
static const struct ui_spec menu6_nodes[] = MENU_NODES(6);

static int menu_event(struct ui_view *v, const struct ui_event *e);
static const struct ui_view_spec file_menu5 = {"file-menu", menu5_nodes, 4, 13, UI_NONE, UI_VIEW_MODAL, menu_event};
static const struct ui_view_spec file_menu6 = {"file-menu", menu6_nodes, 4, 13, UI_NONE, UI_VIEW_MODAL, menu_event};
static const struct ui_view_spec nor_menu3 = {"nor-menu", menu3_nodes, 4, 14, UI_NONE, UI_VIEW_MODAL, menu_event};
static const struct ui_view_spec nor_menu5 = {"nor-menu", menu5_nodes, 4, 14, UI_NONE, UI_VIEW_MODAL, menu_event};

// --- Engine and screens ------------------------------------------------------------

static void engine_enter(void)
{
	if (lists.engine)
		return;
	if (!ui_active()) {
		// Black until the first commit (dialog_blank: mode 0, no layer on),
		// not a forced blank, which shows white; the list comes in from it
		dialog_blank();
		engine_init();
		ui_init(NULL);
		ui_enter(UI_TR_LEAVE, 0);
	} else
		ui_enter(UI_TR_LEAVE, 0);  // the boot's, handed over: in with the theme it set
	if (!lists.icons_made)
		make_icons();
	lists.engine = 1;
}

// Leaves the engine for code that runs a session of its own, or none: the
// screen fades to black, which stays meanwhile
static void engine_leave(void)
{
	if (!lists.engine)
		return;
	ui_leave(UI_TR_LEAVE, 0);
	ui_shutdown();
	dialog_blank();
	lists.engine = 0;
}

// The screen goes out as kind says (ui_leave): before the next one's slow
// reads, so a key answers at once
static void go(u32 kind, s32 way)
{
	if (lists.engine)
		ui_leave(kind, way);
}

static u8 list_node(void)
{
	return (u8)(lists.list_first + N_LIST);
}

static u32 cursor(void)
{
	return ui_depth() ? ui_list_cursor(list_node()) : 0;
}

static void measure(const char *name, u32 frames)
{
	lists.measure = name;
	lists.measure_left = frames;
}

static void count_update(u32 at)
{
	u32 count = list_count(0);
	char *p = lists.count;
	put_num(&p, count ? at + 1 : 0);
	put(&p, " / ");
	put_num(&p, count);
	*p = 0;
	if (ui_depth())
		ui_label_set((u8)(lists.list_first + N_COUNT), lists.count);
}

static void clock_read(void)
{
	u8 t[3];
	rtc_enable();
	rtc_gettime(t);
	rtc_disenable();
	delay(5);
	u8 hms[3] = {UNBCD(t[0] & 0x3F), UNBCD(t[1] & 0x7F), UNBCD(t[2] & 0x7F)};
	if (hms[0] > 23)
		hms[0] = 0;
	if (hms[1] > 59)
		hms[1] = 0;
	if (hms[2] > 59)
		hms[2] = 0;
	if (!memcmp(hms, lists.clock_hms, 3) && lists.clock[0])
		return;
	memcpy(lists.clock_hms, hms, 3);
	snprintf(lists.clock, sizeof(lists.clock), "%02u:%02u:%02u", hms[0], hms[1], hms[2]);
	if (ui_depth())
		ui_label_set((u8)(lists.list_first + N_CLOCK), lists.clock);
}

// The clock under a dialog (dialog_set_tick), while the list is still up
static void clock_tick(void)
{
	const struct ui_view *v = ui_view_at(0);
	if (v && v->ctx == &lists)
		clock_read();
}

// The SD list's title: the folder's path, or its last part if the path
// doesn't fit
static void sd_title(void)
{
	const char *s = currentpath;
	u32 clip = list_nodes[N_TITLE].w - 2 * ui_theme()->pad;
	if (text_width(ui_theme()->body, s, strlen(s)) > clip) {
		const char *slash = strrchr(s, '/');
		if (slash && slash[1])
			s = slash + 1;
	}
	snprintf(lists.title, sizeof(lists.title), "%s", s);
}

// Shows screen s with the cursor at at: every view goes (kind's way, way
// as ui_leave's dir), the list's comes
static void show(enum screen s, u32 at, u32 kind, s32 way)
{
	go(kind, way);  // nothing if the screen went already
	engine_enter();
	// What the last screen's handlers recorded since (keys while a dialog
	// closed) was for it: keys held for this one come with it (ui_leave)
	lists.act = ACT_NONE;
	while (ui_depth())
		ui_pop();
	lists.screen = (u8)s;
	lists.menu = 0;
	lists.queued_a = 0;
	struct ui_view *v = ui_push(&list_views[s], &lists);
	if (!v) {  // a layout over budget: nothing would take keys
		tlog_error("lists: %s", ui_error());
		TLOG_ASSERT(v);
		return;
	}
	lists.list_first = v->first;
	if (s == SCR_SD)
		sd_title();
	else
		snprintf(lists.title, sizeof(lists.title), "%s", s == SCR_NOR ? gl_nor_title : gl_recently_play);
	ui_label_set((u8)(v->first + N_TITLE), lists.title);
	lists.clock[0] = 0;
	clock_read();
	ui_list_reset(list_node(), at);
	input_wrap_init(&lists.list_wrap, ui_input(), cursor());
	count_update(cursor());
	if (s == SCR_RECENT && !lists.recent_count) {
		ui_label_set((u8)(v->first + N_EMPTY), gl_no_game_played);
		ui_show((u8)(v->first + N_EMPTY), 1);
	}
	measure("lists-screen", 3);
}

static const char *folder_name(const void *e)
{
	return names_get(*(const u32 *)e);
}

static const char *file_name(const void *e)
{
	return names_get(((const struct sd_file *)e)->name);
}

// Lists currentpath: folders, then files, each sorted by name
static void list_folder(void)
{
	folder_total = 0;
	game_total_SD = 0;
	f_chdir(currentpath);  // the folder file names are relative to (a launch may have left /SAVER)
	names_reset();
	if (f_opendir(&dir, currentpath) == FR_OK) {
		for (;;) {
			FRESULT res = f_readdir(&dir, &fileinfo);
			if (res != FR_OK || fileinfo.fname[0] == 0)
				break;
			// Hidden and system entries stay out; the other attributes,
			// such as read-only or archive, don't matter
			if (fileinfo.fattrib & (AM_HID | AM_SYS | 0x08))  // 0x08: a volume label
				continue;
			int is_dir = (fileinfo.fattrib & AM_DIR) != 0;
			if (is_dir ? folder_total >= MAX_folder : game_total_SD >= MAX_files)
				continue;  // full: skip, but keep reading the other kind
			u32 h = names_add(fileinfo.fname);
			if (h == NAMES_NONE)
				continue;
			if (is_dir) {
				sd_folders[folder_total++] = h;
			} else {
				sd_files[game_total_SD].name = h;
				sd_files[game_total_SD++].size = (u32)fileinfo.fsize;
			}
		}
	}
	f_closedir(&dir);
	sort_by(sd_folders, folder_total, sizeof(sd_folders[0]), pReadCache + READCACHE_SORT_WORK, folder_name);
	sort_by(sd_files, game_total_SD, sizeof(sd_files[0]), pReadCache + READCACHE_SORT_WORK, file_name);
	tlog_debug("lists: %s: %lu folders, %lu files, %lu bytes of names", currentpath, (unsigned long)folder_total,
		   (unsigned long)game_total_SD, (unsigned long)names_used());
}

static void read_nor(void)
{
	Read_NOR_info();
	game_total_NOR = GetFileListFromNor();
}

static void show_sd(u32 at, u32 kind, s32 way)
{
	go(kind, way);
	list_folder();
	show(SCR_SD, at, kind, way);
}

static void show_nor(u32 at, u32 kind, s32 way)
{
	go(kind, way);
	read_nor();
	show(SCR_NOR, at, kind, way);
}

static void dialog_answer(int yes);

// After SETTINGS, in the same session: the theme, accent and DIM it may
// have changed, as a new session would read them (no view is up)
static void settings_changed(void)
{
	u16 accent;
	const struct ui_theme *t = ui_theme_setting(&accent);
	// SETTINGS sets the theme as it changes: again only if it didn't (the
	// wallpaper shows meanwhile, and would be decoded over itself)
	if (t != ui_theme() || accent != ui_accent())
		ui_theme_set(t, accent);
	ui_dim_set_minutes(ui_dim_setting());
}

// A message (confirm 0) or a question (ui/dialog.h), over the list with its
// menus closed; after says what follows. Runs its own frames, so it is only
// called from do_action, outside ui_frame.
static void dialog_kind(const char *title, const char *text, int confirm, enum after after, u32 kind)
{
	while (ui_depth() > 1)
		ui_pop();
	lists.menu = 0;
	lists.after = (u8)after;
	int yes = 0;
	if (confirm)
		yes = dialog_confirm(title, text, kind);
	else
		dialog_message(title, text, kind);
	dialog_answer(yes);
}

static void dialog(const char *text, int confirm, enum after after)
{
	dialog_kind(NULL, text, confirm, after, DIALOG_INFO);
}

static void error_dialog(u8 error_num, enum after after)
{
	const char *msg = Error_message(error_num);
	tlog_error("error %u: %s", error_num, msg);
	tlog_flush();
	dialog_kind(NULL, msg, 0, after, DIALOG_ERROR);
}

static void menu_open(u32 which, u32 rows, u32 max, u32 line)
{
	while (ui_depth() > 1)
		ui_pop();
	lists.menu = (u8)which;
	lists.menu_rows = (u8)rows;
	lists.menu_max = (u8)max;
	lists.menu_line = (u8)line;
	const struct ui_view_spec *spec;
	if (which == 1)
		spec = rows == 6 ? &file_menu6 : &file_menu5;
	else
		spec = rows == 5 ? &nor_menu5 : &nor_menu3;
	struct ui_view *d = ui_push(spec, &lists);
	if (!d) {
		tlog_error("lists: %s", ui_error());
		lists.menu = 0;
		return;
	}
	lists.dialog_first = d->first;
	ui_list_reset((u8)(d->first + M_LIST), line);
	ui_label_set((u8)(d->first + M_HINT), gl_menu_btn);
	input_wrap_init(&lists.wrap, ui_input(), line);
	measure("lists-menu-open", 3);
}

// --- Events (they only record what to do) ---------------------------------------

// Moves the list's cursor delta items for key: past an end it stops there
// first, and a fresh press at the end wraps to the other end (input_move); a
// held key stays at the end, as each wrap is a page change
static void move(u8 node, u16 key, s32 delta, u32 count, u32 at)
{
	if (!count)
		return;
	u32 to;
	if (ui_input()->pressed & key) {
		to = input_move(ui_input(), &lists.list_wrap, key, at, count, delta);
	} else {
		s32 t = (s32)at + delta;
		to = t < 0 ? 0 : (u32)t >= count ? count - 1 : (u32)t;
	}
	if (to == at)
		return;
	ui_list_move(node, to, key);
	count_update(to);
	// A move too far to scroll to (a wrap) is a page change: its own figure
	if (ui_list_paging(node))
		measure("lists-page", 4);
	else
		measure("lists-scroll", MEASURE_FRAMES);
}

static void act(u8 a, u32 arg)
{
	lists.act = a;
	lists.arg = arg;
	lists.key_l = (ui_input()->held & INPUT_L) != 0;
}

static int list_event(struct ui_view *v, const struct ui_event *e)
{
	(void)v;
	if (e->type != UI_EV_KEYS || lists.act)
		return e->type == UI_EV_KEYS;
	u16 k = e->keys, pressed = ui_input()->pressed;
	u8 node = list_node();
	u32 count = list_count(0), at = ui_list_cursor(node);
	u32 page = lists.screen == SCR_RECENT ? 0 : LIST_ROWS;
	// Keys other than the d-pad act first, on the row shown as selected (a
	// held d-pad fires every frame at top speed, so they often come with a
	// move)
	u16 moves = k & INPUT_DPAD;
	k &= (u16)~INPUT_DPAD;
	if ((k & INPUT_A) && ui_list_paging(node)) {
		// The page the cursor moved to isn't drawn yet: A waits for it, as
		// the list's own A does (lists_run hands it back)
		lists.queued_a = 1;
		k &= (u16)~INPUT_A;
	}
	if (!k) {
	} else if (lists.screen == SCR_RECENT) {
		if (k & INPUT_A) {
			if (count)
				act(ACT_RECENT_OPEN, at);
		} else if (k & INPUT_B) {
			act(ACT_TO_SD, 0);
		}
	} else if (k & pressed & INPUT_L) {
		if (lists.screen == SCR_NOR)
			act(ACT_TO_SD, 0);
	} else if (k & pressed & INPUT_R) {
		act(lists.screen == SCR_SD ? ACT_TO_NOR : ACT_SETTINGS, 0);
	} else if (k & INPUT_B) {
		if (lists.screen == SCR_SD && strcmp(currentpath, "/") != 0)
			act(ACT_FOLDER_UP, 0);
	} else if (k & INPUT_A) {
		if (lists.screen == SCR_NOR) {
			if (count)
				act(ACT_NOR_MENU, at);
		} else if (at < folder_total) {
			act(ACT_FOLDER_IN, at);
		} else if (at < count) {  // an empty folder has no file under the cursor
			act(ACT_OPEN, at);
		}
	} else if (k & INPUT_START) {
		if (lists.screen == SCR_SD) {
			if (!(ui_input()->held & INPUT_L))
				act(ACT_RECENT, 0);
			else if (at >= folder_total && at < count)
				act(ACT_DELETE_ASK, at);
		}
	}
	// SELECT toggled the thumbnail; v0.3 shows none (v0.4's covers will)
	if (lists.act)
		return 1;  // the move waits: the action is on the row the user saw
	// Then one move a frame, a fresh press first
	if (moves & pressed)
		moves &= pressed;
	if (moves & INPUT_DOWN)
		move(node, INPUT_DOWN, 1, count, at);
	else if (moves & INPUT_UP)
		move(node, INPUT_UP, -1, count, at);
	else if ((moves & INPUT_LEFT) && page)
		move(node, INPUT_LEFT, -(s32)page, count, at);
	else if ((moves & INPUT_RIGHT) && page)
		move(node, INPUT_RIGHT, (s32)page, count, at);
	return 1;
}

static int menu_event(struct ui_view *v, const struct ui_event *e)
{
	if (e->type != UI_EV_KEYS || lists.act)
		return e->type == UI_EV_KEYS;
	u16 k = e->keys;
	u8 node = (u8)(v->first + M_LIST);
	u32 line = lists.menu_line;
	if (k & (INPUT_DOWN | INPUT_UP)) {
		u16 key = (k & INPUT_DOWN) ? INPUT_DOWN : INPUT_UP;
		line = input_move(ui_input(), &lists.wrap, key, line, lists.menu_max + 1u, key == INPUT_DOWN ? 1 : -1);
	} else if (k & (INPUT_LEFT | INPUT_RIGHT)) {
		if (lists.menu == 1 && line == 4) {  // the save type
			if ((k & INPUT_LEFT) && lists.game.save_num)
				lists.game.save_num--;
			else if ((k & INPUT_RIGHT) && lists.game.save_num < 5)
				lists.game.save_num++;
			ui_list_refresh(node, 4, 4);
		}
	} else if (k & INPUT_B) {
		act(ACT_MENU_B, 0);
	} else if (k & INPUT_A) {
		act(ACT_MENU_A, line);
	}
	if (line != lists.menu_line) {
		lists.menu_line = (u8)line;
		ui_list_set_cursor(node, line);
	}
	return 1;
}

// --- Actions -----------------------------------------------------------------

static void folder_in(u32 index)
{
	const char *folder = names_get(sd_folders[index]);
	u32 old_len = strlen(currentpath);
	u32 sep = strcmp(currentpath, "/") != 0;
	// A path past MAX_path_len can't be opened; cutting it would open
	// another folder
	if (old_len + sep + strlen(folder) >= MAX_path_len) {
		error_dialog(9, AFTER_POP);
		return;
	}
	if (sep)
		currentpath[old_len] = '/';
	memcpy(currentpath + old_len + sep, folder, strlen(folder) + 1);  // fits: checked above
	if (f_chdir(currentpath) != FR_OK) {
		currentpath[old_len] = 0;
		f_chdir(currentpath);
		error_dialog(0, AFTER_POP);
		return;
	}
	if (lists.depth < FOLDER_DEPTH)
		folder_cursor[lists.depth] = (u16)index;
	if (lists.depth < 0xFF)
		lists.depth++;
	if (lists.depth < FOLDER_DEPTH)
		folder_cursor[lists.depth] = 0;
	show_sd(0, UI_TR_DEEPER, 1);
}

static void folder_up(void)
{
	char *p = strrchr(currentpath, '/');
	if (!p || strcmp(currentpath, "/") == 0)
		return;
	memcpy(currentpath_temp, currentpath, MAX_path_len);
	*p = 0;
	if (currentpath[0] == 0)
		memcpy(currentpath, "/", 2);
	if (f_chdir(currentpath) != FR_OK) {
		memcpy(currentpath, currentpath_temp, MAX_path_len);
		error_dialog(0, AFTER_POP);  // "Can't open the folder" (10 had no message)
		return;
	}
	if (lists.depth > 1)
		lists.depth--;
	show_sd(lists.depth < FOLDER_DEPTH ? folder_cursor[lists.depth] : 0, UI_TR_DEEPER, -1);
}

// Back from the game menu or a launch that didn't start: a Recently Played
// entry's folder goes back to the list's
static void recent_restore(void)
{
	if (!lists.from_recent)
		return;
	memcpy(currentpath, currentpath_temp, MAX_path_len);
	f_chdir(currentpath);
	lists.from_recent = 0;
}

// Leaves the engine, launches the picked game from menu line line, and comes
// back if it didn't start
static void launch(u32 line, u32 key_l)
{
	u32 at = lists.from_recent ? 0 : cursor();
	engine_leave();
	u8 res = Game_launch(&lists.game, line, key_l);
	recent_restore();  // the list's folder, whatever the launch did
	if (res == 2)
		show_nor(0, UI_TR_LEAVE, 0);
	else
		show_sd(at, UI_TR_LEAVE, 0);
}

// Opens current_filename in currentpath: the game menu, or straight to the
// launch for a GB, GBC or NES game
static void open_game(void)
{
	lists.game.name = current_filename;
	if (!Game_pick(&lists.game)) {  // can't boot
		if (lists.from_recent) {
			recent_restore();
			show_sd(0, UI_TR_DEEPER, -1);
		}
		return;
	}
	if (lists.game.is_emu) {
		launch(0, (ui_input()->held & INPUT_L) != 0);  // L held: hard reset, as in the menu
		return;
	}
	cheat_menu_forget();  // cheats never carry over between games
	menu_open(1, lists.game.havecht ? 6 : 5, lists.game.menu_max, 0);
}

static void recent_open(u32 row)
{
	const char *entry = p_recently_play[row];
	const char *nor = Recently_play_nor(entry);
	if (nor) {
		read_nor();
		u32 game = NOR_game_named(nor);
		if (game >= game_total_NOR) {
			tlog("launch %s: not on the game NOR", nor);
			error_dialog(1, AFTER_SD);
			return;
		}
		engine_leave();
		NOR_launch(game, 0);  // returns only if it failed
		show_sd(0, UI_TR_LEAVE, 0);
		return;
	}
	const char *slash = strrchr(entry, '/');  // Recently_play_load kept only paths
	memcpy(currentpath_temp, currentpath, MAX_path_len);
	memset(currentpath, 0, MAX_path_len);
	memcpy(currentpath, entry, (u32)(slash - entry));
	if (currentpath[0] == 0)
		currentpath[0] = '/';
	snprintf(current_filename, sizeof(current_filename), "%s", slash + 1);
	lists.from_recent = 1;
	open_game();
}

static void game_menu_a(u32 line, u32 key_l)
{
	if (line == 1 && (gl_reset_on | gl_rts_on | gl_sleep_on | gl_cheat_on) == 0)
		return;  // BOOT WITH ADDON with no addon on
	if (line == 4)
		return;  // the save type: LEFT and RIGHT
	if (line == 5) {  // the cheat screen, then the menu again on BOOT WITH ADDON
		u32 at = cursor();
		// It runs in this session, in place of the list: both don't fit
		go(UI_TR_DEEPER, 1);
		while (ui_depth())
			ui_pop();
		Open_cht_file(lists.game.name, lists.game.havecht);
		show((enum screen)lists.screen, at, UI_TR_DEEPER, -1);
		menu_open(1, lists.menu_rows, lists.menu_max, 1);
		return;
	}
	if (line >= 2 && strlen(lists.game.name) > sizeof(pNorFS[0].filename) - 1) {
		// The game NOR's list keeps 99 bytes of a name: one longer can't be
		// listed there, and a cut one would save under another name
		if (lists.from_recent) {
			recent_restore();
			error_dialog(8, AFTER_SD);
		} else {
			error_dialog(8, AFTER_POP);  // back to the list, the cursor where it was
		}
		return;
	}
	launch(line, key_l);
}

static void nor_menu_a(u32 line, u32 key_l)
{
	u8 res;
	switch (line) {
	case 0:  // boot
		engine_leave();
		NOR_launch(lists.nor_game, key_l);  // returns only if it failed
		show_nor(lists.nor_game, UI_TR_LEAVE, 0);
		break;
	case 1:  // delete: only the latest game
		if (lists.nor_game + 1 == game_total_NOR) {
			Delete_latest_NOR();
			show_nor(lists.nor_game, UI_TR_FADE, 0);
		} else {
			dialog(gl_lastest_game, 0, AFTER_NOR);
		}
		break;
	case 2:
		dialog(gl_formatnor_info, 1, AFTER_FORMAT);
		break;
	case 3:  // load the save into SRAM
		res = FRAM_save_op(1);
		if (res == 2)
			error_dialog(2, AFTER_NOR);
		else
			dialog_kind(NULL, res ? gl_file_noexist : gl_save_loaded, 0, AFTER_NOR,
				    res ? DIALOG_ERROR : DIALOG_INFO);
		break;
	default:  // back the SRAM up
		res = FRAM_save_op(0);
		if (res == 2) {
			error_dialog(2, AFTER_NOR);
		} else if (res == 0x10) {
			dialog_kind(NULL, gl_file_exist, 1, AFTER_BACKUP, DIALOG_INFO);  // a question, not an error
		} else {
			// Its progress dialog goes over the list alone: over the menu
			// too, it found no VRAM and never showed
			while (ui_depth() > 1)
				ui_pop();
			lists.menu = 0;
			res = FRAM_save_op(2);  // 2: no SAVER folder, 3: written short
			if (res)
				error_dialog(res, AFTER_NOR);
			else
				dialog(gl_save_saved, 0, AFTER_NOR);
		}
		break;
	}
}

static void dialog_answer(int yes)
{
	switch (lists.after) {
	// A question answered no changed nothing: the list stays as it is
	case AFTER_DELETE:
		if (!yes)
			break;
		if (f_chdir(currentpath) == FR_OK)  // cwd may be /SAVER after a launch
			f_unlink(current_filename);
		show_sd(cursor(), UI_TR_FADE, 0);
		break;
	case AFTER_FORMAT:
		if (!yes)
			break;
		FormatNor();  // its progress dialog shows over the list
		show_nor(0, UI_TR_FADE, 0);
		break;
	case AFTER_BACKUP:
		if (yes) {
			u8 res = FRAM_save_op(2);  // 2: no SAVER folder, 3: written short
			if (res)
				error_dialog(res, AFTER_NOR);
			else
				dialog(gl_save_saved, 0, AFTER_NOR);
		}
		break;
	case AFTER_SD:  // from Recently Played
		show_sd(0, UI_TR_DEEPER, -1);
		break;
	case AFTER_NOR:
		show_nor(lists.nor_game, UI_TR_FADE, 0);
		break;
	default:
		break;
	}
}

static void do_action(void)
{
	u8 a = lists.act;
	u32 arg = lists.arg, key_l = lists.key_l;
	lists.act = ACT_NONE;
	switch (a) {
	case ACT_FOLDER_IN:
		folder_in(arg);
		break;
	case ACT_FOLDER_UP:
		folder_up();
		break;
	case ACT_OPEN:
		snprintf(current_filename, sizeof(current_filename), "%s",
			 names_get(sd_files[arg - folder_total].name));
		lists.from_recent = 0;
		open_game();
		break;
	case ACT_TO_SD:  // L from the NOR list, B from Recently Played
		if (lists.screen == SCR_NOR)
			show_sd(0, UI_TR_TAB, -1);
		else
			show_sd(0, UI_TR_DEEPER, -1);
		break;
	case ACT_TO_NOR:
		show_nor(0, UI_TR_TAB, 1);
		break;
	case ACT_SETTINGS:
		// In this session, beside the NOR list
		go(UI_TR_TAB, 1);
		while (ui_depth())
			ui_pop();
		if (Settings_run(SETTINGS_PAGE1) == SETTINGS_TO_DIAGNOSE) {
			engine_leave();
			Diagnostics();  // restarts the kernel
		}
		tlog_flush();
		settings_changed();
		show_nor(0, UI_TR_TAB, -1);
		break;
	case ACT_RECENT:
		go(UI_TR_DEEPER, 1);
		lists.recent_count = Recently_play_load();
		show(SCR_RECENT, 0, UI_TR_DEEPER, 1);
		break;
	case ACT_RECENT_OPEN:
		recent_open(arg);
		break;
	case ACT_DELETE_ASK:
		snprintf(current_filename, sizeof(current_filename), "%s",
			 names_get(sd_files[arg - folder_total].name));
		// "Delete this file?" over the whole name
		dialog_kind(gl_delete_ask, current_filename, 1, AFTER_DELETE, DIALOG_INFO);
		break;
	case ACT_NOR_MENU:
		lists.nor_game = arg;
		menu_open(2, arg == 0 ? 5 : 3, arg == 0 ? 4 : 2, 0);  // the first game also loads and backs up its save
		break;
	case ACT_MENU_A:
		if (lists.menu == 1)
			game_menu_a(arg, key_l);
		else
			nor_menu_a(arg, key_l);
		break;
	case ACT_MENU_B:
		if (lists.menu == 1) {
			cheat_menu_forget();
			if (lists.from_recent) {
				recent_restore();
				show_sd(0, UI_TR_DEEPER, -1);
				break;
			}
			f_chdir(currentpath);  // back to the list's folder
		}
		lists.menu = 0;
		ui_close();
		break;
	default:
		break;
	}
}

// Writes the log to the card while the lists wait for a key: once it fills
// half its RAM ring, so a long stay in the menu loses no lines, and after
// IDLE_FLUSH_ALL_FRAMES with any line waiting, so a power-off loses none. A
// flush's SD write never stalls a scroll or a transition
static void idle_flush(void)
{
	u32 pending = tlog_pending(), idle = ui_idle_frames();
	if (!pending || lists.act || jobs_pending() || gfx_pending() || ui_transitioning())
		return;
	if ((pending >= LOG_RING_SIZE / 2 && idle >= IDLE_FLUSH_FRAMES) || idle >= IDLE_FLUSH_ALL_FRAMES)
		tlog_flush();
}

void lists_run(void)
{
	memset(&lists, 0, sizeof(lists));
	memset(folder_cursor, 0, sizeof(folder_cursor));
	lists.depth = 1;
	dialog_set_tick(clock_tick);
	show_sd(0, UI_TR_LEAVE, 0);  // in from the boot's black screen
	for (;;) {
		if (lists.measure_left) {
			frame_measure(lists.measure);
			lists.measure_left--;
		}
		ui_frame();
		if (lists.clock_wait)
			lists.clock_wait--;
		else {
			lists.clock_wait = CLOCK_POLL;
			clock_read();
		}
		if (lists.queued_a && !lists.act && ui_depth() == 1 && !ui_list_paging(list_node())) {
			struct ui_event e = {UI_EV_KEYS, UI_NONE, INPUT_A, 0};
			lists.queued_a = 0;
			list_event(ui_top(), &e);
		}
		if (lists.act)
			do_action();
		idle_flush();
	}
}
