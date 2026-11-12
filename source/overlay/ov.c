// The in-game menu's overlay (docs/design/v0.8/ingame-menu.md): Thumb code
// that runs from the EWRAM window the stub borrows, in IRQ mode with IME 0,
// while the game is frozen. It draws with sprites only: a dark
// semi-transparent cover over the game's frozen screen, a rounded panel, a
// selection pill and rows of text in 32x16 sprites, all in OBJ tiles from
// 512, which every mode has. The stub has backed up OAM and the OBJ palette
// and puts the display registers back; the overlay backs up the tiles it
// draws over (only it knows how many), records how many in its header, and
// the stub puts them back.
//
// The text comes drawn: the kernel renders each line in the menu's body
// font into the session block (struct ingame_line), and the overlay only
// spreads its 1bpp rows into tiles. The panel shows first; its text a frame
// later, once its tiles are backed up and drawn.
//
// Two pages: the menu, and the cheat list (P2) of the picks, whose rows
// scroll under a title and over a footer that says why a row can't change,
// else how the cheat is activated ("Hold L+SELECT"), or that the toggles are
// kept as picks on Back to Torii. SELECT on a cheat that is off asks for it
// to run once as the game resumes (INGAME_CHEAT_ONCE). The highlighted row's name scrolls
// sideways when it is cut (a marquee, drawn from its whole line the kernel
// wrote at INGAME_SRAM_WIDE).
//
// The cheat search (v0.9, docs/design/v0.9/cheat-search.md): a page that
// remembers the game's RAM in the save-state buffer, narrows the candidates
// down a filter at a time, lists the last few and keeps one as a found
// cheat for Back to Torii, on now through the hook's live block. Its numbers
// are drawn from the kernel's glyphs after a line's text.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../cheatsearch.h"
#include "../ingame_abi.h"
#include "../shotpack.h"

#define REG16(a) (*(volatile uint16_t *)(a))
#define REG_DISPCNT REG16(0x04000000)
#define DISPCNT_BLANK 0x0080  // forced blank: the LCD shows white
#define REG_VCOUNT REG16(0x04000006)
#define REG_BLDCNT REG16(0x04000050)
#define REG_BLDALPHA REG16(0x04000052)
#define REG_KEYINPUT REG16(0x04000130)
#define REG_WAITCNT REG16(0x04000204)
#define OAM ((volatile uint16_t *)0x07000000)
#define OBJ_PAL ((volatile uint16_t *)0x05000200)
#define OBJ_TILES ((volatile uint32_t *)INGAME_OBJ_VRAM)
#define TOGGLES ((volatile uint32_t *)INGAME_CHEAT_TOGGLES)

// mGBA's debug output (source/tlog.c); the real cart ignores these addresses
#define REG_DEBUG_ENABLE REG16(0x04FFF780)
#define REG_DEBUG_FLAGS REG16(0x04FFF700)
#define REG_DEBUG_STRING ((volatile char *)0x04FFF600)

#define KEY_A 0x001
#define KEY_B 0x002
#define KEY_SELECT 0x004
#define KEY_RIGHT 0x010
#define KEY_LEFT 0x020
#define KEY_UP 0x040
#define KEY_DOWN 0x080
#define KEYS_ALL 0x3FF
#define MENU_KEY_DEFAULT 0x308  // L+R+START
#define REPEAT_DELAY 18  // frames a direction is held before it repeats
#define REPEAT_EVERY 5
// The marquee, in frames and pixels: the kernel's lists' (ui_theme.c's
// marquee_delay, _pause and _gap, at its speed of a pixel a frame)
#define MQ_DELAY 45
#define MQ_PAUSE 60
#define MQ_GAP 8

// The last VBlank line the opening still shows the panel in (VBlank runs
// to line 227)
#define VBLANK_LATE 215

// DISPCNT: OBJ on, 1D tile mapping; forced blank and the windows off
#define DISPCNT_OBJ 0x1000
#define DISPCNT_1D 0x0040
#define DISPCNT_OFF_BITS 0xE080

// The menu reopened after a DEBUG snapshot (m.flags): its footer says so
#define NOTICE_DEBUG (1u << 8)

extern struct ingame_image ov_header;
extern uint32_t __ov_bss_start[], __ov_bss_end[], __ov_image_end[];

// --- Layout -------------------------------------------------------------------

#define SCREEN_W 240
#define SCREEN_H 160
#define PANEL_PAD 8
#define ROW_H 16
#define TITLE_GAP 8  // the title and the gap after it fill the panel's top 32 px (column)
#define TEXT_TOP 2  // a line's 12 rows in a 16 px row
#define TEXT_INSET 14  // the text's left edge in the panel
#define PILL_INSET 4
#define PILL_FILLS 7  // the pill's 32x16 middle sprites, for the widest (the cheat list's)
#define CHECK_X 5  // the cheat list's check mark (the kernel's), from the panel's left edge
#define SLOT_SPRITES 5  // 32x16 sprites a row's line takes at most: 160 px
#define SLOT_TILES (SLOT_SPRITES * 8)
#define WIDE_SPRITES 6  // and the menu's title and footer: INGAME_TEXT_W
_Static_assert(WIDE_SPRITES * 32 >= INGAME_TEXT_W, "the wide lines' sprites");

// The lines the menu's opening draws in its first frame of text; the rest
// (and the footer) a frame later
#define FIRST_LINES 7

// The menu: wide enough for its footer's line
#define MENU_W 192
// The cheat list: 6 rows between its title and its footer
#define LIST_W 224
#define LIST_ROWS 6
// The scroll bar of a page with more rows than it shows: its sprite's left
// edge from the panel's right (the bar's 3 px from 2 px into it), the room
// the pill gives it, the thumb's shortest (its two ends, which can't
// overlap their rounded rows) and the 8x16 fills between its ends
#define BAR_RIGHT 12
#define BAR_ROOM 10
#define BAR_MIN 18
#define BAR_FILLS 4
#define BAR_TRACKS 3  // 8x32: a page's rows' height, less 2 px at each end
#define BAR_SPRITES (2 + BAR_FILLS + BAR_TRACKS)
_Static_assert(BAR_TRACKS * 32 >= LIST_ROWS * ROW_H - 4 && BAR_FILLS * 16 + 32 >= LIST_ROWS * ROW_H - 4,
	       "the scroll bar's sprites");
// The cover's strips beside the panel are 32 px wide; the panel's top 32 px
// hold no row
_Static_assert(PANEL_PAD + ROW_H + TITLE_GAP >= 32 && (SCREEN_W - MENU_W) / 2 <= 32 && (SCREEN_W - LIST_W) / 2 <= 32,
	       "the cover's strips");

// OBJ tiles from INGAME_OBJ_TILE: one block of solid tiles that every
// filled shape shares (a 64x32 sprite's 32), the panel's corner, the pill's
// end cap (8x16), the check mark, the scroll bar's track (8x32), thumb and
// thumb's rounded end (8x16 each), then the lines of text:
// as many as each needs on the menu, SLOT_TILES each on the cheat list,
// whose rows scroll
#define TILE_SOLID 0
#define TILE_CORNER 32
#define TILE_CAP 33
#define TILE_CHECK 35
#define TILE_TRACK 36
#define TILE_BAR 40
#define TILE_BAR_END 42
#define TILE_TEXT 44
#define SLOTS_MAX 11  // the menu's title, 9 rows and footer; the list's 8
_Static_assert(TILE_TEXT + 2 * WIDE_SPRITES * 8 + (SLOTS_MAX - 2) * SLOT_TILES <= INGAME_OBJ_MAX / 32,
	       "the lines' tiles");

// Palette banks, colour 1 of each (every tile draws with colour 1)
#define BANK_PANEL 0
#define BANK_DIM 1
#define BANK_PILL 2
#define BANK_TEXT 3
#define BANK_TITLE 4
#define BANK_FAINT 5
#define BANKS 6

// The cover: black, the game's frozen screen kept at DIM_EVB/16 under it
#define DIM_EVB 6

// OAM attributes
#define A0_SEMI (1u << 10)
#define A0_HIDE (1u << 9)
#define SHAPE_SQUARE 0
#define SHAPE_WIDE 1
#define SHAPE_TALL 2
#define A1_HFLIP (1u << 12)
#define A1_VFLIP (1u << 13)

// Most of the search, and the lists' scrolling, are an extension the
// overlay loads itself as a search page or a list that scrolls opens
// (ext_load), after the window, so the stub's copies at every opening stay
// as they were: each byte in the window costs the menu's opening time (the
// search's slowed it by two frames, the scroll bar's by one). EXT code and
// data are linked there (ov.ld's .ext); the rest may call it only while it
// is loaded (m.ext).
#define EXT __attribute__((section(".ext.text"), noinline))
#define EXT_DATA __attribute__((section(".ext.data"), aligned(4)))  // sram_copy takes words
// The extension's second part (v0.9.22), the cheat search's, after the
// first (the lists', the scroll bar's, the screenshot's and the DEBUG
// snapshot's): only the search pages load it, so the others open as soon
// as they did before the search grew
#define EXS __attribute__((section(".exs.text"), noinline))
#define EXS_DATA __attribute__((section(".exs.data"), aligned(4)))
extern uint32_t __ov_ext_start[], __ov_ext_mid[], __ov_ext_end[];
extern const uint32_t __ov_ext_load[];

// --- State --------------------------------------------------------------------

enum row_kind {
	ROW_RESUME,
	ROW_SAVE,
	ROW_LOAD,
	ROW_CHEATS,
	ROW_SEARCH,  // the cheat search, when the game has no cheat list (the list has its row)
	ROW_SHOT,
	ROW_RESET,
	ROW_SLEEP,
	ROW_TORII,
	ROW_DEBUG,
	ROW_KINDS
};

static const uint8_t row_action[ROW_KINDS] = {
	INGAME_ACT_RESUME, INGAME_ACT_SAVE,
	INGAME_ACT_LOAD,   INGAME_ACT_RESUME,
	INGAME_ACT_RESUME, INGAME_ACT_RESUME,
	INGAME_ACT_RESET,  INGAME_ACT_SLEEP,
	INGAME_ACT_TORII,  INGAME_ACT_RESUME /* the overlay's own */,
};
static const uint16_t row_texts[ROW_KINDS] = {
	INGAME_TEXT_RESUME, INGAME_TEXT_SAVE,  INGAME_TEXT_LOAD,  INGAME_TEXT_CHEATS_ON, INGAME_TEXT_SEARCH,
	INGAME_TEXT_SHOT,   INGAME_TEXT_RESET, INGAME_TEXT_SLEEP, INGAME_TEXT_TORII,     INGAME_TEXT_DEBUG,
};
static const uint8_t why_texts[INGAME_WHYS] = {
	INGAME_TEXT_KEPT,     INGAME_TEXT_WHY_ROM,  INGAME_TEXT_WHY_LOCKED,
	INGAME_TEXT_WHY_CANT, INGAME_TEXT_WHY_ROOM, INGAME_TEXT_WHY_SLOW,
};
static const char mark_bytes[16] = INGAME_STATE_MARK;

// The dark theme's colours, for a session block that's missing
static const uint16_t fallback_colours[BANKS] = {0x1063, 0x0000, 0x2D0A, 0x6F7B, 0x7E8F, 0x3DEF};

// A line of text on screen: its sprites, tiles and place
struct slot {
	uint8_t y;  // its top on screen
	uint8_t oam;  // its first text sprite
	uint8_t max;  // sprites it may take
	uint8_t check;  // its check mark's sprite + 1, or 0 for none
	uint16_t tile;  // its first tile, from INGAME_OBJ_TILE
};

enum page_id {
	PAGE_MENU,
	PAGE_CHEATS,
	PAGE_SAVE,  // the state slots (P3): SAVE into one
	PAGE_LOAD,  // LOAD the buffer's
	// The cheat search (v0.9): its filters, the candidates left, one of them
	PAGE_SEARCH,
	PAGE_LIST,
	PAGE_PICK,
	PAGE_PTRS,  // the pointer scan's pointers to a candidate (v0.9.7)
};

// What a second press of A confirms (v0.9.22): what drops something
enum confirm {
	CONFIRM_NONE,
	CONFIRM_NEW,  // New search: the search ends
	CONFIRM_FIND,  // Find pointers: the list found before goes
	CONFIRM_SAVE,  // Save state while a search runs: the search ends
};

// The search page's rows, before a search and during one
enum {
	SR_SIZE,
	SR_START,
	SR_IDLE_ROWS
};
// The filters' rows first, in enum cheatsearch_op's order
enum {
	SR_SHOW = CHEATSEARCH_OPS,
	SR_UNDO,
	SR_AGAINST,  // the relative filters against the last scan or the first
	SR_SIGNED,  // negative numbers off or on
	SR_FORMAT,  // plain numbers or BCD
	SR_NEW,
	SR_ROWS
};
_Static_assert(CHEATSEARCH_OPS == INGAME_SEARCH_VALUES, "a number for each filter");
// Each filter's row's text (and its number's, after it)
static const uint16_t op_text[CHEATSEARCH_OPS] = {
	INGAME_TEXT_SEARCH_SAME,  INGAME_TEXT_SEARCH_CHANGED, INGAME_TEXT_SEARCH_UP,     INGAME_TEXT_SEARCH_DOWN,
	INGAME_TEXT_SEARCH_UP_BY, INGAME_TEXT_SEARCH_DOWN_BY, INGAME_TEXT_SEARCH_EQUALS, INGAME_TEXT_SEARCH_MORE,
	INGAME_TEXT_SEARCH_LESS,  INGAME_TEXT_SEARCH_BETWEEN,
};
// A candidate's page
enum {
	PR_VALUE,
	PR_KEEP,
	PR_KEEP_ON,
	PR_PTR_FIND,  // the pointer scan (v0.9.7)
	PR_PTR_CHECK,
	PR_ROWS
};

// The cheat list: the kernel's entries (but their lines) and what is on.
// Its rows are the entries, then its actions, in the accent as the
// kernel's list's "+ Add code" is: the cheats show as the page opens.
enum {
	LIST_ALL_OFF,
	LIST_AS_LAUNCHED,  // "Back to my picks"
	LIST_SEARCH,
	LIST_ACTIONS
};
struct cheat {
	uint16_t cost;
	uint8_t flags, bit;
};
_Static_assert(offsetof(struct ingame_cheat, flags) - offsetof(struct ingame_cheat, cost) == 6 &&
		       offsetof(struct ingame_cheat, bit) - offsetof(struct ingame_cheat, cost) == 7 &&
		       offsetof(struct ingame_cheat, act) - offsetof(struct ingame_cheat, cost) == 8,
	       "cheats_load reads the cost, flags and bit as two words, footer_text the act as a third");

enum mq_state {
	MQ_OFF,  // the row shows its cut line
	MQ_WAIT,  // that, until mq_wait runs out
	MQ_RUN,  // a step of the whole line, mq_px from its start
	MQ_END,  // the last step, until mq_wait runs out
};

// The session block's head (INGAME_SESSION_HEAD bytes), seen through session()
static uint32_t session_words[INGAME_SESSION_HEAD / 4];

static struct {
	const struct ingame_call *call;
	int have_session;
	uint32_t flags;  // the call's
	// The page
	enum page_id page;
	struct slot slots[SLOTS_MAX];
	uint32_t title;  // 1 if slot 0 is the title
	uint32_t footer;  // 1 if the last slot is the footer
	uint32_t visible;  // row slots
	uint32_t rows;  // rows in all
	uint32_t top;  // the row in the first row slot
	uint32_t cursor;  // a row, from 0
	uint32_t pill_oam;
	int pill_y, pill_to;
	uint32_t bar_oam;  // the scroll bar's first sprite; 0: the page has none
	int x0;  // the panel's left edge and width
	uint32_t panel_w;
	uint32_t oam_next;
	uint32_t tiles_backed;  // tiles from INGAME_OBJ_TILE backed up so far
	uint32_t tiles_end;  // the page's tiles
	uint32_t text_shown;  // the text's sprites are shown
	// The menu's rows (enum row_kind), and greyed ones
	uint8_t kinds[ROW_KINDS];
	uint8_t greyed[ROW_KINDS];
	// The cheat list
	uint32_t cheats;  // its entries
	struct cheat cheat[INGAME_CHEATS];
	uint32_t on[2];  // the entries on now, bit n for entry n
	uint32_t launched[2];  // and at launch
	uint32_t changed;  // a toggle changed since the menu opened
	uint32_t once;  // the entry SELECT asked to run once, + 1; 0: none
	// The marquee of the cursor's cheat: its whole line's width
	enum mq_state mq;
	uint32_t mq_wait, mq_px, mq_width;
	// The state slots (P3): whether the session has them, and the buffer's
	// slot (0: none for this game) and whether it is on the card
	uint32_t states;
	uint32_t buffer_slot, buffer_kept;
	uint32_t saved_slot;  // a SAVE's slot, for the answer
	uint32_t shots;  // the screenshots waiting for the kernel (this time or before)
	uint32_t shot_at;  // the next one's place in cart SRAM
	uint32_t shot_now;  // this opening took one
	uint32_t shot_full;  // and one more didn't fit
	uint32_t first;  // 1 + the session's first opening (the overlay hasn't answered yet); 0: not read yet
	// The display's registers as the game left them (io_readable's), for a screenshot
	uint16_t io[11];
	// The cheat search (v0.9)
	struct ingame_search search;  // its head; magic 0: no search of this launch's
	uint32_t search_size;  // the size Start takes: 0, 1, 2 for 8, 16 and 32 bits
	enum page_id search_from;  // the page that opened it
	uint32_t live;  // the live block's address; 0: found cheats are set once instead
	uint32_t found;  // the found cheats waiting in cart SRAM
	uint8_t found_live;  // and one of them is on now in the live block (INGAME_FOUND_LIVE)
	uint32_t edit, edit_place, edit_value;  // a number being edited (edit 1): its place, its value
	uint32_t edit_mode, edit_stage;  // its enum cheatsearch_mode; Between's second number (stage 1)
	// The pointer scan's list (struct ingame_pointers' head): its pointers,
	// whether the scan was full, those that passed the Checks and how many
	// Checks; the pointers page's rows (those it lists), and whether it
	// shows the list as Find made it after a Check
	uint16_t ptr_count, ptr_passed, ptr_checks;
	uint8_t ptr_full, ptr_all;
	uint32_t ptr_rows;
	uint32_t busy;  // a pass runs: the footer says so
	uint32_t notice;  // a search page's footer until the page changes (a text), 0 none
	uint8_t soft;  // the notice goes when the cursor moves (a confirmation's)
	uint8_t confirm;  // a confirmation asked (enum confirm): a second A does it
	uint8_t menu_notice;  // the menu's footer until the page changes (a text), 0 none
	uint32_t edit_saved;  // Between's lower as it was before its edit
	uint32_t list_n;  // the candidates the list shows (search_found)
	uint32_t pick, pick_value;  // a candidate's page: its index in list, the value to keep
	uint32_t ext;  // the extension's bytes while it is loaded, else 0
} m;

// --- Small helpers ------------------------------------------------------------

// GCC may turn loops into calls to these; the overlay has no C library
void *memset(void *dst, int c, size_t n)
{
	uint8_t *d = dst;
	while (n--)
		*d++ = (uint8_t)c;
	return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
	uint8_t *d = dst;
	const uint8_t *s = src;
	while (n--)
		*d++ = *s++;
	return dst;
}

static void vsync(void)
{
	while (REG_VCOUNT >= SCREEN_H)
		;
	while (REG_VCOUNT < SCREEN_H)
		;
}

static uint32_t keys_held(void)
{
	return ~(uint32_t)REG_KEYINPUT & KEYS_ALL;
}

static void debug_print(const char *text)
{
	REG_DEBUG_ENABLE = 0xC0DE;
	if (REG_DEBUG_ENABLE != 0x1DEA)
		return;
	uint32_t i = 0;
	for (; text[i]; i++)  // a short literal: mGBA's buffer takes 255
		REG_DEBUG_STRING[i] = text[i];
	REG_DEBUG_STRING[i] = 0;
	REG_DEBUG_FLAGS = 4 | 0x100;  // debug level, send
}

static const struct ingame_session *session(void)
{
	return (const struct ingame_session *)(const void *)session_words;
}

static void sram_in(uint32_t sram, void *ram, uint32_t n)
{
	m.call->sram_copy(sram, ram, n, 0);
}

static void sram_out(uint32_t sram, const void *ram, uint32_t n)
{
	m.call->sram_copy(sram, (void *)(uintptr_t)ram, n, 1);
}

