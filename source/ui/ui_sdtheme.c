// Themes from the SD card (ui/sdtheme.h): loading, choosing, and the
// wallpaper on plane 3.
#include <gba_compression.h>
#include <gba_systemcalls.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "ff.h"
#include "sdtheme.h"
#include "../crc32.h"
#include "../engine/asset.h"
#include "../engine/gfx.h"
#include "../engine/psram.h"
#include "../engine/res.h"
#include "../engine/sdread.h"
#include "../engine/stage.h"
#include "../Ezcard_OP.h"
#include "../ez_define.h"
#include "../tlog.h"

#define TAG_WALL_TILES 0x5520
#define TAG_WALL_MAP 0x5521
#define TAG_WALL_PAL 0x5522
#define WALL_MAP_SIZE 32  // entries per side of the map

// What a load borrows of pReadCache (READCACHE_THEME), besides the stage:
// the reader (its FIL), the parsed file, paths and the folder listing
struct scratch {
	struct sd_read reader;  // first: its FIL must be word-aligned
	struct theme_ini ini;
	DIR dir;
	FILINFO info;
	char path[80];
};
_Static_assert(PSRAM_THEME_SLOT_BYTES == ASSET_FILE_MAX, "a theme's PSRAM slot holds the largest asset");
_Static_assert(sizeof(struct scratch) <= READCACHE_THEME_SIZE, "theme scratch over READCACHE_THEME_SIZE");

static struct {
	// Two, alternating: a load never rewrites the theme in use (ui.h: only
	// ui_theme_set switches themes)
	struct {
		struct ui_theme theme;
		char name[THEME_NAME_MAX];
	} buf[2];
	u8 cur;  // buf holding the loaded theme
	char folder[THEME_NAME_MAX];
	const struct ui_theme *base;
	char error[THEME_MESSAGE_MAX];
	u16 warnings;
	u8 loaded;
	// The wallpaper: its file in PSRAM slot wall_slot, wall_bytes long (0: none)
	u8 wall_slot;
	u16 wall_bytes;
	u16 wall_gen;  // counts loads, so a new theme's wallpaper is uploaded
	u16 up_gen;    // the load whose wallpaper is up
	u32 wall_crc;
	// Where it is up (res handles, cached), and its BGCNT
	u8 h_tiles, h_map, h_pal;
	u16 bgcnt;
} sd EWRAM_BSS;

static struct scratch *scratch(void)
{
	return (struct scratch *)(pReadCache + READCACHE_THEME);
}

static u8 *wall_file(u32 slot)
{
	return (u8 *)(PSRAM_THEME_BASE + slot * PSRAM_THEME_SLOT_BYTES);
}

