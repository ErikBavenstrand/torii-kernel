#include "slotstore.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "Ezcard_OP.h"
#include "RTC.h"
#include "crc32.h"
#include "cutpoint.h"
#include "ez_define.h"
#include "ezkernel.h"
#include "ff15/ff.h"
#include "gamesave.h"
#include "settings_model.h"
#include "tlog.h"

#define SS_PATH 320
#define SS_CHUNK 2048
#define SS_NAMES 64
#define SS_STRAYS 4
#define JOURNAL "/torii/slotop.txt"
#define JOURNAL_HEADER "torii-slotop 1\n"

// Everything a call needs, in READCACHE_SLOTS of pReadCache (ez_define.h). The
// paths are named by who uses them: the top-level operations take a to e, the
// helpers below take the rest, and no helper runs inside another that uses the
// same one.
struct ss_work {
	FIL fa, fb;
	DIR dir;
	FILINFO fno;
	u8 buf[2][SS_CHUNK] ALIGN(4);
	char names[SS_NAMES][SAVESLOT_BACKUP_NAME_BYTES];
	u32 sizes[SS_NAMES];
	const char *ptr[SS_NAMES];
	u32 idx[SS_NAMES];
	char strays[SS_STRAYS][SAVESLOT_BACKUP_NAME_BYTES + 8];
	struct slotstore_slot slots[SLOTSTORE_MAX_SLOTS];
	const char *slotp[SLOTSTORE_MAX_SLOTS];
	char a[SS_PATH], b[SS_PATH], c[SS_PATH], d[SS_PATH], e[SS_PATH];
	char tmpf[SS_PATH], bakf[SS_PATH];  // ss_install
	char hp[SS_PATH], hn[SS_PATH];      // slotstore_push_file
	char scratch[SS_PATH], scratch2[SS_PATH];  // ss_mkdirs, ss_move_case, ss_drop_strays
	char trash[SS_PATH];                       // slotstore_delete
	char r1[SS_PATH], r2[SS_PATH], r3[SS_PATH];  // slotstore_recover
	char pfx[SAVESLOT_NAME_BYTES + 64], base[SAVESLOT_NAME_BYTES * 2], clean[SAVESLOT_NAME_BYTES];  // ss_list
};
_Static_assert(sizeof(struct ss_work) <= READCACHE_SLOTS_SIZE, "the slot store's work area fits READCACHE_SLOTS");

#define W ((struct ss_work *)(pReadCache + READCACHE_SLOTS))

// snprintf, for strings built inside the work area: the compiler can't tell
// that the destination and the arguments are different fields of it
static int __attribute__((format(printf, 3, 4))) ss_snprintf(char *dst, u32 size, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(dst, size, fmt, ap);
	va_end(ap);
	return n;
}

static int test_mode = -1;

void slotstore_test_mode(int mode)
{
	test_mode = mode;
}

static int ss_mode(void)
{
	return test_mode >= 0 ? test_mode : gamesave_mode();
}

/* ---- The clock and the setting ---- */

static int bcd_ok(u8 v)
{
	return (v & 0xF) <= 9 && (v >> 4) <= 9;
}

void slotstore_now(struct saveslot_stamp *out)
{
	u8 t[7];
	memset(out, 0, sizeof(*out));
	rtc_enable();
	rtc_get(t);
	rtc_disenable();
	u8 f[6] = {t[0], (u8)(t[1] & 0x1F), (u8)(t[2] & 0x3F), (u8)(t[4] & 0x3F), (u8)(t[5] & 0x7F), (u8)(t[6] & 0x7F)};
	for (u32 i = 0; i < sizeof(f); i++)
		if (!bcd_ok(f[i]))
			return;
	u32 y = 2000 + UNBCD(f[0]), mo = UNBCD(f[1]), d = UNBCD(f[2]), h = UNBCD(f[3]), mi = UNBCD(f[4]),
	    s = UNBCD(f[5]);
	if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 59)
		return;
	out->year = (u16)y;
	out->month = (u8)mo;
	out->day = (u8)d;
	out->hour = (u8)h;
	out->min = (u8)mi;
	out->sec = (u8)s;
}

u32 slotstore_keep(void)
{
	u16 v = Read_SET_info(assress_history);
	return settings_history_keep[v < SET_HISTORY_CHOICES ? v : SET_HISTORY_DEFAULT];
}

int slotstore_size_ok(u32 size, u32 want)
{
	return saveslot_import_size_ok(size, want);
}

/* ---- FatFs helpers ---- */

// 1 if path exists, 0 if not, -1 if the card didn't say
static int ss_exists(const char *path)
{
	FRESULT r = f_stat(path, NULL);
	if (r == FR_OK)
		return 1;
	return r == FR_NO_FILE || r == FR_NO_PATH ? 0 : -1;
}

static int ends_with(const char *s, const char *suffix)
{
	u32 n = (u32)strlen(s), m = (u32)strlen(suffix);
	return n >= m && !strcmp(s + n - m, suffix);
}

// mkdir -p for the first len bytes of path (a folder's path): every part
static int ss_mkdirs_n(const char *path, u32 len)
{
	char *d = W->scratch;
	if (len == 0 || len >= SS_PATH)
		return SLOTSTORE_PATH;
	memcpy(d, path, len);
	d[len] = 0;
	for (u32 i = 1; i <= len; i++) {
		if (d[i] != '/' && d[i] != 0)
			continue;
		char keep = d[i];
		d[i] = 0;
		FRESULT r = f_mkdir(d);
		d[i] = keep;
		if (r != FR_OK && r != FR_EXIST) {
			tlog_error("slot: can't make %s: f_mkdir gave %d", d, r);
			return SLOTSTORE_CARD;
		}
	}
	return SLOTSTORE_OK;
}

static int ss_mkdirs(const char *dir)
{
	return ss_mkdirs_n(dir, (u32)strlen(dir));
}

// The folder holding path
static int ss_mkdirs_parent(const char *path)
{
	const char *slash = strrchr(path, '/');
	return slash && slash != path ? ss_mkdirs_n(path, (u32)(slash - path)) : SLOTSTORE_OK;
}

// Reads len bytes of f into buf; 1 if all of them came
static int read_full(FIL *f, void *buf, u32 len)
{
	UINT got = 0;
	return f_read(f, buf, len, &got) == FR_OK && got == len;
}

static u32 chunk_of(u32 total, u32 off)
{
	return total - off < SS_CHUNK ? total - off : SS_CHUNK;
}

// 1 if the files are the same bytes, 0 if not, -1 if one can't be read
static int ss_same(const char *x, const char *y)
{
	FIL *a = &W->fa, *b = &W->fb;
	if (f_open(a, x, FA_READ) != FR_OK)
		return -1;
	if (f_open(b, y, FA_READ) != FR_OK) {
		f_close(a);
		return -1;
	}
	int same = f_size(a) == f_size(b);
	u32 total = (u32)f_size(a);
	for (u32 off = 0; same >= 0 && same && off < total; off += SS_CHUNK) {
		u32 len = chunk_of(total, off);
		if (!read_full(a, W->buf[0], len) || !read_full(b, W->buf[1], len))
			same = -1;
		else if (memcmp(W->buf[0], W->buf[1], len))
			same = 0;
	}
	f_close(a);
	f_close(b);
	return same;
}

