// Reading a file from SD as a job, a few KB per step, so a frame never
// stalls on the card (decisions doc, Jobs). The FIL stays open in the
// struct between steps.
//
//	static struct sd_read r EWRAM_BSS;  // ~580 bytes: it holds a FIL
//	sd_read_start(&r, "/themes/x/bg.tas", dst, max, 4096, owner);
//	... later: r.result is SD_READ_OK (r.size bytes at dst) or an error
//
// The first step opens the file, each later one reads chunk bytes, and the
// last closes it. Cancelling the job (job_cancel, jobs_cancel_owner) closes
// the file. Another job can also drive a reader itself, calling
// sd_read_step from its own steps (the cover loader does).
//
// The destination is RAM (word-aligned; f_read fills it directly) or the
// PSRAM window: f_read can't write there, since the cart hides PSRAM while
// it talks to the card, so each chunk goes through the stage (engine/
// stage.h) and psram_write within the step.
//
// FatFs and the rules it lives by: steps run only from jobs_run, in the
// frame's job phase, so a step never overlaps the VBlank commit, another
// FatFs call or tlog_flush (which tlog.h allows only where no FatFs call is
// under way), and the kernel's ROM is mapped. Between steps, other code may
// use FatFs freely: the file stays open, which FatFs allows (FF_FS_LOCK 0).
#ifndef TORII_ENGINE_SDREAD_H
#define TORII_ENGINE_SDREAD_H

#include <gba_base.h>
#include "ff.h"
#include "jobs.h"

#define SD_READ_CHUNK_MIN 512
#define SD_READ_CHUNK_MAX 8192

enum {
	SD_READ_BUSY = 0,
	SD_READ_OK = 1,
	SD_READ_ERR_OPEN = -1,  // no such file, or no card
	SD_READ_ERR_READ = -2,  // f_read failed or came up short
	SD_READ_ERR_SIZE = -3,  // the file is bigger than max
	SD_READ_ERR_ARGS = -4,  // a bad destination or chunk
	SD_READ_CANCELLED = -5,
};

struct sd_read {
	FIL fil;  // first, so it is word-aligned
	const char *path;  // read by the first step only
	u8 *dst;
	u32 max;  // room at dst (a multiple of 4)
	u32 size;  // the file's size, once open
	u32 done;  // bytes read so far
	u16 chunk;  // bytes per step: a multiple of 512, 512-8192
	u8 open;  // fil is open
	s8 result;  // SD_READ_BUSY, then SD_READ_OK or an error
	// Called once when result is set (finished, failed or cancelled); may be NULL
	void (*finish)(struct sd_read *r);
	void *user;  // the caller's
};

// Sets r up to read path into dst (max bytes of room). path must stay valid
// until the first step has run. Checks the arguments: on a bad one, result
// is SD_READ_ERR_ARGS already (logged) and steps do nothing. It leaves
// finish and user alone, so set them (to 0 if unused) on a struct that isn't
// zeroed, such as one in pReadCache.
void sd_read_init(struct sd_read *r, const char *path, void *dst, u32 max, u16 chunk);
// One step: opens, reads a chunk, or closes. JOB_MORE until result is set,
// then JOB_DONE.
int sd_read_step(struct sd_read *r);
// Closes the file if open; result becomes SD_READ_CANCELLED unless set.
void sd_read_cancel(struct sd_read *r);
// sd_read_init, then a job (engine/jobs.h) that runs the steps. Returns the
// job's id, or JOB_NONE (r->result is then an error).
u8 sd_read_start(struct sd_read *r, const char *path, void *dst, u32 max, u16 chunk, u8 owner);

#endif
