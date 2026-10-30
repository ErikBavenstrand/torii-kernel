// The kernel's side of the in-game menu: the overlay and the session block
// into cart SRAM at each launch (ingame.h).
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "Ezcard_OP.h"
#include "cartlog.h"
#include "cheat.h"
#include "cheatkeys.h"
#include "cheatstore.h"
#include "charset.h"
#include "GBApatch.h"
#include "ff.h"
#include "ingameprobe.h"
#include "engine/font.h"
#include "engine/text.h"
#include "lang.h"
#include "ez_define.h"
#include "ezkernel.h"
#include "ingame.h"
#include "ingame_abi.h"
#include "settings_model.h"
#include "shotstore.h"
#include "statestore.h"
#include "tlog.h"
#include "ui/dialog.h"
#include "progress.h"
#include "crc32.h"
#include "ui/palette.h"
#include "ui/ui.h"
#include "RTC.h"

#define CART_SRAM ((vu8 *)0x0E000000)

_Static_assert(INGAME_ROLE_TEXT == UI_ROLE_TEXT && INGAME_ROLE_FAINT == UI_ROLE_FAINT &&
		       INGAME_ROLE_TINT == UI_ROLE_TINT && INGAME_ROLE_ACCENT_INK == UI_ROLE_ACCENT_INK &&
		       INGAME_ROLE_FLOAT == UI_ROLE_FLOAT && INGAME_PALETTE == 16,
	       "the overlay's palette roles");

// n bytes from src to cart SRAM at byte offset at, within one 64 KB page
// window; returns how many read back different
static u32 sram_put(u32 at, const u8 *src, u32 n)
{
	SetRampage((u16)((at >> 16) << 4));  // the page register ignores its low bit
	vu8 *dst = CART_SRAM + (at & 0xFFFF);
	for (u32 i = 0; i < n; i++)
		dst[i] = src ? src[i] : 0;
	u32 bad = 0;
	for (u32 i = 0; i < n; i++)
		bad += dst[i] != (src ? src[i] : 0);
	return bad;
}

// The file name's last part without its extension, cut to fit
static void game_name(char *out, u32 size, const char *path)
{
	const char *base = strrchr(path, '/');
	base = base ? base + 1 : path;
	const char *dot = strrchr(base, '.');
	u32 n = dot && dot != base ? (u32)(dot - base) : strlen(base);
	if (n > size - 1)
		n = size - 1;
	// never a cut UTF-8 sequence at the end
	while (n && (base[n] & 0xC0) == 0x80)
		n--;
	memcpy(out, base, n);
	out[n] = 0;
}

static const struct torii_glyph *glyph(u32 cp)
{
	u32 i = cp >= TORII_FONT_FIRST && cp <= TORII_FONT_LAST ? cp - TORII_FONT_FIRST : TORII_FONT_REPLACEMENT;
	return &font_body.glyphs[i];
}

// len bytes of UTF-8 at s in the body font into the INGAME_TEXT_ROWS rows of
// row_bytes at out from their left edge; cut to max_w pixels (at most
// row_bytes * 8), ending in "..." when cut. The pixels inked; 1 in *was_cut
// if it was. Byte by byte, so out may be cart SRAM.
static u32 bits_draw(vu8 *out, u32 row_bytes, const char *s, u32 len, u32 max_w, u32 *was_cut)
{
	for (u32 i = 0; i < INGAME_TEXT_ROWS * row_bytes; i++)
		out[i] = 0;
	u32 pos = 0, w = 0, keep = 0, cut = 0;
	u32 dots = 3 * glyph('.')->advance;
	while (pos < len) {
		u32 cp = charset_next_latin1(s, len, &pos);
		if (cp == CHARSET_NONE)
			continue;
		w += glyph(cp)->advance;
		if (w > max_w) {
			cut = 1;
			break;
		}
		if (w + dots <= max_w)
			keep = pos;
	}
	u32 end = cut ? keep : len, x = 0;
	for (pos = 0; pos < end + (cut ? 3 : 0);) {
		u32 cp = pos < end ? charset_next_latin1(s, len, &pos) : (pos++, '.');
		if (cp == CHARSET_NONE)
			continue;
		const struct torii_glyph *g = glyph(cp);
		const u8 *bits = font_body.bits + g->offset;
		for (u32 r = 0; r < g->rows && g->top + r < INGAME_TEXT_ROWS; r++) {
			u32 row = 0;
			for (u32 b = 0; b < font_body.row_bytes; b++)
				row |= (u32)bits[r * font_body.row_bytes + b] << (8 * b);
			for (u32 i = 0; row >> i; i++)
				if ((row >> i & 1) && x + i < max_w)
					out[(g->top + r) * row_bytes + ((x + i) >> 3)] |= (u8)(1u << ((x + i) & 7));
		}
		x += g->advance;
	}
	*was_cut = cut;
	return x < max_w ? x : max_w;
}

// As bits_draw, into a line of the menu (max_w at most INGAME_TEXT_W); 1 if cut
static u32 line_draw(struct ingame_line *l, const char *s, u32 len, u32 max_w)
{
	u32 cut;
	memset(l, 0, sizeof(*l));
	l->width = (u16)bits_draw((vu8 *)&l->bits[0][0], sizeof(l->bits[0]), s, len, max_w, &cut);
	return cut;
}

// The cheat search's number glyphs (struct ingame_glyph): 0-9, A-F and the
// thousands separator, in the body font
static void glyphs_draw(struct ingame_glyph *g)
{
	static const char hex[] = "0123456789ABCDEF";
	memset(g, 0, INGAME_GLYPHS * sizeof(*g));
	for (u32 i = 0; i < INGAME_GLYPHS; i++) {
		const char *s = i < 16 ? &hex[i] : S(STR_FORMAT_THOUSANDS);
		u32 cut, len = i < 16 ? 1 : (u32)strlen(s);
		g[i].width = (u8)bits_draw((vu8 *)g[i].rows, 1, s, len, 8, &cut);
	}
}