// Writes dst (made new) as a copy of src, reads it back and compares. On any
// failure dst is removed. SLOTSTORE_OK or SLOTSTORE_CARD
static int ss_copy(const char *src, const char *dst)
{
	FIL *a = &W->fa, *b = &W->fb;
	if (f_open(a, src, FA_READ) != FR_OK) {
		tlog_error("slot: can't read %s", src);
		return SLOTSTORE_CARD;
	}
	FRESULT r = f_open(b, dst, FA_WRITE | FA_CREATE_ALWAYS);
	if (r != FR_OK) {
		f_close(a);
		tlog_error("slot: can't create %s: f_open gave %d", dst, r);
		return SLOTSTORE_CARD;
	}
	u32 total = (u32)f_size(a), off;
	FRESULT werr = FR_OK;
	for (off = 0; off < total && werr == FR_OK; off += SS_CHUNK) {
		u32 len = chunk_of(total, off);
		UINT done = 0;
		if (!read_full(a, W->buf[0], len)) {
			werr = FR_DISK_ERR;
			break;
		}
		werr = f_write(b, W->buf[0], len, &done);
		if (werr == FR_OK && done != len)
			werr = FR_DENIED;  // a full card
	}
	f_close(a);
	FRESULT serr = werr == FR_OK ? f_sync(b) : FR_OK;
	FRESULT cerr = f_close(b);
	if (werr != FR_OK || serr != FR_OK || cerr != FR_OK) {
		tlog_error("slot: writing %s: f_write gave %d, f_sync %d, f_close %d", dst, werr, serr, cerr);
		f_unlink(dst);
		return SLOTSTORE_CARD;
	}
	if (ss_same(src, dst) != 1) {
		tlog_error("slot: %s read back wrong", dst);
		f_unlink(dst);
		return SLOTSTORE_CARD;
	}
	return SLOTSTORE_OK;
}

// Writes dst (made new) with size bytes of 0xFF and reads it back
static int ss_write_blank(const char *dst, u32 size)
{
	FIL *b = &W->fb;
	memset(W->buf[0], 0xFF, SS_CHUNK);
	FRESULT r = f_open(b, dst, FA_WRITE | FA_CREATE_ALWAYS);
	if (r != FR_OK) {
		tlog_error("slot: can't create %s: f_open gave %d", dst, r);
		return SLOTSTORE_CARD;
	}
	FRESULT werr = FR_OK;
	for (u32 off = 0; off < size && werr == FR_OK; off += SS_CHUNK) {
		u32 len = chunk_of(size, off);
		UINT done = 0;
		werr = f_write(b, W->buf[0], len, &done);
		if (werr == FR_OK && done != len)
			werr = FR_DENIED;
	}
	FRESULT serr = werr == FR_OK ? f_sync(b) : FR_OK;
	FRESULT cerr = f_close(b);
	int ok = werr == FR_OK && serr == FR_OK && cerr == FR_OK && f_open(b, dst, FA_READ) == FR_OK;
	if (ok) {
		ok = f_size(b) == size;
		for (u32 off = 0; ok && off < size; off += SS_CHUNK) {
			u32 len = chunk_of(size, off);
			ok = read_full(b, W->buf[1], len) && !memcmp(W->buf[0], W->buf[1], len);
		}
		f_close(b);
	}
	if (!ok) {
		tlog_error("slot: writing %s: f_write gave %d, f_sync %d, f_close %d", dst, werr, serr, cerr);
		f_unlink(dst);
		return SLOTSTORE_CARD;
	}
	return SLOTSTORE_OK;
}

// Moves src to dst (their folders there), as a rename that a power cut may
// have cut: nothing at src is done; if dst is there too, either it is a second
// name of src's chain (dropped), or two different things (SLOTSTORE_TAKEN,
// both left)
static int ss_move(const char *src, const char *dst)
{
	int s = ss_exists(src), d = ss_exists(dst);
	if (s < 0 || d < 0)
		return SLOTSTORE_CARD;
	if (!s)
		return SLOTSTORE_OK;
	if (d) {
		FRESULT r = f_unlink_alias(src, dst);
		if (r == FR_OK) {
			tlog("slot: %s was a second name of %s, dropped", src, dst);
			return SLOTSTORE_OK;
		}
		return r == FR_DENIED ? SLOTSTORE_TAKEN : SLOTSTORE_CARD;
	}
	FRESULT r = f_rename(src, dst);
	if (r != FR_OK) {
		tlog_error("slot: moving %s to %s: f_rename gave %d", src, dst, r);
		return SLOTSTORE_CARD;
	}
	return SLOTSTORE_OK;
}

// 1 if the entry at path is stored with exactly path's last part as its name,
// 0 if with another case, -1 if there is none or the card didn't say. (f_stat
// answers with the name it was asked for, so the folder is read.)
static int ss_stored_exactly(const char *path)
{
	const char *slash = strrchr(path, '/');
	char *dir = W->scratch2;
	u32 dlen = (u32)(slash - path);
	if (dlen >= SS_PATH)
		return -1;
	memcpy(dir, path, dlen);
	dir[dlen] = 0;
	if (f_opendir(&W->dir, dlen ? dir : "/") != FR_OK)
		return -1;
	int found = -1;
	for (;;) {
		if (f_readdir(&W->dir, &W->fno) != FR_OK || !W->fno.fname[0])
			break;
		if (saveslot_names_equal(W->fno.fname, slash + 1)) {
			found = !strcmp(W->fno.fname, slash + 1);
			break;
		}
	}
	f_closedir(&W->dir);
	return found;
}

// Renames the file or folder at path to path with other case (new_path), which
// FAT takes for the same name: through new_path + ".ren". Finishes a cut one
// the same way. A cut inside either f_rename leaves two names on a chain, and
// the one that isn't final is dropped by its entry alone.
static int ss_move_case(const char *old_path, const char *new_path)
{
	char *tmp = W->scratch;
	if (strlen(new_path) + 5 > SS_PATH)
		return SLOTSTORE_PATH;
	ss_snprintf(tmp, SS_PATH, "%s.ren", new_path);
	int t = ss_exists(tmp), n = ss_exists(new_path);
	if (t < 0 || n < 0)
		return SLOTSTORE_CARD;
	int same = 0;
	if (n) {
		same = ss_stored_exactly(new_path);  // already in its final case
		if (same < 0)
			return SLOTSTORE_CARD;
	}
	if (t && n) {
		FRESULT r = same ? f_unlink_alias(tmp, new_path) : f_unlink_alias(new_path, tmp);
		if (r != FR_OK)
			return r == FR_DENIED ? SLOTSTORE_TAKEN : SLOTSTORE_CARD;
		if (same)
			return SLOTSTORE_OK;
		n = 0;
	}
	if (t) {
		FRESULT r = f_rename(tmp, new_path);
		return r == FR_OK ? SLOTSTORE_OK : SLOTSTORE_CARD;
	}
	if (!n || same)
		return SLOTSTORE_OK;
	if (f_rename(old_path, tmp) != FR_OK)
		return SLOTSTORE_CARD;
	cutpoint("slot: case rename: to the temporary name");
	return f_rename(tmp, new_path) == FR_OK ? SLOTSTORE_OK : SLOTSTORE_CARD;
}

