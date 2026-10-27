// The kernel's side of the in-game menu: the overlay and the session block
// into cart SRAM at each launch (ingame.h).
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "Ezcard_OP.h"
#include "cartlog.h"
#include "cheat.h"
#include "cheatstore.h"
#include "charset.h"
#include "GBApatch.h"
#include "ff.h"
#include "ingameprobe.h"
#include "engine/font.h"
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

#define CART_SRAM ((vu8 *)0x0E000000)

_Static_assert(INGAME_ROLE_TEXT == UI_ROLE_TEXT && INGAME_ROLE_FAINT == UI_ROLE_FAINT &&
		       INGAME_ROLE_TINT == UI_ROLE_TINT && INGAME_ROLE_ACCENT_INK == UI_ROLE_ACCENT_INK &&
		       INGAME_ROLE_FLOAT == UI_ROLE_FLOAT && INGAME_PALETTE == 16,
	       "the overlay's palette roles");

// n bytes from src to cart SRAM at byte offset at, within one 64 KB page
// window; returns how many read back different
static u32 sram_put(u32 at, const u8 *src, u32 n)
{
	SetRampage((u16)(at >> 12));
	vu8 *dst = CART_SRAM + (at & 0xFFF);
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

// len bytes of UTF-8 at s in the body font into l from its left edge; cut
// to INGAME_TEXT_W pixels, ending in "..." when cut
static void line_draw(struct ingame_line *l, const char *s, u32 len)
{
	memset(l, 0, sizeof(*l));
	u32 pos = 0, w = 0, keep = 0, cut = 0;
	u32 dots = 3 * glyph('.')->advance;
	while (pos < len) {
		u32 cp = charset_next_latin1(s, len, &pos);
		if (cp == CHARSET_NONE)
			continue;
		w += glyph(cp)->advance;
		if (w > INGAME_TEXT_W) {
			cut = 1;
			break;
		}
		if (w + dots <= INGAME_TEXT_W)
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
				if ((row >> i & 1) && x + i < INGAME_TEXT_W)
					l->bits[g->top + r][(x + i) >> 3] |= (u8)(1u << ((x + i) & 7));
		}
		x += g->advance;
	}
	l->width = (u16)(x < INGAME_TEXT_W ? x : INGAME_TEXT_W);
}

