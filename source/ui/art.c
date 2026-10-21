// See art.h. The art itself is in art_data.c, written by `just assets`.
#include "art.h"
#include "../engine/gfx.h"
#include "../engine/res.h"
#include "ui.h"

const u16 *art_icon16_palette(u32 icon)
{
	return art_icon16_palettes[art_icon16_bank[icon < ART_ICON16_COUNT ? icon : 0]];
}

const u16 *art_box_colours(u32 box)
{
	return art_box_palettes[box < ART_BOX_COUNT ? box : ART_BOX_OTHER][ui_theme() != &ui_theme_light];
}

void art_icon8_tile(u32 id, u32 colour, u32 out[8])
{
	const u8 *bits = art_icon8_bits[id < ART_ICON8_COUNT ? id : 0];
	colour &= 15;
	for (u32 y = 0; y < 8; y++) {
		u32 row = 0;
		for (u32 x = 0; x < 8; x++)
			if (bits[y] >> x & 1)
				row |= colour << (x * 4);
		out[y] = row;
	}
}

u32 art_shadow_for(u32 width)
{
	u32 best = 0;
	for (u32 i = 1; i < ART_SHADOW_COUNT; i++)
		if (art_shadows[i].width <= width)
			best = i;
	return best;
}

void art_sprite_colours(u32 sprite, u16 ground, u16 out[16])
{
	const u16 *c = art_sprite_palettes[sprite < ART_SPRITE_COUNT ? sprite : 0];
	for (u32 i = 0; i < 16; i++)
		out[i] = c[i];
	out[ART_SHADOW_INDEX] = ground;
}

// Queues a new block's tiles from ROM (the queue may read them whenever it
// gets there). With the queue full, the block goes, not to the cache:
// revived, it would look loaded with nothing in it. 0 if it went.
static int upload(enum res_kind kind, u8 h, void *dst, const u32 *tiles, u32 count, int logged)
{
	if (!res_fresh(kind))
		return 1;
	if (gfx_queue_len() < GFX_QUEUE_CAP && gfx_queue_copy(dst, tiles, count * 32))
		return 1;
	if (logged)
		res_release(kind, h);
	else
		res_unref(kind, h);
	pool_drop(res_pool(kind), h);
	return 0;
}

// Sprite tiles shared by tag: the first tile's number or ART_NONE, and the
// block in *handle. flags: 0 or RES_NOLOG.
static u16 load_tiles(const u32 *tiles, u32 count, u16 tag, u8 flags, u8 *handle)
{
	u8 h = res_alloc(RES_OBJ_VRAM, (u16)count, 1, 0, RES_OBJ_UNITS, tag, flags);
	if (h == RES_NONE || !upload(RES_OBJ_VRAM, h, res_obj_addr(h), tiles, count, !(flags & RES_NOLOG)))
		return ART_NONE;
	*handle = h;
	return res_start(RES_OBJ_VRAM, h);
}

u16 art_load_icon16(u32 icon, u8 *bank)
{
	if (icon >= ART_ICON16_COUNT)
		return ART_NONE;
	u8 pal = res_palette(RES_OBJ_PAL, 1, (u16)(ART_TAG_ICON16_PAL | art_icon16_bank[icon]));
	if (pal == RES_NONE)
		return ART_NONE;
	*bank = (u8)res_start(RES_OBJ_PAL, pal);
	if (res_fresh(RES_OBJ_PAL))
		gfx_pal_load(16u + *bank, art_icon16_palette(icon), 16);
	u8 h;
	u16 tile = load_tiles(art_icon16_tiles[icon], ART_ICON16_TILES, (u16)(ART_TAG_ICON16 | icon), 0, &h);
	if (tile == ART_NONE)
		res_release(RES_OBJ_PAL, pal);
	return tile;
}

u16 art_load_sprite(u32 sprite)
{
	u8 h;
	return art_take_sprite(sprite, &h, 0);
}

u16 art_load_shadow(u32 shadow)
{
	u8 h;
	return art_take_shadow(shadow, &h, 0);
}

u16 art_take_sprite(u32 sprite, u8 *handle, int unlogged)
{
	if (sprite >= ART_SPRITE_COUNT)
		return ART_NONE;
	return load_tiles(art_sprite_tiles[sprite], ART_SPRITE_TILES, (u16)(ART_TAG_SPRITE | sprite),
			  unlogged ? RES_NOLOG : 0, handle);
}

u16 art_take_shadow(u32 shadow, u8 *handle, int unlogged)
{
	if (shadow >= ART_SHADOW_COUNT)
		return ART_NONE;
	return load_tiles(art_shadow_tiles[shadow], ART_SHADOW_HALF_TILES, (u16)(ART_TAG_SHADOW | shadow),
			  unlogged ? RES_NOLOG : 0, handle);
}

void art_drop(u8 handle)
{
	res_unref(RES_OBJ_VRAM, handle);
}
