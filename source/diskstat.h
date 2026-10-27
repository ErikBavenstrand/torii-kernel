// What the card did during an operation: FatFs's disk_read and disk_write
// calls and sectors (counted in diskio.c), and the time taken. On the cart a
// sector costs about 2 ms, so these counts are where an operation's time goes.
//
//	struct diskstat mark;
//	diskstat_start(&mark);
//	... FatFs calls ...
//	diskstat_log("save copy", &mark);  // "sd: save copy: 12 reads (40 sectors), ..."
#ifndef TORII_DISKSTAT_H
#define TORII_DISKSTAT_H

#include <gba_base.h>

struct diskstat {
	u32 reads, read_sectors;
	u32 read_errors;  // reads the card didn't answer
	u32 cache_hits;  // single-sector reads the cache answered (sdcache.h)
	u32 writes, write_sectors;
	u32 cycles;  // cycles_now() at diskstat_start
};

extern struct diskstat diskstat_total;  // since power-on (cycles unused)

void diskstat_start(struct diskstat *mark);
// Logs what the card did since mark (tlog)
void diskstat_log(const char *what, const struct diskstat *mark);

#endif
