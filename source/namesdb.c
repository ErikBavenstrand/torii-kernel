// See namesdb.h. Hardware-free: tests/host builds it.
#include "namesdb.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "crc32.h"
#include "library.h"

_Static_assert(NAMESDB_CONSOLES == LIB_CONSOLES, "names.db has a table per console");

// --- names.db -----------------------------------------------------------------

u32 namesdb_header_crc(const struct namesdb_header *h)
{
	return crc32(h, offsetof(struct namesdb_header, header_crc));
}

u32 namesdb_file_bytes(const struct namesdb_header *h)
{
	unsigned long long entries = h->old_codes;
	for (u32 c = 0; c < NAMESDB_CONSOLES; c++)
		entries += h->crc_count[c];
	unsigned long long bytes = sizeof(*h) + entries * sizeof(struct namesdb_entry) + h->pool_bytes;
	return bytes > 0xFFFFFFFFu ? 0 : (u32)bytes;
}

int namesdb_header_ok(const struct namesdb_header *h, u32 max)
{
	if (h->magic != NAMESDB_MAGIC || h->version < 1 || h->version > NAMESDB_VERSION ||
	    h->header_bytes != sizeof(*h) || (h->version > 1 && h->old_codes))
		return 0;
	if (h->header_crc != namesdb_header_crc(h) || h->dat_version[NAMESDB_VERSION_LEN - 1])
		return 0;  // and the version ends in NUL
	u32 bytes = namesdb_file_bytes(h);
	return bytes && bytes == h->file_bytes && bytes <= max && h->pool_bytes >= 4 && !(h->pool_bytes & 3);
}

static const struct namesdb_header *hdr(const u8 *db)
{
	return (const struct namesdb_header *)db;
}

static const u8 *pool_of(const u8 *db)
{
	return db + hdr(db)->file_bytes - hdr(db)->pool_bytes;
}

int namesdb_body_ok(const u8 *db)
{
	return pool_of(db)[hdr(db)->pool_bytes - 1] == 0;
}

// The name of the entry with key in the n sorted entries at t, or NULL
static const char *search(const u8 *db, const struct namesdb_entry *t, u32 n, u32 key)
{
	u32 lo = 0, hi = n;
	while (lo < hi) {
		u32 mid = lo + (hi - lo) / 2;
		u32 k = t[mid].key;
		if (k == key) {
			u32 off = t[mid].name;
			return off < hdr(db)->pool_bytes ? (const char *)pool_of(db) + off : NULL;
		}
		if (k < key)
			lo = mid + 1;
		else
			hi = mid;
	}
	return NULL;
}

static const struct namesdb_entry *table(const u8 *db, u32 i)
{
	const struct namesdb_entry *t = (const struct namesdb_entry *)(db + sizeof(struct namesdb_header));
	for (u32 c = 0; c < i; c++)
		t += hdr(db)->crc_count[c];
	return t;
}

const char *namesdb_by_crc(const u8 *db, u32 console, u32 crc)
{
	if (!db || console >= NAMESDB_CONSOLES)
		return NULL;
	return search(db, table(db, console), hdr(db)->crc_count[console], crc);
}

// --- The cache's table ----------------------------------------------------------

static u16 *slots(const struct names_cache *c)
{
	return (u16 *)(c->base + NAMES_CACHE_OFF_SLOTS);
}

static inline __attribute__((always_inline)) int lower(int ch)
{
	return ch >= 'A' && ch <= 'Z' ? ch + 32 : ch;
}

static inline __attribute__((always_inline)) int same_path(const char *a, const char *b)
{
	while (*a && lower((u8)*a) == lower((u8)*b)) {
		a++;
		b++;
	}
	return !*a && !*b;
}

u32 IWRAM_CODE names_path_hash_len(const char *path, u32 *len)
{
	const char *p = path;
	u32 h = 2166136261u;
	for (u32 ch; (ch = (u8)*p) != 0; p++) {
		if (ch - 'A' < 26u)
			ch += 32;
		h = (h ^ ch) * 16777619u;
	}
	*len = (u32)(p - path);
	return h;
}

u32 names_path_hash(const char *path)
{
	u32 len;
	return names_path_hash_len(path, &len);
}

void names_cache_init(struct names_cache *c, u8 *base, names_put_fn put)
{
	c->base = base;
	c->put = put;
	c->count = c->pool = 0;
	c->full = 0;
}

int names_cache_clear_part(struct names_cache *c, u32 at, u32 bytes)
{
	u16 none[64] ALIGN(4);  // in RAM: psram_write takes no source in cart space
	memset(none, 0xFF, sizeof(none));
	u32 end = at + bytes;
	if (end > NAMES_CACHE_SLOTS * 2u)
		end = NAMES_CACHE_SLOTS * 2u;
	for (; at < end; at += sizeof(none)) {
		u32 n = end - at < sizeof(none) ? end - at : sizeof(none);
		c->put(c->base + NAMES_CACHE_OFF_SLOTS + at, none, n);
	}
	return end == NAMES_CACHE_SLOTS * 2u;
}

