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

// Sprite tiles shared by tag: the first tile's number or ART_NONE. A new
// block's tiles are queued from ROM (the queue may read them whenever it
// gets there); with the queue full, the block goes, not to the cache:
// revived, it would look loaded with nothing in it.
static u16 load_tiles(const u32 *tiles, u32 count, u16 tag)
{
	u8 h = res_alloc(RES_OBJ_VRAM, (u16)count, 1, 0, RES_OBJ_UNITS, tag, 0);
	if (h == RES_NONE)
		return ART_NONE;
	if (res_fresh(RES_OBJ_VRAM) &&
	    !(gfx_queue_len() < GFX_QUEUE_CAP && gfx_queue_copy(res_obj_addr(h), tiles, count * 32))) {
		res_release(RES_OBJ_VRAM, h);
		pool_drop(res_pool(RES_OBJ_VRAM), h);
		return ART_NONE;
	}
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
	u16 tile = load_tiles(art_icon16_tiles[icon], ART_ICON16_TILES, (u16)(ART_TAG_ICON16 | icon));
	if (tile == ART_NONE)
		res_release(RES_OBJ_PAL, pal);
	return tile;
}

u16 art_load_sprite(u32 sprite)
{
	if (sprite >= ART_SPRITE_COUNT)
		return ART_NONE;
	return load_tiles(art_sprite_tiles[sprite], ART_SPRITE_TILES, (u16)(ART_TAG_SPRITE | sprite));
}

u16 art_load_shadow(u32 shadow)
{
	if (shadow >= ART_SHADOW_COUNT)
		return ART_NONE;
	return load_tiles(art_shadow_tiles[shadow], ART_SHADOW_HALF_TILES, (u16)(ART_TAG_SHADOW | shadow));
}
