// Shadow hardware state and the VBlank commit.
//
// Draw code never writes video registers, palettes or OAM: it writes the
// shadow copies here, through setters that mark what changed, and queues
// VRAM copies. frame_begin (engine/frame.h) calls gfx_commit in VBlank,
// which copies what changed and then runs the queue within its caps.
//
// What is shadowed:
// - registers: DISPCNT, BG control and scroll, windows, mosaic, blending;
// - palettes: two buffers of all 512 colours (BG then sprites). Themes and
//   assets write the unfaded one; a fade (T5) writes the faded one from it
//   each frame and turns gfx_pal_use_faded on. The commit copies the banks
//   marked dirty from whichever is in use;
// - OAM: 128 entries, rebuilt between gfx_oam_begin and gfx_oam_end. The
//   commit skips OAM while a build is open, so a half-built OAM never shows;
//   gfx_oam_end hides every entry the build didn't fill. Affine matrices
//   live in the entries' fourth halfwords, as in hardware, and are kept
//   across builds.
//
// The VRAM queue holds copies and fills of whole words. Each frame's commit
// moves at most gfx_commit_max_bytes and starts no copy at or past VCOUNT
// gfx_commit_last_line, in chunks of GFX_CHUNK bytes; what is left waits for
// the next frame. A queued copy's source must stay unchanged until it is
// done (gfx_queue_len reaching 0, or the next frame for a short queue).
#ifndef TORII_ENGINE_GFX_H
#define TORII_ENGINE_GFX_H

#include <gba_base.h>

// Shadowed registers, in hardware order within each group
struct gfx_regs {
	u16 dispcnt;
	u16 bgcnt[4];
	u16 bgofs[8];  // BG0HOFS, BG0VOFS, BG1HOFS, ...
	u16 win[6];  // WIN0H, WIN1H, WIN0V, WIN1V, WININ, WINOUT
	u16 mosaic;
	u16 blend[3];  // BLDCNT, BLDALPHA, BLDY
};

// Which shadow regions changed since the last commit
enum {
	GFX_DIRTY_DISPCNT = 1 << 0,
	GFX_DIRTY_BGCNT = 1 << 1,
	GFX_DIRTY_BGOFS = 1 << 2,
	GFX_DIRTY_WIN = 1 << 3,
	GFX_DIRTY_MOSAIC = 1 << 4,
	GFX_DIRTY_BLEND = 1 << 5,
	GFX_DIRTY_OAM = 1 << 6,
	GFX_DIRTY_REGS = 0x3F,
};

struct gfx_obj {
	u16 attr0, attr1, attr2;
	s16 affine;  // a quarter of an affine matrix (pa, pb, pc or pd)
};

#define GFX_OAM_ENTRIES 128
#define GFX_AFFINE_MATS 32
#define GFX_OAM_NONE 0xFF
#define GFX_OBJ_HIDDEN 0x0200  // attr0: not shown (and not affine)

#define GFX_QUEUE_CAP 16  // a power of two
#define GFX_CHUNK 1024
#define GFX_COMMIT_BYTES 16384
// The VBlank lines are 160 to 227. A 1 KB chunk from EWRAM takes about 2
// lines, so the last one starts by line 224 and ends before line 0.
#define GFX_COMMIT_LAST_LINE 225

extern struct gfx_regs gfx_regs;  // read freely; write through the setters
extern u32 gfx_dirty_regs;  // GFX_DIRTY_* bits
extern u32 gfx_hold_regs;  // regions gfx_hold_for_queue holds back
extern u32 gfx_dirty_pal;  // bit n: palette bank n (0-15 BG, 16-31 sprites)
extern u16 gfx_pal_unfaded[512];
extern u16 gfx_pal_faded[512];
extern u8 gfx_pal_faded_on;
extern struct gfx_obj gfx_oam[GFX_OAM_ENTRIES];
extern u8 gfx_oam_building;
extern u32 gfx_commit_max_bytes;
extern u32 gfx_commit_last_line;
// The last commit's queue bytes, and the bytes it left for later (Diagnostics)
extern u32 gfx_commit_bytes;
extern u32 gfx_commit_left;

// Resets the shadow state: forced blank, everything else 0, every OAM entry
// hidden, the queue empty, everything marked dirty.
void gfx_init(void);
// Nonzero if the next commit has anything to do.
u32 gfx_pending(void);

void gfx_set_dispcnt(u16 value);
void gfx_set_bgcnt(u32 bg, u16 value);
void gfx_set_scroll(u32 bg, u16 x, u16 y);
// index: 0 WIN0H, 1 WIN1H, 2 WIN0V, 3 WIN1V, 4 WININ, 5 WINOUT
void gfx_set_win(u32 index, u16 value);
void gfx_set_mosaic(u16 value);
void gfx_set_blend(u16 bldcnt, u16 bldalpha, u16 bldy);

// Copies colours into the unfaded buffer from colour index bank * 16 (bank
// 0-31), and marks the banks they cover dirty. The faded buffer is the fade's.
void gfx_pal_load(u32 bank, const u16 *colours, u32 count);
// Marks banks dirty (bit n: bank n), after writing a buffer directly.
void gfx_pal_mark(u32 banks);
// Commits from the faded buffer (on) or the unfaded one (off); marks all dirty.
void gfx_pal_use_faded(int on);

void gfx_oam_begin(void);
// The next entry, or GFX_OAM_NONE if all 128 are taken (logged once per build).
u8 gfx_oam_add(u16 attr0, u16 attr1, u16 attr2);
void gfx_oam_end(void);
void gfx_affine_set(u32 matrix, s16 pa, s16 pb, s16 pc, s16 pd);

// Queues a copy or a fill of bytes (a multiple of 4, word-aligned addresses)
// into palette RAM, VRAM or OAM, for VBlank. 0 if the queue is full or the
// arguments are bad (logged). The source is read at the commit, so it must
// not be a stack buffer or anything changed before then.
int gfx_queue_copy(void *dst, const void *src, u32 bytes);
int gfx_queue_fill(void *dst, u32 value, u32 bytes);
u32 gfx_queue_len(void);
// Holds the register regions given (GFX_DIRTY_* bits) until the VRAM queued
// so far has been copied: a commit writes them only once the queue is empty
// (in the same VBlank if it finishes in time, else the next one). For page
// flips: queue the hidden page's tiles and map, set the new BG control or
// scroll, then hold it, and the flip never shows half a page. The hold ends
// once the regions are written.
void gfx_hold_for_queue(u32 regions);
// Drops everything queued (when the sources are about to go away). Tagged
// VRAM blocks whose upload was queued then hold garbage: release them, and
// keep every VRAM write to engine blocks in the queue so writes keep their
// order.
void gfx_queue_clear(void);

// The commit. Call it in VBlank, as frame_begin does: registers, palettes,
// OAM, then the queue. It needs the kernel's ROM mapped (as all menu code).
void gfx_commit(void);
// Runs queued work until done, byte_cap bytes, or VCOUNT outside
// [160, last_line); returns the bytes moved. In IWRAM: the commit's hot loop.
u32 IWRAM_CODE gfx_commit_queue(u32 byte_cap, u32 last_line);

#endif
