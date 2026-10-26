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
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../ingame_abi.h"

#define REG16(a) (*(volatile uint16_t *)(a))
#define REG_DISPCNT REG16(0x04000000)
#define REG_VCOUNT REG16(0x04000006)
#define REG_BLDCNT REG16(0x04000050)
#define REG_BLDALPHA REG16(0x04000052)
#define REG_KEYINPUT REG16(0x04000130)
#define OAM ((volatile uint16_t *)0x07000000)
#define OBJ_PAL ((volatile uint16_t *)0x05000200)
#define OBJ_TILES ((volatile uint32_t *)INGAME_OBJ_VRAM)

// mGBA's debug output (source/tlog.c); the real cart ignores these addresses
#define REG_DEBUG_ENABLE REG16(0x04FFF780)
#define REG_DEBUG_FLAGS REG16(0x04FFF700)
#define REG_DEBUG_STRING ((volatile char *)0x04FFF600)

#define KEY_A 0x001
#define KEY_B 0x002
#define KEY_UP 0x040
#define KEY_DOWN 0x080
#define KEYS_ALL 0x3FF
#define MENU_KEY_DEFAULT 0x308  // L+R+START
#define REPEAT_DELAY 18  // frames UP or DOWN is held before it repeats
#define REPEAT_EVERY 5

// DISPCNT: OBJ on, 1D tile mapping; forced blank and the windows off
#define DISPCNT_OBJ 0x1000
#define DISPCNT_1D 0x0040
#define DISPCNT_OFF_BITS 0xE080

extern struct ingame_image ov_header;
extern uint32_t __ov_bss_start[], __ov_bss_end[];

// --- Layout -------------------------------------------------------------------

#define SCREEN_W 240
#define SCREEN_H 160
#define PANEL_W 144
#define PANEL_X ((SCREEN_W - PANEL_W) / 2)
#define PANEL_PAD 8
#define ROW_H 16
#define TITLE_GAP 4
#define TEXT_X (PANEL_X + 14)
#define TEXT_TOP 2  // a line's 12 rows in a 16 px row
#define TEXT_SPRITES 4  // 32x16 sprites a line takes at most (INGAME_TEXT_W)
#define PILL_X (PANEL_X + 4)
#define PILL_W (PANEL_W - 8)
#define PILL_SPRITES 6  // two end caps and four fills

// OBJ tiles from INGAME_OBJ_TILE: one block of solid tiles that every
// filled shape shares (a 64x32 sprite's 32), the panel's corner, the pill's
// end cap (8x16), then the lines of text, 8 tiles a 32x16 sprite
#define TILE_SOLID 0
#define TILE_CORNER 32
#define TILE_CAP 33
#define TILE_TEXT 35

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
	ROW_RESET,
	ROW_SLEEP,
	ROW_TORII,
	ROW_DEBUG,
	ROW_KINDS
};
enum {
	LINE_TITLE = ROW_KINDS,
	LINE_NOTICE
};
#define LINES_MAX (ROW_KINDS + 2)

static const uint8_t row_action[ROW_KINDS] = {
	INGAME_ACT_RESUME, INGAME_ACT_SAVE,  INGAME_ACT_LOAD,  INGAME_ACT_RESUME,
	INGAME_ACT_RESET,  INGAME_ACT_SLEEP, INGAME_ACT_TORII, INGAME_ACT_DEBUG,
};
static const uint8_t line_texts[ROW_KINDS + 2] = {
	INGAME_TEXT_RESUME, INGAME_TEXT_SAVE,  INGAME_TEXT_LOAD,  INGAME_TEXT_CHEATS_ON, INGAME_TEXT_RESET,
	INGAME_TEXT_SLEEP,  INGAME_TEXT_TORII, INGAME_TEXT_DEBUG, INGAME_TEXT_TITLE,     INGAME_TEXT_DEBUG_SAVED,
};
static const char state_flag[16] = {'E', 'Z', '-', 'O', 'm', 'e', 'g', 'a', 'R', 'T', 'C', 'F', 'I', 'L', 'E', '.'};

// The dark theme's colours, for a session block that's missing
static const uint16_t fallback_colours[BANKS] = {0x1063, 0x0000, 0x2D0A, 0x6F7B, 0x7E8F, 0x3DEF};

struct line {
	uint8_t kind;  // enum row_kind, or LINE_*
	uint8_t off;  // greyed out: A does nothing and the cursor skips it
	uint8_t y;  // its top on screen
	uint8_t oam;  // its first text sprite
	uint8_t sprites;  // how many
	uint16_t tile;  // its first tile, from INGAME_OBJ_TILE
};

// The session block's head (INGAME_SESSION_HEAD bytes), seen through session()
static uint32_t session_words[INGAME_SESSION_HEAD / 4];

