// The card's sector cache (sdcache.h). In IWRAM (.bss): 6 KB.
#include <string.h>

#include "sdcache.h"

static u32 tags[SDCACHE_SECTORS];  // the sector + 1; 0: empty
static u32 copies[SDCACHE_SECTORS][512 / 4];
static u32 rng = 1;

static int find(u32 sector)
{
	for (int i = 0; i < SDCACHE_SECTORS; i++) {
		if (tags[i] == sector + 1)
			return i;
	}
	return -1;
}

int sdcache_read(u32 sector, u8 *out)
{
	int i = find(sector);
	if (i < 0)
		return 0;
	memcpy(out, copies[i], 512);
	return 1;
}

void sdcache_fill(u32 sector, const u8 *data)
{
	int i = find(sector);
	if (i < 0) {
		for (i = 0; i < SDCACHE_SECTORS && tags[i]; i++)
			;
		if (i == SDCACHE_SECTORS) {
			// A random victim: a loop over a folder bigger than the cache
			// keeps some of it, where LRU or FIFO would keep none
			rng = rng * 1103515245u + 12345u;
			i = (int)((rng >> 16) % SDCACHE_SECTORS);
		}
	}
	memcpy(copies[i], data, 512);
	tags[i] = sector + 1;
}

void sdcache_wrote(u32 sector, u32 count, const u8 *data, int same, int ok)
{
	for (int i = 0; i < SDCACHE_SECTORS; i++) {
		u32 s = tags[i] - 1;
		if (!tags[i] || s < sector || s - sector >= count)
			continue;
		if (ok)
			memcpy(copies[i], data + (same ? 0 : (s - sector) * 512), 512);
		else
			tags[i] = 0;
	}
}

void sdcache_clear(void)
{
	memset(tags, 0, sizeof(tags));
}
