// See progress.h.
#include <gba_interrupt.h>

#include "Ezcard_OP.h"
#include "progress.h"
#include "tlog.h"
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
	step.lo = (u16)(lo < PROGRESS_FULL ? lo : PROGRESS_FULL);
	step.hi = (u16)(hi < PROGRESS_FULL ? hi : PROGRESS_FULL);
	if (!dialog_progress_active())
		return;
	dialog_progress_set(step.lo, PROGRESS_FULL, NULL);
	if (status) {
		step.status = status;
		dialog_progress_status(status);  // drawn now
	}
}

void progress_part(u32 done, u32 total)
{
	dialog_progress_set(progress_permille(step.lo, step.hi, done, total), PROGRESS_FULL, NULL);
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

// --- The cart's copy ---------------------------------------------------------

// What the VBlank interrupt draws during the copy (EWRAM: the IWRAM it
// needs is the code alone)
static struct {
	struct progress_copy copy;
	struct ui_bar_pixels bar;
} hook EWRAM_BSS;

// The VBlank interrupt during the copy: the fill one frame on. In IWRAM,
// as the kernel's wait is, so it runs whatever the cart does with the ROM
// meanwhile; it calls nothing (progress_copy_advance is inlined), and
// extern, as CLAUDE.md asks of IWRAM code.
void IWRAM_CODE progress_copy_vblank(void)
{
	u32 to = progress_copy_advance(&hook.copy);
	for (u32 x = hook.bar.x; x < to; x++) {
		vu32 *p = hook.bar.tiles + (x >> 3) * hook.bar.column_words + hook.bar.top;
		u32 shift = (x & 7) * 4, ink = hook.bar.ink << shift, keep = ~(0xFu << shift);
		for (u32 y = 0; y < hook.bar.rows; y++)
			p[y] = (p[y] & keep) | ink;
	}
	if (to > hook.bar.x)
		hook.bar.x = to;
}

// The VBlank handler libgba's dispatcher has now, or NULL
static IntFn vblank_handler(void)
{
	for (u32 i = 0; i < MAX_INTS && IntrTable[i].mask; i++)
		if (IntrTable[i].mask == IRQ_VBLANK)
			return IntrTable[i].handler;
	return NULL;
}

u32 progress_cart_copy(u32 *fat, u32 bytes, u32 hi)
{
	u32 frames = bytes / PROGRESS_COPY_RATE;
	// The step starts where the last one ends. The fill can't start at
	// column 0: it would square off the bar's rounded end
	dialog_progress_set(step.hi, PROGRESS_FULL, NULL);
	int hooked = dialog_progress_pixels(hi, &hook.bar) && hook.bar.x > 0;
	IntFn before = vblank_handler();  // put back after (none while the engine installs none)
	if (hooked) {
		progress_copy_plan(&hook.copy, hook.bar.x, hook.bar.px, frames);
		irqSet(IRQ_VBLANK, progress_copy_vblank);
	}
	u32 start = torii_frames;
	u32 refused = Send_FATbuffer(fat, 0);
	u32 took = torii_frames - start;
	if (hooked) {
		irqSet(IRQ_VBLANK, before);
		dialog_progress_drew(hook.bar.x);
	}
	step.lo = step.hi = (u16)(hi < PROGRESS_FULL ? hi : PROGRESS_FULL);
	if (refused)
		return 1;
	if (took)
		tlog("launch: the cart copied %lu KB in %lu frames, %lu bytes a frame (the estimate: %lu frames)",
		     (unsigned long)(bytes >> 10), (unsigned long)took, (unsigned long)(bytes / took),
		     (unsigned long)frames);
	else
		tlog("launch: the cart copied %lu KB within a frame (the estimate: %lu frames)",
		     (unsigned long)(bytes >> 10), (unsigned long)frames);
	return 0;
}
