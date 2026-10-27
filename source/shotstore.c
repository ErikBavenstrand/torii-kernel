// The in-game menu's screenshots on the card (shotstore.h).
#include "shotstore.h"

#include <stdio.h>
#include <string.h>

#include "cutpoint.h"
#include "ez_define.h"
#include "Ezcard_OP.h"
#include "ff.h"
#include "ingame_abi.h"
#include "lang.h"
#include "ppu_render.h"
#include "saveslot.h"
#include "slotstore.h"
#include "statestore.h"
#include "tlog.h"
#include "ui/dialog.h"

extern u8 pReadCache[];

#define CART_SRAM ((vu8 *)0x0E000000)
#define SHOT_ROWS 16  // lines written at a time
#define SHOT_PATH_BYTES (sizeof(SHOTSTORE_DIR) + INGAME_SHOT_FILE + 4)

// pReadCache from READCACHE_SHOT, word-aligned throughout for f_write
struct shot_work {
	u8 vram[INGAME_SHOT_VRAM_BYTES];
	u16 pal[512], oam[512];
	struct ingame_shot head;
	struct ppu_scratch scratch;
	u16 line[PPU_W];
	u8 rows[SHOT_ROWS * PPU_BMP_ROW];
	u8 bmp_head[(PPU_BMP_HEAD + 3) & ~3u];
	char path[(SHOT_PATH_BYTES + 3) & ~3u];
	FIL fil;
};
_Static_assert(sizeof(struct shot_work) <= READCACHE_SHOT_SIZE, "the screenshot's share of pReadCache");
_Static_assert(READCACHE_SHOT + READCACHE_SHOT_SIZE <= MAX_pReadCache_size, "READCACHE_SHOT");
_Static_assert(PPU_VRAM_BYTES == INGAME_SHOT_VRAM_BYTES && PPU_IO_HALVES == INGAME_SHOT_IO, "the dump is the frame");
_Static_assert(PPU_BMP_ROW % 4 == 0 && PPU_H % SHOT_ROWS == 0, "whole word-aligned blocks of lines");

static struct shot_work *work(void)
{
	return (struct shot_work *)(void *)(pReadCache + READCACHE_SHOT);
}

// n bytes of cart SRAM from byte offset at to or from RAM, a page window at a time
static void sram_io(u32 at, void *ram, u32 n, u32 to_sram)
{
	u8 *p = ram;
	while (n) {
		u32 room = 0x10000 - (at & 0xFFF), part = n < room ? n : room;
		SetRampage((u16)(at >> 12));
		statestore_sram_bytes(CART_SRAM + (at & 0xFFF), p, part, to_sram);
		at += part;
		p += part;
		n -= part;
	}
	SetRampage(0);
}

// 1 if path is a whole screenshot: its size and its head
static int file_whole(const char *path)
{
	struct shot_work *w = work();
	UINT got = 0;
	if (f_open(&w->fil, path, FA_READ) != FR_OK)
		return 0;
	int whole = f_size(&w->fil) == PPU_BMP_BYTES && f_read(&w->fil, w->rows, PPU_BMP_HEAD, &got) == FR_OK &&
		    got == PPU_BMP_HEAD;
	f_close(&w->fil);
	ppu_bmp_head(w->bmp_head);
	return whole && memcmp(w->rows, w->bmp_head, PPU_BMP_HEAD) == 0;
}

static int path_of(char *out, const char *file)
{
	int n = snprintf(out, SHOT_PATH_BYTES, "%s/%s.bmp", SHOTSTORE_DIR, file);
	return n > 0 && (u32)n < SHOT_PATH_BYTES;
}

// The file's name: the game's, the clock's date and time (or a number with
// no clock), a number more if taken; kept in the dump's header
static int name_pick(const char *game)
{
	struct shot_work *w = work();
	struct saveslot_stamp t;
	FILINFO info;
	char base[INGAME_SHOT_FILE - 8], name[INGAME_SHOT_FILE];  // room for a number after base
	slotstore_now(&t);
	const char *g = game[0] ? game : "Screenshot";
	if (t.year)
		snprintf(base, sizeof(base), "%.60s %04u-%02u-%02u %02u-%02u-%02u", g, t.year, t.month, t.day, t.hour,
			 t.min, t.sec);
	else
		snprintf(base, sizeof(base), "%.60s", g);
	for (u32 n = t.year ? 1 : 2; n < 1000; n++) {
		if (n == 1)
			snprintf(name, sizeof(name), "%s", base);
		else
			snprintf(name, sizeof(name), "%s %lu", base, (unsigned long)n);
		if (!path_of(w->path, name))
			return 0;
		FRESULT r = f_stat(w->path, &info);
		if (r == FR_NO_FILE || r == FR_NO_PATH)
			break;
		if (r != FR_OK || n == 999)
			return 0;
	}
	memcpy(w->head.file, name, sizeof(name));
	sram_io(INGAME_SRAM_SHOT + offsetof(struct ingame_shot, file), w->head.file, INGAME_SHOT_FILE, 1);
	cutpoint("screenshot: its name kept");
	return 1;
}

