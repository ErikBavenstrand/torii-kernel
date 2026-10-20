// The home carousel's widget (carousel.h): the ring (carousel_ring.h's
// numbers) as sprites, the backdrop, the label and the dot strip.
//
// Video memory, all taken when the view is pushed and kept until its pop:
// - sprite tiles: SLOTS object slots of 64 tiles, the shadows' six halves,
//   and two label sets (icon, name and count columns, dot strip) that take
//   turns, the hidden one drawn while the other shows;
// - sprite banks: one per drawn object (its fog), the label's text, the
//   label's icon, the dots;
// - affine matrices, one per drawn object;
// - three BG tiles (sky, horizon, floor) for the backdrop's map.
// An object slot gets its item's tiles copied in (2 KB, queued for VBlank)
// when the item comes within reach of the ring's position or its target,
// so the tiles of the item a key brings in are queued on the press, and an
// object shows once its copy is done. Nothing is allocated while the ring
// turns, so a held turn never fills the engine's allocation log.
#include <string.h>

#include "carousel.h"
#include "carousel_ring.h"
#include "../tlog.h"
#include "../engine/affine.h"
#include "../engine/frame.h"
#include "../engine/gfx.h"
#include "../engine/res.h"
#include "../engine/text.h"
#include "../ui/art.h"
#include "../ui/ui_priv.h"

// --- Look ---------------------------------------------------------------------
// spec.md's Carousel and motion.png; every number the drawing below uses.
static const struct carousel_look {
	struct ring_look ring;
	u8 horizon_y;     // px: the first row of the ground (floor colour)
	u8 lift;          // px the front object's last row stands above its shadow's middle
	u8 shadow_mix[PAL_THEMES];  // the shadows' tone: the floor with this much black, 256ths
	u8 name_y;        // px: the name's line top (heading font)
	u8 count_y;       // px: the count's line top (body font)
	u8 icon_gap;      // px between the label's icon and the name
	u8 centre_x;      // px: the label's and the dots' centre column
	u8 dots_y;        // px: the dot strip's tile row top
	u8 dot_top;       // px into it: a dot's top row
	u8 dot_size;      // px: a dot's side
	u8 dot_pitch;     // px between dot centres
	u8 dot_on_w;      // px: the front item's dot, long
	u8 dot_on_h;
	u8 dot_mix;       // how far the other dots fade toward the backdrop, 256ths
	u8 held_frames;   // frames between the turns of a held key
	u8 queue;         // items quick presses may get ahead of the ring
} look = {
	.ring =
		{
			.centre_x = 120,
			.radius_x = 98,
			.feet_y = 100,
			.rise = 30,
			.step = 6554,       // 36 degrees
			.scale_far = 108,   // 0.42
			.scale_span = 148,  // + 0.58 cos a
			.fog = 243,         // 0.95 (1 - cos a)
			.scale_min = 24,
			.slots = 5,
		},
	.horizon_y = 74,
	.lift = 4,
	.shadow_mix = {[PAL_LIGHT] = 28, [PAL_DARK] = 156},
	.name_y = 104,
	.count_y = 122,
	.icon_gap = 4,
	.centre_x = 120,
	.dots_y = 136,
	.dot_top = 2,
	.dot_size = 2,
	.dot_pitch = 7,
	.dot_on_w = 6,
	.dot_on_h = 3,
	.dot_mix = 64,
	.held_frames = 8,
	.queue = 2,
};

// --- Layout of the sprite tiles --------------------------------------------------

#define SLOTS 8  // object slots: the objects within reach of the position and the target
#define SLOT_NONE 0xFF
#define NAME_COLS 26   // the name's 8x16 columns (208 px)
#define COUNT_COLS 14  // the count's
#define DOT_COLS 16    // the dot strip's 8x8 tiles
#define SET_NAME 0
#define SET_COUNT (SET_NAME + NAME_COLS * 2)
#define SET_ICON (SET_COUNT + COUNT_COLS * 2)
#define SET_DOTS (SET_ICON + ART_ICON16_TILES)
#define SET_TILES (SET_DOTS + DOT_COLS)
#define SET_NONE 0xFF

