// The game lists (games.h): Recent, Favourites and each console's games.
//
// The rows come from the library index (B3), which keeps each game's
// display name (its custom name or B4's cleaned name) beside its sort key;
// two games of a console that clean to the same name keep the tags that
// tell them apart (gamename_dedupe), worked out as their rows draw. Recent
// and the favourites (B5) are paths: when the page opens, one pass over the
// index finds each path's record (a hash of the file names), so their rows
// show what the console lists show; a path the index doesn't hold (a NOR
// game, a file in a hidden folder) shows its cleaned file name alone. The
// favourites are sorted A-Z by the names shown, into PSRAM (PSRAM_GAMES).
//
// What can't run inside a frame (the popup, a launch, a write to the card)
// is deferred to the menu's loop (nav_defer), which runs it after the frame.
#include <stdio.h>
#include <string.h>

#include "games.h"
#include "../actions.h"
#include "../boxart.h"
#include "../ez_define.h"
#include "../Ezcard_OP.h"
#include "../ezkernel.h"
#include "../gamelist.h"
#include "../gamename.h"
#include "../lang.h"
#include "../library.h"
#include "../namesdb.h"
#include "../sdlist.h"
#include "../showcht.h"
#include "../tlog.h"
#include "../userdata.h"
#include "../engine/engine.h"
#include "../engine/psram.h"
#include "../ui/art.h"
#include "../ui/dialog.h"
#include "../ui/letter.h"
#include "../ui/ui.h"
#include "rename.h"

// --- Look, px (spec.md's grid: title bar 0-18, content 20-143, hints at 147) ---
#define TITLE_H 24       // the title bar's rectangle: whole tiles under its line
#define CARD_Y 16        // the card's rectangle; its body starts 4 px in
#define CARD_H 128
#define HINTS_H 16
#define LIST_X 8         // the list, 8 px in from the screen's edge
#define LIST_Y 24
#define LIST_H 112       // 8 rows of 14 px: whole rows (B9's contract)
#define LIST_PAD 4       // the text's inset in its row
#define LIST_W 216       // compact: up to the scroll bar's column
#define DETAIL_W 64      // compact: the dot and the size or "yesterday"
#define ART_LIST_W 136   // list with art: the list, then the scroll bar, then the box
#define ART_DETAIL_W 12  // and only the dot at the right
#define BOX_X 152        // the placeholder box (80x80, ART_BOX_SIZE)
#define BOX_Y 24
#define FACTS_Y 104      // two facts under the box: names, then values right-aligned
#define FACT_KEYS_W 32
#define FACT_VALUES_W 48
#define FACTS_H 32       // two lines
// "No games here", in the list's middle: narrow, as plane 2's tiles are the
// popups' and confirmations' too (a tall popup and a confirmation fill them)
#define NOTE_W 88
#define NOTE_Y 72
#define NOTE_H 16
static const struct ui_card_config card = {4, 4, 4, 1, 0};
// Game info: a page of its own, its lines on the card: their names, then
// their values right-aligned
#define INFO_LINES_X 16
#define INFO_LINES_Y 32
#define INFO_KEYS_W 72
#define INFO_VALUES_W 136
#define INFO_LINES_H 88  // seven lines, in whole tiles

#define AGO_POLL 30     // frames between checks of Recent's "played" texts
#define AGO_MAX 16      // a "played" text's bytes, with its NUL
#define DEDUPE_RUN 8    // games that clean to one name told apart, at most
_Static_assert(DEDUPE_RUN *USERDATA_PATH_MAX <= READCACHE_DEDUPE_SIZE, "a run's paths fit their part of pReadCache");
#define HASH_BUCKETS 1024  // the resolver's (a power of 2)

enum {
	KIND_CONSOLE,
	KIND_RECENT,
	KIND_FAV,
};

// --- State -------------------------------------------------------------------

static struct {
	u8 item;     // enum nav_item
	u8 kind;
	u8 console;  // a console page's LIB_*
	u8 art;      // the list-with-art view
	u8 az;       // A-Z: LEFT/RIGHT jump by letter
	u8 first;    // the view's first node
	u32 count;
	u32 cursor;  // the list's cursor, as last seen
	u32 defer_generation;  // library_generation() when A or START asked for a row
	u32 row;     // the row the page was asked to open on (games_row before it shows)
	u8 unloaded;  // it opened while the index wasn't loaded (after a launch): a new
		      // index lists it again at row
	u32 lib_generation;
	u32 ago_wait;
	u16 recent_rec[USERDATA_RECENT_MAX];
	// The favourites' A-Z order kept in PSRAM (build_favs): valid while
	// fav_key is the index's generation, whether it was loaded and the
	// favourites' version, and the words still sum to fav_sum
	u8 fav_cached;
	u32 fav_key[3], fav_count, fav_sum;  // each Recent game's record, LIBRARY_NONE none
	char ago[USERDATA_RECENT_MAX][AGO_MAX];
	char position[16];  // the title's "101/176"
	char fact_keys[24], fact_values[32];
	struct ui_hint hints[4];
	u8 hint_count;
	u8 cap_note;       // the hint bar shows the library cap's note
	char cap_text[40];  // "Showing the first 4,096 games"
	// What fetch draws from: valid until the next fetch (a row draws at once)
	char name[GAMENAME_MAX + 1];
	char detail[AGO_MAX];
	char path[USERDATA_PATH_MAX] ALIGN(4);
	// The game the popup is about, copied: the lists move under it
	char pick[USERDATA_PATH_MAX] ALIGN(4);
	char title[GAMENAME_MAX + 1];
	char ask[GAMENAME_MAX + 16];
	struct ui_pop_item items[LANG_POP_ITEMS];
	u8 actions[LANG_POP_ITEMS];
	struct ui_popup popup;
	// Game info
	char info_keys[96], info_values[GAMENAME_MAX + 96], info_size[AGO_MAX];
	struct ui_hint info_hints[3];
	u32 info_type;  // the save type shown, ACTION_SAVE_TYPES: none (not a GBA game on the card)
	u32 info_boot;  // the boot mode shown (ACTION_BOOT_*), with a save type
	u8 info_field;  // the line LEFT/RIGHT change: 0 Save type, 1 Boot mode
	u32 info_console;  // the game's
	u8 info_done;
	u32 rename_size, rename_console;  // the game Rename is about (games.pick)
} games EWRAM_BSS;

// The favourites in A-Z order, while the Favourites page is up: one word
// each, FAV_* fields (PSRAM_GAMES_BASE, written by psram_write)
#define FAV_INDEX(e) ((e) & 0xFFFu)            // in userdata_fav_path's order
#define FAV_REC(e) (((e) >> 12) & 0x1FFFu)     // the library record, FAV_NO_REC none
#define FAV_LETTER(e) ((e) >> 25)              // letter_of its name
#define FAV_NO_REC 0x1FFFu
_Static_assert(USERDATA_FAV_MAX <= 0x1000 && LIBRARY_GAMES_MAX < FAV_NO_REC, "a favourite's word holds them");
_Static_assert(USERDATA_FAV_MAX * 4 <= PSRAM_GAMES_BYTES, "the favourites' order fits its PSRAM");

static u32 fav_entry(u32 i)
{
	return ((const u32 *)PSRAM_GAMES_BASE)[i];
}

// --- One game ------------------------------------------------------------------

struct game {
	u32 rec;           // its record in the index, LIBRARY_NONE if none
	const char *path;  // its path (Recent's or a favourite's, in PSRAM); NULL: the record's
	u32 console;       // LIB_*, LIB_CONSOLE_NONE if unknown
};

static u32 console_of(u32 rec, const char *path)
{
	if (rec != LIBRARY_NONE)
		return library_flags(rec) & LIB_F_CONSOLE;
	if (!path)
		return LIB_CONSOLE_NONE;
	return userdata_nor_name(path) ? LIB_GBA : library_console_of(path);
}