// ram's n bytes to cart SRAM at sram, read back, and written once more if
// they didn't read back (as the stub's backups are): 0 if they still don't,
// and nothing of the game's may be changed then
static int sram_keep(uint32_t sram, const void *ram, uint32_t n)
{
	uint32_t buf[16], tries = 0;
	const volatile uint32_t *r = (const volatile uint32_t *)ram;
	sram_out(sram, ram, n);
	for (uint32_t at = 0; at < n; at += sizeof(buf)) {
		uint32_t k = n - at < sizeof(buf) ? n - at : sizeof(buf);
		sram_in(sram + at, buf, k);
		for (uint32_t i = 0; i < k / 4; i++)
			if (buf[i] != r[at / 4 + i]) {
				if (tries++)
					return 0;
				sram_out(sram, ram, n);  // once more, and read back from the start
				at = 0u - sizeof(buf);
				break;
			}
	}
	return 1;
}

static int bit_of(const uint32_t *bits, uint32_t n)
{
	return (int)(bits[n >> 5] >> (n & 31) & 1);
}

static void bit_set(uint32_t *bits, uint32_t n, int on)
{
	if (on)
		bits[n >> 5] |= 1u << (n & 31);
	else
		bits[n >> 5] &= ~(1u << (n & 31));
}

// --- Sprites ------------------------------------------------------------------

static void obj(int x, int y, uint32_t shape, uint32_t size, uint32_t tile, uint32_t bank, uint32_t a0, uint32_t a1)
{
	if (m.oam_next >= 128)
		return;
	volatile uint16_t *o = OAM + m.oam_next * 4;
	o[0] = (uint16_t)(((uint32_t)y & 0xFF) | a0 | shape << 14);
	o[1] = (uint16_t)(((uint32_t)x & 0x1FF) | a1 | size << 14);
	o[2] = (uint16_t)((INGAME_OBJ_TILE + tile) | bank << 12);
	m.oam_next++;
}

static void hide_all(void)
{
	for (uint32_t i = 0; i < 128; i++)
		OAM[i * 4] = A0_HIDE;
}

static void obj_bank(uint32_t i, uint32_t bank)
{
	volatile uint16_t *o = OAM + i * 4 + 2;
	*o = (uint16_t)((*o & 0x0FFF) | bank << 12);
}

static void obj_y(uint32_t i, int y)
{
	volatile uint16_t *o = OAM + i * 4;
	*o = (uint16_t)((*o & ~0xFFu) | ((uint32_t)y & 0xFF));
}

static void obj_shown(uint32_t i, int shown)
{
	volatile uint16_t *o = OAM + i * 4;
	*o = (uint16_t)(shown ? *o & ~A0_HIDE : *o | A0_HIDE);
}

// Backs up the OBJ tiles from INGAME_OBJ_TILE up to tile end, those not yet,
// and records it
static void tiles_need(uint32_t end)
{
	if (end <= m.tiles_backed)
		return;
	uint32_t from = m.tiles_backed;
	m.call->sram_copy(INGAME_BACKUP_OBJ + from * 32, (void *)(INGAME_OBJ_VRAM + from * 32), (end - from) * 32, 1);
	m.tiles_backed = end;
	ov_header.obj_bytes = end * 32;
}

// The shapes' tiles from TILE_CORNER, 4bpp in colour 1: the panel's top
// left corner (a quarter circle of radius 6), the pill's left end (half a
// circle of radius 8, 8x16) and the check mark of a cheat that is on (the
// kernel's cheat screen's). Tables: drawing them pixel by pixel took a
// third of a frame from EWRAM.
static const uint32_t shapes[] = {
	0x11110000u,
	0x11111100u,
	0x11111110u,
	0x11111110u,
	0x11111111u,
	0x11111111u,
	0x11111111u,
	0x11111111u,
	0x11100000u,
	0x11111000u,
	0x11111100u,
	0x11111110u,
	0x11111110u,
	0x11111111u,
	0x11111111u,
	0x11111111u,
	0x11111111u,
	0x11111111u,
	0x11111111u,
	0x11111110u,
	0x11111110u,
	0x11111100u,
	0x11111000u,
	0x11100000u,
	// the check mark, as the kernel's cheat screen draws it (the body font's)
	0x00000000u,
	0x01000000u,
	0x01100000u,
	0x00110001u,
	0x00011011u,
	0x00001110u,
	0x00000100u,
	0x00000000u,
};
_Static_assert(sizeof(shapes) == (TILE_TRACK - TILE_CORNER) * 32, "a tile each, the cap two");

// A column of solid sprites sh px tall from bottom up to top, the last
// overlapping the one below it at the top. Where two overlap the lines cost
// twice the sprite time (954 cycles a line with the game's H-blank free
// set: past it the hardware drops the last sprites, the cover's, and the
// game showed through bright), so every column overlaps at its top: the
// panel's in its top 32 px, which hold its title and no row (TITLE_GAP).
static void column(int x, int top, int bottom, uint32_t shape, uint32_t size, int sh, uint32_t bank, uint32_t a0)
{
	int y = bottom - sh;
	for (; y > top; y -= sh)
		obj(x, y, shape, size, TILE_SOLID, bank, a0, 0);
	obj(x, top, shape, size, TILE_SOLID, bank, a0, 0);
}

// Columns of 64x32 sprites across x0..x1, the last overlapping the one
// before it
static void columns(int x0, int x1, int top, int bottom, uint32_t bank, uint32_t a0)
{
	for (int x = x0;; x += 64) {
		x = x > x1 - 64 ? x1 - 64 : x;
		column(x, top, bottom, SHAPE_WIDE, 3, 32, bank, a0);
		if (x == x1 - 64)
			break;
	}
}

// The panel x0..x1, y0..y1: its rounded corners, its 8 px sides between
// them and its body between those; then the cover over the game around it:
// bands above and below it, down to and from the corners' rows, which show
// it, and 32 px strips beside it. Never behind the panel, where it only
// took the lines' sprite time.
static void panel(int x0, int y0, int x1, int y1)
{
	obj(x0, y0, SHAPE_SQUARE, 0, TILE_CORNER, BANK_PANEL, 0, 0);
	obj(x1 - 8, y0, SHAPE_SQUARE, 0, TILE_CORNER, BANK_PANEL, 0, A1_HFLIP);
	obj(x0, y1 - 8, SHAPE_SQUARE, 0, TILE_CORNER, BANK_PANEL, 0, A1_VFLIP);
	obj(x1 - 8, y1 - 8, SHAPE_SQUARE, 0, TILE_CORNER, BANK_PANEL, 0, A1_HFLIP | A1_VFLIP);
	column(x0, y0 + 8, y1 - 8, SHAPE_TALL, 1, 32, BANK_PANEL, 0);
	column(x1 - 8, y0 + 8, y1 - 8, SHAPE_TALL, 1, 32, BANK_PANEL, 0);
	columns(x0 + 8, x1 - 8, y0, y1, BANK_PANEL, 0);
	columns(0, SCREEN_W, -31, y0 + 8, BANK_DIM, A0_SEMI);
	columns(0, SCREEN_W, y1 - 8, SCREEN_H, BANK_DIM, A0_SEMI);
	column(x0 - 32, y0 + 8, y1 - 8, SHAPE_TALL, 3, 64, BANK_DIM, A0_SEMI);
	column(x1, y0 + 8, y1 - 8, SHAPE_TALL, 3, 64, BANK_DIM, A0_SEMI);
}

// --- Text ---------------------------------------------------------------------

// Bits 0-7 of b to nibbles 0-7, each 0 or 1: a 1bpp row byte as a tile row
static uint32_t spread(uint32_t b)
{
	b = (b | b << 12) & 0x000F000Fu;
	b = (b | b << 6) & 0x03030303u;
	return (b | b << 3) & 0x11111111u;
}

// A line into slot s's tiles, and as many of its sprites shown as the line
// needs (once the text shows)
static void line_tiles(const struct slot *s, const struct ingame_line *line, uint32_t bank)
{
	uint32_t n = ((uint32_t)line->width + 31) / 32;
	if (n > s->max)
		n = s->max;
	// The tiles in VRAM's order (a sprite's top four, its bottom four, each
	// tile's 8 rows), so the words go out one after another: the opening's
	// frames are tight
	volatile uint32_t *dst = OBJ_TILES + s->tile * 8;
	for (uint32_t c0 = 0; c0 < n * 4u; c0 += 4)
		for (uint32_t y0 = 0; y0 < 16; y0 += 8)
			for (uint32_t c = c0; c < c0 + 4; c++)
				for (uint32_t y = y0; y < y0 + 8; y++) {
					uint32_t row = y - TEXT_TOP;
					*dst++ = row < INGAME_TEXT_ROWS && c < INGAME_TEXT_W / 8
							 ? spread(line->bits[row][c])
							 : 0;
				}
	for (uint32_t k = 0; k < s->max; k++) {
		obj_bank(s->oam + k, bank);
		obj_shown(s->oam + k, m.text_shown && k < n);
	}
}

// Row bits v (bit 0 the leftmost) into l's row from pixel x on
static void row_or(struct ingame_line *l, uint32_t row, uint32_t x, uint32_t v)
{
	uint32_t at = x >> 3;
	v <<= x & 7;
	for (; v && at < INGAME_TEXT_W / 8; at++, v >>= 8)
		l->bits[row][at] |= (uint8_t)v;
}

static void width_to(struct ingame_line *l, uint32_t x)
{
	x = x < INGAME_TEXT_W ? x : INGAME_TEXT_W;
	if (x > l->width)
		l->width = (uint16_t)x;
}

// The check mark (shapes' TILE_CHECK) as rows of a line, bit 0 leftmost
EXT_DATA static uint8_t check_bits[8] =  // not const: .ext.data holds writable data too
	{0x00, 0x40, 0x60, 0x31, 0x1B, 0x0E, 0x04, 0x00};

// How many of the cheats are on, as the check mark and "on/all" after l's
// text: the cheat list's title. The extension's, as all the cheat list's
// own code is (the list loads it)
EXT static void count_put(struct ingame_line *l)
{
	uint32_t on = 0;
	for (uint32_t e = 0; e < m.cheats; e++)
		on += (uint32_t)bit_of(m.on, e);
	uint32_t x = l->width + 8;
	for (uint32_t y = 0; y < 8; y++)  // in the line's rows as the rows' marks sit in theirs
		row_or(l, y + 4 - TEXT_TOP, x, check_bits[y]);
	x += 11;
	// on, the slash, all: each a digit or two (INGAME_CHEATS is 64)
	const uint32_t g[5] = {on / 10, on % 10, INGAME_GLYPH_SLASH, m.cheats / 10, m.cheats % 10};
	for (uint32_t i = 0; i < 5; i++) {
		struct ingame_glyph gl;
		if ((i == 0 || i == 3) && !g[i])
			continue;  // no leading zero
		sram_in(INGAME_SRAM_GLYPHS + g[i] * sizeof(gl), &gl, sizeof(gl));
		for (uint32_t row = 0; row < INGAME_TEXT_ROWS; row++)
			row_or(l, row, x, gl.rows[row]);
		x += gl.width;
	}
	width_to(l, x);
}

// The line at sram (its place in cart SRAM; 0: none) into line
static void line_load(uint32_t sram, struct ingame_line *line)
{
	if (m.have_session && sram)
		sram_in(sram, line, sizeof(*line));
	else
		memset(line, 0, sizeof(*line));
}

// The bytes of a whole line's row a marquee step reads: from a word
// boundary, a line's bytes and the one after them
#define MQ_ROW_WORDS 7
_Static_assert(MQ_ROW_WORDS * 4 >= 3 + INGAME_TEXT_W / 8 + 1, "a step's row: a line's bytes and the one after");

// A marquee step into slot s's tiles: entry e's whole line (INGAME_SRAM_WIDE)
// from pixel px on, as a line of the row's width. Its rows are read from
// cart SRAM a step at a time: the window the stub backs up has no room to
// spare for a copy (the menu showed a frame later with one).
// Kept out of slot_update: the window's stack has room for one line a call
EXT static void slot_draw_wide(const struct slot *s, uint32_t e, uint32_t px, uint32_t bank)
{
	struct ingame_line line;
	uint32_t from = (px >> 3) & ~3u, n = INGAME_WIDE_W / 8 - from, at = (px >> 3) - from;
	n = n < MQ_ROW_WORDS * 4 ? n : MQ_ROW_WORDS * 4;
	line.width = INGAME_ROW_W;
	for (uint32_t row = 0; row < INGAME_TEXT_ROWS; row++) {
		uint32_t words[MQ_ROW_WORDS] = {0};
		const uint8_t *bytes = (const uint8_t *)words;
		sram_in(INGAME_SRAM_WIDE + e * sizeof(struct ingame_wide) + offsetof(struct ingame_wide, bits) +
				row * (INGAME_WIDE_W / 8) + from,
			words, n);
		for (uint32_t c = 0; c < INGAME_TEXT_W / 8; c++)
			line.bits[row][c] = (uint8_t)((bytes[at + c] | (uint32_t)bytes[at + c + 1] << 8) >> (px & 7));
	}
	line_tiles(s, &line, bank);
}

// Text n's line's place in cart SRAM: lines are named by their places
static uint32_t text_at(uint32_t text)
{
	return INGAME_TEXT_SRAM(text);
}

// The width of the line at sram (its place in cart SRAM)
static uint32_t line_width(uint32_t sram)
{
	uint32_t word = 0;
	if (m.have_session && sram >= text_at(0) && sram < text_at(INGAME_TEXT_SEARCH))
		return session()->text_width[(sram - text_at(0)) / sizeof(struct ingame_line)];
	if (m.have_session && sram)
		sram_in(sram, &word, 4);
	return word & 0xFFFF;
}

// --- The cheat list -----------------------------------------------------------

static uint32_t cheat_why(uint32_t e)
{
	return m.cheat[e].flags & INGAME_CHEAT_WHY;
}

// The cheats' load with the entries on now (cheatcode.h's CHEAT_LOAD model).
// This and the cheat list's other helpers are the extension's, which its
// page loads (the menu's opening has no room for them)
EXT static uint32_t cheats_cost(void)
{
	const struct ingame_session *s = session();
	uint32_t total = s->cheat_call + s->cheat_table * (uint32_t)s->cheat_skip;
	for (uint32_t e = 0; e < m.cheats; e++)
		if ((m.cheat[e].flags & INGAME_CHEAT_TABLE) && bit_of(m.on, e))
			total += m.cheat[e].cost - (uint32_t)s->cheat_skip;
	return total;
}

// Why entry e can't change now: its own reason, or the cycle cap for one off
EXT static uint32_t cheat_why_now(uint32_t e)
{
	uint32_t why = cheat_why(e);
	if (why != INGAME_WHY_NONE || bit_of(m.on, e))
		return why;
	const struct ingame_session *s = session();
	return cheats_cost() + m.cheat[e].cost - s->cheat_skip > s->cheat_max ? INGAME_WHY_SLOW : INGAME_WHY_NONE;
}

// 1 if entry e can run once now (SELECT): off, its code waits on no buttons
// of its own, and A could turn it on (one too slow beside those on shows why)
EXT static int cheat_once_ok(uint32_t e)
{
	const struct cheat *c = &m.cheat[e];
	return (c->flags & INGAME_CHEAT_RUN_ONCE) && (c->flags & INGAME_CHEAT_TABLE) && c->bit < INGAME_CHEATS &&
	       cheat_why_now(e) == INGAME_WHY_NONE && !bit_of(m.on, e);
}

// The cheats' entries from the session block, and which are on: the
// toggles the interpreter reads, against the launch
static void cheats_load(void)
{
	const struct ingame_session *s = session();
	m.cheats =
		m.have_session && (m.flags & INGAME_CAN_CHEATS) && s->cheat_count <= INGAME_CHEATS ? s->cheat_count : 0;
	const uint32_t toggles[2] = {TOGGLES[0], TOGGLES[1]};
	for (uint32_t e = 0; e < m.cheats; e++) {
		struct cheat *c = &m.cheat[e];
		uint32_t meta[2];  // cost, number; index, flags, bit
		sram_in(INGAME_SRAM_SESSION + INGAME_SESSION_CHEAT(e) + offsetof(struct ingame_cheat, cost), meta,
			sizeof(meta));
		c->cost = (uint16_t)meta[0];
		c->flags = (uint8_t)(meta[1] >> 16);
		c->bit = (uint8_t)(meta[1] >> 24);
		uint32_t launched = (c->flags & INGAME_CHEAT_ON) != 0;
		bit_set(m.launched, e, (int)launched);
		int on = (int)launched;
		if ((c->flags & INGAME_CHEAT_TABLE) && c->bit < INGAME_CHEATS)
			on ^= bit_of(toggles, c->bit);
		bit_set(m.on, e, on);
	}
}

// The toggles from what is on
static void cheats_store(void)
{
	uint32_t toggles[2] = {TOGGLES[0], TOGGLES[1]};
	for (uint32_t e = 0; e < m.cheats; e++) {
		const struct cheat *c = &m.cheat[e];
		if ((c->flags & INGAME_CHEAT_TABLE) && c->bit < INGAME_CHEATS)
			bit_set(toggles, c->bit, bit_of(m.on, e) != bit_of(m.launched, e));
	}
	TOGGLES[0] = toggles[0];
	TOGGLES[1] = toggles[1];
	m.changed = 1;
}

// The cheat search's side of the pages (below)
static int ext_load(void);
static void edit_cancel(void);
static void found_all_off(void);
static void found_head(void);
static void search_keep_ptr(uint32_t r, int on);
static int searching(void);
static int slot_compose(uint32_t k, struct ingame_line *l);
static int search_greyed(uint32_t r);
static uint32_t search_row_text(uint32_t r);
static uint32_t search_footer(void);
static uint32_t cheats_footer(void);
static uint32_t cheat_why_now(uint32_t e);
static int cheat_once_ok(uint32_t e);

// --- Pages --------------------------------------------------------------------

// The row in slot k's place, or -1 for none
static int slot_row(uint32_t k)
{
	uint32_t r = m.top + k - m.title;
	return r < m.rows && k >= m.title && k < m.title + m.visible ? (int)r : -1;
}

static int row_greyed(uint32_t r)
{
	if (m.page == PAGE_MENU)
		return r < ROW_KINDS && m.greyed[r];  // (the menu's rows are each kind once: r < m.rows <= ROW_KINDS)
	if (m.page == PAGE_SAVE)
		return 0;
	if (m.page == PAGE_LOAD)
		return r + 1 != m.buffer_slot;  // only the buffer's slot loads in game
	if (m.page >= PAGE_SEARCH)
		return search_greyed(r);
	return r < m.cheats && cheat_why_now(r) != INGAME_WHY_NONE;
}

// The buffer holds a state saved in game that the card hasn't
static int buffer_unkept(void)
{
	return m.buffer_slot && !m.buffer_kept;
}

// What waits in cart memory for Back to Torii (a state saved in game, the
// screenshots), as the menu's footer's text; 0 for nothing
static uint32_t unsaved_text(void)
{
	uint32_t state = m.states && buffer_unkept();
	if (m.found)  // found cheats, then with a state, screenshots or both
		return INGAME_TEXT_UNSAVED_FOUND + state + 2 * (m.shots != 0);
	if (!m.shots)
		return state ? INGAME_TEXT_UNSAVED_STATE : 0;
	return (state ? INGAME_TEXT_UNSAVED_BOTH : INGAME_TEXT_UNSAVED_SHOTS) + m.shots - 1;
}

// Row r's text, as its line's place in cart SRAM
static uint32_t row_text(uint32_t r)
{
	if (m.page == PAGE_SAVE || m.page == PAGE_LOAD)
		return INGAME_SRAM_SESSION + INGAME_SESSION_STATE(r + 1, !(r + 1 == m.buffer_slot && buffer_unkept()));
	if (m.page >= PAGE_SEARCH)
		return search_row_text(r);
	if (m.page == PAGE_CHEATS)
		return r < m.cheats                       ? INGAME_SRAM_SESSION + INGAME_SESSION_CHEAT(r)
		       : r == m.cheats + LIST_ALL_OFF     ? text_at(INGAME_TEXT_ALL_OFF)
		       : r == m.cheats + LIST_AS_LAUNCHED ? text_at(INGAME_TEXT_AS_LAUNCHED)
		       : searching()                      ? INGAME_SRAM_SEARCH_ROW
							  : text_at(INGAME_TEXT_SEARCH);
	uint32_t kind = m.kinds[r];
	if (kind == ROW_TORII && unsaved_text())
		return text_at(INGAME_TEXT_TORII_SAVE);
	if (kind == ROW_SHOT && m.shots)
		return text_at(INGAME_TEXT_SHOT_COUNT + m.shots - 1);
	if (kind == ROW_SEARCH && searching())  // with its count: the extension wrote it
		return INGAME_SRAM_SEARCH_ROW;
	if (kind == ROW_CHEATS)
		return text_at(m.cheats                                       ? INGAME_TEXT_CHEATS
			       : *(volatile uint8_t *)INGAME_CHEAT_ONOFF == 1 ? INGAME_TEXT_CHEATS_OFF
									      : INGAME_TEXT_CHEATS_ON);
	return text_at(row_texts[kind]);
}

