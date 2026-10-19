// Staging memory for asset jobs: READCACHE_STAGE_SIZE bytes of pReadCache,
// the kernel's 128 KB scratch buffer, at READCACHE_STAGE. The buffer's owner
// map is in source/ez_define.h; this is one of its owners.
//
// The rule: a job step may use the stage freely, but nothing in it survives
// the step. Whatever must last between steps lives in PSRAM (the SD reader
// stages a file there) or in the job's context. Steps run only from
// jobs_run, never from an IRQ and never inside another owner's operation, so
// the stage can overlap every other owner. One that wants to keep data in
// pReadCache across frames that run jobs must stop the asset jobs first
// (cover_shutdown). The cheat screen's list (pCHTbuffer) is such an owner:
// it calls cover_cache_invalidate first and starts no jobs.
#ifndef TORII_ENGINE_STAGE_H
#define TORII_ENGINE_STAGE_H

#include <gba_base.h>
#include "../ez_define.h"

#define STAGE_BYTES READCACHE_STAGE_SIZE

extern u8 pReadCache[];

// The stage. pReadCache is word-aligned as the kernel links it (f_read's DMA
// relies on that too); cover_init asserts it.
static inline u8 *stage_buffer(void)
{
	return pReadCache + READCACHE_STAGE;
}

#endif
