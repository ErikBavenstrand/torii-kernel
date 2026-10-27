// Safe writes of small text files on the SD card (safefile.h).
#include "safefile.h"

#include <stdio.h>
#include <string.h>

#include "crc32.h"
#include "cutpoint.h"
#include "ff.h"
#include "tlog.h"

// f_read and f_write take word-aligned buffers (CLAUDE.md)
#define CHUNK 512
static FIL sf_file EWRAM_BSS;
static u8 sf_chunk[CHUNK] EWRAM_BSS ALIGN(4);
static struct safefile_scan sf_main EWRAM_BSS, sf_bak EWRAM_BSS;
static u8 sf_failed EWRAM_BSS;  // a read failed since safefile_open began
static u8 sf_big EWRAM_BSS;     // and a copy was over its max (sf_failed too)
// The backup copy's path, and a folder on the way to it: one file is open at
// a time, and these are too long for the stack (SAFEFILE_PATH_MAX)
static char sf_bak_path[SAFEFILE_PATH_MAX] EWRAM_BSS;
static char sf_dir[SAFEFILE_PATH_MAX] EWRAM_BSS;
static u8 sf_cuts EWRAM_BSS;  // safefile_write_cut: cutpoints between the steps

static struct {
	u32 limit;  // bytes of the copy to read: up to its CRC line
	u32 done;   // bytes read from it
	u32 have;   // bytes in sf_chunk
	u32 at;     // the next of them
	u8 open;
	u8 first;  // the next line is the first: a UTF-8 BOM before it is skipped
} rd EWRAM_BSS;

// path's backup copy's name into out (SAFEFILE_PATH_MAX bytes); 0 if too long
static int bak_name(char *out, const char *path)
{
	return (u32)snprintf(out, SAFEFILE_PATH_MAX, "%s.bak", path) < SAFEFILE_PATH_MAX;
}

// Reads the copy at path through s; 0 if it isn't there, can't be read or
// is larger than max bytes (a hand edit gone wrong: reading it would stall)
static int scan(const char *path, struct safefile_scan *s, u32 max)
{
	safefile_scan_begin(s);
	FRESULT res = f_open(&sf_file, path, FA_READ);
	if (res != FR_OK) {
		if (res != FR_NO_FILE && res != FR_NO_PATH) {
			tlog_error("safefile: can't open %s (FatFs %u)", path, (unsigned)res);
			sf_failed = 1;
		}
		return 0;
	}
	if (f_size(&sf_file) > max) {
		tlog("safefile: %s is %lu bytes, over %lu: left alone", path, (unsigned long)f_size(&sf_file),
		     (unsigned long)max);
		f_close(&sf_file);
		sf_failed = 1;  // as a failed read: nothing writes over it
		sf_big = 1;
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
		sf_failed = 1;
		return 0;
	}
	safefile_scan_end(s);
	return 1;
}

// Scans path's copies and returns the one to read (safefile_pick); the scans
// are left in sf_main and sf_bak
static int pick(const char *path, const char *bak, u32 max)
{
	sf_failed = sf_big = 0;
	int has_main = scan(path, &sf_main, max);
	if (has_main && sf_main.valid)
		return SAFEFILE_MAIN;  // NAME.bak doesn't matter
	int has_bak = scan(bak, &sf_bak, max);
	return safefile_pick(has_main ? &sf_main : NULL, has_bak ? &sf_bak : NULL);
}

int safefile_reads_bak(const char *path, u32 max)
{
	char *bak = sf_bak_path;
	TLOG_ASSERT(!rd.open);
	return bak_name(bak, path) && pick(path, bak, max) == SAFEFILE_BAK;
}

