#include "saveorphan.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "cutpoint.h"
#include "ez_define.h"
#include "ezkernel.h"
#include "ff15/ff.h"
#include "slotstore.h"
#include "tlog.h"

#define SO_PATH 512
#define SO_DEPTH 14
#define SO_JBUF 4608
#define SO_NAMES_BYTES 2048
#define JOURNAL "/torii/orphanop.txt"
#define NOR_DIR "/torii/nor/"

// Everything a call needs, in READCACHE_ORPHAN (ez_define.h)
struct so_work {
	DIR dirs[SO_DEPTH];  // the walk's open folders, one a level
	DIR dir;             // the helpers'
	FILINFO fno;
	FIL fa;
	u16 slots[SO_DEPTH];
	u8 game[SO_DEPTH];
	u32 bytes[SO_DEPTH];
	// The four folders of a job: the orphan's saves and history (a, c), the
	// target's (b, d); e and f are the file moves'; g is spare
	char a[SO_PATH], b[SO_PATH], c[SO_PATH], d[SO_PATH], e[SO_PATH], f[SO_PATH], g[SO_PATH];
	char jbuf[SO_JBUF] ALIGN(4);
	char names[SO_NAMES_BYTES];  // the slot names of a plan, and the new ones
	const char *x[ORPHANPLAN_MAX], *y[ORPHANPLAN_MAX];
	struct orphanplan_job job;
};
_Static_assert(sizeof(struct so_work) <= READCACHE_ORPHAN_SIZE, "the orphan manager's work area fits READCACHE_ORPHAN");

#define W ((struct so_work *)(pReadCache + READCACHE_ORPHAN))

// snprintf, for strings built inside the work area: the compiler can't tell
// that the destination and the arguments are different fields of it
static int __attribute__((format(printf, 3, 4))) so_snprintf(char *dst, u32 size, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(dst, size, fmt, ap);
	va_end(ap);
	return n;
}

/* ---- FatFs helpers ---- */

// 1 if path exists, 0 if not, -1 if the card didn't say
static int exists(const char *path)
{
	FRESULT r = f_stat(path, NULL);
	if (r == FR_OK)
		return 1;
	return r == FR_NO_FILE || r == FR_NO_PATH ? 0 : -1;
}

static int has_prefix(const char *s, const char *prefix)
{
	return !strncmp(s, prefix, strlen(prefix));
}

// mkdir -p for the parents of path
static int make_parents(char *path)
{
	for (char *p = path + 1; *p; p++) {
		if (*p != '/')
			continue;
		*p = 0;
		FRESULT r = f_mkdir(path);
		*p = '/';
		if (r != FR_OK && r != FR_EXIST)
			return ORPHAN_CARD;
	}
	return ORPHAN_OK;
}

// Moves src (a file or a folder) to dst, as a rename that a cut may have cut:
// nothing at src is done; if dst is there too, either it is a second name of
// src's chain (dropped), or two different things (ORPHAN_STOPPED, both left)
static int move(const char *src, const char *dst)
{
	int s = exists(src), d = exists(dst);
	if (s < 0 || d < 0)
		return ORPHAN_CARD;
	if (!s)
		return ORPHAN_OK;
	if (d) {
		FRESULT r = f_unlink_alias(src, dst);
		if (r == FR_OK) {
			tlog("orphan: %s was a second name of %s, dropped", src, dst);
			return ORPHAN_OK;
		}
		tlog_error("orphan: %s and %s are both there", src, dst);
		return r == FR_DENIED ? ORPHAN_STOPPED : ORPHAN_CARD;
	}
	FRESULT r = f_rename(src, dst);
	if (r != FR_OK) {
		tlog_error("orphan: moving %s to %s: f_rename gave %d", src, dst, r);
		return ORPHAN_CARD;
	}
	return ORPHAN_OK;
}

// Removes the folders above path that are empty, up to but not including
// floor (the part of path to keep)
static void prune_parents(char *path, const char *floor)
{
	u32 keep = (u32)strlen(floor);
	for (;;) {
		char *slash = strrchr(path, '/');
		if (!slash || (u32)(slash - path) <= keep)
			return;
		*slash = 0;
		if (f_unlink(path) != FR_OK)
			return;
	}
}

