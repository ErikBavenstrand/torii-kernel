// sort_by_name (source/sort.c). Each list, and the work buffer, is a heap
// block of exactly its size, so AddressSanitizer catches any access past it.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../source/sort.h"
#include "../kernel/torii_test.h"
#include "tests.h"

// Like the kernel's FM_FILE_FS: a name, then data that must move with it.
typedef struct {
	char filename[100];
	u32 filesize;
} file_entry;

static void sort(void *entries, u32 count, u32 size)
{
	void *work = malloc(SORT_WORK_SIZE);
	sort_by_name(entries, count, size, work);
	free(work);
}

static file_entry *make_list(const char *const *names, u32 count)
{
	file_entry *list = malloc(count * sizeof(file_entry));
	for (u32 i = 0; i < count; i++) {
		memset(&list[i], 0, sizeof(file_entry));
		snprintf(list[i].filename, sizeof(list[i].filename), "%s", names[i]);
		list[i].filesize = i;
	}
	return list;
}

void test_sort_matches_kernel(void)
{
	static const char *const names[] = {
		"zelda.gbc", "Advance Wars.gba", "mario.gba", "Zelda.gba", "BANJO.gba", "aria.gba",
	};
	static const char *const sorted[] = {
		"Advance Wars.gba", "aria.gba", "BANJO.gba", "mario.gba", "Zelda.gba", "zelda.gbc",
	};
	file_entry *list = make_list(names, 6);
	sort(list, 6, sizeof(file_entry));
	for (u32 i = 0; i < 6; i++)
		CHECK(strcmp(list[i].filename, sorted[i]) == 0);
	CHECK_EQ(list[0].filesize, 1);  // the whole entry moves with its name
	free(list);
}

void test_sort_is_stable(void)
{
	static const char *const names[] = {"b", "A", "a", "B", "a", "A"};
	file_entry *list = make_list(names, 6);
	sort(list, 6, sizeof(file_entry));
	// Names equal but for case keep their listing order.
	static const u32 order[] = {1, 2, 4, 5, 0, 3};
	for (u32 i = 0; i < 6; i++)
		CHECK_EQ(list[i].filesize, order[i]);
	free(list);
}

void test_sort_limits(void)
{
	static const char *const names[] = {"b", "a"};
	file_entry *list = make_list(names, 2);

	sort(list, 0, sizeof(file_entry));
	sort(list, 1, sizeof(file_entry));
	CHECK(strcmp(list[0].filename, "b") == 0);

	// Entries larger than its 104-byte buffer are left alone.
	char *big = calloc(2, 105);
	strcpy(big, "b");
	strcpy(big + 105, "a");
	sort(big, 2, 105);
	CHECK(strcmp(big, "b") == 0);
	free(big);

	// 104 bytes, the largest entry, moves whole.
	char *edge = calloc(2, 104);
	strcpy(edge, "b");
	edge[103] = 'B';
	strcpy(edge + 104, "a");
	edge[207] = 'A';
	sort(edge, 2, 104);
	CHECK(strcmp(edge, "a") == 0);
	CHECK_EQ(edge[103], 'A');
	CHECK_EQ(edge[207], 'B');
	free(edge);

	// Entries as small as one name byte and its NUL.
	char tiny[6] = {'c', 0, 'a', 0, 'b', 0};
	sort(tiny, 3, 2);
	CHECK(memcmp(tiny, "a\0b\0c\0", 6) == 0);
	free(list);
}

void test_sort_largest_list(void)
{
	// SORT_MAX entries in reverse order, then one too many (left unsorted).
	u32 count = SORT_MAX + 1;
	file_entry *list = malloc(count * sizeof(file_entry));
	for (u32 i = 0; i < count; i++) {
		snprintf(list[i].filename, sizeof(list[i].filename), "game%04u", (unsigned)(count - 1 - i));
		list[i].filesize = i;
	}
	sort(list, count, sizeof(file_entry));
	CHECK_EQ(list[0].filesize, 0);

	sort(list + 1, SORT_MAX, sizeof(file_entry));
	int in_order = 1;
	for (u32 i = 2; i < count; i++)
		in_order &= strcmp(list[i - 1].filename, list[i].filename) < 0;
	CHECK(in_order);
	CHECK_EQ(list[1].filesize, count - 1);
	free(list);
}

// sort_by: entries holding a pointer to their name, as the SD list's hold a
// PSRAM handle
typedef struct {
	const char *name;
	u32 index;
} name_ref;

static const char *ref_name(const void *entry)
{
	return ((const name_ref *)entry)->name;
}

void test_sort_by_accessor(void)
{
	static const char *const names[] = {"Zelda.gba", "advance.gba", "Metroid.gba", "advance.gba", "B.gba"};
	static const u32 order[] = {1, 3, 4, 2, 0};  // case ignored, ties keep their order
	name_ref *list = malloc(5 * sizeof(name_ref));
	for (u32 i = 0; i < 5; i++) {
		list[i].name = names[i];
		list[i].index = i;
	}
	void *work = malloc(SORT_WORK_SIZE);
	sort_by(list, 5, sizeof(name_ref), work, ref_name);
	for (u32 i = 0; i < 5; i++)
		CHECK_EQ(list[i].index, order[i]);
	free(work);
	free(list);
}