// The cheat list's footer: a cheat's reason it can't change, else its
// activation's sentence; on its action rows that the picks are kept
EXT static uint32_t cheats_footer(void)
{
	uint32_t why = INGAME_WHY_NONE;
	if (m.cursor < m.cheats) {
		uint32_t e = m.cursor;
		if (m.once == e + 1)
			return text_at(INGAME_TEXT_ONCE_QUEUED);
		if (cheat_once_ok(e))
			return text_at(INGAME_TEXT_ONCE_HINT);
		why = cheat_why_now(e);
		uint32_t act = 0;  // its act byte, read when it shows (a word: sram_copy's)
		if (why == INGAME_WHY_NONE && m.have_session)
			sram_in(INGAME_SRAM_SESSION + INGAME_SESSION_CHEAT(e) + offsetof(struct ingame_cheat, act),
				&act, 4);
		act &= 0xFF;
		if (act && act <= INGAME_ACT_LINES)
			return INGAME_SRAM_SESSION + INGAME_SESSION_ACT(act - 1);
	}
	return text_at(why_texts[why]);
}

// The footer's text on the pages but the menu: why the row under the
// cursor can't change, else that the toggles are kept. The extension's
// (each of those pages loads it, for its scroll bar or its code)
EXT static uint32_t footer_text(void)
{
	if (m.page >= PAGE_SEARCH)
		return search_footer();
	if (m.page == PAGE_SAVE && m.search.magic)
		return text_at(m.confirm == CONFIRM_SAVE ? INGAME_TEXT_SEARCH_CONFIRM_SAVE
							 : INGAME_TEXT_SEARCH_SAVE_ENDS);
	if (m.page == PAGE_SAVE)
		return text_at(buffer_unkept() && m.cursor + 1 != m.buffer_slot ? INGAME_TEXT_STATE_REPLACES
										: INGAME_TEXT_STATE_KEEP);
	if (m.page == PAGE_LOAD)
		return text_at(!row_greyed(m.cursor)                        ? INGAME_TEXT_STATE_LOAD
			       : (session()->state_have & (1u << m.cursor)) ? INGAME_TEXT_STATE_ELSEWHERE
									    : INGAME_TEXT_STATE_EMPTY);
	return cheats_footer();
}

// Whether this is the session's first opening: read once it matters (the
// opening's frames are tight)
static int first_opening(void)
{
	if (!m.first && m.have_session) {
		uint32_t magic;
		sram_in(INGAME_SRAM_SESSION + INGAME_SESSION_OUT, &magic, 4);
		m.first = 1 + (magic != INGAME_OUT_MAGIC);
	}
	return m.first == 2;
}

// The menu's footer: the DEBUG notice, a notice (why the search didn't
// open), that no screenshot more fits or one was just taken, on the Sleep
// row the keys that wake the game; on the Save state row that saving ends
// a search, on Reset game that it stops the found cheats on now; on the
// Save state and Screenshot rows at the session's first opening that Back
// to Torii saves them to the card, and on the Save state row that the
// state saved in game is lost if the console goes off first; what waits
// for Back to Torii, or the keys that close the menu
static uint32_t menu_footer(void)
{
	uint32_t kind = m.rows ? m.kinds[m.cursor] : ROW_KINDS;
	return (m.flags & NOTICE_DEBUG)                                    ? INGAME_TEXT_DEBUG_SAVED
	       : m.menu_notice                                             ? m.menu_notice
	       : m.shot_full                                               ? INGAME_TEXT_SHOT_FULL
	       : m.shot_now                                                ? INGAME_TEXT_SHOT_SAVED
	       : kind == ROW_SLEEP                                         ? INGAME_TEXT_SLEEP_WAKES
	       : kind == ROW_SAVE && m.search.magic                        ? INGAME_TEXT_SEARCH_SAVE_ENDS
	       : kind == ROW_RESET && m.found_live                         ? INGAME_TEXT_RESET_LIVE
	       : (kind == ROW_SAVE || kind == ROW_SHOT) && first_opening() ? INGAME_TEXT_FIRST_SAVE
	       : kind == ROW_SAVE && m.states && buffer_unkept()           ? INGAME_TEXT_STATE_SAVED
	       : unsaved_text()                                            ? unsaved_text()
									   : INGAME_TEXT_HINT;
}

// Slot k's line now, as its place in cart SRAM (0: none)
static uint32_t slot_text(uint32_t k)
{
	if (m.title && k == 0)
		return text_at(m.page == PAGE_CHEATS   ? INGAME_TEXT_CHEATS
			       : m.page == PAGE_PTRS   ? INGAME_TEXT_SEARCH_POINTERS
			       : m.page >= PAGE_SEARCH ? INGAME_TEXT_SEARCH
			       : m.page == PAGE_SAVE   ? INGAME_TEXT_SAVE
			       : m.page == PAGE_LOAD   ? INGAME_TEXT_LOAD
						       : INGAME_TEXT_TITLE);
	if (m.footer && k == m.title + m.visible)
		return m.page == PAGE_MENU ? text_at(menu_footer()) : m.ext ? footer_text() : 0;
	int r = slot_row(k);
	return r >= 0 ? row_text((uint32_t)r) : 0;
}

// Slot k's line, the search's or its text, into its tiles. Kept out of
// slot_update, as slot_draw_wide is: the window's stack has room for one
// line a call
__attribute__((noinline)) static void slot_draw(uint32_t k, uint32_t bank)
{
	struct ingame_line line;
	if (m.page < PAGE_SEARCH || !slot_compose(k, &line))  // the extension only on its pages
		line_load(slot_text(k), &line);
	if (m.page == PAGE_CHEATS && k == 0)
		count_put(&line);
	line_tiles(&m.slots[k], &line, bank);
}

static void slot_update(uint32_t k)
{
	const struct slot *s = &m.slots[k];
	int r = slot_row(k);
	// The Save state page's row for the slot in the buffer (the one Load
	// state gives back) in the accent
	int current = m.page == PAGE_SAVE && r >= 0 && (uint32_t)r + 1 == m.buffer_slot;
	// and the cheat list's actions
	current |= m.page == PAGE_CHEATS && r >= (int)m.cheats;
	uint32_t bank = (m.title && k == 0) || current         ? BANK_TITLE
			: m.footer && k == m.title + m.visible ? BANK_FAINT
			: r >= 0 && row_greyed((uint32_t)r)    ? BANK_FAINT
							       : BANK_TEXT;
	if ((m.mq == MQ_RUN || m.mq == MQ_END) && r >= 0 && (uint32_t)r == m.cursor) {
		slot_draw_wide(s, m.cursor, m.mq_px, bank);
	} else {
		slot_draw(k, bank);
	}
	if (s->check) {
		uint32_t o = s->check - 1u;
		int on = r >= 0 && (uint32_t)r < m.cheats && bit_of(m.on, (uint32_t)r);
		obj_shown(o, on && m.text_shown);
		if (on)
			obj_bank(o, row_greyed((uint32_t)r) ? BANK_FAINT : BANK_TITLE);
	}
}

static void slots_update(void)
{
	for (uint32_t k = 0; k < m.title + m.visible + m.footer; k++)
		slot_update(k);
}

static int row_y(uint32_t r)
{
	return m.slots[m.title + r - m.top].y;
}

static void pill_place(int y)
{
	for (uint32_t i = 0; i < 2 + PILL_FILLS; i++)
		obj_y(m.pill_oam + i, y);
}

// The scroll bar's sprites in their places for the rows shown: the thumb
// as long as the page is of the rows, where the top row is among them, on
// a track the rows' height. An extension's (EXT): a page has a bar only with it loaded
EXT static void bar_place(void)
{
	uint32_t next = m.oam_next;
	uint32_t len = m.visible * ROW_H - 4, t = len * m.visible / m.rows;  // unsigned: no signed division linked
	t = t < BAR_MIN ? BAR_MIN : t;
	int top = m.slots[m.title].y + 2, x = m.x0 + (int)m.panel_w - BAR_RIGHT;
	int y = top + (int)((len - t) * m.top / (m.rows - m.visible));
	m.oam_next = m.bar_oam;
	obj(x, y, SHAPE_TALL, 0, TILE_BAR_END, BANK_TITLE, 0, 0);
	obj(x, y + (int)t - 16, SHAPE_TALL, 0, TILE_BAR_END, BANK_TITLE, 0, A1_VFLIP);
	for (int k = 0, at = y + 16; k < BAR_FILLS; k++, at += 16)
		obj(x, at < y + (int)t - 32 ? at : y + (int)t - 32, SHAPE_TALL, 0, TILE_BAR, BANK_TITLE,
		    at < y + (int)t - 16 ? 0 : A0_HIDE, 0);
	for (uint32_t k = 0; k < BAR_TRACKS; k++)
		obj(x, top + (int)(k * 32 < len - 32 ? k * 32 : len - 32), SHAPE_TALL, 1, TILE_TRACK, BANK_PILL, 0, 0);
	m.oam_next = next;
}

static int state_saved(void)
{
	uint8_t mark[16] __attribute__((aligned(4)));  // sram_copy takes words
	sram_in(INGAME_SRAM_STATE_MARK, mark, sizeof(mark));
	for (uint32_t i = 0; i < sizeof(mark); i++)
		if (mark[i] != (uint8_t)mark_bytes[i])
			return 0;
	return 1;
}

static void add_row(uint32_t kind, int greyed)
{
	if (m.rows >= ROW_KINDS)
		return;  // each kind once: never
	m.greyed[m.rows] = (uint8_t)greyed;
	m.kinds[m.rows++] = (uint8_t)kind;
}

static uint32_t next_row(uint32_t from, int step)
{
	uint32_t at = from;
	for (uint32_t i = 0; i < m.rows; i++) {
		at = (at + m.rows + (uint32_t)step) % m.rows;
		if (m.page != PAGE_MENU || !row_greyed(at))
			return at;
	}
	return from;
}

// The page's rows, then its panel without text, its text sprites in front
// but hidden: the cover, the panel and the pill. The page's own tiles
// backed up first. Shows at the next VBlank.
static void build_page(enum page_id page, uint32_t cursor)
{
	const struct ingame_session *s = session();
	m.page = page;
	m.rows = 0;
	m.top = 0;
	uint32_t panel_w;
	if (page == PAGE_MENU) {
		uint32_t f = m.flags;
		int have_state =
			(f & INGAME_CAN_STATES) && !m.states && state_saved();  // the old rows' (the slots' read it)
		add_row(ROW_RESUME, 0);
		if (f & INGAME_CAN_STATES) {
			add_row(ROW_SAVE, 0);
			add_row(ROW_LOAD, !(m.states ? m.buffer_slot != 0 : have_state));
		}
		if (f & INGAME_CAN_CHEATS)
			add_row(ROW_CHEATS, 0);
		else if (m.have_session)
			add_row(ROW_SEARCH, 0);  // with a cheat list, on its page
		if (m.have_session)
			add_row(ROW_SHOT, 0);
		add_row(ROW_RESET, 0);
		add_row(ROW_SLEEP, 0);
		add_row(ROW_TORII, 0);
		if (m.have_session && (s->flags & INGAME_DEBUG_ON))
			add_row(ROW_DEBUG, 0);
		// The footer (the keys, or a notice) when the screen takes it, or
		// the DEBUG notice; then the title, room left
		m.visible = m.rows;
		uint32_t h = PANEL_PAD * 2 + m.rows * ROW_H;
		m.footer = ((f & NOTICE_DEBUG) || m.have_session) && h + ROW_H <= SCREEN_H;
		h += m.footer * ROW_H;
		panel_w = MENU_W;
		m.title = h + ROW_H + TITLE_GAP <= SCREEN_H - 8;  // the title goes when the screen can't take it
	} else if (page == PAGE_SAVE || page == PAGE_LOAD) {
		m.rows = INGAME_STATE_SLOTS;
		m.visible = LIST_ROWS;
		m.title = 1;
		m.footer = 1;
		panel_w = LIST_W;
	} else if (page >= PAGE_SEARCH) {
		m.rows = page == PAGE_PTRS   ? m.ptr_rows + (m.ptr_checks != 0)
			 : page == PAGE_LIST ? m.list_n
			 : page == PAGE_PICK ? PR_ROWS
			 : m.search.magic    ? SR_ROWS
					     : SR_IDLE_ROWS;
		m.visible = m.rows < LIST_ROWS ? m.rows : LIST_ROWS;
		m.title = 1;
		m.footer = 1;
		panel_w = LIST_W;
	} else {
		m.rows = m.cheats + LIST_ACTIONS;
		m.visible = m.rows < LIST_ROWS ? m.rows : LIST_ROWS;
		m.title = 1;
		m.footer = 1;
		panel_w = LIST_W;
	}
	uint32_t bar = page != PAGE_MENU && m.rows > m.visible && ext_load();  // its code is the extension's
	uint32_t h = PANEL_PAD * 2 + (m.title ? ROW_H + TITLE_GAP : 0) + (m.visible + m.footer) * ROW_H;
	int x0 = (SCREEN_W - (int)panel_w) / 2, y0 = (SCREEN_H - (int)h) / 2;
	uint32_t slots = m.title + m.visible + m.footer;

	// The lines' tiles from the scroll bar's, or in their place when the
	// page has none: the menu, whose opening has no time to back up more
	uint32_t base = bar ? TILE_TEXT : TILE_TRACK;
	tiles_need(base);
	uint16_t colour[BANKS];
	for (uint32_t i = 0; i < BANKS; i++)
		colour[i] = fallback_colours[i];
	if (m.have_session) {
		colour[BANK_PANEL] = s->palette[INGAME_ROLE_FLOAT];
		colour[BANK_PILL] = s->palette[INGAME_ROLE_TINT];
		colour[BANK_TEXT] = s->palette[INGAME_ROLE_TEXT];
		colour[BANK_TITLE] = s->palette[INGAME_ROLE_ACCENT_INK];
		colour[BANK_FAINT] = s->palette[INGAME_ROLE_FAINT];
	}
	for (uint32_t i = 0; i < BANKS; i++)
		OBJ_PAL[i * 16 + 1] = colour[i];
	volatile uint32_t *t = OBJ_TILES + TILE_SOLID * 8;
	for (uint32_t i = 0; i < 32 * 8; i++)
		t[i] = 0x11111111u;
	for (uint32_t i = 0; i < sizeof(shapes) / 4; i++)
		t[TILE_CORNER * 8 + i] = shapes[i];
	// The scroll bar's, as the kernel's lists' (ui_widgets.c): the track a
	// 1 px line, the thumb 3 px around it, its end's first row that line's
	for (uint32_t i = (TILE_TRACK - TILE_CORNER) * 8; i < (base - TILE_CORNER) * 8; i++)
		t[TILE_CORNER * 8 + i] = i < (TILE_BAR - TILE_CORNER) * 8 || i == (TILE_BAR_END - TILE_CORNER) * 8
						 ? 0x00001000u
						 : 0x00011100u;

	// Sprites, front to back: text and check marks (hidden until drawn),
	// the scroll bar, the pill, the panel, the cover
	hide_all();
	m.oam_next = 0;
	int y = y0 + PANEL_PAD;
	uint32_t tile = base;
	for (uint32_t k = 0; k < slots; k++) {
		struct slot *sl = &m.slots[k];
		sl->y = (uint8_t)y;
		sl->tile = (uint16_t)tile;
		// The menu's lines never change their text but the Cheats row's: as
		// many sprites as the line needs, 4 at most (the panel's room)
		uint32_t max = SLOT_SPRITES;
		if (page == PAGE_MENU) {
			uint32_t wide = (m.title && k == 0) || (m.footer && k == m.title + m.visible);
			// the footer: its lines change with the row, the widest nearly all its room
			uint32_t w = wide && k ? WIDE_SPRITES * 32 : line_width(slot_text(k));
			int r = slot_row(k);
			if (r >= 0 && m.kinds[r] == ROW_CHEATS && !m.cheats) {  // the wider of on and off
				uint32_t w2 = line_width(text_at(INGAME_TEXT_CHEATS_OFF));
				uint32_t w3 = line_width(text_at(INGAME_TEXT_CHEATS_ON));
				w = w2 > w3 ? w2 : w3;
			}
			if (r >= 0 && m.kinds[r] == ROW_SHOT) {  // the widest of its counts
				uint32_t w2 = line_width(text_at(INGAME_TEXT_SHOT_COUNT + INGAME_SHOTS_MAX - 1));
				w = w2 > w ? w2 : w;
			}
			uint32_t most = wide ? WIDE_SPRITES : SLOT_SPRITES;
			max = (w + 31) / 32;
			max = max < 1 ? 1 : max > most ? most : max;
		} else if ((m.footer && k == m.title + m.visible) || (page == PAGE_CHEATS && k == 0))
			max = WIDE_SPRITES;  // the list's footer: a line of the menu's width (an activation's sentence); the cheat list's title with its count
		sl->max = (uint8_t)max;
		tile += max * 8;
		// Its sprites' places in OAM, kept for build_text: the panel
		// shows sooner without them
		sl->oam = (uint8_t)m.oam_next;
		m.oam_next += max;
		sl->check = 0;
		int row = k >= m.title && k < m.title + m.visible;
		if (page == PAGE_CHEATS && row)
			sl->check = (uint8_t)(++m.oam_next);
		y += m.title && k == 0 ? ROW_H + TITLE_GAP : ROW_H;
	}
	m.x0 = x0;
	m.panel_w = panel_w;
	m.bar_oam = bar * m.oam_next;
	m.oam_next += bar * BAR_SPRITES;
	m.cursor = cursor < m.rows ? cursor : 0;
	if (page == PAGE_MENU && row_greyed(m.cursor))
		m.cursor = next_row(m.cursor, 1);
	if (m.cursor >= m.top + m.visible)
		m.top = m.cursor - m.visible + 1;
	m.pill_y = m.pill_to = row_y(m.cursor);
	if (bar)
		bar_place();
	m.pill_oam = m.oam_next;
	int pill_x = x0 + PILL_INSET, pill_w = (int)panel_w - 2 * PILL_INSET - (int)bar * BAR_ROOM;
	obj(pill_x, m.pill_y, SHAPE_TALL, 0, TILE_CAP, BANK_PILL, 0, 0);
	obj(pill_x + pill_w - 8, m.pill_y, SHAPE_TALL, 0, TILE_CAP, BANK_PILL, 0, A1_HFLIP);
	// The widest pill's fills, those this one doesn't need hidden
	int fills = (pill_w - 16 + 31) / 32;
	for (int x = pill_x + 8, k = 0; k < PILL_FILLS; k++, x += 32) {
		int at = x > pill_x + pill_w - 8 - 32 ? pill_x + pill_w - 8 - 32 : x;
		obj(at, m.pill_y, SHAPE_WIDE, 2, TILE_SOLID, BANK_PILL, k < fills ? 0 : A0_HIDE, 0);
	}
	panel(x0, y0, x0 + (int)panel_w, y0 + (int)h);
	m.text_shown = 0;
	m.tiles_end = tile;
}

// The text: its tiles backed up and drawn; its sprites shown at the next VBlank
// Slots from..to-1 drawn: their sprites (hidden) and check marks in the
// places build_page kept, their tiles backed up and drawn; shown at the
// next VBlank
static void build_text(uint32_t from, uint32_t to)
{
	uint32_t next = m.oam_next;
	for (uint32_t k = from; k < to; k++) {
		const struct slot *sl = &m.slots[k];
		m.oam_next = sl->oam;
		for (uint32_t j = 0; j < sl->max; j++)
			obj(m.x0 + TEXT_INSET + (int)j * 32, sl->y, SHAPE_WIDE, 2, sl->tile + j * 8, BANK_TEXT, A0_HIDE,
			    0);
		if (sl->check)
			obj(m.x0 + CHECK_X, sl->y + 4, SHAPE_SQUARE, 0, TILE_CHECK, BANK_TITLE, A0_HIDE, 0);
	}
	m.oam_next = next;
	tiles_need(to < m.title + m.visible + m.footer ? m.slots[to].tile : m.tiles_end);
	m.text_shown = 1;
	for (uint32_t k = from; k < to; k++)
		slot_update(k);
}