// Moves the files of the folder src into the folder dst (made), one by one,
// and removes src. A folder moved to another parent would need its ".." entry
// changed after the move, which a cut leaves wrong (FatFs does it last); files
// have no such entry. Run again after a cut, it goes on from the files left.
// Uses W->hp and W->hn.
static int ss_move_files(const char *src, const char *dst)
{
	int r = ss_mkdirs(dst);
	if (r)
		return r;
	for (u32 guard = 0; guard < 4096; guard++) {
		FRESULT fr = f_opendir(&W->dir, src);
		if (fr == FR_NO_PATH || fr == FR_NO_FILE)
			return SLOTSTORE_OK;  // moved before
		if (fr != FR_OK)
			return SLOTSTORE_CARD;
		fr = f_readdir(&W->dir, &W->fno);
		int dir = W->fno.fattrib & AM_DIR;
		int more = fr == FR_OK && W->fno.fname[0];
		if (more) {
			if (ss_snprintf(W->hp, SS_PATH, "%s/%s", src, W->fno.fname) >= SS_PATH ||
			    ss_snprintf(W->hn, SS_PATH, "%s/%s", dst, W->fno.fname) >= SS_PATH)
				fr = FR_INVALID_NAME;
		}
		f_closedir(&W->dir);
		if (fr != FR_OK)
			return fr == FR_INVALID_NAME ? SLOTSTORE_PATH : SLOTSTORE_CARD;
		if (!more) {
			FRESULT u = f_unlink(src);
			return u == FR_OK ? SLOTSTORE_OK : SLOTSTORE_CARD;
		}
		if (dir) {
			tlog_error("slot: %s holds a folder, %s: left where it is", src, W->hp);
			return SLOTSTORE_OK;
		}
		r = ss_move(W->hp, W->hn);
		if (r)
			return r;
	}
	return SLOTSTORE_CARD;
}

/* ---- Directory scans ---- */

// Remembers a stray <x>.new file name found in a scan; they go after it, in
// ss_drop_strays
static u32 stray_count;

static void note_stray(const char *name)
{
	if (stray_count < SS_STRAYS && strlen(name) < sizeof(W->strays[0]))
		strcpy(W->strays[stray_count++], name);
}

// Removes the .new files the last scan of dir found: the file one is the
// start of, if it is there, is the same chain (a cut inside the rename that
// took the .new away), else it is a partial write
static void ss_drop_strays(const char *dir)
{
	char *path = W->scratch;
	for (u32 i = 0; i < stray_count; i++) {
		char *name = W->strays[i];
		u32 len = (u32)strlen(name);
		ss_snprintf(path, SS_PATH, "%s/%s", dir, name);
		FRESULT r = FR_DENIED;
		if (len > 4 && !strcmp(name + len - 4, ".new")) {
			char *final = W->scratch2;
			ss_snprintf(final, SS_PATH, "%s/%.*s", dir, (int)(len - 4), name);
			if (ss_exists(final) == 1)
				r = f_unlink_alias(path, final);
		}
		if (r != FR_OK)
			r = f_unlink(path);
		tlog("slot: removed the stray %s, %d", path, r);
	}
	stray_count = 0;
}

// Reads the history folder dir: W->names takes the valid backup names (up to
// SS_NAMES), W->sizes their sizes, and strays are dropped. *count: how many
// stored, *maxn: the highest n among all valid ones, *newest: the index of
// the stored one with the highest n (valid if *count). SLOTSTORE_OK, or
// SLOTSTORE_CARD.
static int ss_scan_hist(const char *dir, u32 *count, u32 *maxn, u32 *newest)
{
	*count = *maxn = *newest = 0;
	stray_count = 0;
	FRESULT r = f_opendir(&W->dir, dir);
	if (r == FR_NO_PATH || r == FR_NO_FILE)
		return SLOTSTORE_OK;
	if (r != FR_OK)
		return SLOTSTORE_CARD;
	u32 top_stored = 0;
	for (;;) {
		r = f_readdir(&W->dir, &W->fno);
		if (r != FR_OK || !W->fno.fname[0])
			break;
		if (W->fno.fattrib & AM_DIR)
			continue;
		u32 n;
		if (ends_with(W->fno.fname, ".new")) {
			note_stray(W->fno.fname);
			continue;
		}
		if (strlen(W->fno.fname) >= SAVESLOT_BACKUP_NAME_BYTES ||
		    !saveslot_parse_backup_name(W->fno.fname, &n, NULL))
			continue;
		if (n > *maxn)
			*maxn = n;
		if (*count < SS_NAMES) {
			strcpy(W->names[*count], W->fno.fname);
			W->sizes[*count] = (u32)W->fno.fsize;
			if (n > top_stored) {
				top_stored = n;
				*newest = *count;
			}
			(*count)++;
		}
	}
	f_closedir(&W->dir);
	if (r != FR_OK)
		return SLOTSTORE_CARD;
	ss_drop_strays(dir);
	return SLOTSTORE_OK;
}

// Deletes the oldest versions of dir's, in W->names (count of them), beyond
// keep. Removes them from the lists too.
static int ss_prune(const char *dir, u32 *count, u32 keep)
{
	char *path = W->hn;
	for (u32 i = 0; i < *count; i++)
		W->ptr[i] = W->names[i];
	u32 drop = saveslot_prune(W->ptr, *count, keep, W->idx);
	for (u32 k = 0; k < drop; k++) {
		ss_snprintf(path, SS_PATH, "%s/%s", dir, W->names[W->idx[k]]);
		FRESULT r = f_unlink(path);
		tlog("slot: pruned %s, f_unlink gave %d", path, r);
		if (r != FR_OK)
			return SLOTSTORE_CARD;
		cutpoint("history: version pruned");
	}
	*count -= drop;
	return SLOTSTORE_OK;
}

/* ---- Paths of a slot ---- */

// The slot's file path into out, checked to launch. The code or SLOTSTORE_OK
static int slot_path(char *out, const char *rom, const char *slot)
{
	int n = saveslot_save_path(out, SS_PATH, ss_mode(), rom, slot);
	return n < 0 || n > SAV_INFO_NAME_MAX ? SLOTSTORE_PATH : SLOTSTORE_OK;
}

static int hist_path(char *out, const char *rom, const char *slot)
{
	int n = saveslot_backup_dir(out, SS_PATH, rom, slot);
	return n < 0 || n > SAV_INFO_HIST_MAX ? SLOTSTORE_PATH : SLOTSTORE_OK;
}

static int both_paths(char *file, char *hist, const char *rom, const char *slot)
{
	int r = slot_path(file, rom, slot);
	return r ? r : hist_path(hist, rom, slot);
}

/* ---- Listing ---- */

static int is_sav(const char *name)
{
	u32 n = (u32)strlen(name);
	return n > 4 && name[n - 4] == '.' && (name[n - 3] | 0x20) == 's' && (name[n - 2] | 0x20) == 'a' &&
	       (name[n - 1] | 0x20) == 'v';
}

static int prefix_nocase(const char *s, const char *prefix, u32 len)
{
	for (u32 i = 0; i < len; i++) {
		char a = s[i], b = prefix[i];
		if (a >= 'A' && a <= 'Z')
			a = (char)(a + 32);
		if (b >= 'A' && b <= 'Z')
			b = (char)(b + 32);
		if (a != b)
			return 0;
	}
	return 1;
}

