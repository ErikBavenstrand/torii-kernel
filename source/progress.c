// See progress.h.
#include <stdio.h>

#include "progress.h"
#include "ui/dialog.h"
#include "ui/ui.h"

// How long progress_done shows the full bar: a fifth of a second, long
// enough to see
#define PROGRESS_DONE_FRAMES 12

static struct {
	const char *status;
	u16 lo, hi;
} step EWRAM_BSS;

void progress_step(const char *status, u32 lo, u32 hi)
{
	step.status = status;
	step.lo = (u16)(lo < PROGRESS_FULL ? lo : PROGRESS_FULL);
	step.hi = (u16)(hi < PROGRESS_FULL ? hi : PROGRESS_FULL);
	if (!dialog_progress_active())
		return;
	dialog_progress_set(step.lo, PROGRESS_FULL, NULL);
	dialog_progress_status(status);
}

void progress_part(u32 done, u32 total)
{
	if (!dialog_progress_active())
		return;
	char msg[48];
	snprintf(msg, sizeof(msg), "%s %lu%%", step.status ? step.status : "",
		 (unsigned long)progress_permille(0, 100, done, total));
	dialog_progress_set(progress_permille(step.lo, step.hi, done, total), PROGRESS_FULL, msg);
}

void progress_done(const char *status)
{
	if (!dialog_progress_active())
		return;
	dialog_progress_set(PROGRESS_FULL, PROGRESS_FULL, NULL);
	dialog_progress_status(status ? status : step.status ? step.status : "");
	for (u32 i = 0; i < PROGRESS_DONE_FRAMES; i++)
		ui_frame();
}
