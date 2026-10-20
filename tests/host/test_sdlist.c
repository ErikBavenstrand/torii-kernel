// The SD card page's folder listing (source/sdlist.c): folders first, the
// sort order, the cap and the area filling, counts, and the texts.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../source/sdlist.h"
#include "../kernel/torii_test.h"
#include "tests.h"

static void put(void *dst, const void *src, u32 bytes)
{
	CHECK(!(bytes & 1) && !((size_t)dst & 1) && !((size_t)src & 1));
	memcpy(dst, src, bytes);
}

#define AREA_BYTES 0xD0000u  // the kernel's, PSRAM_NAMES_BYTES

static struct sdlist_scratch scratch;

static u8 *new_area(u32 cap)
{
	u8 *a = malloc(cap);
	memset(a, 0xA5, cap);  // nothing may rely on a zeroed area
	return a;
}

static void check_order(const struct sdlist *l, const char *const *want, u32 n)
{
	CHECK_EQ(l->count, n);
	for (u32 i = 0; i < n && i < l->count; i++) {
		const char *got = sdlist_name(sdlist_at(l, i));
		if (strcmp(got, want[i]))
			printf("  sdlist: row %u is \"%s\", expected \"%s\"\n", (unsigned)i, got, want[i]);
		CHECK(!strcmp(got, want[i]));
	}
}

void test_sdlist_order(void)
{
	u8 *area = new_area(64 * 1024);
	struct sdlist l;
	sdlist_begin(&l, area, 64 * 1024, put, &scratch);
	// In the card's order: files and folders mixed
	static const struct {
		const char *name;
		u32 size;
		int dir;
	} add[] = {
		{"readme.txt", 3000, 0},
		{"Mother 3.gba", 16 << 20, 0},
		{"GBA", 0, 1},
		{"Golden Sun - The Lost Age.gba", 1, 0},
		{"Mega Man 10.gba", 1, 0},
		{"golden sun.gba", 1, 0},
		{"Mega Man 2.gba", 1, 0},
		{"NES", 0, 1},
		{"Écran.gb", 1, 0},
		{"emu", 0, 1},
		{"Zelda.gbc", 1, 0},
		{"テスト.gba", 1, 0},
		{"v1.2", 0, 1},
		{"Golden Sun.gba", 1, 0},
		{"Mother.gba", 1, 0},
	};
	for (u32 i = 0; i < sizeof(add) / sizeof(add[0]); i++)
		CHECK(sdlist_add(&l, add[i].name, add[i].size, add[i].dir));
	sdlist_finish(&l);
	static const char *const want[] = {
		"emu",
		"GBA",
		"NES",
		"v1.2",
		// "Golden Sun" sorts before "Golden Sun - The Lost Age" (the
		// extension doesn't count), its two cases by the raw name, numbers
		// by value, accents folded, other scripts last
		"Écran.gb",
		"Golden Sun.gba",
		"golden sun.gba",
		"Golden Sun - The Lost Age.gba",
		"Mega Man 2.gba",
		"Mega Man 10.gba",
		"Mother.gba",
		"Mother 3.gba",
		"readme.txt",
		"Zelda.gbc",
		"テスト.gba",
	};
	check_order(&l, want, sizeof(want) / sizeof(want[0]));
	CHECK_EQ(l.folders, 4u);
	CHECK(!l.truncated);
	CHECK(sdlist_is_dir(sdlist_at(&l, 1)));
	CHECK(!sdlist_is_dir(sdlist_at(&l, 4)));
	CHECK_EQ(sdlist_console(sdlist_at(&l, 0)), (u32)LIB_CONSOLE_NONE);   // a folder
	CHECK_EQ(sdlist_console(sdlist_at(&l, 4)), (u32)LIB_GB);
	CHECK_EQ(sdlist_console(sdlist_at(&l, 11)), (u32)LIB_GBA);
	CHECK_EQ(sdlist_console(sdlist_at(&l, 12)), (u32)LIB_CONSOLE_NONE);  // readme.txt
	CHECK_EQ(sdlist_at(&l, 11)->size, 16u << 20);
	CHECK_EQ(sdlist_at(&l, 1)->count, SDLIST_COUNT_UNKNOWN);
	CHECK_EQ(sdlist_find(&l, "NES"), 2u);
	CHECK_EQ(sdlist_find(&l, "nes"), SDLIST_NONE);  // exact names only
	free(area);
}