static int slot_dir_open(const char *rom, char *prefix, u32 *prefix_len)
{
	char *dir = W->scratch;
	*prefix_len = 0;
	if (ss_mode() == SAVESLOT_BESIDE_ROM) {
		// The path of slot "x" is <rom dir>/<stem>.x.sav: <rom dir>/<stem>. is the prefix
		int n = saveslot_save_path(W->c, SS_PATH, SAVESLOT_BESIDE_ROM, rom, "x");
		if (n < 6)
			return SLOTSTORE_PATH;
		W->c[n - 5] = 0;
		const char *slash = strrchr(W->c, '/');
		u32 dlen = (u32)(slash - W->c);
		memcpy(dir, W->c, dlen);
		dir[dlen] = 0;
		strcpy(prefix, slash + 1);
		*prefix_len = (u32)strlen(prefix);
		if (!dir[0])
			strcpy(dir, "/");
	} else if (saveslot_game_dir(dir, SS_PATH, rom) < 0) {
		return SLOTSTORE_PATH;
	}
	FRESULT r = f_opendir(&W->dir, dir);
	if (r == FR_NO_PATH || r == FR_NO_FILE)
		return 1;  // no folder: no slots
	return r == FR_OK ? SLOTSTORE_OK : SLOTSTORE_CARD;
}

// Fills W->slots with the game's slots (up to SLOTSTORE_MAX_SLOTS); returns how many exist
static int ss_list(const char *rom)
{
	char *prefix = W->pfx;
	u32 plen, found = 0;
	int open = slot_dir_open(rom, prefix, &plen);
	if (open == 1)
		return 0;
	if (open < 0)
		return open;
	for (;;) {
		FRESULT r = f_readdir(&W->dir, &W->fno);
		if (r != FR_OK || !W->fno.fname[0]) {
			f_closedir(&W->dir);
			return r == FR_OK ? (int)found : SLOTSTORE_CARD;
		}
		const char *name = W->fno.fname;
		u32 len = (u32)strlen(name);
		if ((W->fno.fattrib & AM_DIR) || !is_sav(name) || len <= plen + 4 || !prefix_nocase(name, prefix, plen))
			continue;
		char *base = W->base, *clean = W->clean;
		u32 blen = len - plen - 4;
		if (blen >= SAVESLOT_NAME_BYTES)
			continue;
		memcpy(base, name + plen, blen);
		base[blen] = 0;
		if (saveslot_clean_name(clean, base) != blen || strcmp(clean, base))
			continue;
		if (found < SLOTSTORE_MAX_SLOTS) {
			strcpy(W->slots[found].name, base);
			W->slots[found].size = (u32)W->fno.fsize;
		}
		found++;
	}
}

static void slot_pointers(u32 n)
{
	for (u32 i = 0; i < n && i < SLOTSTORE_MAX_SLOTS; i++)
		W->slotp[i] = W->slots[i].name;
}

// The index in W->slots of the slot called name (ignoring case), or -1
static int find_slot(u32 n, const char *name)
{
	for (u32 i = 0; i < n && i < SLOTSTORE_MAX_SLOTS; i++)
		if (saveslot_names_equal(W->slots[i].name, name))
			return (int)i;
	return -1;
}

int slotstore_list(const char *rom_path, struct slotstore_slot *out, u32 max)
{
	int n = ss_list(rom_path);
	if (n < 0)
		return n;
	for (u32 i = 0; i < max && i < (u32)n && i < SLOTSTORE_MAX_SLOTS; i++)
		out[i] = W->slots[i];
	return n;
}

int slotstore_current(const char *rom_path, char *slot)
{
	struct gamerec rec;
	gamesave_record_read(rom_path, &rec);
	strcpy(slot, rec.slot);
	return SLOTSTORE_OK;
}

int slotstore_set_current(const char *rom_path, const char *slot)
{
	int n = ss_list(rom_path);
	if (n < 0)
		return n;
	int i = find_slot((u32)n, slot);
	if (i < 0)
		return SLOTSTORE_NO_SLOT;
	struct gamerec rec;
	gamesave_record_read(rom_path, &rec);
	if (!strcmp(rec.slot, W->slots[i].name))
		return SLOTSTORE_OK;
	strcpy(rec.slot, W->slots[i].name);
	return gamesave_record_write(rom_path, &rec) ? SLOTSTORE_OK : SLOTSTORE_CARD;
}

/* ---- The journal ---- */

// The kinds: 'R' rename, 'D' delete, 'I' install (a file replaced through
// .tmp and .bak: the first line is its path, the others "-"). Lines: header, kind and mode, ROM path,
// two names (R: old and new; D: the slot, then the trash folder), CRC-32 of all
// before it in hex. It is written whole to a file of its own and synced
// before the first step; a cut one is no journal (the CRC), and then nothing
// has been moved.
static int journal_write(char kind, const char *rom, const char *x, const char *y)
{
	char *t = (char *)W->buf[0];
	int n = ss_snprintf(t, SS_CHUNK - 12, JOURNAL_HEADER "%c%d\n%s\n%s\n%s\n", kind, ss_mode(), rom, x, y);
	if (n <= 0 || n >= SS_CHUNK - 12)
		return SLOTSTORE_PATH;
	n += ss_snprintf(t + n, 12, "%08lx\n", (unsigned long)crc32(t, (u32)n));
	FRESULT r = f_open(&W->fa, JOURNAL, FA_WRITE | FA_CREATE_ALWAYS);
	if (r != FR_OK) {
		tlog_error("slot: can't write the journal: f_open gave %d", r);
		return SLOTSTORE_CARD;
	}
	UINT done = 0;
	FRESULT werr = f_write(&W->fa, t, (UINT)n, &done);
	FRESULT serr = werr == FR_OK ? f_sync(&W->fa) : FR_OK;
	FRESULT cerr = f_close(&W->fa);
	if (werr != FR_OK || done != (UINT)n || serr != FR_OK || cerr != FR_OK) {
		tlog_error("slot: writing the journal: f_write gave %d, f_sync %d, f_close %d", werr, serr, cerr);
		f_unlink(JOURNAL);
		return SLOTSTORE_CARD;
	}
	cutpoint("slotop: journal written");
	return SLOTSTORE_OK;
}

static void journal_clear(void)
{
	FRESULT r = f_unlink(JOURNAL);
	tlog("slot: journal cleared, f_unlink gave %d", r);
	cutpoint("slotop: journal cleared");
}

static int slotstore_delete_orphan_history(const char *rom_path, const char *slot);

/* ---- Rename ---- */

// The record follows a renamed slot
static int record_follows(const char *rom, const char *old_name, const char *new_name)
{
	struct gamerec rec;
	gamesave_record_read(rom, &rec);
	if (!saveslot_names_equal(rec.slot, old_name) || !strcmp(rec.slot, new_name))
		return SLOTSTORE_OK;
	strcpy(rec.slot, new_name);
	return gamesave_record_write(rom, &rec) ? SLOTSTORE_OK : SLOTSTORE_CARD;
}

