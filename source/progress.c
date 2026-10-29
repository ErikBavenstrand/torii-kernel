// See progress.h.
#include <gba_interrupt.h>
#include <string.h>

#include "Ezcard_OP.h"
#include "progress.h"
#include "lang.h"
#include "tlog.h"
#include "engine/text.h"
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

// A glyph of the percentage, copied out of the font (in ROM, which the
// interrupt can't read during the copy)
#define PERCENT_ROWS 12
#define PERCENT_AFFIX 4  // glyphs before and after the number ("{1} %")
struct percent_glyph {
	u16 rows[PERCENT_ROWS];  // bit 0 leftmost
	u8 advance, width, top, count;  // count: bitmap rows
};

// What the VBlank interrupt draws during the copy (EWRAM: the IWRAM it
// needs is the code alone)
static struct {
	struct progress_copy copy;
	struct ui_bar_pixels bar;
	struct ui_label_pixels label;  // the percentage's
	struct percent_glyph digit[10], before[PERCENT_AFFIX], after[PERCENT_AFFIX];
	u32 n_before, n_after;
	u32 scale;    // column * scale >> 16: the column's percentage of the bar
	u32 percent;  // the one shown
	u32 painting;  // the percentage is drawn too
} hook EWRAM_BSS;

static void percent_glyph_load(struct percent_glyph *g, const struct torii_font *f, int index)
{
	memset(g, 0, sizeof(*g));
	if (index < 0)
		return;
	const struct torii_glyph *src = &f->glyphs[index];
	g->advance = src->advance;
	g->width = (u8)(src->width < 16 ? src->width : 16);
	g->top = src->top;
	g->count = (u8)(src->rows < PERCENT_ROWS ? src->rows : PERCENT_ROWS);
	for (u32 r = 0; r < g->count; r++) {
		const u8 *b = f->bits + src->offset + r * f->row_bytes;
		g->rows[r] = (u16)(b[0] | (f->row_bytes > 1 ? b[1] << 8 : 0));
	}
}

// The glyphs of s[0, len) into out, at most PERCENT_AFFIX; how many
static u32 percent_affix(struct percent_glyph *out, const struct torii_font *f, const char *s, u32 len)
{
	u32 n = 0, pos = 0;
	while (pos < len && n < PERCENT_AFFIX) {
		int g = text_next_glyph(s, len, &pos);
		if (g >= 0)
			percent_glyph_load(&out[n++], f, g);
	}
	return n;
}

// Readies the percentage for the interrupt: its label's pixels and the
// glyphs of STR_FORMAT_PERCENT (the label's font, ui_label_pixels's look).
// 0 if it can't be drawn from there.
static int percent_hook(void)
{
	const char *t = S(STR_FORMAT_PERCENT), *at = strstr(t, "{1}");
	if (!at || !hook.bar.width || !dialog_percent_pixels(&hook.label, &hook.percent))
		return 0;
	const struct torii_font *f = ui_theme()->body;
	hook.n_before = percent_affix(hook.before, f, t, (u32)(at - t));
	hook.n_after = percent_affix(hook.after, f, at + 3, (u32)strlen(at + 3));
	for (u32 d = 0; d < 10; d++) {
		char c = (char)('0' + d);
		u32 pos = 0;
		percent_glyph_load(&hook.digit[d], f, text_next_glyph(&c, 1, &pos));
	}
	hook.scale = ((100u << 16) + hook.bar.width - 1) / hook.bar.width;
	return 1;
}

// Draws percent into the percentage's label as the label itself would
// (right-aligned, ui_label_pixels): in IWRAM for the interrupt, with no
// division (libgcc's is in ROM); extern, as CLAUDE.md asks of IWRAM code.
void IWRAM_CODE progress_percent_paint(u32 percent)
{
	const struct percent_glyph *g[2 * PERCENT_AFFIX + 3];
	u32 n = 0;
	for (u32 i = 0; i < hook.n_before; i++)
		g[n++] = &hook.before[i];
	u32 hundreds = percent >= 100, rest = percent - 100 * hundreds;
	u32 tens = rest * 205 >> 11;  // rest / 10, for rest under 1029
	if (hundreds)
		g[n++] = &hook.digit[1];
	if (hundreds || tens)
		g[n++] = &hook.digit[tens];
	g[n++] = &hook.digit[rest - tens * 10];
	for (u32 i = 0; i < hook.n_after; i++)
		g[n++] = &hook.after[i];
	// The text's extent: from the pen to the last inked column (text_layout's)
	u32 pen = 0, extent = 0;
	for (u32 i = 0; i < n; i++) {
		if (g[i]->count && pen + g[i]->width > extent)
			extent = pen + g[i]->width;
		pen += g[i]->advance;
	}
	const struct ui_label_pixels *l = &hook.label;
	u32 paper = l->paper * 0x11111111u, words = l->w / 8 * l->column_words;
	for (u32 i = 0; i < words; i++)
		l->tiles[i] = paper;
	u32 x = l->right > extent ? l->right - extent : 0;
	for (u32 i = 0; i < n; i++) {
		for (u32 r = 0; r < g[i]->count; r++) {
			u32 y = l->top + g[i]->top + r, bits = g[i]->rows[r];
			for (u32 px = x; bits && px < l->w && y < l->h; px++, bits >>= 1)
				if (bits & 1) {
					vu32 *p = l->tiles + (px >> 3) * l->column_words + y;
					u32 shift = (px & 7) * 4;
					*p = (*p & ~(0xFu << shift)) | l->ink << shift;
				}
		}
		x += g[i]->advance;
	}
}

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
	if (hook.painting) {
		u32 percent = hook.bar.x * hook.scale >> 16;
		if (percent > 100)
			percent = 100;
		if (percent > hook.percent) {  // never back: the label shows the step's own
			hook.percent = percent;
			progress_percent_paint(percent);
		}
	}
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
		hook.painting = (u32)percent_hook();
		progress_copy_plan(&hook.copy, hook.bar.x, hook.bar.px, frames);
		irqSet(IRQ_VBLANK, progress_copy_vblank);
	}
	u32 start = torii_frames;
	u32 refused = Send_FATbuffer(fat, 0);
	u32 took = torii_frames - start;
	if (hooked) {
		irqSet(IRQ_VBLANK, before);
		dialog_progress_drew(hook.bar.x);
		if (hook.painting)
			dialog_percent_drew(hook.percent);
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