// Waits until no key in mask is held
static void wait_release(uint32_t mask)
{
	while (keys_held() & mask)
		vsync();
}

static void marquee_start(void);

// Logs that page is drawn, as "screen ready: ingame-cheats" (the goldens
// wait for it): one prefix, and the pages' ends in enum page_id's order
static void screen_ready(enum page_id page)
{
	static const char ends[] = "\0-cheats\0-save\0-load\0-search\0-search-list\0-search-pick\0-search-pointers";
	const char *end = ends, *head = "screen ready: ingame";
	for (uint32_t i = 0; i < page; i++)
		while (*end++)
			;
	char line[40];
	uint32_t n = 0;
	while (*head)
		line[n++] = *head++;
	while (*end)
		line[n++] = *end++;
	line[n] = 0;
	debug_print(line);
}

// Opens page at row cursor: the panel this frame, its text the next
static void page_open(enum page_id page, uint32_t cursor)
{
	vsync();
	m.mq = MQ_OFF;
	m.notice = 0;
	m.soft = 0;
	m.confirm = CONFIRM_NONE;
	m.menu_notice = 0;
	m.edit = 0;
	build_page(page, cursor);
	build_text(0, m.title + m.visible + m.footer);
	marquee_start();
	if (page != PAGE_MENU)  // the menu's callers log it once its notices are in
		screen_ready(page);
}

// The cursor's row's slot, or -1 when it isn't shown
static int cursor_slot(void)
{
	return m.cursor >= m.top && m.cursor < m.top + m.visible ? (int)(m.title + m.cursor - m.top) : -1;
}

// The marquee off; the slot that shows a step of it (to draw again), or -1
static int marquee_stop(void)
{
	int k = m.mq == MQ_RUN || m.mq == MQ_END ? cursor_slot() : -1;
	m.mq = MQ_OFF;
	return k;
}

// The marquee from its start for the cursor's row: it waits if the row's
// cheat's name is cut, else stays off
static void marquee_start(void)
{
	m.mq = MQ_OFF;
	m.mq_px = 0;
	m.mq_wait = MQ_DELAY;
	if (m.page == PAGE_CHEATS && m.cursor < m.cheats && (m.cheat[m.cursor].flags & INGAME_CHEAT_CUT))
		m.mq = MQ_WAIT;
}

// A frame of the marquee (the cheat list's: the extension's; called only
// while it runs)
EXT static void marquee_tick(void)
{
	if (!m.text_shown)
		return;
	if (m.mq_wait) {
		m.mq_wait--;
		return;
	}
	int k = cursor_slot();
	if (k < 0)
		return;
	if (m.mq == MQ_END) {  // back to the cut line, which waits again
		m.mq = MQ_WAIT;
		m.mq_px = 0;
		m.mq_wait = MQ_DELAY;
		slot_update((uint32_t)k);
		return;
	}
	if (m.mq == MQ_WAIT) {
		uint32_t head = 0;
		sram_in(INGAME_SRAM_WIDE + m.cursor * sizeof(struct ingame_wide), &head, 4);
		m.mq_width = head & 0xFFFF;
		if (m.mq_width <= INGAME_ROW_W || m.mq_width > INGAME_WIDE_W) {
			m.mq = MQ_OFF;
			return;
		}
		m.mq = MQ_RUN;
		m.mq_px = 0;
	} else {
		m.mq_px++;
	}
	if (m.mq_px + INGAME_ROW_W >= m.mq_width + MQ_GAP) {
		m.mq = MQ_END;
		m.mq_wait = MQ_PAUSE;
	}
	slot_update((uint32_t)k);
}

// Scrolls the rows to top. Drawing every row's line takes longer than a
// VBlank, and the footer, drawn last, showed half drawn: a step of less
// than a page moves the rows' slots (sprites and tiles) with their lines
// and draws only the rows that come in; a longer one draws them all.
// Either way a row's sprites are hidden until its line is drawn, so it
// shows a frame late at worst, never half drawn. An extension's: without it
// (it didn't load) every row is drawn again.
EXT static void rows_scroll(uint32_t top)
{
	uint32_t n = m.visible, down = top > m.top, step = down ? top - m.top : m.top - top;
	struct slot *row = &m.slots[m.title], was[LIST_ROWS];
	m.top = top;
	if (step < n && n <= LIST_ROWS) {  // only the lists scroll
		// Each place takes the slot of the row now in it, keeping its y;
		// the rows that left give theirs to the rows that come in
		memcpy(was, row, n * sizeof(*row));
		for (uint32_t j = 0; j < n; j++) {
			row[j] = was[(j + (down ? step : n - step)) % n];
			row[j].y = was[j].y;
		}
	}
	for (uint32_t j = 0; j < n; j++) {
		const struct slot *sl = &row[j];
		int in = step >= n || (down ? j >= n - step : j < step);
		for (uint32_t k = 0; k < sl->max; k++) {
			obj_y(sl->oam + k, sl->y);
			if (in)
				obj_shown(sl->oam + k, 0);
		}
		if (sl->check) {
			obj_y(sl->check - 1u, sl->y + 4);
			if (in)
				obj_shown(sl->check - 1u, 0);
		}
	}
	for (uint32_t j = 0; j < n; j++)
		if (step >= n || (down ? j >= n - step : j < step))
			slot_update(m.title + j);
}

// Moves the cursor to row r, scrolling the list to show it
static void cursor_to(uint32_t r)
{
	// The menu's footer changes with the row on some rows
	uint32_t footer = m.page == PAGE_MENU ? menu_footer() : 0;
	int was = marquee_stop();
	if (was >= 0)
		slot_update((uint32_t)was);  // the row it left, cut again
	if (m.soft || m.confirm) {  // a confirmation asked (and its notice) goes with the row
		m.notice = 0;
		m.soft = 0;
		m.confirm = CONFIRM_NONE;
		footer = 0;
	}
	m.cursor = r;
	uint32_t top = m.top;
	if (r < top)
		top = r;
	else if (r >= top + m.visible)
		top = r - m.visible + 1;
	// The footer first: it is lowest on screen, so drawn first it is done
	// before the display reaches it
	if (m.footer && (m.page != PAGE_MENU || menu_footer() != footer))
		slot_update(m.title + m.visible);
	if (top != m.top) {
		if (m.ext) {
			rows_scroll(top);
		} else {
			m.top = top;
			slots_update();
		}
		if (m.bar_oam)
			bar_place();
		m.pill_y = row_y(r);  // the pill jumps with the scroll
		pill_place(m.pill_y);
	}
	m.pill_to = row_y(r);
	marquee_start();
}

// A on the cheat list's row r (the extension's)
EXT static void list_pick(uint32_t r)
{
	if (r >= m.cheats) {
		for (uint32_t e = 0; e < m.cheats; e++)
			if (cheat_why(e) == INGAME_WHY_NONE && (m.cheat[e].flags & INGAME_CHEAT_TABLE))
				bit_set(m.on, e, r == m.cheats + LIST_AS_LAUNCHED && bit_of(m.launched, e));
		found_all_off();  // the search's found cheats weren't picked at launch either
	} else {
		uint32_t e = r;
		if (cheat_why_now(e) != INGAME_WHY_NONE || !(m.cheat[e].flags & INGAME_CHEAT_TABLE))
			return;
		bit_set(m.on, e, !bit_of(m.on, e));
	}
	if (m.once && !cheat_once_ok(m.once - 1))
		m.once = 0;  // on now: it runs anyway
	cheats_store();
	// The footer first (see cursor_to), then the title's count and the
	// rows, greyed with what is on: their lines are as they were
	slot_update(m.title + m.visible);
	for (uint32_t k = 0; k < m.title + m.visible; k++)
		slot_update(k);
}

// --- The cheat search (v0.9) -------------------------------------------------

// Most of the search is an extension (EXT, above).
// The ARM code run from IWRAM (the screenshot's packer, the search's
// compare), after the extension in the image: iwram_borrow reads it from
// cart SRAM into the IWRAM it borrows
extern uint32_t __ov_fastext_start[], __ov_fastext_end[];
extern const uint32_t __ov_fastext_load[];

#define LIST_GAP 8  // pixels between a candidate's address and its value
#define CHUNK 256  // bytes of RAM a step of a pass reads, compares and writes back
#define NOT_MARKED 99  // number_put: no digit underlined

// A pass's buffers, in the game's IWRAM the screenshot's packing borrows
// (kept in cart SRAM at INGAME_BACKUP_SHOT meanwhile), before the ARM code
// it runs there (cheatsearch_narrow, ov.ld's .fast): from the window, in
// Thumb, the first pass took 190 frames
struct search_scratch {
	uint32_t now[CHUNK / 4], before[CHUNK / 4], work[CHUNK / 4], bits[CHUNK / 8 / 4];
	uint32_t vals[CHUNK / 4 + 1];  // the first scan's values a chunk's candidates read, from a word
	uint32_t out[CHUNK / 4];  // the first scan's values the first filter keeps, as written
	uint32_t pre[CHUNK / 8 / 4];  // the chunk's candidates before the pass
	uint32_t undo[CHUNK / 4];  // their values the pass writes over, packed
	uint32_t stream[CHUNK / 4];  // Undo's values on their way to cart SRAM
};
_Static_assert(sizeof(struct search_scratch) <= 0xA00, "the scratch before .fast's code");
static uint32_t iwram_borrowed;  // bytes of IWRAM from its start a pass borrows, 0 between passes
// The game's EWRAM under the extension back from its backup
static void ext_unload(void)
{
	if (m.ext)
		sram_in(INGAME_BACKUP_EWRAM + ((uint32_t)(uintptr_t)__ov_ext_start - INGAME_EWRAM), __ov_ext_start,
			m.ext);
	m.ext = 0;
}

// The end of the game's EWRAM the overlay has in cart SRAM: the window, and
// the extension while it is loaded
static uint32_t window_end(void)
{
	return INGAME_EWRAM + ov_header.window_bytes + m.ext;
}

EXS_DATA static uint32_t search_found[INGAME_SEARCH_LIST];  // the list's candidates' addresses
EXS_DATA static uint32_t search_buf[CHUNK / 4];  // the list's and Undo's reads of the candidates
// The pointers page's rows; Undo's values from cart SRAM while it puts the
// snapshot back (the page reads its rows again as it opens)
EXS_DATA static struct ingame_pointer ptr_list[INGAME_SEARCH_LIST];
_Static_assert(sizeof(ptr_list) == CHUNK, "Undo reads its values a chunk at a time");
// The log's lines, out of the window
EXS_DATA static char log_pass[] = "search: pass in ", log_frames[] = " frames",
		     log_search[] = "search: ", log_left[] = " left", log_started[] = "search: started",
		     log_kept[] = "search: kept a cheat", log_kept_on[] = "search: kept a cheat, on",
		     log_undone[] = "search: undone, ", log_ptr_found[] = "search: pointers found: ",
		     log_ptr_checked[] = "search: pointers checked: ", log_ptrs[] = "",
		     log_kept_ptr[] = "search: kept a cheat through a pointer",
		     log_undo_values[] = "search: undo: snapshot back",
		     log_undo_stale[] = "search: undo: snapshot stale", log_failed[] = "search: IWRAM backup failed",
		     log_ptr_none_pass[] = "search: none passed";
EXS_DATA static struct ingame_line spare;  // a line's second part, off the stack (the window's stack is small)
EXS_DATA static struct ingame_glyph glyphs[INGAME_GLYPHS];  // read as the search opens

static int searching(void)
{
	return m.search.magic == INGAME_SEARCH_MAGIC;
}

EXS static uint32_t search_bytes(void)
{
	return m.search.magic ? m.search.size : 1u << m.search_size;
}

// The line of text t (an INGAME_TEXT_*) from the session block
EXS static void line_read(uint32_t t, struct ingame_line *l)
{
	if (m.have_session)
		sram_in(text_at(t), l, sizeof(*l));
	else
		memset(l, 0, sizeof(*l));
}

// src's pixels ORed into l from pixel x on
EXS static void line_or(struct ingame_line *l, const struct ingame_line *src, uint32_t x)
{
	uint32_t bytes = ((uint32_t)src->width + 7) / 8;
	for (uint32_t row = 0; row < INGAME_TEXT_ROWS; row++)
		for (uint32_t c = 0; c < bytes && c < INGAME_TEXT_W / 8; c++)
			row_or(l, row, x + c * 8, src->bits[row][c]);
	width_to(l, x + src->width);
}

// Glyph g at x, underlined if mark; the x after it
EXS static uint32_t glyph_put(struct ingame_line *l, uint32_t x, uint32_t g, int mark)
{
	const struct ingame_glyph *gl = &glyphs[g];
	uint32_t w = gl->width <= 8 ? gl->width : 8;
	for (uint32_t row = 0; row < INGAME_TEXT_ROWS; row++)
		row_or(l, row, x, gl->rows[row]);
	if (mark && w > 1)
		row_or(l, INGAME_TEXT_ROWS - 1, x, (1u << (w - 1)) - 1);
	width_to(l, x + w);
	return x + w;
}

// value in decimal from x: n digits padded with zeros (0: as many as it
// takes), the thousands separator between groups when sep, the digit at
// mark (from the left) underlined; the x after it
EXS static uint32_t number_put(struct ingame_line *l, uint32_t x, uint32_t value, uint32_t n, int sep, uint32_t mark)
{
	uint8_t d[10];
	cheatsearch_decimal(value, d, 10);
	uint32_t first = 10 - n;
	if (!n)
		for (first = 0; first < 9 && !d[first]; first++)
			;
	for (uint32_t i = first; i < 10; i++) {
		x = glyph_put(l, x, d[i], i - first == mark);
		if (sep && i < 9 && (9 - i) % 3 == 0)
			x = glyph_put(l, x, INGAME_GLYPH_THOUSANDS, 0);
	}
	return x;
}

EXS static uint32_t hex_put(struct ingame_line *l, uint32_t x, uint32_t value)
{
	for (int sh = 28; sh >= 0; sh -= 4)
		x = glyph_put(l, x, value >> sh & 15, 0);
	return x;
}

// value of size bytes from x, signed (a minus before a negative) when sign;
// edited (editing), all its places with mark underlined: a signed one's
// sign first, plus or minus
EXS static uint32_t value_put(struct ingame_line *l, uint32_t x, uint32_t value, uint32_t size, uint32_t mode,
			      int editing, uint32_t mark)
{
	uint32_t n = editing ? cheatsearch_digits(size, mode) : 0;
	value &= cheatsearch_max(size);
	if (mode != CHEATSEARCH_SIGNED)
		return number_put(l, x, value, n, 0, editing ? mark : NOT_MARKED);
	int32_t v = cheatsearch_signed(value, size);
	if (v < 0 || editing)
		x = glyph_put(l, x, v < 0 ? INGAME_GLYPH_MINUS : INGAME_GLYPH_PLUS, editing && mark == 0);
	return number_put(l, x, v < 0 ? 0u - (uint32_t)v : (uint32_t)v, n, 0, editing ? mark - 1 : NOT_MARKED);
}

// How the search reads numbers now (enum cheatsearch_mode)
EXS static uint32_t search_mode(void)
{
	return m.search.bcd ? CHEATSEARCH_BCD : m.search.sign ? CHEATSEARCH_SIGNED : CHEATSEARCH_UNSIGNED;
}

// The mode a filter's number is edited in: By's an amount, never signed
EXS static uint32_t number_mode(uint32_t op)
{
	uint32_t mode = search_mode();
	return op < CHEATSEARCH_EQUALS && mode == CHEATSEARCH_SIGNED ? CHEATSEARCH_UNSIGNED : mode;
}

// An element as the search reads it, as a number from x: BCD's digits, or
// the bytes in hex where they aren't BCD
EXS static uint32_t element_put(struct ingame_line *l, uint32_t x, uint32_t raw, uint32_t size)
{
	uint32_t v;
	if (!m.search.bcd)
		return value_put(l, x, raw, size, search_mode(), 0, NOT_MARKED);
	if (cheatsearch_from_bcd(raw, size, &v))
		return number_put(l, x, v, 0, 0, NOT_MARKED);
	for (int sh = 8 * (int)size - 4; sh >= 0; sh -= 4)
		x = glyph_put(l, x, raw >> sh & 15, 0);
	return x;
}  // Text t's line with a number in its place: t's text, the number, then
// t + 1's (the text after it)
EXS static void line_number(struct ingame_line *l, uint32_t t, uint32_t value, uint32_t n, int sep, uint32_t mark)
{
	line_read(t, l);
	uint32_t x = number_put(l, l->width, value, n, sep, mark);
	line_read(t + 1, &spare);
	if (spare.width)
		line_or(l, &spare, x);
}

// A filter's row with its number (Between's two), as edited when editing
EXS static void line_filter(struct ingame_line *l, uint32_t op, int editing)
{
	const uint32_t size = m.search.size, t = op_text[op];
	const uint32_t mode = number_mode(op);
	const int lo = editing && m.edit_stage == 0, hi = editing && m.edit_stage == 1;
	line_read(t, l);
	uint32_t x = value_put(l, l->width, lo ? m.edit_value : m.search.value[op], size, mode, lo, m.edit_place);
	if (op == CHEATSEARCH_BETWEEN) {
		line_read(t + 1, &spare);
		line_or(l, &spare, x);
		x = value_put(l, l->width, hi ? m.edit_value : m.search.value_hi, size, mode, hi, m.edit_place);
		line_read(t + 2, &spare);
	} else {
		line_read(t + 1, &spare);
	}
	if (spare.width)
		line_or(l, &spare, x);
}
// The game's value of size bytes at addr: the window's from the stub's
// backup of it, which the stub puts back
EXS static uint32_t value_read(uint32_t addr, uint32_t size)
{
	uint32_t word;
	if (addr - INGAME_EWRAM < window_end() - INGAME_EWRAM)
		sram_in(INGAME_BACKUP_EWRAM + ((addr - INGAME_EWRAM) & ~3u), &word, 4);
	else
		word = *(const volatile uint32_t *)(addr & ~3u);
	return word >> (addr & 3) * 8 & cheatsearch_max(size);
}

EXS static void value_write(uint32_t addr, uint32_t size, uint32_t value)
{
	if (addr - INGAME_EWRAM < window_end() - INGAME_EWRAM) {
		uint32_t at = INGAME_BACKUP_EWRAM + ((addr - INGAME_EWRAM) & ~3u), word, sh = (addr & 3) * 8;
		uint32_t mask = cheatsearch_max(size) << sh;
		sram_in(at, &word, 4);
		word = (word & ~mask) | (value << sh & mask);
		sram_out(at, &word, 4);
	} else if (size == 1) {
		*(volatile uint8_t *)addr = (uint8_t)value;
	} else if (size == 2) {
		*(volatile uint16_t *)addr = (uint16_t)value;
	} else {
		*(volatile uint32_t *)addr = value;
	}
}

// n bytes of the searched run from at into buf: the window's, and the IWRAM
// a pass borrows, from their backups
EXS static void ram_read(uint32_t at, uint32_t *buf, uint32_t n)
{
	uint32_t addr = cheatsearch_address(at), i = 0, end = window_end();
	if (addr < end) {
		i = end - addr < n ? end - addr : n;
		sram_in(INGAME_BACKUP_EWRAM + (addr - INGAME_EWRAM), buf, i);
	}
	end = INGAME_SHOT_IWRAM + iwram_borrowed;
	if (addr >= INGAME_SHOT_IWRAM && addr < end) {
		i = end - addr < n ? end - addr : n;
		sram_in(INGAME_BACKUP_SHOT + (addr - INGAME_SHOT_IWRAM), buf, i);
	}
	const volatile uint32_t *src = (const volatile uint32_t *)addr;
	for (; i < n; i += 4)
		buf[i / 4] = src[i / 4];
}

// The live block's words, all 0 unless its check matches
EXS static void live_read(uint32_t *w)
{
	const volatile uint32_t *b = (const volatile uint32_t *)m.live;
	for (uint32_t i = 0; i < 1 + 2 * INGAME_LIVE_PAIRS; i++)
		w[i] = b[i];
	if (ingame_live_check(&w[1]) != w[0])
		memset(w, 0, (1 + 2 * INGAME_LIVE_PAIRS) * 4);
}

// The live pair addr takes: its own, else a free one; INGAME_LIVE_PAIRS: none
EXS static uint32_t live_pair(const uint32_t *w, uint32_t addr)
{
	uint32_t free = INGAME_LIVE_PAIRS;
	for (uint32_t j = 0; j < INGAME_LIVE_PAIRS; j++) {
		uint32_t a = w[1 + 2 * j] & 0x0FFFFFFFu;
		if (a == addr)
			return j;
		if (!a && free == INGAME_LIVE_PAIRS)
			free = j;
	}
	return free;
}

