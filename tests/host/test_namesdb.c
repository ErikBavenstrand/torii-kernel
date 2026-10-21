// The CRC name database and cache (source/namesdb.c), and the CRC32 they
// rest on (source/crc32.c).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../source/crc32.h"
#include "../../source/library.h"
#include "../../source/namesdb.h"
#include "../kernel/torii_test.h"
#include "tests.h"

// --- CRC32 ----------------------------------------------------------------------

static u32 crc_bitwise(const u8 *p, u32 n)
{
	u32 crc = 0xFFFFFFFF;
	while (n--) {
		crc ^= *p++;
		for (int k = 0; k < 8; k++)
			crc = crc & 1 ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
	}
	return ~crc;
}

void test_crc32_words(void)
{
	CHECK_EQ(crc32("123456789", 9), 0xCBF43926);
	CHECK_EQ(crc32("", 0), 0);
	static u8 data[300];
	for (u32 i = 0; i < sizeof(data); i++)
		data[i] = (u8)(i * 131 + (i >> 3) * 7);
	// Every start alignment and length, whole and in two pieces
	for (u32 start = 0; start < 8; start++)
		for (u32 len = 0; len + start <= 120; len++) {
			u32 want = crc_bitwise(data + start, len);
			CHECK_EQ(crc32(data + start, len), want);
			u32 cut = len / 3;
			CHECK_EQ(crc32_update(crc32(data + start, cut), data + start + cut, len - cut), want);
		}
	CHECK_EQ(crc32(data, sizeof(data)), crc_bitwise(data, sizeof(data)));
}

// --- names.db --------------------------------------------------------------------

struct entry {
	u32 key;
	const char *name;
};

static int by_key(const void *a, const void *b)
{
	u32 x = ((const struct entry *)a)->key, y = ((const struct entry *)b)->key;
	return x < y ? -1 : x > y;
}

// A names.db image of the tables (sorted here) into out; returns its size
static u32 build_db(u8 *out, u32 cap, struct entry *crcs[NAMESDB_CONSOLES], const u32 counts[NAMESDB_CONSOLES],
		    struct entry *codes, u32 code_count, const char *version)
{
	struct namesdb_header h;
	memset(&h, 0, sizeof(h));
	h.magic = NAMESDB_MAGIC;
	h.version = code_count ? 1 : NAMESDB_VERSION;  // only v0.4.1's version 1 had codes
	h.header_bytes = sizeof(h);
	snprintf(h.dat_version, sizeof(h.dat_version), "%s", version);
	u32 entries = code_count;
	for (u32 c = 0; c < NAMESDB_CONSOLES; c++) {
		h.crc_count[c] = counts[c];
		entries += counts[c];
	}
	h.old_codes = code_count;
	u32 at = sizeof(h), pool_at = at + entries * 8, pool = 0;
	memset(out, 0, cap);
	for (u32 t = 0; t <= NAMESDB_CONSOLES; t++) {
		struct entry *e = t < NAMESDB_CONSOLES ? crcs[t] : codes;
		u32 n = t < NAMESDB_CONSOLES ? counts[t] : code_count;
		if (n)
			qsort(e, n, sizeof(*e), by_key);
		for (u32 i = 0; i < n; i++) {
			struct namesdb_entry ne = {e[i].key, pool};
			memcpy(out + at, &ne, sizeof(ne));
			at += sizeof(ne);
			u32 len = (u32)strlen(e[i].name) + 1;
			memcpy(out + pool_at + pool, e[i].name, len);
			pool += len;
		}
	}
	h.pool_bytes = (pool + 3) & ~3u;
	h.file_bytes = pool_at + h.pool_bytes;
	h.body_crc = crc32(out + sizeof(h), h.file_bytes - sizeof(h));
	h.header_crc = namesdb_header_crc(&h);
	memcpy(out, &h, sizeof(h));
	CHECK(h.file_bytes <= cap);
	return h.file_bytes;
}

static u8 db[4096];

