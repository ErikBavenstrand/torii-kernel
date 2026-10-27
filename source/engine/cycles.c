// See cycles.h.
#include <string.h>

#include "cycles.h"
#include "../tlog.h"

void cycles_report(const char *name, u32 cycles)
{
	tlog("frame-cycles: %s %lu", name, (unsigned long)cycles);
}

// Measured frames per name since the last summary. Names beyond the table
// count in no summary (their console lines still go out).
#define SUMMARY_NAMES 8
static struct {
	const char *name;
	u32 frames;
	u32 worst;
	unsigned long long total;
} summary[SUMMARY_NAMES] EWRAM_BSS;

void cycles_frame(const char *name, u32 cycles)
{
	tlog_console("frame-cycles: %s %lu", name, (unsigned long)cycles);
	for (u32 i = 0; i < SUMMARY_NAMES; i++) {
		if (summary[i].name && strcmp(summary[i].name, name) != 0)
			continue;
		summary[i].name = name;
		summary[i].frames++;
		summary[i].total += cycles;
		if (cycles > summary[i].worst)
			summary[i].worst = cycles;
		return;
	}
}

void cycles_summary(void)
{
	for (u32 i = 0; i < SUMMARY_NAMES && summary[i].name; i++)
		if (summary[i].worst > ENGINE_FRAME_CYCLES)
			tlog_debug("frame-cycles summary: %s: %lu frames, worst %lu, mean %lu", summary[i].name,
				   (unsigned long)summary[i].frames, (unsigned long)summary[i].worst,
				   (unsigned long)(summary[i].total / summary[i].frames));
	memset(summary, 0, sizeof(summary));
}
