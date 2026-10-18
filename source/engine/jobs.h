// Jobs: slow work as state machines (pokeemerald's tasks), run a step at a
// time in the frame's spare cycles. A step does one bounded piece of work:
// one SD command, an f_read of 2-8 KB with its FIL kept in the context, an
// f_readdir of a few entries, one LZ77 cover. It switches on job->state and
// returns JOB_MORE or JOB_DONE.
//
// jobs_run runs steps round-robin until the frame's deadline, and always at
// least one step per frame while any job is waiting, so a heavy frame can
// slow jobs but never starve them.
//
// Cancelling calls the step once more with state JOB_CANCEL, so it can close
// files; whatever it returns, the job ends.
#ifndef TORII_ENGINE_JOBS_H
#define TORII_ENGINE_JOBS_H

#include <gba_base.h>

#define JOB_CAP 8
#define JOB_NONE 0xFF
#define JOB_CANCEL 0xFFFF
#define JOB_OWNER_NONE 0xFF

enum {
	JOB_MORE = 0,
	JOB_DONE = 1,
};

struct job;
typedef int (*job_step)(struct job *job);

struct job {
	job_step step;  // NULL: the slot is free
	void *ctx;  // the caller's state (a FIL, a cursor), kept between steps
	u16 state;  // the step's own state number, 0 at the start
	u8 owner;  // a view (T6), for jobs_cancel_owner; JOB_OWNER_NONE if none
	u8 gen;  // makes stale ids miss
};

// Frees every slot without calling the steps (engine start, tests).
void jobs_init(void);
// Starts a job; returns its id, or JOB_NONE if all JOB_CAP slots are busy
// (logged).
u8 job_start(job_step step, void *ctx, u8 owner);
// 1 while the job with this id hasn't finished.
int job_running(u8 id);
// Cancels a job (see above); does nothing if it has finished.
void job_cancel(u8 id);
// Cancels every job owner started.
void jobs_cancel_owner(u8 owner);
// Jobs waiting.
u32 jobs_pending(void);
// Runs steps until deadline cycles have passed since the frame woke
// (frame_elapsed), at least one. Returns the steps run.
u32 jobs_run(u32 deadline);
// Like jobs_run, measured from start (a cycles_now value) instead of the
// frame's wake: for loops outside the frame loop, and tests.
u32 jobs_run_from(u32 start, u32 deadline);

#endif