static struct entry gba[] = {
	{0x8C5D6F2Bu, "Advance Wars (USA)"},
	{0x00000001u, "Low (World)"},
	{0xFFFFFFFFu, "High (World)"},
	{0x5A0E6E6Au, "Legend of Zelda, The - The Minish Cap (USA)"},
};
static struct entry gbc[] = {{0x11111111u, "Colour Game (USA)"}};
static struct entry gb[] = {{0x22222222u, "Mono Game (Japan)"}};
static struct entry nes[] = {{0x33333333u, "Famicom Game (Japan)"}, {0x44444444u, "Headered (USA)"}};
// A v0.4.1 names.db's code table (version 1), which the kernel skips (C4)
static struct entry codes[] = {
	{0x45575841u, "Advance Wars (USA)"},  // "AXWE"
	{0x45525A42u, "Legend of Zelda, The - The Minish Cap (USA)"},  // "BZRE"
};

static u32 make_db(void)
{
	struct entry *tables[NAMESDB_CONSOLES] = {gba, gbc, gb, nes};
	const u32 counts[NAMESDB_CONSOLES] = {4, 1, 1, 2};
	return build_db(db, sizeof(db), tables, counts, NULL, 0, "2026.10.07");
}

// The same as v0.4.1 wrote it: version 1, with a code table
static u32 make_db_v1(void)
{
	struct entry *tables[NAMESDB_CONSOLES] = {gba, gbc, gb, nes};
	const u32 counts[NAMESDB_CONSOLES] = {4, 1, 1, 2};
	return build_db(db, sizeof(db), tables, counts, codes, 2, "2026.10.07");
}

void test_namesdb_header(void)
{
	u32 bytes = make_db();
	struct namesdb_header h;
	memcpy(&h, db, sizeof(h));
	CHECK(namesdb_header_ok(&h, bytes));
	CHECK(namesdb_body_ok(db));
	CHECK(!namesdb_header_ok(&h, bytes - 1));  // larger than the room
	struct namesdb_header bad = h;
	bad.magic ^= 1;
	CHECK(!namesdb_header_ok(&bad, 1u << 20));
	bad = h;
	bad.version = NAMESDB_VERSION + 1;
	bad.header_crc = namesdb_header_crc(&bad);
	CHECK(!namesdb_header_ok(&bad, 1u << 20));
	bad = h;
	bad.version = 0;
	bad.header_crc = namesdb_header_crc(&bad);
	CHECK(!namesdb_header_ok(&bad, 1u << 20));
	bad = h;
	bad.old_codes = 1;  // only version 1 has codes
	bad.file_bytes += 8;
	bad.header_crc = namesdb_header_crc(&bad);
	CHECK(!namesdb_header_ok(&bad, 1u << 20));
	bad = h;
	bad.crc_count[0]++;  // the header's CRC catches it
	CHECK(!namesdb_header_ok(&bad, 1u << 20));
	bad.header_crc = namesdb_header_crc(&bad);  // and the size, once the CRC agrees
	CHECK(!namesdb_header_ok(&bad, 1u << 20));
	bad = h;
	bad.crc_count[3] = 0x20000000u;  // overflows 32 bits
	bad.header_crc = namesdb_header_crc(&bad);
	CHECK(!namesdb_header_ok(&bad, 0xFFFFFFFFu));
	bad = h;
	bad.pool_bytes += 2;
	bad.file_bytes += 2;
	bad.header_crc = namesdb_header_crc(&bad);
	CHECK(!namesdb_header_ok(&bad, 1u << 20));  // a pool not padded to 4
	bad = h;
	memset(bad.dat_version, '9', sizeof(bad.dat_version));  // no NUL to end it
	bad.header_crc = namesdb_header_crc(&bad);
	CHECK(!namesdb_header_ok(&bad, 1u << 20));
	// A pool that doesn't end in NUL
	db[bytes - 1] = 'x';
	CHECK(!namesdb_body_ok(db));
	db[bytes - 1] = 0;
	// v0.4.1's version 1 still reads, its code table skipped
	bytes = make_db_v1();
	memcpy(&h, db, sizeof(h));
	CHECK_EQ(h.version, 1);
	CHECK(namesdb_header_ok(&h, bytes));
	CHECK(namesdb_body_ok(db));
	CHECK(strcmp(namesdb_by_crc(db, LIB_NES, 0x44444444u), "Headered (USA)") == 0);
	CHECK(namesdb_by_crc(db, LIB_GBA, 0x45575841u) == NULL);  // a code isn't a CRC
}

