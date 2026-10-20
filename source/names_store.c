// The kernel's CRC names (namesdb.h, "The kernel's names"): names.db and
// crc.txt loaded into PSRAM, the walk's checks of the cache, Identify
// library's job and crc.txt's writes.
#include <stdio.h>
#include <string.h>

#include "namesdb.h"
#include "crc32.h"
#include "ff.h"
#include "library.h"
#include "safefile.h"
#include "tlog.h"
#include "userdata.h"
#include "engine/cover.h"
#include "engine/cycles.h"
#include "engine/jobs.h"
#include "engine/psram.h"
#include "ui/ui.h"

#define NAMES_CACHE_FILE "/torii/crc.txt"
#define NAMES_CACHE_BAK "/torii/crc.txt.bak"
#define NAMES_DIR "/torii"
#define NAMES_ART_DIR "/torii/art"  // B22's covers, looked up by CRC
#define NAMES_CACHE_FILE_MAX 0x100000u  // the most of crc.txt read (1 MB)
#define NAMES_IO_BYTES 2048  // a read or write per step: a read's CRC takes about 35,000 cycles
#define NAMES_CLEAR_BYTES 2048  // the cache's slots emptied per step
#define NAMES_LOAD_BYTES 512  // crc.txt read per step: a line costs about 15,000 cycles to keep
#define NAMES_STEP_CYCLES 30000  // one job call's work, as the library's
#define NAMES_KEY_REST 30  // the job waits for the keys to rest this many frames
#define NAMES_WRITE_IDLE 120  // and this long before it writes crc.txt (as the library's index)
#define NAMES_RENAME_EVERY 32  // new names before the index is sorted again
#define NAMES_WRITE_EVERY 256  // CRCs computed before crc.txt is written again (a write rewrites it all)
#define NAMES_CYCLES_PER_MS 16780u

#define DB ((u8 *)PSRAM_NAMES_DB_BASE)

_Static_assert(NAMES_CACHE_BYTES <= PSRAM_NAMES_CACHE_BYTES, "the CRC cache's PSRAM");
_Static_assert(PSRAM_NAMES_CACHE_BASE + PSRAM_NAMES_CACHE_BYTES <= PSRAM_WINDOW + PSRAM_WINDOW_BYTES,
	       "the CRC cache ends in the window");
_Static_assert(SAFEFILE_LINE_MAX >= NAMES_PATH_MAX + 40, "a line of crc.txt fits safefile_lines");

enum {
	P_START,  // nothing loaded
	P_DB,  // names.db's body, a chunk a step
	P_CLEAR,  // the cache's slots
	P_SCAN_MAIN,  // crc.txt's CRC checked, a chunk a step
	P_SCAN_BAK,  // and crc.txt.bak's, when crc.txt doesn't hold
	P_READ,  // the copy that holds, read into the cache a chunk a step
	P_DONE,
};

// What N.dirty says: the walk's changes (dates taken, records forgotten)
// wait for an idle moment, as the next walk finds them again anyway; new
// CRCs are written before a launch too
#define DIRTY_SOFT 1
#define DIRTY_HARD 2

enum {
	M_NONE,
	M_RECENT,  // the games on Recent that have no CRC yet: the ones just played
	M_ALL,  // Identify library
};

enum {
	I_NEXT,
	I_READ,
	I_DONE,
};

enum {
	X_IDLE,
	X_OPEN,  // the next copy
	X_BODY,  // its text, a chunk a step
};

// The lines of crc.txt as safefile_lines asks for them: a comment, then each
// record that is alive and was on the card at the last walk
struct line_cursor {
	u32 rec;  // the record after the last line given
};

