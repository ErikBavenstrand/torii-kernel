// The frame loop. A screen's loop is:
//
//	frame_init();
//	for (;;) {
//		u32 dt = frame_begin();      // halts to VBlank, commits, wakes
//		... read input, update motion by dt (call frame_wake while active)
//		if (!frame_idle()) {
//			jobs_run(budget);    // what draw leaves of the frame
//			... draw into the shadow state and the VRAM queue
//		}
//		frame_end();
//	}
//
// Interrupts: the kernel's IRQ handler stays in charge (source/crash.s, in
// IWRAM, chaining to libgba's IntrMain, also in IWRAM). It counts VBlanks in
// torii_frames and IntrMain sets REG_IFBIOS for VBlankIntrWait. Both run
// from IWRAM, so a VBlank during an SD transfer (at the slow WAITCNT, with
// the ROM unmapped) is safe. The engine adds no IRQ handler: a handler in
// IntrTable would run from ROM.
//
// Idle: when nothing woke the frame (input, motion, a finished job: they
// call frame_wake), no job waits and no shadow region is dirty, the frame
// skips draw, and the next commit has nothing to copy, so the loop only
// halts in VBlankIntrWait.
#ifndef TORII_ENGINE_FRAME_H
#define TORII_ENGINE_FRAME_H

#include <gba_base.h>

// dt never exceeds this, so motion after a long stall doesn't jump.
#define FRAME_DT_MAX 3

// Makes sure the kernel's IRQ handler is installed and VBlank interrupts are
// on, and starts the cycle counter. Call it after the kernel's irqInit.
void frame_init(void);
// Waits for the next VBlank, commits the shadow state (gfx_commit) if
// anything is dirty, and returns dt: VBlanks since the last frame_begin,
// 1 to FRAME_DT_MAX.
u32 frame_begin(void);
// Ends the frame: clears the wake flag and logs a pending frame_measure.
void frame_end(void);
// Something needs a draw this frame (input, motion, a job's result).
void frame_wake(void);
// 1 if the frame can skip draw: not woken, no job, nothing to commit.
int frame_idle(void);
// Cycles since this frame woke from VBlankIntrWait.
u32 frame_elapsed(void);
// The cycles_now value when this frame woke.
u32 frame_start_cycles(void);
// Busy cycles (wake to frame_end) of the last finished frame.
u32 frame_last_cycles(void);
// Frames begun since frame_init.
u32 frame_count(void);
// At this frame's frame_end, logs its busy cycles as "frame-cycles: NAME N"
// to mGBA's console and counts them in NAME's summary (cycles_frame). For
// the cycle budget test.
void frame_measure(const char *name);
// 1 if frame_measure was called for this frame.
int frame_measuring(void);

#endif