void test_namesdb_lookup(void)
{
	make_db();
	CHECK(strcmp(namesdb_by_crc(db, LIB_GBA, 0x8C5D6F2Bu), "Advance Wars (USA)") == 0);
	CHECK(strcmp(namesdb_by_crc(db, LIB_GBA, 1), "Low (World)") == 0);
	CHECK(strcmp(namesdb_by_crc(db, LIB_GBA, 0xFFFFFFFFu), "High (World)") == 0);
	CHECK(namesdb_by_crc(db, LIB_GBA, 0x11111111u) == NULL);  // GBC's, not GBA's
	CHECK(namesdb_by_crc(db, LIB_GBA, 0) == NULL);
	CHECK(strcmp(namesdb_by_crc(db, LIB_GBC, 0x11111111u), "Colour Game (USA)") == 0);
	CHECK(strcmp(namesdb_by_crc(db, LIB_GB, 0x22222222u), "Mono Game (Japan)") == 0);
	CHECK(strcmp(namesdb_by_crc(db, LIB_NES, 0x44444444u), "Headered (USA)") == 0);
	CHECK(namesdb_by_crc(db, LIB_NES, 0x44444445u) == NULL);
	CHECK(namesdb_by_crc(db, NAMESDB_CONSOLES, 1) == NULL);
	CHECK(namesdb_by_crc(NULL, LIB_GBA, 1) == NULL);
	// A name offset past the pool reads as no name
	struct namesdb_entry e;
	memcpy(&e, db + sizeof(struct namesdb_header), sizeof(e));
	u32 good = e.name;
	e.name = 0xFFFF;
	memcpy(db + sizeof(struct namesdb_header), &e, sizeof(e));
	CHECK(namesdb_by_crc(db, LIB_GBA, e.key) == NULL);
	e.name = good;
	memcpy(db + sizeof(struct namesdb_header), &e, sizeof(e));
}

// --- The cache ---------------------------------------------------------------------

static u8 cache_mem[NAMES_CACHE_BYTES];

static void put_mem(void *dst, const void *src, u32 bytes)
{
	CHECK(!(bytes & 1));
	CHECK((u8 *)dst >= cache_mem && (u8 *)dst + bytes <= cache_mem + sizeof(cache_mem));
	memcpy(dst, src, bytes);
}

static void fresh(struct names_cache *c)
{
	memset(cache_mem, 0x5A, sizeof(cache_mem));  // junk, as PSRAM after a game
	names_cache_init(c, cache_mem, put_mem);
	u32 at = 0;
	while (!names_cache_clear_part(c, at, 1000))
		at += 1000;
}