static struct {
	u8 phase;
	u8 db_ok;  // names.db is loaded
	u8 cache_ok;  // crc.txt was read whole: it may be written back
	u8 dirty;  // the cache differs from crc.txt: DIRTY_* bits
	u8 active;  // crc.txt may be made and Recent identified: names_active
	u8 seen_ok;  // seen[] holds the last whole walk's files
	u8 job;
	u8 mode, step;
	u8 file_open;
	u8 want_recent;  // check Recent once the menu runs
	u8 console;
	u8 logged_full;
	// crc.txt's write (safefile.h's two copies, in steps through N.u.fil)
	u8 wphase;
	u8 wcopy;  // 0: the first copy, 1: the second
	// The copy to write next is crc.txt (else crc.txt.bak): the one a
	// reader doesn't take now, so the other stays whole until it is done
	u8 main_first;
	u32 wpos, wcrc, wstarted;
	// crc.txt's load (safefile.h's rules, in steps through N.u.fil)
	struct safefile_scan scan_main, scan_bak;
	u8 has_main, has_bak, first_line, line_long;
	u16 line_len;
	u32 limit, rd_done, lines_read, lines_bad;
	u32 at;  // names.db bytes loaded, or slot bytes cleared
	u32 started;
	struct names_cache cache;
	u32 cursor;  // M_RECENT: the Recent entry; M_ALL: the library record
	u32 total, done, computed, named, bytes, since_write, since_rename;
	u32 crc, size, date, code, read;
	u8 seen[NAMES_CACHE_MAX / 8];
	u32 walk_names;  // the names the walk under way found: names_identity
	char path[NAMES_PATH_MAX + 1];
	struct line_cursor lc;
	struct safefile_lines lines;
	union {
		FIL fil;
		FILINFO fi;
	} u;
	u8 buf[NAMES_IO_BYTES] ALIGN(4);
} N EWRAM_BSS;

static void put(void *dst, const void *src, u32 bytes)
{
	psram_write(dst, src, bytes);
}

static u32 ms_since(u32 start)
{
	return cycles_since(start) / NAMES_CYCLES_PER_MS;
}

static const struct namesdb_header *db_header(void)
{
	return (const struct namesdb_header *)DB;
}

static void close_file(void)
{
	if (N.file_open) {
		f_close(&N.u.fil);
		N.file_open = 0;
	}
}

static void mark_seen(u32 n)
{
	if (n < NAMES_CACHE_MAX)
		N.seen[n >> 3] |= (u8)(1 << (n & 7));
}

static int was_seen(u32 n)
{
	return n < NAMES_CACHE_MAX && (N.seen[n >> 3] >> (n & 7) & 1);
}

// --- Loading ------------------------------------------------------------------

void names_boot(void)
{
	memset(&N, 0, sizeof(N));
	N.job = JOB_NONE;
}

static void db_failed(const char *why)
{
	close_file();
	tlog("names: %s not loaded (%s)", NAMESDB_FILE, why);
	N.db_ok = 0;
	N.phase = P_CLEAR;
	N.at = 0;
}

// names.db's header, then the body a chunk at a time, into PSRAM
static void db_step(void)
{
	UINT got = 0;
	if (N.phase == P_START) {
		N.started = cycles_now();
		N.db_ok = 0;
		FRESULT fr = f_open(&N.u.fil, NAMESDB_FILE, FA_READ);
		if (fr != FR_OK) {
			if (fr == FR_NO_FILE || fr == FR_NO_PATH)
				tlog("names: no %s (just names-update makes it)", NAMESDB_FILE);
			else
				tlog_error("names: can't open %s (FatFs error %d)", NAMESDB_FILE, fr);
			N.phase = P_CLEAR;
			N.at = 0;
			return;
		}
		N.file_open = 1;
		struct namesdb_header *h = (struct namesdb_header *)N.buf;
		if (f_read(&N.u.fil, h, sizeof(*h), &got) != FR_OK || got != sizeof(*h)) {
			db_failed("a short header");
			return;
		}
		if (!namesdb_header_ok(h, PSRAM_NAMES_DB_BYTES) || f_size(&N.u.fil) != h->file_bytes) {
			db_failed("another version, too large, or damaged");
			return;
		}
		put(DB, h, sizeof(*h));
		N.at = sizeof(*h);
		N.phase = P_DB;
		return;
	}
	u32 total = db_header()->file_bytes, n = total - N.at;
	if (n > NAMES_IO_BYTES)
		n = NAMES_IO_BYTES;
	if (f_read(&N.u.fil, N.buf, n, &got) != FR_OK || got != n) {
		db_failed("a short read");
		return;
	}
	put(DB + N.at, N.buf, n);
	N.at += n;
	if (N.at < total)
		return;
	close_file();
	if (!namesdb_body_ok(DB)) {
		db_failed("its names don't end");
		return;
	}
	const struct namesdb_header *h = db_header();
	N.db_ok = 1;
	tlog("names: %s from %.16s: GBA %lu, GBC %lu, GB %lu, NES %lu, %lu codes, %lu KB in %lu ms", NAMESDB_FILE,
	     h->dat_version, (unsigned long)h->crc_count[LIB_GBA], (unsigned long)h->crc_count[LIB_GBC],
	     (unsigned long)h->crc_count[LIB_GB], (unsigned long)h->crc_count[LIB_NES], (unsigned long)h->code_count,
	     (unsigned long)(total >> 10), (unsigned long)ms_since(N.started));
	N.phase = P_CLEAR;
	N.at = 0;
}