EXS static int live_full(void)
{
	uint32_t w[1 + 2 * INGAME_LIVE_PAIRS];
	if (!m.live || m.list_n <= m.pick)
		return 0;
	live_read(w);
	return live_pair(w, search_found[m.pick]) == INGAME_LIVE_PAIRS;
}

EXS static void live_add(uint32_t addr, uint32_t size, uint32_t value)
{
	uint32_t w[1 + 2 * INGAME_LIVE_PAIRS];
	live_read(w);
	uint32_t j = live_pair(w, addr);
	if (j == INGAME_LIVE_PAIRS)
		return;
	w[1 + 2 * j] = addr | (size == 1 ? 0u : size == 2 ? 1u : 2u) << 28;
	w[2 + 2 * j] = value;
	w[0] = ingame_live_check(&w[1]);
	volatile uint32_t *b = (volatile uint32_t *)m.live;
	for (uint32_t i = 0; i < 1 + 2 * INGAME_LIVE_PAIRS; i++)
		b[i] = w[i];
	m.found_live = 1;
	found_head();
}

EXS static void debug_number(const char *text, uint32_t n, const char *after)
{
	char line[64];
	uint8_t d[10];
	uint32_t at = 0, i = 0;
	while (text[i] && at < 30)
		line[at++] = text[i++];
	cheatsearch_decimal(n, d, 10);
	for (i = 0; i < 9 && !d[i]; i++)
		;
	for (; i < 10; i++)
		line[at++] = (char)('0' + d[i]);
	for (i = 0; after[i] && at < sizeof(line) - 1; i++)
		line[at++] = after[i];
	line[at] = 0;
	debug_print(line);
}

// The search's head, the found cheats and the live block, at opening
static void search_load(void)
{
	sram_in(INGAME_SRAM_SEARCH, &m.search, sizeof(m.search));
	// The size Start takes: this launch's last search's
	if (m.have_session && m.search.launch_id == session()->launch_id)
		m.search_size = m.search.size == 4 ? 2 : m.search.size == 2 ? 1 : 0;
	// A search of this launch's, which no state saved since has overwritten
	if (m.search.magic != INGAME_SEARCH_MAGIC || !m.have_session || m.search.launch_id != session()->launch_id ||
	    (m.search.size != 1 && m.search.size != 2 && m.search.size != 4) || state_saved())
		m.search.magic = 0;
	uint32_t head[2];
	sram_in(INGAME_SRAM_FOUND, head, sizeof(head));
	m.found = head[0] == INGAME_FOUND_MAGIC && (head[1] & 0xFFFF) <= INGAME_FOUND_MAX ? head[1] & 0xFFFF : 0;
	m.found_live = m.found && (head[1] >> 16 & INGAME_FOUND_LIVE);
	uint32_t ptrs[4];
	sram_in(INGAME_SRAM_POINTERS, ptrs, sizeof(ptrs));  // the pointer scan's, for Check
	m.ptr_count =
		ptrs[0] == INGAME_POINTERS_MAGIC && (ptrs[1] & 0xFFFF) <= INGAME_POINTERS_MAX ? ptrs[1] & 0xFFFF : 0;
	m.ptr_full = m.ptr_count && (ptrs[1] >> 16);
	m.ptr_passed = m.ptr_count ? ptrs[3] & 0xFFFF : 0;
	m.ptr_checks = m.ptr_count ? ptrs[3] >> 16 : 0;
	uint32_t live = m.have_session ? session()->live_at : 0;
	m.live =
		(m.flags & INGAME_CAN_CHEATS) && live && live == (uint32_t)(uintptr_t)m.call->spend + INGAME_SPEND_BYTES
			? live
			: 0;
}

static void search_save(void)
{
	sram_out(INGAME_SRAM_SEARCH, &m.search, sizeof(m.search));
}

// The game's IWRAM a search's pass or a screenshot borrows for its buffers
// and the ARM code (the packer's and the compare's, ov.ld's .fastext, read
// from the image in cart SRAM), into its backup, read back, and the code
// in (on: 0 if the backup didn't read back, IWRAM as it was), or back (off)
EXT static int iwram_borrow(int on)
{
	const uint32_t borrowed = (uint32_t)(uintptr_t)__ov_fastext_end - INGAME_SHOT_IWRAM;
	if (on) {
		if (!sram_keep(INGAME_BACKUP_SHOT, (const void *)INGAME_SHOT_IWRAM, borrowed))
			return 0;
		sram_in(INGAME_SRAM_EXT +
				((uint32_t)(uintptr_t)__ov_fastext_load - (uint32_t)(uintptr_t)__ov_image_end),
			__ov_fastext_start, (uint32_t)((uintptr_t)__ov_fastext_end - (uintptr_t)__ov_fastext_start));
		iwram_borrowed = borrowed;
	} else {
		sram_in(INGAME_BACKUP_SHOT, (void *)INGAME_SHOT_IWRAM, iwram_borrowed);
		iwram_borrowed = 0;
	}
	return 1;
}

// The candidates' bits from one place in cart SRAM to another (whole words)
EXS static void bits_copy(uint32_t from, uint32_t to)
{
	const uint32_t total = (cheatsearch_bits_bytes(m.search.size) + 3) & ~3u;
	for (uint32_t at = 0; at < total; at += CHUNK) {
		uint32_t n = total - at < CHUNK ? total - at : CHUNK;
		sram_in(from + at, search_buf, n);
		sram_out(to + at, search_buf, n);
	}
}

// The first scan's values of the chunk at at's candidates into the
// scratch's before, from the kept ones at *fptr: the first filter's
// candidates (INGAME_SRAM_SEARCH_BASE), each one's value after the last's.
// *fptr goes past the chunk's whether it has candidates now (any) or not.
EXS static void first_chunk(uint32_t at, uint32_t any, uint32_t *fptr)
{
	struct search_scratch *k = (struct search_scratch *)INGAME_SHOT_IWRAM;
	const uint32_t size = m.search.size, bits = CHUNK / size / 8;
	uint32_t base[CHUNK / 8 / 4];
	sram_in(INGAME_SRAM_SEARCH_BASE + at / size / 8, base, bits);
	uint32_t n = cheatsearch_count((const uint8_t *)base, bits) * size;
	if (any && n) {
		uint32_t from = INGAME_SRAM_SEARCH_FIRST + *fptr, skew = from & 3;
		sram_in(from - skew, k->vals, (n + skew + 3) & ~3u);
		const uint8_t *v = (const uint8_t *)k->vals + skew, *b = (const uint8_t *)base;
		uint8_t *to = (uint8_t *)k->before;
		for (uint32_t e = 0; e < CHUNK / size; e++)
			if (b[e >> 3] >> (e & 7) & 1) {
				for (uint32_t i = 0; i < size; i++)
					to[e * size + i] = v[i];
				v += size;
			}
	}
	*fptr += n;
}

// The first scan's values (the snapshot's, in the scratch's before) of the
// chunk's candidates the first filter left, after those before them: 0
// once they don't fit
EXS static int first_keep(uint32_t *wpos, uint32_t *wn)
{
	struct search_scratch *k = (struct search_scratch *)INGAME_SHOT_IWRAM;
	const uint32_t size = m.search.size;
	const uint8_t *b = (const uint8_t *)k->bits, *v = (const uint8_t *)k->before;
	uint8_t *out = (uint8_t *)k->out;
	uint32_t pos = *wpos, nb = *wn;
	for (uint32_t i = 0; i < CHUNK / size / 8; i++) {  // most bytes of bits are 0 after the first filter
		for (uint32_t bb = b[i], l = 0; bb; bb >>= 1, l++) {
			if (!(bb & 1))
				continue;
			if (pos + nb + size > INGAME_SEARCH_FIRST_BYTES)
				return 0;
			const uint8_t *from = v + (i * 8 + l) * size;
			for (uint32_t q = 0; q < size; q++)
				out[nb++] = from[q];
			if (nb == CHUNK) {
				sram_out(INGAME_SRAM_SEARCH_FIRST + pos, k->out, CHUNK);
				pos += CHUNK;
				nb = 0;
			}
		}
	}
	*wpos = pos;
	*wn = nb;
	return 1;
}

// The words of the searched run that are Torii's in game (v0.9.22): the
// hook's spend area (and the live block after it), which a game whose
// stack top is in EWRAM has in the run (GBApatch.c's Get_spend_address),
// and the IRQ stack's frames under the menu, wherever the game put that
// stack. A candidate there would be the hook's saved registers.
EXS static int owned(uint32_t addr, uint32_t size)
{
	const uint32_t lo = (uint32_t)(uintptr_t)m.call->spend,
		       hi = lo + INGAME_SPEND_BYTES + (m.live ? INGAME_LIVE_BYTES : 0),
		       sp = m.call->spend[INGAME_SPEND_IRQ_SP / 4];
	return (addr < hi && addr + size > lo) || (sp > 0x100 && addr < sp + 0x20 && addr + size > sp - 0x100);
}

// Whether size bytes at addr are all in EWRAM or IWRAM (what a cheat writes)
EXS static int in_ram(uint32_t addr, uint32_t size)
{
	return (addr >= 0x02000000 && addr + size <= 0x02040000) || (addr >= 0x03000000 && addr + size <= 0x03008000);
}

// Drops from the chunk of the run at at (its bits) the candidates owned()
// covers; how many it dropped
EXS static uint32_t owned_drop(uint32_t at, uint32_t *bits)
{
	const uint32_t size = m.search.size, addr = cheatsearch_address(at);
	uint8_t *b = (uint8_t *)bits;
	uint32_t dropped = 0;
	if (!owned(addr, CHUNK))
		return 0;
	for (uint32_t e = 0; e < CHUNK / size; e++)
		if ((b[e >> 3] >> (e & 7) & 1) && owned(addr + e * size, size)) {
			b[e >> 3] &= (uint8_t)~(1u << (e & 7));
			dropped++;
		}
	return dropped;
}

// Undo's values (v0.9.22): n bytes after those before at *pos (the scratch's
// stream holding *held of them), to cart SRAM a chunk at a time; 0 once
// they don't fit INGAME_UNDO_VALUES_BYTES
EXS static int undo_put(const uint8_t *v, uint32_t n, uint32_t *pos, uint32_t *held)
{
	struct search_scratch *k = (struct search_scratch *)INGAME_SHOT_IWRAM;
	uint8_t *out = (uint8_t *)k->stream;
	if (*pos + *held + n > INGAME_UNDO_VALUES_BYTES)
		return 0;
	for (uint32_t i = 0; i < n; i++) {
		out[(*held)++] = v[i];
		if (*held == CHUNK) {
			sram_out(INGAME_SRAM_UNDO_VALUES + *pos, k->stream, CHUNK);
			*pos += CHUNK;
			*held = 0;
		}
	}
	return 1;
}

// A pass over the searched run, in the IWRAM borrowed (iwram_borrow):
// start remembers it all, every element a candidate but the words that
// are Torii's; else op narrows the candidates. Chunks without candidates
// are skipped. A chunk whose snapshot the pass writes over keeps its
// candidates' values from before in INGAME_SRAM_UNDO_VALUES, for Undo, as
// many as fit. VBlanks are counted for the log.
EXS static void search_pass(int start, uint32_t op)
{
	uint32_t size = m.search.size, bits = CHUNK / size / 8, left = 0, frames = 0, line = REG_VCOUNT;
	struct search_scratch *k = (struct search_scratch *)INGAME_SHOT_IWRAM;
	uint32_t *chunk_now = k->now, *chunk_before = k->before, *chunk_bits = k->bits;
	// The earlier values: the snapshot's (the last scan's, and the first's
	// before any filter), or the first scan's kept ones; the first filter
	// keeps its candidates' first values as it goes
	const int relative = cheatsearch_relative(op), keep_first = !start && m.search.first == INGAME_FIRST_SNAPSHOT;
	const int use_first = relative && m.search.against && m.search.first == INGAME_FIRST_KEPT;
	uint32_t fptr = 0, wpos = 0, wn = 0, lost = 0, upos = 0, uheld = 0, uok = !start;
	for (uint32_t at = 0; at < CHEATSEARCH_BYTES; at += CHUNK) {
		uint32_t bits_at = INGAME_SRAM_SEARCH_BITS + at / size / 8, now = REG_VCOUNT;
		int have_snap = 0;  // chunk_before holds the snapshot's chunk
		frames += now < line;
		line = now;
		if (start) {
			memset(chunk_bits, 0xFF, bits);
			left += CHUNK / size;
		} else {
			sram_in(bits_at, chunk_bits, bits);
			uint32_t any = 0;
			for (uint32_t i = 0; i < bits / 4; i++)
				any |= k->pre[i] = chunk_bits[i];
			if (use_first)
				first_chunk(at, any, &fptr);
			if (!any)
				continue;
			if (!use_first && relative) {
				sram_in(INGAME_SRAM_SEARCH_SNAP + at, chunk_before, CHUNK);
				have_snap = 1;
			}
		}
		ram_read(at, chunk_now, CHUNK);
		uint32_t kept = CHUNK / size;
		if (!start)
			kept = cheatsearch_chunk((const uint8_t *)chunk_now, (const uint8_t *)chunk_before,
						 (uint8_t *)k->work, (uint8_t *)chunk_bits, CHUNK, size, op,
						 search_mode(), m.search.value[op], m.search.value_hi);
		uint32_t dropped = owned_drop(at, chunk_bits);
		kept -= dropped;
		left -= start ? dropped : 0;
		if (!start) {
			left += kept;
			if (keep_first && !lost && kept) {  // the first scan's values: the snapshot's
				if (!have_snap)
					sram_in(INGAME_SRAM_SEARCH_SNAP + at, chunk_before, CHUNK);
				have_snap = 1;
				lost = !first_keep(&wpos, &wn);
			}
			if (kept && uok) {  // the snapshot's values written over below, for Undo
				const uint32_t *snap = chunk_before;
				if (!have_snap) {
					sram_in(INGAME_SRAM_SEARCH_SNAP + at, k->work, CHUNK);
					snap = k->work;
				}
				uint32_t n = cheatsearch_pack((uint8_t *)k->undo, (const uint8_t *)snap,
							      (const uint8_t *)k->pre, CHUNK, size);
				uok = undo_put((const uint8_t *)k->undo, n, &upos, &uheld);
			}
		}
		// The snapshot only where candidates are left: the rest is never read again
		if (kept)
			sram_out(INGAME_SRAM_SEARCH_SNAP + at, chunk_now, CHUNK);
		sram_out(bits_at, chunk_bits, bits);
	}
	if (keep_first && !lost && wn)
		sram_out(INGAME_SRAM_SEARCH_FIRST + wpos, k->out, (wn + 3) & ~3u);
	if (uok && uheld)
		sram_out(INGAME_SRAM_UNDO_VALUES + upos, k->stream, (uheld + 3) & ~3u);
	if (keep_first) {
		if (!lost)
			bits_copy(INGAME_SRAM_SEARCH_BITS, INGAME_SRAM_SEARCH_BASE);
		m.search.first = lost ? INGAME_FIRST_LOST : INGAME_FIRST_KEPT;
		m.search.first_pass = (uint8_t)(m.search.passes + 1);
	}
	m.search.undo_values = (uint8_t)uok;
	m.search.stale = 0;  // the snapshot is the values now wherever candidates are left
	m.search.left = left;
	debug_number(log_pass, frames, log_frames);
	debug_number(log_search, left, log_left);
}
// The footer says Searching... while a pass runs
EXS static void search_busy(int busy)
{
	m.busy = (uint32_t)busy;
	if (m.footer)
		slot_update(m.title + m.visible);
	if (busy)
		vsync();
}

// The Cheat search row while a search runs, with the candidates left
// ("Cheat search: 1,204 left"), to cart SRAM for the menu and the cheat
// list (which draw it without the extension's numbers)
EXS static void search_row_write(void)
{
	struct ingame_line line;
	line_number(&line, INGAME_TEXT_SEARCH_ROW, m.search.left, 0, 1, NOT_MARKED);
	sram_out(INGAME_SRAM_SEARCH_ROW, &line, sizeof(line));
}

// Cart SRAM didn't keep the game's IWRAM a pass borrows: nothing searched
EXS static void search_failed(void)
{
	m.busy = 0;
	m.notice = INGAME_TEXT_SEARCH_FAILED;
	debug_print(log_failed);
	slots_update();
}

// Start: the save-state buffer's state let go (Load state has nothing in
// cart SRAM then), the RAM remembered, the head with its magic last. 0 if
// cart SRAM didn't keep the IWRAM the pass borrows (nothing changed)
EXS static int search_start(void)
{
	uint32_t zeros[4] = {0, 0, 0, 0};
	search_busy(1);
	if (!iwram_borrow(1)) {
		search_failed();
		return 0;
	}
	m.search.magic = 0;
	search_save();
	sram_out(INGAME_SRAM_STATE_MARK, zeros, sizeof(zeros));
	sram_out(INGAME_SRAM_STATE_TAG, zeros, sizeof(zeros));
	m.buffer_slot = 0;
	m.search.size = (uint8_t)(1u << m.search_size);
	m.search.passes = 0;
	memset(m.search.value, 0, sizeof(m.search.value));
	m.search.value[CHEATSEARCH_UP_BY] = m.search.value[CHEATSEARCH_DOWN_BY] = 1;
	m.search.value_hi = 0;
	m.search.undo = 0;
	m.search.first = INGAME_FIRST_SNAPSHOT;
	m.search.against = 0;
	if (m.search.launch_id != session()->launch_id) {  // a new search keeps this launch's last one's
		m.search.sign = 0;
		m.search.bcd = 0;
	}
	m.search.reserved[0] = m.search.reserved[1] = m.search.reserved[2] = 0;
	m.search.launch_id = session()->launch_id;
	search_pass(1, 0);
	iwram_borrow(0);
	m.search.magic = INGAME_SEARCH_MAGIC;
	search_save();
	search_row_write();
	m.busy = 0;
	debug_print(log_started);
	return 1;
}

static void search_drop(void)
{
	m.search.magic = 0;
	search_save();
}

EXS static void search_filter(uint32_t op)
{
	search_busy(1);
	if (!iwram_borrow(1)) {
		search_failed();
		return;
	}
	// The candidates as they are, for Undo (a filter picked by mistake
	// would otherwise end the search)
	bits_copy(INGAME_SRAM_SEARCH_BITS, INGAME_SRAM_SEARCH_UNDO);
	m.search.undo = 1;
	m.search.undo_left = m.search.left;
	search_pass(0, op);
	iwram_borrow(0);
	m.search.passes++;
	search_save();
	search_row_write();
	m.busy = 0;
	slots_update();  // the rows greyed or not with what is left
}

// The snapshot's values the last pass wrote over, back (v0.9.22): in each
// chunk that had candidates before it (INGAME_SRAM_SEARCH_UNDO) and has
// some after it, the values of those before, from INGAME_SRAM_UNDO_VALUES
// in their order
EXS static void undo_snapshot(void)
{
	const uint32_t size = m.search.size, bits = CHUNK / size / 8;
	uint32_t pre[CHUNK / 8 / 4], post[CHUNK / 8 / 4], pos = 0, held = 0, used = 0;
	const uint8_t *in = (const uint8_t *)ptr_list, *b = (const uint8_t *)pre;
	uint8_t *v = (uint8_t *)search_buf;
	for (uint32_t at = 0; at < CHEATSEARCH_BYTES; at += CHUNK) {
		uint32_t any_pre = 0, any_post = 0;
		sram_in(INGAME_SRAM_SEARCH_UNDO + at / size / 8, pre, bits);
		sram_in(INGAME_SRAM_SEARCH_BITS + at / size / 8, post, bits);
		for (uint32_t i = 0; i < bits / 4; i++) {
			any_pre |= pre[i];
			any_post |= post[i];
		}
		if (!any_pre || !any_post)
			continue;
		sram_in(INGAME_SRAM_SEARCH_SNAP + at, search_buf, CHUNK);
		for (uint32_t e = 0; e < CHUNK / size; e++) {
			if (!(b[e >> 3] >> (e & 7) & 1))
				continue;
			for (uint32_t q = 0; q < size; q++) {
				if (used == held) {
					sram_in(INGAME_SRAM_UNDO_VALUES + pos, ptr_list, CHUNK);
					pos += CHUNK;
					held = CHUNK;
					used = 0;
				}
				v[e * size + q] = in[used++];
			}
		}
		sram_out(INGAME_SRAM_SEARCH_SNAP + at, search_buf, CHUNK);
	}
}