void test_names_cache_table(void)
{
	struct names_cache c;
	fresh(&c);
	CHECK_EQ(names_cache_find(&c, "/GBA/Zelda.gba"), NAMES_CACHE_NONE);
	u32 a = names_cache_put(&c, "/GBA/Zelda.gba", 100, 0x12345678, 0x5A0E6E6Au, NAMES_CRC | NAMES_DATE);
	CHECK_EQ(a, 0);
	CHECK_EQ(names_cache_find(&c, "/gba/ZELDA.GBA"), a);  // without ASCII case, as FAT
	CHECK_EQ(names_cache_find(&c, "/GBA/Zelda.gb"), NAMES_CACHE_NONE);
	const struct names_rec *r = names_cache_rec(&c, a);
	CHECK(strcmp(names_cache_path(&c, r), "/GBA/Zelda.gba") == 0);
	CHECK_EQ(names_rec_flags(r), NAMES_CRC | NAMES_DATE);
	CHECK_EQ(r->crc, 0x5A0E6E6Au);
	// A date, then a change: the record stays where it is
	names_cache_set_date(&c, a, 0x11112222);
	CHECK_EQ(r->date, 0x11112222);
	CHECK_EQ(names_cache_put(&c, "/GBA/Zelda.gba", 200, 0, 0x5A0E6E6Bu, NAMES_CRC), a);
	CHECK_EQ(c.count, 1);
	CHECK_EQ(r->size, 200);
	CHECK_EQ(names_rec_flags(r), NAMES_CRC);
	// Dropped and revived
	names_cache_drop(&c, a);
	CHECK(names_rec_flags(r) & NAMES_DEAD);
	CHECK_EQ(names_cache_find(&c, "/GBA/Zelda.gba"), a);
	CHECK_EQ(names_cache_put(&c, "/GBA/Zelda.gba", 100, 0, 0x5A0E6E6Au, NAMES_CRC), a);
	CHECK(!(names_rec_flags(r) & NAMES_DEAD));
	// Paths the cache doesn't take
	CHECK_EQ(names_cache_put(&c, "GBA/Zelda.gba", 1, 0, 0, 0), NAMES_CACHE_NONE);
	CHECK_EQ(names_cache_put(&c, "", 1, 0, 0, 0), NAMES_CACHE_NONE);
	char longest[NAMES_PATH_MAX + 2];
	memset(longest, 'a', sizeof(longest));
	longest[0] = '/';
	longest[NAMES_PATH_MAX + 1] = 0;
	CHECK_EQ(names_cache_put(&c, longest, 1, 0, 0, 0), NAMES_CACHE_NONE);
	longest[NAMES_PATH_MAX] = 0;
	CHECK(names_cache_put(&c, longest, 1, 0, 0, 0) != NAMES_CACHE_NONE);
	CHECK(!c.full);
}

void test_names_cache_fits(void)
{
	struct names_cache c;
	fresh(&c);
	u32 a = names_cache_put(&c, "/A.gba", 100, 0x1234, 1, NAMES_CRC | NAMES_DATE);
	u32 b = names_cache_put(&c, "/B.gba", 100, 0, 1, NAMES_CRC);  // from a PC: no date
	CHECK_EQ(names_cache_fits(names_cache_rec(&c, a), 100, 0x1234), NAMES_FIT);
	CHECK_EQ(names_cache_fits(names_cache_rec(&c, a), 100, 0x1235), NAMES_STALE);
	CHECK_EQ(names_cache_fits(names_cache_rec(&c, a), 101, 0x1234), NAMES_STALE);
	CHECK_EQ(names_cache_fits(names_cache_rec(&c, b), 100, 0x9999), NAMES_FIT_DATED);
	CHECK_EQ(names_cache_fits(names_cache_rec(&c, b), 99, 0x9999), NAMES_STALE);
	names_cache_set_date(&c, b, 0x9999);
	CHECK_EQ(names_cache_fits(names_cache_rec(&c, b), 100, 0x9999), NAMES_FIT);
	names_cache_drop(&c, a);
	CHECK_EQ(names_cache_fits(names_cache_rec(&c, a), 100, 0x1234), NAMES_STALE);
}

void test_names_cache_full(void)
{
	struct names_cache c;
	fresh(&c);
	char path[64];
	u32 i;
	for (i = 0; i < NAMES_CACHE_MAX + 10; i++) {
		snprintf(path, sizeof(path), "/Games/Game number %u.gba", i);
		u32 n = names_cache_put(&c, path, i, 0, i * 7, NAMES_CRC);
		if (n == NAMES_CACHE_NONE)
			break;
		CHECK_EQ(n, i);
	}
	CHECK_EQ(i, NAMES_CACHE_MAX);
	CHECK(c.full);
	// Every one is found, through every collision
	for (u32 j = 0; j < NAMES_CACHE_MAX; j += 1) {
		snprintf(path, sizeof(path), "/GAMES/game NUMBER %u.GBA", j);
		u32 n = names_cache_find(&c, path);
		CHECK_EQ(n, j);
		if (n != j)
			break;
	}
	snprintf(path, sizeof(path), "/Games/Game number %u.gba", NAMES_CACHE_MAX);
	CHECK_EQ(names_cache_find(&c, path), NAMES_CACHE_NONE);
	// A full table still changes the records it has
	CHECK_EQ(names_cache_put(&c, "/Games/Game number 5.gba", 5, 0, 1, NAMES_CRC), 5);
	// The pool fills before the table with long paths
	fresh(&c);
	char longp[NAMES_PATH_MAX + 1];
	memset(longp, 'x', NAMES_PATH_MAX);
	longp[0] = '/';
	longp[NAMES_PATH_MAX] = 0;
	for (i = 0; i < NAMES_CACHE_MAX; i++) {
		longp[1] = (char)('A' + i % 26);
		longp[2] = (char)('A' + i / 26 % 26);
		longp[3] = (char)('A' + i / 676 % 26);
		if (names_cache_put(&c, longp, 1, 0, 0, 0) == NAMES_CACHE_NONE)
			break;
	}
	CHECK_EQ(i, NAMES_CACHE_POOL_BYTES / (NAMES_PATH_MAX + 1));
	CHECK(c.full);
}