// --- crc.txt, in steps -------------------------------------------------------
//
// safefile.h's reader (safefile_pick's choice of copy, a BOM, CR, blank and
// '#' lines) a chunk a step through a FIL of its own: B5's module reads a
// whole file in one call, which for a large cache held the menu for seconds.
// The line being read collects in N.lines.buf, unused until a write.

static void load_done(void)
{
	close_file();
	// CRCs are kept only on a card that uses them: one with names.db, a
	// crc.txt already (names-update's), or art (B22 looks covers up by CRC)
	FILINFO *fi = &N.u.fi;
	N.active = N.db_ok || N.has_main || N.has_bak || f_stat(NAMES_ART_DIR, fi) == FR_OK;
	if (N.cache.full) {
		N.cache_ok = 0;  // writing back would lose the lines left out
		tlog("names: the CRC cache is full: %s isn't written back", NAMES_CACHE_FILE);
	}
	N.dirty = 0;
	N.phase = P_DONE;
	N.want_recent = 1;
	tlog("names: %lu files' CRCs from %s in %lu ms%s", (unsigned long)N.lines_read, NAMES_CACHE_FILE,
	     (unsigned long)ms_since(N.started), N.cache_ok ? "" : " (it isn't written back)");
	if (N.lines_bad)
		tlog("names: %lu lines of %s left out (unreadable, or the cache is full)", (unsigned long)N.lines_bad,
		     NAMES_CACHE_FILE);
}

// Opens a copy to read; 0 if it isn't there (a card error marks the cache
// unwritable) or is larger than the cap
static int open_copy(const char *path)
{
	FRESULT fr = f_open(&N.u.fil, path, FA_READ);
	if (fr != FR_OK) {
		if (fr != FR_NO_FILE && fr != FR_NO_PATH) {
			tlog_error("names: can't open %s (FatFs error %d)", path, fr);
			N.cache_ok = 0;
		}
		return 0;
	}
	N.file_open = 1;
	if (f_size(&N.u.fil) > NAMES_CACHE_FILE_MAX) {
		tlog("names: %s is over %lu bytes: left alone", path, (unsigned long)NAMES_CACHE_FILE_MAX);
		close_file();
		N.cache_ok = 0;  // never written over: it holds the PC's CRCs
		return 0;
	}
	return 1;
}

// A chunk of the copy being scanned; 1 once it is all scanned (or failed:
// *ok 0)
static int scan_chunk(struct safefile_scan *sc, int *ok)
{
	UINT got = 0;
	FRESULT fr = f_read(&N.u.fil, N.buf, NAMES_LOAD_BYTES, &got);
	*ok = fr == FR_OK;
	if (!*ok) {
		tlog_error("names: reading %s failed (FatFs error %d)", NAMES_CACHE_FILE, fr);
		N.cache_ok = 0;
		close_file();
		return 1;
	}
	safefile_scan_feed(sc, N.buf, got);
	if (got == NAMES_LOAD_BYTES)
		return 0;
	safefile_scan_end(sc);
	close_file();
	return 1;
}

