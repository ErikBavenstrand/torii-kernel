// CPU cycle counter: timers 2 and 3 cascaded into one free-running 32-bit
// count of CPU cycles (16.78 MHz; it wraps after 256 s). Everything that
// times code reads it and subtracts: the frame budget, the jobs' deadline,
// `frame-cycles:` figures and Diagnostics' speed tests. Nothing restarts it,
// so timings can nest.
//
// Cycles spent in IRQ handlers inside a timed section count towards it: the
// figures are wall-clock time, as the frame budget needs. The kernel's VBlank
// handler (source/crash.s, then libgba's IntrMain) takes about 100 cycles.
#ifndef TORII_ENGINE_CYCLES_H
#define TORII_ENGINE_CYCLES_H

#include <gba_base.h>

// Cycles in one frame: 228 lines of 1232 cycles.
#define ENGINE_FRAME_CYCLES 280896u
// Cycles in VBlank: lines 160 to 227.
#define ENGINE_VBLANK_CYCLES 83776u

#define CYCLES_TM2CNT_L (*(vu16 *)0x04000108)
#define CYCLES_TM2CNT_H (*(vu16 *)0x0400010A)
#define CYCLES_TM3CNT_L (*(vu16 *)0x0400010C)
#define CYCLES_TM3CNT_H (*(vu16 *)0x0400010E)

// Starts the counter if it isn't running (both timers enabled, timer 3
// counting timer 2's overflows); leaves a running one alone.
static inline __attribute__((always_inline)) void cycles_init(void)
{
	if ((CYCLES_TM2CNT_H & 0xC7) == 0x80 && (CYCLES_TM3CNT_H & 0xC7) == 0x84)
		return;
	CYCLES_TM2CNT_H = 0;
	CYCLES_TM3CNT_H = 0;
	CYCLES_TM2CNT_L = 0;
	CYCLES_TM3CNT_L = 0;
	CYCLES_TM3CNT_H = 0x84;  // enabled, cascade
	CYCLES_TM2CNT_H = 0x80;  // enabled, every cycle
}

// The count now. Always inlined, so it runs from wherever its caller runs (IWRAM
// code at a test WAITCNT included). The high half is read on both sides of
// the low one, so a carry between the reads can't tear the value.
static inline __attribute__((always_inline)) u32 cycles_now(void)
{
	u32 hi, lo;
	do {
		hi = CYCLES_TM3CNT_L;
		lo = CYCLES_TM2CNT_L;
	} while (hi != CYCLES_TM3CNT_L);
	return hi << 16 | lo;
}

// Cycles since start, a value cycles_now returned.
static inline __attribute__((always_inline)) u32 cycles_since(u32 start)
{
	return cycles_now() - start;
}

// Logs "frame-cycles: NAME N", the line tests/perf/budget.txt checks and
// Diagnostics' renderer step compares. NAME has no spaces.
void cycles_report(const char *name, u32 cycles);

#endif