// The steps of a rename, from whatever is done. recovering: decide by what
// exists (saveslot_recover); else do all of them. Uses W->d, W->e for the old
// and new files' paths and W->b, W->c for the history folders.
static int rename_steps(int mode, const char *rom, const char *old_name, const char *new_name, int recovering)
{
	int saved_mode = test_mode;
	test_mode = mode;
	char *os = W->d, *ns = W->e, *oh = W->b, *nh = W->c;
	int r = both_paths(os, oh, rom, old_name);
	if (!r)
		r = both_paths(ns, nh, rom, new_name);
	test_mode = saved_mode;
	if (r)
		return r;
	if (saveslot_names_equal(old_name, new_name)) {
		// Only the case differs: through a temporary name, to the end
		r = ss_move_case(os, ns);
		if (r == SLOTSTORE_OK) {
			cutpoint("rename: save in its new case");
			r = ss_move_case(oh, nh);
		}
		if (r == SLOTSTORE_OK)
			cutpoint("rename: history in its new case");
		return r ? r : record_follows(rom, old_name, new_name);
	}
	if (recovering) {
		int flags[4] = {ss_exists(os), ss_exists(ns), ss_exists(oh), ss_exists(nh)};
		if (flags[0] < 0 || flags[1] < 0 || flags[2] < 0 || flags[3] < 0)
			return SLOTSTORE_CARD;
		// A cut inside an f_rename leaves two names on one chain: the new
		// name goes, and the rename is as if it had not got that far
		if (flags[0] && flags[1] && f_unlink_alias(ns, os) == FR_OK)
			flags[1] = 0;
		if (flags[2] && flags[3] && f_unlink_alias(nh, oh) == FR_OK)
			flags[3] = 0;
		enum saveslot_recovery act = saveslot_recover(
			(u32)((flags[0] ? SAVESLOT_OLD_SAVE : 0) | (flags[1] ? SAVESLOT_NEW_SAVE : 0) |
			      (flags[2] ? SAVESLOT_OLD_HIST : 0) | (flags[3] ? SAVESLOT_NEW_HIST : 0)));
		tlog("slot: rename %s to %s of %s: recovery says %d", old_name, new_name, rom, act);
		if (act == SAVESLOT_RECOVER_NOTHING)  // not started, or done but for the record
			return flags[1] && !flags[0] ? record_follows(rom, old_name, new_name) : SLOTSTORE_OK;
		if (act == SAVESLOT_RECOVER_UNDO) {
			r = ss_move(ns, os);
			if (r == SLOTSTORE_OK)
				cutpoint("rename: undone, save back");
			return r;
		}
	} else {
		if ((r = ss_mkdirs_parent(ns)))
			return r;
		if ((r = ss_move(os, ns)))
			return r;
		cutpoint("rename: save moved");
	}
	// The history folder follows (a game that has none has nothing to move)
	if (ss_exists(oh) == 1) {
		if ((r = ss_mkdirs_parent(nh)))
			return r;
		if ((r = ss_move(oh, nh)))
			return r;
		cutpoint("rename: history moved");
	}
	r = record_follows(rom, old_name, new_name);
	if (r == SLOTSTORE_OK)
		cutpoint("rename: record follows");
	return r;
}

int slotstore_rename(const char *rom_path, const char *slot, const char *name, char *clean_out)
{
	char clean[SAVESLOT_NAME_BYTES];
	if (!saveslot_clean_name(clean, name))
		return SLOTSTORE_NAME;
	if (clean_out)
		strcpy(clean_out, clean);
	if (slotstore_recover() < 0)
		return SLOTSTORE_CARD;
	int n = ss_list(rom_path);
	if (n < 0)
		return n;
	int i = find_slot((u32)n, slot);
	if (i < 0)
		return SLOTSTORE_NO_SLOT;
	if (n > SLOTSTORE_MAX_SLOTS)
		n = SLOTSTORE_MAX_SLOTS;
	slot_pointers((u32)n);
	if (saveslot_name_taken(clean, W->slotp, (u32)n, (u32)i))
		return SLOTSTORE_TAKEN;
	char old_name[SAVESLOT_NAME_BYTES];
	strcpy(old_name, W->slots[i].name);  // the name as the file spells it
	if (!strcmp(old_name, clean))
		return SLOTSTORE_OK;
	int r = both_paths(W->a, W->b, rom_path, clean);
	if (r)
		return r;
	if (!saveslot_names_equal(old_name, clean)) {
		// A history folder of the new name with no slot (left by a delete cut
		// short) goes to the trash first, so the renamed slot starts clean
		r = hist_path(W->c, rom_path, clean);
		if (!r && ss_exists(W->c) == 1) {
			r = slotstore_delete_orphan_history(rom_path, clean);
			if (r)
				return r;
		}
	}
	r = journal_write('R', rom_path, old_name, clean);
	if (r)
		return r;
	r = rename_steps(ss_mode(), rom_path, old_name, clean, 0);
	if (r) {
		// Left for the next power-on to finish or undo, unless nothing moved
		tlog_error("slot: rename %s to %s of %s stopped: %d", old_name, clean, rom_path, r);
		if (r == SLOTSTORE_TAKEN || r == SLOTSTORE_PATH)
			journal_clear();
		return r;
	}
	journal_clear();
	tlog("slot: renamed %s to %s of %s", old_name, clean, rom_path);
	return SLOTSTORE_OK;
}

/* ---- Create, duplicate ---- */

int slotstore_create(const char *rom_path, const char *name, const char *copy_from, u32 blank_size, char *clean_out)
{
	char clean[SAVESLOT_NAME_BYTES];
	if (!saveslot_clean_name(clean, name))
		return SLOTSTORE_NAME;
	if (clean_out)
		strcpy(clean_out, clean);
	if (slotstore_recover() < 0)
		return SLOTSTORE_CARD;
	int n = ss_list(rom_path);
	if (n < 0)
		return n;
	if (n >= SLOTSTORE_MAX_SLOTS)
		return SLOTSTORE_FULL;
	slot_pointers((u32)n);
	if (saveslot_name_taken(clean, W->slotp, (u32)n, (u32)n + 1))
		return SLOTSTORE_TAKEN;
	char *src = W->d;
	if (copy_from) {
		int i = find_slot((u32)n, copy_from);
		if (i < 0)
			return SLOTSTORE_NO_SLOT;
		int r = slot_path(src, rom_path, W->slots[i].name);
		if (r)
			return r;
	} else if (blank_size < 512 || blank_size % 512 || blank_size > 128 * 1024) {
		return SLOTSTORE_SIZE;
	}
	int r = both_paths(W->a, W->b, rom_path, clean);
	if (r)
		return r;
	if (ss_exists(W->a) != 0)
		return SLOTSTORE_TAKEN;
	if (ss_snprintf(W->c, SS_PATH, "%s.new", W->a) >= SS_PATH)
		return SLOTSTORE_PATH;
	if ((r = ss_mkdirs_parent(W->a)))
		return r;
	// A stray .new of an earlier cut goes; this one starts fresh anyway
	f_unlink(W->c);
	r = copy_from ? ss_copy(src, W->c) : ss_write_blank(W->c, blank_size);
	if (r)
		return r;
	cutpoint("slot: new file written");
	if (f_rename(W->c, W->a) != FR_OK) {
		f_unlink(W->c);
		return SLOTSTORE_CARD;
	}
	cutpoint("slot: new file in place");
	tlog("slot: made %s of %s%s%s", clean, rom_path, copy_from ? ", a copy of " : ", blank",
	     copy_from ? copy_from : "");
	return SLOTSTORE_OK;
}

int slotstore_duplicate(const char *rom_path, const char *slot, const char *name, char *clean_out)
{
	return slotstore_create(rom_path, name, slot, 0, clean_out);
}

/* ---- Trash, delete ---- */

// The trash folder of an entry made by saveslot_trash_path: the path up to and
// including the "<stamp>-<n>" part, into dir
static void trash_folder_of(char *dir, const char *trash_file)
{
	const char *p = trash_file + strlen(SAVESLOT_TRASH_DIR) + 1;
	const char *slash = strchr(p, '/');
	u32 len = (u32)(slash - trash_file);
	memcpy(dir, trash_file, len);
	dir[len] = 0;
}