// Sprite banks, from pal_first
enum {
	BANK_OBJECTS = 0,  // RING_OBJECTS of them, in drawing order
	BANK_TEXT = RING_OBJECTS,
	BANK_ICON,
	BANK_DOTS,
	BANKS,
};
// The label's text inks and the dots' in their banks
#define INK_NAME 1
#define INK_COUNT 2
#define INK_DOT_ON 15  // the front item's dot; the others 1 + their accent

// attr0/attr1 bits
#define OBJ_AFFINE 0x0100
#define OBJ_WIDE 0x4000
#define OBJ_TALL 0x8000
#define OBJ_HFLIP 0x1000
#define OBJ_SIZE_1 0x4000  // 16x16 square, 32x8 wide
#define OBJ_SIZE_3 0xC000  // 64x64 square
#define TAG_BACKDROP 0x6400  // + the horizon's row in its tile

enum {
	DIRTY_BACKDROP = 1,
	DIRTY_LABEL = 2,
};

// What a label set holds, and where its parts go on screen
struct label_set {
	u8 entry;  // SET_NONE: nothing yet
	u8 name_cols, count_cols, dot_cols;
	s16 icon_x, name_x, count_x, dots_x;
};

static struct {
	u8 node;  // the carousel's node (one at a time: the last one pushed)
	u8 count;
	const struct carousel_entry *items;
	struct ring_motion m;
	u32 since_turn;  // frames since the last turn began
	u8 pal_dirty;    // the colours need working out again
	u8 slots_dirty;  // the slots need working out again (the ring reached
	                 // another item, or a copy waited for the queue)
	s32 slots_at;    // the whole item the ring was past when they were
	u8 settled;      // UI_EV_CURSOR sent for the rest the ring is at
	// Sprite resources
	u16 slot_first;  // the object slots' first tile
	u16 shadow_first;
	u16 label_first;
	u8 label_tiles;  // its block, for its address
	u8 pal_first;
	u8 matrix[RING_OBJECTS];
	u8 slot_entry[SLOTS];   // the entry whose tiles a slot holds, SLOT_NONE
	u32 slot_ticket[SLOTS];  // its copy's queue ticket
	// Label sets: the one the sprites show, and which sets the sprites of
	// this frame and the last use (bit per set: a set is drawn only when
	// neither did, so nothing on screen changes under the beam)
	struct label_set set[2];
	u8 shown;
	u8 used_now, used_last;
	// The backdrop's three BG tiles
	u16 back_first;
} car EWRAM_BSS;

// --- Helpers ----------------------------------------------------------------------

// a with w/256 of b, per channel, rounded: ui_colour_mix unrolled (the
// same result), as the fog mixes a hundred colours a frame while the ring
// turns
static u16 mix(u16 a, u16 b, u32 w)
{
	if (w >= 256)
		return b;
	if (!w)
		return a;
	u32 v = 256 - w;
	u32 r = ((a & 31u) * v + (b & 31u) * w + 128u) >> 8;
	u32 g = ((a >> 5 & 31u) * v + (b >> 5 & 31u) * w + 128u) >> 8;
	u32 bl = ((a >> 10 & 31u) * v + (b >> 10 & 31u) * w + 128u) >> 8;
	return (u16)(r | g << 5 | bl << 10);
}

static const struct ui_theme *theme(void)
{
	return ui_theme();
}

static u16 base_of(u32 entry)
{
	return theme()->accent[car.items[entry].accent < PAL_ITEMS ? car.items[entry].accent : 0].base;
}

// The backdrop above the horizon for entry: the paper with backdrop_mix of
// its base (the GROUND and SKY roles' value for it)
static u16 sky_of(u32 entry)
{
	return mix(theme()->surface->bg, base_of(entry), theme()->backdrop_mix);
}

static u32 *obj_tiles(u32 first)
{
	return (u32 *)(uintptr_t)(0x06010000 + first * 32u);
}

