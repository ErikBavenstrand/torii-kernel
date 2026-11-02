// The language packs on the cart (lang.h): found in /torii/lang at boot,
// checked whole in pReadCache (READCACHE_LANG), and read by S() in PSRAM
// (PSRAM_LANG_BASE). A launch keeps its own strings in the row cache's
// room meanwhile, and the menu loads the pack again if the launch comes back.
#include <string.h>

#include "lang.h"
#include "ezkernel.h"
#include "Ezcard_OP.h"
#include "ff15/ff.h"
#include "tlog.h"
#include "version.h"
#include "engine/psram.h"
#include "engine/script.h"
#include "crc32.h"
#include "ui/rowcache.h"
#include "ui/ui.h"

#define LANG_DIR "/torii/lang"

_Static_assert(LANG_PACK_MAX <= PSRAM_LANG_BYTES, "a pack fits its PSRAM");
_Static_assert(LANG_PACK_MAX <= READCACHE_LANG_SIZE, "a pack fits its share of pReadCache");
_Static_assert(READCACHE_LANG + READCACHE_LANG_SIZE <= MAX_pReadCache_size, "READCACHE_LANG");
_Static_assert(PSRAM_LANG_FONT_BASE + SCRIPT_MAX == PSRAM_WINDOW + PSRAM_WINDOW_BYTES,
	       "a script font has the rest of the window");
_Static_assert(SCRIPT_MAX % READCACHE_LANG_SIZE == 0 || SCRIPT_MAX > READCACHE_LANG_SIZE,
	       "script fonts load in pieces");
_Static_assert(LANG_LAUNCH_MAX <= ROW_CACHE_BYTES, "the launch's strings fit the row cache's room");

static struct {
	u8 current;  // lang_choice's index in use
	u8 held;     // a launch's strings are in the row cache's room (lang_before_launch)
} load EWRAM_BSS;

static void path_of(char *out, u32 room, const char *code)
{
	lang_fill(out, room, LANG_DIR "/{1}.lng", LS(code));
}

// Reads a pack's header; 1 if it is one this kernel takes, its code its file's
static int read_header(const char *file, u8 head[LANG_HEADER], char code[LANG_CODE_BYTES])
{
	char path[64];
	lang_fill(path, sizeof(path), LANG_DIR "/{1}", LS(file));
	FIL f;
	UINT got = 0;
	if (f_open(&f, path, FA_READ) != FR_OK)
		return 0;
	int ok = f_read(&f, head, LANG_HEADER, &got) == FR_OK && got == LANG_HEADER;
	f_close(&f);
	if (!ok) {
		tlog_error("lang: %s didn't read", path);
		return 0;
	}
	u32 why = lang_pack_check_header(head);
	char release[LANG_RELEASE_BYTES];
	lang_pack_release(head, release);
	if (why != LANG_PACK_OK) {
		tlog_error("lang: %s left out: %s%s%s", path, lang_pack_check_names[why],
			   release[0] ? ", built for " : "", release);
		return 0;
	}
	lang_pack_code(head, code);
	u32 len = (u32)strlen(code);
	if (strncmp(file, code, len) != 0 || strcmp(file + len, ".lng") != 0) {
		tlog_error("lang: %s left out: its code is %s", path, code);
		return 0;
	}
	return 1;
}

static void find_packs(void)
{
	lang_list_reset();
	DIR dir;
	if (f_opendir(&dir, LANG_DIR) != FR_OK)
		return;
	FILINFO fi;
	u32 head_words[LANG_HEADER / 4];
	u8 *head = (u8 *)head_words;
	while (f_readdir(&dir, &fi) == FR_OK && fi.fname[0]) {
		u32 len = (u32)strlen(fi.fname);
		if (fi.fattrib & AM_DIR || len < 5 || len > LANG_CODE_BYTES + 3 ||
		    strcmp(fi.fname + len - 4, ".lng") != 0)
			continue;
		char code[LANG_CODE_BYTES], name[LANG_NAME_BYTES];
		if (!read_header(fi.fname, head, code))
			continue;
		lang_pack_name(head, name);
		char release[LANG_RELEASE_BYTES];
		lang_pack_release(head, release);
		if (!lang_list_add(code, name))
			tlog_error("lang: %s left out: the list is full, or has its id", code);
		else if (strcmp(release, TORII_VERSION) != 0)
			tlog("lang: %s (%s) on the card, built for %s", code, name, release);
		else
			tlog("lang: %s (%s) on the card", code, name);
	}
	f_closedir(&dir);
}

