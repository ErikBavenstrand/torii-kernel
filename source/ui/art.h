// The menu's art: 16x16 icons, 8x8 icons, the carousel objects, their ground
// shadows and the placeholder box, by id, so screens never name tile
// offsets. `just assets` converts the PNGs in assets/ui into art_data.c and
// art_ids.h (the ids); redrawing one is a PNG edit and `just assets`.
//
// - 16x16 icons (ART_ICON16_*): 4bpp sprite tiles, four in 1D order. Two
//   icons share each 16-colour bank (art_icon16_palette; art.toml pairs
//   them), so the pixels index into it as they are.
// - 8x8 icons (ART_ICON8_*): one colour, kept as 1 bit a pixel and expanded
//   into a 4bpp BG tile in whatever colour index the screen draws them in
//   (art_icon8_tile). The PNGs' own colour is a preview only.
// - Carousel objects (ART_SPRITE_*): 64x64 4bpp sprite tiles for an affine
//   OBJ, drawn 1:1 (no resampling), centred across and standing on the
//   last row (ART_SPRITE_FOOT). Each has a bank of its own with at most
//   ART_SPRITE_COLOURS colours, so fog can step it toward the backdrop;
//   index ART_SHADOW_INDEX is left for the ground shadow's tone: it is 0 in
//   art_sprite_palettes, and the screen writes its ground tone there
//   (art_sprite_colours does). An affine
//   OBJ scales about its box's centre, so at scale s the foot row lands
//   (ART_SPRITE_FOOT + 0.5 - 32) * s below it: anchor the feet from that.
// - Ground shadows (art_shadows): ordinary sprites in a few widths, each a
//   32x8 left half whose right half is the same tiles flipped across, both
//   drawn with the object's bank in colour ART_SHADOW_INDEX. The drawn rows
//   are centred in the 8: put the halves' top ART_SHADOW_RISE rows above
//   the foot row.
// - The placeholder box: ART_BOX_SIZE square 4bpp BG tiles, 10 a row (map
//   them in order; 80x80 is no sprite size), in a BG bank of its own
//   (a ui_image, as list with art shows it) whose colours ART_BOX_* the screen recolours: its
//   defaults are GBA indigo, a muted red and a darker red.
// - Items (ART_ITEM_*, in the carousel's order): each item's 16x16 icon,
//   8x8 icon and carousel object.
//
// The loaders put art into VRAM through the engine (engine/res.h, shared by
// tag, so loading it again is free; the current view owns it) and queue the
// copy for VBlank. A screen that loads and drops objects on its own schedule
// (the carousel's ring, turning) takes them unlogged with art_take_* and
// drops each with art_drop, as the cover streamer does, so a long run of
// turns never fills the engine's allocation log.
#ifndef TORII_UI_ART_H
#define TORII_UI_ART_H

#include <gba_base.h>
#include "art_ids.h"

#define ART_NONE 0xFFFF

#define ART_ICON16_TILES 4
#define ART_SPRITE_SIZE 64  // pixels a side
#define ART_SPRITE_TILES 64
#define ART_SPRITE_FOOT 63  // the row every object stands on
#define ART_SPRITE_COLOURS 14  // indices 1-14; 15 is the shadow's
#define ART_SHADOW_INDEX 15
#define ART_SHADOW_HALF_TILES 4  // a 32x8 half
#define ART_SHADOW_RISE 4
#define ART_BOX_SIZE 80
#define ART_BOX_TILES 100
// The placeholder box's colour indices
#define ART_BOX_BAND 1  // the band across the top (the console's base)
#define ART_BOX_BODY 2
#define ART_BOX_SHAPE 3  // the hill and the ground on the body

// Tags of the engine blocks the loaders take: kind << 8 | id
#define ART_TAG_ICON16 0xA000
#define ART_TAG_ICON16_PAL 0xA100
#define ART_TAG_SPRITE 0xA200
#define ART_TAG_SHADOW 0xA300

struct art_sprite {
	u8 width, height;  // the drawn part, centred across, its last row at ART_SPRITE_FOOT
	u8 shadow;  // the ground shadow its width suggests at scale 1 (art_shadows)
};

struct art_shadow {
	u8 width;  // pixels, even, centred on the boundary of the two halves
	u8 height;
};

struct art_item {
	u8 icon16, icon8, sprite;
};

extern const u32 art_icon16_tiles[ART_ICON16_COUNT][ART_ICON16_TILES * 8];
extern const u8 art_icon16_bank[ART_ICON16_COUNT];
extern const u16 art_icon16_palettes[ART_ICON16_BANKS][16];
extern const u8 art_icon8_bits[ART_ICON8_COUNT][8];  // a byte a row, bit 0 the left pixel
extern const u32 art_sprite_tiles[ART_SPRITE_COUNT][ART_SPRITE_TILES * 8];
extern const u16 art_sprite_palettes[ART_SPRITE_COUNT][16];
extern const struct art_sprite art_sprites[ART_SPRITE_COUNT];
extern const u32 art_shadow_tiles[ART_SHADOW_COUNT][ART_SHADOW_HALF_TILES * 8];
extern const struct art_shadow art_shadows[ART_SHADOW_COUNT];  // narrowest first
extern const u32 art_box_tiles[ART_BOX_TILES * 8];
extern const u16 art_box_palette[16];
extern const struct art_item art_items[ART_ITEM_COUNT];

// The 16 colours of a 16x16 icon's bank (it shares them with its pair).
const u16 *art_icon16_palette(u32 icon);
// Writes 8x8 icon id as a 4bpp tile: its pixels in colour (1-15), the rest
// 0 (transparent). To queue it for VRAM, out must outlive the copy (not on
// the stack).
void art_icon8_tile(u32 id, u32 colour, u32 out[8]);
// The widest ground shadow no wider than width pixels (the narrowest if
// none is).
u32 art_shadow_for(u32 width);
// A carousel object's 16 colours with ground (the shadow's tone) in
// ART_SHADOW_INDEX, for its bank; fog steps them toward the backdrop.
void art_sprite_colours(u32 sprite, u16 ground, u16 out[16]);

// Loaders: each returns the first tile's number (OAM attribute 2's low
// bits), or ART_NONE when sprite VRAM or the palette is full (logged) or
// the VRAM queue is (try again next frame).
// A 16x16 icon's tiles, with its bank loaded; *bank gets the bank number.
u16 art_load_icon16(u32 icon, u8 *bank);
// A carousel object's tiles; its colours (art_sprite_palettes) go into a
// bank the screen picks, so each object can fog on its own.
u16 art_load_sprite(u32 sprite);
// A ground shadow's 32x8 half.
u16 art_load_shadow(u32 shadow);
// The same, unlogged when unlogged is nonzero: *handle gets the block, for
// art_drop. Taking one already held shares it (each take needs a drop).
u16 art_take_sprite(u32 sprite, u8 *handle, int unlogged);
u16 art_take_shadow(u32 shadow, u8 *handle, int unlogged);
// Drops what an unlogged art_take_* took.
void art_drop(u8 handle);

#endif