static u32 *set_tiles(u32 set, u32 offset)
{
	return obj_tiles(car.label_first + set * SET_TILES + offset);
}

static s32 round_fix(s32 v)
{
	return (v + 0x8000) >> 16;
}


// --- Widget -----------------------------------------------------------------------

static int car_measure(u8 node, struct engine_budget *need)
{
	const struct ui_node *n = ui_node(node);
	if (n->w != 240 || n->h != 160) {
		ui_set_error("view %s: the carousel takes the whole screen", ui_node_view(node)->spec->name);
		return 0;
	}
	need->obj_tiles = (u16)(need->obj_tiles + SLOTS * ART_SPRITE_TILES + ART_SHADOW_COUNT * ART_SHADOW_HALF_TILES +
				2 * SET_TILES);
	need->obj_banks = (u8)(need->obj_banks + BANKS);
	need->affine = (u8)(need->affine + RING_OBJECTS);
	need->bg_tiles = (u16)(need->bg_tiles + 3);
	return 1;
}

// The backdrop's tiles: sky, the horizon's (sky over floor), floor, all in
// bank 1's backdrop colours
static void make_backdrop(u32 *tiles)
{
	u32 split = look.horizon_y % 8u;
	for (u32 y = 0; y < 8; y++) {
		tiles[y] = UI_ROLE_SKY * 0x11111111u;
		tiles[8 + y] = (y < split ? UI_ROLE_SKY : UI_ROLE_FLOOR) * 0x11111111u;
		tiles[16 + y] = UI_ROLE_FLOOR * 0x11111111u;
	}
}

static int car_init(u8 node)
{
	struct ui_view *v = ui_node_view(node);
	u8 slots = res_obj_tiles(SLOTS * ART_SPRITE_TILES, 0, 0);
	u8 shadows = res_obj_tiles(ART_SHADOW_COUNT * ART_SHADOW_HALF_TILES, 0, 0);
	u8 label = res_obj_tiles(2 * SET_TILES, 0, 0);
	u8 pal = res_palette(RES_OBJ_PAL, BANKS, 0);
	if (slots == RES_NONE || shadows == RES_NONE || label == RES_NONE || pal == RES_NONE) {
		ui_set_error("view %s: no room for the carousel's sprites", v->spec->name);
		return 0;
	}
	memset(&car, 0, sizeof(car));
	car.node = node;
	car.slot_first = res_start(RES_OBJ_VRAM, slots);
	car.shadow_first = res_start(RES_OBJ_VRAM, shadows);
	car.label_first = res_start(RES_OBJ_VRAM, label);
	car.label_tiles = label;
	car.pal_first = (u8)res_start(RES_OBJ_PAL, pal);
	for (u32 j = 0; j < RING_OBJECTS; j++) {
		u8 h = res_affine(0);
		if (h == RES_NONE) {
			car.node = UI_NONE;
			ui_set_error("view %s: no affine matrix for the carousel", v->spec->name);
			return 0;
		}
		car.matrix[j] = (u8)res_start(RES_AFFINE, h);
	}
	u8 back = ui_tiles(node, 3, (u16)(TAG_BACKDROP + look.horizon_y % 8u), &car.back_first);
	if (back == RES_NONE) {
		car.node = UI_NONE;
		return 0;
	}
	// Nothing shows these new blocks yet: written straight into VRAM
	if (res_fresh(RES_BG_VRAM))
		make_backdrop((u32 *)(uintptr_t)(0x06000000 + ui_plane_cb(ui_node_spec(node)->plane) * 0x4000 +
						 car.back_first * 32u));
	u32 *sh = obj_tiles(car.shadow_first);
	for (u32 i = 0; i < ART_SHADOW_COUNT; i++)
		for (u32 w = 0; w < ART_SHADOW_HALF_TILES * 8; w++)
			sh[i * ART_SHADOW_HALF_TILES * 8 + w] = art_shadow_tiles[i][w];
	for (u32 i = 0; i < SLOTS; i++)
		car.slot_entry[i] = SLOT_NONE;
	car.set[0].entry = car.set[1].entry = SET_NONE;
	car.shown = 0;
	ring_motion_rest(&car.m, 0);
	ui_view_uses_objs(v, 0);
	ui_dirty(node, DIRTY_BACKDROP | DIRTY_LABEL);
	return 1;
}

