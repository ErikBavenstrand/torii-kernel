// See res.h.
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#include "res.h"
#include "../tlog.h"

static struct pool_block bg_blocks[RES_BG_BLOCKS] EWRAM_BSS;
static struct pool_block obj_blocks[RES_OBJ_BLOCKS] EWRAM_BSS;
static struct pool_block bg_pal_blocks[RES_PAL_BANKS] EWRAM_BSS;
static struct pool_block obj_pal_blocks[RES_PAL_BANKS] EWRAM_BSS;
static struct pool_block affine_blocks[RES_AFFINE_MATS] EWRAM_BSS;
static struct pool pools[RES_KINDS] EWRAM_BSS;

static struct {
	u8 kind;
	u8 handle;
} res_log[RES_LOG_CAP] EWRAM_BSS;
static u8 res_log_len EWRAM_BSS;
static char res_msg[120] EWRAM_BSS;
u8 res_errors_quiet;

void res_init(void)
{
	pool_init(&pools[RES_BG_VRAM], "BG VRAM", bg_blocks, RES_BG_BLOCKS, RES_BG_UNITS, RES_UNIT_BYTES);
	pool_init(&pools[RES_OBJ_VRAM], "sprite tiles", obj_blocks, RES_OBJ_BLOCKS, RES_OBJ_UNITS, RES_UNIT_BYTES);
	pool_init(&pools[RES_BG_PAL], "BG palette", bg_pal_blocks, RES_PAL_BANKS, RES_PAL_BANKS, 32);
	pool_init(&pools[RES_OBJ_PAL], "sprite palette", obj_pal_blocks, RES_PAL_BANKS, RES_PAL_BANKS, 32);
	pool_init(&pools[RES_AFFINE], "affine matrices", affine_blocks, RES_AFFINE_MATS, RES_AFFINE_MATS, 8);
	res_log_len = 0;
	res_msg[0] = 0;
}

struct pool *res_pool(enum res_kind kind)
{
	return &pools[kind];
}

const char *res_error(void)
{
	return res_msg;
}

void res_set_error(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vsnprintf(res_msg, sizeof(res_msg), format, args);
	va_end(args);
	if (!res_errors_quiet)
		tlog_error("engine: %s", res_msg);
}

u8 res_alloc(enum res_kind kind, u16 len, u16 align, u16 lo, u16 hi, u16 tag, u8 flags)
{
	struct pool *pool = &pools[kind];
	if (res_log_len == RES_LOG_CAP) {
		res_set_error("%s: the allocation log is full (%u references held through res_alloc)", pool->name,
			      RES_LOG_CAP);
		return RES_NONE;
	}
	u8 h = pool_alloc(pool, len, align, lo, hi, tag, flags);
	if (h == RES_NONE) {
		res_set_error("%s: can't allocate %u units of %u bytes (align %u, tag %04X): %s; %u free, %u cached, "
			      "longest free run %u",
			      pool->name, len, pool->unit_bytes, align, tag, pool_error_text(pool->error),
			      pool_free_units(pool, 0), pool_free_units(pool, 1) - pool_free_units(pool, 0),
			      pool_largest_free(pool, 1));
		return RES_NONE;
	}
	res_log[res_log_len].kind = (u8)kind;
	res_log[res_log_len].handle = h;
	res_log_len++;
	return h;
}

int res_release(enum res_kind kind, u8 handle)
{
	for (u8 i = res_log_len; i-- > 0;) {
		if (res_log[i].kind != kind || res_log[i].handle != handle)
			continue;
		for (u8 j = i; j + 1 < res_log_len; j++)
			res_log[j] = res_log[j + 1];
		res_log_len--;
		return pool_unref(&pools[kind], handle);
	}
	return 0;
}

u8 res_mark(void)
{
	return res_log_len;
}

void res_release_to(u8 mark)
{
	while (res_log_len > mark) {
		res_log_len--;
		pool_unref(&pools[res_log[res_log_len].kind], res_log[res_log_len].handle);
	}
}

u8 res_bg_tiles(u16 count, int bpp8, u8 charblock, u16 tag)
{
	u16 lo = (u16)(charblock & 3) * 512;
	u16 hi = bpp8 ? RES_BG_UNITS : lo + 1024;
	return res_alloc(RES_BG_VRAM, bpp8 ? count * 2 : count, bpp8 ? 2 : 1, lo, hi, tag, 0);
}

u16 res_bg_tile_index(u8 handle, int bpp8, u8 charblock)
{
	u16 units = res_start(RES_BG_VRAM, handle) - (u16)(charblock & 3) * 512;
	return bpp8 ? units / 2 : units;
}

u8 res_bg_map(u8 blocks, u16 tag)
{
	return res_alloc(RES_BG_VRAM, (u16)blocks * RES_MAP_UNITS, RES_MAP_UNITS, 0, RES_BG_UNITS, tag, POOL_TOP);
}

u8 res_bg_screenblock(u8 handle)
{
	return (u8)(res_start(RES_BG_VRAM, handle) / RES_MAP_UNITS);
}

void *res_bg_addr(u8 handle)
{
	return (void *)(uintptr_t)(0x06000000 + (u32)res_start(RES_BG_VRAM, handle) * RES_UNIT_BYTES);
}

u8 res_obj_tiles(u16 count, int bpp8, u16 tag)
{
	return res_alloc(RES_OBJ_VRAM, bpp8 ? count * 2 : count, bpp8 ? 2 : 1, 0, RES_OBJ_UNITS, tag, 0);
}

void *res_obj_addr(u8 handle)
{
	return (void *)(uintptr_t)(0x06010000 + (u32)res_start(RES_OBJ_VRAM, handle) * RES_UNIT_BYTES);
}

u8 res_palette(enum res_kind kind, u8 banks, u16 tag)
{
	return res_alloc(kind, banks, 1, 0, RES_PAL_BANKS, tag, 0);
}

u8 res_affine(u16 tag)
{
	return res_alloc(RES_AFFINE, 1, 1, 0, RES_AFFINE_MATS, tag, 0);
}
