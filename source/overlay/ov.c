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
// Two pages: the menu, and the cheat list (P2), whose rows scroll under a
// title and over a footer that says why a row can't change, or that the
// toggles are kept as picks on Back to Torii.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../ingame_abi.h"
#include "../shotpack.h"

#define REG16(a) (*(volatile uint16_t *)(a))
#define REG_DISPCNT REG16(0x04000000)
#define DISPCNT_BLANK 0x0080  // forced blank: the LCD shows white
#define REG_VCOUNT REG16(0x04000006)
#define REG_BLDCNT REG16(0x04000050)
#define REG_BLDALPHA REG16(0x04000052)
#define REG_KEYINPUT REG16(0x04000130)
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
#define KEY_RIGHT 0x010
#define KEY_LEFT 0x020
#define KEY_UP 0x040
#define KEY_DOWN 0x080
#define KEYS_ALL 0x3FF
#define MENU_KEY_DEFAULT 0x308  // L+R+START
#define REPEAT_DELAY 18  // frames a direction is held before it repeats
#define REPEAT_EVERY 5

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
#define CHECK_INSET 22  // the cheat list's check box, from the panel's right edge
#define SLOT_SPRITES 5  // 32x16 sprites a row's line takes at most: 160 px
#define SLOT_TILES (SLOT_SPRITES * 8)
#define WIDE_SPRITES 6  // and the menu's title and footer: INGAME_TEXT_W
_Static_assert(WIDE_SPRITES * 32 >= INGAME_TEXT_W, "the wide lines' sprites");

// The menu: wide enough for its footer's line
#define MENU_W 192
// The cheat list: 6 rows between its title and its footer
#define LIST_W 224
#define LIST_ROWS 6

// OBJ tiles from INGAME_OBJ_TILE: one block of solid tiles that every
// filled shape shares (a 64x32 sprite's 32), the panel's corner, the pill's
// end cap (8x16), the check box empty and ticked, then the lines of text:
// as many as each needs on the menu, SLOT_TILES each on the cheat list,
// whose rows scroll
#define TILE_SOLID 0
#define TILE_CORNER 32
#define TILE_CAP 33
#define TILE_BOX 35
#define TILE_TICK 36
#define TILE_TEXT 37
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
	ROW_SHOT,
	ROW_RESET,
	ROW_SLEEP,
	ROW_TORII,
	ROW_DEBUG,
	ROW_KINDS
};

static const uint8_t row_action[ROW_KINDS] = {
	INGAME_ACT_RESUME, INGAME_ACT_SAVE,   INGAME_ACT_LOAD,
	INGAME_ACT_RESUME, INGAME_ACT_RESUME, INGAME_ACT_RESET,
	INGAME_ACT_SLEEP,  INGAME_ACT_TORII,  INGAME_ACT_RESUME /* the overlay's own */,
};
static const uint8_t row_texts[ROW_KINDS] = {
	INGAME_TEXT_RESUME, INGAME_TEXT_SAVE,  INGAME_TEXT_LOAD,  INGAME_TEXT_CHEATS_ON, INGAME_TEXT_SHOT,
	INGAME_TEXT_RESET,  INGAME_TEXT_SLEEP, INGAME_TEXT_TORII, INGAME_TEXT_DEBUG,
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
	uint8_t check;  // its check box's sprite + 1, or 0 for none
	uint16_t tile;  // its first tile, from INGAME_OBJ_TILE
};

enum page_id {
	PAGE_MENU,
	PAGE_CHEATS,
	PAGE_SAVE,  // the state slots (P3): SAVE into one
	PAGE_LOAD,  // LOAD the buffer's
};

// The cheat list: the kernel's entries (but their lines) and what is on
#define LIST_FIXED 2  // "All off" and "All as launched" before the cheats
struct cheat {
	uint16_t cost;
	uint8_t flags, bit;
};
_Static_assert(offsetof(struct ingame_cheat, flags) - offsetof(struct ingame_cheat, cost) == 6 &&
		       offsetof(struct ingame_cheat, bit) - offsetof(struct ingame_cheat, cost) == 7,
	       "cheats_load reads the cost, flags and bit as two words");

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