// The scans are done: open the copy safefile_pick takes
static void read_begin(void)
{
	int from = safefile_pick(N.has_main ? &N.scan_main : NULL, N.has_bak ? &N.scan_bak : NULL);
	// The next write leaves the copy read now whole until the other is
	// written (safefile_write's order)
	N.main_first = from == SAFEFILE_BAK;
	if (from == SAFEFILE_NONE) {
		if ((N.has_main && N.scan_main.size) || (N.has_bak && N.scan_bak.size))
			tlog_error("names: %s: neither copy holds up: read as empty", NAMES_CACHE_FILE);
		load_done();
		return;
	}
	const struct safefile_scan *sc = from == SAFEFILE_BAK ? &N.scan_bak : &N.scan_main;
	if (from != SAFEFILE_MAIN)
		tlog("names: %s: %s", NAMES_CACHE_FILE,
		     from == SAFEFILE_BAK ? "read the backup copy" : "edited by hand: read as it is");
	if (!open_copy(from == SAFEFILE_BAK ? NAMES_CACHE_BAK : NAMES_CACHE_FILE)) {
		load_done();
		return;
	}
	N.limit = sc->valid ? sc->body : sc->size;
	N.rd_done = 0;
	N.line_len = 0;
	N.line_long = 0;
	N.first_line = 1;
	N.phase = P_READ;
}

// The line collected in N.lines.buf is complete
static void line_end(void)
{
	char *line = N.lines.buf;
	u32 len = N.line_len;
	while (len && line[len - 1] == '\r')
		len--;
	line[len] = 0;
	if (N.first_line && len >= 3 && !memcmp(line, "\xEF\xBB\xBF", 3))
		line += 3;  // a BOM from a Windows editor
	N.first_line = 0;
	struct names_line l;
	int kept = 0, text = N.line_long || (line[0] && line[0] != '#');
	if (!N.line_long && names_parse_line(line, &l))
		kept = names_cache_put(&N.cache, l.path, l.size, l.date, l.crc, l.code, l.flags) != NAMES_CACHE_NONE;
	if (kept)
		N.lines_read++;
	else if (text)
		N.lines_bad++;  // too long, unreadable, or no room
	N.line_len = 0;
	N.line_long = 0;
}

static void read_chunk(void)
{
	UINT got = 0;
	u32 want = N.limit - N.rd_done;
	if (want > NAMES_LOAD_BYTES)
		want = NAMES_LOAD_BYTES;
	if (want && f_read(&N.u.fil, N.buf, want, &got) != FR_OK) {
		tlog_error("names: reading %s failed", NAMES_CACHE_FILE);
		N.cache_ok = 0;
		got = 0;
		want = 0;
	}
	N.rd_done += got;
	// A line at a time, as safefile_scan_feed does
	const u8 *p = N.buf, *end = N.buf + got;
	while (p < end) {
		const u8 *nl = memchr(p, '\n', (size_t)(end - p));
		u32 text = (u32)((nl ? nl : end) - p), room = (u32)sizeof(N.lines.buf) - 1 - N.line_len;
		if (text > room) {
			N.line_long = 1;
			text = room;
		}
		memcpy(N.lines.buf + N.line_len, p, text);
		N.line_len = (u16)(N.line_len + text);
		p = nl ? nl + 1 : end;
		if (nl)
			line_end();
	}
	if (want && got == want && N.rd_done < N.limit)
		return;
	if (N.line_len || N.line_long)
		line_end();  // a last line with no newline
	load_done();
}

int names_load_step(void)
{
	switch (N.phase) {
	case P_START:
	case P_DB:
		psram_begin();
		db_step();
		return 0;
	case P_CLEAR:
		psram_begin();
		if (N.at == 0)
			names_cache_init(&N.cache, (u8 *)PSRAM_NAMES_CACHE_BASE, put);
		if (names_cache_clear_part(&N.cache, N.at, NAMES_CLEAR_BYTES)) {
			N.phase = P_SCAN_MAIN;
			N.started = cycles_now();
			N.cache_ok = 1;
			N.has_main = N.has_bak = 0;
			N.lines_read = N.lines_bad = 0;
		}
		N.at += NAMES_CLEAR_BYTES;
		return 0;
	case P_SCAN_MAIN:
	case P_SCAN_BAK: {
		int main = N.phase == P_SCAN_MAIN, ok;
		struct safefile_scan *sc = main ? &N.scan_main : &N.scan_bak;
		if (!N.file_open) {
			if (!open_copy(main ? NAMES_CACHE_FILE : NAMES_CACHE_BAK)) {
				if (main)
					N.phase = P_SCAN_BAK;
				else
					read_begin();
				return 0;
			}
			safefile_scan_begin(sc);
		}
		if (!scan_chunk(sc, &ok))
			return 0;
		if (main) {
			N.has_main = (u8)ok;
			if (ok && sc->valid)
				read_begin();  // crc.txt holds: the backup doesn't matter
			else
				N.phase = P_SCAN_BAK;
		} else {
			N.has_bak = (u8)ok;
			read_begin();
		}
		return 0;
	}
	case P_READ:
		psram_begin();
		read_chunk();
		return N.phase == P_DONE;
	default:
		return 1;
	}
}

