// Shadow hardware state and the VBlank commit.
//
// Draw code never writes video registers, palettes or OAM: it writes the
// shadow copies here, through setters that mark what changed, and queues
// VRAM copies. frame_begin (engine/frame.h) calls gfx_commit in VBlank,
// which copies what changed and then runs the queue within its caps.
//
// What is shadowed:
// - registers: DISPCNT, BG control and scroll, windows, blending;
// - palettes: two buffers of all 512 colours (BG then sprites). Themes and
//   assets write the unfaded one; a fade (T5) writes the faded one from it
//   each frame and turns gfx_pal_use_faded on. The commit copies the banks
//   marked dirty from whichever is in use;
// - a raster split (gfx_set_split): values that change at one line;
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
	u16 blend[3];  // BLDCNT, BLDALPHA, BLDY
};

// Which shadow regions changed since the last commit
enum {
	GFX_DIRTY_DISPCNT = 1 << 0,
	GFX_DIRTY_BGCNT = 1 << 1,
	GFX_DIRTY_BGOFS = 1 << 2,
	GFX_DIRTY_WIN = 1 << 3,
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
// What the hardware shows: DISPCNT and the BG scrolls as the last commit
// wrote them (checks that nothing draws where the screen shows it)
extern u16 gfx_live_dispcnt;
extern u16 gfx_live_bgofs[8];
extern u32 gfx_dirty_regs;  // GFX_DIRTY_* bits
extern u32 gfx_hold_regs;  // regions gfx_hold_for_queue holds back
extern u32 gfx_dirty_pal;  // bit n: palette bank n (0-15 BG, 16-31 sprites)
// Banks gfx_pal_load has written since gfx_init: the only ones a fade
// needs to work out (the rest stay black in both buffers)
extern u32 gfx_pal_used;
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
void gfx_set_blend(u16 bldcnt, u16 bldalpha, u16 bldy);

// Copies colours into the unfaded buffer from colour index bank * 16 (bank
// 0-31), and marks the banks they cover dirty and used. The faded buffer is
// the fade's.
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
// Queues count runs of run bytes (words, at most GFX_CHUNK), the nth from
// src + n * src_stride to dst + n * dst_stride: one entry for a list row's
// columns, which lie apart in VRAM. 0 if the queue is full or the arguments
// are bad (logged).
int gfx_queue_copy2d(void *dst, u32 dst_stride, const void *src, u32 src_stride, u32 run, u32 count);
u32 gfx_queue_len(void);
// Tickets: each queued copy or fill counts one, and they finish in order.
// gfx_queue_ticket() is the latest one's; gfx_queue_done(t) is 1 once that
// copy (and every one before it) has been made, or dropped by
// gfx_queue_clear. Streamers use them to know when an upload is complete.
u32 gfx_queue_ticket(void);
int gfx_queue_done(u32 ticket);
// Holds the register regions given (GFX_DIRTY_* bits) until the VRAM queued
// so far has been copied: a commit writes them only once the queue is empty
// (in the same VBlank if it finishes in time, else the next one). For page
// flips: queue the hidden page's tiles and map, set the new BG control or
// scroll, then hold it, and the flip never shows half a page. The hold ends
// once the regions are written.
void gfx_hold_for_queue(u32 regions);
// While frozen, commits write no register, palette or OAM: the screen keeps
// what the last commit (or anything before the engine) put there, and only
// the VRAM queue runs. What changed meanwhile goes at the first commit after
// gfx_freeze(0). gfx_init leaves the freeze as it is. For a screen drawn
// again just as it was in VRAM the screen shows (Diagnostics after its unit
// tests): the page stays up while its new session draws it.
void gfx_freeze(int on);
// Drops everything queued (when the sources are about to go away). Tagged
// VRAM blocks whose upload was queued then hold garbage: release them, and
// keep every VRAM write to engine blocks in the queue so writes keep their
// order.
void gfx_queue_clear(void);

// --- Raster split -------------------------------------------------------------
// From one line down the screen shows other values in palette RAM or an I/O
// register: the theme wipe's old colours under its edge, the open's title
// bar slid sideways over its bottom line (B19, research overrides R7 and R8).
// Each VBlank the commit copies every part's top values (what the lines
// above the split show) into place and arms a VCOUNT interrupt on the line
// before the split; its handler, in IWRAM, starts DMA0 and DMA1 in HBlank
// mode, which copy the parts' split values in that line's HBlank, so the
// split line and every line under it show them. The split values come from
// gfx_split_data in IWRAM: a palette run from EWRAM wouldn't fit one HBlank.
// mGBA draws each line whole at HBlank's start, so goldens show the split on
// the line the cart does; a copy running late (a long DMA3 holding the
// interrupt back) shows only on a cart.
#define GFX_SPLIT_PARTS 2  // DMA0 and DMA1
#define GFX_SPLIT_WORDS 32  // a palette run of 48 BG colours and an OBJ bank
struct gfx_split_part {
	u32 dst;          // palette RAM or a video register; 0: the part is unused
	const void *top;  // the values above the split (copied in VBlank); a run of
			  // gfx_pal_unfaded is read from gfx_pal_faded while a fade is on
	u16 offset;       // the split values: gfx_split_data from this word
	u16 count;        // words, or halfwords with half
	u8 half;
};
struct gfx_split {
	u8 line;  // the first line showing the split values, 1-159; 0: no split
	struct gfx_split_part part[GFX_SPLIT_PARTS];
};
// In IWRAM: the split values. Written straight away, not at the commit, so
// change them only while no split uses them (before setting one).
extern u32 gfx_split_data[GFX_SPLIT_WORDS];
// Sets the split from the next commit on (NULL or line 0: none), as the
// registers are shadowed. gfx_init, gfx_split_off and gfx_freeze end it.
// It needs a commit every VBlank (ui_frame's): a frame that misses one
// shows the split values from the top too, so nothing may block while a
// split runs.
void gfx_set_split(const struct gfx_split *split);
// Ends a split at once: the interrupt and both DMAs off (before a game, or
// ui_shutdown)
void gfx_split_off(void);
// The VCOUNT handler (libgba's dispatcher calls it): arms the DMAs
void IWRAM_CODE gfx_split_irq(void);

// The commit. Call it in VBlank, as frame_begin does: registers, palettes,
// the split, OAM, then the queue. It needs the kernel's ROM mapped (as all
// menu code).
void gfx_commit(void);
// Runs queued work until done, byte_cap bytes, or VCOUNT outside
// [160, last_line); returns the bytes moved. In IWRAM: the commit's hot
// loop. It stops at a strided copy (gfx_queue_copy2d), which
// gfx_commit_vram runs from ROM.
u32 IWRAM_CODE gfx_commit_queue(u32 byte_cap, u32 last_line);
// The whole queue as gfx_commit_queue runs it, strided copies included (a
// run at a time, whole runs only).
u32 gfx_commit_vram(u32 byte_cap, u32 last_line);

#endif