// Moves the slot's file and history under trash (the "<trash>/<stamp>-<n>"
// folder): the file to <trash>/<rom path>/<slot>.sav, the history to
// <trash>/backups/<rom path>/<slot>
static int trash_steps(int mode, const char *rom, const char *slot, const char *trash)
{
	int saved_mode = test_mode, r;
	test_mode = mode;
	r = both_paths(W->a, W->b, rom, slot);
	test_mode = saved_mode;
	if (r)
		return r;
	if (ss_snprintf(W->c, SS_PATH, "%s%s/%s.sav", trash, rom, slot) >= SS_PATH ||
	    ss_snprintf(W->d, SS_PATH, "%s/backups%s/%s", trash, rom, slot) >= SS_PATH)
		return SLOTSTORE_PATH;
	if (ss_exists(W->a) == 1) {
		if ((r = ss_mkdirs_parent(W->c)) || (r = ss_move(W->a, W->c)))
			return r;
		cutpoint("delete: save in the trash");
	} else if (ss_exists(W->c) != 1 && ss_exists(W->b) != 1) {
		return SLOTSTORE_NO_SLOT;
	}
	if (ss_exists(W->b) == 1) {
		if ((r = ss_move_files(W->b, W->d)))
			return r;
		cutpoint("delete: history in the trash");
	}
	return SLOTSTORE_OK;
}

int slotstore_delete(const char *rom_path, const char *slot)
{
	if (slotstore_recover() < 0)
		return SLOTSTORE_CARD;
	int n = ss_list(rom_path);
	if (n < 0)
		return n;
	int i = find_slot((u32)n, slot);
	if (i < 0)
		return SLOTSTORE_NO_SLOT;
	if (n < 2)
		return SLOTSTORE_LAST;
	char name[SAVESLOT_NAME_BYTES];
	strcpy(name, W->slots[i].name);
	// Another slot becomes current first, if this was
	struct gamerec rec;
	gamesave_record_read(rom_path, &rec);
	if (saveslot_names_equal(rec.slot, name)) {
		int j = find_slot((u32)n, SAVESLOT_MAIN);
		if (j < 0 || j == i) {
			j = 0;
			while (j == i || (u32)j >= SLOTSTORE_MAX_SLOTS)
				j++;
		}
		strcpy(rec.slot, W->slots[j].name);
		if (!gamesave_record_write(rom_path, &rec))
			return SLOTSTORE_CARD;
		cutpoint("delete: another slot is current");
	}
	// The trash folder: today's stamp, the first n not used
	struct saveslot_stamp now;
	slotstore_now(&now);
	char *trash = W->trash;
	for (u32 k = 1;; k++) {
		if (saveslot_trash_path(W->e, SS_PATH, rom_path, name, &now, k) < 0)
			return SLOTSTORE_PATH;
		trash_folder_of(trash, W->e);
		int e = ss_exists(trash);
		if (e < 0)
			return SLOTSTORE_CARD;
		if (!e)
			break;
		if (k > 1000)
			return SLOTSTORE_CARD;
	}
	int r = journal_write('D', rom_path, name, trash);
	if (r)
		return r;
	r = trash_steps(ss_mode(), rom_path, name, trash);
	if (r) {
		tlog_error("slot: delete %s of %s stopped: %d", name, rom_path, r);
		if (r == SLOTSTORE_TAKEN || r == SLOTSTORE_PATH || r == SLOTSTORE_NO_SLOT)
			journal_clear();
		return r;
	}
	journal_clear();
	tlog("slot: deleted %s of %s to %s", name, rom_path, trash);
	return SLOTSTORE_OK;
}

// A history folder with no slot of its name moves to the trash (with no file),
// so a new slot of that name starts with no history it didn't make
static int slotstore_delete_orphan_history(const char *rom_path, const char *slot)
{
	struct saveslot_stamp now;
	slotstore_now(&now);
	char *trash = W->trash;
	for (u32 k = 1; k < 1000; k++) {
		if (saveslot_trash_path(W->e, SS_PATH, rom_path, slot, &now, k) < 0)
			return SLOTSTORE_PATH;
		trash_folder_of(trash, W->e);
		if (ss_exists(trash) == 0)
			break;
	}
	int r = hist_path(W->b, rom_path, slot);
	if (!r && ss_snprintf(W->d, SS_PATH, "%s/backups%s/%s", trash, rom_path, slot) >= SS_PATH)
		r = SLOTSTORE_PATH;
	return r ? r : ss_move_files(W->b, W->d);
}

/* ---- Purge ---- */

// Parses "YYYYMMDD-HHMMSS-n" at the start of name (a trash folder's name)
static int parse_trash_name(const char *name, struct saveslot_stamp *t)
{
	u32 v[6] = {0};
	static const u8 pos[6] = {0, 4, 6, 9, 11, 13}, len[6] = {4, 2, 2, 2, 2, 2};
	for (u32 k = 0; k < 6; k++)
		for (u32 i = 0; i < len[k]; i++) {
			char c = name[pos[k] + i];
			if (c < '0' || c > '9')
				return 0;
			v[k] = v[k] * 10 + (u32)(c - '0');
		}
	if (name[8] != '-' || name[15] != '-' || name[16] < '0' || name[16] > '9')
		return 0;
	t->year = (u16)v[0];
	t->month = (u8)v[1];
	t->day = (u8)v[2];
	t->hour = (u8)v[3];
	t->min = (u8)v[4];
	t->sec = (u8)v[5];
	return 1;
}

// Removes the folder at path and everything in it. The path buffer is the
// caller's (SS_PATH bytes) and is left as it was. 1 if all went, 0 if not.
static int remove_tree(char *path)
{
	u32 root = (u32)strlen(path);
	u32 len = root;
	for (u32 guard = 0; guard < 4096; guard++) {
		FRESULT r = f_opendir(&W->dir, path);
		if (r != FR_OK)
			return r == FR_NO_PATH || r == FR_NO_FILE;
		r = f_readdir(&W->dir, &W->fno);
		f_closedir(&W->dir);
		if (r != FR_OK)
			return 0;
		if (!W->fno.fname[0]) {  // empty: it goes, and we go up
			if (f_unlink(path) != FR_OK)
				return 0;
			if (len == root)
				return 1;
			while (path[len - 1] != '/')
				len--;
			path[--len] = 0;
			continue;
		}
		u32 nlen = (u32)strlen(W->fno.fname);
		if (len + 1 + nlen >= SS_PATH)
			return 0;
		path[len] = '/';
		memcpy(path + len + 1, W->fno.fname, nlen + 1);
		if (W->fno.fattrib & AM_DIR) {
			len += 1 + nlen;  // descend
		} else {
			FRESULT u = f_unlink(path);
			path[len] = 0;
			if (u != FR_OK)
				return 0;
		}
	}
	return 0;
}

