// The game lists' hardware-free parts (screens/games.c; host tests:
// tests/host/test_gamelist.c): a game's size as the rows show it, and the
// sort that puts the favourites in A-Z order.
#ifndef TORII_GAMELIST_H
#define TORII_GAMELIST_H

#include <gba_base.h>

// bytes as a row's size, into out (size bytes): "256 KB" under a megabyte
// (rounded up, so a small file never reads "0 KB"), "1.5 MB" under 10 MB
// when it isn't whole, else whole megabytes rounded ("16 MB"), in the
// language's units and decimal separator.
void gamelist_size(char *out, u32 size, u32 bytes);

// The order of items a and b (<0, 0, >0)
typedef int (*gamelist_cmp)(void *ctx, u32 a, u32 b);
// Sorts n items (u16s) by cmp, stable (a merge sort, n log n compares), with
// tmp as scratch for n more.
void gamelist_sort(u16 *items, u16 *tmp, u32 n, gamelist_cmp cmp, void *ctx);

#endif