static void obj_tile(uint32_t i, uint32_t tile)
{
	volatile uint16_t *o = OAM + i * 4 + 2;
	*o = (uint16_t)((*o & 0xFC00) | (INGAME_OBJ_TILE + tile));
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
// circle of radius 8, 8x16), the check box empty (a rounded outline) and
// ticked (filled). Tables: drawing them pixel by pixel took a third of a
// frame from EWRAM.
static const uint32_t shapes[] = {
	0x11110000u, 0x11111100u, 0x11111110u, 0x11111110u, 0x11111111u, 0x11111111u, 0x11111111u, 0x11111111u,
	0x11100000u, 0x11111000u, 0x11111100u, 0x11111110u, 0x11111110u, 0x11111111u, 0x11111111u, 0x11111111u,
	0x11111111u, 0x11111111u, 0x11111111u, 0x11111110u, 0x11111110u, 0x11111100u, 0x11111000u, 0x11100000u,
	0x01111110u, 0x10000001u, 0x10000001u, 0x10000001u, 0x10000001u, 0x10000001u, 0x10000001u, 0x01111110u,
	0x01111110u, 0x11111111u, 0x11111111u, 0x11111111u, 0x11111111u, 0x11111111u, 0x11111111u, 0x01111110u,
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

// The line at sram (a session block offset; 0: none) into slot s's tiles,
// and as many of its sprites shown as the line needs (once the text shows)
static void slot_draw(const struct slot *s, uint32_t sram, uint32_t bank)
{
	struct ingame_line line;
	if (m.have_session && sram)
		sram_in(INGAME_SRAM_SESSION + sram, &line, sizeof(line));
	else
		memset(&line, 0, sizeof(line));
	uint32_t n = ((uint32_t)line.width + 31) / 32;
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
							 ? spread(line.bits[row][c])
							 : 0;
				}
	for (uint32_t k = 0; k < s->max; k++) {
		obj_bank(s->oam + k, bank);
		obj_shown(s->oam + k, m.text_shown && k < n);
	}
}

static uint32_t text_at(uint32_t text)
{
	return INGAME_SESSION_TEXT(text);
}

// The width of the line at sram (a session block offset)
static uint32_t line_width(uint32_t sram)
{
	uint32_t word = 0;
	if (m.have_session && sram >= text_at(0) && sram < text_at(INGAME_TEXTS))
		return session()->text_width[(sram - text_at(0)) / sizeof(struct ingame_line)];
	if (m.have_session && sram)
		sram_in(INGAME_SRAM_SESSION + sram, &word, 4);
	return word & 0xFFFF;
}

// --- The cheat list -----------------------------------------------------------

static uint32_t cheat_why(uint32_t e)
{
	return m.cheat[e].flags & INGAME_CHEAT_WHY;
}

// The cheats' cycles a frame with the entries on now (cheatcode.h's model)
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
		return state ? INGAME_TEXT_UNSAVED_STATE : 0;
	return (state ? INGAME_TEXT_UNSAVED_BOTH : INGAME_TEXT_UNSAVED_SHOTS) + m.shots - 1;
}

// Row r's text, as a session block offset
static uint32_t row_text(uint32_t r)
{
	if (m.page == PAGE_SAVE || m.page == PAGE_LOAD)
		return INGAME_SESSION_STATE(r + 1, !(r + 1 == m.buffer_slot && buffer_unkept()));
	if (m.page == PAGE_CHEATS)
		return r == 0   ? text_at(INGAME_TEXT_ALL_OFF)
		       : r == 1 ? text_at(INGAME_TEXT_AS_LAUNCHED)
				: INGAME_SESSION_CHEAT(r - LIST_FIXED);
	uint32_t kind = m.kinds[r];
	if (kind == ROW_TORII && unsaved_text())
		return text_at(INGAME_TEXT_TORII_SAVE);
	if (kind == ROW_SHOT && m.shots)
		return text_at(INGAME_TEXT_SHOT_COUNT + m.shots - 1);
	if (kind == ROW_CHEATS)
		return text_at(m.cheats                                        ? INGAME_TEXT_CHEATS
			       : *(volatile uint32_t *)INGAME_CHEAT_ONOFF == 1 ? INGAME_TEXT_CHEATS_OFF
									       : INGAME_TEXT_CHEATS_ON);
	return text_at(row_texts[kind]);
}

// The footer's text: why the row under the cursor can't change, else that
// the toggles are kept
static uint32_t footer_text(void)
{
	if (m.page == PAGE_SAVE)
		return text_at(buffer_unkept() && m.cursor + 1 != m.buffer_slot ? INGAME_TEXT_STATE_REPLACES
										: INGAME_TEXT_STATE_KEEP);
	if (m.page == PAGE_LOAD)
		return text_at(!row_greyed(m.cursor)                        ? INGAME_TEXT_STATE_LOAD
			       : (session()->state_have & (1u << m.cursor)) ? INGAME_TEXT_STATE_ELSEWHERE
									    : INGAME_TEXT_STATE_EMPTY);
	uint32_t why = INGAME_WHY_NONE;
	if (m.cursor >= LIST_FIXED)
		why = cheat_why_now(m.cursor - LIST_FIXED);
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
	       : kind == ROW_SAVE && m.states && buffer_unkept()           ? INGAME_TEXT_STATE_SAVED
	       : unsaved_text()                                            ? unsaved_text()
									   : INGAME_TEXT_HINT;
}