int slotstore_trash_purge(const struct saveslot_stamp *now_in)
{
	struct saveslot_stamp now;
	if (now_in)
		now = *now_in;
	else
		slotstore_now(&now);
	if (now.year == 0)
		return 0;
	if (f_opendir(&W->dir, SAVESLOT_TRASH_DIR) != FR_OK)
		return 0;
	// The expired folders' names first: removing while reading would move the entries
	u32 found = 0;
	char (*gone)[SAVESLOT_STAMP_BYTES + 12] = (char (*)[SAVESLOT_STAMP_BYTES + 12]) W->names;
	_Static_assert(sizeof(W->names) >= SLOTSTORE_PURGE_MAX * (SAVESLOT_STAMP_BYTES + 12), "room for the names");
	for (;;) {
		FRESULT r = f_readdir(&W->dir, &W->fno);
		if (r != FR_OK || !W->fno.fname[0])
			break;
		struct saveslot_stamp then;
		if (!(W->fno.fattrib & AM_DIR) || strlen(W->fno.fname) >= SAVESLOT_STAMP_BYTES + 12 ||
		    !parse_trash_name(W->fno.fname, &then) || !saveslot_trash_expired(&then, &now))
			continue;
		strcpy(gone[found++], W->fno.fname);
		if (found == SLOTSTORE_PURGE_MAX)
			break;
	}
	f_closedir(&W->dir);
	u32 removed = 0;
	for (u32 i = 0; i < found; i++) {
		char *path = W->e;
		ss_snprintf(path, SS_PATH, "%s/%s", SAVESLOT_TRASH_DIR, gone[i]);
		int ok = remove_tree(path);
		tlog("slot: purged %s: %s", path, ok ? "done" : "failed");
		removed += ok;
		cutpoint("trash: folder purged");
	}
	return (int)removed;
}

/* ---- History ---- */

int slotstore_push_file(const char *file, const char *hist_dir, u32 keep)
{
	int e = ss_exists(file);
	if (e < 0)
		return SLOTSTORE_CARD;
	if (!e)
		return SLOTSTORE_NO_SLOT;
	u32 count, maxn, newest;
	int r = ss_scan_hist(hist_dir, &count, &maxn, &newest);
	if (r)
		return r;
	char *hp = W->hp, *hn = W->hn;
	if (count) {
		ss_snprintf(hp, SS_PATH, "%s/%s", hist_dir, W->names[newest]);
		int same = ss_same(file, hp);
		if (same < 0)
			return SLOTSTORE_CARD;
		if (same) {
			tlog("slot: %s is the newest version of %s already", file, hist_dir);
			return keep ? ss_prune(hist_dir, &count, keep) : SLOTSTORE_OK;
		}
	}
	if (maxn >= SAVESLOT_COUNTER_MAX) {
		tlog_error("slot: the counter of %s has run out", hist_dir);
		return SLOTSTORE_CARD;
	}
	struct saveslot_stamp now;
	slotstore_now(&now);
	char name[SAVESLOT_BACKUP_NAME_BYTES];
	saveslot_backup_name(name, maxn + 1, &now);
	if (ss_snprintf(hp, SS_PATH, "%s/%s", hist_dir, name) >= SS_PATH - 5)
		return SLOTSTORE_PATH;
	ss_snprintf(hn, SS_PATH, "%s.new", hp);
	if ((r = ss_mkdirs(hist_dir)))
		return r;
	if ((r = ss_copy(file, hn)))
		return r;
	cutpoint("history: version written");
	if (f_rename(hn, hp) != FR_OK) {
		f_unlink(hn);
		return SLOTSTORE_CARD;
	}
	cutpoint("history: version in place");
	tlog("slot: pushed %s to %s", file, hp);
	if (count < SS_NAMES) {
		strcpy(W->names[count], name);
		count++;
	}
	return keep ? ss_prune(hist_dir, &count, keep) : SLOTSTORE_OK;
}

static u32 explicit_keep(void)
{
	u32 k = slotstore_keep();
	return k ? k : 1;
}

int slotstore_history_push(const char *rom_path, const char *slot)
{
	if (slotstore_recover() < 0)
		return SLOTSTORE_CARD;
	int r = both_paths(W->a, W->b, rom_path, slot);
	if (r)
		return r;
	if (ss_exists(W->a) != 1)
		return SLOTSTORE_NO_SLOT;
	return slotstore_push_file(W->a, W->b, explicit_keep());
}

int slotstore_history(const char *rom_path, const char *slot, struct slotstore_version *out, u32 max)
{
	int r = hist_path(W->b, rom_path, slot);
	if (r)
		return r;
	u32 count, maxn, newest;
	if ((r = ss_scan_hist(W->b, &count, &maxn, &newest)))
		return r;
	// Newest first: each version goes in by its n, the ones past max fall off
	u32 given = 0;
	for (u32 i = 0; i < count; i++) {
		u32 n, at = 0;
		struct saveslot_stamp when;
		saveslot_parse_backup_name(W->names[i], &n, &when);
		while (at < given && out[at].n > n)
			at++;
		if (at >= max)
			continue;
		for (u32 k = given < max ? given : max - 1; k > at; k--)
			out[k] = out[k - 1];
		out[at].n = n;
		out[at].when = when;
		out[at].size = W->sizes[i];
		if (given < max)
			given++;
	}
	return (int)count;
}

/* ---- Installing a file as the slot's ---- */

// Makes src the file at dest, whole or not at all: through dest.tmp, and the
// old dest moved to dest.bak and removed, as Save_savefile does it (Save_recover
// finishes or undoes a cut one). SLOTSTORE_OK or SLOTSTORE_CARD
static int ss_install(const char *src, const char *dest)
{
	char *tmp = W->tmpf, *bak = W->bakf;
	if (ss_snprintf(tmp, SS_PATH, "%s.tmp", dest) >= SS_PATH ||
	    ss_snprintf(bak, SS_PATH, "%s.bak", dest) >= SS_PATH)
		return SLOTSTORE_PATH;
	Save_recover(dest);
	int r = ss_copy(src, tmp);
	if (r)
		return r;
	cutpoint("install: tmp verified");
	int had = ss_exists(dest);
	if (had < 0) {
		f_unlink(tmp);
		return SLOTSTORE_CARD;
	}
	// Between the two renames the slot has no .sav and would not be listed:
	// the journal has boot recovery (Save_recover) finish it
	if ((r = journal_write('I', dest, "-", "-"))) {
		f_unlink(tmp);
		return r;
	}
	if (had) {
		if (f_rename(dest, bak) != FR_OK) {
			Save_recover(dest);
			journal_clear();
			return SLOTSTORE_CARD;
		}
		cutpoint("install: old file moved to bak");
	}
	if (f_rename(tmp, dest) != FR_OK) {
		Save_recover(dest);
		journal_clear();
		return SLOTSTORE_CARD;
	}
	cutpoint("install: tmp in place");
	if (had) {
		f_unlink(bak);
		cutpoint("install: bak removed");
	}
	journal_clear();
	return SLOTSTORE_OK;
}

int slotstore_restore(const char *rom_path, const char *slot, u32 n)
{
	if (slotstore_recover() < 0)
		return SLOTSTORE_CARD;
	int r = both_paths(W->a, W->b, rom_path, slot);
	if (r)
		return r;
	if (ss_exists(W->a) != 1)
		return SLOTSTORE_NO_SLOT;
	u32 count, maxn, newest;
	if ((r = ss_scan_hist(W->b, &count, &maxn, &newest)))
		return r;
	int found = -1;
	for (u32 i = 0; i < count; i++) {
		u32 v;
		if (saveslot_parse_backup_name(W->names[i], &v, NULL) && v == n)
			found = (int)i;
	}
	if (found < 0)
		return SLOTSTORE_NO_VERSION;
	if (ss_snprintf(W->c, SS_PATH, "%s/%s", W->b, W->names[found]) >= SS_PATH)
		return SLOTSTORE_PATH;
	// The file it replaces is safe in the history first
	if ((r = slotstore_push_file(W->a, W->b, 0)))
		return r;
	if ((r = ss_install(W->c, W->a)))
		return r;
	// The version is the file now: it leaves the history
	if (f_unlink(W->c) == FR_OK)
		cutpoint("restore: version taken out of the history");
	if ((r = ss_scan_hist(W->b, &count, &maxn, &newest)))
		return r;
	r = ss_prune(W->b, &count, explicit_keep());
	tlog("slot: restored version %lu of %s of %s", (unsigned long)n, slot, rom_path);
	return r;
}

