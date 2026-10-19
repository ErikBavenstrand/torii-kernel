// See gfx.h. Everything runs from ROM but gfx_commit_queue, the loop that
// feeds the VRAM queue to DMA in VBlank: the rest of the commit is a few
// register writes and DMA starts, so IWRAM wouldn't speed it up.
#include <string.h>

#include "gfx.h"
#include "res.h"

#define IO ((vu16 *)0x04000000)
#define REG_VCOUNT_ (*(vu16 *)0x04000006)
#define PAL_RAM 0x05000000u
#define OAM_RAM 0x07000000u
#define DMA3SAD (*(vu32 *)0x040000D4)
#define DMA3DAD (*(vu32 *)0x040000D8)
#define DMA3CNT (*(vu32 *)0x040000DC)
#define DMA_ON_32 0x84000000u
#define DMA_SRC_FIXED_ 0x01000000u

enum {
	QUEUE_COPY,
	QUEUE_FILL,
};

struct queue_entry {
	u32 src;  // QUEUE_COPY: the source address; QUEUE_FILL: the word to fill with
	u32 dst;
	u16 bytes;  // left to do
	u16 kind;
};

struct gfx_regs gfx_regs EWRAM_BSS;
u16 gfx_live_dispcnt EWRAM_BSS;
u16 gfx_live_bgofs[8] EWRAM_BSS;
u32 gfx_dirty_regs;
u32 gfx_hold_regs;
static u8 gfx_frozen;
u32 gfx_dirty_pal;
u16 gfx_pal_unfaded[512] EWRAM_BSS ALIGN(4);
u16 gfx_pal_faded[512] EWRAM_BSS ALIGN(4);
u8 gfx_pal_faded_on;
struct gfx_obj gfx_oam[GFX_OAM_ENTRIES] EWRAM_BSS ALIGN(4);
u8 gfx_oam_building;
u32 gfx_commit_max_bytes = GFX_COMMIT_BYTES;
u32 gfx_commit_last_line = GFX_COMMIT_LAST_LINE;
u32 gfx_commit_bytes;
u32 gfx_commit_left;

static struct queue_entry queue[GFX_QUEUE_CAP] EWRAM_BSS;
static u8 queue_head, queue_count;
static u32 queue_pushed EWRAM_BSS;  // tickets handed out
static u8 oam_count, oam_shown, oam_changed, oam_full_logged;

void gfx_init(void)
{
	memset(&gfx_regs, 0, sizeof(gfx_regs));
	gfx_regs.dispcnt = 0x0080;  // forced blank
	gfx_live_dispcnt = 0x0080;
	memset(gfx_live_bgofs, 0, sizeof(gfx_live_bgofs));
	memset(gfx_pal_unfaded, 0, sizeof(gfx_pal_unfaded));
	memset(gfx_pal_faded, 0, sizeof(gfx_pal_faded));
	gfx_pal_faded_on = 0;
	for (u32 i = 0; i < GFX_OAM_ENTRIES; i++) {
		gfx_oam[i].attr0 = GFX_OBJ_HIDDEN;
		gfx_oam[i].attr1 = gfx_oam[i].attr2 = 0;
		// identity matrices
		gfx_oam[i].affine = (i & 3) == 0 || (i & 3) == 3 ? 0x100 : 0;
	}
	gfx_oam_building = 0;
	oam_count = oam_shown = oam_changed = oam_full_logged = 0;
	queue_head = queue_count = 0;
	gfx_commit_max_bytes = GFX_COMMIT_BYTES;
	gfx_commit_last_line = GFX_COMMIT_LAST_LINE;
	gfx_commit_bytes = gfx_commit_left = 0;
	gfx_dirty_regs = GFX_DIRTY_REGS | GFX_DIRTY_OAM;
	gfx_hold_regs = 0;
	gfx_dirty_pal = 0xFFFFFFFF;
}

u32 gfx_pending(void)
{
	return gfx_dirty_regs | gfx_dirty_pal | queue_count;
}

static void set_reg(u16 *reg, u16 value, u32 dirty)
{
	if (*reg != value) {
		*reg = value;
		gfx_dirty_regs |= dirty;
	}
}

void gfx_set_dispcnt(u16 value)
{
	set_reg(&gfx_regs.dispcnt, value, GFX_DIRTY_DISPCNT);
}

void gfx_set_bgcnt(u32 bg, u16 value)
{
	set_reg(&gfx_regs.bgcnt[bg & 3], value, GFX_DIRTY_BGCNT);
}

void gfx_set_scroll(u32 bg, u16 x, u16 y)
{
	set_reg(&gfx_regs.bgofs[(bg & 3) * 2], x & 0x1FF, GFX_DIRTY_BGOFS);
	set_reg(&gfx_regs.bgofs[(bg & 3) * 2 + 1], y & 0x1FF, GFX_DIRTY_BGOFS);
}

void gfx_set_win(u32 index, u16 value)
{
	if (index < 6)
		set_reg(&gfx_regs.win[index], value, GFX_DIRTY_WIN);
}

