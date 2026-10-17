#include "sort.h"

#include <string.h>
#include <strings.h>

static u16 order[SORT_MAX] EWRAM_BSS;
static u16 scratch[SORT_MAX] EWRAM_BSS;
static u8 held[104] EWRAM_BSS;

static const char *name_of(const u8 *entries, u32 size, u16 i)
{
	return (const char *) (entries + i * size);
}

void sort_by_name(void *entries, u32 count, u32 size)
{
	u8 *base = entries;
	if (count < 2 || count > SORT_MAX || size > sizeof(held))
		return;

	for (u32 i = 0; i < count; i++)
		order[i] = i;

	// Bottom-up merge sort of the indices; ties keep their listing order.
	u16 *from = order, *to = scratch;
	for (u32 width = 1; width < count; width *= 2) {
		for (u32 lo = 0; lo < count; lo += 2 * width) {
			u32 mid = lo + width < count ? lo + width : count;
			u32 hi = lo + 2 * width < count ? lo + 2 * width : count;
			u32 a = lo, b = mid, out = lo;
			while (a < mid && b < hi) {
				if (strcasecmp(name_of(base, size, from[b]), name_of(base, size, from[a])) < 0)
					to[out++] = from[b++];
				else
					to[out++] = from[a++];
			}
			while (a < mid)
				to[out++] = from[a++];
			while (b < hi)
				to[out++] = from[b++];
		}
		u16 *swap = from;
		from = to;
		to = swap;
	}

	// Slot p takes entry from[p]: follow each cycle of the permutation,
	// moving every entry once and marking finished slots with from[p] = p.
	for (u32 p = 0; p < count; p++) {
		if (from[p] == p)
			continue;
		memcpy(held, base + p * size, size);
		u32 slot = p;
		while (from[slot] != p) {
			u32 next = from[slot];
			memcpy(base + slot * size, base + next * size, size);
			from[slot] = slot;
			slot = next;
		}
		memcpy(base + slot * size, held, size);
		from[slot] = slot;
	}
}