// --- The walk ----------------------------------------------------------------------

void names_walk_begin(void)
{
	memset(N.seen, 0, sizeof(N.seen));
	N.seen_ok = 0;
	N.walk_names = 0;
}

void names_walk_end(int whole)
{
	N.seen_ok = (u8)whole;
}

const char *names_walk_seen(const char *path, u32 size, u32 date, u32 console)
{
	if (N.phase != P_DONE)
		return NULL;
	u32 n = names_cache_find(&N.cache, path);
	if (n == NAMES_CACHE_NONE)
		return NULL;
	mark_seen(n);
	const struct names_rec *r = names_cache_rec(&N.cache, n);
	switch (names_cache_fits(r, size, date)) {
	case NAMES_STALE:
		if (!(names_rec_flags(r) & NAMES_DEAD)) {
			tlog("names: %s changed since its CRC: forgotten", path);
			names_cache_drop(&N.cache, n);
			N.dirty |= DIRTY_SOFT;
		}
		return NULL;
	case NAMES_FIT_DATED:
		names_cache_set_date(&N.cache, n, date);
		N.dirty |= DIRTY_SOFT;
		break;
	default:
		break;
	}
	const char *name = N.db_ok ? names_db_name(DB, r, console) : NULL;
	if (name) {
		// Which game has which name, in any order: a CRC that finds no name
		// changes nothing the lists sort by
		u32 x = r->hash ^ ((u32)name * 0x9E3779B1u);
		x ^= x >> 15;
		N.walk_names ^= x * 0x2C1B3C6Du;
	}
	return name;
}

u32 names_identity(void)
{
	if (N.phase != P_DONE || !N.db_ok)
		return 0;
	u32 id = db_header()->body_crc ^ N.walk_names;
	return id ? id : 1;
}

const char *names_lookup(const char *path, u32 size, u32 console)
{
	if (N.phase != P_DONE || !N.db_ok)
		return NULL;
	u32 n = names_cache_find(&N.cache, path);
	if (n == NAMES_CACHE_NONE)
		return NULL;
	const struct names_rec *r = names_cache_rec(&N.cache, n);
	return r->size == size ? names_db_name(DB, r, console) : NULL;
}

const char *names_dat_version(void)
{
	return N.phase == P_DONE && N.db_ok ? db_header()->dat_version : NULL;
}

// --- Writing crc.txt ---------------------------------------------------------------
//
// safefile.h's format and order, in steps with a FIL of its own: a write of
// a large cache takes seconds (SD writes run at about 200 KB/s on the cart),
// and B5's module writes in one call through its one FIL.

static u32 cache_line(void *ctx, u32 n, char *buf, u32 size)
{
	struct line_cursor *lc = ctx;
	if (n == 0) {
		lc->rec = 0;
		return (u32)snprintf(buf, size, "# Torii's CRC cache: CRC32, GBA game code, size, date, path\n");
	}
	for (; lc->rec < N.cache.count; lc->rec++) {
		if (N.seen_ok && !was_seen(lc->rec))
			continue;  // not on the card at the last walk
		u32 len = names_format_line(&N.cache, lc->rec, buf, size);
		if (len) {
			lc->rec++;
			return len;
		}
	}
	return 0;
}

static void write_begin(void)
{
	N.since_write = 0;
	if (!N.active) {
		N.dirty = 0;  // nothing uses CRCs on this card: crc.txt isn't made
		return;
	}
	if (!N.cache_ok) {
		tlog("names: %s isn't written: reading or writing it failed this session", NAMES_CACHE_FILE);
		N.dirty = 0;
		return;
	}
	N.dirty = 0;  // a change from now on writes again
	N.wphase = X_OPEN;
	N.wcopy = 0;
	N.wstarted = cycles_now();
}

static const char *write_name(void)
{
	return N.main_first ? NAMES_CACHE_FILE : NAMES_CACHE_BAK;
}