// Removes the folder at path and everything in it; path (SO_PATH bytes) is
// left as it was. 1 if all went.
static int remove_tree(char *path)
{
	u32 root = (u32)strlen(path), len = root;
	for (u32 guard = 0; guard < 100000; guard++) {
		FRESULT r = f_opendir(&W->dir, path);
		if (r != FR_OK)
			return r == FR_NO_PATH || r == FR_NO_FILE;
		r = f_readdir(&W->dir, &W->fno);
		f_closedir(&W->dir);
		if (r != FR_OK)
			return 0;
		if (!W->fno.fname[0]) {
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
		if (len + 1 + nlen >= SO_PATH)
			return 0;
		path[len] = '/';
		memcpy(path + len + 1, W->fno.fname, nlen + 1);
		if (W->fno.fattrib & AM_DIR) {
			len += 1 + nlen;
		} else {
			FRESULT u = f_unlink(path);
			path[len] = 0;
			if (u != FR_OK)
				return 0;
		}
	}
	return 0;
}

/* ---- The scan ---- */

struct so_sum {
	u32 files, bytes;
	u32 entries;
	u8 partial;
};

static int is_sav(const char *name)
{
	u32 n = (u32)strlen(name);
	return n > 4 && !strcasecmp(name + n - 4, ".sav");
}

static void add_orphan(struct orphan_scan *s, const char *rom, u32 slots, u32 bytes)
{
	u32 len = (u32)strlen(rom) + 1;
	if (s->count >= ORPHAN_MAX || s->pool_used + len > ORPHAN_POOL) {
		s->more++;
		return;
	}
	memcpy(s->pool + s->pool_used, rom, len);
	s->off[s->count] = s->pool_used;
	s->slots[s->count] = (u16)(slots > 0xFFFF ? 0xFFFF : slots);
	s->bytes[s->count] = bytes;
	s->count++;
	s->pool_used = (u16)(s->pool_used + len);
}

// The folder at path (path, SO_PATH bytes, is the walk's buffer and is left
// as it was) and everything under it. With s (the saves' walk) a folder
// holding .sav files or a game.ini is a game folder, and one whose ROM isn't
// a file is listed as an orphan.
static int walk(char *path, struct so_sum *sum, struct orphan_scan *s, orphan_progress_fn progress, void *ctx,
		u32 phase)
{
	u32 len = (u32)strlen(path), level = 0;
	u32 base = s ? (u32)strlen(SAVESLOT_SAVES_DIR) : 0;
	FRESULT r = f_opendir(&W->dirs[0], path);
	if (r == FR_NO_PATH || r == FR_NO_FILE)
		return ORPHAN_OK;
	if (r != FR_OK)
		return ORPHAN_CARD;
	W->slots[0] = 0;
	W->game[0] = 0;
	W->bytes[0] = 0;
	for (;;) {
		r = f_readdir(&W->dirs[level], &W->fno);
		if (r != FR_OK) {
			sum->partial = 1;
			W->fno.fname[0] = 0;
		}
		if (!W->fno.fname[0]) {
			f_closedir(&W->dirs[level]);
			if (s && level > 0 && W->game[level]) {
				const char *rom = path + base;
				s->games++;
				if (!has_prefix(rom, NOR_DIR)) {
					FRESULT st = f_stat(rom, &W->fno);
					if (st == FR_NO_FILE || st == FR_NO_PATH || (st == FR_OK && (W->fno.fattrib & AM_DIR)))
						add_orphan(s, rom, W->slots[level], W->bytes[level]);
					else if (st != FR_OK)
						sum->partial = 1;
				}
			}
			if (level == 0)
				return ORPHAN_OK;
			while (path[len - 1] != '/')
				len--;
			path[--len] = 0;
			level--;
			continue;
		}
		if ((++sum->entries & 31) == 0 && progress)
			progress(ctx, phase, sum->entries);
		u32 nlen = (u32)strlen(W->fno.fname);
		if (len + 1 + nlen >= SO_PATH) {
			sum->partial = 1;
			continue;
		}
		if (W->fno.fattrib & AM_DIR) {
			if (level + 1 >= SO_DEPTH) {
				sum->partial = 1;
				continue;
			}
			path[len] = '/';
			memcpy(path + len + 1, W->fno.fname, nlen + 1);
			if (f_opendir(&W->dirs[level + 1], path) != FR_OK) {
				path[len] = 0;
				sum->partial = 1;
				continue;
			}
			len += 1 + nlen;
			level++;
			W->slots[level] = 0;
			W->game[level] = 0;
			W->bytes[level] = 0;
			continue;
		}
		u32 size = (u32)W->fno.fsize;
		sum->files++;
		sum->bytes += size;
		if (s) {
			W->bytes[level] += size;
			if (is_sav(W->fno.fname)) {
				W->slots[level]++;
				W->game[level] = 1;
			} else if (!strcasecmp(W->fno.fname, "game.ini")) {
				W->game[level] = 1;
			}
		}
	}
}

int saveorphan_scan(struct orphan_scan *s, orphan_progress_fn progress, void *ctx)
{
	memset(s, 0, sizeof(*s));
	struct so_sum sum;
	int worst = ORPHAN_OK;
	char *path = W->a;

	memset(&sum, 0, sizeof(sum));
	so_snprintf(path, SO_PATH, "%s", SAVESLOT_SAVES_DIR);
	int r = walk(path, &sum, s, progress, ctx, 0);
	if (r)
		worst = r;
	s->saves_bytes = sum.bytes;
	s->saves_files = sum.files;
	u8 partial = sum.partial;

	memset(&sum, 0, sizeof(sum));
	so_snprintf(path, SO_PATH, "%s", SAVESLOT_BACKUPS_DIR);
	r = walk(path, &sum, NULL, progress, ctx, 1);
	if (r)
		worst = r;
	s->backups_bytes = sum.bytes;
	s->backups_files = sum.files;
	partial |= sum.partial;

	memset(&sum, 0, sizeof(sum));
	so_snprintf(path, SO_PATH, "%s", SAVESLOT_TRASH_DIR);
	r = walk(path, &sum, NULL, progress, ctx, 2);
	if (r)
		worst = r;
	s->trash_bytes = sum.bytes;
	s->trash_files = sum.files;
	partial |= sum.partial;

	// The orphans' history
	for (u32 i = 0; i < s->count; i++) {
		memset(&sum, 0, sizeof(sum));
		if (so_snprintf(path, SO_PATH, "%s%s", SAVESLOT_BACKUPS_DIR, saveorphan_path(s, i)) >= SO_PATH)
			continue;
		if (walk(path, &sum, NULL, progress, ctx, 3) == ORPHAN_OK)
			s->bytes[i] += sum.bytes;
		partial |= sum.partial;
	}
	s->partial = partial;
	tlog("orphan: scan: %lu games, %lu orphans (%lu more), saves %lu, history %lu, trash %lu bytes%s",
	     (unsigned long)s->games, (unsigned long)s->count, (unsigned long)s->more, (unsigned long)s->saves_bytes,
	     (unsigned long)s->backups_bytes, (unsigned long)s->trash_bytes, s->partial ? " (partial)" : "");
	return worst;
}

/* ---- The journal ---- */

static int journal_write(u32 len)
{
	FRESULT r = f_open(&W->fa, JOURNAL, FA_WRITE | FA_CREATE_ALWAYS);
	if (r != FR_OK) {
		tlog_error("orphan: can't write the journal: f_open gave %d", r);
		return ORPHAN_CARD;
	}
	UINT done = 0;
	FRESULT werr = f_write(&W->fa, W->jbuf, len, &done);
	FRESULT serr = werr == FR_OK ? f_sync(&W->fa) : FR_OK;
	FRESULT cerr = f_close(&W->fa);
	if (werr != FR_OK || done != len || serr != FR_OK || cerr != FR_OK) {
		tlog_error("orphan: writing the journal: f_write gave %d, f_sync %d, f_close %d", werr, serr, cerr);
		f_unlink(JOURNAL);
		return ORPHAN_CARD;
	}
	cutpoint("orphan: journal written");
	return ORPHAN_OK;
}

static void journal_clear(void)
{
	FRESULT r = f_unlink(JOURNAL);
	tlog("orphan: journal cleared, f_unlink gave %d", r);
	cutpoint("orphan: journal cleared");
}

/* ---- Steps ---- */

// "/torii/saves" + rom, into out
static int under(char *out, const char *dir, const char *rom)
{
	return so_snprintf(out, SO_PATH, "%s%s", dir, rom) >= SO_PATH ? ORPHAN_PATH : ORPHAN_OK;
}

// Moves every file of slot x in the folder src (x.sav and its leftovers) to
// dst, as y's. Folders must exist.
static int move_slot_files(const char *src_dir, const char *dst_dir, const char *x, const char *y)
{
	for (u32 guard = 0; guard < 64; guard++) {
		if (f_opendir(&W->dir, src_dir) != FR_OK)
			return exists(src_dir) == 0 ? ORPHAN_OK : ORPHAN_CARD;
		int found = 0;
		for (;;) {
			FRESULT r = f_readdir(&W->dir, &W->fno);
			if (r != FR_OK || !W->fno.fname[0])
				break;
			const char *rest;
			u32 n = orphanplan_slot_of(W->fno.fname, &rest);
			if (!n || (W->fno.fattrib & AM_DIR) || n != strlen(x) || strncasecmp(W->fno.fname, x, n))
				continue;
			if (so_snprintf(W->e, SO_PATH, "%s/%s", src_dir, W->fno.fname) >= SO_PATH ||
			    so_snprintf(W->f, SO_PATH, "%s/%s%s", dst_dir, y, rest) >= SO_PATH)
				break;
			found = 1;
			break;
		}
		f_closedir(&W->dir);
		if (!found)
			return ORPHAN_OK;
		int r = move(W->e, W->f);
		if (r)
			return r;
		cutpoint("orphan: save moved");
	}
	return ORPHAN_CARD;
}

static int folders_of(const char *from, const char *to)
{
	if (under(W->a, SAVESLOT_SAVES_DIR, from) || under(W->c, SAVESLOT_BACKUPS_DIR, from))
		return ORPHAN_PATH;
	if (to && (under(W->b, SAVESLOT_SAVES_DIR, to) || under(W->d, SAVESLOT_BACKUPS_DIR, to)))
		return ORPHAN_PATH;
	return ORPHAN_OK;
}

// Moves the folder src (SO_PATH bytes, as is dst) and everything in it to dst,
// a file at a time, then removes src's emptied folders. A folder is never
// renamed into another parent: FatFs writes its ".." entry after the move, and a
// cut between the two leaves a folder whose ".." is wrong. The buffers are
// left as they were. Finishes a cut move too: what is there already is skipped.
static int move_tree(char *src, char *dst)
{
	u32 root = (u32)strlen(src), sl = root, dl = (u32)strlen(dst);
	int r = make_parents(dst);
	if (r)
		return r;
	FRESULT mk = f_mkdir(dst);
	if (mk != FR_OK && mk != FR_EXIST)
		return ORPHAN_CARD;
	for (u32 guard = 0; guard < 100000; guard++) {
		FRESULT fr = f_opendir(&W->dir, src);
		if (fr == FR_NO_PATH || fr == FR_NO_FILE)
			return ORPHAN_OK;
		if (fr != FR_OK)
			return ORPHAN_CARD;
		fr = f_readdir(&W->dir, &W->fno);
		f_closedir(&W->dir);
		if (fr != FR_OK)
			return ORPHAN_CARD;
		if (!W->fno.fname[0]) {  // empty: it goes, and we go up
			if (f_unlink(src) != FR_OK)
				return ORPHAN_CARD;
			cutpoint("orphan: folder emptied");
			if (sl == root)
				return ORPHAN_OK;
			while (src[sl - 1] != '/')
				sl--;
			src[--sl] = 0;
			while (dst[dl - 1] != '/')
				dl--;
			dst[--dl] = 0;
			continue;
		}
		u32 nlen = (u32)strlen(W->fno.fname);
		if (sl + 1 + nlen >= SO_PATH || dl + 1 + nlen >= SO_PATH)
			return ORPHAN_PATH;
		int dir = W->fno.fattrib & AM_DIR;
		src[sl] = '/';
		memcpy(src + sl + 1, W->fno.fname, nlen + 1);
		dst[dl] = '/';
		memcpy(dst + dl + 1, W->fno.fname, nlen + 1);
		if (dir) {
			mk = f_mkdir(dst);
			if (mk != FR_OK && mk != FR_EXIST) {
				src[sl] = 0;
				dst[dl] = 0;
				return ORPHAN_CARD;
			}
			sl += 1 + nlen;
			dl += 1 + nlen;
			continue;
		}
		r = move(src, dst);
		src[sl] = 0;
		dst[dl] = 0;
		if (r)
			return r;
		cutpoint("orphan: file moved");
	}
	return ORPHAN_CARD;
}

// Removes the emptied folder at path (if it is empty) and the empty folders
// above it; path is clobbered
static void tidy(char *path, const char *floor)
{
	if (f_unlink(path) != FR_OK && exists(path) == 1)
		tlog("orphan: %s keeps some files, left where it is", path);
	prune_parents(path, floor);
}

// The relink's steps for the job: W->a to d are its folders. Every step is a
// move that does nothing once it is done, so this finishes a cut one too.
static int relink_steps(const struct orphanplan_job *job)
{
	int r = folders_of(job->from, job->to);
	if (r)
		return r;
	if (exists(W->a) == 1) {
		so_snprintf(W->g, SO_PATH, "%s/x", W->b);
		if ((r = make_parents(W->g)))
			return r;
	}
	for (u32 i = 0; i < job->n; i++) {
		if ((r = move_slot_files(W->a, W->b, job->x[i], job->y[i])))
			return r;
		if (so_snprintf(W->e, SO_PATH, "%s/%s", W->c, job->x[i]) >= SO_PATH ||
		    so_snprintf(W->f, SO_PATH, "%s/%s", W->d, job->y[i]) >= SO_PATH)
			return ORPHAN_PATH;
		if (exists(W->e) == 1) {
			if ((r = move_tree(W->e, W->f)))
				return r;
			cutpoint("orphan: history moved");
		}
	}
	// The record: the target keeps its own
	if (so_snprintf(W->e, SO_PATH, "%s/game.ini", W->a) >= SO_PATH || so_snprintf(W->f, SO_PATH, "%s/game.ini", W->b) >= SO_PATH)
		return ORPHAN_PATH;
	if (exists(W->e) == 1) {
		int t = exists(W->f);
		if (t < 0)
			return ORPHAN_CARD;
		if (t == 0) {
			if ((r = move(W->e, W->f)))
				return r;
		} else if (f_unlink_alias(W->e, W->f) != FR_OK && f_unlink(W->e) != FR_OK) {
			// (A cut rename leaves two names on one chain: the first call drops the old
			// name alone. Otherwise the target has a record of its own, and the orphan's goes.)
			return ORPHAN_CARD;
		}
		cutpoint("orphan: record moved");
	}
	tidy(W->a, SAVESLOT_SAVES_DIR);
	tidy(W->c, SAVESLOT_BACKUPS_DIR);
	cutpoint("orphan: folders tidied");
	return ORPHAN_OK;
}

// The delete's steps: the two folders go to the trash folder job->to
static int trash_steps(const struct orphanplan_job *job)
{
	int r = folders_of(job->from, NULL);
	if (r)
		return r;
	if (so_snprintf(W->e, SO_PATH, "%s%s", job->to, job->from) >= SO_PATH ||
	    so_snprintf(W->f, SO_PATH, "%s/backups%s", job->to, job->from) >= SO_PATH)
		return ORPHAN_PATH;
	if (exists(W->a) == 1) {
		if ((r = move_tree(W->a, W->e)))
			return r;
		cutpoint("orphan: saves in the trash");
	}
	if (exists(W->c) == 1) {
		if ((r = move_tree(W->c, W->f)))
			return r;
		cutpoint("orphan: history in the trash");
	}
	tidy(W->a, SAVESLOT_SAVES_DIR);
	tidy(W->c, SAVESLOT_BACKUPS_DIR);
	cutpoint("orphan: folders tidied");
	return ORPHAN_OK;
}

/* ---- Relink ---- */

// Adds name (n bytes) to the plan's names if no name of it ignoring case is
// there; 0 if the pool or the list is full
static int add_name(u32 *count, u32 *used, const char *name, u32 n)
{
	for (u32 i = 0; i < *count; i++)
		if (strlen(W->x[i]) == n && !strncasecmp(W->x[i], name, n))
			return 1;
	if (*count >= ORPHANPLAN_MAX || *used + n + 1 > SO_NAMES_BYTES)
		return 0;
	char *p = W->names + *used;
	memcpy(p, name, n);
	p[n] = 0;
	W->x[(*count)++] = p;
	*used += n + 1;
	return 1;
}

// The slots of the orphan: the names of its save files and of its history
// folders. ORPHAN_PATH if there are too many.
static int collect_names(u32 *count, u32 *used)
{
	*count = *used = 0;
	if (f_opendir(&W->dir, W->a) != FR_OK)
		return ORPHAN_CARD;
	int r = ORPHAN_OK;
	for (;;) {
		FRESULT fr = f_readdir(&W->dir, &W->fno);
		if (fr != FR_OK) {
			r = ORPHAN_CARD;
			break;
		}
		if (!W->fno.fname[0])
			break;
		u32 n = (W->fno.fattrib & AM_DIR) ? 0 : orphanplan_slot_of(W->fno.fname, NULL);
		if (n && !add_name(count, used, W->fno.fname, n)) {
			r = ORPHAN_PATH;
			break;
		}
	}
	f_closedir(&W->dir);
	if (r)
		return r;
	if (exists(W->c) == 1) {
		if (f_opendir(&W->dir, W->c) != FR_OK)
			return ORPHAN_CARD;
		for (;;) {
			FRESULT fr = f_readdir(&W->dir, &W->fno);
			if (fr != FR_OK) {
				r = ORPHAN_CARD;
				break;
			}
			if (!W->fno.fname[0])
				break;
			if ((W->fno.fattrib & AM_DIR) && !add_name(count, used, W->fno.fname, (u32)strlen(W->fno.fname))) {
				r = ORPHAN_PATH;
				break;
			}
		}
		f_closedir(&W->dir);
	}
	return r;
}

struct taken_ctx {
	u32 assigned;  // y[0..assigned) are taken
	int error;
};

// A slot name the target has: a save file or a history folder, or one an earlier slot takes
static int taken(void *vctx, const char *name)
{
	struct taken_ctx *c = vctx;
	for (u32 i = 0; i < c->assigned; i++)
		if (saveslot_names_equal(W->y[i], name))
			return 1;
	if (so_snprintf(W->e, SO_PATH, "%s/%s.sav", W->b, name) >= SO_PATH ||
	    so_snprintf(W->f, SO_PATH, "%s/%s", W->d, name) >= SO_PATH) {
		c->error = ORPHAN_PATH;
		return 1;
	}
	int a = exists(W->e), b = exists(W->f);
	if (a < 0 || b < 0) {
		c->error = ORPHAN_CARD;
		return 1;
	}
	return a || b;
}

int saveorphan_relink(const char *from, const char *to, u32 *renamed)
{
	if (renamed)
		*renamed = 0;
	if (slotstore_recover() < 0 || saveorphan_recover() < 0)
		return ORPHAN_CARD;
	if (!from || !to || from[0] != '/' || to[0] != '/' || strlen(from) > SAVESLOT_ROM_PATH_MAX ||
	    strlen(to) > SAVESLOT_ROM_PATH_MAX || has_prefix(to, "/torii/") || has_prefix(from, "/torii/"))
		return ORPHAN_PATH;
	if (!strcasecmp(from, to))
		return ORPHAN_SAME;
	int r = folders_of(from, to);
	if (r)
		return r;
	if (saveslot_game_dir(W->g, SO_PATH, to) < 0)  // a bad path: ".." or an empty part
		return ORPHAN_PATH;
	FRESULT st = f_stat(to, &W->fno);
	if (st == FR_NO_FILE || st == FR_NO_PATH || (st == FR_OK && (W->fno.fattrib & AM_DIR)))
		return ORPHAN_NO_TARGET;
	if (st != FR_OK)
		return ORPHAN_CARD;
	int e = exists(W->a);
	if (e < 0)
		return ORPHAN_CARD;
	if (!e)
		return ORPHAN_NO_SOURCE;
	u32 n, used;
	if ((r = collect_names(&n, &used)))
		return r;
	struct taken_ctx ctx = {0, 0};
	u32 changed = 0;
	for (u32 i = 0; i < n; i++) {
		char picked[SAVESLOT_NAME_BYTES];
		if (!orphanplan_pick_name(picked, W->x[i], taken, &ctx))
			return ctx.error ? ctx.error : ORPHAN_PATH;
		if (ctx.error)
			return ctx.error;
		if (strcmp(picked, W->x[i])) {
			u32 len = (u32)strlen(picked);
			if (used + len + 1 > SO_NAMES_BYTES)
				return ORPHAN_PATH;
			char *p = W->names + used;
			memcpy(p, picked, len + 1);
			used += len + 1;
			W->y[i] = p;
			changed++;
		} else {
			W->y[i] = W->x[i];
		}
		ctx.assigned = i + 1;
		// The launch must be able to take the save and its history
		char path[SAV_INFO_NAME_MAX + 64];
		int sl = saveslot_save_path(path, sizeof(path), SAVESLOT_CENTRAL, to, W->y[i]);
		int hl = saveslot_backup_dir(path, sizeof(path), to, W->y[i]);
		if (sl < 0 || sl > SAV_INFO_NAME_MAX || hl < 0 || hl > SAV_INFO_HIST_MAX) {
			tlog("orphan: %s of %s would make a path too long at %s", W->y[i], from, to);
			return ORPHAN_PATH;
		}
	}
	u32 len = orphanplan_write(W->jbuf, SO_JBUF, ORPHANPLAN_RELINK, from, to, W->x, W->y, n);
	if (!len) {
		tlog("orphan: the plan for %s does not fit its journal", from);
		return ORPHAN_PATH;
	}
	if ((r = journal_write(len)))
		return r;
	W->jbuf[len] = 0;
	r = orphanplan_read(W->jbuf, len, &W->job) ? relink_steps(&W->job) : ORPHAN_CARD;
	if (r == ORPHAN_CARD) {
		tlog_error("orphan: relink of %s stopped, the journal stays", from);
		return r;
	}
	journal_clear();
	if (r) {
		tlog_error("orphan: relink of %s to %s stopped: %d", from, to, r);
		return r;
	}
	tlog("orphan: relinked %s to %s, %lu slots, %lu renamed", from, to, (unsigned long)n, (unsigned long)changed);
	if (renamed)
		*renamed = changed;
	return ORPHAN_OK;
}

/* ---- Delete, empty the trash ---- */

int saveorphan_trash(const char *from)
{
	if (slotstore_recover() < 0 || saveorphan_recover() < 0)
		return ORPHAN_CARD;
	if (!from || from[0] != '/' || strlen(from) > ORPHAN_TRASH_ROM_MAX || has_prefix(from, "/torii/"))
		return ORPHAN_PATH;
	int r = folders_of(from, NULL);
	if (r)
		return r;
	int e = exists(W->a);
	if (e < 0)
		return ORPHAN_CARD;
	if (!e)
		return ORPHAN_NO_SOURCE;
	// The trash folder: today's stamp, the first n not used
	struct saveslot_stamp now;
	slotstore_now(&now);
	char *trash = W->b;
	for (u32 k = 1;; k++) {
		if (saveslot_trash_path(W->g, SO_PATH, from, "x", &now, k) < 0)
			return ORPHAN_PATH;
		const char *slash = strchr(W->g + sizeof(SAVESLOT_TRASH_DIR), '/');
		u32 tl = (u32)(slash - W->g);
		memcpy(trash, W->g, tl);
		trash[tl] = 0;
		int t = exists(trash);
		if (t < 0)
			return ORPHAN_CARD;
		if (!t)
			break;
		if (k > 1000)
			return ORPHAN_CARD;
	}
	u32 len = orphanplan_write(W->jbuf, SO_JBUF, ORPHANPLAN_TRASH, from, trash, NULL, NULL, 0);
	if (!len)
		return ORPHAN_PATH;
	if ((r = journal_write(len)))
		return r;
	W->jbuf[len] = 0;
	r = orphanplan_read(W->jbuf, len, &W->job) ? trash_steps(&W->job) : ORPHAN_CARD;
	if (r == ORPHAN_CARD) {
		tlog_error("orphan: delete of %s stopped, the journal stays", from);
		return r;
	}
	journal_clear();
	if (r)
		return r;
	tlog("orphan: deleted %s to %s", from, W->job.to);
	return ORPHAN_OK;
}

int saveorphan_trash_empty(orphan_progress_fn progress, void *ctx, u32 *removed)
{
	u32 gone = 0;
	int result = ORPHAN_OK;
	for (u32 guard = 0; guard < 100000; guard++) {
		FRESULT r = f_opendir(&W->dir, SAVESLOT_TRASH_DIR);
		if (r == FR_NO_PATH || r == FR_NO_FILE)
			break;
		if (r != FR_OK) {
			result = ORPHAN_CARD;
			break;
		}
		r = f_readdir(&W->dir, &W->fno);
		f_closedir(&W->dir);
		if (r != FR_OK) {
			result = ORPHAN_CARD;
			break;
		}
		if (!W->fno.fname[0])
			break;
		if (so_snprintf(W->a, SO_PATH, "%s/%s", SAVESLOT_TRASH_DIR, W->fno.fname) >= SO_PATH) {
			result = ORPHAN_PATH;
			break;
		}
		int dir = W->fno.fattrib & AM_DIR;
		int ok = dir ? remove_tree(W->a) : f_unlink(W->a) == FR_OK;
		tlog("orphan: trash %s removed: %s", W->a, ok ? "done" : "failed");
		if (!ok) {
			result = ORPHAN_CARD;
			break;
		}
		gone++;
		cutpoint("orphan: trash folder removed");
		if (progress)
			progress(ctx, 0, gone);
	}
	if (removed)
		*removed = gone;
	return result;
}

/* ---- Boot recovery ---- */

int saveorphan_recover(void)
{
	FRESULT r = f_open(&W->fa, JOURNAL, FA_READ);
	if (r == FR_NO_FILE || r == FR_NO_PATH)
		return 0;
	if (r != FR_OK)
		return ORPHAN_CARD;
	UINT got = 0;
	FRESULT rr = f_read(&W->fa, W->jbuf, SO_JBUF - 1, &got);
	u32 size = (u32)f_size(&W->fa);
	f_close(&W->fa);
	if (rr != FR_OK)
		return ORPHAN_CARD;
	if (size >= SO_JBUF || !orphanplan_read(W->jbuf, got, &W->job) || has_prefix(W->job.from, "/torii/")) {
		// Cut while it was written (the CRC says so): nothing had been moved
		tlog("orphan: the journal is cut short, dropped");
		journal_clear();
		return 1;
	}
	int res;
	if (W->job.kind == ORPHANPLAN_RELINK) {
		tlog("orphan: finishing the relink of %s to %s", W->job.from, W->job.to);
		res = relink_steps(&W->job);
	} else if (has_prefix(W->job.to, SAVESLOT_TRASH_DIR "/")) {
		tlog("orphan: finishing the delete of %s", W->job.from);
		res = trash_steps(&W->job);
	} else {
		res = ORPHAN_PATH;
	}
	if (res == ORPHAN_CARD) {
		tlog_error("orphan: the journal stays, the card said no");
		return ORPHAN_CARD;
	}
	journal_clear();
	return 1;
}