// Slot k's line now, as a session block offset (0: none)
static uint32_t slot_text(uint32_t k)
{
	if (m.title && k == 0)
		return text_at(m.page == PAGE_CHEATS ? INGAME_TEXT_CHEATS
			       : m.page == PAGE_SAVE ? INGAME_TEXT_SAVE
			       : m.page == PAGE_LOAD ? INGAME_TEXT_LOAD
						     : INGAME_TEXT_TITLE);
	if (m.footer && k == m.title + m.visible)
		return m.page == PAGE_MENU ? text_at(menu_footer()) : footer_text();
	int r = slot_row(k);
	return r >= 0 ? row_text((uint32_t)r) : 0;
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
	slot_draw(s, slot_text(k), bank);
	if (s->check) {
		uint32_t o = s->check - 1u;
		int cheat = r >= (int)LIST_FIXED;
		obj_shown(o, cheat && m.text_shown);
		if (cheat) {
			uint32_t e = (uint32_t)r - LIST_FIXED;
			obj_tile(o, bit_of(m.on, e) ? TILE_TICK : TILE_BOX);
			obj_bank(o, row_greyed((uint32_t)r) ? BANK_FAINT : bit_of(m.on, e) ? BANK_TITLE : BANK_TEXT);
		}
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
	uint8_t mark[16];
	sram_in(INGAME_SRAM_STATE_MARK, mark, sizeof(mark));
	for (uint32_t i = 0; i < sizeof(mark); i++)
		if (mark[i] != (uint8_t)mark_bytes[i])
			return 0;
	return 1;
}

static void add_row(uint32_t kind, int greyed)
{
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

	// Sprites, front to back: text and check boxes (hidden until drawn),
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
		}
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
// Slots from..to-1 drawn: their sprites (hidden) and check boxes in the
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
			obj(m.x0 + (int)m.panel_w - CHECK_INSET, sl->y + 4, SHAPE_SQUARE, 0, TILE_BOX, BANK_TEXT,
			    A0_HIDE, 0);
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

// Opens page at row cursor: the panel this frame, its text the next
static void page_open(enum page_id page, uint32_t cursor)
{
	vsync();
	build_page(page, cursor);
	build_text(0, m.title + m.visible + m.footer);
	if (page == PAGE_CHEATS)
		debug_print("screen ready: ingame-cheats");
	else if (page == PAGE_SAVE)
		debug_print("screen ready: ingame-save");
	else if (page == PAGE_LOAD)
		debug_print("screen ready: ingame-load");
}

// Moves the cursor to row r, scrolling the list to show it
static void cursor_to(uint32_t r)
{
	// The menu's footer changes with the row on some rows
	uint32_t footer = m.page == PAGE_MENU ? menu_footer() : 0;
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
	cheats_store();
	slots_update();  // the greyed rows change with what is on
}

static void state_mark(uint32_t slot);
static void shot_take(void);
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
		uint32_t keys = keys_held(), down = keys & ~prev;
		uint32_t move = keys & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT);
		held_for = move && (move & prev) ? held_for + 1 : 0;
		if (held_for >= REPEAT_DELAY && (held_for - REPEAT_DELAY) % REPEAT_EVERY == 0)
			down |= move;
		prev = keys;
		if ((keys & menu_key) == menu_key && (down & menu_key))
			return INGAME_ACT_RESUME;
		if (down & KEY_B) {
			if (m.page == PAGE_MENU)
				return INGAME_ACT_RESUME;
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
		if (down & KEY_A) {
			if (m.page == PAGE_CHEATS) {
				list_pick(m.cursor);
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
				shot_take();
				if (m.footer)
					slot_update(m.title + m.visible);
				continue;
			}
			if (kind == ROW_DEBUG) {
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
					volatile uint32_t *onoff = (volatile uint32_t *)INGAME_CHEAT_ONOFF;
					*onoff = *onoff == 1 ? 0 : 1;
					slot_update(m.title + m.cursor);
				}
				continue;
			}
			return row_action[kind];
		}
	}
}

