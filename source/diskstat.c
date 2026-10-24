// What the card did during an operation (diskstat.h)
#include "diskstat.h"
#include "tlog.h"
#include "engine/cycles.h"

#define CYCLES_PER_MS 16780u

struct diskstat diskstat_total;

void diskstat_start(struct diskstat *mark)
{
	*mark = diskstat_total;
	mark->cycles = cycles_now();
}

void diskstat_log(const char *what, const struct diskstat *mark)
{
	tlog("sd: %s: %lu reads (%lu sectors, %lu more from the cache), %lu writes (%lu sectors) in %lu ms", what,
	     (unsigned long)(diskstat_total.reads - mark->reads),
	     (unsigned long)(diskstat_total.read_sectors - mark->read_sectors),
	     (unsigned long)(diskstat_total.cache_hits - mark->cache_hits),
	     (unsigned long)(diskstat_total.writes - mark->writes),
	     (unsigned long)(diskstat_total.write_sectors - mark->write_sectors),
	     (unsigned long)(cycles_since(mark->cycles) / CYCLES_PER_MS));
}