// A hash's first slot. FNV's low bits depend only on the bytes' low bits,
// so paths that differ in a few digits crowded a few slots: mix first
static u32 first_slot(u32 h)
{
	h ^= h >> 16;
	h *= 0x7FEB352Du;
	h ^= h >> 15;
	return h & (NAMES_CACHE_SLOTS - 1);
}

// The record of path, whose hash is h
static u32 find_hashed(const struct names_cache *c, const char *path, u32 h)
{
	const u16 *s = slots(c);
	for (u32 i = first_slot(h), probes = 0; probes < NAMES_CACHE_SLOTS;
	     i = (i + 1) & (NAMES_CACHE_SLOTS - 1), probes++) {
		u32 n = s[i];
		if (n >= c->count)
			return NAMES_CACHE_NONE;  // empty (0xFFFF), or junk past the records
		const struct names_rec *r = names_cache_rec(c, n);
		if (r->hash == h && same_path(names_cache_path(c, r), path))
			return n;
	}
	return NAMES_CACHE_NONE;
}

u32 names_cache_find(const struct names_cache *c, const char *path)
{
	return find_hashed(c, path, names_path_hash(path));
}

static void write_rec(struct names_cache *c, u32 n, const struct names_rec *r)
{
	c->put((u8 *)names_cache_rec(c, n), r, sizeof(*r));
}

u32 names_cache_put(struct names_cache *c, const char *path, u32 size, u32 date, u32 crc, u32 flags)
{
	u32 len, hash = names_path_hash_len(path, &len);
	if (path[0] != '/' || len > NAMES_PATH_MAX)
		return NAMES_CACHE_NONE;
	flags &= NAMES_CRC | NAMES_DATE;
	struct names_rec r = {hash, size, (flags & NAMES_DATE) ? date : 0, (flags & NAMES_CRC) ? crc : 0, 0};
	u32 n = find_hashed(c, path, r.hash);
	if (n != NAMES_CACHE_NONE) {
		const struct names_rec *old = names_cache_rec(c, n);
		r.path = (old->path & 0xFFFFFFu) | flags << 24;
		write_rec(c, n, &r);
		return n;
	}
	// The pool takes the path padded to halfwords (psram_write's unit)
	u32 bytes = (len + 2) & ~1u;
	if (c->count >= NAMES_CACHE_MAX || c->pool + bytes > NAMES_CACHE_POOL_BYTES) {
		c->full = 1;
		return NAMES_CACHE_NONE;
	}
	static char buf[NAMES_PATH_MAX + 2] EWRAM_BSS ALIGN(4);  // halfword-aligned for put, off the stack
	memcpy(buf, path, len);
	buf[len] = buf[len + 1] = 0;
	c->put(c->base + NAMES_CACHE_OFF_POOL + c->pool, buf, bytes);
	r.path = c->pool | flags << 24;
	c->pool += bytes;
	n = c->count++;
	write_rec(c, n, &r);
	const u16 *s = slots(c);
	u32 i = first_slot(r.hash);
	// The next empty slot: NAMES_CACHE_MAX < NAMES_CACHE_SLOTS keeps one
	for (u32 probes = 0; s[i] < n && probes < NAMES_CACHE_SLOTS; probes++)
		i = (i + 1) & (NAMES_CACHE_SLOTS - 1);
	u16 v = (u16)n;
	c->put((u8 *)&s[i], &v, 2);
	return n;
}

void names_cache_drop(struct names_cache *c, u32 n)
{
	if (n >= c->count)
		return;
	struct names_rec r = *names_cache_rec(c, n);
	r.path |= NAMES_DEAD << 24;
	write_rec(c, n, &r);
}

void names_cache_set_date(struct names_cache *c, u32 n, u32 date)
{
	if (n >= c->count)
		return;
	struct names_rec r = *names_cache_rec(c, n);
	r.date = date;
	r.path |= NAMES_DATE << 24;
	write_rec(c, n, &r);
}

// Hours since 1980 of a FAT date and time (fdate << 16 | ftime), or -1 for
// one that names no moment. The days are Howard Hinnant's days from civil,
// counted from 1 March of year 0.
static s32 fat_hours(u32 date)
{
	static const u8 month_days[12] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	u32 y = 1980 + (date >> 25), m = date >> 21 & 15, d = date >> 16 & 31;
	u32 hour = date >> 11 & 31, minute = date >> 5 & 63, secs = date & 31;
	if (m < 1 || m > 12 || d < 1 || d > month_days[m - 1] || hour > 23 || minute > 59 || secs > 29)
		return -1;
	if (m == 2 && d == 29 && (y % 4 || (y % 100 == 0 && y % 400)))
		return -1;
	if (m <= 2)
		y--;
	u32 era = y / 400, yoe = y - era * 400;
	u32 doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
	u32 days = era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return (s32)((days - 723120u) * 24 + hour);  // 723120: 1 January 1980
}