// Undo: the candidates as before the last pass, and the snapshot's values
// it wrote over, so the next filter compares as it would have then. When
// they didn't all fit, the filters against the last scan wait for one
// against a number (stale), which compares the values now.
EXS static void search_undo(void)
{
	search_busy(1);
	if (m.search.undo_values)
		undo_snapshot();
	m.search.stale = !m.search.undo_values;
	bits_copy(INGAME_SRAM_SEARCH_UNDO, INGAME_SRAM_SEARCH_BITS);
	m.search.left = m.search.undo_left;
	m.search.undo = 0;
	m.search.undo_values = 0;
	if (m.search.first == INGAME_FIRST_KEPT && m.search.passes == m.search.first_pass)
		m.search.first =
			INGAME_FIRST_LOST;  // the candidates back past the first filter's: the snapshot has its values
	m.search.passes--;
	search_save();
	search_row_write();
	m.busy = 0;
	debug_print(m.search.stale ? log_undo_stale : log_undo_values);
	debug_number(log_undone, m.search.left, log_left);
	slots_update();
}

// The candidates, all of them (at most INGAME_SEARCH_LIST), into the list
EXS static void search_list(void)
{
	uint32_t size = m.search.size, total = cheatsearch_bits_bytes(size);
	m.list_n = 0;
	for (uint32_t at = 0; at < total && m.list_n < INGAME_SEARCH_LIST; at += CHUNK) {
		uint32_t n = total - at < CHUNK ? (total - at + 3) & ~3u : CHUNK;
		const uint8_t *b = (const uint8_t *)search_buf;
		sram_in(INGAME_SRAM_SEARCH_BITS + at, search_buf, n);
		for (uint32_t i = 0; i < n && m.list_n < INGAME_SEARCH_LIST; i++)
			for (uint32_t k = 0; k < 8 && b[i] >> k; k++)
				if (b[i] >> k & 1 && ((at + i) * 8 + k) * size < CHEATSEARCH_BYTES &&
				    m.list_n < INGAME_SEARCH_LIST)
					search_found[m.list_n++] = cheatsearch_address(((at + i) * 8 + k) * size);
	}
}

// The value a kept cheat writes: the pick page's number, as BCD's digits
// when the search reads them
EXS static uint32_t keep_value(void)
{
	uint32_t size = m.search.size;
	return (m.search.bcd ? cheatsearch_to_bcd(m.pick_value, size) : m.pick_value) & cheatsearch_max(size);
}

// The found cheats' head: their count, and whether one is on now in the
// live block (the Reset game row's footer says it stops them)
EXT static void found_head(void)
{
	uint32_t head[2] = {INGAME_FOUND_MAGIC, m.found | (m.found_live ? INGAME_FOUND_LIVE << 16 : 0)};
	sram_out(INGAME_SRAM_FOUND, head, sizeof(head));
}

// A found cheat's record: in place of one kept before for the same place
// (Keep pressed again, with another value or on now), else after the
// others, then the head
EXS static void found_add(const struct ingame_found_cheat *c)
{
	uint32_t at = m.found;
	for (uint32_t i = 0; i < m.found; i++) {
		struct ingame_found_cheat o;
		sram_in(INGAME_SRAM_FOUND + offsetof(struct ingame_found, cheat) + i * sizeof(o), &o, sizeof(o));
		if (o.address == c->address && o.size == c->size && !((o.flags ^ c->flags) & INGAME_FOUND_POINTER) &&
		    o.reserved == c->reserved)
			at = i;
	}
	sram_out(INGAME_SRAM_FOUND + offsetof(struct ingame_found, cheat) + at * sizeof(*c), c, sizeof(*c));
	if (at < m.found)
		return;
	m.found++;
	found_head();
}

// The live pair of addr gone (a found cheat kept again, off)
EXS static void live_drop(uint32_t addr)
{
	uint32_t w[1 + 2 * INGAME_LIVE_PAIRS];
	if (!m.live)
		return;
	live_read(w);
	uint32_t j = live_pair(w, addr);
	if (j == INGAME_LIVE_PAIRS || (w[1 + 2 * j] & 0x0FFFFFFFu) != addr)
		return;
	w[1 + 2 * j] = w[2 + 2 * j] = 0;
	w[0] = ingame_live_check(&w[1]);
	for (uint32_t i = 0; i < 1 + 2 * INGAME_LIVE_PAIRS; i++)
		((volatile uint32_t *)m.live)[i] = w[i];
}

// All off and Back to my picks on the cheat list (v0.9.22): the found
// cheats on now stop (the live block cleared: no pair writes) and are
// filed off
EXT static void found_all_off(void)
{
	if (m.live)
		for (uint32_t i = 0; i < 1 + 2 * INGAME_LIVE_PAIRS; i++)
			((volatile uint32_t *)m.live)[i] = 0;
	m.found_live = 0;
	if (m.found)
		found_head();
	for (uint32_t i = 0; i < m.found; i++) {
		struct ingame_found_cheat o;
		uint32_t at = INGAME_SRAM_FOUND + offsetof(struct ingame_found, cheat) + i * sizeof(o);
		sram_in(at, &o, sizeof(o));
		if (o.flags & INGAME_FOUND_ON) {
			o.flags &= (uint8_t)~INGAME_FOUND_ON;
			sram_out(at, &o, sizeof(o));
		}
	}
}

// The pointer list's head (struct ingame_pointers) from m
EXS static void pointers_head(uint32_t target)
{
	uint32_t head[4] = {INGAME_POINTERS_MAGIC, m.ptr_count | m.ptr_full << 16, target,
			    m.ptr_passed | m.ptr_checks << 16};
	sram_out(INGAME_SRAM_POINTERS, head, sizeof(head));
}

// The pointers page's rows: the first INGAME_SEARCH_LIST of the list as it
// shows, those that passed every Check (once one is made) or all
EXS static void pointers_rows(void)
{
	const struct ingame_pointer *e = (const struct ingame_pointer *)search_buf;
	const int only = m.ptr_checks && !m.ptr_all;
	m.ptr_rows = 0;
	for (uint32_t i = 0; i < m.ptr_count && m.ptr_rows < INGAME_SEARCH_LIST; i += CHUNK / sizeof(*e)) {
		uint32_t k = m.ptr_count - i < CHUNK / sizeof(*e) ? m.ptr_count - i : CHUNK / sizeof(*e);
		sram_in(INGAME_SRAM_POINTERS + offsetof(struct ingame_pointers, p) + i * sizeof(*e), search_buf,
			k * sizeof(*e));
		for (uint32_t j = 0; j < k && m.ptr_rows < INGAME_SEARCH_LIST; j++)
			if (!only || (e[j].offset & INGAME_POINTER_PASSED))
				ptr_list[m.ptr_rows++] = e[j];
	}
}

// The pointer scan (v0.9.7): every word of the game's RAM that points at
// most INGAME_POINTER_REACH bytes below target (the window's and the
// extension's from their backup), with its offset, into cart SRAM (struct
// ingame_pointers), in place of the list before. Words of Torii's
// (owned) aren't the game's pointers.
EXS static void pointers_find(uint32_t target)
{
	struct ingame_pointer batch[16];
	uint32_t n = 0, full = 0, at_out = 0;
	for (uint32_t at = 0; at < CHEATSEARCH_BYTES && !full; at += CHUNK) {
		ram_read(at, search_buf, CHUNK);
		for (uint32_t i = 0; i < CHUNK / 4; i++) {
			uint32_t w = search_buf[i], region = w >> 24, addr = cheatsearch_address(at + i * 4);
			if ((region != 2 && region != 3) || w > target || target - w >= INGAME_POINTER_REACH ||
			    owned(addr, 4))
				continue;
			if (n >= INGAME_POINTERS_MAX) {
				full = 1;
				break;
			}
			batch[at_out].at = addr;
			batch[at_out].offset = target - w;
			if (++at_out == 16) {
				sram_out(INGAME_SRAM_POINTERS + offsetof(struct ingame_pointers, p) +
						 (n + 1 - at_out) * sizeof(batch[0]),
					 batch, sizeof(batch));
				at_out = 0;
			}
			n++;
		}
	}
	if (at_out)
		sram_out(INGAME_SRAM_POINTERS + offsetof(struct ingame_pointers, p) + (n - at_out) * sizeof(batch[0]),
			 batch, at_out * sizeof(batch[0]));
	m.ptr_count = n;
	m.ptr_full = full;
	m.ptr_passed = m.ptr_checks = m.ptr_all = 0;
	pointers_head(target);
	debug_number(log_ptr_found, n, log_ptrs);
}

// Check (v0.9.22): of the list's pointers (those that passed every Check
// before), those that point the same way at target now (the value found
// again after it moved) get INGAME_POINTER_PASSED, the rest lose it. None
// passing leaves the list as it was. The pointers that pass.
EXS static uint32_t pointers_check(uint32_t target)
{
	struct ingame_pointer *e = (struct ingame_pointer *)search_buf;
	const uint32_t per = CHUNK / sizeof(*e);
	uint32_t pass = 0;
	for (uint32_t round = 0; round < 2 && (!round || pass); round++)  // count, then mark
		for (uint32_t i = 0; i < m.ptr_count; i += per) {
			uint32_t k = m.ptr_count - i < per ? m.ptr_count - i : per, changed = 0;
			uint32_t at = INGAME_SRAM_POINTERS + offsetof(struct ingame_pointers, p) + i * sizeof(*e);
			sram_in(at, search_buf, k * sizeof(*e));
			for (uint32_t j = 0; j < k; j++) {
				uint32_t off = e[j].offset & ~INGAME_POINTER_PASSED;
				uint32_t ok = (!m.ptr_checks || (e[j].offset & INGAME_POINTER_PASSED)) &&
					      !owned(e[j].at, 4) && value_read(e[j].at, 4) + off == target;
				pass += !round && ok;
				if (round && (off | (ok ? INGAME_POINTER_PASSED : 0)) != e[j].offset) {
					e[j].offset = off | (ok ? INGAME_POINTER_PASSED : 0);
					changed = 1;
				}
			}
			if (changed)
				sram_out(at, search_buf, k * sizeof(*e));
		}
	debug_number(log_ptr_checked, pass, log_ptrs);
	if (!pass)
		return 0;
	m.ptr_passed = pass;
	m.ptr_checks++;
	m.ptr_all = 0;
	pointers_head(target);
	return pass;
}

// Keeps the picked candidate through the pointer on the pointers page's
// row r: [pointer]+offset. On (A) only once it passed a Check: then picked,
// and set once now where it points in RAM (the live block takes plain
// addresses only); else, or with SELECT, kept off.
EXS static void search_keep_ptr(uint32_t r, int on)
{
	if (m.found >= INGAME_FOUND_MAX || r >= m.ptr_rows)
		return;
	const struct ingame_pointer *p = &ptr_list[r];
	const uint32_t off = p->offset & ~INGAME_POINTER_PASSED, size = m.search.size, value = keep_value();
	const int passed = m.ptr_checks && (p->offset & INGAME_POINTER_PASSED), now = on && passed;
	struct ingame_found_cheat c = {p->at, value, (uint8_t)size,
				       (uint8_t)(INGAME_FOUND_POINTER | (now ? INGAME_FOUND_ON : 0)), (uint16_t)off};
	found_add(&c);
	uint32_t to = value_read(p->at, 4) + off;
	if (now && in_ram(to, size) && !owned(to, size))
		value_write(to, size, value);
	m.notice = on && !passed ? INGAME_TEXT_SEARCH_PTR_KEPT_OFF : INGAME_TEXT_SEARCH_KEPT;
	debug_print(log_kept_ptr);
	slots_update();
}

// Keeps the picked candidate as a found cheat (on: and on now)
EXS static void search_keep(int on)
{
	if (m.found >= INGAME_FOUND_MAX || m.pick >= m.list_n || (on && live_full()))
		return;
	uint32_t addr = search_found[m.pick], size = m.search.size, value = keep_value();
	if (owned(addr, size))
		return;
	struct ingame_found_cheat c = {addr, value, (uint8_t)size, (uint8_t)(on ? INGAME_FOUND_ON : 0), 0};
	found_add(&c);
	if (on) {
		if (m.live)
			live_add(addr, size, value);
		value_write(addr, size, value);  // at once; without the live block, this once
	} else {
		live_drop(addr);  // kept again, off: not held any more
	}
	m.notice = INGAME_TEXT_SEARCH_KEPT;
	debug_print(on ? log_kept_on : log_kept);
	slots_update();
}

// A filter against the last scan waits after an Undo that couldn't put the
// snapshot back (stale), unless it compares with the first scan
EXS static int search_stale(uint32_t r)
{
	return r < CHEATSEARCH_OPS && cheatsearch_relative(r) && m.search.stale &&
	       !(m.search.against && m.search.first == INGAME_FIRST_KEPT);
}

// The picked candidate is Torii's in game: no keeping it
EXS static int pick_owned(void)
{
	return m.pick < m.list_n && owned(search_found[m.pick], m.search.size);
}

EXS static int search_greyed(uint32_t r)
{
	if (m.page == PAGE_SEARCH) {
		if (!searching())
			return r == SR_START && m.states && buffer_unkept();
		return (r < CHEATSEARCH_OPS && !m.search.left) || search_stale(r) ||
		       (r == SR_SHOW && !(m.search.left && m.search.left <= INGAME_SEARCH_LIST)) ||
		       (r == SR_UNDO && !m.search.undo) || (r == SR_AGAINST && m.search.first == INGAME_FIRST_LOST) ||
		       (r == SR_SIGNED && m.search.bcd);
	}
	if (m.page == PAGE_PICK)
		return ((r == PR_KEEP || r == PR_KEEP_ON) && (m.found >= INGAME_FOUND_MAX || pick_owned())) ||
		       (r == PR_KEEP_ON && live_full()) || (r == PR_PTR_CHECK && !m.ptr_count);
	if (m.page == PAGE_PTRS)
		return r < m.ptr_rows && m.found >= INGAME_FOUND_MAX;
	return 0;
}
EXS static uint32_t search_row_text(uint32_t r)
{
	if (m.page == PAGE_SEARCH && !searching())
		return text_at(r == SR_SIZE ? INGAME_TEXT_SEARCH_SIZE + m.search_size : INGAME_TEXT_SEARCH_START);
	if (m.page == PAGE_SEARCH)
		return text_at(r < CHEATSEARCH_OPS ? op_text[r]
			       : r == SR_SHOW      ? INGAME_TEXT_SEARCH_SHOW
			       : r == SR_UNDO      ? INGAME_TEXT_SEARCH_UNDO
			       : r == SR_AGAINST   ? INGAME_TEXT_SEARCH_AGAINST + (m.search.against != 0)
			       : r == SR_SIGNED    ? INGAME_TEXT_SEARCH_SIGNED + (m.search.sign != 0)
			       : r == SR_FORMAT    ? INGAME_TEXT_SEARCH_FORMAT + (m.search.bcd != 0)
						   : INGAME_TEXT_SEARCH_NEW);
	if (m.page == PAGE_PICK)
		return text_at(r == PR_VALUE       ? INGAME_TEXT_SEARCH_VALUE
			       : r == PR_PTR_FIND  ? INGAME_TEXT_SEARCH_PTR_FIND
			       : r == PR_PTR_CHECK ? INGAME_TEXT_SEARCH_PTR_CHECK
			       : r == PR_KEEP      ? INGAME_TEXT_SEARCH_KEEP
			       : m.live            ? INGAME_TEXT_SEARCH_KEEP_ON
						   : INGAME_TEXT_SEARCH_KEEP_SET);
	if (m.page == PAGE_PTRS && r == m.ptr_rows)  // the list as Find made it, or those that passed
		return text_at(m.ptr_all ? INGAME_TEXT_SEARCH_PTR_ONLY : INGAME_TEXT_SEARCH_PTR_ALL);
	return 0;
}
// The search pages' footer, a text; 0 for a number in it ("1,204 left")
EXS static uint32_t search_footer(void)
{
	uint32_t t = 0;
	if (m.busy)
		t = INGAME_TEXT_SEARCH_BUSY;
	else if (m.edit)
		t = INGAME_TEXT_SEARCH_EDIT;
	else if (m.page == PAGE_PICK && m.found >= INGAME_FOUND_MAX)  // before the notice of the keep that filled it
		t = INGAME_TEXT_SEARCH_FOUND_FULL;
	else if (m.notice)
		t = m.notice;
	else if (m.page == PAGE_LIST)
		t = INGAME_TEXT_SEARCH_PICK;
	else if (m.page == PAGE_PTRS)
		t = m.cursor >= m.ptr_rows ? 0
		    : m.ptr_checks && (ptr_list[m.cursor].offset & INGAME_POINTER_PASSED)
			    ? INGAME_TEXT_SEARCH_PTR_HINT_ON
			    : INGAME_TEXT_SEARCH_PTR_HINT_OFF;
	else if (m.page == PAGE_SEARCH && !searching())
		t = m.cursor != SR_START || !m.states ? INGAME_TEXT_SEARCH_HINT
		    : buffer_unkept()                 ? INGAME_TEXT_SEARCH_STATE_FIRST
		    : m.buffer_slot                   ? INGAME_TEXT_SEARCH_DROPS
						      : INGAME_TEXT_SEARCH_HINT;
	else if (m.page == PAGE_PICK && (m.cursor == PR_KEEP || m.cursor == PR_KEEP_ON) && pick_owned())
		t = INGAME_TEXT_SEARCH_OWNED;
	else if (m.page == PAGE_PICK && m.cursor == PR_KEEP_ON && live_full())
		t = INGAME_TEXT_SEARCH_LIVE_FULL;
	else if (m.page == PAGE_PICK && m.cursor == PR_KEEP_ON && !m.live)
		t = INGAME_TEXT_SEARCH_SET_HINT;
	else if (!m.search.left)
		t = INGAME_TEXT_SEARCH_NONE;
	else if (m.page == PAGE_SEARCH && search_stale(m.cursor))
		t = INGAME_TEXT_SEARCH_STALE;
	else if (m.page == PAGE_SEARCH && m.cursor == SR_AGAINST && m.search.first == INGAME_FIRST_LOST)
		t = INGAME_TEXT_SEARCH_FIRST_LOST;
	return t ? text_at(t) : 0;
}

// Slot k's line when the search draws it (its numbers): 1, in l
EXS static int slot_compose(uint32_t k, struct ingame_line *l)
{
	if (m.page < PAGE_SEARCH || !m.have_session)
		return 0;
	memset(l, 0, sizeof(*l));
	if (m.title && k == 0) {
		if (m.page == PAGE_PTRS) {  // "12 found", or after a Check "3 passed the check"
			const int only = m.ptr_checks && !m.ptr_all;
			line_number(l, only ? INGAME_TEXT_SEARCH_PTR_PASSED : INGAME_TEXT_SEARCH_PTR_FOUND,
				    only ? m.ptr_passed : m.ptr_count, 0, 1, NOT_MARKED);
			return 1;
		}
		if (m.page != PAGE_PICK || m.pick >= m.list_n)
			return 0;
		hex_put(l, 0, search_found[m.pick]);
		return 1;
	}
	if (m.footer && k == m.title + m.visible) {
		if (search_footer())
			return 0;
		if (m.page == PAGE_PTRS)
			line_number(l, INGAME_TEXT_SEARCH_PTR_FOUND, m.ptr_count, 0, 1, NOT_MARKED);
		else if (m.page == PAGE_SEARCH && m.cursor == SR_SHOW && m.search.left > INGAME_SEARCH_LIST)
			line_number(l, INGAME_TEXT_SEARCH_FEW, m.search.left, 0, 1,
				    NOT_MARKED);  // "1,204 left: narrow to 32"
		else
			line_number(l, INGAME_TEXT_SEARCH_LEFT, m.search.left, 0, 1, NOT_MARKED);
		return 1;
	}
	int r = slot_row(k);
	if (r < 0)
		return 0;
	uint32_t size = search_bytes(), editing = m.edit && (uint32_t)r == m.cursor;
	if (m.page == PAGE_SEARCH && searching() && r >= CHEATSEARCH_UP_BY && r < CHEATSEARCH_OPS) {
		line_filter(l, (uint32_t)r, (int)editing);
		return 1;
	}
	if (m.page == PAGE_PICK && r == PR_VALUE) {
		line_read(INGAME_TEXT_SEARCH_VALUE, l);
		uint32_t x = value_put(l, l->width, editing ? m.edit_value : m.pick_value, size,
				       number_mode(CHEATSEARCH_EQUALS), (int)editing, m.edit_place);
		line_read(INGAME_TEXT_SEARCH_VALUE_AFTER, &spare);
		if (spare.width)
			line_or(l, &spare, x);
		return 1;
	}
	if (m.page == PAGE_LIST) {
		uint32_t addr = search_found[r], x = hex_put(l, 0, addr);
		element_put(l, x + LIST_GAP, value_read(addr, size), size);
		return 1;
	}
	if (m.page == PAGE_PTRS && (uint32_t)r < m.ptr_rows) {  // [pointer]+offset
		const struct ingame_pointer *p = &ptr_list[r];
		uint32_t x = glyph_put(l, 0, INGAME_GLYPH_OPEN, 0);
		x = hex_put(l, x, p->at);
		x = glyph_put(l, x, INGAME_GLYPH_CLOSE, 0);
		x = glyph_put(l, x, INGAME_GLYPH_PLUS, 0);
		for (int sh = 12; sh >= 0; sh -= 4)
			x = glyph_put(l, x, p->offset >> sh & 15, 0);
		return 1;
	}
	return 0;
}

