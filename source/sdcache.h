// A small cache of the card's sectors for FatFs (diskio.c): the folder and
// FAT sectors that its one-sector window reads again and again (a slot rename
// read 234 sectors, 9 of them different). Write-through: every write goes to
// the card first and then updates the copies it covers, so the card always
// holds what a power cut would find, and a copy is never newer than the card.
// Only reads into FatFs's window go through it (a sector at a time); file
// data read straight into a caller's buffer, and the FAT hint's scan, go to
// the card.
#ifndef TORII_SDCACHE_H
#define TORII_SDCACHE_H

#include <gba_base.h>

#define SDCACHE_SECTORS 12

// 1 and the sector's 512 bytes in out if it is cached
int sdcache_read(u32 sector, u8 *out);
// The card gave sector's 512 bytes (a single-sector read): keep a copy
void sdcache_fill(u32 sector, const u8 *data);
// count sectors from sector were written from data (each the same 512 bytes
// at data when same is set): the copies follow. ok 0: the write failed, the
// card's bytes are unknown, and the copies go
void sdcache_wrote(u32 sector, u32 count, const u8 *data, int same, int ok);
// Forgets everything (a new mount)
void sdcache_clear(void);

#endif