// --- Object slots ---------------------------------------------------------------

static u32 slot_of(u32 entry)
{
	for (u32 i = 0; i < SLOTS; i++)
		if (car.slot_entry[i] == entry)
			return i;
	return SLOT_NONE;
}

// Every entry within reach of the position or the target gets a slot, its
// tiles queued; the rest give theirs up
static void slots_update(void)
{
	// What's within reach changes only with the whole item the ring is past
	// and the target
	s32 at = car.m.pos >> 16;
	if (!car.slots_dirty && at == car.slots_at)
		return;
	car.slots_dirty = 0;
	car.slots_at = at;
	struct ring_object now[RING_OBJECTS], then[RING_OBJECTS];
	u32 n = ring_objects(&look.ring, car.count, car.m.pos, now);
	u32 t = ring_objects(&look.ring, car.count, car.m.to * FIX_ONE, then);
	u8 want[2 * RING_OBJECTS];
	u32 wants = 0;
	for (u32 i = 0; i < n + t; i++) {
		u32 e = ring_item(i < n ? now[i].k : then[i - n].k, car.count);
		u32 seen = 0;
		for (u32 j = 0; j < wants; j++)
			seen |= want[j] == e;
		if (!seen)
			want[wants++] = (u8)e;
	}
	u8 keep[SLOTS] = {0};
	for (u32 j = 0; j < wants; j++) {
		u32 s = slot_of(want[j]);
		if (s != SLOT_NONE)
			keep[s] = 1;
	}
	for (u32 j = 0; j < wants; j++) {
		if (slot_of(want[j]) != SLOT_NONE)
			continue;
		u32 s = 0;
		while (s < SLOTS && keep[s])
			s++;
		if (s == SLOTS) {
			car.slots_dirty = 1;  // more than the slots hold: the rest wait
			break;
		}
		// No sprite this frame uses the slot (it isn't kept): the copy lands
		// in VBlank before anything shows it again
		car.slot_entry[s] = SLOT_NONE;
		const u32 *src = art_sprite_tiles[car.items[want[j]].sprite % ART_SPRITE_COUNT];
		if (!gfx_queue_copy(obj_tiles(car.slot_first + s * ART_SPRITE_TILES), src, ART_SPRITE_TILES * 32)) {
			car.slots_dirty = 1;  // the queue is full: next frame
			break;
		}
		car.slot_entry[s] = want[j];
		car.slot_ticket[s] = gfx_queue_ticket();
		keep[s] = 1;
	}
}

// --- Colours ----------------------------------------------------------------------

// This frame's backdrop: the tint between the two items the ring is between
static u16 sky_now(void)
{
	s32 pos = car.m.pos;
	u32 a = ring_item(pos >> 16, car.count), b = ring_item((pos >> 16) + 1, car.count);
	u32 frac = (u32)(pos & 0xFFFF) >> 8;
	return mix(sky_of(a), sky_of(b), frac);
}

static void backdrop_colours(u16 sky)
{
	u16 bank[16];
	memcpy(bank, &gfx_pal_unfaded[UI_BANK_CONSOLES * 16], sizeof(bank));
	bank[UI_ROLE_SKY] = sky;
	bank[UI_ROLE_FLOOR] = mix(sky, 0, theme()->floor_mix);
	gfx_pal_load(UI_BANK_CONSOLES, bank, 16);
}

static void bank_load(u32 bank, const u16 *colours)
{
	gfx_pal_load(16u + car.pal_first + bank, colours, 16);
}

