// See budget.h.
#include <stdio.h>

#include "budget.h"
#include "covercache.h"
#include "res.h"

u32 budget_psram_bytes = COVER_PSRAM_BYTES;

void budget_capacity(struct engine_budget *out)
{
	out->bg_tiles = RES_BG_UNITS;
	out->bg_maps = 0;
	out->obj_tiles = RES_OBJ_UNITS;
	out->bg_banks = RES_PAL_BANKS;
	out->obj_banks = RES_PAL_BANKS;
	out->affine = RES_AFFINE_MATS;
	out->psram_bytes = budget_psram_bytes;
}

void budget_available(struct engine_budget *out)
{
	out->bg_tiles = pool_free_units(res_pool(RES_BG_VRAM), 1);
	out->bg_maps = 0;
	out->obj_tiles = pool_free_units(res_pool(RES_OBJ_VRAM), 1);
	out->bg_banks = (u8)pool_free_units(res_pool(RES_BG_PAL), 1);
	out->obj_banks = (u8)pool_free_units(res_pool(RES_OBJ_PAL), 1);
	out->affine = (u8)pool_free_units(res_pool(RES_AFFINE), 1);
	out->psram_bytes = budget_psram_bytes;
}

static int over(char *msg, u32 size, int code, const char *what, unsigned long need, unsigned long have,
		const char *unit)
{
	if (msg)
		snprintf(msg, size, "needs %lu %s of %s, %lu available", need, unit, what, have);
	return code;
}

int budget_check(const struct engine_budget *need, const struct engine_budget *have, char *msg, u32 size)
{
	// BG VRAM in bytes: tiles and maps share it
	unsigned long bg_need = (unsigned long)need->bg_tiles * 32 + (unsigned long)need->bg_maps * 2048;
	unsigned long bg_have = (unsigned long)have->bg_tiles * 32 + (unsigned long)have->bg_maps * 2048;
	if (bg_need > bg_have)
		return over(msg, size, BUDGET_BG_VRAM, "BG VRAM (tiles and maps)", bg_need, bg_have, "bytes");
	if (need->obj_tiles > have->obj_tiles)
		return over(msg, size, BUDGET_OBJ_TILES, "sprite VRAM", need->obj_tiles, have->obj_tiles, "tiles");
	if (need->bg_banks > have->bg_banks)
		return over(msg, size, BUDGET_BG_BANKS, "the BG palette", need->bg_banks, have->bg_banks, "banks");
	if (need->obj_banks > have->obj_banks)
		return over(msg, size, BUDGET_OBJ_BANKS, "the sprite palette", need->obj_banks, have->obj_banks,
			    "banks");
	if (need->affine > have->affine)
		return over(msg, size, BUDGET_AFFINE, "affine matrices", need->affine, have->affine, "matrices");
	if (need->psram_bytes > have->psram_bytes)
		return over(msg, size, BUDGET_PSRAM, "PSRAM", need->psram_bytes, have->psram_bytes, "bytes");
	if (msg && size)
		msg[0] = 0;
	return BUDGET_OK;
}

int budget_check_layout(const char *name, const struct engine_budget *need)
{
	struct engine_budget have;
	char why[80];
	budget_available(&have);
	int code = budget_check(need, &have, why, sizeof(why));
	if (code != BUDGET_OK)
		res_set_error("layout %s is over budget: it %s", name, why);
	return code;
}
