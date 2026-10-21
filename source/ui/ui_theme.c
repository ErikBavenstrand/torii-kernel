// The built-in themes and the palette banks they give (ui.h). Hardware-free:
// ui/ui_run.c loads the banks this builds. Every look value of the new menu
// that more than one screen shares is here, from docs/design/v0.4/spec.md;
// the colours are docs/design/v0.4/palette.json (ui/palette.c).
#include "ui.h"
#include "ui_priv.h"

// Screen transitions, the same in both
#define TRANSITIONS \
	{ \
		[UI_TR_TAB] = {UI_TR_FADES | UI_TR_SLIDE_X, 3, 8, 16}, \
		[UI_TR_DEEPER] = {UI_TR_FADES | UI_TR_SLIDE_Y, 3, 8, 12}, \
		[UI_TR_FADE] = {UI_TR_FADES, 3, 6, 0}, \
		[UI_TR_LEAVE] = {UI_TR_FADES, 3, 8, 0}, \
	}

// The Paper clean sizes, px: the same in both
#define LOOK \
	{ \
		.margin = 4, \
		.title_h = 18, \
		.title_icon_x = 5, \
		.title_icon_y = 1, \
		.title_text_x = 25, \
		.title_text_y = 1, \
		.title_right = 235, \
		.title_right_y = 2, \
		.title_right_w = 64, \
		.title_sub_gap = 4, \
		.content_top = 20, \
		.content_bottom = 143, \
		.list_rows = 8, \
		.list_pitch = 14, \
		.settings_pitch = 12, \
		.pill_h = 12, \
		.pill_radius = 3, \
		.pill_out_l = 1, \
		.pill_out_r = 1, \
		.card_radius = 4, \
		.button_radius = 7, \
		.edge = 1, \
		.shadow_offset = 1, \
		.scrollbar_w = 3, \
		.scrollbar_x = 5, \
		.section_icon_gap = 3, \
		.path_gap = 3, \
		.storage_h = 5, \
		.reading_key_w = 44, \
		.row_mark_gap = 4, \
		.row_badge_gap = 3, \
		.row_dot_gap = 2, \
		.hint_y = 147, \
		.hint_chip_h = 10, \
		.hint_chip_radius = 3, \
		.hint_chip_pad = 3, \
		.hint_label_gap = 2, \
		.hint_gap = 8, \
		.hint_max = 4, \
	}

// The motions, 60 frames a second: the same in both
#define MOTIONS \
	{ \
		.step = {10, EASE_OUT_CUBIC}, \
		.label_slide = 14, \
		.open = {12, EASE_LINEAR}, \
		.open_zoom_end = 5, \
		.open_zoom = 486, /* 1.9 in 256ths */ \
		.open_rise = 12, \
		.open_card = {7, EASE_OUT_QUAD}, \
		.close = {10, EASE_LINEAR}, \
		.popup_open = {6, EASE_OUT_QUAD}, \
		.popup_close = {4, EASE_OUT_QUAD}, \
		.popup_slide = 8, \
		.theme_wipe = {16, EASE_SMOOTHSTEP}, \
		.wipe_edge = 4, \
		.letter_linger = 20, \
		.letter_close = {4, EASE_OUT_QUAD}, \
		.pill_spring = SPRING_FAST, \
	}

// What the v0.3 widgets and the rest share
#define SHARED \
	.look = LOOK, .motion = MOTIONS, .body = &font_body, .heading = &font_heading, .row_h = 16, .pad = 6, \
	.idle_dim = 11, .fade_frames = 8, .slide = 24, .marquee_delay = 45, .marquee_speed = 60, .marquee_pause = 60, \
	.marquee_gap = 8, .page_fade = 4, .page_fade_out = 4, .input = INPUT_TIMING_DEFAULT, .dialog_in = 12, \
	.dialog_out = 10, .transition = TRANSITIONS, .bar_shade_mix = 85

const struct ui_theme ui_theme_dark = {
	.name = "Dark",
	.surface = &pal_surfaces[PAL_DARK],
	.accent = pal_accents[PAL_DARK],
	.pill_mix = PAL_DARK_PILL_MIX,
	.backdrop_mix = 20,  // 8 %
	.floor_step = {1, 1, 1},  // a step lighter, as the mockups' dark floor
	.float_mix = 13,     // 5 %
	.modal_dim = 8,
	.confirm_dim = 6,  // towards black: the page's cards near the shadow colour, as spec.md asks
	.confirm_brighten = 0,
	SHARED,
};

const struct ui_theme ui_theme_light = {
	.name = "Light",
	.surface = &pal_surfaces[PAL_LIGHT],
	.accent = pal_accents[PAL_LIGHT],
	.pill_mix = PAL_LIGHT_PILL_MIX,
	.backdrop_mix = 15,  // 6 %
	.floor_step = {-1, -1, 0},  // about 4 % darker (spec.md), as the mockups' light floor
	.float_mix = 0,
	.modal_dim = 6,
	.confirm_dim = 8,  // half way to white, within a level or two of half way to the paper
	.confirm_brighten = 1,
	SHARED,
};

static struct {
	const struct ui_theme *theme;
	u8 item;
	u8 item_chosen;  // 0: UI_ITEM_DEFAULT
} current EWRAM_BSS;

void ui_theme_select(const struct ui_theme *theme)
{
	current.theme = theme ? theme : &ui_theme_dark;
}

