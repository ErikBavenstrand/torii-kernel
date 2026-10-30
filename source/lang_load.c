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
#include "ui/rowcache.h"
#include "ui/ui.h"

#define LANG_DIR "/torii/lang"

_Static_assert(LANG_PACK_MAX <= PSRAM_LANG_BYTES, "a pack fits its PSRAM");
_Static_assert(LANG_PACK_MAX <= READCACHE_LANG_SIZE, "a pack fits its share of pReadCache");
_Static_assert(READCACHE_LANG + READCACHE_LANG_SIZE <= MAX_pReadCache_size, "READCACHE_LANG");
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

// Reads choice i's pack into pReadCache and checks it whole; its bytes, or 0
static u32 read_pack(u32 i)
{
	char path[32];
	path_of(path, sizeof(path), lang_choice(i)->code);
	FIL f;
	if (f_open(&f, path, FA_READ) != FR_OK) {
		tlog_error("lang: %s didn't open", path);
		return 0;
	}
	u32 size = (u32)f_size(&f);
	UINT got = 0;
	u8 *buf = pReadCache + READCACHE_LANG;
	int ok = size <= LANG_PACK_MAX && f_read(&f, buf, size, &got) == FR_OK && got == size;
	f_close(&f);
	if (!ok) {
		tlog_error("lang: %s didn't read (%lu bytes)", path, (unsigned long)size);
		return 0;
	}
	u32 why = lang_pack_check(buf, size);
	if (why != LANG_PACK_OK) {
		tlog_error("lang: %s rejected: %s", path, lang_pack_check_names[why]);
		return 0;
	}
	return size;
}

int lang_switch(u32 i)
{
	// Nothing reads the pack while PSRAM changes under it
	lang_use(NULL);
	load.held = 0;
	load.current = 0;
	ui_list_cache_clear();
	if (i == 0 || i >= lang_count()) {
		tlog("lang: English");
		return i == 0;
	}
	u32 size = read_pack(i);
	if (!size)
		return 0;
	psram_begin();
	if (!psram_write((void *)PSRAM_LANG_BASE, pReadCache + READCACHE_LANG, size))
		return 0;
	lang_use((const u8 *)PSRAM_LANG_BASE);
	load.current = (u8)i;
	ui_list_cache_clear();  // rows drawn in the language before
	tlog("lang: %s, %lu bytes", lang_choice(i)->code, (unsigned long)size);
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