// Row i's game; 0 if there is none
static int game_at(u32 i, struct game *g)
{
	if (i >= games.count)
		return 0;
	if (games.kind == KIND_CONSOLE) {
		g->rec = library_game(games.console, i);
		g->path = NULL;
		g->console = games.console;
		return g->rec != LIBRARY_NONE;
	}
	if (games.kind == KIND_RECENT) {
		g->rec = games.recent_rec[i];
		g->path = userdata_recent_path(i);
	} else {
		u32 e = fav_entry(i);
		g->rec = FAV_REC(e) == FAV_NO_REC ? LIBRARY_NONE : FAV_REC(e);
		g->path = userdata_fav_path(FAV_INDEX(e));
	}
	g->console = console_of(g->rec, g->path);
	return *g->path != 0;
}

// Its path into out (USERDATA_PATH_MAX bytes); 0 if it has none
static int game_path(const struct game *g, char *out)
{
	const char *p = g->path;
	if (!p)
		return library_path(g->rec, out, USERDATA_PATH_MAX) != 0;
	u32 len = (u32)strnlen(p, USERDATA_PATH_MAX);
	if (!len || len >= USERDATA_PATH_MAX)
		return 0;
	memcpy(out, p, len + 1);
	return 1;
}

// 1 if record m of console has a cleaned name equal to display
static int same_name(u32 console, u32 m, const char *display)
{
	u32 rec = library_game(console, m);
	return rec != LIBRARY_NONE && !(library_flags(rec) & LIB_F_CUSTOM) &&
	       gamename_same(library_display(rec), display);
}

// The name a record shows: its display name, and when games of its console
// clean to the same name, the tags that tell it apart (into buf, GAMENAME_MAX
// + 1 bytes). They sort next to each other, so its neighbours are the run.
static const char *record_name(u32 rec, char *buf)
{
	const char *display = library_display(rec);
	if (library_flags(rec) & LIB_F_CUSTOM)
		return display;
	u32 console = library_flags(rec) & LIB_F_CONSOLE, n = library_position(rec), count = library_count(console);
	if (n == LIBRARY_NONE)
		return display;
	u32 lo = n, hi = n;
	while (lo > 0 && hi - lo + 1 < DEDUPE_RUN && same_name(console, lo - 1, display))
		lo--;
	while (hi + 1 < count && hi - lo + 1 < DEDUPE_RUN && same_name(console, hi + 1, display))
		hi++;
	if (lo == hi)
		return display;
	// Their paths, so names with the same tags in other folders add their
	// folder's (a file name where the path doesn't fit)
	const char *names[DEDUPE_RUN] = {0};
	char (*paths)[USERDATA_PATH_MAX] = (char (*)[USERDATA_PATH_MAX])(void *)(pReadCache + READCACHE_DEDUPE);
	for (u32 k = lo; k <= hi; k++) {
		u32 r = library_game(console, k);
		names[k - lo] = library_path(r, paths[k - lo], USERDATA_PATH_MAX) ? paths[k - lo] : library_name(r);
	}
	u32 len = (u32)strnlen(display, GAMENAME_MAX);
	memcpy(buf, display, len);
	buf[len] = 0;
	gamename_dedupe(buf, names, hi - lo + 1, n - lo);
	return buf;
}

// The name a game shows, into buf if it needs one
static const char *game_name(const struct game *g, char *buf)
{
	if (g->rec != LIBRARY_NONE)
		return record_name(g->rec, buf);
	const char *nor = userdata_nor_name(g->path);
	gamename_clean(buf, nor ? nor : g->path);
	return buf;
}

static int game_is_fav(const struct game *g)
{
	if (g->path)
		return userdata_fav_has(g->path);
	return library_path(g->rec, games.path, sizeof(games.path)) && userdata_fav_has(games.path);
}

// When the game at path was played, from Recent (0: not there, or the clock
// wasn't set)
static u32 played_time(const char *path)
{
	u32 n = userdata_recent_count();
	for (u32 i = 0; i < n && i < USERDATA_RECENT_MAX; i++)
		if (!strcmp(userdata_recent_path(i), path))
			return userdata_recent_time(i);
	return 0;
}

static const u8 console_icons[LIB_CONSOLES] = {ART_ICON8_GBA, ART_ICON8_GBC, ART_ICON8_GB, ART_ICON8_NES};

// --- The list's model ---------------------------------------------------------

static u32 model_count(void *ctx)
{
	(void)ctx;
	return games.count;
}

static int model_fetch(void *ctx, u32 i, struct ui_item *out)
{
	(void)ctx;
	struct game g;
	if (!game_at(i, &g))
		return UI_ITEM_PENDING;
	out->text = game_name(&g, games.name);
	if (games.kind != KIND_CONSOLE && g.console < LIB_CONSOLES) {
		out->mark = (u8)(console_icons[g.console] + 1);
		out->mark_ink = (u8)(UI_ROLE_INK_GBA + g.console);
	}
	if (games.kind != KIND_FAV && game_is_fav(&g)) {
		out->badge = ART_ICON8_STAR + 1;
		out->badge_ink = UI_ROLE_INK_FAV;
	}
	if (g.rec != LIBRARY_NONE && (library_flags(g.rec) & LIB_F_SAVE))
		out->dot = ART_ICON8_SAVE + 1;
	if (games.art)
		return UI_ITEM_READY;
	if (games.kind == KIND_RECENT) {
		out->detail = games.ago[i];
	} else if (g.rec != LIBRARY_NONE) {
		gamelist_size(games.detail, sizeof(games.detail), library_size(g.rec), gl_games->size_kb,
			      gl_games->size_mb);
		out->detail = games.detail;
	}
	return UI_ITEM_READY;
}

static const struct ui_list_model model = {model_count, model_fetch, 0, 0, 0};

// The letter row i files under (A-Z lists, in library_sort_key order)
static u32 key_letter_byte(u32 c)
{
	if (c >= 'a' && c <= 'z')
		return LETTER_A + (u32)(c - 'a');
	return c < 'a' ? LETTER_HASH_LOW : LETTER_HASH_HIGH;
}

static u32 key_letter(const char *key)
{
	return key_letter_byte((u8)key[0]);
}

static u32 row_letter(void *ctx, u32 i)
{
	(void)ctx;
	if (games.kind == KIND_FAV)
		return FAV_LETTER(fav_entry(i));
	return key_letter(library_key(library_game(games.console, i)));
}

// --- Layouts ----------------------------------------------------------------------

// Each view's nodes: the list last, so its rows draw within the frame's
// budget after everything else a first draw draws (ui_draw_room)
enum {
	N_TITLE,
	N_HINTS,
	N_CARD,
	N_BAR,
	N_NOTE,
	N_LETTER,
	N_LIST_COMPACT,          // the compact view's list, its last node
	N_BOX = N_LIST_COMPACT,  // the list with art's
	N_FACT_KEYS,
	N_FACT_VALUES,
	N_LIST_ART,
};

static const struct ui_title_config title_config = {"", "", ART_ICON16_GBA};
static const struct ui_hints_config no_hints = {0, 0};
static const struct ui_label_config note_label = {"", UI_FONT_BODY, UI_ALIGN_CENTER, 0, 0};
static const struct ui_label_config keys_label = {"", UI_FONT_BODY, UI_ALIGN_LEFT, 0, 0};
static const struct ui_label_config values_label = {"", UI_FONT_BODY, UI_ALIGN_RIGHT, UI_LABEL_SWAP, 0};
static const struct ui_image_config box_image = {art_box_tiles[ART_BOX_OTHER], art_box_palettes[ART_BOX_OTHER][0],
						 0};  // its own: box art goes there