// A copy that can't be written whole is left empty, so a reader takes the
// other (as safefile_write does); the write ends there
static void write_failed(const char *what, FRESULT fr)
{
	tlog_error("names: can't write %s: %s (FatFs error %d)", write_name(), what, fr);
	if (N.file_open) {
		f_lseek(&N.u.fil, 0);
		f_truncate(&N.u.fil);
	}
	close_file();
	N.wphase = X_IDLE;
	N.cache_ok = 0;  // no more tries (and errors) this session: a read-only file, say
}

// One step of the write: a copy opened, or a chunk of its text
static void write_step(void)
{
	UINT done = 0;
	FRESULT fr;
	if (N.wphase == X_OPEN) {
		fr = f_open(&N.u.fil, write_name(), FA_WRITE | FA_CREATE_ALWAYS);
		if (fr == FR_NO_PATH) {
			f_mkdir(NAMES_DIR);
			fr = f_open(&N.u.fil, write_name(), FA_WRITE | FA_CREATE_ALWAYS);
		}
		if (fr != FR_OK) {
			write_failed("open", fr);
			return;
		}
		N.file_open = 1;
		memset(&N.lines, 0, sizeof(N.lines));
		N.lines.line = cache_line;
		N.lines.ctx = &N.lc;
		N.wpos = N.wcrc = 0;
		N.wphase = X_BODY;
		return;
	}
	u32 n = safefile_lines_fill(&N.lines, N.wpos, (char *)N.buf, sizeof(N.buf));
	int seal = !n;
	if (seal)
		n = safefile_crc_line((char *)N.buf, N.wcrc);  // the end: the CRC line
	else
		N.wcrc = crc32_update(N.wcrc, N.buf, n);
	fr = f_write(&N.u.fil, N.buf, n, &done);
	if (fr != FR_OK || done != n) {
		write_failed("the text", fr != FR_OK ? fr : FR_DENIED);  // short: the card is full
		return;
	}
	if (!seal) {
		N.wpos += n;
		return;
	}
	// The CRC line went out: this copy is done
	N.file_open = 0;
	fr = f_close(&N.u.fil);
	if (fr != FR_OK) {
		write_failed("close", fr);
		return;
	}
	if (N.wcopy++ == 0) {
		// This copy is whole: should the write stop before the other, the
		// next one leaves it alone and starts with the other
		N.main_first = !N.main_first;
		N.wphase = X_OPEN;
		return;
	}
	N.wphase = X_IDLE;
	N.main_first = 0;  // both whole: crc.txt is the one a reader takes
	tlog("names: wrote %s in %lu ms", NAMES_CACHE_FILE, (unsigned long)ms_since(N.wstarted));
}

// --- Identify library ------------------------------------------------------------

// The next game to identify into N.path, N.console; 0 when there is none
static int next_game(void)
{
	if (N.mode == M_RECENT) {
		while (N.cursor < userdata_recent_count()) {
			const char *p = userdata_recent_path(N.cursor++);
			u32 len = (u32)strlen(p);
			if (userdata_nor_name(p) || len > NAMES_PATH_MAX)
				continue;
			memcpy(N.path, p, len + 1);  // out of PSRAM before FatFs sees it
			// Only the library's games: crc.txt keeps no others
			if (library_find(N.path) == LIBRARY_NONE)
				continue;
			N.console = (u8)library_console_of(N.path);
			N.total++;
			return 1;
		}
		return 0;
	}
	// The records in walk order, which the rebuilds new names cause keep
	// (sorted order would change under the cursor)
	while (N.cursor < library_records()) {
		u32 rec = N.cursor++;
		if ((library_flags(rec) & LIB_F_GONE) || !library_path(rec, N.path, sizeof(N.path)))
			continue;
		N.console = (u8)(library_flags(rec) & LIB_F_CONSOLE);
		return 1;
	}
	return 0;
}

static void identify_finish(void)
{
	close_file();
	if (N.since_rename)
		library_changed();
	tlog("names: identified %lu of %lu games (%lu KB) in %lu ms; %lu have a database name",
	     (unsigned long)N.computed, (unsigned long)N.done, (unsigned long)(N.bytes >> 10),
	     (unsigned long)ms_since(N.started), (unsigned long)N.named);
	N.mode = M_NONE;
}