// The extension, its first part (search 0) or both: for each part not in
// yet, the game's EWRAM there into its backup, after the window's (one run
// in cart SRAM), read back, and the part from its image in, checked (the
// first with the IWRAM code at the image's end) against the kernel's check
// word. 0 if a backup didn't read back or a part didn't load whole, the
// game's EWRAM as it was.
static int ext_load_part(int search)
{
	const uint32_t image = INGAME_SRAM_EXT - (uint32_t)(uintptr_t)__ov_image_end;
	uint32_t *const part[3] = {__ov_ext_start, __ov_ext_mid, __ov_ext_end};
	if (!m.have_session)
		return 0;
	for (uint32_t k = 0; k <= (uint32_t)search; k++) {
		uint32_t *start = part[k];
		const uint32_t n = (uint32_t)((uintptr_t)part[k + 1] - (uintptr_t)start);
		if (m.ext > (uint32_t)((uintptr_t)start - (uintptr_t)__ov_ext_start))
			continue;  // in already
		if (!sram_keep(INGAME_BACKUP_EWRAM + ((uint32_t)(uintptr_t)start - INGAME_EWRAM), start, n))
			goto failed;
		m.ext += n;  // the game's bytes there are in the backup from now on
		sram_in(image + (uint32_t)(uintptr_t)__ov_ext_load +
				((uint32_t)(uintptr_t)start - (uint32_t)(uintptr_t)__ov_ext_start),
			start, n);
		uint32_t s = ingame_image_check(start, n / 4), words[16];
		const uint32_t code = k ? 0 : (uint32_t)((uintptr_t)__ov_fastext_end - (uintptr_t)__ov_fastext_start);
		for (uint32_t off = 0; off < code; off += sizeof(words)) {
			uint32_t c = code - off < sizeof(words) ? code - off : sizeof(words);
			sram_in(image + (uint32_t)(uintptr_t)__ov_fastext_load + off, words, c);
			for (uint32_t i = 0; i < c / 4; i++)
				s = (s << 5 | s >> 27) + words[i];
		}
		if (s != (k ? session()->ext2_check : session()->ext_check))
			goto failed;
	}
	return 1;
failed:  // the menu's footer says why its row did nothing
	debug_print("ingame: the extension didn't load: its EWRAM's backup or its code didn't read back");
	ext_unload();
	m.menu_notice = INGAME_TEXT_SEARCH_FAILED;
	if (m.page == PAGE_MENU && m.footer)
		slot_update(m.title + m.visible);
	return 0;
}

static int ext_load(void)
{
	return ext_load_part(0);
}

static void search_open(enum page_id from)
{
	if (!ext_load_part(1))  // (its footer says why)
		return;
	sram_in(INGAME_SRAM_GLYPHS, glyphs, sizeof(glyphs));
	m.search_from = from;
	page_open(PAGE_SEARCH, searching() ? CHEATSEARCH_SAME : SR_START);
}

// A number edit's keys (down): the digit, its value, A done (1), B cancelled (-1)
EXS static int edit_keys(uint32_t down)
{
	uint32_t n = cheatsearch_places(m.search.size, m.edit_mode);
	if (down & KEY_B) {
		m.edit = 0;
		return -1;
	}
	if (down & KEY_A) {
		m.edit = 0;
		return 1;
	}
	if ((down & KEY_LEFT) && m.edit_place)
		m.edit_place--;
	if ((down & KEY_RIGHT) && m.edit_place + 1 < n)
		m.edit_place++;
	if (down & (KEY_UP | KEY_DOWN))
		m.edit_value = cheatsearch_edit(m.edit_value, m.search.size, m.edit_mode, m.edit_place,
						(down & KEY_UP) ? 1 : -1);
	return 0;
}
// A number's edit, on its last digit; signed numbers have a sign's place
// first
EXS static void edit_begin(uint32_t value, uint32_t mode, uint32_t stage)
{
	const uint32_t top = cheatsearch_largest(m.search.size, mode);
	m.edit = 1;
	m.edit_mode = mode;
	m.edit_stage = stage;
	m.edit_value = value & cheatsearch_max(m.search.size);
	if (mode != CHEATSEARCH_SIGNED && m.edit_value > top)
		m.edit_value = top;
	m.edit_place = cheatsearch_places(m.search.size, mode) - 1;
	slots_update();
}

// How the search would read filter op's number with the toggles at sign
// and bcd (number_mode's)
EXS static uint32_t mode_with(uint32_t op, uint32_t sign, uint32_t bcd)
{
	return bcd ? CHEATSEARCH_BCD : sign && op >= CHEATSEARCH_EQUALS ? CHEATSEARCH_SIGNED : CHEATSEARCH_UNSIGNED;
}

// Negative numbers or Stored as toggled (v0.9.22): the filters' numbers as
// the same numbers read the new way, or their first values where that
// can't hold them (-5 unsigned, 300 in BCD's two digits)
EXS static void values_convert(uint32_t sign, uint32_t bcd)
{
	const uint32_t size = m.search.size;
	for (uint32_t op = 0; op < CHEATSEARCH_OPS; op++) {
		uint32_t from = mode_with(op, sign, bcd), to = number_mode(op);
		uint32_t first = op == CHEATSEARCH_UP_BY || op == CHEATSEARCH_DOWN_BY;
		m.search.value[op] = cheatsearch_convert(m.search.value[op], size, from, to, first);
		if (op == CHEATSEARCH_BETWEEN)
			m.search.value_hi = cheatsearch_convert(m.search.value_hi, size, from, to, 0);
	}
}

// A on the search pages' row r
EXS static void search_a(uint32_t r)
{
	if (search_greyed(r))
		return;
	if (m.page == PAGE_SEARCH && !searching()) {
		if (r == SR_SIZE) {
			m.search_size = (m.search_size + 1) % 3;
			slots_update();
		} else if (search_start()) {
			page_open(PAGE_SEARCH, CHEATSEARCH_SAME);
		}
	} else if (m.page == PAGE_SEARCH) {
		if (r < CHEATSEARCH_UP_BY) {
			search_filter(r);
		} else if (r < CHEATSEARCH_OPS) {  // its number first; By's an amount, never signed
			edit_begin(m.search.value[r], number_mode(r), 0);
		} else if (r == SR_AGAINST || r == SR_SIGNED || r == SR_FORMAT) {
			const uint32_t sign = m.search.sign, bcd = m.search.bcd;
			if (r == SR_AGAINST)
				m.search.against ^= 1;
			else if (r == SR_SIGNED)
				m.search.sign ^= 1;
			else
				m.search.bcd ^= 1;
			values_convert(sign, bcd);
			search_save();
			slots_update();
		} else if (r == SR_SHOW) {
			search_list();
			page_open(PAGE_LIST, 0);
		} else if (r == SR_UNDO) {
			search_undo();
		} else if (m.confirm != CONFIRM_NEW) {  // New search ends this one: A again
			m.confirm = CONFIRM_NEW;
			m.notice = INGAME_TEXT_SEARCH_CONFIRM_NEW;
			m.soft = 1;
			slots_update();
		} else {
			search_drop();
			page_open(PAGE_SEARCH, SR_START);
		}
	} else if (m.page == PAGE_LIST) {
		m.pick = r;
		m.pick_value = value_read(search_found[r], m.search.size);
		if (m.search.bcd && !cheatsearch_from_bcd(m.pick_value, m.search.size, &m.pick_value))
			m.pick_value = 0;  // as a number: BCD's digits
		page_open(PAGE_PICK, PR_KEEP);
	} else if (m.page == PAGE_PTRS) {
		if (r < m.ptr_rows) {
			search_keep_ptr(r, 1);
		} else {  // the list as Find made it, or those that passed
			m.ptr_all ^= 1;
			pointers_rows();
			page_open(PAGE_PTRS, m.ptr_rows);
		}
	} else if (r == PR_VALUE) {
		edit_begin(m.pick_value, number_mode(CHEATSEARCH_EQUALS), 0);
	} else if (r == PR_PTR_FIND && m.ptr_count && m.confirm != CONFIRM_FIND) {  // the list goes: A again
		m.confirm = CONFIRM_FIND;
		m.notice = INGAME_TEXT_SEARCH_CONFIRM_FIND;
		m.soft = 1;
		slots_update();
	} else if (r == PR_PTR_FIND || r == PR_PTR_CHECK) {
		search_busy(1);
		uint32_t found = 0;
		if (r == PR_PTR_FIND) {
			pointers_find(search_found[m.pick]);
			found = m.ptr_count;
		} else {
			found = pointers_check(search_found[m.pick]);
		}
		m.busy = 0;
		if (found) {
			pointers_rows();
			page_open(PAGE_PTRS, 0);
			if (m.ptr_full) {  // the scan stopped at INGAME_POINTERS_MAX: the real one may be past it
				m.notice = m.ptr_checks ? INGAME_TEXT_SEARCH_PTR_FULL : INGAME_TEXT_SEARCH_PTR_MANY;
				m.soft = 1;
				slot_update(m.title + m.visible);
			}
		} else {  // none: no page to show (a Check's leaves the list as it was)
			m.notice = r == PR_PTR_FIND ? INGAME_TEXT_SEARCH_PTR_NONE : INGAME_TEXT_SEARCH_PTR_NONE_PASS;
			if (r == PR_PTR_CHECK)
				debug_print(log_ptr_none_pass);
			slots_update();
		}
	} else {
		search_keep(r == PR_KEEP_ON);
	}
}

// Between's numbers in order (v0.9.22): From 9 to 2 searches from 2 to 9
EXS static void between_order(void)
{
	uint32_t lo = m.search.value[CHEATSEARCH_BETWEEN], hi = m.search.value_hi, flip = 0;
	if (number_mode(CHEATSEARCH_BETWEEN) == CHEATSEARCH_SIGNED)
		flip = 1u << (8 * m.search.size - 1);
	if ((lo ^ flip) > (hi ^ flip)) {
		m.search.value[CHEATSEARCH_BETWEEN] = hi;
		m.search.value_hi = lo;
	}
}

// A number's edit done: its filter searches with it (Between's, after its
// upper too), Value keeps it to keep
EXS static void edit_done(void)
{
	if (m.page == PAGE_SEARCH) {
		uint32_t op = m.cursor;
		if (op == CHEATSEARCH_BETWEEN && m.edit_stage == 0) {  // its upper next
			m.edit_saved = m.search.value[op];
			m.search.value[op] = m.edit_value;
			edit_begin(m.search.value_hi, m.edit_mode, 1);
			return;
		}
		if (op == CHEATSEARCH_BETWEEN) {
			m.search.value_hi = m.edit_value;
			between_order();
		} else {
			m.search.value[op] = m.edit_value;
		}
		search_filter(op);
	} else {
		m.pick_value = m.edit_value;
		slots_update();
	}
}

// B in a number's edit: the number as it was; on Between's upper, its
// lower too (v0.9.22: the lower stayed as edited)
EXS static void edit_cancel(void)
{
	if (m.page == PAGE_SEARCH && m.cursor == CHEATSEARCH_BETWEEN && m.edit_stage == 1)
		m.search.value[CHEATSEARCH_BETWEEN] = m.edit_saved;
	slots_update();
}
// B on a search page: back where it was opened from
EXS static void search_back(void)
{
	if (m.page == PAGE_PTRS) {
		page_open(PAGE_PICK, PR_PTR_FIND);
	} else if (m.page == PAGE_PICK) {
		page_open(PAGE_LIST, m.pick);
	} else if (m.page == PAGE_LIST) {
		page_open(PAGE_SEARCH, SR_SHOW);
	} else if (m.search_from == PAGE_CHEATS) {
		page_open(PAGE_CHEATS, m.cheats + LIST_SEARCH);
	} else {
		uint32_t back = 0;
		for (uint32_t r = 0; r < ROW_KINDS; r++)
			if (m.kinds[r] == ROW_SEARCH)
				back = r;
		page_open(PAGE_MENU, back);
	}
}

static void state_mark(uint32_t slot);
static uint32_t shadow_take(uint16_t *io);
static void shot_take(const uint16_t *io, uint32_t shadow);
static void debug_take(void);

// Runs the menu until a choice; returns its INGAME_ACT_*
static uint32_t run(uint32_t menu_key)
{
	uint32_t prev = keys_held(), held_for = 0;
	for (;;) {
		vsync();
		if (m.pill_y != m.pill_to) {
			int step = (m.pill_to - m.pill_y) / 2;
			m.pill_y += step ? step : (m.pill_to > m.pill_y ? 1 : -1);
			pill_place(m.pill_y);
		}
		if (m.mq != MQ_OFF)
			marquee_tick();
		uint32_t keys = keys_held(), down = keys & ~prev;
		uint32_t move = keys & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT);
		held_for = move && (move & prev) ? held_for + 1 : 0;
		if (held_for >= REPEAT_DELAY && (held_for - REPEAT_DELAY) % REPEAT_EVERY == 0)
			down |= move;
		prev = keys;
		if ((keys & menu_key) == menu_key && (down & menu_key))
			return INGAME_ACT_RESUME;
		if (m.edit) {
			int done = edit_keys(down);
			if (done > 0)
				edit_done();
			else if (done < 0)
				edit_cancel();
			else
				slots_update();
			continue;
		}
		if (down & KEY_B) {
			if (m.page == PAGE_MENU)
				return INGAME_ACT_RESUME;
			if (m.page >= PAGE_SEARCH) {
				search_back();
				continue;
			}
			// Back to the menu, on the row that opened the page
			uint32_t from = m.page == PAGE_SAVE ? ROW_SAVE : m.page == PAGE_LOAD ? ROW_LOAD : ROW_CHEATS;
			uint32_t back = 0;
			for (uint32_t r = 0; r < ROW_KINDS; r++)
				if (m.kinds[r] == from)
					back = r;
			page_open(PAGE_MENU, back);
			continue;
		}
		if (down & (KEY_UP | KEY_DOWN)) {
			cursor_to(next_row(m.cursor, (down & KEY_UP) ? -1 : 1));
		} else if (m.page != PAGE_MENU && (down & (KEY_LEFT | KEY_RIGHT))) {
			// a page of rows at a time, stopping at the ends
			uint32_t r = m.cursor;
			if (down & KEY_LEFT)
				r = r > m.visible ? r - m.visible : 0;
			else
				r = r + m.visible < m.rows ? r + m.visible : m.rows - 1;
			cursor_to(r);
		}
		if ((down & KEY_SELECT) && m.page == PAGE_PTRS) {  // a pointer kept off (A: on, if it passed a Check)
			search_keep_ptr(m.cursor, 0);
			continue;
		}
		if ((down & KEY_SELECT) && m.page == PAGE_CHEATS && m.cursor < m.cheats && cheat_once_ok(m.cursor)) {
			uint32_t e = m.cursor;
			m.once = m.once == e + 1 ? 0 : e + 1;
			if (m.footer)
				slot_update(m.title + m.visible);
			continue;
		}
		if (down & KEY_A) {
			if (m.page == PAGE_CHEATS && m.cursor == m.cheats + LIST_SEARCH) {
				search_open(PAGE_CHEATS);
				continue;
			}
			if (m.page == PAGE_CHEATS) {
				list_pick(m.cursor);
				continue;
			}
			if (m.page >= PAGE_SEARCH) {
				search_a(m.cursor);
				continue;
			}
			if (m.page == PAGE_SAVE) {
				if (searching() && m.confirm != CONFIRM_SAVE) {  // it ends the search: A again
					m.confirm = CONFIRM_SAVE;
					slot_update(m.title + m.visible);
					continue;
				}
				state_mark(m.cursor + 1);
				return INGAME_ACT_SAVE;
			}
			if (m.page == PAGE_LOAD) {
				if (row_greyed(m.cursor))
					continue;
				return INGAME_ACT_LOAD;
			}
			uint32_t kind = m.kinds[m.cursor];
			if (row_greyed(m.cursor))
				continue;
			if (m.states && kind == ROW_SAVE) {
				page_open(PAGE_SAVE, m.buffer_slot ? m.buffer_slot - 1 : 0);
				continue;
			}
			if (m.states && kind == ROW_LOAD) {
				page_open(PAGE_LOAD, m.buffer_slot - 1);
				continue;
			}
			if (kind == ROW_SHOT) {
				if (m.shot_full)
					continue;
				if (m.shots >= INGAME_SHOTS_MAX ||
				    m.shot_at + sizeof(struct ingame_shot) + 0x1000 > INGAME_SHOTS_END) {
					m.shot_full = 1;
					slot_update(m.title + m.visible);
					continue;
				}
				// The game's copy of the registers first: the extension
				// loads over EWRAM the scan reads
				uint16_t io[INGAME_SHOT_IO] = {0};
				ext_unload();
				uint32_t shadow = shadow_take(io);
				if (!ext_load())
					continue;
				shot_take(io, shadow);
				if (m.footer)
					slot_update(m.title + m.visible);
				continue;
			}
			if (kind == ROW_SEARCH) {
				search_open(PAGE_MENU);
				continue;
			}
			if (kind == ROW_DEBUG) {
				if (ext_load())
					debug_take();
				m.flags |= NOTICE_DEBUG;
				page_open(PAGE_MENU, 0);
				screen_ready(PAGE_MENU);
				continue;
			}
			if (kind == ROW_CHEATS) {
				if (m.cheats) {
					if (ext_load())  // the list's code
						page_open(PAGE_CHEATS, 0);
				} else {
					volatile uint8_t *onoff = (volatile uint8_t *)INGAME_CHEAT_ONOFF;
					*onoff = *onoff == 1 ? 0 : 1;
					slot_update(m.title + m.cursor);
				}
				continue;
			}
			return row_action[kind];
		}
	}
}

// What a state saved now is of (struct ingame_state_info): the running
// ROM's header, the session's ROM size, the launch's hook
static void state_info(struct ingame_state_info *info)
{
	const volatile uint8_t *rom = (const volatile uint8_t *)0x08000000;
	memset(info, 0, sizeof(*info));
	info->magic = INGAME_STATE_INFO_MAGIC;
	info->format = INGAME_STATE_FORMAT;
	info->bytes = sizeof(*info);
	for (uint32_t i = 0; i < 4; i++)
		info->code[i] = (char)rom[0xAC + i];
	info->version = rom[0xBC];
	info->check = rom[0xBD];
	info->rom_bytes = m.have_session ? session()->rom_bytes : 0;
	info->hook = m.call->hook;
}

// The buffer's tag before SAVE: slot's, not kept; its mark cleared, so a
// SAVE cut short leaves no state (the stub writes the mark last); and what
// it is of
static void state_mark(uint32_t slot)
{
	struct ingame_state_tag tag;
	struct ingame_state_info info;
	uint32_t zeros[4] = {0, 0, 0, 0};
	memset(&tag, 0, sizeof(tag));
	tag.magic = INGAME_STATE_TAG_MAGIC;
	tag.slot = (uint8_t)slot;
	tag.rom_crc = session()->rom_crc;
	state_info(&info);
	sram_out(INGAME_SRAM_STATE_MARK, zeros, sizeof(zeros));
	sram_out(INGAME_SRAM_STATE_TAG, &tag, sizeof(tag));
	sram_out(INGAME_SRAM_STATE_INFO, &info, sizeof(info));
	m.saved_slot = slot;
}

