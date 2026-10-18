// File list sorting (source/sort.c).
#include <string.h>

#include <gba_base.h>

#include "../../source/sort.h"
#include "torii_test.h"
#include "tests.h"

typedef struct {
	char filename[100];
	u32 filesize;
} file_entry;

// 52 KB: borrows the kernel's 128 KB read cache, which is free while tests run,
// and its last bytes as the sort's work buffer.
extern u8 pReadCache[];
#define files ((file_entry *)pReadCache)
#define work (pReadCache + 0x20000 - SORT_WORK_SIZE)
_Static_assert(SORT_MAX * sizeof(file_entry) <= 0x20000 - SORT_WORK_SIZE, "the list and work overlap");

void test_sort_by_name(void)
{
	static const char *const names[] = {
		"zelda.gbc", "Advance Wars.gba", "mario.gba", "Zelda.gba", "BANJO.gba", "aria.gba",
	};
	static const char *const sorted[] = {
		"Advance Wars.gba", "aria.gba", "BANJO.gba", "mario.gba", "Zelda.gba", "zelda.gbc",
	};
	unsigned n = sizeof(names) / sizeof(*names);
	for (unsigned i = 0; i < n; i++) {
		strcpy(files[i].filename, names[i]);
		files[i].filesize = i;
	}
	sort_by_name(files, n, sizeof(file_entry), work);
	for (unsigned i = 0; i < n; i++)
		CHECK(strcmp(files[i].filename, sorted[i]) == 0);
	CHECK_EQ(files[0].filesize, 1);  // the whole entry moves with its name
	CHECK_EQ(files[5].filesize, 0);
}

void test_sort_full_list(void)
{
	// A full, reversed list ends up in order, with every entry kept once.
	for (unsigned i = 0; i < SORT_MAX; i++) {
		unsigned v = SORT_MAX - 1 - i;
		files[i].filename[0] = 'a' + v / 26 / 26 % 26;
		files[i].filename[1] = (v / 26 % 2) ? 'A' + v / 26 % 26 : 'a' + v / 26 % 26;
		files[i].filename[2] = 'a' + v % 26;
		files[i].filename[3] = 0;
		files[i].filesize = v;
	}
	sort_by_name(files, SORT_MAX, sizeof(file_entry), work);
	for (unsigned i = 0; i < SORT_MAX; i++)
		CHECK_EQ(files[i].filesize, i);
}