// The buffer's tag before SAVE: slot's, not kept; its mark cleared, so a
// SAVE cut short leaves no state (the stub writes the mark last)
static void state_mark(uint32_t slot)
{
	struct ingame_state_tag tag;
	uint32_t zeros[4] = {0, 0, 0, 0};
	memset(&tag, 0, sizeof(tag));
	tag.magic = INGAME_STATE_TAG_MAGIC;
	tag.slot = (uint8_t)slot;
	tag.rom_crc = session()->rom_crc;
	sram_out(INGAME_SRAM_STATE_MARK, zeros, sizeof(zeros));
	sram_out(INGAME_SRAM_STATE_TAG, &tag, sizeof(tag));
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
	sram_in(INGAME_SRAM_STATE_TAG, &tag, sizeof(tag));
	if (state_saved() && tag.magic == INGAME_STATE_TAG_MAGIC && tag.slot >= 1 && tag.slot <= INGAME_STATE_SLOTS &&
	    tag.rom_crc == s->rom_crc) {
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
extern const uint32_t __ov_fast_load[], __ov_fast_start[], __ov_fast_end[];
_Static_assert(sizeof(struct shot_scratch) == 0xA00 && 0xA00 + 0x800 <= INGAME_BACKUP_SHOT_BYTES,
	       "ov.ld links the code at INGAME_SHOT_IWRAM + 0xA00, 2 KB at most");

static int shot_flush(struct shotpack_out *o)
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
static void shot_take(void)
{
	if (m.shots >= INGAME_SHOTS_MAX || m.shot_at + sizeof(struct ingame_shot) + 0x1000 > INGAME_SHOTS_END) {
		m.shot_full = 1;
		return;
	}
	const uint16_t dispcnt = REG_DISPCNT;
	REG_DISPCNT = (uint16_t)(dispcnt | DISPCNT_BLANK);
	struct ingame_shot h;
	memset(&h, 0, sizeof(h));
	h.version = INGAME_SHOT_VERSION;
	h.bytes = sizeof(h);
	h.shadow = shadow_take(h.io);
	for (uint32_t i = 0; i < sizeof(io_readable); i++)
		h.io[io_readable[i]] = m.io[i];
	sram_in(INGAME_BACKUP_OBJ, (void *)INGAME_OBJ_VRAM, m.tiles_backed * 32);
	sram_in(INGAME_BACKUP_OAM, (void *)0x07000000, 0x400);
	sram_in(INGAME_BACKUP_OBJ_PAL, (void *)0x05000200, 0x200);
	sram_out(m.shot_at, &h, sizeof(h));  // no magic while it is written
	shot_out = m.shot_at + sizeof(h);
	struct shot_scratch *k = (struct shot_scratch *)INGAME_SHOT_IWRAM;
	const uint32_t fast = (uint32_t)(__ov_fast_end - __ov_fast_start), borrowed = sizeof(*k) + fast * 4;
	sram_out(INGAME_BACKUP_SHOT, k, borrowed);
	for (uint32_t i = 0; i < fast; i++)
		k->code[i] = __ov_fast_load[i];
	struct shotpack_out o = {k->buf, SHOT_BUF, 0, 0, shot_flush};
	int ok = shotpack_pack((const volatile uint16_t *)0x05000000, INGAME_SHOT_PAL_HALVES, k->hash, &o) &&
		 shotpack_pack((const volatile uint16_t *)0x07000000, INGAME_SHOT_OAM_HALVES, k->hash, &o) &&
		 shotpack_pack((const volatile uint16_t *)0x06000000, INGAME_SHOT_VRAM_BYTES / 2, k->hash, &o) &&
		 (!o.n || shot_flush(&o));
	sram_in(INGAME_BACKUP_SHOT, k, borrowed);
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
static void debug_take(void)
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
	static const char magic[16] = INGAME_SNAPSHOT_MAGIC;
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
	if (m.have_session)  // screenshots taken at earlier openings still wait
		shots_load();

	build_page(PAGE_MENU, 0);
	uint16_t dispcnt = REG_DISPCNT;
	vsync();
	REG_BLDCNT = 0x3F00;  // every layer a second target: the cover blends over them
	REG_BLDALPHA = DIM_EVB << 8;
	REG_DISPCNT = (uint16_t)((dispcnt & ~DISPCNT_OFF_BITS) | DISPCNT_OBJ | DISPCNT_1D);
	ov_header.shown = 1;
	// The title and rows, then the footer a frame later: its 40 tiles to
	// back up kept the rows from showing within a frame of the panel
	uint32_t rows = m.title + m.visible;
	build_text(0, rows);
	vsync();
	ov_header.shown = 2;
	build_text(rows, rows + m.footer);
	debug_print("screen ready: ingame");

	wait_release(menu_key);
	uint32_t act = run(menu_key);
	answer(act);
	wait_release(KEYS_ALL);
	vsync();
	hide_all();
	ov_header.shown = 0;
	return act;
}
