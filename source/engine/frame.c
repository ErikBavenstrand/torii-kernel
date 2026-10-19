// See frame.h.
#include <gba_interrupt.h>
#include <gba_systemcalls.h>

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
static const char *measure_name EWRAM_BSS;

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
	measure_name = 0;
}

u32 frame_begin(void)
{
	VBlankIntrWait();
	start_cycles = cycles_now();
	if (gfx_pending())
		gfx_commit();
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
	if (measure_name) {
		cycles_frame(measure_name, last_cycles);
		measure_name = 0;
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

int frame_measuring(void)
{
	return measure_name != 0;
}

void frame_measure(const char *name)
{
	measure_name = name;
}