// One step: the next game's stat, or a chunk of its file
static int identify_step(void)
{
	UINT got = 0;
	switch (N.step) {
	case I_NEXT: {
		if (!next_game()) {
			identify_finish();
			return JOB_DONE;
		}
		N.done++;
		u32 known = names_cache_find(&N.cache, N.path);
		if (known != NAMES_CACHE_NONE && was_seen(known) && N.seen_ok &&
		    (names_rec_flags(names_cache_rec(&N.cache, known)) & (NAMES_CRC | NAMES_DATE | NAMES_DEAD)) ==
			    (NAMES_CRC | NAMES_DATE))
			return JOB_MORE;  // the walk checked it against its file: no stat
		FRESULT fr = f_stat(N.path, &N.u.fi);
		if (fr != FR_OK) {
			tlog("names: %s can't be read (FatFs error %d)", N.path, fr);
			return JOB_MORE;
		}
		N.size = (u32)N.u.fi.fsize;
		N.date = (u32)N.u.fi.fdate << 16 | N.u.fi.ftime;
		u32 n = names_cache_find(&N.cache, N.path);
		if (n != NAMES_CACHE_NONE) {
			const struct names_rec *r = names_cache_rec(&N.cache, n);
			int fits = names_cache_fits(r, N.size, N.date);
			if (fits != NAMES_STALE && (names_rec_flags(r) & NAMES_CRC)) {
				if (fits == NAMES_FIT_DATED) {
					names_cache_set_date(&N.cache, n, N.date);
					N.dirty |= DIRTY_SOFT;
				}
				return JOB_MORE;  // known
			}
		}
		fr = f_open(&N.u.fil, N.path, FA_READ);
		if (fr != FR_OK) {
			tlog("names: %s can't be opened (FatFs error %d)", N.path, fr);
			return JOB_MORE;
		}
		N.file_open = 1;
		N.crc = N.read = N.code = 0;
		N.step = I_READ;
		return JOB_MORE;
	}
	case I_READ:
		if (f_read(&N.u.fil, N.buf, NAMES_IO_BYTES, &got) != FR_OK) {
			tlog_error("names: reading %s failed at %lu bytes", N.path, (unsigned long)N.read);
			close_file();
			N.step = I_NEXT;
			return JOB_MORE;
		}
		// A GBA game's code, from its header
		if (N.read == 0 && N.console == LIB_GBA && got >= 0xB0)
			N.code = namesdb_code(N.buf + 0xAC);
		N.crc = crc32_update(N.crc, N.buf, got);
		N.read += got;
		if (got == NAMES_IO_BYTES && N.read < N.size)
			return JOB_MORE;
		close_file();
		N.step = I_DONE;
		return JOB_MORE;
	default: {
		N.step = I_NEXT;
		if (N.read != N.size) {
			tlog_error("names: %s gave %lu of its %lu bytes", N.path, (unsigned long)N.read,
				   (unsigned long)N.size);
			return JOB_MORE;
		}
		u32 n = names_cache_put(&N.cache, N.path, N.read, N.date, N.crc, N.code, NAMES_CRC | NAMES_DATE);
		if (n == NAMES_CACHE_NONE) {
			if (!N.logged_full)
				tlog("names: the CRC cache is full: %s and later games aren't kept", N.path);
			N.logged_full = 1;
			return JOB_MORE;
		}
		mark_seen(n);
		N.dirty |= DIRTY_HARD;
		N.computed++;
		N.since_write++;
		N.bytes += N.read;
		const char *name = N.db_ok ? names_db_name(DB, names_cache_rec(&N.cache, n), N.console) : NULL;
		tlog_debug("names: %s is %08lX%s%s", N.path, (unsigned long)N.crc, name ? ": " : "", name ? name : "");
		if (name) {
			N.named++;
			if (++N.since_rename >= NAMES_RENAME_EVERY) {
				N.since_rename = 0;
				library_changed();
			}
		}
		return JOB_MORE;
	}
	}
}

static void identify_begin(u32 mode)
{
	// The FIL may be names.db's load or crc.txt's write: close only a game
	// Identify was reading
	if (N.mode != M_NONE && N.step == I_READ)
		close_file();
	N.mode = (u8)mode;
	N.step = I_NEXT;
	N.cursor = 0;
	N.total = N.done = N.computed = N.named = N.bytes = N.since_write = N.since_rename = 0;
	N.logged_full = 0;
	N.started = cycles_now();
	if (mode == M_ALL)
		for (u32 c = 0; c < LIB_CONSOLES; c++)
			N.total += library_count(c);
}

