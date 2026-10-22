#include "cutpoint.h"

#include <gba_base.h>

#include "tlog.h"

// Zero at each boot, as all .bss
static u32 cutpoints;

void cutpoint(const char *name)
{
	cutpoints++;
	tlog_debug("cutpoint %lu: %s", (unsigned long)cutpoints, name);
}