void gfx_set_mosaic(u16 value)
{
	set_reg(&gfx_regs.mosaic, value, GFX_DIRTY_MOSAIC);
}

void gfx_set_blend(u16 bldcnt, u16 bldalpha, u16 bldy)
{
	set_reg(&gfx_regs.blend[0], bldcnt, GFX_DIRTY_BLEND);
	set_reg(&gfx_regs.blend[1], bldalpha, GFX_DIRTY_BLEND);
	set_reg(&gfx_regs.blend[2], bldy, GFX_DIRTY_BLEND);
}

void gfx_pal_load(u32 bank, const u16 *colours, u32 count)
{
	u32 first = bank * 16;
	if (first >= 512 || !count)
		return;
	if (count > 512 - first)
		count = 512 - first;
	memcpy(&gfx_pal_unfaded[first], colours, count * 2);
	u32 last = (first + count - 1) / 16;
	for (u32 b = bank; b <= last; b++)
		gfx_dirty_pal |= 1u << b;
}

void gfx_pal_mark(u32 banks)
{
	gfx_dirty_pal |= banks;
}

void gfx_pal_use_faded(int on)
{
	if (gfx_pal_faded_on != !!on) {
		gfx_pal_faded_on = !!on;
		gfx_dirty_pal = 0xFFFFFFFF;
	}
}

void gfx_oam_begin(void)
{
	gfx_oam_building = 1;
	oam_count = 0;
	oam_changed = 0;
}

u8 gfx_oam_add(u16 attr0, u16 attr1, u16 attr2)
{
	if (oam_count == GFX_OAM_ENTRIES) {
		if (!oam_full_logged)
			res_set_error("OAM full: more than %d sprites in one frame", GFX_OAM_ENTRIES);
		oam_full_logged = 1;
		return GFX_OAM_NONE;
	}
	struct gfx_obj *o = &gfx_oam[oam_count];
	if (o->attr0 != attr0 || o->attr1 != attr1 || o->attr2 != attr2) {
		o->attr0 = attr0;
		o->attr1 = attr1;
		o->attr2 = attr2;
		oam_changed = 1;
	}
	return oam_count++;
}

void gfx_oam_end(void)
{
	// Hide what the last build showed and this one didn't
	for (u32 i = oam_count; i < oam_shown; i++) {
		gfx_oam[i].attr0 = GFX_OBJ_HIDDEN;
		oam_changed = 1;
	}
	oam_shown = oam_count;
	if (oam_count < GFX_OAM_ENTRIES)
		oam_full_logged = 0;
	if (oam_changed)
		gfx_dirty_regs |= GFX_DIRTY_OAM;
	gfx_oam_building = 0;
}

void gfx_affine_set(u32 matrix, s16 pa, s16 pb, s16 pc, s16 pd)
{
	if (matrix >= GFX_AFFINE_MATS)
		return;
	struct gfx_obj *o = &gfx_oam[matrix * 4];
	if (o[0].affine == pa && o[1].affine == pb && o[2].affine == pc && o[3].affine == pd)
		return;
	o[0].affine = pa;
	o[1].affine = pb;
	o[2].affine = pc;
	o[3].affine = pd;
	gfx_dirty_regs |= GFX_DIRTY_OAM;
}

static int queue_push(u32 dst, u32 src, u32 bytes, u16 kind)
{
	// Palette, VRAM and OAM only: a stray address must never reach the cart
	if ((dst | bytes | (kind == QUEUE_COPY ? src : 0)) & 3 || !bytes || bytes > 0xFFFF || dst < 0x05000000 ||
	    bytes > 0x07000400 - dst) {
		res_set_error("bad VRAM copy: %08lX from %08lX, %lu bytes (words, under 64 KB, into video memory)",
			      (unsigned long)dst, (unsigned long)src, (unsigned long)bytes);
		return 0;
	}
	if (queue_count == GFX_QUEUE_CAP) {
		res_set_error("VRAM queue full (%d copies waiting)", GFX_QUEUE_CAP);
		return 0;
	}
	struct queue_entry *e = &queue[(queue_head + queue_count) & (GFX_QUEUE_CAP - 1)];
	e->src = src;
	e->dst = dst;
	e->bytes = (u16)bytes;
	e->kind = kind;
	queue_count++;
	queue_pushed++;
	return 1;
}

int gfx_queue_copy(void *dst, const void *src, u32 bytes)
{
	return queue_push((u32)dst, (u32)src, bytes, QUEUE_COPY);
}

int gfx_queue_fill(void *dst, u32 value, u32 bytes)
{
	return queue_push((u32)dst, value, bytes, QUEUE_FILL);
}

u32 gfx_queue_len(void)
{
	return queue_count;
}

u32 gfx_queue_ticket(void)
{
	return queue_pushed;
}

int gfx_queue_done(u32 ticket)
{
	// Entries finish in order: all but the last queue_count tickets are done
	return (s32)(queue_pushed - queue_count - ticket) >= 0;
}

void gfx_hold_for_queue(u32 regions)
{
	gfx_hold_regs |= regions & GFX_DIRTY_REGS;
}

