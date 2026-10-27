// The kernel's side of the in-game menu: the overlay and the session block
// into cart SRAM at each launch (ingame.h).
#include <stddef.h>
#include <string.h>

#include "Ezcard_OP.h"
#include "cartlog.h"
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
#include "tlog.h"
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

// s in the body font into l from its left edge; cut to INGAME_TEXT_W
// pixels, ending in "..." when cut
static void line_draw(struct ingame_line *l, const char *s)
{
	memset(l, 0, sizeof(*l));
	u32 len = strlen(s), pos = 0, w = 0, keep = 0, cut = 0;
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
	// Its place in the report: no magic, then the sector
	u8 in[20];
	memset(in, 0, sizeof(in));
	memcpy(in + 16, &lba, 4);
	SetRampage(CARTLOG_PAGE);
	for (u32 i = 0; i < sizeof(in); i++)
		((vu8 *)CARTLOG_INGAME_PROBE)[i] = in[i];
	SetRampage(0);
}

int ingame_prepare(const char *name, u32 rom_bytes)
{
	// The block's head (its fields before the slots); the rest is written as
	// zeros, and the overlay's answer as none yet
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

	u32 bad = 0;
	u32 image = ingame_overlay_size;
	TLOG_ASSERT(image <= INGAME_IMAGE_MAX && image % 4 == 0);
	for (u32 at = 0; at < image; at += 0x1000) {
		u32 n = image - at < 0x1000 ? image - at : 0x1000;
		bad += sram_put(INGAME_SRAM_IMAGE + at, ingame_overlay + at, n);
	}
	bad += sram_put(INGAME_SRAM_SESSION, (const u8 *)words, sizeof(words));
	const struct lang_ingame *t = gl_ingame;
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
	};
	for (u32 i = 0; i < INGAME_TEXTS; i++) {
		struct ingame_line line;
		line_draw(&line, text[i]);
		bad += sram_put(INGAME_SRAM_SESSION + INGAME_SESSION_TEXT(i), (const u8 *)&line, sizeof(line));
	}
	u32 rest = INGAME_SESSION_TEXT(INGAME_TEXTS);
	bad += sram_put(INGAME_SRAM_SESSION + rest, NULL, INGAME_SESSION_OUT - rest);
	bad += sram_put(INGAME_SRAM_SESSION + INGAME_SESSION_OUT, (const u8 *)&out, sizeof(out));
	SetRampage(0);
	if (bad) {
		tlog_error("launch: the in-game menu's %lu bytes in cart SRAM read back %lu different",
			   (unsigned long)image + (unsigned long)sizeof(struct ingame_session), (unsigned long)bad);
		return -1;
	}
	const struct ingame_image *hdr = (const struct ingame_image *)ingame_overlay;
	tlog("launch: in-game menu in cart SRAM: %lu bytes, %lu loaded into a %lu-byte EWRAM window",
	     (unsigned long)image, (unsigned long)hdr->image_bytes, (unsigned long)hdr->window_bytes);
	return 0;
}