void test_names_lines(void)
{
	struct names_line l;
	char line[256];
	strcpy(line, "5A0E6E6A\t16777216\t5A3C7B20\t/GBA/Minish Cap.gba");
	CHECK(names_parse_line(line, &l));
	CHECK_EQ(l.crc, 0x5A0E6E6Au);
	CHECK_EQ(l.size, 16777216);
	CHECK_EQ(l.date, 0x5A3C7B20u);
	CHECK_EQ(l.flags, NAMES_CRC | NAMES_DATE);
	CHECK(strcmp(l.path, "/GBA/Minish Cap.gba") == 0);
	strcpy(line, "-\t40976\t-\t/NES/Odd\tname.nes");  // a tab in a path is the path's
	CHECK(names_parse_line(line, &l));
	CHECK_EQ(l.flags, 0);
	CHECK(strcmp(l.path, "/NES/Odd\tname.nes") == 0);
	strcpy(line, "5a0e6e6a\t1\t-\t/a.gb");
	CHECK(names_parse_line(line, &l) && l.crc == 0x5A0E6E6Au && l.flags == NAMES_CRC);
	// v0.4.1's lines, with a GBA game code second, read the same
	strcpy(line, "5A0E6E6A\tBZRE\t16777216\t5A3C7B20\t/GBA/Minish Cap.gba");
	CHECK(names_parse_line(line, &l));
	CHECK(l.crc == 0x5A0E6E6Au && l.size == 16777216 && l.date == 0x5A3C7B20u);
	CHECK(l.flags == (NAMES_CRC | NAMES_DATE) && strcmp(l.path, "/GBA/Minish Cap.gba") == 0);
	strcpy(line, "-\t-\t40976\t-\t/NES/Odd\tname.nes");
	CHECK(names_parse_line(line, &l) && l.flags == 0 && l.size == 40976);
	CHECK(strcmp(l.path, "/NES/Odd\tname.nes") == 0);
	strcpy(line, "5a0e6e6a\t-\t1\t-\t/a.gb");
	CHECK(names_parse_line(line, &l) && l.crc == 0x5A0E6E6Au && l.flags == NAMES_CRC && l.size == 1);
	static const char *const bad[] = {
		"",
		"# Torii's CRC cache",
		"#crc32 1a2b3c4d",
		"5A0E6E6\t1\t-\t/a.gb",  // 7 digits
		"5A0E6E6A0\t1\t-\t/a.gb",  // 9
		"5A0E6E6A\t\t-\t/a.gb",  // no size
		"5A0E6E6A\t12x\t-\t/a.gb",
		"5A0E6E6A\t1\t1234\t/a.gb",  // a date of 4 digits
		"5A0E6E6A\t1\t-\ta.gb",  // not from the root
		"5A0E6E6A\t1\t-",
		"5A0E6E6A\t1",
		"5A0E6E6\t-\t1\t-\t/a.gb",  // v0.4.1's: 7 digits
		"5A0E6E6A0\t-\t1\t-\t/a.gb",  // 9
		"5A0E6E6G\t-\t1\t-\t/a.gb",
		"5A0E6E6A\tbzre\t1\t-\t/a.gb",  // a code in lower case
		"5A0E6E6A\tBZR\t1\t-\t/a.gb",
		"5A0E6E6A\t-\t\t-\t/a.gb",  // no size
		"5A0E6E6A\t-\t12x\t-\t/a.gb",
		"5A0E6E6A\t-\t4294967296\t-\t/a.gb",  // over 32 bits
		"5A0E6E6A\t-\t1\t1234\t/a.gb",  // a date of 4 digits
		"5A0E6E6A\t-\t1\t-\ta.gb",  // not from the root
		"5A0E6E6A\t-\t1\t-",
		"5A0E6E6A",
	};
	for (u32 i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		snprintf(line, sizeof(line), "%s", bad[i]);
		CHECK(!names_parse_line(line, &l));
	}
	// Records write as they read
	struct names_cache c;
	fresh(&c);
	u32 a = names_cache_put(&c, "/GBA/Minish Cap.gba", 16777216, 0x5A3C7B20, 0x5A0E6E6Au, NAMES_CRC | NAMES_DATE);
	u32 b = names_cache_put(&c, "/NES/Mario.nes", 40976, 0, 0, 0);
	char out[256];
	CHECK_EQ(names_format_line(&c, a, out, sizeof(out)), 47);
	CHECK(strcmp(out, "5A0E6E6A\t16777216\t5A3C7B20\t/GBA/Minish Cap.gba\n") == 0);
	CHECK(names_format_line(&c, b, out, sizeof(out)) > 0);
	CHECK(strcmp(out, "-\t40976\t-\t/NES/Mario.nes\n") == 0);
	CHECK_EQ(names_format_line(&c, b, out, 10), 0);  // doesn't fit
	CHECK_EQ(names_format_line(&c, 2, out, sizeof(out)), 0);  // past the records
	names_cache_drop(&c, b);
	CHECK_EQ(names_format_line(&c, b, out, sizeof(out)), 0);  // dead
	names_format_line(&c, a, out, sizeof(out));
	out[strlen(out) - 1] = 0;
	CHECK(names_parse_line(out, &l));
	CHECK(l.crc == 0x5A0E6E6Au && l.size == 16777216 && l.date == 0x5A3C7B20u);
}