// --- The job ------------------------------------------------------------------------

// One step of whatever is due: a write under way, a write between two games
// (every NAMES_WRITE_EVERY CRCs), the next piece of Identify, or a write of
// what the walk or Identify changed once the keys have rested
static int names_step(void)
{
	if (N.wphase != X_IDLE) {
		write_step();
		return JOB_MORE;
	}
	int between = N.step == I_NEXT;
	if (N.mode != M_NONE && !(between && N.dirty && N.since_write >= NAMES_WRITE_EVERY))
		return identify_step() == JOB_DONE && !N.dirty ? JOB_DONE : JOB_MORE;
	if (N.dirty && ui_idle_frames() >= NAMES_WRITE_IDLE) {
		write_begin();
		return N.wphase != X_IDLE ? JOB_MORE : JOB_DONE;
	}
	return N.dirty ? JOB_WAIT : JOB_DONE;
}

static int has_work(void)
{
	return N.phase == P_DONE && (N.mode != M_NONE || N.dirty || N.wphase != X_IDLE);
}

static int names_job(struct job *job)
{
	if (job->state == JOB_CANCEL) {
		if (N.wphase != X_IDLE && N.file_open) {
			// A copy left half written would read as a hand edit: empty it,
			// so the other holds; the write starts over later
			f_lseek(&N.u.fil, 0);
			f_truncate(&N.u.fil);
		}
		close_file();
		if (N.wphase != X_IDLE) {
			N.wphase = X_IDLE;
			N.dirty |= DIRTY_HARD;
		}
		return JOB_DONE;
	}
	if (!has_work())
		return JOB_DONE;
	// As the library's job: the covers and the library first, and what
	// the user moves keeps its frames
	const struct ui_view *top = ui_top();
	if (cover_loading() || library_busy() || ui_idle_frames() < NAMES_KEY_REST || ui_transitioning() ||
	    (top && (top->opening || (top->spec->flags & UI_VIEW_MODAL))))
		return JOB_WAIT;
	psram_begin();
	u32 start = cycles_now();
	int r;
	do
		r = names_step();
	while (r == JOB_MORE && cycles_since(start) < NAMES_STEP_CYCLES);
	return r;
}

void names_identify(void)
{
	N.active = 1;  // asked for: crc.txt may be made
	if (N.mode == M_ALL)
		return;
	tlog("names: identifying the library");
	identify_begin(M_ALL);
}

void names_identify_now(void)
{
	if (N.phase != P_DONE)
		return;
	if (N.mode == M_NONE)
		identify_begin(M_ALL);
	if (job_running(N.job))
		job_cancel(N.job);
	N.job = JOB_NONE;
	psram_begin();
	while (N.wphase != X_IDLE)
		write_step();
	while (identify_step() == JOB_MORE)
		;
	if (N.dirty)
		write_begin();
	while (N.wphase != X_IDLE)
		write_step();
}

int names_identifying(u32 *done, u32 *total)
{
	if (done)
		*done = N.done;
	if (total)
		*total = N.total;
	return N.mode == M_ALL;
}

void names_poll(void)
{
	if (N.phase != P_DONE)
		return;
	if (N.want_recent && N.active && N.mode == M_NONE && library_ready() && !library_busy()) {
		N.want_recent = 0;
		identify_begin(M_RECENT);
	}
	if (has_work() && !job_running(N.job))
		N.job = job_start(names_job, 0, JOB_OWNER_NONE);
}

void names_before_launch(void)
{
	if (job_running(N.job))
		job_cancel(N.job);  // closes a file it read or wrote
	N.job = JOB_NONE;
	close_file();
	if (N.mode != M_NONE)
		tlog("names: identifying stopped for a launch (%lu of %lu games done)", (unsigned long)N.done,
		     (unsigned long)N.total);
	N.mode = M_NONE;
	if (N.phase == P_DONE && ((N.dirty & DIRTY_HARD) || N.wphase != X_IDLE)) {
		// PSRAM is about to hold the game: what changed goes to the card first
		psram_begin();
		write_begin();
		while (N.wphase != X_IDLE)
			write_step();
	}
	N.phase = P_START;
	N.db_ok = 0;
}
