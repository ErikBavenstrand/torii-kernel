// The game lists' hardware-free parts (source/gamelist.c).
#include <stdlib.h>
#include <string.h>

#include "../../source/gamelist.h"
#include "../kernel/torii_test.h"
#include "tests.h"

void test_gamelist_size(void)
{
	static const struct {
		u32 bytes;
		const char *text;
	} cases[] = {
		{0, "0 KB"},
		{1, "1 KB"},
		{1024, "1 KB"},
		{1025, "2 KB"},
		{256 * 1024, "256 KB"},
		{1024 * 1024 - 1, "1024 KB"},
		{1024 * 1024, "1 MB"},
		{1536 * 1024, "1.5 MB"},
		{4 * 1024 * 1024, "4 MB"},
		{4 * 1024 * 1024 + 1000, "4 MB"},
		{9 * 1024 * 1024 + 1000 * 1024, "10 MB"},  // 9.98 rounds to a whole 10
		{16 * 1024 * 1024, "16 MB"},
		{16 * 1024 * 1024 + 600 * 1024, "17 MB"},  // whole past 10
		{32 * 1024 * 1024, "32 MB"},
	};
	for (u32 i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		char out[16];
		gamelist_size(out, sizeof(out), cases[i].bytes, "KB", "MB");
		if (strcmp(out, cases[i].text))
			tlog_error("FAIL %lu bytes: \"%s\", expected \"%s\"", (unsigned long)cases[i].bytes, out,
				   cases[i].text);
		CHECK(!strcmp(out, cases[i].text));
	}
	char small[4];
	gamelist_size(small, sizeof(small), 16 * 1024 * 1024, "KB", "MB");
	CHECK_EQ(strlen(small), 3);  // cut to fit, still ended
}

static int by_value(void *ctx, u32 a, u32 b)
{
	const u16 *keys = ctx;
	return (int)keys[a] - (int)keys[b];
}

void test_gamelist_sort(void)
{
	enum { N = 1000 };
	static u16 keys[N], items[N], tmp[N];
	srand(7);
	for (u32 n = 0; n <= N; n = n ? n * 3 + 1 : 1) {
		for (u32 i = 0; i < n; i++) {
			keys[i] = (u16)(rand() % 50);  // many ties
			items[i] = (u16)i;
		}
		gamelist_sort(items, tmp, n, by_value, keys);
		for (u32 i = 1; i < n; i++) {
			CHECK(keys[items[i - 1]] <= keys[items[i]]);
			if (keys[items[i - 1]] == keys[items[i]])
				CHECK(items[i - 1] < items[i]);  // stable
		}
	}
}
