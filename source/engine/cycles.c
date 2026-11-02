// See cycles.h.
#include <string.h>

#include "cycles.h"
#include "../logbuf.h"
#include "../tlog.h"

void cycles_report(const char *name, u32 cycles)
{
	tlog("frame-cycles: %s %lu", name, (unsigned long)cycles);
}

// Measured frames per name since the last summary. Names beyond the table
// count together under "other" (their console lines still go out by name).
#define SUMMARY_NAMES 8
static struct {
	const char *name;
	u32 frames;
	u32 worst;
	unsigned long long total;
} summary[SUMMARY_NAMES + 1] EWRAM_BSS;  // the last: "other"

void cycles_frame(const char *name, u32 cycles)
{
	tlog_console("frame-cycles: %s %lu", name, (unsigned long)cycles);
	cycles_note(name, cycles);
}

void cycles_note(const char *name, u32 cycles)
{
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
	summary[SUMMARY_NAMES].name = "other";
	summary[SUMMARY_NAMES].frames++;
	summary[SUMMARY_NAMES].total += cycles;
	if (cycles > summary[SUMMARY_NAMES].worst)
		summary[SUMMARY_NAMES].worst = cycles;
}

void cycles_summary(void)
{
	// The lines go into the ring: with no room left for all of them they
	// would push older lines out, so they wait for the next flush
	u32 lines = 0;
	for (u32 i = 0; i <= SUMMARY_NAMES && summary[i].name; i++)
		lines += summary[i].worst > ENGINE_FRAME_CYCLES;
	if (lines && tlog_pending() + lines * LOG_LINE_MAX > LOG_RING_SIZE)
		return;
	for (u32 i = 0; i <= SUMMARY_NAMES && summary[i].name; i++)
		if (summary[i].worst > ENGINE_FRAME_CYCLES)
			tlog_debug("frame-cycles summary: %s: %lu frames, worst %lu, mean %lu", summary[i].name,
				   (unsigned long)summary[i].frames, (unsigned long)summary[i].worst,
				   (unsigned long)(summary[i].total / summary[i].frames));
	memset(summary, 0, sizeof(summary));
}
