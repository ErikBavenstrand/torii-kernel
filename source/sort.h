// Sorts file list entries by name, ignoring ASCII case.
#ifndef TORII_SORT_H
#define TORII_SORT_H

#include <gba_base.h>

// Largest list sort_by_name takes: MAX_files in ez_define.h.
#define SORT_MAX 0x200

// Sorts count entries of size bytes each (at most 104, FM_FILE_FS), each
// starting with its NUL-terminated name. A stable merge sort over an index
// array, then one pass that moves each entry once.
void sort_by_name(void *entries, u32 count, u32 size);

#endif