/* ---- Import, export ---- */

int slotstore_import(const char *rom_path, const char *slot, const char *path, u32 want)
{
	if (slotstore_recover() < 0)
		return SLOTSTORE_CARD;
	if (f_stat(path, &W->fno) != FR_OK || (W->fno.fattrib & AM_DIR))
		return SLOTSTORE_NO_FILE;
	if (W->fno.fsize > 0xFFFFFFu || !saveslot_import_size_ok((u32)W->fno.fsize, want))
		return SLOTSTORE_SIZE;
	int r = both_paths(W->a, W->b, rom_path, slot);
	if (r)
		return r;
	int had = ss_exists(W->a);
	if (had < 0)
		return SLOTSTORE_CARD;
	if (!had)
		return SLOTSTORE_NO_SLOT;
	if ((r = slotstore_push_file(W->a, W->b, 0)))
		return r;
	if ((r = ss_install(path, W->a)))
		return r;
	u32 count, maxn, newest;
	if ((r = ss_scan_hist(W->b, &count, &maxn, &newest)))
		return r;
	r = ss_prune(W->b, &count, explicit_keep());
	tlog("slot: imported %s into %s of %s", path, slot, rom_path);
	return r;
}

int slotstore_export(const char *rom_path, const char *slot, const char *path)
{
	int r = slot_path(W->a, rom_path, slot);
	if (r)
		return r;
	int e = ss_exists(W->a);
	if (e < 0)
		return SLOTSTORE_CARD;
	if (!e)
		return SLOTSTORE_NO_SLOT;
	e = ss_exists(path);
	if (e < 0)
		return SLOTSTORE_CARD;
	if (e)
		return SLOTSTORE_TAKEN;
	if (ss_snprintf(W->c, SS_PATH, "%s.new", path) >= SS_PATH)
		return SLOTSTORE_PATH;
	if ((r = ss_copy(W->a, W->c)))
		return r;
	cutpoint("export: copy written");
	if (f_rename(W->c, path) != FR_OK) {
		f_unlink(W->c);
		return SLOTSTORE_CARD;
	}
	cutpoint("export: copy in place");
	return SLOTSTORE_OK;
}

/* ---- Type change ---- */

int slotstore_retire_file(const char *file, const char *hist_dir)
{
	Save_recover(file);
	int e = ss_exists(file);
	if (e < 0)
		return SLOTSTORE_CARD;
	if (!e)
		return SLOTSTORE_OK;
	int r = slotstore_push_file(file, hist_dir, explicit_keep());
	if (r)
		return r;
	if (f_unlink(file) != FR_OK)
		return SLOTSTORE_CARD;
	cutpoint("retire: file left the slot");
	tlog("slot: %s kept in the history, the slot starts blank", file);
	return SLOTSTORE_OK;
}

/* ---- Boot recovery ---- */

// Reads the journal into W->buf[0] (NUL-ended) and splits it into lines; 0 if
// it is none or doesn't hold up, -1 if the card didn't say
static int journal_read(char *lines[4], char *kind, int *mode)
{
	char *t = (char *)W->buf[0];
	FRESULT r = f_open(&W->fa, JOURNAL, FA_READ);
	if (r == FR_NO_FILE || r == FR_NO_PATH)
		return 0;
	if (r != FR_OK)
		return -1;
	UINT got = 0;
	r = f_read(&W->fa, t, SS_CHUNK - 1, &got);
	f_close(&W->fa);
	if (r != FR_OK)
		return -1;
	t[got] = 0;
	// The last line is the CRC of the rest
	u32 n = got;
	if (n < 10 || t[n - 1] != '\n')
		return 0;
	u32 body = n - 9;  // 8 hex digits and '\n'
	char hex[9];
	memcpy(hex, t + body, 8);
	hex[8] = 0;
	char want[9];
	ss_snprintf(want, sizeof(want), "%08lx", (unsigned long)crc32(t, body));
	if (strcmp(hex, want) || body < sizeof(JOURNAL_HEADER) - 1 ||
	    memcmp(t, JOURNAL_HEADER, sizeof(JOURNAL_HEADER) - 1))
		return 0;
	t[body] = 0;
	char *p = t + sizeof(JOURNAL_HEADER) - 1;
	for (u32 i = 0; i < 4; i++) {
		lines[i] = p;
		char *nl = strchr(p, '\n');
		if (!nl)
			return 0;
		*nl = 0;
		p = nl + 1;
	}
	if (*p)
		return 0;
	*kind = lines[0][0];
	*mode = lines[0][1] - '0';
	if ((*kind != 'R' && *kind != 'D' && *kind != 'I') || lines[0][2] ||
	    (*mode != SAVESLOT_CENTRAL && *mode != SAVESLOT_BESIDE_ROM) || lines[1][0] != '/' || !lines[2][0] ||
	    !lines[3][0])
		return 0;
	return 1;
}

int slotstore_recover(void)
{
	char *lines[4], kind;
	int mode;
	int j = journal_read(lines, &kind, &mode);
	if (j < 0)
		return SLOTSTORE_CARD;
	if (j == 0) {
		if (ss_exists(JOURNAL) == 1) {
			// Cut while it was written: nothing had been moved
			tlog("slot: the journal is cut short, dropped");
			journal_clear();
			return 1;
		}
		return 0;
	}
	// The lines live in W->buf[0]; the steps use the other buffers and paths,
	// but copy what they need out of it: they write the journal's neighbours
	char *rom = W->r1, *x = W->r2, *y = W->r3;
	ss_snprintf(rom, SS_PATH, "%s", lines[1]);
	ss_snprintf(x, SS_PATH, "%s", lines[2]);
	ss_snprintf(y, SS_PATH, "%s", lines[3]);
	char clean[SAVESLOT_NAME_BYTES];
	int r;
	if (kind == 'I') {
		if (strlen(rom) > SAV_INFO_NAME_MAX) {
			journal_clear();
			return 1;
		}
		tlog("slot: finishing the install of %s", rom);
		Save_recover(rom);
		journal_clear();
		return 1;
	}
	if (kind == 'R') {
		if (saveslot_clean_name(clean, x) != strlen(x) || strcmp(clean, x) ||
		    saveslot_clean_name(clean, y) != strlen(y) || strcmp(clean, y)) {
			journal_clear();
			return 1;
		}
		tlog("slot: finishing the rename of %s to %s of %s", x, y, rom);
		r = rename_steps(mode, rom, x, y, 1);
	} else {
		if (saveslot_clean_name(clean, x) != strlen(x) || strcmp(clean, x) ||
		    strncmp(y, SAVESLOT_TRASH_DIR "/", sizeof(SAVESLOT_TRASH_DIR))) {
			journal_clear();
			return 1;
		}
		tlog("slot: finishing the delete of %s of %s", x, rom);
		r = trash_steps(mode, rom, x, y);
		if (r == SLOTSTORE_NO_SLOT)
			r = SLOTSTORE_OK;  // nothing left to move
	}
	if (r == SLOTSTORE_CARD) {
		tlog_error("slot: the journal stays, the card said no");
		return SLOTSTORE_CARD;
	}
	journal_clear();
	return 1;
}