// The label's colours faded fade/256 toward the backdrop, and the dots'
static void label_colours(u16 sky, u32 fade, const struct label_set *ls)
{
	const struct pal_surfaces *s = theme()->surface;
	u16 c[16] = {0};
	c[INK_NAME] = mix(s->ink, sky, fade);
	c[INK_COUNT] = mix(s->ink2, sky, fade);
	bank_load(BANK_TEXT, c);
	if (ls->entry != SET_NONE) {
		const u16 *icon = art_icon16_palette(car.items[ls->entry].icon16);
		for (u32 i = 0; i < 16; i++)
			c[i] = mix(icon[i], sky, fade);
		bank_load(BANK_ICON, c);
		for (u32 i = 0; i < 16; i++)
			c[i] = 0;
		for (u32 a = 0; a < PAL_ITEMS && 1 + a < INK_DOT_ON; a++)
			c[1 + a] = mix(theme()->accent[a].base, sky, look.dot_mix);
		c[INK_DOT_ON] = base_of(ls->entry);
		bank_load(BANK_DOTS, c);
	}
}

// --- Keys -------------------------------------------------------------------------

static void slots_redo(void)
{
	car.slots_dirty = 1;
}

static int car_event(u8 node, const struct ui_event *e)
{
	if (e->type != UI_EV_KEYS || !car.count)
		return 0;
	if (e->keys & INPUT_A) {
		ui_emit(node, UI_EV_ACTIVATE, carousel_facing(node));
		return 1;
	}
	s32 dir = e->keys & INPUT_RIGHT ? 1 : e->keys & INPUT_LEFT ? -1 : 0;
	if (!dir)
		return 0;
	if (car.count < 2)
		return 1;  // one item: nothing to turn to
	const InputState *in = ui_input();
	int press = (in->pressed & (dir > 0 ? INPUT_RIGHT : INPUT_LEFT)) != 0;
	s32 lag = ring_motion_lag(&car.m) + dir * FIX_ONE;
	if (lag > look.queue * FIX_ONE || lag < -look.queue * FIX_ONE)
		return 1;
	// A held key's repeats turn at most every held_frames, gliding
	if (!press && car.m.moving && car.since_turn < look.held_frames)
		return 1;
	const struct ui_move *step = &theme()->motion.step;
	ring_motion_turn(&car.m, dir, step->frames, (Ease)step->ease);
	slots_redo();  // the item coming in: its tiles queued now
	car.since_turn = 0;
	car.settled = 0;
	car.pal_dirty = 1;
	ui_wake();
	return 1;
}

// --- Update: the sprites ---------------------------------------------------------

static void add_label(u8 node, s32 dx, s32 dy, const struct label_set *ls, u32 set, int text)
{
	u32 first = car.label_first + set * SET_TILES, b = car.pal_first;
	if (text) {
		ui_obj_add(node, (u16)((look.name_y + dy) & 0xFF), (u16)(OBJ_SIZE_1 | ((ls->icon_x + dx) & 0x1FF)),
			   (u16)((first + SET_ICON) | (b + BANK_ICON) << 12));
		for (u32 c = 0; c < ls->name_cols; c++)
			ui_obj_add(node, (u16)(OBJ_TALL | ((look.name_y + dy) & 0xFF)),
				   (u16)((ls->name_x + dx + (s32)c * 8) & 0x1FF),
				   (u16)((first + SET_NAME + c * 2) | (b + BANK_TEXT) << 12));
		for (u32 c = 0; c < ls->count_cols; c++)
			ui_obj_add(node, (u16)(OBJ_TALL | ((look.count_y + dy) & 0xFF)),
				   (u16)((ls->count_x + dx + (s32)c * 8) & 0x1FF),
				   (u16)((first + SET_COUNT + c * 2) | (b + BANK_TEXT) << 12));
	}
}

