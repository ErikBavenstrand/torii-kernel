// The FAT's first free cluster, found ahead of time (fathint.c).
//
// FatFs looks for a free cluster from its "last allocated" hint (FSINFO's
// Next Free on FAT32). With no hint (a card a PC wrote last, or one whose
// hint is stale) the first file or folder made after power-on reads the FAT
// from its start, one sector per 128 clusters: on a card with gigabytes of
// games ahead of the free space that is hundreds of reads, a second or more
// before a save copy's first byte (a hint into used clusters costs the same
// from there). This job reads the FAT in the background
// from the mount on (the boot's save question shows meanwhile), 8 sectors a
// step straight into the job stage (never FatFs's window, which may hold a
// change), and sets the hint at the first free cluster it finds.
#ifndef TORII_FATHINT_H
#define TORII_FATHINT_H

// Starts the job on the mounted card (FAT32 only: exFAT keeps a bitmap, FAT16
// is small); nothing if the job runs. It starts at the hint, if one is set.
void fathint_start(void);

#endif
