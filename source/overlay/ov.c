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
extern uint32_t __ov_bss_start[], __ov_bss_end[];

// --- Layout -------------------------------------------------------------------

#define SCREEN_W 240
#define SCREEN_H 160
#define PANEL_PAD 8
#define ROW_H 16
#define TITLE_GAP 4
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

// OBJ tiles from INGAME_OBJ_TILE: one block of solid tiles that every
// filled shape shares (a 64x32 sprite's 32), the panel's corner, the pill's
// end cap (8x16), the check mark, then the lines of text:
// as many as each needs on the menu, SLOT_TILES each on the cheat list,
// whose rows scroll
#define TILE_SOLID 0
#define TILE_CORNER 32
#define TILE_CAP 33
#define TILE_CHECK 35
#define TILE_TEXT 36
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
};

// The search page's rows, before a search and during one
enum {
	SR_SIZE,
	SR_START,
	SR_IDLE_ROWS
};
enum {
	SR_SAME,
	SR_CHANGED,
	SR_UP,
	SR_DOWN,
	SR_EQUALS,
	SR_SHOW,
	SR_NEW,
	SR_ROWS
};
_Static_assert((int)SR_SAME == (int)CHEATSEARCH_SAME && (int)SR_DOWN == (int)CHEATSEARCH_DOWN &&
		       (int)SR_EQUALS == (int)CHEATSEARCH_EQUALS,
	       "the filters' rows are cheatsearch_op's");
// A candidate's page
enum {
	PR_VALUE,
	PR_KEEP,
	PR_KEEP_ON,
	PR_ROWS
};

// The cheat list: the kernel's entries (but their lines) and what is on
#define LIST_FIXED 3  // "All off", "Back to my picks" and "Cheat search" before the cheats
#define LIST_SEARCH 2
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
	uint32_t edit, edit_place, edit_value;  // a number being edited (edit 1): its digit, its value
	uint32_t busy;  // a pass runs: the footer says so
	uint32_t notice;  // a search page's footer until the page changes (a text), 0 none
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
_Static_assert(sizeof(shapes) == (TILE_TEXT - TILE_CORNER) * 32, "a tile each, the cap two");

// A rectangle of solid sw x sh sprites, the last ones overlapping
static void obj_rect(int x0, int y0, int w, int h, int sw, int sh, uint32_t shape, uint32_t size, uint32_t bank,
		     uint32_t a0)
{
	for (int y = y0;; y += sh) {
		if (y > y0 + h - sh)
			y = y0 + h - sh;
		for (int x = x0;; x += sw) {
			if (x > x0 + w - sw)
				x = x0 + w - sw;
			obj(x, y, shape, size, TILE_SOLID, bank, a0, 0);
			if (x == x0 + w - sw)
				break;
		}
		if (y == y0 + h - sh)
			break;
	}
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
__attribute__((noinline)) static void slot_draw_wide(const struct slot *s, uint32_t e, uint32_t px, uint32_t bank)
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

// The cheats' load with the entries on now (cheatcode.h's CHEAT_LOAD model)
static uint32_t cheats_cost(void)
{
	const struct ingame_session *s = session();
	uint32_t total = s->cheat_call + s->cheat_table * (uint32_t)s->cheat_skip;
	for (uint32_t e = 0; e < m.cheats; e++)
		if ((m.cheat[e].flags & INGAME_CHEAT_TABLE) && bit_of(m.on, e))
			total += m.cheat[e].cost - (uint32_t)s->cheat_skip;
	return total;
}

// Why entry e can't change now: its own reason, or the cycle cap for one off
static uint32_t cheat_why_now(uint32_t e)
{
	uint32_t why = cheat_why(e);
	if (why != INGAME_WHY_NONE || bit_of(m.on, e))
		return why;
	const struct ingame_session *s = session();
	return cheats_cost() + m.cheat[e].cost - s->cheat_skip > s->cheat_max ? INGAME_WHY_SLOW : INGAME_WHY_NONE;
}

// 1 if entry e can run once now (SELECT): off, its code waits on no buttons
// of its own, and A could turn it on (one too slow beside those on shows why)
static int cheat_once_ok(uint32_t e)
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
static int slot_compose(uint32_t k, struct ingame_line *l);
static int search_greyed(uint32_t r);
static uint32_t search_row_text(uint32_t r);
static uint32_t search_footer(void);

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
		return m.greyed[r];
	if (m.page == PAGE_SAVE)
		return 0;
	if (m.page == PAGE_LOAD)
		return r + 1 != m.buffer_slot;  // only the buffer's slot loads in game
	if (m.page >= PAGE_SEARCH)
		return search_greyed(r);
	return r >= LIST_FIXED && cheat_why_now(r - LIST_FIXED) != INGAME_WHY_NONE;
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
	if (!m.shots)
		return state ? INGAME_TEXT_UNSAVED_STATE : m.found ? INGAME_TEXT_UNSAVED_FOUND : 0;
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
		return r == 0             ? text_at(INGAME_TEXT_ALL_OFF)
		       : r == 1           ? text_at(INGAME_TEXT_AS_LAUNCHED)
		       : r == LIST_SEARCH ? text_at(INGAME_TEXT_SEARCH)
					  : INGAME_SRAM_SESSION + INGAME_SESSION_CHEAT(r - LIST_FIXED);
	uint32_t kind = m.kinds[r];
	if (kind == ROW_TORII && unsaved_text())
		return text_at(INGAME_TEXT_TORII_SAVE);
	if (kind == ROW_SHOT && m.shots)
		return text_at(INGAME_TEXT_SHOT_COUNT + m.shots - 1);
	if (kind == ROW_CHEATS)
		return text_at(m.cheats                                       ? INGAME_TEXT_CHEATS
			       : *(volatile uint8_t *)INGAME_CHEAT_ONOFF == 1 ? INGAME_TEXT_CHEATS_OFF
									      : INGAME_TEXT_CHEATS_ON);
	return text_at(row_texts[kind]);
}