// The answer for the kernel's next boot: the last action, and with the
// cheat list, the entries on (as launched again after Reset game and Load
// state: the
// BIOS's SoftReset and LOAD zero the toggles)
static void answer(uint32_t act)
{
	struct ingame_out out;
	if (!m.have_session)
		return;
	sram_in(INGAME_SRAM_SESSION + INGAME_SESSION_OUT, &out, sizeof(out));
	if (out.magic != INGAME_OUT_MAGIC) {
		memset(&out, 0, sizeof(out));
		out.pending_state = 0xFFFF;
		out.magic = INGAME_OUT_MAGIC;
	}
	out.action = (uint16_t)act;
	if (act == INGAME_ACT_SAVE && m.saved_slot)
		out.pending_state = (uint16_t)m.saved_slot;
	if (m.shot_now)
		out.screenshot = (uint16_t)m.shots;
	int as_launched = act == INGAME_ACT_RESET || act == INGAME_ACT_LOAD;  // both zero the toggles
	if (m.changed || (as_launched && out.cheats_changed)) {
		const uint32_t *on = as_launched ? m.launched : m.on;
		out.cheat_on[0] = on[0];
		out.cheat_on[1] = on[1];
		out.cheats_changed = 1;
	}
	sram_out(INGAME_SRAM_SESSION + INGAME_SESSION_OUT, &out, sizeof(out));
}

// The state slots, if the session has them: the buffer's slot when its
// state is this game's (its tag's rom_crc the session's)
static void states_load(void)
{
	const struct ingame_session *s = session();
	m.states = m.have_session && (m.flags & INGAME_CAN_STATES) && s->state_slots == INGAME_STATE_SLOTS;
	if (!m.states)
		return;
	struct ingame_state_tag tag;
	struct ingame_state_info info, now;
	sram_in(INGAME_SRAM_STATE_TAG, &tag, sizeof(tag));
	sram_in(INGAME_SRAM_STATE_INFO, &info, sizeof(info));
	state_info(&now);
	// The game's (v0.8.0's states have no info: their tag's rom_crc alone)
	// and a format LOAD reads
	int game = info.magic != INGAME_STATE_INFO_MAGIC ||
		   (info.format <= INGAME_STATE_FORMAT && info.code[0] == now.code[0] && info.code[1] == now.code[1] &&
		    info.code[2] == now.code[2] && info.code[3] == now.code[3] && info.version == now.version &&
		    info.check == now.check && info.bytes == sizeof(info) &&
		    (!now.rom_bytes || info.rom_bytes == now.rom_bytes));  // as statestore.c's buffer_whose
	if (state_saved() && tag.magic == INGAME_STATE_TAG_MAGIC && tag.slot >= 1 && tag.slot <= INGAME_STATE_SLOTS &&
	    tag.rom_crc == s->rom_crc && game) {
		m.buffer_slot = tag.slot;
		m.buffer_kept = tag.kept;
	}
}

// --- The screenshots (P4) ------------------------------------------------------

// The display's registers the hardware reads back (halfword indexes from
// 0x04000000): DISPCNT, DISPSTAT, VCOUNT, BG0-3CNT, WININ, WINOUT, BLDCNT,
// BLDALPHA; the rest are write-only
static const uint8_t io_readable[] = {0x00, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x24, 0x25, 0x28, 0x29};
_Static_assert(sizeof(io_readable) == sizeof(m.io) / 2, "m.io holds them");

// The screenshots waiting in cart SRAM: how many, and where the next goes
static void shots_load(void)
{
	m.shots = 0;
	m.shot_at = INGAME_SRAM_SHOT;
	while (m.shots < INGAME_SHOTS_MAX) {
		uint32_t head[3];  // magic, version and bytes, packed
		sram_in(m.shot_at, head, sizeof(head));
		if ((head[0] != INGAME_SHOT_MAGIC && head[0] != INGAME_SHOT_DONE) ||
		    head[1] != (INGAME_SHOT_VERSION | sizeof(struct ingame_shot) << 16) || head[2] % 4 ||
		    head[2] > INGAME_SHOT_PACKED_MAX ||
		    m.shot_at + sizeof(struct ingame_shot) + head[2] > INGAME_SHOTS_END)
			break;
		m.shot_at += sizeof(struct ingame_shot) + head[2];
		m.shots++;
	}
}

// The game's copy of the display registers, which games built on
// Nintendo's library keep in RAM and copy to them each frame (Pokémon's
// gGpuRegBuffer): the write-only scroll is in it. A place from p to end
// whose DISPCNT is the display's and whose BG controls are too (every
// enabled BG's), scored by the readable registers it matches; the best
// beyond *best, or 0
static const volatile uint16_t *shadow_scan(const volatile uint16_t *p, const volatile uint16_t *end, uint32_t *best)
{
	const volatile uint16_t *found = 0;
	const uint32_t disp = m.io[0];
	for (end -= INGAME_SHOT_IO; p < end; p++) {
		if (*p != disp)
			continue;
		uint32_t score = 0;
		for (uint32_t b = 0; b < 4; b++)
			if (p[4 + b] == m.io[3 + b])
				score++;
			else if (disp >> (8 + b) & 1)
				score = 8;  // an enabled BG's differs
		if (score >= 8)
			continue;
		score += (p[0x24] == m.io[7]) + (p[0x25] == m.io[8]) + (p[0x28] == m.io[9]) + (p[0x29] == m.io[10]);
		if (score > *best) {
			*best = score;
			found = p;
		}
	}
	return found;
}

// The write-only registers into io from the game's copy, if found: IWRAM
// first, EWRAM past the window (the overlay's) if IWRAM has no copy whose
// BG controls all match. Returns the copy's address, or 0.
static uint32_t shadow_take(uint16_t *io)
{
	const uint32_t disp = m.io[0];
	if ((disp & 7) > 2 || !(disp & 0x0F00))  // a bitmap mode, or no BG: no scroll
		return 0;
	uint32_t best = 0;
	const volatile uint16_t *p =
		shadow_scan((const volatile uint16_t *)0x03000000, (const volatile uint16_t *)0x03007E00, &best);
	if (best < 4) {
		const volatile uint16_t *q =
			shadow_scan((const volatile uint16_t *)(INGAME_EWRAM + ov_header.window_bytes),
				    (const volatile uint16_t *)0x02040000, &best);
		p = q ? q : p;
	}
	if (!p)
		return 0;
	for (uint32_t i = 0x08; i < INGAME_SHOT_IO; i++)
		if (i < 0x24 || i > 0x29 || i == 0x26 || i == 0x27)  // not WININ, WINOUT, BLDCNT, BLDALPHA
			io[i] = p[i];
	return (uint32_t)(uintptr_t)p;
}

// The packing's output, a buffer at a time into cart SRAM up to the
// screenshots' room's end
#define SHOT_BUF 254  // halfwords: whole words a flush, and a spare for the last one's padding
static uint32_t shot_out;

// The packing's scratch: the game's IWRAM from its start, kept in cart
// SRAM meanwhile (INGAME_BACKUP_SHOT): the packer's table, the buffer, then
// its ARM code (ov.ld's .fast, linked there and loaded with the image).
// From the window, in Thumb, a screenshot took 40 frames; and every byte of
// the window costs the menu's opening time.
struct shot_scratch {
	uint16_t hash[SHOTPACK_HASH];
	uint16_t buf[SHOT_BUF + 2];
	uint32_t code[];
};
_Static_assert(sizeof(struct shot_scratch) == 0xA00 && 0xA00 + 0x800 <= INGAME_BACKUP_SHOT_BYTES,
	       "ov.ld links the code at INGAME_SHOT_IWRAM + 0xA00, 2 KB at most");

EXT static int shot_flush(struct shotpack_out *o)
{
	if (o->n & 1)
		o->buf[o->n] = 0;
	uint32_t bytes = (o->n * 2 + 3) & ~3u;
	if (shot_out + bytes > INGAME_SHOTS_END)
		return 0;
	sram_out(shot_out, o->buf, bytes);
	shot_out += bytes;
	return 1;
}

// The game's screen as it was under the menu into the next record, for the
// kernel's next boot. The screen goes white (forced blank: the shutter's
// flash) while the game's OBJ tiles, OAM and OBJ palette come back from the
// stub's backups and the palettes, OAM and VRAM are packed; the registers
// are those read at opening, the scroll from the game's copy; the magic
// last. Then the menu again.
// The cart's clock (the game's GPIO port at 0x080000C4, source/RTC.c's
// protocol) into clock, as struct ingame_shot keeps it, when the game has
// it and isn't part way through its own read: the port's chip select is
// high from a command's first bit to its last byte. The port's three
// registers are put back as the game left them.
#define GPIO_DATA (*(volatile uint16_t *)0x080000C4)
#define GPIO_DIR (*(volatile uint16_t *)0x080000C6)
#define GPIO_READABLE (*(volatile uint16_t *)0x080000C8)

EXT static void clock_bits(uint32_t v)
{
	v <<= 1;
	for (int l = 7; l >= 0; l--) {
		uint16_t b = (uint16_t)((v >> l) & 2);
		GPIO_DATA = b | 4;
		GPIO_DATA = b | 4;
		GPIO_DATA = b | 4;
		GPIO_DATA = b | 5;
	}
}

EXT static void clock_take(uint8_t *clock)
{
	if (!(session()->flags & INGAME_CLOCK_ON))
		return;
	// Readable, the port reads as itself: 4 bits, and 1 at 0x080000C8;
	// else as the ROM's bytes there
	const uint16_t data = GPIO_DATA, dir = GPIO_DIR, readable = GPIO_READABLE;
	const int was_readable = readable == 1 && data < 16 && dir < 16;
	if (was_readable && (data & 4)) {
		debug_print("ingame: the game was reading the clock: the screenshot takes the boot's time");
		return;
	}
	// Its serial lines are timed by repeated writes: at the slowest wait
	// states, as the in-game probe (tests/roms/ingame) reads it
	const uint16_t waitcnt = REG_WAITCNT;
	REG_WAITCNT = 0;
	GPIO_READABLE = 1;
	GPIO_DATA = 1;
	GPIO_DIR = 7;
	GPIO_DATA = 1;
	GPIO_DATA = 5;
	clock_bits(0x65);  // date and time
	GPIO_DIR = 5;
	for (uint32_t i = 0; i < 7; i++) {
		uint32_t v = 0;
		for (uint32_t l = 0; l < 8; l++) {
			for (uint32_t j = 0; j < 5; j++)
				GPIO_DATA = 4;
			GPIO_DATA = 5;
			v |= (GPIO_DATA & 2u) << l;
		}
		clock[i] = (uint8_t)(v >> 1);
	}
	GPIO_DATA = 1;  // chip select low: the command's end
	clock[7] = 1;
	if (was_readable) {
		GPIO_DIR = dir;
		GPIO_DATA = data;
	}
	GPIO_READABLE = was_readable;
	REG_WAITCNT = waitcnt;
}

// Kept out of run: its header is big for the window's stack
// In the extension: io and shadow are shadow_take's, from before it loaded
// over the EWRAM it scans
EXT static void shot_take(const uint16_t *io, uint32_t shadow)
{
	const uint16_t dispcnt = REG_DISPCNT;
	REG_DISPCNT = (uint16_t)(dispcnt | DISPCNT_BLANK);
	struct ingame_shot h;
	memset(&h, 0, sizeof(h));
	h.version = INGAME_SHOT_VERSION;
	h.bytes = sizeof(h);
	h.shadow = shadow;
	for (uint32_t i = 0; i < INGAME_SHOT_IO; i++)
		h.io[i] = io[i];
	clock_take(h.clock);
	for (uint32_t i = 0; i < sizeof(io_readable); i++)
		h.io[io_readable[i]] = m.io[i];
	sram_in(INGAME_BACKUP_OBJ, (void *)INGAME_OBJ_VRAM, m.tiles_backed * 32);
	sram_in(INGAME_BACKUP_OAM, (void *)0x07000000, 0x400);
	sram_in(INGAME_BACKUP_OBJ_PAL, (void *)0x05000200, 0x200);
	sram_out(m.shot_at, &h, sizeof(h));  // no magic while it is written
	shot_out = m.shot_at + sizeof(h);
	struct shot_scratch *k = (struct shot_scratch *)INGAME_SHOT_IWRAM;
	if (!iwram_borrow(1)) {  // cart SRAM didn't keep the game's IWRAM: nothing taken
		debug_print("ingame: the game's IWRAM didn't read back from cart SRAM: no screenshot");
		page_open(PAGE_MENU, m.cursor);
		m.menu_notice = INGAME_TEXT_SEARCH_FAILED;
		slot_update(m.title + m.visible);
		vsync();
		REG_DISPCNT = dispcnt;
		return;
	}
	struct shotpack_out o = {k->buf, SHOT_BUF, 0, 0, shot_flush};
	int ok = shotpack_pack((const volatile uint16_t *)0x05000000, INGAME_SHOT_PAL_HALVES, k->hash, &o) &&
		 shotpack_pack((const volatile uint16_t *)0x07000000, INGAME_SHOT_OAM_HALVES, k->hash, &o) &&
		 shotpack_pack((const volatile uint16_t *)0x06000000, INGAME_SHOT_VRAM_BYTES / 2, k->hash, &o) &&
		 (!o.n || shot_flush(&o));
	iwram_borrow(0);
	if (ok) {
		h.packed = shot_out - m.shot_at - sizeof(h);
		uint32_t none = 0;  // the next record's place: the list ends there
		if (shot_out + 4 <= INGAME_SHOTS_END)
			sram_out(shot_out, &none, 4);
		sram_out(m.shot_at, &h, sizeof(h));
		h.magic = INGAME_SHOT_MAGIC;
		sram_out(m.shot_at, &h, 4);
		m.shot_at = shot_out;
		m.shots++;
		m.shot_now = 1;
		debug_print(h.shadow ? "ingame: screenshot dumped, the scroll from the game's copy"
				     : "ingame: screenshot dumped");
	} else {
		m.shot_full = 1;
		debug_print("ingame: screenshot didn't fit");
	}
	page_open(PAGE_MENU, m.cursor);
	vsync();
	REG_DISPCNT = dispcnt;
}

// --- The DEBUG snapshot ---------------------------------------------------------

void ov_system_regs(uint32_t *out);  // ov_header.s

// The game's state at the menu key into cart SRAM page 0xF0 (struct
// cartlog_snapshot in source/cartlog.c), for the kernel's next boot to log:
// the ROM header, the hook's address, the registers the hook kept, the
// BIOS's IRQ frame, IWRAM's top, the I/O and the game's stack. The
// registers the stub or the overlay changed are the game's: DISPCNT and the
// blend from the opening's read, IME and WAITCNT from the stub's copies.
// The magic goes last, so a write cut off counts as none.
EXT static void debug_take(void)
{
	const uint32_t *spend = m.call->spend;
	uint32_t words[0x60 / 4];
	memset(words, 0, 16);
	sram_out(INGAME_SRAM_SNAPSHOT, words, 16);  // no magic while the rest is written
	sram_out(INGAME_SRAM_SNAPSHOT + 0x10, (const void *)0x080000A0, 0x20);
	sram_out(INGAME_SRAM_SNAPSHOT + 0x30, &m.call->hook, 4);
	for (uint32_t i = 0; i < 0x40 / 4; i++)
		words[i] = spend[i];
	ov_system_regs(&words[INGAME_SPEND_SYS_SP / 4]);
	sram_out(INGAME_SRAM_SNAPSHOT + 0x40, words, 0x40);
	sram_out(INGAME_SRAM_SNAPSHOT + 0x80, (const void *)(spend[INGAME_SPEND_IRQ_SP / 4] & ~3u), 0x20);
	sram_out(INGAME_SRAM_SNAPSHOT + 0xA0, (const void *)0x03007FF0, 0x10);
	uint16_t *io = (uint16_t *)(void *)words;
	for (uint32_t i = 0; i < 0x60 / 2; i++)
		io[i] = REG16(0x04000000 + i * 2);
	io[0] = m.io[0];  // DISPCNT, BLDCNT and BLDALPHA as the opening read them (io_readable's)
	io[0x28] = m.io[9];
	io[0x29] = m.io[10];
	sram_out(INGAME_SRAM_SNAPSHOT + 0xC0, words, 0x60);
	for (uint32_t i = 0; i < 0x10 / 2; i++)
		io[i] = REG16(0x04000200 + i * 2);
	const uint16_t *halves = (const uint16_t *)(const void *)spend;
	io[2] = halves[INGAME_SPEND_WAITCNT / 2];
	io[4] = halves[INGAME_SPEND_IME / 2];
	sram_out(INGAME_SRAM_SNAPSHOT + 0x120, words, 0x10);
	sram_out(INGAME_SRAM_SNAPSHOT + 0x200, (const void *)0x03007E00, 0x200);
	ov_system_regs(words);
	sram_out(INGAME_SRAM_SNAPSHOT + 0x400, (const void *)(words[0] & ~3u), 0x100);
	static const char magic[16] __attribute__((aligned(4))) = INGAME_SNAPSHOT_MAGIC;  // sram_copy takes words
	sram_out(INGAME_SRAM_SNAPSHOT, magic, 16);
	debug_print("ingame: debug snapshot saved");
}

uint32_t ov_entry(const struct ingame_call *call);

uint32_t ov_entry(const struct ingame_call *call)
{
	uint32_t bss_words = (uint32_t)((uintptr_t)__ov_bss_end - (uintptr_t)__ov_bss_start) / 4;
	for (uint32_t i = 0; i < bss_words; i++)
		__ov_bss_start[i] = 0;
	m.call = call;
	m.flags = call->flags;
	for (uint32_t i = 0; i < sizeof(io_readable); i++)  // before the menu changes them
		m.io[i] = REG16(0x04000000 + io_readable[i] * 2);
	ov_header.shown = 0;
	ov_header.obj_bytes = 0;
	hide_all();  // the game's sprites go at once
	sram_in(INGAME_SRAM_SESSION, session_words, sizeof(session_words));
	const struct ingame_session *s = session();
	m.have_session = s->magic == INGAME_SESSION_MAGIC && s->version == INGAME_SESSION_VERSION;
	uint32_t menu_key = m.have_session && s->menu_key ? s->menu_key : MENU_KEY_DEFAULT;
	// Torii (v0.9.17): In-game menu Off. An SD game's hook never gets here, but a
	// NOR game's has the key it was written with: give the game back, once the keys
	// are up so the hook doesn't open it again at once
	if (m.have_session && (s->flags & INGAME_MENU_OFF)) {
		wait_release(menu_key);
		return INGAME_ACT_RESUME;
	}
	cheats_load();
	states_load();
	search_load();
	if (m.have_session)  // screenshots taken at earlier openings still wait
		shots_load();

	build_page(PAGE_MENU, 0);
	uint16_t dispcnt = REG_DISPCNT;
	// The panel at the next VBlank, or in this one if it has lines left:
	// the stub's copies end anywhere in the frame, and waiting out a VBlank
	// already begun cost the opening a frame
	if (REG_VCOUNT < SCREEN_H || REG_VCOUNT > VBLANK_LATE)
		vsync();
	REG_BLDCNT = 0x3F00;  // every layer a second target: the cover blends over them
	REG_BLDALPHA = DIM_EVB << 8;
	REG_DISPCNT = (uint16_t)((dispcnt & ~DISPCNT_OFF_BITS) | DISPCNT_OBJ | DISPCNT_1D);
	ov_header.shown = 1;
	// The title and rows, then the footer a frame later: its 40 tiles to
	// back up kept the rows from showing within a frame of the panel. So
	// do the lines past the seventh (the cheat search's row, v0.9, made
	// eight in a game with states and no cheat list): they go with it.
	uint32_t rows = m.title + m.visible, first = rows < FIRST_LINES ? rows : FIRST_LINES;
	build_text(0, first);
	vsync();
	ov_header.shown = 2;
	build_text(first, rows + m.footer);
	screen_ready(PAGE_MENU);

	wait_release(menu_key);
	uint32_t act = run(menu_key);
	// Run once as the game goes on (not into a state saved now, which would
	// run it again at each load)
	if (m.once && (act == INGAME_ACT_RESUME || act == INGAME_ACT_SLEEP))
		*(volatile uint8_t *)INGAME_CHEAT_ONCE = (uint8_t)(m.cheat[m.once - 1].bit + 1);
	else if (act == INGAME_ACT_SAVE)
		*(volatile uint8_t *)INGAME_CHEAT_ONCE = 0;  // one the game hasn't taken yet isn't saved
	if (act == INGAME_ACT_SAVE && searching())
		search_drop();  // the state takes the buffer the search was in
	if (act == INGAME_ACT_RESET && m.found_live) {  // the BIOS's SoftReset clears the live block
		uint32_t head[2];
		sram_in(INGAME_SRAM_FOUND, head, sizeof(head));
		head[1] &= 0xFFFF;
		sram_out(INGAME_SRAM_FOUND, head, sizeof(head));
	}
	ext_unload();
	answer(act);
	wait_release(KEYS_ALL);
	vsync();
	hide_all();
	ov_header.shown = 0;
	return act;
}