void test_names_db_name(void)
{
	make_db();
	struct names_cache c;
	fresh(&c);
	u32 hit = names_cache_put(&c, "/a.gba", 1, 0, 0x8C5D6F2Bu, NAMES_CRC);
	u32 unknown = names_cache_put(&c, "/b.gba", 1, 0, 0, 0);
	u32 miss = names_cache_put(&c, "/c.gba", 1, 0, 0xDEADBEEFu, NAMES_CRC);
	u32 colour = names_cache_put(&c, "/colour.gb", 1, 0, 0x11111111u, NAMES_CRC);
	u32 mono = names_cache_put(&c, "/mono.gbc", 1, 0, 0x22222222u, NAMES_CRC);
	u32 nes_miss = names_cache_put(&c, "/x.nes", 1, 0, 0xDEADBEEFu, NAMES_CRC);
#define NAME(n, console) names_db_name(db, names_cache_rec(&c, n), console)
	CHECK(strcmp(NAME(hit, LIB_GBA), "Advance Wars (USA)") == 0);
	// A CRC miss keeps its own name: a romhack, not its base game's (C4)
	CHECK(NAME(unknown, LIB_GBA) == NULL);
	CHECK(NAME(miss, LIB_GBA) == NULL);
	// The other Game Boy table, when the extension misleads
	CHECK(strcmp(NAME(colour, LIB_GB), "Colour Game (USA)") == 0);
	CHECK(strcmp(NAME(mono, LIB_GBC), "Mono Game (Japan)") == 0);
	CHECK(NAME(colour, LIB_NES) == NULL);
	CHECK(NAME(nes_miss, LIB_NES) == NULL);
	CHECK(NAME(hit, LIB_GBC) == NULL);
	names_cache_drop(&c, hit);
	CHECK(NAME(hit, LIB_GBA) == NULL);
	CHECK(names_db_name(NULL, names_cache_rec(&c, mono), LIB_GBC) == NULL);
#undef NAME
}