// A text with "{1}" in it, as the text before it and after it (each cut to
// size bytes); one without has it all before
static void text_split(const char *t, char *before, char *after, u32 size)
{
	const char *at = strstr(t, "{1}");
	u32 n = at ? (u32)(at - t) : (u32)strlen(t);
	snprintf(before, size, "%.*s", (int)n, t);
	snprintf(after, size, "%s", at ? at + 3 : "");
}

void ingame_probe_prepare(const u8 *code)
{
	if (memcmp(code, INGAME_PROBE_CODE, 4) != 0)
		return;
	// The report's place blank (its SD test, which read a scratch sector
	// from here, is retired), and the last run's copy in the probe's save
	// (loaded into cart SRAM just before) gone, so the next boot can't take
	// either for this run's
	SetRampage(CARTLOG_PAGE);
	for (u32 i = 0; i < 24; i++)
		((vu8 *)CARTLOG_INGAME_PROBE)[i] = 0;
	SetRampage(0);
	for (u32 i = 0; i < 16; i++)
		CART_SRAM[INGAME_PROBE_SAVE_OFFSET + i] = 0;
}

// The in-game list's cheats (cheatstore_launch): how many it wrote to cart
// SRAM, and how many of them are in the interpreter's table
static struct {
	u16 count, table;
	u16 acts;  // the activation sentences written (session's act_text)
	u16 act[INGAME_ACT_LINES];  // their values (cheatkeys.h)
} cheats;

void ingame_cheats_clear(void)
{
	cheats.count = cheats.table = cheats.acts = 0;
}

void ingame_cheats_set(u32 count, u32 table)
{
	cheats.count = (u16)count;
	cheats.table = (u16)table;
}

// struct ingame_cheat's fields after its line
struct cheat_meta {
	u16 cost, number, index;
	u8 flags, bit, act, reserved[3];
};
_Static_assert(sizeof(struct cheat_meta) == sizeof(struct ingame_cheat) - offsetof(struct ingame_cheat, cost),
	       "struct ingame_cheat's fields after its line");
#define CHEAT_META(n) (INGAME_SRAM_SESSION + INGAME_SESSION_CHEAT(n) + offsetof(struct ingame_cheat, cost))

// n bytes of cart SRAM at byte offset at, within one 64 KB page window, to dst
static void sram_get(u32 at, u8 *dst, u32 n)
{
	SetRampage((u16)((at >> 16) << 4));  // the page register ignores its low bit
	const vu8 *src = CART_SRAM + (at & 0xFFFF);
	for (u32 i = 0; i < n; i++)
		dst[i] = src[i];
}