// More than SDLIST_MAX: the first SDLIST_MAX are kept, in order, whatever
// order the card gives them in
void test_sdlist_cap(void)
{
	u8 *area = new_area(AREA_BYTES);
	struct sdlist l;
	sdlist_begin(&l, area, AREA_BYTES, put, &scratch);
	char name[32];
	u32 added = 0;
	srand(7);
	u32 perm[SDLIST_MAX + 1];
	for (u32 i = 0; i <= SDLIST_MAX; i++)
		perm[i] = i + 1;
	for (u32 i = SDLIST_MAX; i > 0; i--) {
		u32 j = (u32)rand() % (i + 1), t = perm[i];
		perm[i] = perm[j];
		perm[j] = t;
	}
	u32 dropped = 0;
	for (u32 i = 0; i <= SDLIST_MAX; i++) {
		snprintf(name, sizeof(name), "Game %04u.gba", (unsigned)perm[i]);
		if (sdlist_add(&l, name, 32, 0))
			added++;
		else
			dropped = perm[i];
	}
	CHECK_EQ(added, (u32)SDLIST_MAX);
	CHECK(l.truncated);
	sdlist_finish(&l);
	CHECK_EQ(l.count, (u32)SDLIST_MAX);
	u32 prev = 0;
	for (u32 i = 0; i < l.count; i++) {
		unsigned n = 0;
		CHECK(sscanf(sdlist_name(sdlist_at(&l, i)), "Game %u.gba", &n) == 1);
		CHECK(n > prev && n != dropped);
		prev = n;
	}
	free(area);
}

// An area too small for every name: the list stops, truncated, at what fits
void test_sdlist_area_full(void)
{
	u32 cap = SDLIST_ORDER_BYTES + 5 * 64;
	u8 *area = new_area(cap);
	struct sdlist l;
	sdlist_begin(&l, area, cap, put, &scratch);
	char name[64];
	u32 added = 0;
	for (u32 i = 0; i < 20; i++) {
		snprintf(name, sizeof(name), "A file of a fair length %02u.gba", (unsigned)i);
		added += (u32)sdlist_add(&l, name, 1, 0);
	}
	CHECK(added > 0 && added < 20);
	CHECK(l.truncated);
	CHECK(l.top <= cap);
	sdlist_finish(&l);
	CHECK_EQ(l.count, added);
	free(area);
	// Names past 255 bytes, or empty, aren't listed and don't truncate it
	cap = SDLIST_ORDER_BYTES + SDLIST_REC_MAX;  // room for the longest record
	area = new_area(cap);
	sdlist_begin(&l, area, cap, put, &scratch);
	char longer[300];
	memset(longer, 'x', sizeof(longer) - 1);
	longer[sizeof(longer) - 1] = 0;
	CHECK(!sdlist_add(&l, longer, 1, 0));
	CHECK(!sdlist_add(&l, "", 1, 0));
	CHECK(!l.truncated);
	longer[SDLIST_NAME_MAX] = 0;  // 255 bytes: fits
	CHECK(sdlist_add(&l, longer, 1, 0));
	sdlist_finish(&l);
	CHECK_EQ(strlen(sdlist_name(sdlist_at(&l, 0))), (size_t)SDLIST_NAME_MAX);
	free(area);
}

void test_sdlist_counts(void)
{
	u8 *area = new_area(8192);
	struct sdlist l;
	sdlist_begin(&l, area, 8192, put, &scratch);
	CHECK(sdlist_add(&l, "B", 0, 1));
	CHECK(sdlist_add(&l, "A", 0, 1));
	sdlist_finish(&l);
	sdlist_set_count(&l, 0, 176);
	sdlist_set_count(&l, 1, SDLIST_MAX + 1);
	CHECK_EQ(sdlist_at(&l, 0)->count, 176u);
	CHECK(!strcmp(sdlist_name(sdlist_at(&l, 0)), "A"));
	CHECK_EQ(sdlist_at(&l, 1)->count, SDLIST_COUNT_MORE);
	sdlist_set_count(&l, 1, SDLIST_COUNT_FAILED);
	CHECK_EQ(sdlist_at(&l, 1)->count, SDLIST_COUNT_FAILED);
	sdlist_set_count(&l, 5, 3);  // past the end: nothing
	free(area);
}

