#include "gamesave.h"

#include <stdio.h>
#include <string.h>

#include "Ezcard_OP.h"
#include "ez_define.h"
#include "ezkernel.h"
#include "ff15/ff.h"
#include "saveslot.h"
#include "settings_model.h"
#include "tlog.h"

int gamesave_rom_path_in(char *out, u32 size, const char *dir, const char *name)
{
	// FatFs may give a volume prefix ("0:/..."); the root is "/" or nothing
	const char *colon = strchr(dir, ':');
	const char *d = colon ? colon + 1 : dir;
	int root = d[0] == 0 || (d[0] == '/' && d[1] == 0);
	int n = root ? snprintf(out, size, "/%s", name)
		     : snprintf(out, size, "%s%s/%s", d[0] == '/' ? "" : "/", d, name);
	if (n < 0 || (u32)n >= size) {
		out[0] = 0;
		return -1;
	}
	return n;
}

int gamesave_nor_path(char *out, u32 size, const char *name)
{
	int n = snprintf(out, size, "%s%s", GAMESAVE_NOR_DIR, name);
	if (n < 0 || (u32)n >= size) {
		out[0] = 0;
		return -1;
	}
	return n;
}

// The calls below share these: a launch is deep in the stack by the time it
// reaches them
static char dir_buf[GAMESAVE_PATH_BYTES] EWRAM_BSS;
static char file_buf[GAMEREC_BYTES + 4] ALIGN(4) EWRAM_BSS;
static char path_buf[GAMESAVE_PATH_BYTES] EWRAM_BSS;
static FIL rec_fil EWRAM_BSS;

int gamesave_make_parents(const char *path)
{
	char *dir = dir_buf;
	// (a path elsewhere has its folders already)
	if (strncmp(path, SAVESLOT_SAVES_DIR "/", sizeof(SAVESLOT_SAVES_DIR)) != 0)
		return 1;
	u32 len = (u32)strnlen(path, GAMESAVE_PATH_BYTES);
	if (len >= GAMESAVE_PATH_BYTES)
		return 0;
	memcpy(dir, path, len + 1);
	int made = 1;
	for (u32 i = 1; i < len; i++) {
		if (dir[i] != '/')
			continue;
		dir[i] = 0;
		FRESULT r = f_mkdir(dir);
		made = r == FR_OK || r == FR_EXIST;
		if (!made) {
			tlog_error("save: can't make %s (FatFs %d)", dir, r);
			return 0;
		}
		dir[i] = '/';
	}
	return made;
}

// The record's path into dir_buf (the folder's, then the file's name)
static int record_path(const char *rom_path)
{
	int n = saveslot_game_dir(dir_buf, GAMESAVE_PATH_BYTES, rom_path);
	if (n < 0)
		return -1;
	int m = snprintf(dir_buf + n, GAMESAVE_PATH_BYTES - (u32)n, "/%s", GAMEREC_FILE);
	return m > 0 && (u32)n + (u32)m < GAMESAVE_PATH_BYTES ? n + m : -1;
}

void gamesave_record_read(const char *rom_path, struct gamerec *r)
{
	char *text = file_buf;
	gamerec_init(r);
	if (record_path(rom_path) < 0)
		return;
	const char *path = dir_buf;
	FIL *f = &rec_fil;
	if (f_open(f, path, FA_READ) != FR_OK)
		return;
	UINT got = 0;
	if (f_read(f, text, GAMEREC_BYTES, &got) != FR_OK)
		got = 0;
	f_close(f);
	gamerec_parse(r, text, got);
}

int gamesave_record_write(const char *rom_path, const struct gamerec *r)
{
	char *text = file_buf;
	u32 len = gamerec_format(text, GAMEREC_BYTES, r);
	if (!len || record_path(rom_path) < 0)
		return 0;
	char *path = path_buf;
	memcpy(path, dir_buf, GAMESAVE_PATH_BYTES);
	// In place and one sector: a cut leaves the old record or the new one;
	// its folders are made only when it has none yet
	FIL *f = &rec_fil;
	FRESULT res = f_open(f, path, FA_WRITE | FA_OPEN_ALWAYS);
	if (res == FR_NO_PATH && gamesave_make_parents(path))
		res = f_open(f, path, FA_WRITE | FA_OPEN_ALWAYS);
	if (res != FR_OK) {
		tlog_error("save: can't open %s (FatFs %d)", path, res);
		return 0;
	}
	UINT done = 0;
	res = f_write(f, text, len, &done);
	if (res == FR_OK && done == len)
		res = f_truncate(f);
	else
		res = res == FR_OK ? FR_DENIED : res;
	FRESULT closed = f_close(f);
	return res == FR_OK && closed == FR_OK;
}

int gamesave_current_path(char *out, u32 size, const char *rom_path, const struct gamerec *r)
{
	return saveslot_save_path(out, size, SAVESLOT_CENTRAL, rom_path, r->slot);
}