// The dump drawn into the file at w->path: a blank head, the lines, the head
static int file_write(void)
{
	struct shot_work *w = work();
	FIL *f = &w->fil;
	UINT put = 0;
	f_mkdir("/torii");
	f_mkdir(SHOTSTORE_DIR);
	// The journal first: a power cut loses the dump (cart SRAM past the
	// save has no battery), so the next boot deletes what it left
	UINT len = (UINT)strlen(w->head.file);
	memcpy(w->rows, w->head.file, len);
	if (f_open(f, SHOTSTORE_JOURNAL, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
		return 0;
	int journal = f_write(f, w->rows, len, &put) == FR_OK && put == len;
	journal = f_close(f) == FR_OK && journal;
	cutpoint("screenshot: its journal written");
	if (!journal || f_open(f, w->path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
		return 0;
	memset(w->bmp_head, 0, sizeof(w->bmp_head));
	int ok = f_write(f, w->bmp_head, PPU_BMP_HEAD, &put) == FR_OK && put == PPU_BMP_HEAD;
	cutpoint("screenshot: a blank head written");
	sram_io(INGAME_SHOT_PAL, w->pal, sizeof(w->pal), 0);
	sram_io(INGAME_SHOT_OAM, w->oam, sizeof(w->oam), 0);
	sram_io(INGAME_SHOT_VRAM, w->vram, sizeof(w->vram), 0);
	struct ppu_frame frame = {w->vram, w->pal, w->oam, w->head.io};
	u32 start = torii_frames, drawing = 0;
	for (u32 y = 0; ok && y < PPU_H; y += SHOT_ROWS) {
		u32 t0 = torii_frames;
		for (u32 k = 0; k < SHOT_ROWS; k++) {
			ppu_render_line(&frame, y + k, w->line, &w->scratch);
			ppu_bmp_row(w->line, w->rows + k * PPU_BMP_ROW);
		}
		drawing += torii_frames - t0;
		ok = f_write(f, w->rows, sizeof(w->rows), &put) == FR_OK && put == sizeof(w->rows);
	}
	ok = ok && f_sync(f) == FR_OK;
	cutpoint("screenshot: its lines written");
	ppu_bmp_head(w->bmp_head);
	ok = ok && f_lseek(f, 0) == FR_OK && f_write(f, w->bmp_head, PPU_BMP_HEAD, &put) == FR_OK &&
	     put == PPU_BMP_HEAD;
	ok = f_close(f) == FR_OK && ok;
	cutpoint("screenshot: its head written and closed");
	if (ok) {
		f_unlink(SHOTSTORE_JOURNAL);
		cutpoint("screenshot: its journal deleted");
	}
	tlog("screenshot: %s %s in %lu frames (%lu drawing)", w->path, ok ? "written" : "not written",
	     (unsigned long)(torii_frames - start), (unsigned long)drawing);
	return ok;
}

void shotstore_recover(void)
{
	struct shot_work *w = work();
	UINT got = 0;
	if (f_open(&w->fil, SHOTSTORE_JOURNAL, FA_READ) != FR_OK)
		return;
	int read = f_read(&w->fil, w->rows, INGAME_SHOT_FILE - 1, &got) == FR_OK;
	f_close(&w->fil);
	char name[INGAME_SHOT_FILE];
	memcpy(name, w->rows, got);
	name[got] = 0;
	if (read && got && !strchr(name, '/') && path_of(w->path, name) && f_stat(w->path, NULL) == FR_OK &&
	    !file_whole(w->path)) {
		FRESULT r = f_unlink(w->path);
		cutpoint("screenshot: a broken one deleted");
		tlog("screenshot: %s, cut short, %s", w->path, r == FR_OK ? "deleted" : "not deleted");
	}
	f_unlink(SHOTSTORE_JOURNAL);
	cutpoint("screenshot: its journal deleted");
}

int shotstore_file(const char *game)
{
	struct shot_work *w = work();
	struct ingame_shot *h = &w->head;
	sram_io(INGAME_SRAM_SHOT, h, sizeof(*h), 0);
	if (h->magic != INGAME_SHOT_MAGIC || h->version != 1 || h->bytes != sizeof(*h)) {
		tlog_error("screenshot: none in cart SRAM, though the menu took one");
		return 1;  // nothing to try again
	}
	dialog_notice(NULL, gl_states->shot_saving, DIALOG_INFO);
	sram_io(INGAME_SRAM_SHOT, h, sizeof(*h), 0);  // pReadCache again, after the notice's frames
	int ok;
	char name[INGAME_SHOT_FILE];
	memcpy(name, h->file, sizeof(name));
	if (name[0] && memchr(name, 0, sizeof(name)) && !strchr(name, '/'))
		ok = path_of(w->path, name);  // a name picked before a power cut
	else
		ok = name_pick(game);
	if (ok && file_whole(w->path))
		tlog("screenshot: %s is whole already", w->path);
	else
		ok = ok && file_write();
	if (ok) {
		u32 none = 0;
		sram_io(INGAME_SRAM_SHOT, &none, 4, 1);
	}
	dialog_notice_end();
	if (!ok)
		dialog_message(NULL, gl_states->shot_failed, DIALOG_ERROR);
	return ok;
}