// Reads choice i's pack into pReadCache, its strings and checked whole; their
// bytes, or 0. *font_at and *font_bytes say where its script font is in the file (0: none).
static u32 read_pack(u32 i, u32 *font_at, u32 *font_bytes)
{
	char path[32];
	path_of(path, sizeof(path), lang_choice(i)->code);
	FIL f;
	if (f_open(&f, path, FA_READ) != FR_OK) {
		tlog_error("lang: %s didn't open", path);
		return 0;
	}
	u32 size = (u32)f_size(&f), strings = 0;
	UINT got = 0;
	u8 *buf = pReadCache + READCACHE_LANG;
	int ok = f_read(&f, buf, LANG_HEADER, &got) == FR_OK && got == LANG_HEADER &&
		 lang_pack_check_header(buf) == LANG_PACK_OK;
	if (ok) {
		strings = lang_pack_strings_bytes(buf);
		lang_pack_font(buf, font_at, font_bytes);
		ok = strings && strings <= LANG_PACK_MAX && size == strings + *font_bytes && f_lseek(&f, 0) == FR_OK &&
		     f_read(&f, buf, strings, &got) == FR_OK && got == strings;
	}
	f_close(&f);
	if (!ok) {
		tlog_error("lang: %s didn't read (%lu bytes)", path, (unsigned long)size);
		return 0;
	}
	u32 why = lang_pack_check(buf, strings);
	if (why != LANG_PACK_OK) {
		tlog_error("lang: %s rejected: %s", path, lang_pack_check_names[why]);
		return 0;
	}
	return strings;
}

// Reads the script font at [at, at + bytes) of choice i's pack into PSRAM
// (PSRAM_LANG_FONT_BASE) in pieces of pReadCache's room for packs, checks
// its CRC, and has the menu fonts use it; 1 if they do
static int load_font(u32 i, u32 at, u32 bytes)
{
	char path[32];
	path_of(path, sizeof(path), lang_choice(i)->code);
	FIL f;
	if (f_open(&f, path, FA_READ) != FR_OK || f_lseek(&f, at) != FR_OK) {
		tlog_error("lang: %s's font didn't open", path);
		f_close(&f);
		return 0;
	}
	u8 *buf = pReadCache + READCACHE_LANG;
	u32 done = 0, crc = 0, want = 0;
	int ok = 1;
	psram_begin();
	while (ok && done < bytes) {
		u32 n = bytes - done < READCACHE_LANG_SIZE ? bytes - done : READCACHE_LANG_SIZE;
		UINT got = 0;
		ok = f_read(&f, buf, n, &got) == FR_OK && got == n;
		if (ok && !done) {
			want = script_blob_crc(buf);
			crc = crc32(buf + 12, n >= SCRIPT_HEADER ? n - 12 : 0);
			ok = n >= SCRIPT_HEADER && script_header_ok(buf) && script_blob_size(buf) == bytes;
		} else if (ok) {
			crc = crc32_update(crc, buf, n);
		}
		ok = ok && psram_write((void *)(PSRAM_LANG_FONT_BASE + done), buf, n);
		done += n;
	}
	f_close(&f);
	if (!ok || crc != want) {
		tlog_error("lang: %s's font is damaged or didn't read", path);
		return 0;
	}
	if (!script_use((const u8 *)PSRAM_LANG_FONT_BASE, bytes)) {
		tlog_error("lang: %s's font is not one this kernel takes", path);
		return 0;
	}
	return 1;
}

int lang_switch(u32 i)
{
	// Nothing reads the pack, or its font, while PSRAM changes under it
	lang_use(NULL);
	script_drop();
	load.held = 0;
	load.current = 0;
	ui_list_cache_clear();
	if (i == 0 || i >= lang_count()) {
		tlog("lang: English");
		return i == 0;
	}
	u32 font_at = 0, font_bytes = 0;
	u32 size = read_pack(i, &font_at, &font_bytes);
	if (!size)
		return 0;
	psram_begin();
	if (!psram_write((void *)PSRAM_LANG_BASE, pReadCache + READCACHE_LANG, size))
		return 0;
	lang_use((const u8 *)PSRAM_LANG_BASE);
	load.current = (u8)i;
	tlog("lang: %s, %lu bytes", lang_choice(i)->code, (unsigned long)size);
	if (font_bytes && load_font(i, font_at, font_bytes))
		tlog("lang: %s's script font, %lu bytes", lang_choice(i)->code, (unsigned long)font_bytes);
	ui_list_cache_clear();  // rows drawn in the language before
	return 1;
}

u32 lang_current(void)
{
	return load.current;
}

void lang_boot(void)
{
	find_packs();
	u32 i = lang_index_of(Read_SET_info(assress_language));
	if (i)
		lang_switch(i);
}

// A list wants the row cache back while the launch's strings are in it
static void cache_back(void)
{
	lang_use(NULL);
}

void lang_before_launch(void)
{
	script_drop();  // the launch fills PSRAM
	if (!lang_using_pack() || load.held)
		return;
	void *room = ui_list_cache_lend(cache_back);
	load.held = 1;
	if (!lang_keep_launch(room, ROW_CACHE_BYTES))
		tlog_error("lang: the launch's strings didn't fit: English");
}

void lang_poll(void)
{
	if (!load.held)
		return;
	u32 i = load.current;
	tlog("lang: back from a launch");
	ui_list_cache_reclaim();
	lang_switch(i);
}