static struct {
	const struct ingame_call *call;
	int have_session;
	struct line lines[LINES_MAX];
	uint32_t count;  // lines
	uint32_t first;  // the first menu row among them (after the title)
	uint32_t rows;  // menu rows
	uint32_t cursor;  // a menu row, from 0
	uint32_t pill_oam;
	int pill_y, pill_to;
	uint32_t oam_next;
	uint32_t tile_next;
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
	for (; text[i] && i < 255; i++)
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

// --- Text ---------------------------------------------------------------------

// Bits 0-7 of b to nibbles 0-7, each 0 or 1: a 1bpp row byte as a tile row
static uint32_t spread(uint32_t b)
{
	b = (b | b << 12) & 0x000F000Fu;
	b = (b | b << 6) & 0x03030303u;
	return (b | b << 3) & 0x11111111u;
}

// A line's width, from its session block line
static uint32_t text_width(uint32_t text)
{
	uint32_t word = 0;
	if (m.have_session)
		sram_in(INGAME_SRAM_SESSION + INGAME_SESSION_TEXT(text), &word, 4);
	return word & 0xFFFF;
}

// Line text from the session block into l's tiles
static void text_draw(const struct line *l, uint32_t text)
{
	struct ingame_line line;
	if (m.have_session)
		sram_in(INGAME_SRAM_SESSION + INGAME_SESSION_TEXT(text), &line, sizeof(line));
	else
		memset(&line, 0, sizeof(line));
	uint32_t columns = l->sprites * 4u;
	for (uint32_t y = 0; y < 16; y++) {
		uint32_t row = y - TEXT_TOP;
		for (uint32_t c = 0; c < columns; c++) {
			uint32_t word = row < INGAME_TEXT_ROWS ? spread(line.bits[row][c]) : 0;
			OBJ_TILES[(l->tile + (c >> 2) * 8 + (y >> 3) * 4 + (c & 3)) * 8 + (y & 7)] = word;
		}
	}
}

// --- Sprites ------------------------------------------------------------------

static void obj(int x, int y, uint32_t shape, uint32_t size, uint32_t tile, uint32_t bank, uint32_t a0, uint32_t a1)
{
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

// Sets the pixels of a column of tiles (8 wide, rows tall) where inside says
static void tile_shape(uint32_t tile, uint32_t rows, int (*inside)(int, int))
{
	volatile uint32_t *t = OBJ_TILES + tile * 8;
	for (uint32_t y = 0; y < rows; y++) {
		uint32_t word = 0;
		for (uint32_t x = 0; x < 8; x++)
			if (inside((int)x, (int)y))
				word |= 1u << (x * 4);
		t[y] = word;  // a column's tiles follow each other in 1D mapping
	}
}

// The panel's top left corner: a quarter circle of radius 6 (doubled coordinates)
static int corner_inside(int x, int y)
{
	int dx = 2 * (6 - x) - 1, dy = 2 * (6 - y) - 1;
	return x >= 6 || y >= 6 || dx * dx + dy * dy <= 144;
}

// The pill's left end: half a circle of radius 8
static int cap_inside(int x, int y)
{
	int dx = 2 * (8 - x) - 1, dy = 2 * (8 - y) - 1;
	return dx * dx + dy * dy <= 256;
}

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

// --- The menu -----------------------------------------------------------------

static int cheats_on(void)
{
	return *(volatile uint32_t *)INGAME_CHEAT_ONOFF != 1;
}

static uint32_t line_text(const struct line *l)
{
	if (l->kind == ROW_CHEATS && !cheats_on())
		return INGAME_TEXT_CHEATS_OFF;
	return line_texts[l->kind];
}

static uint32_t line_bank(const struct line *l)
{
	if (l->kind == LINE_TITLE)
		return BANK_TITLE;
	return l->off || l->kind == LINE_NOTICE ? BANK_FAINT : BANK_TEXT;
}

static struct line *row(uint32_t r)
{
	return &m.lines[m.first + r];
}

static void pill_place(int y)
{
	for (uint32_t i = 0; i < PILL_SPRITES; i++) {
		volatile uint16_t *o = OAM + (m.pill_oam + i) * 4;
		*o = (uint16_t)((*o & ~0xFFu) | ((uint32_t)y & 0xFF));
	}
}

static uint32_t next_row(uint32_t from, int step)
{
	uint32_t at = from;
	for (uint32_t i = 0; i < m.rows; i++) {
		at = (at + m.rows + (uint32_t)step) % m.rows;
		if (!row(at)->off)
			return at;
	}
	return from;
}

static int state_saved(void)
{
	uint8_t flag[16];
	sram_in(INGAME_SRAM_STATE_FLAG, flag, sizeof(flag));
	for (uint32_t i = 0; i < sizeof(flag); i++)
		if (flag[i] != (uint8_t)state_flag[i])
			return 0;
	return 1;
}

static void add_line(uint32_t kind, int off)
{
	struct line *l = &m.lines[m.count++];
	l->kind = (uint8_t)kind;
	l->off = (uint8_t)off;
}

// Lays the menu's lines out in a panel from *top, *height tall: each line's
// place, and as many sprites and tiles as its text needs (the cheats' row
// the longer of its two)
static void layout(uint32_t flags, int *top, uint32_t *height)
{
	int have_state = (flags & INGAME_CAN_STATES) && state_saved();
	uint32_t rows = 4 + ((flags & INGAME_CAN_STATES) ? 2 : 0) + ((flags & INGAME_CAN_CHEATS) ? 1 : 0) +
			((flags & INGAME_CAN_DEBUG) ? 1 : 0);
	uint32_t notice = (flags & INGAME_NOTICE_DEBUG) ? 1 : 0;

	// The height; the title goes when the screen can't take it
	uint32_t h = PANEL_PAD * 2 + (rows + notice) * ROW_H;
	int title = h + ROW_H + TITLE_GAP <= SCREEN_H - 8;
	if (title)
		h += ROW_H + TITLE_GAP;
	*top = (int)(SCREEN_H - h) / 2;
	*height = h;

	if (title)
		add_line(LINE_TITLE, 0);
	m.first = m.count;
	add_line(ROW_RESUME, 0);
	if (flags & INGAME_CAN_STATES) {
		add_line(ROW_SAVE, 0);
		add_line(ROW_LOAD, !have_state);
	}
	if (flags & INGAME_CAN_CHEATS)
		add_line(ROW_CHEATS, 0);
	add_line(ROW_RESET, 0);
	add_line(ROW_SLEEP, 0);
	add_line(ROW_TORII, 0);
	if (flags & INGAME_CAN_DEBUG)
		add_line(ROW_DEBUG, 0);
	m.rows = m.count - m.first;
	if (notice)
		add_line(LINE_NOTICE, 0);

	int y = *top + PANEL_PAD;
	m.tile_next = TILE_TEXT;
	for (uint32_t i = 0; i < m.count; i++) {
		struct line *l = &m.lines[i];
		uint32_t w = text_width(line_texts[l->kind]);
		if (l->kind == ROW_CHEATS) {
			uint32_t w2 = text_width(INGAME_TEXT_CHEATS_OFF);
			w = w2 > w ? w2 : w;
		}
		uint32_t n = (w + 31) / 32;
		l->sprites = (uint8_t)(n < 1 ? 1 : n > TEXT_SPRITES ? TEXT_SPRITES : n);
		l->tile = (uint16_t)m.tile_next;
		m.tile_next += l->sprites * 8u;
		l->y = (uint8_t)y;
		y += l->kind == LINE_TITLE ? ROW_H + TITLE_GAP : ROW_H;
	}
}

// Backs up the OBJ tiles from INGAME_OBJ_TILE up to tile end, and records it
static void tiles_backup(uint32_t from, uint32_t end)
{
	m.call->sram_copy(INGAME_BACKUP_OBJ + from * 32, (void *)(INGAME_OBJ_VRAM + from * 32), (end - from) * 32, 1);
	ov_header.obj_bytes = end * 32;
}

// The menu without its text, its text sprites in front but hidden: the
// cover, the panel and the pill. Shows at the next VBlank.
static void build_panel(uint32_t flags)
{
	const struct ingame_session *s = session();
	int x0 = PANEL_X, y0;
	uint32_t h;
	layout(flags, &y0, &h);
	tiles_backup(0, TILE_TEXT);

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
	tile_shape(TILE_CORNER, 8, corner_inside);
	tile_shape(TILE_CAP, 16, cap_inside);

	// Sprites, front to back: text (hidden until drawn), the pill, the panel, the cover
	m.oam_next = 0;
	for (uint32_t i = 0; i < m.count; i++) {
		struct line *l = &m.lines[i];
		l->oam = (uint8_t)m.oam_next;
		for (uint32_t k = 0; k < l->sprites; k++)
			obj(TEXT_X + (int)k * 32, l->y, SHAPE_WIDE, 2, l->tile + k * 8, line_bank(l), A0_HIDE, 0);
	}
	m.cursor = next_row(m.rows - 1, 1);
	m.pill_y = m.pill_to = row(m.cursor)->y;
	m.pill_oam = m.oam_next;
	obj(PILL_X, m.pill_y, SHAPE_TALL, 0, TILE_CAP, BANK_PILL, 0, 0);
	obj(PILL_X + PILL_W - 8, m.pill_y, SHAPE_TALL, 0, TILE_CAP, BANK_PILL, 0, A1_HFLIP);
	for (int x = PILL_X + 8, k = 0; k < 4; k++, x += 32) {
		int at = x > PILL_X + PILL_W - 8 - 32 ? PILL_X + PILL_W - 8 - 32 : x;
		obj(at, m.pill_y, SHAPE_WIDE, 2, TILE_SOLID, BANK_PILL, 0, 0);
	}
	int x1 = x0 + PANEL_W, y1 = y0 + (int)h;
	obj(x0, y0, SHAPE_SQUARE, 0, TILE_CORNER, BANK_PANEL, 0, 0);
	obj(x1 - 8, y0, SHAPE_SQUARE, 0, TILE_CORNER, BANK_PANEL, 0, A1_HFLIP);
	obj(x0, y1 - 8, SHAPE_SQUARE, 0, TILE_CORNER, BANK_PANEL, 0, A1_VFLIP);
	obj(x1 - 8, y1 - 8, SHAPE_SQUARE, 0, TILE_CORNER, BANK_PANEL, 0, A1_HFLIP | A1_VFLIP);
	obj_rect(x0 + 8, y0, PANEL_W - 16, 8, 32, 8, SHAPE_WIDE, 1, BANK_PANEL, 0);
	obj_rect(x0 + 8, y1 - 8, PANEL_W - 16, 8, 32, 8, SHAPE_WIDE, 1, BANK_PANEL, 0);
	obj_rect(x0, y0 + 8, PANEL_W, (int)h - 16, 64, 32, SHAPE_WIDE, 3, BANK_PANEL, 0);
	obj_rect(0, 0, SCREEN_W, SCREEN_H, 64, 32, SHAPE_WIDE, 3, BANK_DIM, A0_SEMI);
}

// The text: its tiles backed up and drawn; its sprites shown at the next VBlank
static void build_text(void)
{
	tiles_backup(TILE_TEXT, m.tile_next);
	for (uint32_t i = 0; i < m.count; i++)
		text_draw(&m.lines[i], line_text(&m.lines[i]));
}

static void text_show(void)
{
	for (uint32_t i = 0; i < m.count; i++) {
		const struct line *l = &m.lines[i];
		for (uint32_t k = 0; k < l->sprites; k++)
			OAM[(l->oam + k) * 4] &= (uint16_t)~A0_HIDE;
	}
}

// Waits until no key in mask is held
static void wait_release(uint32_t mask)
{
	while (keys_held() & mask)
		vsync();
}

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
		uint32_t move = keys & (KEY_UP | KEY_DOWN);
		held_for = move && (move & prev) ? held_for + 1 : 0;
		if (held_for >= REPEAT_DELAY && (held_for - REPEAT_DELAY) % REPEAT_EVERY == 0)
			down |= move;
		prev = keys;
		if ((down & KEY_B) || ((keys & menu_key) == menu_key && (down & menu_key)))
			return INGAME_ACT_RESUME;
		if (down & (KEY_UP | KEY_DOWN)) {
			m.cursor = next_row(m.cursor, (down & KEY_UP) ? -1 : 1);
			m.pill_to = row(m.cursor)->y;
		}
		if (down & KEY_A) {
			struct line *l = row(m.cursor);
			if (l->off)
				continue;
			if (l->kind == ROW_CHEATS) {
				*(volatile uint32_t *)INGAME_CHEAT_ONOFF = cheats_on() ? 1 : 0;
				text_draw(l, line_text(l));
				continue;
			}
			return row_action[l->kind];
		}
	}
}