void ingame_probe_prepare(const u8 *code)
{
	if (memcmp(code, INGAME_PROBE_CODE, 4) != 0)
		return;
	// A sector of the kernel's bytes in a file of its own, for the probe to
	// write and put back in game mode
	u32 sector[INGAME_PROBE_SECTOR / 4];  // word-aligned for f_write
	for (u32 i = 0; i < INGAME_PROBE_SECTOR; i++)
		((u8 *)sector)[i] = ingame_probe_byte(i, INGAME_PROBE_SEED_KERNEL);
	u32 lba = 0;
	UINT wrote = 0;
	f_mkdir("/torii/probe");
	if (f_open(&gfile, INGAME_PROBE_FILE, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK) {
		if (f_write(&gfile, sector, sizeof(sector), &wrote) == FR_OK && wrote == sizeof(sector) &&
		    f_sync(&gfile) == FR_OK && gfile.obj.sclust >= 2)
			lba = (u32)(gfile.obj.fs->database + (LBA_t)(gfile.obj.sclust - 2) * gfile.obj.fs->csize);
		f_close(&gfile);
	}
	tlog("launch: the in-game probe's scratch sector: %lu", (unsigned long)lba);
	// Its place in the report: no magic, then the sector and its check
	u8 in[24];
	u32 check = INGAME_PROBE_CHECK(lba);
	memset(in, 0, sizeof(in));
	memcpy(in + 16, &lba, 4);
	memcpy(in + 20, &check, 4);
	SetRampage(CARTLOG_PAGE);
	for (u32 i = 0; i < sizeof(in); i++)
		((vu8 *)CARTLOG_INGAME_PROBE)[i] = in[i];
	// The last run's copy in the probe's save (loaded into cart SRAM just
	// before) goes, so the next boot can't take it for this run's
	SetRampage(0);
	for (u32 i = 0; i < 16; i++)
		CART_SRAM[INGAME_PROBE_SAVE_OFFSET + i] = 0;
}

// The in-game list's cheats (cheatstore_launch): how many it wrote to cart
// SRAM, and how many of them are in the interpreter's table
static struct {
	u16 count, table;
} cheats;

void ingame_cheats_clear(void)
{
	cheats.count = cheats.table = 0;
}

void ingame_cheats_set(u32 count, u32 table)
{
	cheats.count = (u16)count;
	cheats.table = (u16)table;
}

// struct ingame_cheat's fields after its line
struct cheat_meta {
	u16 cost, number, index;
	u8 flags, bit;
};
_Static_assert(sizeof(struct cheat_meta) == sizeof(struct ingame_cheat) - offsetof(struct ingame_cheat, cost),
	       "struct ingame_cheat's fields after its line");
#define CHEAT_META(n) (INGAME_SRAM_SESSION + INGAME_SESSION_CHEAT(n) + offsetof(struct ingame_cheat, cost))

// n bytes of cart SRAM at byte offset at, within one 64 KB page window, to dst
static void sram_get(u32 at, u8 *dst, u32 n)
{
	SetRampage((u16)(at >> 12));
	const vu8 *src = CART_SRAM + (at & 0xFFF);
	for (u32 i = 0; i < n; i++)
		dst[i] = src[i];
}

void ingame_cheat_put(u32 n, const char *name, u32 len, u32 number, u32 index, u32 flags, u32 bit, u32 cost)
{
	TLOG_ASSERT(n < INGAME_CHEATS);
	struct ingame_line line;
	line_draw(&line, name, len);
	sram_put(INGAME_SRAM_SESSION + INGAME_SESSION_CHEAT(n), (const u8 *)&line, sizeof(line));
	struct cheat_meta m = {(u16)cost, (u16)number, (u16)index, (u8)flags, (u8)bit};
	sram_put(CHEAT_META(n), (const u8 *)&m, sizeof(m));
	SetRampage(0);
}

void ingame_cheat_meta(u32 n, u32 flags, u32 bit, u32 cost)
{
	struct cheat_meta m;
	sram_get(CHEAT_META(n), (u8 *)&m, sizeof(m));
	m.flags = (u8)flags;
	m.bit = (u8)bit;
	m.cost = (u16)cost;
	sram_put(CHEAT_META(n), (const u8 *)&m, sizeof(m));
	SetRampage(0);
}

void ingame_cheats_trim(u32 table)
{
	for (u32 n = 0; n < cheats.count; n++) {
		struct cheat_meta m;
		sram_get(CHEAT_META(n), (u8 *)&m, sizeof(m));
		if ((m.flags & INGAME_CHEAT_TABLE) && m.bit >= table)
			ingame_cheat_meta(n, INGAME_WHY_ROOM, 0, 0);
	}
	cheats.table = (u16)table;
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

int ingame_states_launch(const char *rom_path, const char *save_slot, const char *rom_name, u32 slot)
{
	states.ready = 0;
	if (strlen(save_slot) >= sizeof(states.save_slot) || strlen(rom_path) >= INGAME_PATH_BYTES)
		return !slot;  // no states: a launch that asked for none goes on
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
	loaded = loaded || (want && statestore_load(rom_path, save_slot, want, states.rom_crc, slot != 0));
	// The newest didn't read whole (a card's error): the next newest, and on
	while (!loaded && !slot && want) {
		states.list.slot[want].have = 0;  // the session's rows say empty: it can't load
		want = statestore_newest(&states.list);
		loaded = want && statestore_load(rom_path, save_slot, want, states.rom_crc, 0);
	}
	if (!loaded && slot)
		tlog_error("states: slot %lu of %s (%s) didn't read", (unsigned long)slot, rom_path, save_slot);
	else if (!loaded)
		tlog("states: %s (%s): no state in cart SRAM%s", rom_path, save_slot, want ? ", it didn't read" : "");
	else
		tlog("states: put back in %lu frames", (unsigned long)(torii_frames - start));
	states.ready = 1;
	return !slot || loaded;
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
	h->flags = gl_sleep_on == 1 ? INGAME_SLEEP_KEY_ON : 0;
	// The cheat list, when the table it toggles went in whole
	if (gl_cheat_on == 1 && cheats.table == gl_cheat_count) {
		h->cheat_count = cheats.count;
		h->cheat_table = cheats.table;
	}
	h->cheat_call = CHEAT_COST_CALL;
	h->cheat_skip = CHEAT_COST_SKIP;
	h->cheat_max = CHEAT_CYCLES_MAX;
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
	bad += sram_put(INGAME_SRAM_SESSION, (const u8 *)words, sizeof(words));
	const struct lang_ingame *t = gl_ingame;
	// The menu's footer: its keys as Settings shows them, then the hint
	char hint[48];
	u8 keys[3];
	u32 n = settings_keys_list(h->menu_key, keys), len = 0;
	hint[0] = 0;
	for (u32 i = 0; i < n; i++)
		len += (u32)snprintf(hint + len, sizeof(hint) - len, "%s%s", i ? "+" : "", settings_key_name(keys[i]));
	tlog("launch: in-game menu keys %s%s", hint, gl_menu_off ? ", off (In-game menu Off)" : "");
	snprintf(hint + len, sizeof(hint) - len, "%s", t->hint);
	const char *const text[INGAME_TEXTS] = {
		[INGAME_TEXT_TITLE] = h->name[0] ? h->name : "Torii",
		[INGAME_TEXT_RESUME] = t->resume,
		[INGAME_TEXT_SAVE] = t->save,
		[INGAME_TEXT_LOAD] = t->load,
		[INGAME_TEXT_CHEATS_ON] = t->cheats_on,
		[INGAME_TEXT_CHEATS_OFF] = t->cheats_off,
		[INGAME_TEXT_RESET] = t->reset,
		[INGAME_TEXT_SLEEP] = t->sleep,
		[INGAME_TEXT_TORII] = t->torii,
		[INGAME_TEXT_DEBUG] = t->debug,
		[INGAME_TEXT_DEBUG_SAVED] = t->debug_saved,
		[INGAME_TEXT_CHEATS] = t->cheats,
		[INGAME_TEXT_ALL_OFF] = t->all_off,
		[INGAME_TEXT_AS_LAUNCHED] = t->as_launched,
		[INGAME_TEXT_KEPT] = t->kept,
		[INGAME_TEXT_WHY_ROM] = t->why_rom,
		[INGAME_TEXT_WHY_LOCKED] = t->why_locked,
		[INGAME_TEXT_WHY_CANT] = t->why_cant,
		[INGAME_TEXT_WHY_ROOM] = t->why_room,
		[INGAME_TEXT_WHY_SLOW] = t->why_slow,
		[INGAME_TEXT_STATE_KEEP] = t->state_keep,
		[INGAME_TEXT_STATE_REPLACES] = t->state_replaces,
		[INGAME_TEXT_STATE_LOAD] = t->state_load,
		[INGAME_TEXT_STATE_ELSEWHERE] = t->state_elsewhere,
		[INGAME_TEXT_STATE_EMPTY] = t->state_empty,
		[INGAME_TEXT_SHOT] = t->shot,
		[INGAME_TEXT_SHOT_SAVED] = t->shot_saved,
		[INGAME_TEXT_HINT] = hint,
	};
	for (u32 i = 0; i < INGAME_TEXTS; i++) {
		struct ingame_line line;
		TLOG_ASSERT(text[i]);
		line_draw(&line, text[i], strlen(text[i]));
		bad += sram_put(INGAME_SRAM_SESSION + INGAME_SESSION_TEXT(i), (const u8 *)&line, sizeof(line));
		h->text_width[i] = line.width;
	}
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
				line_draw(&line, row, strlen(row));
				bad += sram_put(INGAME_SRAM_SESSION + INGAME_SESSION_STATE(s, kept), (const u8 *)&line,
						sizeof(line));
			}
	}
	bad += sram_put(INGAME_SRAM_SESSION + INGAME_SESSION_SAVE_SLOT, (const u8 *)slot_name, sizeof(slot_name));
	bad += sram_put(INGAME_SRAM_SESSION + INGAME_SESSION_OUT, (const u8 *)&out, sizeof(out));
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
	snprintf(text, sizeof(text), EN_STATE_SLOT, (unsigned long)tag.slot);
	dialog_open();
	dialog_progress_begin(gl_states->keeping, text);
	progress_step(NULL, 0, PROGRESS_FULL);
	u32 start = torii_frames;
	int ok = statestore_keep(path, slot_name, tag.slot);
	if (ok) {
		tlog("states: kept slot %u in %lu frames", tag.slot, (unsigned long)(torii_frames - start));
		progress_done(NULL);
	}
	dialog_progress_end();
	if (!ok)
		dialog_message(NULL, gl_states->not_kept, DIALOG_ERROR);
	dialog_close();
}

// The cheat list's toggles as the game left them, into the cheat file's
// picks (written power-cut safe, only if one changed)
static void cheats_keep(const struct ingame_session *h, const char *path, const struct ingame_out *out)
{
	if (!cheatstore_read(path)) {
		tlog_error("cheats: %s: no cheat file to keep the in-game picks in", path);
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
	// A screenshot the card didn't take waits for the next boot
	u32 shot_left = out.screenshot && !shotstore_file(h->name);
	// Done with: the answer goes, so the next boot does nothing again
	memset(&out, 0, sizeof(out));
	if (shot_left) {
		out.magic = INGAME_OUT_MAGIC;
		out.pending_state = 0xFFFF;
		out.screenshot = 1;
	}
	sram_put(INGAME_SRAM_SESSION + INGAME_SESSION_OUT, (const u8 *)&out, sizeof(out));
	SetRampage(0);
}