static void car_update(u8 node, u32 dt)
{
	if (car.node != node || !car.count)
		return;
	int was = car.m.moving;
	if (ring_motion_step(&car.m, dt) || was) {
		car.since_turn += dt;
		car.pal_dirty = 1;
		ui_wake();
		frame_measure("carousel-step");
	}
	ring_motion_wrap(&car.m, car.count);
	if (!car.m.moving && !car.settled) {
		car.settled = 1;
		ui_item_set(car.items[carousel_facing(node)].accent);
		ui_emit(node, UI_EV_CURSOR, carousel_facing(node));
	}
	slots_update();

	car.used_last = car.used_now;
	car.used_now = 0;
	const struct ui_node *n = ui_node(node);
	const struct ui_view *v = ui_node_view(node);
	if ((n->flags & UI_N_HIDDEN) || !ui_node_shown(node) || !ui_plane_owned(node))
		return;
	s32 dx = ui_view_dx(v), dy = v->offset;

	// The label: the item nearest the position, fading out toward the half
	// way point and the next in after it, sliding the way the ring turns
	s32 near = round_fix(car.m.pos);
	s32 u = car.m.pos - near * FIX_ONE;  // -0.5 to 0.5
	u32 entry = ring_item(near, car.count);
	u32 fade = (u32)(u < 0 ? -u : u) >> 7;  // 0-256
	s32 slide = -((u * 2 * (s32)theme()->motion.label_slide) >> 16);
	struct label_set *ls = &car.set[car.shown];
	if (ls->entry != entry) {
		u32 other = car.shown ^ 1u;
		if (car.set[other].entry == entry) {
			car.shown = (u8)other;
			ls = &car.set[other];
			car.pal_dirty = 1;
		} else {
			ui_dirty(node, DIRTY_LABEL);
		}
	}
	int text = ls->entry == entry && fade < 256;
	if (ls->entry != SET_NONE)
		car.used_now |= (u8)(1u << car.shown);

	u16 sky = sky_now();
	struct ring_object obj[RING_OBJECTS];
	u32 count = ring_objects(&look.ring, car.count, car.m.pos, obj);
	if (car.pal_dirty) {
		car.pal_dirty = 0;
		backdrop_colours(sky);
		label_colours(sky, text ? fade : 256, ls);
		u16 tone = mix(mix(sky, 0, theme()->floor_mix), 0,
					 look.shadow_mix[theme() == &ui_theme_light ? PAL_LIGHT : PAL_DARK]);
		for (u32 j = 0; j < count; j++) {
			u16 c[16];
			art_sprite_colours(car.items[ring_item(obj[j].k, car.count)].sprite, tone, c);
			for (u32 i = 1; i < 16; i++)
				c[i] = mix(c[i], sky, obj[j].fog);
			bank_load(BANK_OBJECTS + j, c);
		}
	}

	add_label(node, dx + slide, dy, ls, car.shown, text);
	// The objects, front first (on top, and first to the line's sprite
	// cycles), then their shadows behind them
	u8 drawn[RING_OBJECTS];
	for (u32 j = 0; j < count; j++) {
		const struct ring_object *o = &obj[j];
		u32 e = ring_item(o->k, car.count), s = slot_of(e);
		drawn[j] = 0;
		if (s == SLOT_NONE || !gfx_queue_done(car.slot_ticket[s]))
			continue;
		u32 pd = ring_matrix(o->scale);
		s32 x = o->x - ART_SPRITE_SIZE / 2 + dx;
		// The feet on the row the matrix written puts the last texel row on
		s32 feet = o->feet - (((s32)look.lift * o->scale + 128) >> 8);
		s32 y = feet - ART_SPRITE_SIZE / 2 - ring_foot_dy(pd) + dy;
		if (x <= -ART_SPRITE_SIZE || x >= 240 || y <= -ART_SPRITE_SIZE || y >= 160)
			continue;
		gfx_affine_set(car.matrix[j], (s16)pd, 0, 0, (s16)pd);
		drawn[j] = ui_obj_add(node, (u16)(OBJ_AFFINE | (y & 0xFF)),
				      (u16)(OBJ_SIZE_3 | car.matrix[j] << 9 | (x & 0x1FF)),
				      (u16)((car.slot_first + s * ART_SPRITE_TILES) | (car.pal_first + BANK_OBJECTS + j) << 12));
	}
	for (u32 j = 0; j < count; j++) {
		if (!drawn[j])
			continue;
		const struct ring_object *o = &obj[j];
		const struct art_sprite *sp = &art_sprites[car.items[ring_item(o->k, car.count)].sprite % ART_SPRITE_COUNT];
		u32 shadow = art_shadow_for((u32)art_shadows[sp->shadow].width * o->scale >> 8);
		u16 tile = (u16)((car.shadow_first + shadow * ART_SHADOW_HALF_TILES) | (car.pal_first + BANK_OBJECTS + j) << 12);
		s32 y = o->feet - ART_SHADOW_RISE + dy, x = o->x + dx;  // centred on the ground row
		if (y <= -8 || y >= 160)
			continue;
		ui_obj_add(node, (u16)(OBJ_WIDE | (y & 0xFF)), (u16)(OBJ_SIZE_1 | ((x - 32) & 0x1FF)), tile);
		ui_obj_add(node, (u16)(OBJ_WIDE | (y & 0xFF)), (u16)(OBJ_SIZE_1 | OBJ_HFLIP | (x & 0x1FF)), tile);
	}
	// The dot strip, from the set shown (which moves at the half way point)
	if (ls->entry != SET_NONE)
		for (u32 c = 0; c < ls->dot_cols; c++)
			ui_obj_add(node, (u16)((look.dots_y + dy) & 0xFF), (u16)((ls->dots_x + dx + (s32)c * 8) & 0x1FF),
				   (u16)((car.label_first + car.shown * SET_TILES + SET_DOTS + c) |
					 (car.pal_first + BANK_DOTS) << 12));
}