void test_sdlist_texts(void)
{
	char out[24];
	static const struct {
		u32 bytes;
		const char *want;
	} sizes[] = {
		{0, "0 B"},          {300, "300 B"},      {1023, "1023 B"},           {1024, "1 KB"},
		{1536, "1.5 KB"},    {3000, "2.9 KB"},    {10 * 1024 + 900, "10 KB"}, {512 * 1024, "512 KB"},
		{1572864, "1.5 MB"}, {16 << 20, "16 MB"}, {0xFFFFFFFFu, "3.9 GB"},
	};
	for (u32 i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
		sdlist_size_text(out, sizeof(out), sizes[i].bytes);
		if (strcmp(out, sizes[i].want))
			printf("  sdlist: %u bytes reads \"%s\", expected \"%s\"\n", (unsigned)sizes[i].bytes, out,
			       sizes[i].want);
		CHECK(!strcmp(out, sizes[i].want));
	}
	sdlist_number(out, sizeof(out), 1204);
	CHECK(!strcmp(out, "1,204"));
	sdlist_number(out, sizeof(out), 999);
	CHECK(!strcmp(out, "999"));
	sdlist_number(out, sizeof(out), 1234567);
	CHECK(!strcmp(out, "1,234,567"));
	sdlist_number(out, 4, 1234567);  // cut, never past the buffer
	CHECK(strlen(out) < 4);
	sdlist_count_text(out, sizeof(out), SDLIST_COUNT_UNKNOWN, "1 item", "items", "empty");
	CHECK(!strcmp(out, ""));
	sdlist_count_text(out, sizeof(out), SDLIST_COUNT_FAILED, "1 item", "items", "empty");
	CHECK(!strcmp(out, ""));
	sdlist_count_text(out, sizeof(out), 0, "1 item", "items", "empty");
	CHECK(!strcmp(out, "empty"));
	sdlist_count_text(out, sizeof(out), 1, "1 item", "items", "empty");
	CHECK(!strcmp(out, "1 item"));
	sdlist_count_text(out, sizeof(out), 1024, "1 item", "items", "empty");
	CHECK(!strcmp(out, "1,024 items"));
	sdlist_count_text(out, sizeof(out), SDLIST_COUNT_MORE, "1 item", "items", "empty");
	CHECK(!strcmp(out, "1,024+ items"));
}

// Random names from a few characters, so many share their first bytes and
// the sort has to fall back to the area: every neighbour pair in order
void test_sdlist_random(void)
{
	u8 *area = new_area(AREA_BYTES);
	struct sdlist l;
	static const char chars[] = "ab`B 0129.-";
	srand(11);
	for (u32 round = 0; round < 20; round++) {
		sdlist_begin(&l, area, AREA_BYTES, put, &scratch);
		u32 n = 1 + (u32)rand() % SDLIST_MAX;
		for (u32 i = 0; i < n; i++) {
			char name[24];
			u32 len = 1 + (u32)rand() % 16;
			for (u32 j = 0; j < len; j++)
				name[j] = chars[(u32)rand() % (sizeof(chars) - 1)];
			name[len] = 0;
			sdlist_add(&l, name, 1, rand() % 4 == 0);  // duplicates can't be on a card; harmless here
		}
		sdlist_finish(&l);
		for (u32 i = 1; i < l.count; i++) {
			int c = sdlist_compare(sdlist_at(&l, i - 1), sdlist_at(&l, i));
			if (c > 0)
				printf("  sdlist: \"%s\" before \"%s\"\n", sdlist_name(sdlist_at(&l, i - 1)),
				       sdlist_name(sdlist_at(&l, i)));
			CHECK(c <= 0);
		}
	}
	free(area);
}
