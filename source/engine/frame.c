// See frame.h.
#include <gba_interrupt.h>
#include <gba_systemcalls.h>
#include <string.h>

#include "frame.h"
#include "cycles.h"
#include "gfx.h"
#include "jobs.h"
#include "../cartlog.h"
#include "../tlog.h"

extern void torii_irq(void);  // source/crash.s

static u32 last_vblank EWRAM_BSS;
static u32 start_cycles EWRAM_BSS;
static u32 last_cycles EWRAM_BSS;
static u32 frames EWRAM_BSS;
static u8 woken EWRAM_BSS;
// This frame's frame_measure names
static const char *measure_names[FRAME_MEASURES] EWRAM_BSS;
static u8 measure_count EWRAM_BSS;
// frame_split marks of this frame, and the line its commit ended on
static u32 split_at[FRAME_SPLITS] EWRAM_BSS;
static u8 split_count EWRAM_BSS;
static u8 commit_line EWRAM_BSS;
// The slowest measured frame since the last report: logged once measuring
// stops, as logging costs a frame's worth of cycles it would add
static const char *worst_name EWRAM_BSS;
static u32 worst_cycles EWRAM_BSS;
static u32 worst_split[FRAME_SPLITS] EWRAM_BSS;
static u8 worst_line EWRAM_BSS;

void frame_init(void)
{
	if (INT_VECTOR != torii_irq) {
		tlog_error("engine: the kernel's IRQ handler wasn't installed");
		cartlog_install();
	}
	irqEnable(IRQ_VBLANK);
	REG_IME = 1;
	cycles_init();
	last_vblank = torii_frames;
	start_cycles = cycles_now();
	last_cycles = 0;
	frames = 0;
	woken = 1;
	measure_count = 0;
}

u32 frame_begin(void)
{
	VBlankIntrWait();
	start_cycles = cycles_now();
	if (gfx_pending())
		gfx_commit();
	commit_line = (u8) * (vu16 *)0x04000006;
	split_count = 0;
	frame_split();
	u32 now = torii_frames;
	u32 dt = now - last_vblank;
	last_vblank = now;
	frames++;
	if (dt < 1)
		dt = 1;
	if (dt > FRAME_DT_MAX)
		dt = FRAME_DT_MAX;
	return dt;
}

void frame_end(void)
{
	last_cycles = cycles_since(start_cycles);
	woken = 0;
	if (measure_count) {
		for (u32 i = 0; i < measure_count; i++)
			cycles_frame(measure_names[i], last_cycles);
		if (last_cycles > worst_cycles) {
			// Where the time went: the commit, then each stretch between marks
			worst_name = measure_names[0];
			worst_cycles = last_cycles;
			worst_line = commit_line;
			for (u32 i = 0; i < FRAME_SPLITS; i++)
				worst_split[i] =
					i < split_count ? split_at[i] - (i ? split_at[i - 1] : start_cycles) : 0;
		}
		measure_count = 0;
	} else if (worst_name) {
		const u32 *d = worst_split;
		tlog_console("frame-split: %s %lu, commit to line %u: %lu %lu %lu %lu %lu %lu %lu %lu", worst_name,
			     (unsigned long)worst_cycles, worst_line, (unsigned long)d[0], (unsigned long)d[1],
			     (unsigned long)d[2], (unsigned long)d[3], (unsigned long)d[4], (unsigned long)d[5],
			     (unsigned long)d[6], (unsigned long)d[7]);
		worst_name = 0;
		worst_cycles = 0;
	}
}

void frame_wake(void)
{
	woken = 1;
}

int frame_idle(void)
{
	return !woken && !jobs_pending() && !gfx_pending();
}

u32 frame_elapsed(void)
{
	return cycles_since(start_cycles);
}

u32 frame_start_cycles(void)
{
	return start_cycles;
}

u32 frame_last_cycles(void)
{
	return last_cycles;
}

u32 frame_count(void)
{
	return frames;
}

void frame_split(void)
{
	if (split_count < FRAME_SPLITS)
		split_at[split_count++] = cycles_now();
}

int frame_measuring(void)
{
	return measure_count != 0;
}

void frame_measure(const char *name)
{
	for (u32 i = 0; i < measure_count; i++)
		if (measure_names[i] == name || !strcmp(measure_names[i], name))
			return;
	if (measure_count < FRAME_MEASURES)
		measure_names[measure_count++] = name;
	else
		tlog_error("frame: more than %d names measure one frame (%s)", FRAME_MEASURES, name);
}
