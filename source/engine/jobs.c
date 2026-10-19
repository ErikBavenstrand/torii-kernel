// See jobs.h.
#include "jobs.h"
#include "cycles.h"
#include "frame.h"
#include "res.h"

static struct job jobs[JOB_CAP] EWRAM_BSS;
static u8 next_slot EWRAM_BSS;

void jobs_init(void)
{
	for (u32 i = 0; i < JOB_CAP; i++) {
		jobs[i].step = 0;
		jobs[i].ctx = 0;
		jobs[i].state = 0;
		jobs[i].owner = JOB_OWNER_NONE;
	}
	next_slot = 0;
}

static u8 id_of(u32 slot)
{
	return (u8)(jobs[slot].gen << 3 | slot);
}

static struct job *job_of(u8 id)
{
	if (id == JOB_NONE)
		return 0;
	struct job *j = &jobs[id & 7];
	return j->step && id_of(id & 7) == id ? j : 0;
}

u8 job_start(job_step step, void *ctx, u8 owner)
{
	for (u32 i = 0; i < JOB_CAP; i++) {
		struct job *j = &jobs[i];
		if (j->step)
			continue;
		j->step = step;
		j->ctx = ctx;
		j->state = 0;
		j->owner = owner;
		j->gen = (u8)((j->gen + 1) & 0x1F);
		if (id_of(i) == JOB_NONE)  // gen 31 in slot 7
			j->gen = 0;
		return id_of(i);
	}
	res_set_error("no free job slot (%d jobs running)", JOB_CAP);
	return JOB_NONE;
}

int job_running(u8 id)
{
	return job_of(id) != 0;
}

static void cancel(struct job *j)
{
	// The slot frees first, so a cancel from inside the step can't recurse
	job_step step = j->step;
	j->step = 0;
	j->state = JOB_CANCEL;
	step(j);
}

void job_cancel(u8 id)
{
	struct job *j = job_of(id);
	if (j)
		cancel(j);
}

void jobs_cancel_owner(u8 owner)
{
	for (u32 i = 0; i < JOB_CAP; i++)
		if (jobs[i].step && jobs[i].owner == owner)
			cancel(&jobs[i]);
}

u32 jobs_pending(void)
{
	u32 n = 0;
	for (u32 i = 0; i < JOB_CAP; i++)
		n += jobs[i].step != 0;
	return n;
}

u32 jobs_run_from(u32 start, u32 deadline)
{
	u32 steps = 0;
	u32 waiting = 0;  // slots whose step said JOB_WAIT
	do {
		struct job *j = 0;
		u32 slot = 0;
		for (u32 i = 0; i < JOB_CAP && !j; i++) {
			slot = (next_slot + i) % JOB_CAP;
			if (jobs[slot].step && !(waiting >> slot & 1)) {
				j = &jobs[slot];
				next_slot = (u8)((slot + 1) % JOB_CAP);
			}
		}
		if (!j)
			break;
		// The step may cancel itself (a view pop), and a new job may then
		// take the slot: only a job still the same one ends here
		job_step step = j->step;
		u8 gen = j->gen;
		int r = step(j);
		if (r == JOB_DONE && j->step == step && j->gen == gen) {
			j->step = 0;
			frame_wake();  // its result needs drawing
		} else if (r == JOB_WAIT) {
			waiting |= 1u << slot;
		}
		steps++;
	} while (cycles_since(start) < deadline);
	return steps;
}

u32 jobs_run(u32 deadline)
{
	return jobs_run_from(frame_start_cycles(), deadline);
}