void ingame_dump_image(void)
{
	// What cart SRAM holds where the launch wrote the overlay's image, for
	// a refusal whose image didn't check: compared on a PC with the build's
	FIL file;
	if (f_open(&file, "/torii/logs/ingame-image.bin", FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
		return;
	u32 part[512 / 4];  // word-aligned for f_write
	UINT wrote = 0;
	u32 at;
	for (at = 0; at < INGAME_IMAGE_MAX; at += sizeof(part)) {
		sram_get(INGAME_SRAM_IMAGE + at, (u8 *)part, sizeof(part));
		if (f_write(&file, part, sizeof(part), &wrote) != FR_OK || wrote != sizeof(part))
			break;
	}
	SetRampage(0);
	f_close(&file);
	tlog("in-game menu: cart SRAM's image area (%lu bytes) is in /torii/logs/ingame-image.bin", (unsigned long)at);
}

// The session's line for activation act (cheatkeys.h): its index + 1 in
// act_text, written the first time; 0 for none (0, or no line left)
static u32 act_line(u16 act)
{
	if (!cheatkeys_valid(act) || cheatkeys_mode(act) == CHEATKEYS_NONE)
		return 0;
	for (u32 k = 0; k < cheats.acts; k++)
		if (cheats.act[k] == act)
			return k + 1;
	if (cheats.acts >= INGAME_ACT_LINES)
		return 0;
	char text[64];
	struct ingame_line line;
	cheatkeys_sentence(act, text, sizeof(text));
	line_draw(&line, text, strlen(text), INGAME_TEXT_W);
	sram_put(INGAME_SRAM_SESSION + INGAME_SESSION_ACT(cheats.acts), (const u8 *)&line, sizeof(line));
	cheats.act[cheats.acts++] = act;
	return cheats.acts;
}

void ingame_cheat_put(u32 n, const char *name, u32 len, u32 number, u32 index, u32 flags, u32 bit, u32 cost, u16 act)
{
	TLOG_ASSERT(n < INGAME_CHEATS);
	struct ingame_line line;
	if (line_draw(&line, name, len, INGAME_ROW_W)) {
		// Cut: whole for the marquee, as far as its room goes, drawn
		// straight into cart SRAM (no RAM to spare for its 772 bytes)
		u32 at = INGAME_SRAM_WIDE + n * (u32)sizeof(struct ingame_wide), cut;
		SetRampage((u16)((at >> 16) << 4));
		u32 width = bits_draw(CART_SRAM + (at & 0xFFFF) + offsetof(struct ingame_wide, bits), INGAME_WIDE_W / 8,
				      name, len, INGAME_WIDE_W, &cut);
		u8 head[4] = {(u8)width, (u8)(width >> 8), 0, 0};  // its width and reserved, little-endian
		sram_put(at, head, sizeof(head));
		flags |= INGAME_CHEAT_CUT;
	}
	sram_put(INGAME_SRAM_SESSION + INGAME_SESSION_CHEAT(n), (const u8 *)&line, sizeof(line));
	struct cheat_meta m = {(u16)cost, (u16)number, (u16)index, (u8)flags, (u8)bit, (u8)act_line(act), {0}};
	sram_put(CHEAT_META(n), (const u8 *)&m, sizeof(m));
	SetRampage(0);
}

// The launch's state slots (ingame_states_launch), for the session block
static struct {
	u8 ready;
	u32 rom_crc;
	char save_slot[INGAME_SAVE_SLOT_BYTES];
	struct statestore_list list;
} states;  // IWRAM: EWRAM is the tighter

// What a state tag of the game's save slot carries: its paths' CRC
static u32 states_crc(const char *rom_path, const char *save_slot)
{
	return crc32_update(crc32(rom_path, (u32)strlen(rom_path) + 1), save_slot, (u32)strlen(save_slot));
}

void ingame_states_none(void)
{
	states.ready = 0;
	// Whatever another launch left in the buffer (a state of another game,
	// its autoload mark) goes: the patch's entry code and the menu's old
	// Load row would act on it
	statestore_buffer_clear();
}

int ingame_states_launch(const char *rom_path, const char *save_slot, const char *rom_name, u32 slot, u32 rom_bytes)
{
	states.ready = 0;
	statestore_game(GAMECODE, GAMEHEAD[0], GAMEHEAD[1], GAMEHEAD[2], rom_bytes);
	if (strlen(save_slot) >= sizeof(states.save_slot) || strlen(rom_path) >= INGAME_PATH_BYTES) {
		statestore_buffer_clear();  // no state of another game's, as ingame_states_none
		return !slot;  // no states: a launch that asked for none goes on
	}
	memset(states.save_slot, 0, sizeof(states.save_slot));
	memcpy(states.save_slot, save_slot, strlen(save_slot) + 1);  // its length checked above
	states.rom_crc = states_crc(rom_path, save_slot);
	int imported = statestore_import_rts(rom_path, save_slot, rom_name, states.rom_crc);
	statestore_scan(rom_path, save_slot, &states.list);
	u32 want = slot ? slot : statestore_newest(&states.list);
	u32 start = torii_frames;
	// An /RTS file just imported is in cart SRAM already, as that slot
	struct ingame_state_tag tag;
	int loaded = imported && !slot && want == 1 && statestore_buffer(&tag) && tag.slot == 1 &&
		     tag.rom_crc == states.rom_crc;
	if (loaded)
		tlog("states: slot 1 in cart SRAM from the /RTS import");
	int r = loaded || !want ? loaded : statestore_load(rom_path, save_slot, want, states.rom_crc, slot != 0);
	loaded = r == 1;
	// The newest didn't read whole (a card's error) or isn't the game's: the
	// next newest, and on
	while (!loaded && !slot && want) {
		states.list.slot[want].have = 0;  // the session's rows say empty: it can't load
		want = statestore_newest(&states.list);
		loaded = want && statestore_load(rom_path, save_slot, want, states.rom_crc, 0) == 1;
	}
	if (!loaded)
		statestore_buffer_clear();  // whatever another launch left: the patch's entry code would act on it
	if (!loaded && slot)
		tlog_error("states: slot %lu of %s (%s) %s", (unsigned long)slot, rom_path, save_slot,
			   r == STATESTORE_OTHER_GAME ? "is another game's"
			   : r == STATESTORE_NEWER    ? "is of a newer format"
						      : "didn't read");
	else if (!loaded)
		tlog("states: %s (%s): no state in cart SRAM%s", rom_path, save_slot, want ? ", it didn't read" : "");
	else
		tlog("states: put back in %lu frames", (unsigned long)(torii_frames - start));
	states.ready = 1;
	return !slot || loaded ? 1 : r;
}

int ingame_prepare(const char *name, u32 rom_bytes, const char *rom_path)
{
	// The block's head (its fields before the lines); the cheats are
	// cheatstore_launch's, and the overlay's answer none yet
	u32 words[INGAME_SESSION_HEAD / 4];
	memset(words, 0, sizeof(words));
	struct ingame_session *h = (struct ingame_session *)(void *)words;
	struct ingame_out out;
	memset(&out, 0, sizeof(out));
	out.pending_state = 0xFFFF;
	h->magic = INGAME_SESSION_MAGIC;
	h->version = INGAME_SESSION_VERSION;
	h->bytes = sizeof(struct ingame_session);
	h->rom_bytes = rom_bytes;
	game_name(h->name, sizeof(h->name), name);
	u16 banks[UI_BANKS * 16];
	ui_theme_banks(ui_theme_setting(), PAL_GBA, banks);
	memcpy(h->palette, banks, sizeof(h->palette));
	u16 slots[assress_max];
	for (u32 a = 0; a < assress_max; a++)
		slots[a] = Read_SET_info(a);
	u16 menu_key, sleep_key;
	settings_hotkeys(slots, &menu_key, &sleep_key);
	h->menu_key = menu_key & 0x3FF;
	h->sleep_key = sleep_key & 0x3FF;
	h->rtc_status = Game_GPIO_status();
	h->flags = (gl_sleep_on == 1 ? INGAME_SLEEP_KEY_ON : 0) | (tlog_level != TLOG_OFF ? INGAME_DEBUG_ON : 0) |
		   (Game_clock_on() ? INGAME_CLOCK_ON : 0);
	// The clock now, for the screenshots' times (shotstore_file)
	rtc_enable();
	rtc_get(h->launch_clock);
	rtc_disenable();
	h->launch_clock[7] = 1;
	// The cheat list, when the table it toggles went in whole
	if (gl_cheat_on == 1 && cheats.table == gl_cheat_count) {
		h->cheat_count = cheats.count;
		h->cheat_table = cheats.table;
	}
	// The cheat search's (v0.9): the live block, when this launch's patch has
	// the interpreter, and an id the search keeps to know it is this launch's
	h->live_at = cheat_live_at;
	u32 now = torii_frames;
	h->launch_id = crc32_update(crc32(&now, sizeof(now)), h->launch_clock, sizeof(h->launch_clock));
	h->cheat_call = CHEAT_LOAD(CHEAT_COST_CALL);
	h->cheat_skip = CHEAT_LOAD(CHEAT_COST_SKIP);
	h->cheat_max = CHEAT_LOAD_MAX;
	// The state slots, when this launch put them in place
	if (states.ready) {
		h->state_slots = STATESTORE_SLOTS;
		for (u32 s = 1; s <= STATESTORE_SLOTS; s++)
			if (states.list.slot[s].have)
				h->state_have |= (u8)(1u << (s - 1));
		h->rom_crc = states.rom_crc;
	}

	u32 bad = 0;
	u32 image = ingame_overlay_size;
	TLOG_ASSERT(image <= INGAME_IMAGE_MAX && image % 4 == 0);
	for (u32 at = 0; at < image; at += 0x1000) {
		u32 n = image - at < 0x1000 ? image - at : 0x1000;
		bad += sram_put(INGAME_SRAM_IMAGE + at, ingame_overlay + at, n);
	}
	// The header again, with the check word the stub verifies the loaded image by
	struct ingame_image hdr_out;
	memcpy(&hdr_out, ingame_overlay, sizeof(hdr_out));
	TLOG_ASSERT(hdr_out.image_bytes <= image && hdr_out.image_bytes >= sizeof(hdr_out));
	hdr_out.check = ingame_image_check((const u32 *)(const void *)(ingame_overlay + sizeof(hdr_out)),
					   (hdr_out.image_bytes - (u32)sizeof(hdr_out)) / 4);
	bad += sram_put(INGAME_SRAM_IMAGE, (const u8 *)&hdr_out, sizeof(hdr_out));
	h->ext_check = ingame_image_check((const u32 *)(const void *)(ingame_overlay + hdr_out.image_bytes),
					  (image - hdr_out.image_bytes) / 4);
	bad += sram_put(INGAME_SRAM_SESSION, (const u8 *)words, sizeof(words));
	// The menu's footer: its keys as Settings shows them in A's hint;
	// without A's word where that doesn't fit
	char keys_text[32], hint[64];
	u8 keys[3];
	u32 n = settings_keys_list(h->menu_key, keys), len = 0;
	keys_text[0] = 0;
	for (u32 i = 0; i < n; i++)
		len += (u32)snprintf(keys_text + len, sizeof(keys_text) - len, "%s%s", i ? "+" : "",
				     settings_key_name(keys[i]));
	tlog("launch: in-game menu keys %s%s", keys_text, gl_menu_off ? ", off (In-game menu Off)" : "");
	SF(hint, sizeof(hint), STR_INGAME_HINT, LS(keys_text));
	if (text_width(&font_body, hint, (u32)strlen(hint)) > INGAME_TEXT_W)
		SF(hint, sizeof(hint), STR_INGAME_HINT_SHORT, LS(keys_text));
	char counts[INGAME_SHOTS_MAX][24];
	for (u32 i = 0; i < INGAME_SHOTS_MAX; i++)
		SF(counts[i], sizeof(counts[i]), STR_INGAME_SHOT_COUNT, LN(i + 1), LN(INGAME_SHOTS_MAX));
	// The lines by their ingame_abi.h index: the strings', then those built here
	static const u16 ids[INGAME_TEXTS] = {
		[INGAME_TEXT_RESUME] = STR_INGAME_RESUME,
		[INGAME_TEXT_SAVE] = STR_INGAME_SAVE,
		[INGAME_TEXT_LOAD] = STR_INGAME_LOAD,
		[INGAME_TEXT_CHEATS_ON] = STR_INGAME_CHEATS_ON,
		[INGAME_TEXT_CHEATS_OFF] = STR_INGAME_CHEATS_OFF,
		[INGAME_TEXT_RESET] = STR_INGAME_RESET,
		[INGAME_TEXT_SLEEP] = STR_INGAME_SLEEP,
		[INGAME_TEXT_TORII] = STR_INGAME_TORII,
		[INGAME_TEXT_DEBUG] = STR_INGAME_DEBUG,
		[INGAME_TEXT_DEBUG_SAVED] = STR_INGAME_DEBUG_SAVED,
		[INGAME_TEXT_CHEATS] = STR_INGAME_CHEATS,
		[INGAME_TEXT_ALL_OFF] = STR_INGAME_ALL_OFF,
		[INGAME_TEXT_AS_LAUNCHED] = STR_INGAME_AS_LAUNCHED,
		[INGAME_TEXT_KEPT] = STR_INGAME_KEPT,
		[INGAME_TEXT_WHY_ROM] = STR_INGAME_WHY_ROM,
		[INGAME_TEXT_WHY_LOCKED] = STR_INGAME_WHY_LOCKED,
		[INGAME_TEXT_WHY_CANT] = STR_INGAME_WHY_CANT,
		[INGAME_TEXT_WHY_ROOM] = STR_INGAME_WHY_ROOM,
		[INGAME_TEXT_WHY_SLOW] = STR_INGAME_WHY_SLOW,
		[INGAME_TEXT_ONCE_HINT] = STR_INGAME_ONCE_HINT,
		[INGAME_TEXT_ONCE_QUEUED] = STR_INGAME_ONCE_QUEUED,
		[INGAME_TEXT_STATE_KEEP] = STR_INGAME_STATE_KEEP,
		[INGAME_TEXT_STATE_REPLACES] = STR_INGAME_STATE_REPLACES,
		[INGAME_TEXT_STATE_LOAD] = STR_INGAME_STATE_LOAD,
		[INGAME_TEXT_STATE_ELSEWHERE] = STR_INGAME_STATE_ELSEWHERE,
		[INGAME_TEXT_STATE_EMPTY] = STR_INGAME_STATE_EMPTY,
		[INGAME_TEXT_SHOT] = STR_INGAME_SHOT,
		[INGAME_TEXT_SHOT_SAVED] = STR_INGAME_SHOT_SAVED,
		[INGAME_TEXT_SLEEP_WAKES] = STR_INGAME_SLEEP_WAKES,
		[INGAME_TEXT_SHOT_FULL] = STR_INGAME_SHOT_FULL,
		[INGAME_TEXT_STATE_SAVED] = STR_INGAME_STATE_SAVED,
		[INGAME_TEXT_TORII_SAVE] = STR_INGAME_TORII_SAVE,
		[INGAME_TEXT_FIRST_SAVE] = STR_INGAME_FIRST_SAVE,
		[INGAME_TEXT_UNSAVED_STATE] = STR_INGAME_UNSAVED_STATE,
		[INGAME_TEXT_SEARCH] = STR_INGAME_SEARCH,
		[INGAME_TEXT_SEARCH_SIZE] = STR_INGAME_SEARCH_SIZE8,
		[INGAME_TEXT_SEARCH_SIZE + 1] = STR_INGAME_SEARCH_SIZE16,
		[INGAME_TEXT_SEARCH_SIZE + 2] = STR_INGAME_SEARCH_SIZE32,
		[INGAME_TEXT_SEARCH_START] = STR_INGAME_SEARCH_START,
		[INGAME_TEXT_SEARCH_SAME] = STR_INGAME_SEARCH_SAME,
		[INGAME_TEXT_SEARCH_CHANGED] = STR_INGAME_SEARCH_CHANGED,
		[INGAME_TEXT_SEARCH_UP] = STR_INGAME_SEARCH_UP,
		[INGAME_TEXT_SEARCH_DOWN] = STR_INGAME_SEARCH_DOWN,
		[INGAME_TEXT_SEARCH_SHOW] = STR_INGAME_SEARCH_SHOW,
		[INGAME_TEXT_SEARCH_NEW] = STR_INGAME_SEARCH_NEW,
		[INGAME_TEXT_SEARCH_UNDO] = STR_INGAME_SEARCH_UNDO,
		[INGAME_TEXT_SEARCH_NONE] = STR_INGAME_SEARCH_NONE,
		[INGAME_TEXT_SEARCH_BUSY] = STR_INGAME_SEARCH_BUSY,
		[INGAME_TEXT_SEARCH_HINT] = STR_INGAME_SEARCH_HINT,
		[INGAME_TEXT_SEARCH_DROPS] = STR_INGAME_SEARCH_DROPS,
		[INGAME_TEXT_SEARCH_STATE_FIRST] = STR_INGAME_SEARCH_STATE_FIRST,
		[INGAME_TEXT_SEARCH_FEW] = STR_INGAME_SEARCH_FEW,
		[INGAME_TEXT_SEARCH_EDIT] = STR_INGAME_SEARCH_EDIT,
		[INGAME_TEXT_SEARCH_KEEP] = STR_INGAME_SEARCH_KEEP,
		[INGAME_TEXT_SEARCH_KEEP_ON] = STR_INGAME_SEARCH_KEEP_ON,
		[INGAME_TEXT_SEARCH_KEEP_SET] = STR_INGAME_SEARCH_KEEP_SET,
		[INGAME_TEXT_SEARCH_KEPT] = STR_INGAME_SEARCH_KEPT,
		[INGAME_TEXT_SEARCH_LIVE_FULL] = STR_INGAME_SEARCH_LIVE_FULL,
		[INGAME_TEXT_SEARCH_FOUND_FULL] = STR_INGAME_SEARCH_FOUND_FULL,
		[INGAME_TEXT_SEARCH_PICK] = STR_INGAME_SEARCH_PICK,
		[INGAME_TEXT_SEARCH_SAVE_ENDS] = STR_INGAME_SEARCH_SAVE_ENDS,
		[INGAME_TEXT_UNSAVED_FOUND] = STR_INGAME_UNSAVED_FOUND,
	};
	const char *text[INGAME_TEXTS];
	for (u32 i = 0; i < INGAME_TEXTS; i++)
		text[i] = ids[i] ? S(ids[i]) : NULL;
	text[INGAME_TEXT_TITLE] = h->name[0] ? h->name : S(STR_INGAME_UNTITLED);
	text[INGAME_TEXT_HINT] = hint;
	char unsaved[2][INGAME_SHOTS_MAX][40];
	for (u32 i = 0; i < INGAME_SHOTS_MAX; i++) {
		SF(unsaved[0][i], sizeof(unsaved[0][i]), STR_N(STR_INGAME_UNSAVED_SHOTS, i + 1), LN(i + 1));
		SF(unsaved[1][i], sizeof(unsaved[1][i]), STR_N(STR_INGAME_UNSAVED_BOTH, i + 1), LN(i + 1));
		text[INGAME_TEXT_SHOT_COUNT + i] = counts[i];
		text[INGAME_TEXT_UNSAVED_SHOTS + i] = unsaved[0][i];
		text[INGAME_TEXT_UNSAVED_BOTH + i] = unsaved[1][i];
	}
	// The search's lines with a number in them: the text before it and after
	static const u16 split_ids[][2] = {
		{STR_INGAME_SEARCH_EQUALS, INGAME_TEXT_SEARCH_EQUALS},
		{STR_INGAME_SEARCH_LEFT, INGAME_TEXT_SEARCH_LEFT},
		{STR_INGAME_SEARCH_VALUE, INGAME_TEXT_SEARCH_VALUE},
	};
	char split[sizeof(split_ids) / sizeof(split_ids[0])][2][48];
	for (u32 i = 0; i < sizeof(split_ids) / sizeof(split_ids[0]); i++) {
		text_split(S(split_ids[i][0]), split[i][0], split[i][1], sizeof(split[i][0]));
		text[split_ids[i][1]] = split[i][0];
		text[split_ids[i][1] + 1] = split[i][1];
	}
	for (u32 i = 0; i < INGAME_TEXTS; i++) {
		struct ingame_line line;
		TLOG_ASSERT(text[i]);
		line_draw(&line, text[i], strlen(text[i]), INGAME_TEXT_W);
		bad += sram_put(INGAME_TEXT_SRAM(i), (const u8 *)&line, sizeof(line));
		if (i < INGAME_TEXT_SEARCH)
			h->text_width[i] = line.width;
	}
	struct ingame_glyph glyphs[INGAME_GLYPHS];
	glyphs_draw(glyphs);
	bad += sram_put(INGAME_SRAM_GLYPHS, (const u8 *)glyphs, sizeof(glyphs));
	// The head again, with the lines' widths
	bad += sram_put(INGAME_SRAM_SESSION, (const u8 *)words, sizeof(words));
	char path[INGAME_PATH_BYTES];
	memset(path, 0, sizeof(path));
	if (rom_path)
		strncpy(path, rom_path, sizeof(path) - 1);
	bad += sram_put(INGAME_SRAM_SESSION + INGAME_SESSION_PATH, (const u8 *)path, sizeof(path));
	char slot_name[INGAME_SAVE_SLOT_BYTES];
	memset(slot_name, 0, sizeof(slot_name));
	if (states.ready) {
		memcpy(slot_name, states.save_slot, sizeof(slot_name));
		for (u32 s = 1; s <= STATESTORE_SLOTS; s++)
			for (u32 kept = 0; kept < 2; kept++) {
				char row[48];
				struct ingame_line line;
				statestore_row(row, sizeof(row), s, &states.list.slot[s], !kept);
				line_draw(&line, row, strlen(row), INGAME_ROW_W);
				bad += sram_put(INGAME_SRAM_SESSION + INGAME_SESSION_STATE(s, kept), (const u8 *)&line,
						sizeof(line));
			}
	}
	bad += sram_put(INGAME_SRAM_SESSION + INGAME_SESSION_SAVE_SLOT, (const u8 *)slot_name, sizeof(slot_name));
	bad += sram_put(INGAME_SRAM_SESSION + INGAME_SESSION_OUT, (const u8 *)&out, sizeof(out));
	// No cheats found yet (the last session's were filed at the boot after it)
	bad += sram_put(INGAME_SRAM_FOUND, NULL, 8);
	SetRampage(0);
	if (bad) {
		tlog_error("launch: the in-game menu's %lu bytes in cart SRAM read back %lu different",
			   (unsigned long)image + (unsigned long)sizeof(struct ingame_session), (unsigned long)bad);
		return -1;
	}
	const struct ingame_image *hdr = (const struct ingame_image *)ingame_overlay;
	tlog("launch: in-game menu in cart SRAM: %lu bytes, %lu loaded into a %lu-byte EWRAM window; %u cheats "
	     "listed",
	     (unsigned long)image, (unsigned long)hdr->image_bytes, (unsigned long)hdr->window_bytes, h->cheat_count);
	return 0;
}

// --- After the game: the kernel's next boot -------------------------------------

// The session block's head, path and answer, if cart SRAM holds a launch's
// (a soft reset from the game keeps it; a power cycle doesn't)
static int session_read(struct ingame_session *h, char *path, struct ingame_out *out)
{
	sram_get(INGAME_SRAM_SESSION, (u8 *)h, INGAME_SESSION_HEAD);
	sram_get(INGAME_SRAM_SESSION + INGAME_SESSION_PATH, (u8 *)path, INGAME_PATH_BYTES);
	sram_get(INGAME_SRAM_SESSION + INGAME_SESSION_OUT, (u8 *)out, sizeof(*out));
	SetRampage(0);
	return h->magic == INGAME_SESSION_MAGIC && h->version == INGAME_SESSION_VERSION &&
	       h->bytes == sizeof(struct ingame_session) && out->magic == INGAME_OUT_MAGIC && path[0] == '/' &&
	       memchr(path, 0, INGAME_PATH_BYTES) && h->cheat_count <= INGAME_CHEATS;
}

// A state saved in game and not kept yet (the buffer's tag), filed for the
// session's game and save slot under a progress dialog
static void states_after_game(const struct ingame_session *h, const char *path)
{
	struct ingame_state_tag tag;
	char slot_name[INGAME_SAVE_SLOT_BYTES];
	if (!h->state_slots || !statestore_buffer(&tag) || tag.kept || tag.slot < 1 || tag.slot > STATESTORE_SLOTS)
		return;
	sram_get(INGAME_SRAM_SESSION + INGAME_SESSION_SAVE_SLOT, (u8 *)slot_name, sizeof(slot_name));
	SetRampage(0);
	slot_name[sizeof(slot_name) - 1] = 0;
	if (tag.rom_crc != h->rom_crc || h->rom_crc != states_crc(path, slot_name)) {
		tlog_error("states: the state in cart SRAM isn't %s's (%s): not kept", path, slot_name);
		return;
	}
	char text[24];
	SF(text, sizeof(text), STR_STATE_SLOT, LN(tag.slot));
	dialog_open();
	dialog_progress_begin(S(STR_STATE_KEEPING), text);
	progress_step(NULL, 0, PROGRESS_FULL);
	u32 start = torii_frames;
	int ok = statestore_keep(path, slot_name, tag.slot);
	if (ok) {
		tlog("states: kept slot %u in %lu frames", tag.slot, (unsigned long)(torii_frames - start));
		progress_done(NULL);
	}
	dialog_progress_end();
	if (!ok)
		dialog_message(NULL, S(STR_STATE_NOT_KEPT), DIALOG_ERROR);
	dialog_close();
}

// cheats_keep for a larger file (cheatstore.h): each window that has listed
// cheats in turn, its index the cheat's place in the whole file
static void cheats_keep_windows(const struct ingame_session *h, const char *path, const struct ingame_out *out)
{
	struct cheatstore *cs = cheatstore();
	struct cheatfile *f = &cs->file;
	u32 changed = 0;
	for (u32 pass = 0; pass < 2; pass++)  // drops first, so the picks after them fit
		for (u32 k = 0; k < cheatstore_windows(); k++) {
			u32 start = cheatstore_window_start(k), went = 0;
			for (u32 n = 0; n < h->cheat_count; n++) {
				struct cheat_meta m;
				sram_get(CHEAT_META(n), (u8 *)&m, sizeof(m));
				SetRampage(0);
				if (m.index < start || m.index >= cheatstore_window_start(k + 1))
					continue;
				if (!went) {
					if (!cheatstore_window_go(k))
						return;
					cheatstore_decode(0);
					went = 1;
				}
				u32 i = m.index - start;
				if (i >= f->count || cs->cheats[i].number != m.number) {
					tlog_error(
						"cheats: %s changed since the launch: the in-game picks are not kept",
						path);
					return;
				}
				if ((m.flags & INGAME_CHEAT_WHY) != INGAME_WHY_NONE || !(m.flags & INGAME_CHEAT_TABLE))
					continue;
				u32 on = out->cheat_on[n >> 5] >> (n & 31) & 1;
				if (cs->cheats[i].picked != on && on == pass && cheatstore_toggle(i))
					changed++;
			}
		}
	tlog("cheats: %s: %lu picks changed in game", path, (unsigned long)changed);
	if (changed && !cheatstore_write())
		tlog_error("cheats: %s: the in-game picks weren't written", path);
}

// The cheat list's toggles as the game left them, into the cheat file's
// picks (written power-cut safe, only if one changed)
static void cheats_keep(const struct ingame_session *h, const char *path, const struct ingame_out *out)
{
	if (!cheatstore_read(path)) {
		tlog_error("cheats: %s: no cheat file to keep the in-game picks in", path);
		return;
	}
	if (cheatstore_windows()) {
		cheats_keep_windows(h, path, out);
		return;
	}
	struct cheatstore *cs = cheatstore();
	struct cheatfile *f = &cs->file;
	u32 last = 0;
	for (u32 n = 0; n < h->cheat_count; n++) {
		struct cheat_meta m;
		sram_get(CHEAT_META(n), (u8 *)&m, sizeof(m));
		SetRampage(0);
		if (m.index >= f->count || cs->cheats[m.index].number != m.number) {
			tlog_error("cheats: %s changed since the launch: the in-game picks are not kept", path);
			return;
		}
		if (m.index + 1u > last)
			last = m.index + 1u;
	}
	// Decoded up to the last listed: a pick has to fit as the cheat screen's do
	while (cs->work.next < last && cheatfile_next(f, &cs->work) >= 0)
		;
	u32 changed = 0;
	for (u32 pass = 0; pass < 2; pass++)  // drops first, so the picks after them fit
		for (u32 n = 0; n < h->cheat_count; n++) {
			struct cheat_meta m;
			sram_get(CHEAT_META(n), (u8 *)&m, sizeof(m));
			SetRampage(0);
			if ((m.flags & INGAME_CHEAT_WHY) != INGAME_WHY_NONE || !(m.flags & INGAME_CHEAT_TABLE))
				continue;
			u32 on = out->cheat_on[n >> 5] >> (n & 31) & 1;
			if (cs->cheats[m.index].picked != on && on == pass && cheatfile_toggle(f, m.index))
				changed++;
		}
	tlog("cheats: %s: %lu picks changed in game", path, (unsigned long)changed);
	if (changed && !cheatstore_write())
		tlog_error("cheats: %s: the in-game picks weren't written", path);
}

// The cheats the in-game search found (struct ingame_found), added to the
// game's cheat file as codes of the user's ("Found 02030100", VBA raw), the
// ones turned on in game picked; written power-cut safe, once. Kept in cart
// SRAM if the card didn't take them: 1 then, for the next boot to try again
static int found_after_game(const char *path)
{
	struct ingame_found found;
	sram_get(INGAME_SRAM_FOUND, (u8 *)&found, sizeof(found));
	SetRampage(0);
	if (found.magic != INGAME_FOUND_MAGIC || !found.count)
		return 0;
	u32 n = found.count <= INGAME_FOUND_MAX ? found.count : INGAME_FOUND_MAX;
	int r = cheatstore_open(path);
	if (r != CHEATSTORE_READ && r != CHEATSTORE_NEW) {
		tlog_error("cheats: %s: the cheat file can't take the %lu found in game (%d): they wait", path,
			   (unsigned long)n, r);
		return 1;
	}
	if (cheatstore_windows() && !cheatstore_window_go(cheatstore_windows() - 1)) {
		tlog_error("cheats: %s: its last window didn't read: the found cheats wait", path);
		return 1;
	}
	u32 added = 0, idx[INGAME_FOUND_MAX], on[INGAME_FOUND_MAX];
	for (u32 i = 0; i < n; i++) {
		const struct ingame_found_cheat *c = &found.cheat[i];
		idx[i] = CHEATFILE_NONE;
		// One cheat an address: the last kept of it (the same code twice
		// would be greyed as doing what the first does), picked if any
		// keep of it turned it on (it ran on in game)
		u32 later = 0;
		on[i] = 0;
		for (u32 j = 0; j < n; j++)
			if (found.cheat[j].address == c->address && found.cheat[j].size == c->size) {
				later |= j > i;
				on[i] |= found.cheat[j].flags & INGAME_FOUND_ON;
			}
		if (later)
			continue;
		char addr[12], name[40], code[24];
		snprintf(addr, sizeof(addr), "%08lX", (unsigned long)c->address);
		SF(name, sizeof(name), STR_CHEATS_FOUND_NAME, LS(addr));
		if (c->size == 1)
			snprintf(code, sizeof(code), "%s:%02lX", addr, (unsigned long)(c->value & 0xFF));
		else if (c->size == 2)
			snprintf(code, sizeof(code), "%s:%04lX", addr, (unsigned long)(c->value & 0xFFFF));
		else
			snprintf(code, sizeof(code), "%s:%08lX", addr, (unsigned long)c->value);
		u32 e = cheatstore_add(name, code, CHEAT_FMT_VBA, &idx[i]);
		tlog("cheats: %s: found in game, %s %s: %s", path, code, on[i] ? "on" : "off",
		     e == CHEATFILE_EDIT_OK ? "added" : "refused");
		added += e == CHEATFILE_EDIT_OK;
		if (e != CHEATFILE_EDIT_OK)
			idx[i] = CHEATFILE_NONE;
	}
	if (!added)  // all refused (the file full): trying again won't help
		n = 0;
	if (added) {
		cheatstore_begin();
		cheatstore_decode(0);
	}
	for (u32 i = 0; i < n && added; i++)
		if (on[i] && idx[i] != CHEATFILE_NONE && idx[i] < cheatstore()->file.count &&
		    !cheatstore()->cheats[idx[i]].picked)
			cheatstore_toggle(idx[i]);
	if (added && !cheatstore_write()) {
		tlog_error("cheats: %s: the found cheats weren't written: they wait for the next boot", path);
		return 1;
	}
	u32 none[2] = {0, 0};
	sram_put(INGAME_SRAM_FOUND, (const u8 *)none, sizeof(none));
	SetRampage(0);
	return 0;
}

void ingame_refused_notice(void)
{
	struct ingame_refused r;
	if (!cartlog_ingame_refused(&r))
		return;
	// The game: the session block's name, which the kernel wrote at launch
	u32 words[INGAME_SESSION_HEAD / 4] = {0};
	struct ingame_session *h = (struct ingame_session *)(void *)words;
	sram_get(INGAME_SRAM_SESSION, (u8 *)words, sizeof(words));
	SetRampage(0);
	const char *name = S(STR_INGAME_REFUSED_GAME);
	if (h->magic == INGAME_SESSION_MAGIC && h->version == INGAME_SESSION_VERSION && h->name[0] &&
	    memchr(h->name, 0, sizeof(h->name)))
		name = h->name;
	// Its save type, from the pending-save record (Check_save_flag clears it)
	u32 type = Read_sav_info(0) == 0x11 ? Read_sav_info(SAV_INFO_TYPE) : 0;
	const char *kind = (type & 0xF0) == 0x30 ? "Flash" : (type & 0xF0) == 0x20 ? "EEPROM" : NULL;
	char why[64];
	if (r.reason == INGAME_REFUSED_IMAGE)
		snprintf(why, sizeof(why), "%s", S(STR_INGAME_REFUSED_IMAGE));
	else if (kind)
		SF(why, sizeof(why), STR_INGAME_REFUSED_SAVE, LS(kind));
	else
		snprintf(why, sizeof(why), "%s", S(STR_INGAME_REFUSED_MEMORY));
	char msg[200];
	SF(msg, sizeof(msg), STR_INGAME_REFUSED, LCUT(name, 24), LS(why));
	dialog_open();  // dialog_message logs it
	dialog_message(S(STR_INGAME_REFUSED_TITLE), msg, DIALOG_INFO);
	dialog_close();
}

void ingame_after_game(void)
{
	u32 words[INGAME_SESSION_HEAD / 4];  // the head alone: the whole block is 22 KB
	const struct ingame_session *h = (const struct ingame_session *)(void *)words;
	struct ingame_out out;
	char path[INGAME_PATH_BYTES];
	if (!session_read((struct ingame_session *)(void *)words, path, &out))
		return;
	tlog("in-game: back from %s, the menu's last %u", path, out.action);
	states_after_game(h, path);
	if (out.cheats_changed && h->cheat_count)
		cheats_keep(h, path, &out);
	u32 found_left = found_after_game(path);
	// Screenshots the card didn't take wait for the next boot (and are
	// filed then even if the menu took none this time)
	u32 shot_left = !shotstore_file(h->name, h->launch_clock);
	// Done with: the answer goes, so the next boot does nothing again
	memset(&out, 0, sizeof(out));
	// Screenshots or found cheats the card didn't take: the next boot again
	if (shot_left || found_left) {
		out.magic = INGAME_OUT_MAGIC;
		out.pending_state = 0xFFFF;
		out.screenshot = (u16)shot_left;
	}
	sram_put(INGAME_SRAM_SESSION + INGAME_SESSION_OUT, (const u8 *)&out, sizeof(out));
	SetRampage(0);
}