void ui_item_select(u32 item)
{
	current.item = (u8)(item < PAL_ITEMS ? item : UI_ITEM_DEFAULT);
	current.item_chosen = 1;
}

const struct ui_theme *ui_theme(void)
{
	return current.theme ? current.theme : &ui_theme_dark;
}

u32 ui_item(void)
{
	return current.item_chosen ? current.item : UI_ITEM_DEFAULT;
}

u32 ui_round_inset(u32 radius, u32 e)
{
	// The pixels a circle of the radius covers, centred on the corner's
	// grid point, a little fuller than the true circle (r^2 + r - 1), as
	// the mockups round: 2,1,0 at r3, 3,1,1,0 at r4, 3,2,1,0,0 at r5
	if (e >= radius)
		return 0;
	u32 dy = radius - e, limit = radius * radius + radius - 1, x = 0;
	while (x < radius && (radius - x) * (radius - x) + dy * dy > limit)
		x++;
	return x;
}

u16 ui_colour_mix(u16 a, u16 b, u32 amount)
{
	if (amount > 256)
		amount = 256;
	u32 out = 0;
	for (u32 shift = 0; shift < 15; shift += 5) {
		u32 ca = (a >> shift) & 31, cb = (b >> shift) & 31;
		out |= ((ca * (256 - amount) + cb * amount + 128) >> 8) << shift;
	}
	return (u16)out;
}

u16 ui_floor_colour(const struct ui_theme *theme, u16 sky)
{
	u32 out = 0;
	for (u32 i = 0; i < 3; i++) {
		s32 c = (s32)((sky >> (5 * i)) & 31) + theme->floor_step[i];
		out |= (u32)(c < 0 ? 0 : c > 31 ? 31 : c) << (5 * i);
	}
	return (u16)out;
}

u16 ui_colour_bldy(u16 c, u32 evy, int brighten)
{
	if (evy > 16)
		evy = 16;
	u32 out = 0;
	for (u32 shift = 0; shift < 15; shift += 5) {
		u32 i = (c >> shift) & 31;
		i = brighten ? i + ((31 - i) * evy >> 4) : i - (i * evy >> 4);
		out |= i << shift;
	}
	return (u16)out;
}

void ui_theme_banks(const struct ui_theme *theme, u32 item, u16 out[UI_BANKS * 16])
{
	const struct pal_surfaces *s = theme->surface;
	const struct pal_accent *a = &theme->accent[item < PAL_ITEMS ? item : UI_ITEM_DEFAULT];
	const struct pal_accent *danger = &theme->accent[PAL_DANGER];
	const struct pal_accent *nor = &theme->accent[PAL_NOR];
	u16 *normal = out, *consoles = out + 16, *alert = out + 32;

	// The neutrals, in every bank; index 0 (transparent) shows as the backdrop
	normal[UI_ROLE_NONE] = s->bg;
	normal[UI_ROLE_BG] = s->bg;
	normal[UI_ROLE_PANEL] = s->card;
	normal[UI_ROLE_BORDER] = s->edge;
	normal[UI_ROLE_SHADOW] = s->shadow;
	normal[UI_ROLE_TRACK] = s->soft;
	normal[UI_ROLE_TEXT] = s->ink;
	normal[UI_ROLE_DIM] = s->ink2;
	normal[UI_ROLE_FAINT] = s->ink3;

	normal[UI_ROLE_ACCENT] = a->base;
	normal[UI_ROLE_TINT] = a->tint;
	normal[UI_ROLE_ACCENT_INK] = a->ink;
	normal[UI_ROLE_ACCENT_TEXT] = a->on;
	normal[UI_ROLE_GROUND] = ui_colour_mix(s->bg, a->base, theme->backdrop_mix);
	normal[UI_ROLE_DANGER] = danger->ink;
	normal[UI_ROLE_FLOAT] = ui_colour_mix(s->card, s->ink, theme->float_mix);

	// Indices a bank doesn't use stay black: nothing draws with them
	for (u32 i = 0; i < 16; i++)
		consoles[i] = alert[i] = i <= UI_ROLE_FAINT ? normal[i] : 0;

	consoles[UI_ROLE_INK_GBA] = theme->accent[PAL_GBA].ink;
	consoles[UI_ROLE_INK_GBC] = theme->accent[PAL_GBC].ink;
	consoles[UI_ROLE_INK_GB] = theme->accent[PAL_GB].ink;
	consoles[UI_ROLE_INK_NES] = theme->accent[PAL_NES].ink;
	consoles[UI_ROLE_SKY] = normal[UI_ROLE_GROUND];
	consoles[UI_ROLE_FLOOR] = ui_floor_colour(theme, normal[UI_ROLE_GROUND]);
	consoles[UI_ROLE_INK_FAV] = theme->accent[PAL_FAV].ink;

	alert[UI_ROLE_ALERT_INK] = danger->ink;
	alert[UI_ROLE_ALERT_TINT] = danger->tint;
	alert[UI_ROLE_NOR_FILL] = nor->base;
	alert[UI_ROLE_NOR_FILL_MID] = ui_colour_mix(nor->base, s->card, theme->bar_shade_mix);
	alert[UI_ROLE_NOR_FILL_LIGHT] = ui_colour_mix(nor->base, s->card, 2u * theme->bar_shade_mix);
	alert[UI_ROLE_NOR_INK] = nor->ink;
}
