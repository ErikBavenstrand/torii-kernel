// The game lists' hardware-free parts (gamelist.h).
#include <stdio.h>
#include <string.h>

#include "gamelist.h"

#define KB 1024u
#define MB (1024u * 1024u)

void gamelist_size(char *out, u32 size, u32 bytes, const char *kb, const char *mb)
{
	if (bytes < MB) {
		snprintf(out, size, "%lu %s", (unsigned long)((bytes + KB - 1) / KB), kb);
		return;
	}
	u32 tenths = (bytes / KB * 10u + KB / 2) / KB;  // megabytes in tenths, rounded
	if (tenths < 100 && tenths % 10)
		snprintf(out, size, "%lu.%lu %s", (unsigned long)(tenths / 10), (unsigned long)(tenths % 10), mb);
	else
		snprintf(out, size, "%lu %s", (unsigned long)((tenths + 5) / 10), mb);
}

void gamelist_sort(u16 *items, u16 *tmp, u32 n, gamelist_cmp cmp, void *ctx)
{
	// Bottom-up: runs of width w merged pairwise from src into dst, which
	// then swap; the result is copied back if it ends in tmp
	u16 *src = items, *dst = tmp;
	for (u32 w = 1; w < n; w *= 2) {
		for (u32 lo = 0; lo < n; lo += 2 * w) {
			u32 mid = lo + w < n ? lo + w : n, hi = lo + 2 * w < n ? lo + 2 * w : n;
			u32 i = lo, j = mid, k = lo;
			while (i < mid && j < hi)
				dst[k++] = cmp(ctx, src[j], src[i]) < 0 ? src[j++] : src[i++];
			while (i < mid)
				dst[k++] = src[i++];
			while (j < hi)
				dst[k++] = src[j++];
		}
		u16 *t = src;
		src = dst;
		dst = t;
	}
	if (src != items)
		memcpy(items, src, n * sizeof(*items));
}