int names_cache_fits(const struct names_rec *r, u32 size, u32 date)
{
	u32 flags = names_rec_flags(r);
	if ((flags & NAMES_DEAD) || r->size != size)
		return NAMES_STALE;
	if (!(flags & NAMES_DATE))
		return NAMES_FIT_DATED;
	if (r->date == date)
		return NAMES_FIT;
	// A whole number of hours off, minutes and seconds equal: names-update's
	// date, from a PC whose local time isn't the one the card's driver wrote
	if ((r->date ^ date) & 0x7FFu)
		return NAMES_STALE;
	s32 a = fat_hours(r->date), b = fat_hours(date);
	if (a < 0 || b < 0)
		return NAMES_STALE;
	s32 hours = a > b ? a - b : b - a;
	return hours <= NAMES_DATE_SKEW_HOURS ? NAMES_FIT_DATED : NAMES_STALE;
}

// --- crc.txt's lines -----------------------------------------------------------------

// The next tab-separated field of *s, NUL-ended in place; NULL if none
static inline __attribute__((always_inline)) char *field(char **s)
{
	char *start = *s;
	if (!start)
		return NULL;
	char *tab = start;
	while (*tab && *tab != '\t')
		tab++;
	if (*tab) {
		*tab = 0;
		*s = tab + 1;
	} else {
		*s = NULL;
	}
	return start;
}

// Parses up to 8 hex digits (exactly 8 if exact) into *out
static inline __attribute__((always_inline)) int hex(const char *s, u32 *out, int exact)
{
	u32 v = 0, n = 0;
	for (; *s; s++, n++) {
		char ch = *s;
		u32 d = ch >= '0' && ch <= '9'   ? (u32)(ch - '0')
			: ch >= 'a' && ch <= 'f' ? (u32)(ch - 'a' + 10)
			: ch >= 'A' && ch <= 'F' ? (u32)(ch - 'A' + 10)
						 : 16;
		if (d == 16 || n == 8)
			return 0;
		v = v << 4 | d;
	}
	*out = v;
	return n && (!exact || n == 8);
}

static inline __attribute__((always_inline)) int decimal(const char *s, u32 *out)
{
	u32 v = 0;
	if (!*s)
		return 0;
	for (; *s; s++) {
		u32 d = (u32)(*s - '0');
		if (*s < '0' || *s > '9' || v > (0xFFFFFFFFu - d) / 10)
			return 0;
		v = v * 10 + d;
	}
	*out = v;
	return 1;
}

static inline __attribute__((always_inline)) int dash(const char *s)
{
	return s[0] == '-' && !s[1];
}

// 1 if s is a GBA game code: four characters 0-9 or A-Z
static inline __attribute__((always_inline)) int game_code(const char *s)
{
	for (u32 i = 0; i < 4; i++)
		if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'A' && s[i] <= 'Z')))
			return 0;
	return !s[4];
}

int IWRAM_CODE names_parse_line(char *line, struct names_line *out)
{
	if (line[0] == '#' || !line[0])
		return 0;
	char *rest = line;
	char *crc = field(&rest), *size = field(&rest), *date = field(&rest);
	if (rest && rest[0] != '/') {  // v0.4.1's line: the code came second
		const char *code = size;
		if (!code || !(dash(code) || game_code(code)))
			return 0;
		size = date;
		date = field(&rest);
	}
	char *path = rest;  // the rest of the line: a path may hold anything but a line break
	if (!crc || !size || !date || !path || path[0] != '/')
		return 0;
	*out = (struct names_line){0};
	out->path = path;
	if (!dash(crc)) {
		if (!hex(crc, &out->crc, 1))
			return 0;
		out->flags |= NAMES_CRC;
	}
	if (!decimal(size, &out->size))
		return 0;
	if (!dash(date)) {
		if (!hex(date, &out->date, 1))
			return 0;
		out->flags |= NAMES_DATE;
	}
	return 1;
}

u32 names_format_line(const struct names_cache *c, u32 n, char *buf, u32 size)
{
	if (n >= c->count)
		return 0;
	const struct names_rec *r = names_cache_rec(c, n);
	u32 flags = names_rec_flags(r);
	if (flags & NAMES_DEAD)
		return 0;
	char crc[9] = "-", date[9] = "-";
	if (flags & NAMES_CRC)
		snprintf(crc, sizeof(crc), "%08lX", (unsigned long)r->crc);
	if (flags & NAMES_DATE)
		snprintf(date, sizeof(date), "%08lX", (unsigned long)r->date);
	int len = snprintf(buf, size, "%s\t%lu\t%s\t%s\n", crc, (unsigned long)r->size, date, names_cache_path(c, r));
	return len > 0 && (u32)len < size ? (u32)len : 0;
}

// --- The name ----------------------------------------------------------------------

const char *names_db_name(const u8 *db, const struct names_rec *r, u32 console)
{
	if (!db || !r)
		return NULL;
	u32 flags = names_rec_flags(r);
	if (flags & NAMES_DEAD)
		return NULL;
	const char *name = NULL;
	if (flags & NAMES_CRC) {
		name = namesdb_by_crc(db, console, r->crc);
		// The Game Boy tables: a colour game named .gb, or the other way round
		if (!name && (console == LIB_GBC || console == LIB_GB))
			name = namesdb_by_crc(db, console == LIB_GBC ? LIB_GB : LIB_GBC, r->crc);
	}
	return name;
}