// --- Draw: the backdrop's map and the label sets ---------------------------------

static int draw_backdrop(u8 node)
{
	const struct ui_node *n = ui_node(node);
	u32 rows = (u32)n->h / 8, cols = (u32)n->w / 8, ty = (u32)n->y / 8, tx = (u32)n->x / 8;
	u16 *map = ui_map_rows(node, ty, rows);
	if (!map)
		return 0;
	u32 horizon = look.horizon_y / 8u;
	for (u32 y = 0; y < rows; y++) {
		u32 t = ty + y < horizon ? 0 : ty + y == horizon ? 1 : 2;
		for (u32 x = 0; x < cols; x++)
			map[y * 32 + tx + x] = ui_entry(car.back_first + t, UI_BANK_CONSOLES);
	}
	return 1;
}

static void clear_words(u32 *p, u32 words)
{
	while (words--)
		*p++ = 0;
}

// A run of a dot strip's row: [x, x + w) on row y, in colour
static void dot_row(const struct text_window *win, s32 x, u32 y, s32 w, u32 colour)
{
	if (x >= 0 && w > 0)
		text_clear(win, (u32)x, y, (u32)w, 1, colour);
}

// Draws entry's label into set: its icon, name and count, and the dots
static void draw_label(u32 set, u32 entry)
{
	const struct carousel_entry *it = &car.items[entry];
	const struct ui_theme *t = theme();
	struct label_set *ls = &car.set[set];
	clear_words(set_tiles(set, 0), SET_TILES * 8);
	const u32 *icon = art_icon16_tiles[it->icon16 < ART_ICON16_COUNT ? it->icon16 : 0];
	u32 *dst = set_tiles(set, SET_ICON);
	for (u32 w = 0; w < ART_ICON16_TILES * 8; w++)
		dst[w] = icon[w];

	struct text_line line;
	struct text_window win = {set_tiles(set, SET_NAME), NAME_COLS, 2};
	u32 ext = 0;
	if (it->name && *it->name) {
		text_layout(t->heading, it->name, strlen(it->name), NAME_COLS * 8, &line);
		text_draw_line(&win, t->heading, 0, 0, INK_NAME, &line);
		ext = line.extent;
	}
	u32 group = 16 + look.icon_gap + ext;
	ls->icon_x = (s16)(look.centre_x - (s32)group / 2);
	ls->name_x = (s16)(ls->icon_x + 16 + look.icon_gap);
	ls->name_cols = (u8)((ext + 7) / 8);

	win = (struct text_window){set_tiles(set, SET_COUNT), COUNT_COLS, 2};
	ext = 0;
	if (it->count && *it->count) {
		text_layout(t->body, it->count, strlen(it->count), COUNT_COLS * 8, &line);
		text_draw_line(&win, t->body, 0, 0, INK_COUNT, &line);
		ext = line.extent;
	}
	ls->count_x = (s16)(look.centre_x - (s32)ext / 2);
	ls->count_cols = (u8)((ext + 7) / 8);

	// One dot per item in its colour, the front one long in its base
	win = (struct text_window){set_tiles(set, SET_DOTS), DOT_COLS, 1};
	s32 strip = (s32)(car.count - 1) * look.dot_pitch + look.dot_on_w;
	if (strip > DOT_COLS * 8)
		strip = DOT_COLS * 8;
	for (u32 i = 0; i < car.count; i++) {
		s32 c = look.dot_on_w / 2 + (s32)i * look.dot_pitch;
		if (c + look.dot_on_w / 2 > strip)
			break;
		if (i == entry) {
			u32 top = look.dot_top - (look.dot_on_h - look.dot_size + 1u) / 2u;
			for (u32 r = 0; r < look.dot_on_h; r++) {
				int end = r == 0 || r + 1 == look.dot_on_h;  // rounded ends
				dot_row(&win, c - look.dot_on_w / 2 + end, top + r, look.dot_on_w - 2 * end, INK_DOT_ON);
			}
		} else {
			for (u32 r = 0; r < look.dot_size; r++)
				dot_row(&win, c - look.dot_size / 2, look.dot_top + r, look.dot_size,
					1 + (car.items[i].accent < PAL_ITEMS ? car.items[i].accent : 0));
		}
	}
	ls->dots_x = (s16)(look.centre_x - strip / 2);
	ls->dot_cols = (u8)((strip + 7) / 8);
	ls->entry = (u8)entry;
}

