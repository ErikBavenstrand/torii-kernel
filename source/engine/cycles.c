// See cycles.h.
#include "cycles.h"
#include "../tlog.h"

void cycles_report(const char *name, u32 cycles)
{
	tlog("frame-cycles: %s %lu", name, (unsigned long)cycles);
}