static const struct ui_scrollbar_config bar_compact = {N_LIST_COMPACT};
static const struct ui_scrollbar_config bar_art = {N_LIST_ART};

#define LIST_FLAGS (UI_LIST_WRAP | UI_LIST_MARQUEE | UI_LIST_CONSOLES | UI_LIST_NO_SHOULDERS)
static const struct ui_list_config list_plain = {&model, UI_ROW_PITCH_LIST, LIST_FLAGS, DETAIL_W, LIST_PAD};
static const struct ui_list_config list_marks = {&model, UI_ROW_PITCH_LIST, LIST_FLAGS | UI_LIST_MARKS, DETAIL_W,
						 LIST_PAD};
static const struct ui_list_config art_plain = {&model, UI_ROW_PITCH_LIST, LIST_FLAGS, ART_DETAIL_W, LIST_PAD};
static const struct ui_list_config art_marks = {&model, UI_ROW_PITCH_LIST, LIST_FLAGS | UI_LIST_MARKS, ART_DETAIL_W,
						LIST_PAD};

#define NODES(list_w, bar) \
	[N_TITLE] = {UI_TITLE, 1, UI_NONE, 3, UI_HFILL, 0, 0, 0, 0, 0, 0, TITLE_H, &title_config}, \
	[N_HINTS] = {UI_HINTS, 2, UI_NONE, 3, UI_HFILL | UI_BOTTOM, 0, 0, 0, 0, 0, 0, HINTS_H, &no_hints}, \
	[N_CARD] = {UI_CARD, 3, UI_NONE, 2, UI_HFILL, 0, 0, 0, 0, CARD_Y, 0, CARD_H, &card}, \
	[N_BAR] = {UI_SCROLLBAR,      5,      UI_NONE, 2,      UI_LEFT, 0, 0, UI_ROLE_PANEL, \
		   LIST_X + (list_w), LIST_Y, 8,       LIST_H, &(bar)}, \
	[N_NOTE] = {UI_LABEL, \
		    6, \
		    UI_NONE, \
		    2, \
		    UI_LEFT, \
		    0, \
		    UI_ROLE_FAINT, \
		    UI_ROLE_PANEL, \
		    LIST_X + ((list_w) - NOTE_W) / 2, \
		    NOTE_Y, \
		    NOTE_W, \
		    NOTE_H, \
		    &note_label}, \
	[N_LETTER] = UI_LETTER_NODE(7)

#define LIST_NODE(index, list_config, list_w) \
	[index] = {UI_LIST, 4,      UI_NONE, 1,        UI_LEFT, UI_FOCUSABLE,  0, \
		   0,       LIST_X, LIST_Y,  (list_w), LIST_H,  &(list_config)}

#define ART_NODES \
	[N_BOX] = {UI_IMAGE, 8, UI_NONE, 2, UI_LEFT, 0, 0, 0, BOX_X, BOX_Y, ART_BOX_SIZE, ART_BOX_SIZE, &box_image}, \
	[N_FACT_KEYS] = {UI_LABEL,      9,     UI_NONE, 2,           UI_LEFT, 0,          UI_ROLE_FAINT, \
			 UI_ROLE_PANEL, BOX_X, FACTS_Y, FACT_KEYS_W, FACTS_H, &keys_label}, \
	[N_FACT_VALUES] = {UI_LABEL, \
			   10, \
			   UI_NONE, \
			   2, \
			   UI_LEFT, \
			   0, \
			   UI_ROLE_TEXT, \
			   UI_ROLE_PANEL, \
			   BOX_X + FACT_KEYS_W, \
			   FACTS_Y, \
			   FACT_VALUES_W, \
			   FACTS_H, \
			   &values_label}

static const struct ui_spec nodes_plain[] = {NODES(LIST_W, bar_compact), LIST_NODE(N_LIST_COMPACT, list_plain, LIST_W)};
static const struct ui_spec nodes_marks[] = {NODES(LIST_W, bar_compact), LIST_NODE(N_LIST_COMPACT, list_marks, LIST_W)};
static const struct ui_spec nodes_art_plain[] = {NODES(ART_LIST_W, bar_art), ART_NODES,
						 LIST_NODE(N_LIST_ART, art_plain, ART_LIST_W)};
static const struct ui_spec nodes_art_marks[] = {NODES(ART_LIST_W, bar_art), ART_NODES,
						 LIST_NODE(N_LIST_ART, art_marks, ART_LIST_W)};

static int page_event(struct ui_view *v, const struct ui_event *e);
#define VIEW(name, nodes, list) {(name), (nodes), (list) + 1, (list), UI_VIEW_LR_PAGES, page_event}
static const struct ui_view_spec views[2][2] = {
	{VIEW("games", nodes_plain, N_LIST_COMPACT), VIEW("games", nodes_marks, N_LIST_COMPACT)},
	{VIEW("games-art", nodes_art_plain, N_LIST_ART), VIEW("games-art", nodes_art_marks, N_LIST_ART)},
};

// 1 while the page's view is up (at the stack's bottom: popups and Game
// info go over it)
static int up(void)
{
	if (!ui_depth())
		return 0;
	const struct ui_view_spec *s = ui_view_at(0)->spec;
	return s >= &views[0][0] && s < &views[0][0] + 4;
}

static u8 node(u32 n)
{
	return (u8)(games.first + n);
}

// The list's node
static u8 list_node(void)
{
	return node(games.art ? N_LIST_ART : N_LIST_COMPACT);
}

// --- Building the lists ---------------------------------------------------------
//
// Recent's and the favourites' records, and the favourites' order, are
// worked out in pReadCache (READCACHE_GAMES) while the page opens.

#define KEY_BYTES 12  // a favourite's key's bytes kept for the merge (whole words)
#define KEY_WORDS (KEY_BYTES / 4)

struct build {
	u16 head[HASH_BUCKETS];
	u16 next[USERDATA_FAV_MAX];  // the resolver's chains, then (with tmp) the favourites' words
	u16 tmp[USERDATA_FAV_MAX];   // the merges' scratch
	u16 rec[USERDATA_FAV_MAX];   // each path's record
	u16 order[USERDATA_FAV_MAX];  // the paths found, in the index's order; then A-Z
	u16 hash[USERDATA_FAV_MAX];   // each path's file name's hash, past the bucket's bits
	u32 key[USERDATA_FAV_MAX][KEY_WORDS];  // each favourite's key's first bytes, big-endian
};
_Static_assert(sizeof(struct build) <= READCACHE_GAMES_SIZE, "the build fits its share of pReadCache");

static struct build *build_area(void)
{
	return (struct build *)(pReadCache + READCACHE_GAMES);
}