static void car_draw(u8 node)
{
	struct ui_node *n = ui_node(node);
	u8 todo = n->dirty;
	n->dirty = 0;
	if (car.node != node)
		return;
	if ((todo & DIRTY_BACKDROP) && !draw_backdrop(node))
		ui_dirty(node, DIRTY_BACKDROP);
	if (!(todo & DIRTY_LABEL) || !car.count)
		return;
	u32 entry = ring_item(round_fix(car.m.pos), car.count);
	if (car.set[car.shown].entry == entry || car.set[car.shown ^ 1u].entry == entry)
		return;
	// Into a set no sprite of this frame or the last uses
	u32 busy = car.used_now | car.used_last, set = !(busy & 1) ? 0 : !(busy & 2) ? 1 : SET_NONE;
	if (set == SET_NONE) {
		ui_dirty(node, DIRTY_LABEL);  // next frame
		return;
	}
	draw_label(set, entry);
	car.pal_dirty = 1;
}

const struct ui_ops ui_carousel_ops = {4, car_measure, car_init, car_event, car_update, car_draw};

// --- API ----------------------------------------------------------------------------

void carousel_set(u8 node, const struct carousel_entry *entries, u32 count, u32 facing)
{
	if (car.node != node)
		return;
	TLOG_ASSERT(count <= CAROUSEL_MAX);
	car.items = entries;
	car.count = (u8)(count < CAROUSEL_MAX ? count : CAROUSEL_MAX);
	ring_motion_rest(&car.m, (s32)(facing < car.count ? facing : 0));
	for (u32 i = 0; i < SLOTS; i++)
		car.slot_entry[i] = SLOT_NONE;
	slots_redo();
	carousel_refresh(node);
	car.settled = 1;
	if (car.count)
		ui_item_set(entries[carousel_facing(node)].accent);
}

u32 carousel_facing(u8 node)
{
	(void)node;
	return car.count ? ring_item(car.m.to, car.count) : 0;
}

void carousel_refresh(u8 node)
{
	if (car.node != node)
		return;
	car.set[0].entry = car.set[1].entry = SET_NONE;
	car.pal_dirty = 1;
	ui_dirty(node, DIRTY_LABEL);
}
