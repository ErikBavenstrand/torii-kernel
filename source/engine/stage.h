// Staging memory for asset jobs: the first STAGE_BYTES of pReadCache, the
// kernel's 128 KB SD buffer, borrowed one job step at a time.
//
// The rule: a job step may use the stage freely, but nothing in it survives
// the step. Whatever must last between steps lives in PSRAM (the SD reader
// stages a file there) or in the job's context. So the stage never overlaps
// pReadCache's other users, as long as none of them keeps data in pReadCache
// across frames that run jobs (jobs_run). None does today: launching, saves,
// NOR writes, cheats, Diagnostics and mode 3 drawing all use it within one
// call and run no jobs meanwhile. A screen that wants to keep data in
// pReadCache while jobs run must stop the asset jobs first
// (cover_shutdown).
//
// Steps run only from jobs_run, never from an IRQ, so a step can't land in
// the middle of another pReadCache user.
#ifndef TORII_ENGINE_STAGE_H
#define TORII_ENGINE_STAGE_H

#include <gba_base.h>

#define STAGE_BYTES 0x8000

extern u8 pReadCache[];

// The stage, word-aligned.
static inline u8 *stage_buffer(void)
{
	return pReadCache;
}

#endif