int safefile_open(const char *path, u32 max)
{
	char *bak = sf_bak_path;
	TLOG_ASSERT(!rd.open);
	safefile_close();
	if (!bak_name(bak, path))
		return SAFEFILE_NONE;
	int from = pick(path, bak, max);
	if (from == SAFEFILE_NONE) {
		if (sf_main.size || sf_bak.size)
			tlog_error("safefile: %s: neither copy holds up: read as empty", path);
		return from;
	}
	const struct safefile_scan *s = from == SAFEFILE_BAK ? &sf_bak : &sf_main;
	FRESULT res = f_open(&sf_file, from == SAFEFILE_BAK ? bak : path, FA_READ);
	if (res != FR_OK) {
		tlog_error("safefile: can't open %s again (FatFs %u)", path, (unsigned)res);
		sf_failed = 1;
		return SAFEFILE_NONE;
	}
	memset(&rd, 0, sizeof(rd));
	rd.limit = s->valid ? s->body : s->size;
	rd.open = 1;
	rd.first = 1;
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
				sf_failed = 1;
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
	if (rd.first && len >= 3 && memcmp(buf, "\xEF\xBB\xBF", 3) == 0) {
		len -= 3;  // a BOM from a Windows editor
		memmove(buf, buf + 3, len);
	}
	rd.first = 0;
	if (size)
		buf[len] = 0;
	return cut ? SAFEFILE_LONG : (int)len;
}

int safefile_read_part(void *buf, u32 size)
{
	TLOG_ASSERT(((u32)buf & 3) == 0);
	if (!rd.open)
		return -1;
	u32 have = rd.have - rd.at, n;
	rd.first = 0;
	if (have) {
		// What safefile_line left first
		n = have < size ? have : size;
		memcpy(buf, sf_chunk + rd.at, n);
		rd.at += n;
		return (int)n;
	}
	u32 left = rd.limit - rd.done;
	n = left < size ? left : size;
	if (!n)
		return 0;
	UINT got = 0;
	FRESULT res = f_read(&sf_file, buf, n, &got);
	rd.done += got;
	if (res != FR_OK || got != n) {
		tlog_error("safefile: a read stopped %lu bytes short", (unsigned long)(left - got));
		sf_failed = 1;
		return -1;
	}
	return (int)n;
}

int safefile_failed(void)
{
	return sf_failed;
}

int safefile_too_big(void)
{
	return sf_big;
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
	char *dir = sf_dir;
	snprintf(dir, SAFEFILE_PATH_MAX, "%s", path);
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
		// A file the card won't make (/torii a file, a write-protected
		// card) fails every write the same way: an error once a boot
		static u8 told;
		int unmakeable = res == FR_NO_PATH || res == FR_EXIST || res == FR_DENIED || res == FR_WRITE_PROTECTED;
		if (unmakeable && told)
			tlog("safefile: can't write %s (FatFs %u)", name, (unsigned)res);
		else
			tlog_error("safefile: can't write %s (FatFs %u)", name, (unsigned)res);
		told |= (u8)unmakeable;
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
		if (sf_cuts)
			cutpoint("safefile: a copy's text written, not sealed");
		n = safefile_crc_line((char *)sf_chunk, crc);
		ok = f_write(&sf_file, sf_chunk, n, &put) == FR_OK && put == n;
	}
	if (!ok) {
		// A part of the text (the card full) would read as a hand edit and
		// outrank the other copy: leave the copy empty, as a cut write does
		f_lseek(&sf_file, 0);
		f_truncate(&sf_file);
	}
	ok = (f_close(&sf_file) == FR_OK) && ok;
	if (sf_cuts)
		cutpoint("safefile: a copy closed");
	if (!ok)
		tlog_error("safefile: writing %s failed", name);
	return ok;
}

int safefile_write(const char *path, u32 max, safefile_fill fill, void *ctx)
{
	char *bak = sf_bak_path;
	TLOG_ASSERT(!rd.open);
	safefile_close();
	if (!bak_name(bak, path))
		return 0;
	// The copy a reader takes now stays whole until the other is written
	int bak_first = pick(path, bak, max) != SAFEFILE_BAK;
	if (sf_failed) {
		tlog_error("safefile: %s: not written, as reading it failed", path);
		return 0;  // which copy holds up is unknown: write neither
	}
	tlog_debug("safefile: writing %s, %s first", path, bak_first ? "the backup" : "the main copy");
	if (!write_copy(bak_first ? bak : path, fill, ctx))
		return 0;
	return write_copy(bak_first ? path : bak, fill, ctx);
}

int safefile_write_cut(const char *path, u32 max, safefile_fill fill, void *ctx)
{
	sf_cuts = 1;
	int ok = safefile_write(path, max, fill, ctx);
	sf_cuts = 0;
	return ok;
}
