// Sorts file list entries by name, ignoring ASCII case.
#ifndef TORII_SORT_H
#define TORII_SORT_H

#include <gba_base.h>

// Largest list sort_by_name takes: MAX_files in ez_define.h.
#define SORT_MAX 0x200
// Largest entry it takes: FM_FILE_FS.
#define SORT_ENTRY_MAX 104
// The caller's work buffer: two index arrays and one entry. The kernel lends
// it from pReadCache (READCACHE_SORT_WORK in ez_define.h), not EWRAM of its own.
#define SORT_WORK_SIZE (2 * SORT_MAX * sizeof(u16) + SORT_ENTRY_MAX)

// Sorts count entries of size bytes each (at most SORT_ENTRY_MAX), each
// starting with its NUL-terminated name. A stable merge sort over an index
// array, then one pass that moves each entry once. work is SORT_WORK_SIZE
// bytes, 2-byte aligned, and holds nothing once it returns.
void sort_by_name(void *entries, u32 count, u32 size, void *work);

#endif
