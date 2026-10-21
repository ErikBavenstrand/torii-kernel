// The home carousel (v0.4 decisions, Look: Carousel; spec.md; research
// override R6), a widget of kind UI_CAROUSEL: the items' objects on a ring
// seen slightly from above, the front one lowest and largest, the others
// higher, smaller and fogged toward the backdrop, each over a soft ground
// shadow; the backdrop (the item's tint over the paper, the ground below a
// low horizon a shade darker); and the label under the ring: the front
// item's 16x16 icon and name, its count, and a strip of one dot per item
// in the items' colours with the front one long.
//
// LEFT and RIGHT turn the ring by an item (the theme's motion.step: the
// objects slide along the ring, the backdrop's tint crossfades, the label
// fades out sliding the way the ring turns and the next fades in, the dot
// moves half way); a held key keeps turning, gliding from item to item. The
// ring wraps. A sends UI_EV_ACTIVATE with the index it faces, and each turn
// that comes to rest sends UI_EV_CURSOR with it.
//
// Its node takes the whole screen on a plane of its own (the backdrop's
// map) and draws everything else with sprites: the objects as affine
// sprites (double size never: a ring slot's scale is at most 1), shadows as
// ordinary ones in pre-drawn widths, the label as columns of text sprites
// that fade by palette and slide. Its tiles are fixed slots it fills as the
// ring turns, so turning never allocates. One carousel at a time.
#ifndef TORII_SCREENS_CAROUSEL_H
#define TORII_SCREENS_CAROUSEL_H

#include <gba_base.h>

// One item on the ring
struct carousel_entry {
	const char *name;   // the label's heading
	const char *count;  // the line under it ("176 games"; "" none)
	u8 sprite;          // ART_SPRITE_*: its object
	u8 icon16;          // ART_ICON16_*: the label's icon
	u8 accent;          // enum pal_item: its colour (backdrop tint, dot)
};

// Items a carousel takes (dot strip and colours have room for this many)
#define CAROUSEL_MAX 14

// Shows count entries (kept by pointer, valid until the next call or the
// view's pop), facing entries[facing], at rest: no motion. The screen's
// accent becomes the facing item's (ui_item_set).
void carousel_set(u8 node, const struct carousel_entry *entries, u32 count, u32 facing);
// The entry the carousel faces: where it is turning to, if it is
u32 carousel_facing(u8 node);
// The entries' texts changed (a count): the label draws again
void carousel_refresh(u8 node);

// The open's zoom (ui_zoom_set, UI_TR_OPEN): the object of the item the
// carousel faces, as it shows now. 0 if it doesn't show (its tiles not
// loaded yet): the open is a plain fade then.
struct ui_zoom;
int carousel_zoom(u8 node, struct ui_zoom *zoom);
// The close's (UI_TR_CLOSE): entry's object as it will show at the front at
// rest, before home is up
void carousel_zoom_rest(const struct carousel_entry *entry, struct ui_zoom *zoom);

#endif