static const char *file_name(const char *path)
{
	const char *slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

// 1 if path is folder's file name (folder "/": the root's)
int IWRAM_CODE games_path_is(const char *folder, const char *name, const char *path);
int IWRAM_CODE games_path_is(const char *folder, const char *name, const char *path)
{
	if (folder[0] == '/' && !folder[1])
		folder++;
	while (*folder)
		if (*folder++ != *path++)
			return 0;
	if (*path++ != '/')
		return 0;
	while (*name)
		if (*name++ != *path++)
			return 0;
	return !*path;
}

#define HASH_CHECK(h) ((u16)((h) >> 16))  // a hash's bits kept to check a match

// Copies key's first KEY_BYTES into words, big-endian, zero-padded
void IWRAM_CODE games_key_words(const char *key, u32 *words);
void IWRAM_CODE games_key_words(const char *key, u32 *words)
{
	u32 w = 0;
	for (u32 i = 0; i < KEY_BYTES; i++) {
		u32 c = (u8)*key;
		key += c != 0;
		w = w << 8 | c;
		if ((i & 3) == 3)
			*words++ = w;
	}
}

// The paths' records from one console's A-Z list (count records of the
// index slot from order): each record's file name hashed and looked up in
// b's chains, a hash that matches comparing the whole path (path(i)); found
// ones get their key's first words and are appended to b->order from
// found, up to n. Returns the new found. The build's hot loop, so in IWRAM.
u32 IWRAM_CODE games_match(struct build *b, const u8 *slot, const u16 *order, u32 count, u32 found, u32 n,
			   const char *(*path)(u32));
u32 IWRAM_CODE games_match(struct build *b, const u8 *slot, const u16 *order, u32 count, u32 found, u32 n,
			   const char *(*path)(u32))
{
	const u32 *folders = (const u32 *)(slot + LIB_OFF_FOLDERS);
	for (u32 k = 0; k < count && found < n; k++) {
		u32 rec = order[k];
		const struct lib_rec *r = lib_rec_at(slot, rec);
		u32 off = r->name & LIB_NAME_OFFSET;
		const char *name = lib_str(slot, off);
		u32 h = userdata_name_hash(name);
		for (u32 i = b->head[h & (HASH_BUCKETS - 1)]; i != 0xFFFF; i = b->next[i])
			if (b->hash[i] == HASH_CHECK(h) && b->rec[i] == LIBRARY_NONE &&
			    games_path_is(lib_str(slot, folders[r->folder]), name, path(i))) {
				b->rec[i] = (u16)rec;
				b->order[found++] = (u16)i;
				games_key_words(lib_str(slot, off + (r->name >> 24) * 2), b->key[i]);
				break;
			}
	}
	return found;
}

// Finds the records of n paths (path(i)) in one pass over the index: their
// file names' hashes (hash(i)) chained, then every record's looked up (games_match). b->rec[i]
// gets each, LIBRARY_NONE none, b->key[i] its key's first words, and
// b->order the paths found in the index's order: console by console, each
// A-Z. Returns how many; ends[c] where console c's run ends in it.
static u32 resolve(struct build *b, u32 n, const char *(*path)(u32), u32 (*hash)(u32), u32 *ends)
{
	memset(b->head, 0xFF, sizeof(b->head));
	for (u32 i = 0; i < n; i++) {
		b->rec[i] = LIBRARY_NONE;
		if (*path(i) != '/')
			continue;  // a NOR game
		u32 h = hash(i);
		b->hash[i] = HASH_CHECK(h);
		b->next[i] = b->head[h & (HASH_BUCKETS - 1)];
		b->head[h & (HASH_BUCKETS - 1)] = (u16)i;
	}
	const u8 *slot = library_slot();
	u32 found = 0;
	for (u32 c = 0; c < LIB_CONSOLES; c++) {
		if (slot)
			found = games_match(b, slot, lib_order(slot) + lib_console_start(slot, c), library_count(c),
					    found, n, path);
		if (ends)
			ends[c] = found;
	}
	return found;
}

static const char *recent_path(u32 i)
{
	return userdata_recent_path(i);
}

static u32 recent_hash(u32 i)
{
	return userdata_name_hash(userdata_recent_path(i));
}

static const char *fav_path(u32 i)
{
	return userdata_fav_path(i);
}

// A favourite's key's first KEY_BYTES as words, big-endian, zero-padded:
// its record's key, or its cleaned name's (cut to fit, so it ends inside)
static void fav_key(u32 rec, const char *path, u32 *words)
{
	if (rec != LIBRARY_NONE) {
		games_key_words(library_key(rec), words);
		return;
	}
	const char *nor = userdata_nor_name(path);
	char key[KEY_BYTES];
	gamename_clean(games.name, nor ? nor : path);
	library_sort_key(key, KEY_BYTES, games.name);
	games_key_words(key, words);
}

// Favourites a and b (userdata's order) by their keys' first words (ka,
// kb), the rest of their keys, then as the library ties: file names, then
// paths
static int key_cmp(const u32 *ka, u32 ra, u32 a, const u32 *kb, u32 rb, u32 b)
{
	for (u32 w = 0; w < KEY_WORDS; w++)
		if (ka[w] != kb[w])
			return ka[w] < kb[w] ? -1 : 1;
	int c = 0;
	if ((ka[KEY_WORDS - 1] & 0xFF) && ra != LIBRARY_NONE && rb != LIBRARY_NONE)
		c = strcmp(library_key(ra) + KEY_BYTES, library_key(rb) + KEY_BYTES);  // both go on
	if (!c)
		c = strcmp(file_name(fav_path(a)), file_name(fav_path(b)));
	return c ? c : strcmp(fav_path(a), fav_path(b));
}

static int fav_cmp(void *ctx, u32 a, u32 b)
{
	const struct build *bd = ctx;
	return key_cmp(bd->key[a], bd->rec[a], a, bd->key[b], bd->rec[b], b);
}

// Merges the sorted runs order[0, mid) and order[mid, end) into one
static void merge(struct build *b, u32 mid, u32 end)
{
	u32 i = 0, j = mid, k = 0;
	while (i < mid && j < end)
		b->tmp[k++] = fav_cmp(b, b->order[j], b->order[i]) < 0 ? b->order[j++] : b->order[i++];
	while (i < mid)
		b->tmp[k++] = b->order[i++];
	memcpy(b->order, b->tmp, k * sizeof(u16));  // the rest of the second run is in place
}

static void build_recent(void)
{
	u32 n = userdata_recent_count();
	games.count = n < USERDATA_RECENT_MAX ? n : USERDATA_RECENT_MAX;
	struct build *b = build_area();
	resolve(b, games.count, recent_path, recent_hash, NULL);
	memcpy(games.recent_rec, b->rec, games.count * sizeof(u16));
}

// --- The favourites' order, kept ------------------------------------------------
// The words in PSRAM stay between visits: they are the list while the
// index, its being loaded and the favourites are as they were, and PSRAM
// holds what was written (a launch fills it with the game). The page's own
// favourite changes update them in place.

static u32 words_sum(const u32 *words, u32 n)
{
	u32 s = n;
	for (u32 i = 0; i < n; i++)
		s = (s << 1 | s >> 31) ^ words[i];
	return s;
}

static void cache_key(u32 *key)
{
	key[0] = library_generation();
	key[1] = (u32)library_ready();
	key[2] = userdata_fav_version();
}

// 1 if the words in PSRAM are the favourites' order
static int cache_ok(void)
{
	u32 key[3];
	cache_key(key);
	return games.fav_cached && !memcmp(key, games.fav_key, sizeof(key)) &&
	       words_sum((const u32 *)PSRAM_GAMES_BASE, games.fav_count) == games.fav_sum;
}

// Writes n words as the favourites' order; 0 if PSRAM refused them
static int cache_put(const u32 *words, u32 n)
{
	games.fav_cached = 0;
	psram_begin();
	if (n && !psram_write((void *)PSRAM_GAMES_BASE, words, n * 4))
		return 0;
	games.fav_count = n;
	games.fav_sum = words_sum(words, n);
	cache_key(games.fav_key);
	games.fav_cached = 1;
	return 1;
}

static u32 fav_word(u32 i, u32 rec, const u32 *key)
{
	return i | (rec == LIBRARY_NONE ? FAV_NO_REC : rec) << 12 | key_letter_byte(key[0] >> 24) << 25;
}

static void build_favs(void)
{
	if (cache_ok()) {
		games.count = games.fav_count;
		tlog_debug("games: %lu favourites kept", (unsigned long)games.count);
		return;
	}
	u32 start = torii_frames;
	u32 n = userdata_fav_count();
	if (n > USERDATA_FAV_MAX)
		n = USERDATA_FAV_MAX;
	struct build *b = build_area();
	u32 ends[LIB_CONSOLES];
	u32 found = resolve(b, n, fav_path, userdata_fav_hash, ends);
	// The rest, not in the index, after them
	u32 m = found;
	for (u32 i = 0; i < n; i++)
		if (b->rec[i] == LIBRARY_NONE) {
			fav_key(LIBRARY_NONE, fav_path(i), b->key[i]);
			b->order[m++] = (u16)i;
		}
	gamelist_sort(b->order + found, b->tmp, n - found, fav_cmp, b);
	// Each console's run is A-Z already: merged one by one, then the rest
	u32 done = ends[0];
	for (u32 c = 1; c < LIB_CONSOLES; c++) {
		if (ends[c] > done && done)
			merge(b, done, ends[c]);
		done = ends[c];
	}
	if (n > found && found)
		merge(b, found, n);
	// The words, over the chains and the scratch (done with)
	u32 *words = (u32 *)b->next;
	for (u32 j = 0; j < n; j++) {
		u32 i = b->order[j];
		words[j] = fav_word(i, b->rec[i], b->key[i]);
	}
	games.count = cache_put(words, n) ? n : 0;
	tlog("games: %lu favourites sorted in %lu frames", (unsigned long)n, (unsigned long)(torii_frames - start));
}

// After the page turned favourite path on or off (rec its record):
// userdata's version was ver, and changed if the favourites did. Its word
// goes in or out of the kept order in place, if that was kept; else the
// next visit sorts them again.
static void cache_changed(const char *path, u32 rec, int on, u32 ver)
{
	u32 now = userdata_fav_version();
	if (now == ver)
		return;  // nothing changed
	u32 key[3];
	cache_key(key);
	int ok = games.fav_cached && now == ver + 1 && key[0] == games.fav_key[0] && key[1] == games.fav_key[1] &&
		 games.fav_key[2] == ver && words_sum((const u32 *)PSRAM_GAMES_BASE, games.fav_count) == games.fav_sum;
	games.fav_cached = 0;
	int found;
	u32 idx = userdata_fav_find(path, &found), n = games.fav_count;
	if (!ok || found != on || (on && n >= USERDATA_FAV_MAX))
		return;
	u32 *words = (u32 *)build_area()->next;
	memcpy(words, (const u32 *)PSRAM_GAMES_BASE, n * 4);
	if (on) {
		// The later favourites one on, then its word where it sorts
		for (u32 j = 0; j < n; j++)
			if (FAV_INDEX(words[j]) >= idx)
				words[j]++;
		u32 kn[KEY_WORDS], kj[KEY_WORDS], lo = 0, hi = n;
		fav_key(rec, path, kn);
		while (lo < hi) {
			u32 mid = lo + (hi - lo) / 2, e = words[mid], i = FAV_INDEX(e);
			u32 rj = FAV_REC(e) == FAV_NO_REC ? LIBRARY_NONE : FAV_REC(e);
			fav_key(rj, fav_path(i), kj);
			if (key_cmp(kn, rec, idx, kj, rj, i) < 0)
				hi = mid;
			else
				lo = mid + 1;
		}
		memmove(words + lo + 1, words + lo, (n - lo) * 4);
		words[lo] = fav_word(idx, rec, kn);
		n++;
	} else {
		// Its word out (idx is where it was), the later ones one back
		u32 k = 0;
		for (u32 j = 0; j < n; j++) {
			u32 e = words[j], i = FAV_INDEX(e);
			if (i == idx)
				continue;
			words[k++] = i > idx ? e - 1 : e;
		}
		if (k != n - 1)
			return;
		n = k;
	}
	if (!cache_put(words, n) || !tlog_in_emulator())
		return;
	tlog_debug("games: favourite %s %s in place", path, on ? "in" : "out");
	// In the emulator, checked against a whole sort
	u32 sum = games.fav_sum, count = games.count;
	games.fav_cached = 0;
	build_favs();
	games.count = count;  // the page's, until it lists again
	TLOG_ASSERT(games.fav_count == n && games.fav_sum == sum);
}

// Recent's "played" texts as the clock reads now; 1 if one changed (and
// the rows showing it are refreshed)
static int ago_update(void)
{
	if (games.kind != KIND_RECENT)
		return 0;
	u32 now = userdata_now(), changed = 0;
	for (u32 i = 0; i < games.count; i++) {
		char text[AGO_MAX];
		u32 then = userdata_recent_time(i);
		userdata_ago(text, sizeof(text), then, then ? now : 0, &gl_ago);
		if (strcmp(text, games.ago[i]) != 0) {
			memcpy(games.ago[i], text, sizeof(text));
			changed = 1;
			tlog_debug("games: row %lu played %s", (unsigned long)i, text);  // tests/games.sh reads it
			if (up())
				ui_list_refresh(list_node(), i, i);  // cached rows too (B7's contract)
		}
	}
	return (int)changed;
}

// Lists the page's games again (a game deleted, a favourite dropped, a new
// index), the cursor at cursor (clamped)
static void build(void)
{
	userdata_load();  // both lists, if a launch dropped them
	readcache_poison();
	if (games.kind == KIND_CONSOLE)
		games.count = library_count(games.console);
	else if (games.kind == KIND_RECENT)
		build_recent();
	else
		build_favs();
	memset(games.ago, 0, sizeof(games.ago));
	ago_update();
	games.lib_generation = library_generation();
	games.ago_wait = AGO_POLL;
}

// --- What the page shows beside the list ----------------------------------------

static void show_cap_note(void);

static void show_position(void)
{
	if (games.count)
		snprintf(games.position, sizeof(games.position), "%lu/%lu", (unsigned long)games.cursor + 1,
			 (unsigned long)games.count);
	else
		games.position[0] = 0;
	ui_title_set_right(node(N_TITLE), games.position);
	show_cap_note();
}

// The placeholder box of each console (LIB_GBA...), for a game without art
_Static_assert(LIB_CONSOLES == 4, "give a new console its placeholder box");
static const u8 console_box[LIB_CONSOLES] = {
	[LIB_GBA] = ART_BOX_GBA,
	[LIB_GBC] = ART_BOX_GBC,
	[LIB_GB] = ART_BOX_GB,
	[LIB_NES] = ART_BOX_NES,
};

// List with art: the selected game's box art, or its console's placeholder
// box, and its size and when it was played
static void show_facts(void)
{
	if (!games.art)
		return;
	struct game g;
	games.fact_keys[0] = games.fact_values[0] = 0;
	if (game_at(games.cursor, &g)) {
		boxart_select(games.cursor, g.console < LIB_CONSOLES ? console_box[g.console] : ART_BOX_OTHER);
		char size[AGO_MAX] = "", ago[AGO_MAX] = "";
		if (g.rec != LIBRARY_NONE)
			gamelist_size(size, sizeof(size), library_size(g.rec), gl_games->size_kb, gl_games->size_mb);
		if (games.kind == KIND_RECENT)
			memcpy(ago, games.ago[games.cursor], sizeof(ago));
		else if (game_path(&g, games.path))
			userdata_ago_text(ago, sizeof(ago), played_time(games.path));
		snprintf(games.fact_keys, sizeof(games.fact_keys), "%s\n%s", gl_games->fact_size,
			 gl_games->fact_played);
		snprintf(games.fact_values, sizeof(games.fact_values), "%s\n%s", size, ago);
	} else {
		boxart_select(games.cursor, ART_BOX_OTHER);  // no game: the plain placeholder
	}
	ui_label_set(node(N_FACT_KEYS), games.fact_keys);
	ui_label_set(node(N_FACT_VALUES), games.fact_values);
}

static void show_hints(void)
{
	u32 n = 0;
	games.hints[n++] = (struct ui_hint){"A", gl_games->hint_play};
	games.hints[n++] = (struct ui_hint){"START", gl_games->hint_menu};
	if (games.az)
		games.hints[n++] = (struct ui_hint){"\xC2\xAB\xC2\xBB", gl_games->hint_letter};
	games.hints[n++] = (struct ui_hint){"B", gl_hint_back};
	games.hint_count = (u8)n;
	games.cap_note = 0;
	ui_hints_set(node(N_HINTS), games.hints, n);
}

// Past the library's cap (LIBRARY_GAMES_MAX games), a console's last row
// says only the first were indexed, where the hint bar's keys were (as the
// SD card page does at a big folder's end)
static void show_cap_note(void)
{
	u8 note = games.kind == KIND_CONSOLE && games.count && games.cursor + 1 >= games.count && library_truncated();
	if (note == games.cap_note)
		return;  // a change redraws the bar: not on every move
	games.cap_note = note;
	if (!note) {
		ui_hints_set(node(N_HINTS), games.hints, games.hint_count);
		return;
	}
	char n[12];
	sdlist_number(n, sizeof(n), LIBRARY_GAMES_MAX);
	snprintf(games.cap_text, sizeof(games.cap_text), "%s %s %s", gl_sd_first, n, gl_count_games);
	ui_hints_text(node(N_HINTS), games.cap_text);
	tlog_debug("games: %s", games.cap_text);  // tests/library.sh
}

// "No games here", once the index can say so (not while it loads again
// after a launch)
static const char *note_text(void)
{
	return games.count || games.unloaded ? "" : gl_games->empty;
}

// After the list changed under the page: it lists again from cursor
static void relist(u32 cursor)
{
	build();
	games.cursor = games.count ? (cursor < games.count ? cursor : games.count - 1) : 0;
	if (!up())
		return;
	ui_list_reset(list_node(), games.cursor);
	ui_letter_hide(node(N_LETTER));
	ui_label_set(node(N_NOTE), note_text());
	show_position();
	show_facts();
}

// --- Entering -----------------------------------------------------------------------

// Box art's view of a row: its path and size
static int art_row(u32 i, char *path, u32 *size)
{
	struct game g;
	if (!game_at(i, &g) || !game_path(&g, path))
		return 0;
	*size = g.rec != LIBRARY_NONE ? library_size(g.rec) : 0;
	return 1;
}

static struct ui_view *games_enter(u32 item, u32 row)
{
	games.item = (u8)item;
	games.console = registry_items[item].console;
	games.kind = item == NAV_RECENT ? KIND_RECENT : item == NAV_FAV ? KIND_FAV : KIND_CONSOLE;
	games.az = games.kind != KIND_RECENT;
	games.art = Read_SET_info(assress_game_view) == 1;
	library_load_now();  // after a launch that failed: the page lists the index, not nothing
	build();
	games.unloaded = !library_ready();
	games.cursor = games.count ? (row < games.count ? row : games.count - 1) : 0;
	games.row = games.unloaded ? row : games.cursor;
	struct ui_view *v = ui_push(&views[games.art][games.kind != KIND_CONSOLE], &games);
	if (!v)
		return 0;
	games.first = v->first;
	ui_title_set(node(N_TITLE), nav_item_name(item));
	ui_title_icon_set(node(N_TITLE), registry_items[item].icon16);
	ui_label_set(node(N_NOTE), note_text());
	show_hints();
	ui_list_reset(list_node(), games.cursor);
	show_position();
	if (games.art)
		boxart_open(node(N_BOX), art_row);
	show_facts();
	return v;
}

static u32 games_row(void)
{
	return up() ? ui_list_cursor(list_node()) : games.row;
}

// Each pass of the menu's loop: a new index lists again; Recent's "played"
// texts follow the clock
static void games_poll(void)
{
	if (!up())
		return;
	if (games.art)
		boxart_poll();
	// At once: Recent's and the favourites' records are the old index's
	if (games.lib_generation != library_generation()) {
		tlog_debug("games: a new index");
		// Opened before the index was back: at the row it was asked for
		u32 row = games.unloaded ? games.row : ui_list_cursor(list_node());
		games.unloaded = !library_ready();
		relist(row);
		return;
	}
	if (games.ago_wait > 1) {
		games.ago_wait--;
		return;
	}
	games.ago_wait = AGO_POLL;
	if (ago_update() && games.art)
		show_facts();
}

const struct nav_page games_page = {games_enter, games_row, 0, games_poll, 0};

// --- Game info -----------------------------------------------------------------------
// A page in the list's place: the game's name and console in the title bar,
// and on the card its console, size, folder, when it was played, its save
// type and boot mode (a GBA game on the card: UP/DOWN pick one, LEFT/RIGHT
// change it, written to its .mde file as the page closes) and how to start
// it with a hard reset.

static void reshow(u32 row);

enum {
	I_TITLE,
	I_HINTS,
	I_CARD,
	I_KEYS,
	I_VALUES,
	I_NODES
};

static const struct ui_spec info_nodes[I_NODES] = {
	[I_TITLE] = {UI_TITLE, 1, UI_NONE, 3, UI_HFILL, 0, 0, 0, 0, 0, 0, TITLE_H, &title_config},
	[I_HINTS] = {UI_HINTS, 2, UI_NONE, 3, UI_HFILL | UI_BOTTOM, 0, 0, 0, 0, 0, 0, HINTS_H, &no_hints},
	[I_CARD] = {UI_CARD, 3, UI_NONE, 2, UI_HFILL, 0, 0, 0, 0, CARD_Y, 0, CARD_H, &card},
	[I_KEYS] = {UI_LABEL, 4, UI_NONE, 2, UI_LEFT, 0, UI_ROLE_FAINT, UI_ROLE_PANEL, INFO_LINES_X, INFO_LINES_Y,
		    INFO_KEYS_W, INFO_LINES_H, &keys_label},
	[I_VALUES] = {UI_LABEL, 5, UI_NONE, 2, UI_LEFT, 0, UI_ROLE_TEXT, UI_ROLE_PANEL, INFO_LINES_X + INFO_KEYS_W,
		      INFO_LINES_Y, INFO_VALUES_W, INFO_LINES_H, &values_label},
};

static int info_event(struct ui_view *v, const struct ui_event *e);
static const struct ui_view_spec info_view = {"game-info", info_nodes, I_NODES, UI_NONE, 0, info_event};

#define INFO_PICKED "\xC2\xAB %s \xC2\xBB\n"  // the line LEFT/RIGHT change
#define INFO_OTHER "%s\n"
_Static_assert(sizeof(((struct lang_games *)0)->boot_modes) / sizeof(char *) == ACTION_BOOTS, "a name per boot mode");

// The values' lines, the picked one's between « »
static void info_values(struct ui_view *v)
{
	const struct lang_games *s = gl_games;
	char ago[AGO_MAX] = "", type[48] = "";
	userdata_ago_text(ago, sizeof(ago), played_time(games.pick));
	if (games.info_type < ACTION_SAVE_TYPES) {
		u32 o = (u32)snprintf(type, sizeof(type), games.info_field ? INFO_OTHER : INFO_PICKED,
				      s->save_types[games.info_type]);
		snprintf(type + o, sizeof(type) - o, games.info_field ? INFO_PICKED : INFO_OTHER,
			 s->boot_modes[games.info_boot]);
	}
	const char *nor = userdata_nor_name(games.pick), *slash = strrchr(games.pick, '/');
	int folder_len = nor ? (int)strlen(s->nor) : slash == games.pick ? 1 : (int)(slash - games.pick);
	snprintf(games.info_values, sizeof(games.info_values), "%s\n%s\n%.*s\n%s\n%s%s",
		 games.info_console < LIB_CONSOLES ? s->consoles[games.info_console] : "", games.info_size, folder_len,
		 nor ? s->nor : games.pick, ago, type, s->hard_reset);
	ui_label_set((u8)(v->first + I_VALUES), games.info_values);
}

static int info_event(struct ui_view *v, const struct ui_event *e)
{
	if (e->type != UI_EV_KEYS)
		return 0;
	if (e->keys & ui_input()->pressed & (INPUT_A | INPUT_B)) {
		games.info_done = 1;
	} else if (games.info_type >= ACTION_SAVE_TYPES) {
		return 1;  // nothing to change
	} else if (e->keys & ui_input()->pressed & (INPUT_UP | INPUT_DOWN)) {
		games.info_field ^= 1;  // two lines: either key goes to the other
		info_values(v);
	} else if (e->keys & (INPUT_LEFT | INPUT_RIGHT)) {
		int left = (e->keys & INPUT_LEFT) != 0;
		u32 *value = games.info_field ? &games.info_boot : &games.info_type,
		    n = games.info_field ? ACTION_BOOTS : ACTION_SAVE_TYPES;
		*value = left ? (*value + n - 1) % n : (*value + 1) % n;
		tlog_debug("games: info %s %lu", games.info_field ? "boot mode" : "save type", (unsigned long)*value);
		info_values(v);
	}
	return 1;
}

// Shows Game info for the game the popup was about (g) until A or B, in the
// list's place, then writes a changed save type or boot mode and brings the
// list back
static void game_info(const struct game *g)
{
	u32 row = ui_list_cursor(list_node());
	struct action_facts facts;
	action_game_facts(games.pick, &facts);
	games.info_type = facts.save_type;
	games.info_boot = facts.boot_mode;
	games.info_field = 0;
	games.info_console = g->console;
	// Now: the index may change while the page shows
	games.info_size[0] = 0;
	if (g->rec != LIBRARY_NONE)
		gamelist_size(games.info_size, sizeof(games.info_size), library_size(g->rec), gl_games->size_kb,
			      gl_games->size_mb);
	games.info_done = 0;
	ui_leave(UI_TR_DEEPER, 1);
	while (ui_depth())
		ui_pop();
	struct ui_view *v = ui_push(&info_view, &games);
	if (v) {
		const struct lang_games *s = gl_games;
		ui_title_set((u8)(v->first + I_TITLE), games.title);
		ui_title_icon_set((u8)(v->first + I_TITLE), registry_items[games.item].icon16);
		u32 o = 0;
		for (u32 k = 0; k < LANG_INFO_KEYS && o < sizeof(games.info_keys); k++)
			if ((k != LANG_INFO_SAVE_TYPE && k != LANG_INFO_BOOT_MODE) ||
			    games.info_type < ACTION_SAVE_TYPES)
				o += (u32)snprintf(games.info_keys + o, sizeof(games.info_keys) - o, "%s%s",
						   o ? "\n" : "", s->info_keys[k]);
		ui_label_set((u8)(v->first + I_KEYS), games.info_keys);
		info_values(v);
		u32 n = 0;
		if (games.info_type < ACTION_SAVE_TYPES) {
			games.info_hints[n++] = (struct ui_hint){"^v", gl_hint_move};
			games.info_hints[n++] = (struct ui_hint){"\xC2\xAB\xC2\xBB", gl_hint_change};
		}
		games.info_hints[n++] = (struct ui_hint){"B", gl_hint_back};
		ui_hints_set((u8)(v->first + I_HINTS), games.info_hints, n);
		while (!games.info_done && ui_depth())
			ui_frame();
		ui_leave(UI_TR_DEEPER, -1);
		while (ui_depth())
			ui_pop();
	} else {
		tlog_error("games: %s", ui_error());
	}
	// Each written on its own: one failing keeps the other's change
	int type_ok = games.info_type == facts.save_type || action_set_save_type(games.pick, games.info_type);
	int boot_ok = games.info_boot == facts.boot_mode || action_set_boot_mode(games.pick, games.info_boot);
	if (!type_ok || !boot_ok)
		dialog_message(NULL, type_ok ? gl_games->boot_mode_failed : gl_games->save_type_failed, DIALOG_ERROR);
	reshow(row);
}

// --- The popup -----------------------------------------------------------------------

// The page again in the menu's session, after something took its place and
// no game started; at row
static void reshow(u32 row)
{
	if (!ui_active() || ui_depth() || games_enter(games.item, row))
		return;
	// No room for it: the session ends, and nav brings the place back from
	// black in a new one (as after a launch)
	tlog_error("games: %s", ui_error());
	ui_shutdown();
	dialog_blank();
}

// Starts the picked game: the menu's session ends, and nav brings the page
// back if no game started (and the page here, if the session didn't end).
// how: ACTION_CLEAN, ACTION_DEFAULT (the game's boot mode, else Settings'),
// or ACTION_ADDON with the cheats picked on the cheat screen (keep) or none
static void launch(u32 how, int keep_cheats, u32 hard)
{
	if (!keep_cheats)
		cheat_menu_forget();
	tlog("games: start %s (how %lu%s)", games.pick, (unsigned long)how, hard ? ", hard reset" : "");
	action_launch(games.pick, how, ACTION_SAVE_KEEP, hard);
}

extern TCHAR currentpath[];

// Boot with addon: the cheat screen first when Cheats is on and the game
// has a cheat file; else (or once it has no cheat in it) the addons alone
static void boot_with_addon(u32 hard)
{
	struct action_facts facts;
	if (gl_cheat_on && action_game_facts(games.pick, &facts) && facts.cheats) {
		u32 row = ui_list_cursor(list_node());
		static TCHAR file[GAME_NAME_MAX + 1] EWRAM_BSS;
		static TCHAR kept[MAX_path_len] EWRAM_BSS;
		snprintf(file, sizeof(file), "%s", file_name(games.pick));
		// It takes the page's place in the menu's session (both don't fit)
		ui_leave(UI_TR_DEEPER, 1);
		while (ui_depth())
			ui_pop();
		// A cheat file found by game code moves currentpath to its folder
		memcpy(kept, currentpath, sizeof(kept));
		u32 r = Open_cht_file(file, facts.cheats, games.title, registry_items[games.item].icon16);
		memcpy(currentpath, kept, sizeof(kept));
		f_chdir(currentpath[0] ? currentpath : "/");
		if (r == CHEATS_BACK) {
			reshow(row);
			return;
		}
		launch(ACTION_ADDON, r == CHEATS_PLAY, hard);  // no cheat in the file: the addons alone
		reshow(row);
		return;
	}
	launch(ACTION_ADDON, 0, hard);
}

static int addons_off(void)
{
	return (gl_reset_on | gl_rts_on | gl_sleep_on | gl_cheat_on) == 0;
}

static void popup_add(u32 action, const char *value, u8 flags)
{
	u32 n = games.popup.count;
	games.items[n] = (struct ui_pop_item){gl_games->pop[action], value, flags};
	games.actions[n] = (u8)action;
	games.popup.count = (u8)(n + 1);
}

// 1 if row i still is the game the key was pressed on: a console's rows are
// the index's, which may have been swapped since (Recent's and the
// favourites' are paths)
static int row_still(u32 i)
{
	if (games.kind != KIND_CONSOLE || games.defer_generation == library_generation())
		return 1;
	tlog("games: a new index came before row %lu opened; nothing started", (unsigned long)i);
	relist(i);
	return 0;
}

// --- Rename ----------------------------------------------------------------------------
// A game's display name, kept in /torii/names.txt (its file stays): the
// keyboard starts from the name it shows, and OK writes the new one; its
// own name again (what it shows without one) takes its line out.

static const char *rename_submit(void *ctx, const char *name)
{
	(void)ctx;
	const char *crc = names_lookup(games.pick, games.rename_size, games.rename_console);
	if (crc)
		gamename_clean_title(games.name, crc);
	else
		gamename_clean(games.name, games.pick);
	int own = strcmp(name, games.name) == 0;
	if (!gamename_store_set(games.pick, own ? NULL : name))
		return gamename_store_full() ? gl_rename->names_full : gl_rename->name_failed;
	library_named(games.pick, own ? NULL : name);  // its row moves to its new place at once
	return NULL;
}

// Where the renamed game (games.pick) is on the page now; row if not found
static u32 pick_row(u32 row)
{
	if (games.kind == KIND_CONSOLE) {
		u32 at = library_position(library_find(games.pick));
		return at != LIBRARY_NONE ? at : row;
	}
	for (u32 j = 0; games.kind == KIND_FAV && j < games.count; j++)
		if (!strcmp(userdata_fav_path(FAV_INDEX(fav_entry(j))), games.pick))
			return j;
	return row;
}

static void rename_game(u32 row)
{
	// Its record now: a new index may have come while the popup was up
	u32 rec = library_find(games.pick);
	if (rec == LIBRARY_NONE)
		return;
	games.rename_size = library_size(rec);
	games.rename_console = library_flags(rec) & LIB_F_CONSOLE;
	snprintf(games.ask, sizeof(games.ask), "%s", library_display(rec));
	const struct rename_ask ask = {
		registry_items[games.item].icon16, games.ask, NULL, RENAME_GAME_MAX, 0, rename_submit, 0};
	int renamed = rename_run(&ask);
	reshow(row);
	if (renamed && up()) {
		games.cursor = pick_row(row);
		ui_list_set_cursor(list_node(), games.cursor);
		show_position();
		show_facts();
	}
}

// The game popup for row i, run after the frame
static void popup_run(u32 i)
{
	struct game g;
	if (!up() || !row_still(i) || !game_at(i, &g) || !game_path(&g, games.pick))
		return;
	u8 list = list_node(), hints = node(N_HINTS);
	const char *name = game_name(&g, games.name);
	snprintf(games.title, sizeof(games.title), "%s", name);
	int nor = userdata_nor_name(games.pick) != 0, fav = userdata_fav_has(games.pick), fav_was = fav;
	int gba = !nor && g.console == LIB_GBA;
	games.popup = (struct ui_popup){games.title, (u8)(g.console < LIB_CONSOLES ? console_icons[g.console] + 1 : 0),
					games.items, 0, 0};
	popup_add(LANG_POP_PLAY, 0, 0);
	popup_add(LANG_POP_FAV, fav ? gl_games->on : gl_games->off, 0);
	u32 fav_item = 1u;
	if (gba) {
		popup_add(LANG_POP_CLEAN, 0, 0);
		popup_add(LANG_POP_ADDON, 0, addons_off() ? UI_POP_DIM : 0);
	}
	if (!nor && g.rec != LIBRARY_NONE)
		popup_add(LANG_POP_RENAME, 0, 0);  // a name in names.txt, for games the lists name
	popup_add(LANG_POP_INFO, 0, 0);
	if (!nor)
		popup_add(LANG_POP_DELETE, 0, UI_POP_DANGER);
	if (!ui_popup_open(list, hints, &games.popup))
		return;
	tlog_debug("screen ready: game-popup %s", games.title);
	for (;;) {
		s32 pick = ui_popup_choose();
		if (pick < 0)
			break;
		u32 action = games.actions[pick], hard = (ui_input()->held & INPUT_L) != 0;
		if (action == LANG_POP_FAV) {
			u32 ver = userdata_fav_version();
			if (userdata_fav_set(games.pick, !fav))
				fav = !fav;
			cache_changed(games.pick, g.rec, fav, ver);
			games.items[fav_item].value = fav ? gl_games->on : gl_games->off;
			ui_popup_changed(fav_item);
			if (games.kind != KIND_FAV)
				ui_list_refresh(list, i, i);  // its star
			continue;
		}
		if (action == LANG_POP_ADDON && addons_off())
			continue;  // greyed out: nothing to boot with
		if (action == LANG_POP_DELETE) {
			snprintf(games.ask, sizeof(games.ask), EN_GAME_DELETE, games.title);
			struct ui_confirm ask = {games.ask, gl_games->delete_text, gl_games->delete_button, 1};
			if (!ui_confirm(hints, &ask))
				continue;  // Cancel: back to the popup
			ui_popup_close();
			if (action_delete_file(games.pick))
				relist(i);
			else
				dialog_message(NULL, gl_sd_delete_failed, DIALOG_ERROR);
			return;
		}
		ui_popup_close();
		// A favourite dropped leaves the Favourites page before a launch
		// that may come back (Game info and Rename list the page again)
		if (fav != fav_was && games.kind == KIND_FAV && action != LANG_POP_INFO && action != LANG_POP_RENAME)
			relist(i);
		switch (action) {
		case LANG_POP_PLAY:
			launch(ACTION_DEFAULT, 0, hard);
			return;
		case LANG_POP_CLEAN:
			launch(ACTION_CLEAN, 0, hard);
			return;
		case LANG_POP_ADDON:
			boot_with_addon(hard);
			return;
		case LANG_POP_INFO:
			game_info(&g);
			return;
		case LANG_POP_RENAME:
			rename_game(i);
			return;
		default:
			return;
		}
	}
	ui_popup_close();
	if (fav != fav_was && games.kind == KIND_FAV)
		relist(i);  // a favourite dropped leaves the page
}

// A on a row (L held: a hard reset), run after the frame
static void play_run(u32 arg)
{
	u32 i = arg & 0xFFFF, hard = arg >> 16;
	struct game g;
	if (!up() || !row_still(i) || !game_at(i, &g) || !game_path(&g, games.pick))
		return;
	launch(ACTION_DEFAULT, 0, hard);
}

// --- Keys -------------------------------------------------------------------------------

static void letter_jump_key(u16 keys)
{
	u8 list = list_node();
	u32 to = letter_jump(games.count, ui_list_cursor(list), (keys & INPUT_LEFT) ? -1 : 1, row_letter, 0), prev,
	    next;
	ui_list_set_cursor(list, to);
	games.cursor = to;
	letter_around(games.count, to, row_letter, 0, &prev, &next);
	ui_letter_show(node(N_LETTER), row_letter(0, to), prev, next);
	tlog_debug("games: letter %s, row %lu", letter_text(row_letter(0, to)), (unsigned long)to);  // tests/shots walk
	show_position();
	show_facts();
}

static int page_event(struct ui_view *v, const struct ui_event *e)
{
	(void)v;
	const InputState *in = ui_input();
	switch (e->type) {
	case UI_EV_CURSOR:
		games.cursor = e->index;
		show_position();
		show_facts();
		frame_measure("games-move");
		return 1;
	case UI_EV_ACTIVATE: {
		// L held: a hard reset (L doesn't page here, so the row stays)
		u32 i = e->index, hard = (in->held & INPUT_L) != 0;
		games.defer_generation = library_generation();
		if (i < games.count)
			nav_defer(play_run, i | hard << 16);
		return 1;
	}
	case UI_EV_KEYS:
		if (e->keys & in->pressed & INPUT_B) {
			nav_back();
			return 1;
		}
		if (e->keys & in->pressed & INPUT_START) {
			if (games.count && !ui_list_paging(list_node())) {
				games.defer_generation = library_generation();
				nav_defer(popup_run, ui_list_cursor(list_node()));
			}
			return 1;
		}
		if ((e->keys & (INPUT_LEFT | INPUT_RIGHT)) && games.az && games.count) {
			letter_jump_key(e->keys);
			return 1;
		}
		return 1;  // SELECT and the rest do nothing (T8)
	default:
		return 0;
	}
}