void gfx_queue_clear(void)
{
	queue_head = queue_count = 0;
}

u32 IWRAM_CODE gfx_commit_queue(u32 byte_cap, u32 last_line)
{
	u32 done = 0;
	while (queue_count) {
		u32 line = REG_VCOUNT_;
		if (line < 160 || line >= last_line)
			break;
		struct queue_entry *e = &queue[queue_head];
		u32 n = e->bytes;
		if (n > GFX_CHUNK)
			n = GFX_CHUNK;
		if (n > byte_cap - done)
			n = (byte_cap - done) & ~3u;
		if (!n)
			break;
		if (e->kind == QUEUE_FILL) {
			DMA3SAD = (u32)&e->src;
			DMA3DAD = e->dst;
			DMA3CNT = DMA_ON_32 | DMA_SRC_FIXED_ | n / 4;
		} else {
			DMA3SAD = e->src;
			DMA3DAD = e->dst;
			DMA3CNT = DMA_ON_32 | n / 4;
			e->src += n;
		}
		e->dst += n;
		e->bytes -= (u16)n;
		done += n;
		if (!e->bytes) {
			queue_head = (queue_head + 1) & (GFX_QUEUE_CAP - 1);
			queue_count--;
		}
	}
	return done;
}

static void write_regs(u32 dirty)
{
	vu16 *io = IO;
	if (dirty & GFX_DIRTY_DISPCNT)
		io[0] = gfx_live_dispcnt = gfx_regs.dispcnt;
	if (dirty & GFX_DIRTY_BGCNT)
		for (u32 i = 0; i < 4; i++)
			io[4 + i] = gfx_regs.bgcnt[i];
	if (dirty & GFX_DIRTY_BGOFS)
		for (u32 i = 0; i < 8; i++)
			io[8 + i] = gfx_live_bgofs[i] = gfx_regs.bgofs[i];
	if (dirty & GFX_DIRTY_WIN)
		for (u32 i = 0; i < 6; i++)
			io[0x20 + i] = gfx_regs.win[i];
	if (dirty & GFX_DIRTY_MOSAIC)
		io[0x26] = gfx_regs.mosaic;
	if (dirty & GFX_DIRTY_BLEND)
		for (u32 i = 0; i < 3; i++)
			io[0x28 + i] = gfx_regs.blend[i];
}

void gfx_freeze(int on)
{
	gfx_frozen = (u8)(on != 0);
}

void gfx_commit(void)
{
	if (gfx_frozen) {
		// The screen shows what it showed: only the queue runs, and what
		// changed stays marked for the commit after the freeze
		gfx_commit_bytes = gfx_commit_queue(gfx_commit_max_bytes, gfx_commit_last_line);
		gfx_commit_left = 0;
		for (u32 i = 0; i < queue_count; i++)
			gfx_commit_left += queue[(queue_head + i) & (GFX_QUEUE_CAP - 1)].bytes;
		return;
	}
	u32 dirty = gfx_dirty_regs;
	// Regions held for the queue wait while it has work
	u32 held = queue_count ? dirty & gfx_hold_regs & GFX_DIRTY_REGS : 0;
	write_regs(dirty & ~held);
	gfx_hold_regs &= ~(dirty & ~held);

	// Palettes: one DMA per run of dirty banks
	u32 banks = gfx_dirty_pal;
	const u16 *pal = gfx_pal_faded_on ? gfx_pal_faded : gfx_pal_unfaded;
	for (u32 bank = 0; banks;) {
		if (!(banks & 1)) {
			banks >>= 1;
			bank++;
			continue;
		}
		u32 run = 0;
		while (banks & 1) {
			banks >>= 1;
			run++;
		}
		DMA3SAD = (u32)&pal[bank * 16];
		DMA3DAD = PAL_RAM + bank * 32;
		DMA3CNT = DMA_ON_32 | run * 8;
		bank += run;
	}
	gfx_dirty_pal = 0;

	if ((dirty & GFX_DIRTY_OAM) && !gfx_oam_building) {
		DMA3SAD = (u32)gfx_oam;
		DMA3DAD = OAM_RAM;
		DMA3CNT = DMA_ON_32 | sizeof(gfx_oam) / 4;
		dirty &= ~(u32)GFX_DIRTY_OAM;
	}
	dirty &= GFX_DIRTY_OAM;

	gfx_commit_bytes = gfx_commit_queue(gfx_commit_max_bytes, gfx_commit_last_line);
	if (held && !queue_count) {
		// The queue finished: the held regions go now if VBlank lasts
		u32 line = REG_VCOUNT_;
		if (line >= 160 && line < 227) {
			write_regs(held);
			gfx_hold_regs &= ~held;
			held = 0;
		}
	}
	gfx_dirty_regs = dirty | held;
	u32 left = 0;
	for (u32 i = 0; i < queue_count; i++)
		left += queue[(queue_head + i) & (GFX_QUEUE_CAP - 1)].bytes;
	gfx_commit_left = left;
}