// The footer's text: why the row under the cursor can't change, else that
// the toggles are kept
static uint32_t footer_text(void)
{
	if (m.page >= PAGE_SEARCH)
		return search_footer();
	if (m.page == PAGE_SAVE && m.search.magic)
		return text_at(INGAME_TEXT_SEARCH_SAVE_ENDS);
	if (m.page == PAGE_SAVE)
		return text_at(buffer_unkept() && m.cursor + 1 != m.buffer_slot ? INGAME_TEXT_STATE_REPLACES
										: INGAME_TEXT_STATE_KEEP);
	if (m.page == PAGE_LOAD)
		return text_at(!row_greyed(m.cursor)                        ? INGAME_TEXT_STATE_LOAD
			       : (session()->state_have & (1u << m.cursor)) ? INGAME_TEXT_STATE_ELSEWHERE
									    : INGAME_TEXT_STATE_EMPTY);
	// A cheat's reason it can't change, else its activation's sentence
	uint32_t why = INGAME_WHY_NONE;
	if (m.cursor >= LIST_FIXED) {
		uint32_t e = m.cursor - LIST_FIXED;
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

// The menu's footer: the DEBUG notice, that no screenshot more fits or one
// was just taken, on the Sleep row the keys that wake the game; on the
// Save state and Screenshot rows at the session's first opening that Back
// to Torii saves them to the card, and on the Save state row that the
// state saved in game is lost if the console goes off first; what waits
// for Back to Torii, or the keys that close the menu
static uint32_t menu_footer(void)
{
	uint32_t kind = m.rows ? m.kinds[m.cursor] : ROW_KINDS;
	return (m.flags & NOTICE_DEBUG)                                    ? INGAME_TEXT_DEBUG_SAVED
	       : m.shot_full                                               ? INGAME_TEXT_SHOT_FULL
	       : m.shot_now                                                ? INGAME_TEXT_SHOT_SAVED
	       : kind == ROW_SLEEP                                         ? INGAME_TEXT_SLEEP_WAKES
	       : (kind == ROW_SAVE || kind == ROW_SHOT) && first_opening() ? INGAME_TEXT_FIRST_SAVE
	       : kind == ROW_SAVE && m.search.magic                        ? INGAME_TEXT_SEARCH_SAVE_ENDS
	       : kind == ROW_SAVE && m.states && buffer_unkept()           ? INGAME_TEXT_STATE_SAVED
	       : unsaved_text()                                            ? unsaved_text()
									   : INGAME_TEXT_HINT;
}

// Slot k's line now, as its place in cart SRAM (0: none)
static uint32_t slot_text(uint32_t k)
{
	if (m.title && k == 0)
		return text_at(m.page == PAGE_CHEATS   ? INGAME_TEXT_CHEATS
			       : m.page >= PAGE_SEARCH ? INGAME_TEXT_SEARCH
			       : m.page == PAGE_SAVE   ? INGAME_TEXT_SAVE
			       : m.page == PAGE_LOAD   ? INGAME_TEXT_LOAD
						       : INGAME_TEXT_TITLE);
	if (m.footer && k == m.title + m.visible)
		return m.page == PAGE_MENU ? text_at(menu_footer()) : footer_text();
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
	line_tiles(&m.slots[k], &line, bank);
}

static void slot_update(uint32_t k)
{
	const struct slot *s = &m.slots[k];
	int r = slot_row(k);
	// The Save state page's row for the slot in the buffer (the one Load
	// state gives back) in the accent
	int current = m.page == PAGE_SAVE && r >= 0 && (uint32_t)r + 1 == m.buffer_slot;
	uint32_t bank = (m.title && k == 0) || current         ? BANK_TITLE
			: m.footer && k == m.title + m.visible ? BANK_FAINT
			: r >= 0 && row_greyed((uint32_t)r)    ? BANK_FAINT
							       : BANK_TEXT;
	if ((m.mq == MQ_RUN || m.mq == MQ_END) && r >= 0 && (uint32_t)r == m.cursor) {
		slot_draw_wide(s, m.cursor - LIST_FIXED, m.mq_px, bank);
	} else {
		slot_draw(k, bank);
	}
	if (s->check) {
		uint32_t o = s->check - 1u;
		int on = r >= (int)LIST_FIXED && bit_of(m.on, (uint32_t)r - LIST_FIXED);
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
	for (uint32_t i = 0; i < 2 + PILL_FILLS; i++) {
		volatile uint16_t *o = OAM + (m.pill_oam + i) * 4;
		*o = (uint16_t)((*o & ~0xFFu) | ((uint32_t)y & 0xFF));
	}
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
		m.rows = page == PAGE_LIST   ? m.list_n
			 : page == PAGE_PICK ? PR_ROWS
			 : m.search.magic    ? SR_ROWS
					     : SR_IDLE_ROWS;
		m.visible = m.rows < LIST_ROWS ? m.rows : LIST_ROWS;
		m.title = 1;
		m.footer = 1;
		panel_w = LIST_W;
	} else {
		m.rows = LIST_FIXED + m.cheats;
		m.visible = m.rows < LIST_ROWS ? m.rows : LIST_ROWS;
		m.title = 1;
		m.footer = 1;
		panel_w = LIST_W;
	}
	uint32_t h = PANEL_PAD * 2 + (m.title ? ROW_H + TITLE_GAP : 0) + (m.visible + m.footer) * ROW_H;
	int x0 = (SCREEN_W - (int)panel_w) / 2, y0 = (SCREEN_H - (int)h) / 2;
	uint32_t slots = m.title + m.visible + m.footer;

	tiles_need(TILE_TEXT);
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

	// Sprites, front to back: text and check marks (hidden until drawn),
	// the pill, the panel, the cover
	hide_all();
	m.oam_next = 0;
	int y = y0 + PANEL_PAD;
	uint32_t tile = TILE_TEXT;
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
		} else if (m.footer && k == m.title + m.visible)
			max = WIDE_SPRITES;  // the list's footer: a line of the menu's width (an activation's sentence)
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
	m.cursor = cursor < m.rows ? cursor : 0;
	if (page == PAGE_MENU && row_greyed(m.cursor))
		m.cursor = next_row(m.cursor, 1);
	if (m.cursor >= m.top + m.visible)
		m.top = m.cursor - m.visible + 1;
	m.pill_y = m.pill_to = row_y(m.cursor);
	m.pill_oam = m.oam_next;
	int pill_x = x0 + PILL_INSET, pill_w = (int)panel_w - 2 * PILL_INSET;
	obj(pill_x, m.pill_y, SHAPE_TALL, 0, TILE_CAP, BANK_PILL, 0, 0);
	obj(pill_x + pill_w - 8, m.pill_y, SHAPE_TALL, 0, TILE_CAP, BANK_PILL, 0, A1_HFLIP);
	// The widest pill's fills, those this one doesn't need hidden
	int fills = (pill_w - 16 + 31) / 32;
	for (int x = pill_x + 8, k = 0; k < PILL_FILLS; k++, x += 32) {
		int at = x > pill_x + pill_w - 8 - 32 ? pill_x + pill_w - 8 - 32 : x;
		obj(at, m.pill_y, SHAPE_WIDE, 2, TILE_SOLID, BANK_PILL, k < fills ? 0 : A0_HIDE, 0);
	}
	int x1 = x0 + (int)panel_w, y1 = y0 + (int)h;
	obj(x0, y0, SHAPE_SQUARE, 0, TILE_CORNER, BANK_PANEL, 0, 0);
	obj(x1 - 8, y0, SHAPE_SQUARE, 0, TILE_CORNER, BANK_PANEL, 0, A1_HFLIP);
	obj(x0, y1 - 8, SHAPE_SQUARE, 0, TILE_CORNER, BANK_PANEL, 0, A1_VFLIP);
	obj(x1 - 8, y1 - 8, SHAPE_SQUARE, 0, TILE_CORNER, BANK_PANEL, 0, A1_HFLIP | A1_VFLIP);
	obj_rect(x0 + 8, y0, (int)panel_w - 16, 8, 32, 8, SHAPE_WIDE, 1, BANK_PANEL, 0);
	obj_rect(x0 + 8, y1 - 8, (int)panel_w - 16, 8, 32, 8, SHAPE_WIDE, 1, BANK_PANEL, 0);
	obj_rect(x0, y0 + 8, (int)panel_w, (int)h - 16, 64, 32, SHAPE_WIDE, 3, BANK_PANEL, 0);
	obj_rect(0, 0, SCREEN_W, SCREEN_H, 64, 32, SHAPE_WIDE, 3, BANK_DIM, A0_SEMI);
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

// Opens page at row cursor: the panel this frame, its text the next
static void page_open(enum page_id page, uint32_t cursor)
{
	vsync();
	m.mq = MQ_OFF;
	m.notice = 0;
	m.edit = 0;
	build_page(page, cursor);
	build_text(0, m.title + m.visible + m.footer);
	marquee_start();
	if (page == PAGE_CHEATS)
		debug_print("screen ready: ingame-cheats");
	else if (page == PAGE_SAVE)
		debug_print("screen ready: ingame-save");
	else if (page == PAGE_LOAD)
		debug_print("screen ready: ingame-load");
	else if (page == PAGE_SEARCH)
		debug_print("screen ready: ingame-search");
	else if (page == PAGE_LIST)
		debug_print("screen ready: ingame-search-list");
	else if (page == PAGE_PICK)
		debug_print("screen ready: ingame-search-pick");
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
	if (m.page == PAGE_CHEATS && m.cursor >= LIST_FIXED &&
	    (m.cheat[m.cursor - LIST_FIXED].flags & INGAME_CHEAT_CUT))
		m.mq = MQ_WAIT;
}

// A frame of the marquee
static void marquee_tick(void)
{
	if (m.mq == MQ_OFF || !m.text_shown)
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
		sram_in(INGAME_SRAM_WIDE + (m.cursor - LIST_FIXED) * sizeof(struct ingame_wide), &head, 4);
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

// Moves the cursor to row r, scrolling the list to show it
static void cursor_to(uint32_t r)
{
	// The menu's footer changes with the row on some rows
	uint32_t footer = m.page == PAGE_MENU ? menu_footer() : 0;
	int was = marquee_stop();
	if (was >= 0)
		slot_update((uint32_t)was);  // the row it left, cut again
	m.cursor = r;
	uint32_t top = m.top;
	if (r < top)
		top = r;
	else if (r >= top + m.visible)
		top = r - m.visible + 1;
	if (top != m.top) {
		m.top = top;
		slots_update();  // the rows' lines in their slots again
		m.pill_y = row_y(r);  // the pill jumps with the scroll
		pill_place(m.pill_y);
	} else if (m.footer && (m.page != PAGE_MENU || menu_footer() != footer)) {
		slot_update(m.title + m.visible);
	}
	m.pill_to = row_y(r);
	marquee_start();
}

// A on the cheat list's row r
static void list_pick(uint32_t r)
{
	if (r == 0 || r == 1) {
		for (uint32_t e = 0; e < m.cheats; e++)
			if (cheat_why(e) == INGAME_WHY_NONE && (m.cheat[e].flags & INGAME_CHEAT_TABLE))
				bit_set(m.on, e, r == 1 && bit_of(m.launched, e));
	} else {
		uint32_t e = r - LIST_FIXED;
		if (cheat_why_now(e) != INGAME_WHY_NONE || !(m.cheat[e].flags & INGAME_CHEAT_TABLE))
			return;
		bit_set(m.on, e, !bit_of(m.on, e));
	}
	if (m.once && !cheat_once_ok(m.once - 1))
		m.once = 0;  // on now: it runs anyway
	cheats_store();
	slots_update();  // the greyed rows change with what is on
}

// --- The cheat search (v0.9) -------------------------------------------------

// Most of the search is an extension the overlay loads itself as the search
// page opens (ext_load), after the window, so the stub's copies at every
// opening stay as they were: in the window it slowed the menu's opening by
// two frames. EXT code and data are linked there (ov.ld's .ext); the rest
// may call it only while it is loaded (a search page is open).
#define EXT __attribute__((section(".ext.text"), noinline))
#define EXT_DATA __attribute__((section(".ext.data")))
extern uint32_t __ov_ext_start[], __ov_ext_end[];
extern const uint32_t __ov_ext_load[];
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
	uint32_t now[CHUNK / 4], before[CHUNK / 4], bits[CHUNK / 8 / 4];
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

EXT_DATA static uint32_t search_found[INGAME_SEARCH_LIST];  // the list's candidates' addresses
// The log's lines, out of the window
EXT_DATA static char log_pass[] = "search: pass in ", log_frames[] = " frames",
		     log_search[] = "search: ", log_left[] = " left", log_started[] = "search: started",
		     log_kept[] = "search: kept a cheat", log_kept_on[] = "search: kept a cheat, on";
EXT_DATA static struct ingame_line spare;  // a line's second part, off the stack (the window's stack is small)
EXT_DATA static struct ingame_glyph glyphs[INGAME_GLYPHS];  // read as the search opens

static int searching(void)
{
	return m.search.magic == INGAME_SEARCH_MAGIC;
}

EXT static uint32_t search_bytes(void)
{
	return m.search.magic ? m.search.size : 1u << m.search_size;
}

// The line of text t (an INGAME_TEXT_*) from the session block
EXT static void line_read(uint32_t t, struct ingame_line *l)
{
	if (m.have_session)
		sram_in(text_at(t), l, sizeof(*l));
	else
		memset(l, 0, sizeof(*l));
}

// Row bits v (bit 0 the leftmost) into l's row from pixel x on
EXT static void row_or(struct ingame_line *l, uint32_t row, uint32_t x, uint32_t v)
{
	uint32_t at = x >> 3;
	v <<= x & 7;
	for (; v && at < INGAME_TEXT_W / 8; at++, v >>= 8)
		l->bits[row][at] |= (uint8_t)v;
}

EXT static void width_to(struct ingame_line *l, uint32_t x)
{
	x = x < INGAME_TEXT_W ? x : INGAME_TEXT_W;
	if (x > l->width)
		l->width = (uint16_t)x;
}

// src's pixels ORed into l from pixel x on
EXT static void line_or(struct ingame_line *l, const struct ingame_line *src, uint32_t x)
{
	uint32_t bytes = ((uint32_t)src->width + 7) / 8;
	for (uint32_t row = 0; row < INGAME_TEXT_ROWS; row++)
		for (uint32_t c = 0; c < bytes && c < INGAME_TEXT_W / 8; c++)
			row_or(l, row, x + c * 8, src->bits[row][c]);
	width_to(l, x + src->width);
}

// Glyph g at x, underlined if mark; the x after it
EXT static uint32_t glyph_put(struct ingame_line *l, uint32_t x, uint32_t g, int mark)
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
EXT static uint32_t number_put(struct ingame_line *l, uint32_t x, uint32_t value, uint32_t n, int sep, uint32_t mark)
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

EXT static uint32_t hex_put(struct ingame_line *l, uint32_t x, uint32_t value)
{
	for (int sh = 28; sh >= 0; sh -= 4)
		x = glyph_put(l, x, value >> sh & 15, 0);
	return x;
}

// Text t's line with a number in its place: t's text, the number, then
// t + 1's (the text after it)
EXT static void line_number(struct ingame_line *l, uint32_t t, uint32_t value, uint32_t n, int sep, uint32_t mark)
{
	line_read(t, l);
	uint32_t x = number_put(l, l->width, value, n, sep, mark);
	line_read(t + 1, &spare);
	if (spare.width)
		line_or(l, &spare, x);
}

// The game's value of size bytes at addr: the window's from the stub's
// backup of it, which the stub puts back
EXT static uint32_t value_read(uint32_t addr, uint32_t size)
{
	uint32_t word;
	if (addr - INGAME_EWRAM < window_end() - INGAME_EWRAM)
		sram_in(INGAME_BACKUP_EWRAM + ((addr - INGAME_EWRAM) & ~3u), &word, 4);
	else
		word = *(const volatile uint32_t *)(addr & ~3u);
	return word >> (addr & 3) * 8 & cheatsearch_max(size);
}

EXT static void value_write(uint32_t addr, uint32_t size, uint32_t value)
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
EXT static void ram_read(uint32_t at, uint32_t *buf, uint32_t n)
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
EXT static void live_read(uint32_t *w)
{
	const volatile uint32_t *b = (const volatile uint32_t *)m.live;
	for (uint32_t i = 0; i < 1 + 2 * INGAME_LIVE_PAIRS; i++)
		w[i] = b[i];
	if (ingame_live_check(&w[1]) != w[0])
		memset(w, 0, (1 + 2 * INGAME_LIVE_PAIRS) * 4);
}

// The live pair addr takes: its own, else a free one; INGAME_LIVE_PAIRS: none
EXT static uint32_t live_pair(const uint32_t *w, uint32_t addr)
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

EXT static int live_full(void)
{
	uint32_t w[1 + 2 * INGAME_LIVE_PAIRS];
	if (!m.live || m.list_n <= m.pick)
		return 0;
	live_read(w);
	return live_pair(w, search_found[m.pick]) == INGAME_LIVE_PAIRS;
}

EXT static void live_add(uint32_t addr, uint32_t size, uint32_t value)
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
}

EXT static void debug_number(const char *text, uint32_t n, const char *after)
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
	// A search of this launch's, which no state saved since has overwritten
	if (m.search.magic != INGAME_SEARCH_MAGIC || !m.have_session || m.search.launch_id != session()->launch_id ||
	    (m.search.size != 1 && m.search.size != 2 && m.search.size != 4) || state_saved())
		m.search.magic = 0;
	uint32_t head[2];
	sram_in(INGAME_SRAM_FOUND, head, sizeof(head));
	m.found = head[0] == INGAME_FOUND_MAGIC && (head[1] & 0xFFFF) <= INGAME_FOUND_MAX ? head[1] & 0xFFFF : 0;
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
// from the image in cart SRAM), into its backup and the code in (on), or
// back (off)
static void iwram_borrow(int on)
{
	const uint32_t borrowed = (uint32_t)(uintptr_t)__ov_fastext_end - INGAME_SHOT_IWRAM;
	if (on) {
		sram_out(INGAME_BACKUP_SHOT, (const void *)INGAME_SHOT_IWRAM, borrowed);
		sram_in(INGAME_SRAM_IMAGE + ((uint32_t)(uintptr_t)__ov_fastext_load - INGAME_EWRAM), __ov_fastext_start,
			(uint32_t)((uintptr_t)__ov_fastext_end - (uintptr_t)__ov_fastext_start));
		iwram_borrowed = borrowed;
	} else {
		sram_in(INGAME_BACKUP_SHOT, (void *)INGAME_SHOT_IWRAM, iwram_borrowed);
		iwram_borrowed = 0;
	}
}

// A pass over the searched run: start remembers it all, every element a
// candidate; else op narrows the candidates. Chunks without candidates are
// skipped. VBlanks are counted for the log.
EXT static void search_pass(int start, uint32_t op, uint32_t value)
{
	uint32_t size = m.search.size, bits = CHUNK / size / 8, left = 0, frames = 0, line = REG_VCOUNT;
	struct search_scratch *k = (struct search_scratch *)INGAME_SHOT_IWRAM;
	uint32_t *chunk_now = k->now, *chunk_before = k->before, *chunk_bits = k->bits;
	iwram_borrow(1);
	for (uint32_t at = 0; at < CHEATSEARCH_BYTES; at += CHUNK) {
		uint32_t bits_at = INGAME_SRAM_SEARCH_BITS + at / size / 8, now = REG_VCOUNT;
		frames += now < line;
		line = now;
		if (start) {
			memset(chunk_bits, 0xFF, bits);
			left += CHUNK / size;
		} else {
			sram_in(bits_at, chunk_bits, bits);
			uint32_t any = 0;
			for (uint32_t i = 0; i < bits / 4; i++)
				any |= chunk_bits[i];
			if (!any)
				continue;
			if (op == CHEATSEARCH_EQUALS)
				cheatsearch_fill((uint8_t *)chunk_before, CHUNK, size, value);  // no snapshot to read
			else
				sram_in(INGAME_SRAM_SEARCH_SNAP + at, chunk_before, CHUNK);
		}
		ram_read(at, chunk_now, CHUNK);
		uint32_t kept = CHUNK / size;
		if (!start) {
			kept = cheatsearch_narrow((const uint8_t *)chunk_now, (const uint8_t *)chunk_before,
						  (uint8_t *)chunk_bits, CHUNK, size, op);
			left += kept;
		}
		// The snapshot only where candidates are left: the rest is never read again
		if (kept)
			sram_out(INGAME_SRAM_SEARCH_SNAP + at, chunk_now, CHUNK);
		sram_out(bits_at, chunk_bits, bits);
	}
	iwram_borrow(0);
	m.search.left = left;
	debug_number(log_pass, frames, log_frames);
	debug_number(log_search, left, log_left);
}

// The footer says Searching... while a pass runs
EXT static void search_busy(int busy)
{
	m.busy = (uint32_t)busy;
	if (m.footer)
		slot_update(m.title + m.visible);
	if (busy)
		vsync();
}

// Start: the save-state buffer's state let go (Load state has nothing in
// cart SRAM then), the RAM remembered, the head with its magic last
EXT static void search_start(void)
{
	uint32_t zeros[4] = {0, 0, 0, 0};
	search_busy(1);
	m.search.magic = 0;
	search_save();
	sram_out(INGAME_SRAM_STATE_MARK, zeros, sizeof(zeros));
	sram_out(INGAME_SRAM_STATE_TAG, zeros, sizeof(zeros));
	m.buffer_slot = 0;
	m.search.size = (uint8_t)(1u << m.search_size);
	m.search.passes = 0;
	m.search.value = 0;
	m.search.launch_id = session()->launch_id;
	search_pass(1, 0, 0);
	m.search.magic = INGAME_SEARCH_MAGIC;
	search_save();
	m.busy = 0;
	debug_print(log_started);
}

static void search_drop(void)
{
	m.search.magic = 0;
	search_save();
}

EXT static void search_filter(uint32_t op, uint32_t value)
{
	search_busy(1);
	search_pass(0, op, value);
	m.search.passes++;
	search_save();
	m.busy = 0;
	slots_update();  // the rows greyed or not with what is left
}

// The candidates, all of them (at most INGAME_SEARCH_LIST), into the list
EXT static void search_list(void)
{
	EXT_DATA static uint32_t chunk_now[CHUNK / 4];
	uint32_t size = m.search.size, total = cheatsearch_bits_bytes(size);
	m.list_n = 0;
	for (uint32_t at = 0; at < total && m.list_n < INGAME_SEARCH_LIST; at += CHUNK) {
		uint32_t n = total - at < CHUNK ? (total - at + 3) & ~3u : CHUNK;
		const uint8_t *b = (const uint8_t *)chunk_now;
		sram_in(INGAME_SRAM_SEARCH_BITS + at, chunk_now, n);
		for (uint32_t i = 0; i < n && m.list_n < INGAME_SEARCH_LIST; i++)
			for (uint32_t k = 0; k < 8 && b[i] >> k; k++)
				if (b[i] >> k & 1 && ((at + i) * 8 + k) * size < CHEATSEARCH_BYTES &&
				    m.list_n < INGAME_SEARCH_LIST)
					search_found[m.list_n++] = cheatsearch_address(((at + i) * 8 + k) * size);
	}
}

// Keeps the picked candidate as a found cheat (on: and on now)
EXT static void search_keep(int on)
{
	if (m.found >= INGAME_FOUND_MAX || m.pick >= m.list_n || (on && live_full()))
		return;
	uint32_t addr = search_found[m.pick], size = m.search.size, value = m.pick_value & cheatsearch_max(size);
	struct ingame_found_cheat c = {addr, value, (uint8_t)size, (uint8_t)(on ? INGAME_FOUND_ON : 0), 0};
	sram_out(INGAME_SRAM_FOUND + offsetof(struct ingame_found, cheat) + m.found * sizeof(c), &c, sizeof(c));
	uint32_t head[2] = {INGAME_FOUND_MAGIC, m.found + 1};
	sram_out(INGAME_SRAM_FOUND, head, sizeof(head));
	m.found++;
	if (on) {
		if (m.live)
			live_add(addr, size, value);
		value_write(addr, size, value);  // at once; without the live block, this once
	}
	m.notice = INGAME_TEXT_SEARCH_KEPT;
	debug_print(on ? log_kept_on : log_kept);
	slots_update();
}

EXT static int search_greyed(uint32_t r)
{
	if (m.page == PAGE_SEARCH) {
		if (!searching())
			return r == SR_START && m.states && buffer_unkept();
		return (r <= SR_EQUALS && !m.search.left) ||
		       (r == SR_SHOW && !(m.search.left && m.search.left <= INGAME_SEARCH_LIST));
	}
	if (m.page == PAGE_PICK)
		return (r != PR_VALUE && m.found >= INGAME_FOUND_MAX) || (r == PR_KEEP_ON && live_full());
	return 0;
}

EXT static uint32_t search_row_text(uint32_t r)
{
	if (m.page == PAGE_SEARCH && !searching())
		return text_at(r == SR_SIZE ? INGAME_TEXT_SEARCH_SIZE + m.search_size : INGAME_TEXT_SEARCH_START);
	if (m.page == PAGE_SEARCH)
		return text_at(r <= SR_EQUALS ? INGAME_TEXT_SEARCH_SAME + r
			       : r == SR_SHOW ? INGAME_TEXT_SEARCH_SHOW
					      : INGAME_TEXT_SEARCH_NEW);
	if (m.page == PAGE_PICK)
		return text_at(r == PR_VALUE  ? INGAME_TEXT_SEARCH_VALUE
			       : r == PR_KEEP ? INGAME_TEXT_SEARCH_KEEP
			       : m.live       ? INGAME_TEXT_SEARCH_KEEP_ON
					      : INGAME_TEXT_SEARCH_KEEP_SET);
	return 0;
}

// The search pages' footer, a text; 0 for the candidates left ("1,204 left")
EXT static uint32_t search_footer(void)
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
	else if (m.page == PAGE_SEARCH && !searching())
		t = m.cursor != SR_START || !m.states ? INGAME_TEXT_SEARCH_HINT
		    : buffer_unkept()                 ? INGAME_TEXT_SEARCH_STATE_FIRST
		    : m.buffer_slot                   ? INGAME_TEXT_SEARCH_DROPS
						      : INGAME_TEXT_SEARCH_HINT;
	else if (m.page == PAGE_PICK && m.cursor == PR_KEEP_ON && live_full())
		t = INGAME_TEXT_SEARCH_LIVE_FULL;
	else if (!m.search.left)
		t = INGAME_TEXT_SEARCH_NONE;
	else if (m.page == PAGE_SEARCH && m.cursor == SR_SHOW && m.search.left > INGAME_SEARCH_LIST)
		t = INGAME_TEXT_SEARCH_FEW;
	return t ? text_at(t) : 0;
}

// Slot k's line when the search draws it (its numbers): 1, in l
EXT static int slot_compose(uint32_t k, struct ingame_line *l)
{
	if (m.page < PAGE_SEARCH || !m.have_session)
		return 0;
	memset(l, 0, sizeof(*l));
	if (m.title && k == 0) {
		if (m.page != PAGE_PICK || m.pick >= m.list_n)
			return 0;
		hex_put(l, 0, search_found[m.pick]);
		return 1;
	}
	if (m.footer && k == m.title + m.visible) {
		if (search_footer())
			return 0;
		line_number(l, INGAME_TEXT_SEARCH_LEFT, m.search.left, 0, 1, NOT_MARKED);
		return 1;
	}
	int r = slot_row(k);
	if (r < 0)
		return 0;
	uint32_t size = search_bytes(), editing = m.edit && (uint32_t)r == m.cursor;
	uint32_t n = editing ? cheatsearch_digits(size) : 0, mark = editing ? m.edit_place : NOT_MARKED;
	if (m.page == PAGE_SEARCH && searching() && r == SR_EQUALS) {
		line_number(l, INGAME_TEXT_SEARCH_EQUALS, editing ? m.edit_value : m.search.value, n, 0, mark);
		return 1;
	}
	if (m.page == PAGE_PICK && r == PR_VALUE) {
		line_number(l, INGAME_TEXT_SEARCH_VALUE, editing ? m.edit_value : m.pick_value, n, 0, mark);
		return 1;
	}
	if (m.page == PAGE_LIST) {
		uint32_t addr = search_found[r], x = hex_put(l, 0, addr);
		number_put(l, x + LIST_GAP, value_read(addr, size), 0, 0, NOT_MARKED);
		return 1;
	}
	return 0;
}

// The game's EWRAM where the extension goes into its backup, after the
// window's (one run in cart SRAM), and the extension from the image in, if
// its check word is the kernel's; 0 if it isn't
static int ext_load(void)
{
	const uint32_t n = (uint32_t)((uintptr_t)__ov_ext_end - (uintptr_t)__ov_ext_start);
	const uint32_t at = (uint32_t)(uintptr_t)__ov_ext_start - INGAME_EWRAM;
	if (m.ext)
		return 1;
	if (!m.have_session || !n)
		return 0;
	sram_out(INGAME_BACKUP_EWRAM + at, __ov_ext_start, n);
	m.ext = n;  // the game's bytes there are in the backup from now on
	sram_in(INGAME_SRAM_IMAGE + ((uint32_t)(uintptr_t)__ov_ext_load - INGAME_EWRAM), __ov_ext_start, n);
	// The check runs on over the compare's code, which follows in the image
	uint32_t s = ingame_image_check(__ov_ext_start, n / 4), words[16];
	const uint32_t code = (uint32_t)((uintptr_t)__ov_fastext_end - (uintptr_t)__ov_fastext_start);
	for (uint32_t off = 0; off < code; off += sizeof(words)) {
		uint32_t k = code - off < sizeof(words) ? code - off : sizeof(words);
		sram_in(INGAME_SRAM_IMAGE + ((uint32_t)(uintptr_t)__ov_fastext_load - INGAME_EWRAM) + off, words, k);
		for (uint32_t i = 0; i < k / 4; i++)
			s = (s << 5 | s >> 27) + words[i];
	}
	if (s == session()->ext_check)
		return 1;
	debug_print("ingame: the cheat search's code didn't load whole");
	ext_unload();
	return 0;
}

static void search_open(enum page_id from)
{
	if (!ext_load())
		return;
	sram_in(INGAME_SRAM_GLYPHS, glyphs, sizeof(glyphs));
	m.search_from = from;
	page_open(PAGE_SEARCH, searching() ? SR_SAME : SR_START);
}

// A number edit's keys (down): the digit, its value, A done (1), B cancelled (-1)
EXT static int edit_keys(uint32_t down)
{
	uint32_t n = cheatsearch_digits(m.search.size);
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
		m.edit_value = cheatsearch_edit(m.edit_value, m.search.size, m.edit_place, (down & KEY_UP) ? 1 : -1);
	return 0;
}

EXT static void edit_begin(uint32_t value)
{
	m.edit = 1;
	m.edit_value = value & cheatsearch_max(m.search.size);
	m.edit_place = cheatsearch_digits(m.search.size) - 1;
	slots_update();
}

// A on the search pages' row r
EXT static void search_a(uint32_t r)
{
	if (search_greyed(r))
		return;
	if (m.page == PAGE_SEARCH && !searching()) {
		if (r == SR_SIZE) {
			m.search_size = (m.search_size + 1) % 3;
			slots_update();
		} else {
			search_start();
			page_open(PAGE_SEARCH, SR_SAME);
		}
	} else if (m.page == PAGE_SEARCH) {
		if (r < SR_EQUALS)
			search_filter(r, 0);
		else if (r == SR_EQUALS)
			edit_begin(m.search.value);
		else if (r == SR_SHOW) {
			search_list();
			page_open(PAGE_LIST, 0);
		} else {
			search_drop();
			page_open(PAGE_SEARCH, SR_START);
		}
	} else if (m.page == PAGE_LIST) {
		m.pick = r;
		m.pick_value = value_read(search_found[r], m.search.size);
		page_open(PAGE_PICK, PR_KEEP);
	} else if (r == PR_VALUE) {
		edit_begin(m.pick_value);
	} else {
		search_keep(r == PR_KEEP_ON);
	}
}

// A number's edit done: Equals searches for it, Value keeps it to keep
EXT static void edit_done(void)
{
	if (m.page == PAGE_SEARCH) {
		m.search.value = m.edit_value;
		search_filter(CHEATSEARCH_EQUALS, m.edit_value);
	} else {
		m.pick_value = m.edit_value;
		slots_update();
	}
}

// B on a search page: back where it was opened from
EXT static void search_back(void)
{
	if (m.page == PAGE_PICK) {
		page_open(PAGE_LIST, m.pick);
	} else if (m.page == PAGE_LIST) {
		page_open(PAGE_SEARCH, SR_SHOW);
	} else if (m.search_from == PAGE_CHEATS) {
		page_open(PAGE_CHEATS, LIST_SEARCH);
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
		if ((down & KEY_SELECT) && m.page == PAGE_CHEATS && m.cursor >= LIST_FIXED &&
		    cheat_once_ok(m.cursor - LIST_FIXED)) {
			uint32_t e = m.cursor - LIST_FIXED;
			m.once = m.once == e + 1 ? 0 : e + 1;
			if (m.footer)
				slot_update(m.title + m.visible);
			continue;
		}
		if (down & KEY_A) {
			if (m.page == PAGE_CHEATS && m.cursor == LIST_SEARCH) {
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
				debug_print("screen ready: ingame");
				continue;
			}
			if (kind == ROW_CHEATS) {
				if (m.cheats) {
					page_open(PAGE_CHEATS, LIST_FIXED);
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
	iwram_borrow(1);
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
	debug_print("screen ready: ingame");

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
	ext_unload();
	answer(act);
	wait_release(KEYS_ALL);
	vsync();
	hide_all();
	ov_header.shown = 0;
	return act;
}
