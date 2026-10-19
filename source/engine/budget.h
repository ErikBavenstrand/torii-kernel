// The budget checker: a layout declares what it needs up front, and it is
// checked against the hardware's capacity (budget_capacity, when a layout is
// written: the host tests and T6's layout tests) or against what is free now
// (budget_available, when a view is pushed). A layout over budget is
// rejected with a readable error naming the resource, the need and the room.
//
// Free space counts cached blocks, which would be reclaimed. A layout that
// passes can still fail an allocation if the space is fragmented; res_alloc
// then reports that.
#ifndef TORII_ENGINE_BUDGET_H
#define TORII_ENGINE_BUDGET_H

#include <gba_base.h>

struct engine_budget {
	u16 bg_tiles;  // 4bpp BG tiles (an 8bpp tile counts two)
	u8 bg_maps;  // 2 KB screenblocks; they share BG VRAM with the tiles
	u16 obj_tiles;  // 4bpp sprite tiles (an 8bpp tile counts two)
	u8 bg_banks;  // BG palette banks
	u8 obj_banks;  // sprite palette banks
	u8 affine;  // affine matrices
	u32 psram_bytes;  // PSRAM cache (covers)
};

// What budget_check found
enum {
	BUDGET_OK = 0,
	BUDGET_BG_VRAM,
	BUDGET_OBJ_TILES,
	BUDGET_BG_BANKS,
	BUDGET_OBJ_BANKS,
	BUDGET_AFFINE,
	BUDGET_PSRAM,
};

// PSRAM for the cover cache: COVER_PSRAM_BYTES (engine/covercache.h), what
// the cache uses of its 8 MB page (decisions doc, Covers).
extern u32 budget_psram_bytes;

// The hardware's whole capacity in mode 0.
void budget_capacity(struct engine_budget *out);
// What is free now in the engine's pools (cached blocks counted as free).
void budget_available(struct engine_budget *out);
// BUDGET_OK, or the first resource need exceeds in have; writes a sentence
// saying so to msg (size bytes) unless msg is NULL. Hardware-free.
int budget_check(const struct engine_budget *need, const struct engine_budget *have, char *msg, u32 size);
// Checks layout name's needs against what is free now. On failure it
// records the error for res_error() and logs it; returns budget_check's code.
int budget_check_layout(const char *name, const struct engine_budget *need);

#endif
