// Safe writes of small text files on the SD card (safefile.h).
#include "safefile.h"

#include <stdio.h>
#include <string.h>

#include "crc32.h"
#include "ff.h"
#include "tlog.h"

// f_read and f_write take word-aligned buffers (CLAUDE.md)
#define CHUNK 512
static FIL sf_file EWRAM_BSS;
static u8 sf_chunk[CHUNK] EWRAM_BSS ALIGN(4);
static struct safefile_scan sf_main EWRAM_BSS, sf_bak EWRAM_BSS;

static struct {
	u32 limit;  // bytes of the copy to read: up to its CRC line
	u32 done;   // bytes read from it
	u32 have;   // bytes in sf_chunk
	u32 at;     // the next of them
	u8 open;
} rd EWRAM_BSS;

// path's backup copy's name into out (SAFEFILE_PATH_MAX bytes); 0 if too long
static int bak_name(char *out, const char *path)
{
	return (u32)snprintf(out, SAFEFILE_PATH_MAX, "%s.bak", path) < SAFEFILE_PATH_MAX;
}

// Reads the copy at path through s; 0 if it isn't there or can't be read
static int scan(const char *path, struct safefile_scan *s, u32 prefix_len)
{
	safefile_scan_begin(s, prefix_len);
	FRESULT res = f_open(&sf_file, path, FA_READ);
	if (res != FR_OK) {
		if (res != FR_NO_FILE && res != FR_NO_PATH)
			tlog_error("safefile: can't open %s (FatFs %u)", path, (unsigned)res);
		return 0;
	}
	UINT got = 0;
	do {
		res = f_read(&sf_file, sf_chunk, CHUNK, &got);
		if (res == FR_OK)
			safefile_scan_feed(s, sf_chunk, got);
	} while (res == FR_OK && got);
	f_close(&sf_file);
	if (res != FR_OK) {
		tlog_error("safefile: can't read %s (FatFs %u)", path, (unsigned)res);
		return 0;
	}
	safefile_scan_end(s);
	return 1;
}

// Scans path's copies and returns the one to read (safefile_pick); the scans
// are left in sf_main and sf_bak
static int pick(const char *path, const char *bak)
{
	int has_main = scan(path, &sf_main, 0);
	if (has_main && sf_main.valid)
		return SAFEFILE_MAIN;  // NAME.bak doesn't matter
	int has_bak = scan(bak, &sf_bak, has_main ? sf_main.size : 0);
	int from = safefile_pick(has_main ? &sf_main : NULL, has_bak ? &sf_bak : NULL);
	if (from == SAFEFILE_NONE && ((has_main && sf_main.size) || has_bak))
		tlog_error("safefile: %s: neither copy holds up: read as empty", path);
	return from;
}

int safefile_open(const char *path)
{
	char bak[SAFEFILE_PATH_MAX];
	TLOG_ASSERT(!rd.open);
	safefile_close();
	if (!bak_name(bak, path))
		return SAFEFILE_NONE;
	int from = pick(path, bak);
	if (from == SAFEFILE_NONE)
		return from;
	const struct safefile_scan *s = from == SAFEFILE_BAK ? &sf_bak : &sf_main;
	FRESULT res = f_open(&sf_file, from == SAFEFILE_BAK ? bak : path, FA_READ);
	if (res != FR_OK) {
		tlog_error("safefile: can't open %s again (FatFs %u)", path, (unsigned)res);
		return SAFEFILE_NONE;
	}
	memset(&rd, 0, sizeof(rd));
	rd.limit = s->valid ? s->body : s->size;
	rd.open = 1;
	if (from != SAFEFILE_MAIN)
		tlog("safefile: %s: %s", path,
		     from == SAFEFILE_BAK ? "read the backup copy" : "edited by hand: read as it is");
	return from;
}

int safefile_line(char *buf, u32 size)
{
	u32 len = 0;
	int any = 0, cut = 0;
	while (rd.open) {
		if (rd.at == rd.have) {
			u32 want = rd.limit - rd.done < CHUNK ? rd.limit - rd.done : CHUNK;
			UINT got = 0;
			if (!want)
				break;
			if (f_read(&sf_file, sf_chunk, want, &got) != FR_OK || !got) {
				tlog_error("safefile: a read stopped %lu bytes short",
					   (unsigned long)(rd.limit - rd.done));
				rd.limit = rd.done;
				break;
			}
			rd.have = got;
			rd.at = 0;
			rd.done += got;
		}
		char c = (char)sf_chunk[rd.at++];
		any = 1;
		if (c == '\n')
			break;
		if (len + 1 < size)
			buf[len++] = c;
		else
			cut = 1;
	}
	if (!any)
		return SAFEFILE_END;
	while (len && buf[len - 1] == '\r')
		len--;
	if (size)
		buf[len] = 0;
	return cut ? SAFEFILE_LONG : (int)len;
}

void safefile_close(void)
{
	if (rd.open)
		f_close(&sf_file);
	rd.open = 0;
}

// Makes the folders on the way to the file at path
static void make_folders(const char *path)
{
	char dir[SAFEFILE_PATH_MAX];
	snprintf(dir, sizeof(dir), "%s", path);
	for (char *p = dir + 1; *p; p++) {
		if (*p == '/') {
			*p = 0;
			f_mkdir(dir);
			*p = '/';
		}
	}
}

// Writes one copy: the text, then its CRC line
static int write_copy(const char *name, safefile_fill fill, void *ctx)
{
	FRESULT res = f_open(&sf_file, name, FA_WRITE | FA_CREATE_ALWAYS);
	if (res == FR_NO_PATH) {
		make_folders(name);
		res = f_open(&sf_file, name, FA_WRITE | FA_CREATE_ALWAYS);
	}
	if (res != FR_OK) {
		tlog_error("safefile: can't write %s (FatFs %u)", name, (unsigned)res);
		return 0;
	}
	u32 crc = 0, pos = 0, n;
	UINT put = 0;
	int ok = 1;
	while (ok && (n = fill(ctx, pos, (char *)sf_chunk, CHUNK)) != 0) {
		crc = crc32_update(crc, sf_chunk, n);
		ok = f_write(&sf_file, sf_chunk, n, &put) == FR_OK && put == n;
		pos += n;
	}
	if (ok) {
		// tests/userdata.sh cuts the power here
		tlog_debug("safefile: %s: %lu bytes written, sealing", name, (unsigned long)pos);
		n = safefile_crc_line((char *)sf_chunk, crc);
		ok = f_write(&sf_file, sf_chunk, n, &put) == FR_OK && put == n;
	}
	ok = (f_close(&sf_file) == FR_OK) && ok;
	if (!ok)
		tlog_error("safefile: writing %s failed", name);
	return ok;
}

int safefile_write(const char *path, safefile_fill fill, void *ctx)
{
	char bak[SAFEFILE_PATH_MAX];
	TLOG_ASSERT(!rd.open);
	safefile_close();
	if (!bak_name(bak, path))
		return 0;
	// The copy a reader takes now stays whole until the other is written
	int bak_first = pick(path, bak) != SAFEFILE_BAK;
	tlog_debug("safefile: writing %s, %s first", path, bak_first ? "the backup" : "the main copy");
	if (!write_copy(bak_first ? bak : path, fill, ctx))
		return 0;
	return write_copy(bak_first ? path : bak, fill, ctx);
}
