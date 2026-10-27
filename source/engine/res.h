// The engine's video resources, each a pool (engine/pool.h): BG VRAM (tiles
// and maps), sprite tiles, BG and sprite palette banks, and affine matrices.
// Hardware-free apart from the addresses it hands out.
//
// Ownership is an arena: every allocation is logged, res_mark() returns the
// log's depth, and res_release_to(mark) drops every reference taken since,
// newest first. A view marks when it is pushed and releases to its mark when
// it is popped (T6), so a view owns what it allocated. Tagged blocks it drops
// stay cached, so pushing the same view again finds them still loaded.
//
// Every failure logs a readable error (tlog_error) and keeps it for
// res_error(), for an error screen.
#ifndef TORII_ENGINE_RES_H
#define TORII_ENGINE_RES_H

#include <gba_base.h>
#include "pool.h"

enum res_kind {
	RES_BG_VRAM,  // 64 KB in 32-byte units (a 4bpp tile): tiles and maps
	RES_OBJ_VRAM,  // 32 KB in 32-byte units: sprite tiles (mode 0, 1D mapping)
	RES_BG_PAL,  // 16 banks of 16 colours
	RES_OBJ_PAL,  // 16 banks of 16 colours
	RES_AFFINE,  // 32 affine matrices
	RES_KINDS
};

// Units in each pool, and block slots (the most blocks it can be split into)
#define RES_BG_UNITS 2048
#define RES_OBJ_UNITS 1024
#define RES_PAL_BANKS 16
#define RES_AFFINE_MATS 32
#define RES_BG_BLOCKS 40  // a Paper clean page: a dozen widgets, two tile sets each
#define RES_OBJ_BLOCKS 24
// The allocation log's capacity: references held at once through res_*
#define RES_LOG_CAP 64

#define RES_NONE POOL_NONE
#define RES_UNIT_BYTES 32
#define RES_MAP_UNITS 64  // a 2 KB screenblock

// Sets up every pool empty and clears the log and the error.
void res_init(void);

// Allocates len units of kind at a multiple of align inside [lo, hi) units,
// sharing or reviving a block with the same tag (0: never shared). flags is
// POOL_TOP, POOL_KEEP or both, or 0. Returns a handle, or
// RES_NONE after logging why.
u8 res_alloc(enum res_kind kind, u16 len, u16 align, u16 lo, u16 hi, u16 tag, u8 flags);
// Drops one reference to handle. Its newest log entry becomes a gap, so a
// later res_release_to doesn't drop it again and the marks views hold stay
// right. 0 if handle holds no reference taken through res_alloc.
int res_release(enum res_kind kind, u8 handle);
// 1 if the last res_alloc of kind placed a new block, whose contents must be
// uploaded; 0 if it shared a used block or revived a cached one (still
// loaded), or failed.
int res_fresh(enum res_kind kind);
// The log's depth now: what res_release_to(mark) returns to.
u8 res_mark(void);
// Drops every reference taken since mark, newest first.
void res_release_to(u8 mark);

// The pool behind kind, for queries (pool_free_units and the like).
struct pool *res_pool(enum res_kind kind);
static inline u16 res_start(enum res_kind kind, u8 handle)
{
	return pool_start(res_pool(kind), handle);
}

// The last error, as a sentence; "" if none since res_init.
const char *res_error(void);
// Records an engine error for res_error() and logs it ("engine: ...");
// every engine module reports through it.
void res_set_error(const char *format, ...) __attribute__((format(printf, 1, 2)));
// Nonzero: errors are recorded but not logged (tests of the caps, so
// Diagnostics' log on the card shows no made-up errors).
extern u8 res_errors_quiet;

// --- Helpers for each kind ---

// count 4bpp tiles (or 8bpp ones, as two units each) reachable from BG
// character base charblock (0-3): a BG's tile numbers reach 1024 tiles past
// its base. Returns a handle; res_bg_tile_index gives the first tile's number.
u8 res_bg_tiles(u16 count, int bpp8, u8 charblock, u16 tag);
u16 res_bg_tile_index(u8 handle, int bpp8, u8 charblock);
// A BG map of blocks screenblocks (1 for 32x32, 2 for 64x32 or 32x64, 4 for
// 64x64), taken from the top of VRAM so tiles and maps don't fragment each
// other. res_bg_screenblock gives its number (BG_MAP_BASE).
u8 res_bg_map(u8 blocks, u16 tag);
u8 res_bg_screenblock(u8 handle);
// Address of a BG VRAM block
void *res_bg_addr(u8 handle);

// count sprite tiles (4bpp; 8bpp ones count two and start even). The
// handle's start is the tile number for OAM attribute 2.
u8 res_obj_tiles(u16 count, int bpp8, u16 tag);
void *res_obj_addr(u8 handle);

// banks consecutive palette banks (16 for a 256-colour palette), shared by
// tag (as pokeemerald's tagged palettes). The handle's start is the bank.
u8 res_palette(enum res_kind kind, u8 banks, u16 tag);

// An affine matrix, shared by tag. The handle's start is its number.
u8 res_affine(u16 tag);

#endif