static void set_error(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void set_error(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vsnprintf(sd.error, sizeof(sd.error), format, args);
	va_end(args);
	// Quiet while tests make errors on purpose, as the engine's (res.h)
	if (res_errors_quiet)
		tlog("theme: %s", sd.error);
	else
		tlog_error("theme: %s", sd.error);
}

const struct ui_theme *ui_theme_sd(void)
{
	return sd.loaded ? &sd.buf[sd.cur].theme : 0;
}

const char *ui_theme_sd_folder(void)
{
	return sd.loaded ? sd.folder : "";
}

const char *ui_theme_error(void)
{
	return sd.error;
}

u32 ui_theme_warnings(void)
{
	return sd.warnings;
}

// Reads path into dst (max bytes of room) with the SD reader, to the end
static int read_file(const char *path, void *dst, u32 max, u32 *size)
{
	struct sd_read *r = &scratch()->reader;
	sd_read_init(r, path, dst, max, 4096);
	while (sd_read_step(r) == JOB_MORE)
		;
	*size = r->size;
	return r->result;
}

static int file_exists(const char *path)
{
	FILINFO *info = &scratch()->info;
	return f_stat(path, info) == FR_OK && !(info->fattrib & AM_DIR);
}

static void log_warning(const char *message)
{
	tlog("theme: %s", message);
}

// Reads, checks and keeps the wallpaper in PSRAM slot; 0 on a broken or
// over-budget file (the error set)
static int load_wallpaper(const char *folder, const char *file, u32 slot, u32 *bytes, u32 *crc)
{
	struct scratch *s = scratch();
	snprintf(s->path, sizeof(s->path), THEME_DIR "/%s/%s", folder, file);
	u8 *dst = wall_file(slot);
	u32 size = 0;
	int r = read_file(s->path, dst, PSRAM_THEME_SLOT_BYTES, &size);
	if (r != SD_READ_OK && r != SD_READ_ERR_SIZE) {
		set_error("%s: wallpaper %s can't be read", folder, file);
		return 0;
	}
	char msg[THEME_MESSAGE_MAX];
	// Over the slot is over ASSET_FILE_MAX: the check says so
	if (!theme_wallpaper_check(folder, file, dst, r == SD_READ_OK ? size : ASSET_FILE_MAX + 1, msg, sizeof(msg))) {
		set_error("%s", msg);
		return 0;
	}
	*bytes = size;
	*crc = crc32(dst, size);
	return 1;
}

const struct ui_theme *ui_theme_load(const char *name)
{
	sd.error[0] = 0;
	if (!name || !theme_name_ok(name)) {
		set_error(THEME_MSG_BAD_FOLDER, name ? name : "");
		return 0;
	}
	struct scratch *s = scratch();
	snprintf(s->path, sizeof(s->path), THEME_DIR "/%s/theme.ini", name);
	if (!file_exists(s->path)) {
		set_error(THEME_MSG_NO_INI, name);
		return 0;
	}
	char *text = (char *)stage_buffer();
	u32 size = 0;
	int r = read_file(s->path, text, THEME_INI_MAX, &size);
	if (r != SD_READ_OK) {
		if (r == SD_READ_ERR_SIZE)
			set_error(THEME_MSG_INI_BIG, name);
		else
			set_error(THEME_MSG_INI_READ, name);
		return 0;
	}
	struct theme_ini *ini = &s->ini;
	ini->on_warning = log_warning;
	if (theme_ini_parse(text, size, ini) != THEME_OK) {
		set_error("%s: %s", name, ini->message);
		return 0;
	}
	// The wallpaper goes to the slot the current theme doesn't use
	u32 slot = sd.loaded && sd.wall_bytes ? sd.wall_slot ^ 1u : sd.wall_slot;
	u32 bytes = 0, crc = 0;
	if (ini->wallpaper[0]) {
		snprintf(s->path, sizeof(s->path), THEME_DIR "/%s/%s", name, ini->wallpaper);
		if (!file_exists(s->path)) {
			tlog("theme: " THEME_MSG_NO_WALLPAPER, name, ini->wallpaper);
			ini->warnings++;
		} else if (!load_wallpaper(name, ini->wallpaper, slot, &bytes, &crc))
			return 0;
	}
	// All good: it replaces the SD theme loaded before
	if (sd.loaded)
		sd.cur ^= 1;
	struct ui_theme *theme = &sd.buf[sd.cur].theme;
	*theme = ini->theme;
	const char *shown = ini->name[0] ? ini->name : name;
	memcpy(sd.buf[sd.cur].name, shown, strlen(shown) + 1);
	theme->name = sd.buf[sd.cur].name;
	memcpy(sd.folder, name, strlen(name) + 1);
	sd.base = ini->base;
	sd.warnings = ini->warnings;
	sd.wall_slot = (u8)slot;
	sd.wall_bytes = (u16)bytes;
	sd.wall_crc = crc;
	sd.wall_gen++;
	sd.loaded = 1;
	tlog("theme: " THEME_MSG_LOADED, name, theme->name, sd.warnings, bytes ? ", a wallpaper" : "");
	return theme;
}

u32 ui_theme_list(char (*names)[THEME_NAME_MAX], u32 cap)
{
	struct scratch *s = scratch();
	if (f_opendir(&s->dir, THEME_DIR) != FR_OK)
		return 0;
	u32 count = 0;
	while (count < cap && f_readdir(&s->dir, &s->info) == FR_OK && s->info.fname[0]) {
		const char *n = s->info.fname;
		if (!(s->info.fattrib & AM_DIR) || (s->info.fattrib & (AM_HID | AM_SYS)) || !theme_name_ok(n))
			continue;
		// Insertion sort: lists are short
		u32 i = count++;
		while (i > 0 && strcmp(names[i - 1], n) > 0) {
			memcpy(names[i], names[i - 1], THEME_NAME_MAX);
			i--;
		}
		memcpy(names[i], n, strlen(n) + 1);
	}
	f_closedir(&s->dir);
	return count;
}

int ui_theme_choice_read(char name[THEME_NAME_MAX])
{
	if (!file_exists(THEME_CHOICE_FILE))
		return 0;
	// Word-aligned, as f_read wants: the stage
	char *text = (char *)stage_buffer();
	u32 size = 0;
	// The first line is all that counts: a longer file is read in part
	FIL *f = &scratch()->reader.fil;
	UINT got = 0;
	if (f_open(f, THEME_CHOICE_FILE, FA_READ) != FR_OK)
		return 0;
	int ok = f_read(f, text, 64, &got) == FR_OK;
	f_close(f);
	if (!ok)
		return 0;
	size = got;
	u32 start = 0, end;
	if (size >= 3 && (u8)text[0] == 0xEF && (u8)text[1] == 0xBB && (u8)text[2] == 0xBF)
		start = 3;
	while (start < size && (text[start] == ' ' || text[start] == '\t'))
		start++;
	for (end = start; end < size && text[end] != '\n' && text[end] != '\r'; end++)
		;
	while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t'))
		end--;
	if (end - start >= THEME_NAME_MAX)
		return 0;
	memcpy(name, text + start, end - start);
	name[end - start] = 0;
	return theme_name_ok(name);
}

int ui_theme_choice_write(const char *name)
{
	if (!theme_name_ok(name))
		return 0;
	u32 *buf = (u32 *)stage_buffer();  // word-aligned for f_write
	u32 len = (u32)strlen(name);
	memcpy(buf, name, len);
	((char *)buf)[len++] = '\n';
	f_mkdir("/torii");
	FIL *f = &scratch()->reader.fil;
	UINT done = 0;
	if (f_open(f, THEME_CHOICE_FILE, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
		return 0;
	int ok = f_write(f, buf, len, &done) == FR_OK && done == len;
	ok &= f_close(f) == FR_OK;
	if (!ok)
		tlog_error("theme: can't write " THEME_CHOICE_FILE);
	return ok;
}

int ui_theme_boot(void)
{
	if (Read_SET_info(assress_theme) != UI_THEME_SD)
		return 1;
	char name[THEME_NAME_MAX];
	if (!ui_theme_choice_read(name)) {
		set_error("THEME is set to an SD theme, but " THEME_CHOICE_FILE " names none");
		return 0;
	}
	return ui_theme_load(name) != 0;
}

// --- The wallpaper ---------------------------------------------------------

static int wall_current(void)
{
	return sd.loaded && sd.wall_bytes && ui_theme() == ui_theme_sd();
}

// 1 while the uploaded wallpaper's blocks are still the ones it filled
static int wall_up(void)
{
	return sd.h_tiles != RES_NONE && pool_find(res_pool(RES_BG_VRAM), TAG_WALL_TILES) == sd.h_tiles &&
	       pool_find(res_pool(RES_BG_VRAM), TAG_WALL_MAP) == sd.h_map &&
	       pool_find(res_pool(RES_BG_PAL), TAG_WALL_PAL) == sd.h_pal;
}

static void wall_drop(const char *why)
{
	tlog_error("theme: %s: wallpaper %s; none shown", sd.folder, why);
	sd.wall_bytes = 0;
}

// 1 if the wallpaper that is up lies under UI_WALL_VRAM_END
static int wall_low(void)
{
	return res_start(RES_BG_VRAM, sd.h_map) < UI_WALL_VRAM_END &&
	       res_start(RES_BG_VRAM, sd.h_tiles) < UI_WALL_VRAM_END;
}

// Frees the wallpaper's VRAM blocks (cached) at once
static void wall_free(void)
{
	pool_drop(res_pool(RES_BG_VRAM), sd.h_tiles);
	pool_drop(res_pool(RES_BG_VRAM), sd.h_map);
	sd.h_tiles = RES_NONE;
}

// Takes map and tiles blocks for the wallpaper at the top of [0, hi), the
// map above the tiles, without reclaiming any cached block (the wallpaper
// mustn't cost a screen its caches); 0 (none taken) if they don't fit. With
// tagged 0 it only tries: blocks without tags, freed again. The pool
// directly, outside the view log, and no room is no error (res_alloc would
// format one).
static int wall_place(u32 tiles, u16 hi, u8 *m, u8 *t, int tagged)
{
	struct pool *pool = res_pool(RES_BG_VRAM);
	u8 hm = pool_alloc(pool, RES_MAP_UNITS, RES_MAP_UNITS, 0, hi, tagged ? TAG_WALL_MAP : 0, POOL_TOP | POOL_KEEP);
	if (hm == POOL_NONE)
		return 0;
	u8 ht = pool_alloc(pool, (u16)tiles, 1, 0, hi, tagged ? TAG_WALL_TILES : 0, POOL_TOP | POOL_KEEP);
	if (ht == POOL_NONE || !tagged) {
		if (ht != POOL_NONE)
			pool_unref(pool, ht);
		// Freed, not cached: the next try mustn't revive it here
		pool_unref(pool, hm);
		pool_drop(pool, hm);
		return ht != POOL_NONE;
	}
	*m = hm;
	*t = ht;
	return 1;
}

int ui_wallpaper_setup(void)
{
	if (!wall_current())
		return 0;
	if (wall_up() && sd.up_gen == sd.wall_gen) {
		// Up among the maps: back under UI_WALL_VRAM_END if it fits there
		// now (tried untagged: the real tags would free the blocks it shows)
		if (wall_low() || !wall_place(pool_len(res_pool(RES_BG_VRAM), sd.h_tiles), UI_WALL_VRAM_END, 0, 0, 0))
			return 0;
		wall_free();
	}
	sd.h_tiles = RES_NONE;
	// Page 0, where the file is: a dialog's ui_init can run while a PSRAM
	// loader has left another page selected (it selects its page again
	// before each copy)
	psram_begin();
	// The file in PSRAM is lost after a game was loaded there
	const u8 *file = wall_file(sd.wall_slot);
	if (crc32(file, sd.wall_bytes) != sd.wall_crc) {
		wall_drop("changed in PSRAM");
		return 0;
	}
	struct asset_header h;
	memcpy(&h, file, sizeof(h));
	u32 tiles = (u32)h.width * h.height;
	u32 pal_bytes = asset_palette_bytes(&h);
	// Under UI_WALL_VRAM_END (sdtheme.h); while a screen's planes 0 and 2
	// use that room, among the maps until the next push or pop
	u8 m = RES_NONE, t = RES_NONE, p = RES_NONE;
	if (wall_place(tiles, UI_WALL_VRAM_END, &m, &t, 1) || wall_place(tiles, RES_BG_UNITS, &m, &t, 1)) {
		p = pool_alloc(res_pool(RES_BG_PAL), 1, 1, 0, RES_PAL_BANKS, TAG_WALL_PAL, POOL_TOP);
		if (p == RES_NONE) {
			res_unref(RES_BG_VRAM, t);
			res_unref(RES_BG_VRAM, m);
		}
	}
	// No room on this screen is no error: not logged as one
	if (p == RES_NONE) {
		tlog("theme: no room for the wallpaper on this screen");
		return 0;
	}
	// Decoded in the stage first: a broken stream can't reach past the
	// tiles' block in VRAM (asset.h: up to 17 bytes past the end)
	u8 *stage = stage_buffer();
	LZ77UnCompWram(file + ASSET_HEADER_BYTES + pal_bytes, stage);
	// The character block whose reach its tiles start in
	u16 start = res_start(RES_BG_VRAM, t);
	u32 cb = start / 512u;
	if (cb > 3)
		cb = 3;
	u32 first = start - cb * 512u;
	// Word copies (VRAM takes no byte writes), by the BIOS: it runs in a frame
	CpuFastSet(stage, res_bg_addr(t), tiles * RES_UNIT_BYTES / 4);
	u16 *map = res_bg_addr(m);
	u32 bank = res_start(RES_BG_PAL, p);
	// One period of rows, then copies of it: a forward copy repeats them
	u32 rows = h.height < WALL_MAP_SIZE ? h.height : WALL_MAP_SIZE;
	u32 row = first | bank << 12;
	for (u32 y = 0; y < rows; y++, row += h.width)
		for (u32 x = 0, tx = 0; x < WALL_MAP_SIZE; x++) {
			map[y * WALL_MAP_SIZE + x] = (u16)(row + tx);
			if (++tx == h.width)
				tx = 0;
		}
	if (rows < WALL_MAP_SIZE)
		CpuFastSet(map, map + rows * WALL_MAP_SIZE, (WALL_MAP_SIZE - rows) * WALL_MAP_SIZE / 2);
	u16 pal[16] ALIGN(4);
	memset(pal, 0, sizeof(pal));
	memcpy(pal, file + ASSET_HEADER_BYTES, (u32)h.colours * 2);
	gfx_pal_load(bank, pal, 16);
	// Left cached: a screen that needs the room takes it back
	res_unref(RES_BG_VRAM, t);
	res_unref(RES_BG_VRAM, m);
	res_unref(RES_BG_PAL, p);
	sd.h_tiles = t;
	sd.h_map = m;
	sd.h_pal = p;
	sd.bgcnt = (u16)(3 | cb << 2 | (u32)res_bg_screenblock(m) << 8);
	sd.up_gen = sd.wall_gen;
	tlog_debug("theme: wallpaper up, %lu tiles at %u, map %u, bank %lu", (unsigned long)tiles, start,
		   res_bg_screenblock(m), (unsigned long)bank);
	return 1;
}

int ui_wallpaper_yield(void)
{
	if (!wall_up() || wall_low())
		return 0;
	wall_free();
	return 1;
}

int ui_wallpaper_bgcnt(u16 *bgcnt)
{
	if (!wall_current() || sd.up_gen != sd.wall_gen || !wall_up())
		return 0;
	*bgcnt = sd.bgcnt;
	return 1;
}

const struct ui_theme *ui_theme_fallback_base(void)
{
	return sd.loaded && ui_theme() == ui_theme_sd() ? sd.base : 0;
}

void ui_theme_fell_back(const char *why)
{
	set_error("%s broke a screen (%s); using the built-in %s theme", sd.folder, why, sd.base->name);
}