uint32_t ov_entry(const struct ingame_call *call);

uint32_t ov_entry(const struct ingame_call *call)
{
	for (uint32_t *p = __ov_bss_start; p < __ov_bss_end; p++)
		*p = 0;
	m.call = call;
	ov_header.shown = 0;
	ov_header.obj_bytes = 0;
	hide_all();  // the game's sprites go at once
	sram_in(INGAME_SRAM_SESSION, session_words, sizeof(session_words));
	const struct ingame_session *s = session();
	m.have_session = s->magic == INGAME_SESSION_MAGIC && s->version == INGAME_SESSION_VERSION;
	uint32_t menu_key = m.have_session && s->menu_key ? s->menu_key : MENU_KEY_DEFAULT;

	build_panel(call->flags);
	uint16_t dispcnt = REG_DISPCNT;
	vsync();
	REG_BLDCNT = 0x3F00;  // every layer a second target: the cover blends over them
	REG_BLDALPHA = DIM_EVB << 8;
	REG_DISPCNT = (uint16_t)((dispcnt & ~DISPCNT_OFF_BITS) | DISPCNT_OBJ | DISPCNT_1D);
	ov_header.shown = 1;
	build_text();
	vsync();
	text_show();
	ov_header.shown = 2;
	debug_print("screen ready: ingame");

	wait_release(menu_key);
	uint32_t act = run(menu_key);
	wait_release(KEYS_ALL);
	vsync();
	hide_all();
	ov_header.shown = 0;
	return act;
}
