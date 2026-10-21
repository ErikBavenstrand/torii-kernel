// The built-in themes' palette banks (source/ui/ui_theme.c) from the
// generated palette (source/ui/palette.c): every bank per theme and item,
// and that what is drawn as text stays readable.
#include <stdint.h>
#include <string.h>

#include "../../source/ui/ui.h"
#include "../kernel/torii_test.h"
#include "tests.h"

static const struct ui_theme *const themes[PAL_THEMES] = {&ui_theme_light, &ui_theme_dark};

// sRGB to linear light, in millionths, for each 5-bit channel value (as the
// GBA shows it: v << 3 | v >> 2)
static const u32 linear[32] = {
	0,      2428,   5182,   9134,   15209,  22174,  30713,  40915,  54480,  68478,   84376,
	102242, 124772, 147027, 171441, 198069, 230740, 262251, 296138, 332452, 376262,  417885,
	462077, 508881, 564712, 617207, 672443, 730461, 799103, 863157, 930111, 1000000,
};

// Relative luminance in millionths
static u32 luminance(u16 c)
{
	return (u32)((2126ull * linear[c & 31] + 7152ull * linear[(c >> 5) & 31] + 722ull * linear[(c >> 10) & 31] +
		      5000) /
		     10000);
}

// The WCAG contrast ratio of a and b, in hundredths
static u32 contrast(u16 a, u16 b)
{
	u32 la = luminance(a), lb = luminance(b);
	if (la < lb) {
		u32 t = la;
		la = lb;
		lb = t;
	}
	return (u32)(((uint64_t)(la + 50000) * 100 + (lb + 50000) / 2) / (lb + 50000));
}

void test_theme_colour_mix(void)
{
	CHECK_EQ(ui_colour_mix(0x0000, 0x7FFF, 0), 0x0000);
	CHECK_EQ(ui_colour_mix(0x0000, 0x7FFF, 256), 0x7FFF);
	CHECK_EQ(ui_colour_mix(0x0000, 0x7FFF, 999), 0x7FFF);  // clamped
	CHECK_EQ(ui_colour_mix(0x0000, 0x001F, 128), 16);      // 15.5 rounds up
	CHECK_EQ(ui_colour_mix(0x7C00, 0x03E0, 128), 16 << 10 | 16 << 5);
	// Each channel on its own
	CHECK_EQ(ui_colour_mix(31, 31 << 10, 64), 23 | 8 << 10);
	// palette.json's tints are card mixed with pill_mix of base: the same
	// sums give them back (to within a step of the JSON's own rounding)
	for (u32 t = 0; t < PAL_THEMES; t++)
		for (u32 i = 0; i < PAL_ITEMS; i++) {
			const struct pal_accent *a = &themes[t]->accent[i];
			u16 mix = ui_colour_mix(themes[t]->surface->card, a->base, themes[t]->pill_mix);
			for (u32 shift = 0; shift < 15; shift += 5) {
				int d = (int)((mix >> shift) & 31) - (int)((a->tint >> shift) & 31);
				CHECK(d >= -1 && d <= 1);
			}
		}
}

void test_theme_banks_neutral(void)
{
	u16 pal[UI_BANKS * 16];
	for (u32 t = 0; t < PAL_THEMES; t++) {
		const struct pal_surfaces *s = themes[t]->surface;
		CHECK(s == &pal_surfaces[t]);
		for (u32 item = 0; item < PAL_ITEMS; item++) {
			ui_theme_banks(themes[t], item, pal);
			CHECK_EQ(pal[0], s->bg);  // the backdrop
			// The neutrals, the same in every bank
			for (u32 b = 0; b < UI_BANKS; b++) {
				const u16 *bank = pal + b * 16;
				CHECK_EQ(bank[UI_ROLE_BG], s->bg);
				CHECK_EQ(bank[UI_ROLE_PANEL], s->card);
				CHECK_EQ(bank[UI_ROLE_BORDER], s->edge);
				CHECK_EQ(bank[UI_ROLE_SHADOW], s->shadow);
				CHECK_EQ(bank[UI_ROLE_TRACK], s->soft);
				CHECK_EQ(bank[UI_ROLE_TEXT], s->ink);
				CHECK_EQ(bank[UI_ROLE_DIM], s->ink2);
				CHECK_EQ(bank[UI_ROLE_FAINT], s->ink3);
				for (u32 i = 0; i < 16; i++)
					CHECK(!(bank[i] & 0x8000));
			}
		}
	}
	// The themes differ, the layout doesn't
	u16 light[UI_BANKS * 16], dark[UI_BANKS * 16];
	ui_theme_banks(&ui_theme_light, PAL_SD, light);
	ui_theme_banks(&ui_theme_dark, PAL_SD, dark);
	CHECK(light[UI_ROLE_BG] != dark[UI_ROLE_BG]);
	CHECK(!memcmp(&ui_theme_light.look, &ui_theme_dark.look, sizeof(ui_theme_light.look)));
	CHECK(!memcmp(&ui_theme_light.motion, &ui_theme_dark.motion, sizeof(ui_theme_light.motion)));
}

void test_theme_banks_item(void)
{
	u16 pal[UI_BANKS * 16];
	for (u32 t = 0; t < PAL_THEMES; t++) {
		const struct ui_theme *th = themes[t];
		const struct pal_surfaces *s = th->surface;
		for (u32 item = 0; item < PAL_ITEMS; item++) {
			const struct pal_accent *a = &pal_accents[t][item];
			ui_theme_banks(th, item, pal);
			const u16 *normal = pal;
			// Bank 0 holds the item's accent
			CHECK_EQ(normal[UI_ROLE_ACCENT], a->base);
			CHECK_EQ(normal[UI_ROLE_TINT], a->tint);
			CHECK_EQ(normal[UI_ROLE_ACCENT_INK], a->ink);
			CHECK_EQ(normal[UI_ROLE_ACCENT_TEXT], a->on);
			CHECK_EQ(normal[UI_ROLE_DANGER], pal_accents[t][PAL_DANGER].ink);
			CHECK_EQ(normal[UI_ROLE_GROUND], ui_colour_mix(s->bg, a->base, th->backdrop_mix));
			CHECK_EQ(normal[UI_ROLE_FLOAT], ui_colour_mix(s->card, s->ink, th->float_mix));
			// Bank 1's backdrop: the ground above the horizon, a shade darker below
			const u16 *consoles = pal + 16;
			CHECK_EQ(consoles[UI_ROLE_SKY], normal[UI_ROLE_GROUND]);
			CHECK_EQ(consoles[UI_ROLE_FLOOR], ui_floor_colour(th, normal[UI_ROLE_GROUND]));
			// Darker in light, lighter in dark: a horizon shows in both
			if (t == PAL_LIGHT)
				CHECK(luminance(consoles[UI_ROLE_FLOOR]) < luminance(consoles[UI_ROLE_SKY]));
			else
				CHECK(luminance(consoles[UI_ROLE_FLOOR]) > luminance(consoles[UI_ROLE_SKY]));
			// Switching item changes only bank 0's accent indices and bank 1's backdrop
			u16 other[UI_BANKS * 16];
			ui_theme_banks(th, (item + 1) % PAL_ITEMS, other);
			for (u32 i = 0; i < UI_BANKS * 16; i++) {
				u32 b = i / 16, role = i % 16;
				int accent = role >= UI_ROLE_ACCENT && role <= UI_ROLE_GROUND;
				int backdrop = role == UI_ROLE_SKY || role == UI_ROLE_FLOOR;
				if ((b != UI_BANK_NORMAL || !accent) && (b != UI_BANK_CONSOLES || !backdrop))
					CHECK_EQ(other[i], pal[i]);
			}
		}
		// Bank 1: the console inks; bank 2: danger and the NOR bar
		ui_theme_banks(th, PAL_SD, pal);
		const u16 *consoles = pal + 16, *alert = pal + 32;
		CHECK_EQ(consoles[UI_ROLE_INK_GBA], pal_accents[t][PAL_GBA].ink);
		CHECK_EQ(consoles[UI_ROLE_INK_GBC], pal_accents[t][PAL_GBC].ink);
		CHECK_EQ(consoles[UI_ROLE_INK_GB], pal_accents[t][PAL_GB].ink);
		CHECK_EQ(consoles[UI_ROLE_INK_NES], pal_accents[t][PAL_NES].ink);
		CHECK_EQ(consoles[UI_ROLE_INK_FAV], pal_accents[t][PAL_FAV].ink);
		CHECK_EQ(alert[UI_ROLE_ALERT_INK], pal_accents[t][PAL_DANGER].ink);
		CHECK_EQ(alert[UI_ROLE_ALERT_TINT], pal_accents[t][PAL_DANGER].tint);
		CHECK_EQ(alert[UI_ROLE_NOR_FILL], pal_accents[t][PAL_NOR].base);
		CHECK_EQ(alert[UI_ROLE_NOR_INK], pal_accents[t][PAL_NOR].ink);
		// The bar's shades step from base towards the card
		CHECK(luminance(alert[UI_ROLE_NOR_FILL]) < luminance(alert[UI_ROLE_NOR_FILL_MID]) || t == PAL_DARK);
		CHECK(alert[UI_ROLE_NOR_FILL_MID] != alert[UI_ROLE_NOR_FILL_LIGHT]);
	}
	// An item past the list gets the default's colours
	u16 def[UI_BANKS * 16];
	ui_theme_banks(&ui_theme_dark, PAL_ITEMS + 5, pal);
	ui_theme_banks(&ui_theme_dark, UI_ITEM_DEFAULT, def);
	CHECK(!memcmp(pal, def, sizeof(pal)));
}

// The roles drawn as text or marks, by bank. Keep it in step with the
// widgets: tests/host.sh also fails when a node's or row's ink is a fill
// (ACCENT, TINT, GROUND, NOR_FILL*).
static const u8 text_roles[UI_BANKS][8] = {
	[UI_BANK_NORMAL] = {UI_ROLE_TEXT, UI_ROLE_DIM, UI_ROLE_FAINT, UI_ROLE_ACCENT_INK, UI_ROLE_ACCENT_TEXT,
			    UI_ROLE_DANGER},
	[UI_BANK_CONSOLES] = {UI_ROLE_TEXT, UI_ROLE_DIM, UI_ROLE_FAINT, UI_ROLE_INK_GBA, UI_ROLE_INK_GBC,
			      UI_ROLE_INK_GB, UI_ROLE_INK_NES, UI_ROLE_INK_FAV},
	[UI_BANK_ALERT] = {UI_ROLE_TEXT, UI_ROLE_DIM, UI_ROLE_FAINT, UI_ROLE_ALERT_INK, UI_ROLE_NOR_INK},
};

void test_theme_text_contrast(void)
{
	u16 pal[UI_BANKS * 16];
	for (u32 t = 0; t < PAL_THEMES; t++) {
		const struct pal_surfaces *s = themes[t]->surface;
		for (u32 item = 0; item < PAL_ITEMS; item++) {
			const struct pal_accent *a = &pal_accents[t][item];
			ui_theme_banks(themes[t], item, pal);
			// A base too light for text on a card (Recent, NOR and Info in
			// the light theme) is never a text colour, in any bank
			if (contrast(a->base, s->card) < 300)
				for (u32 b = 0; b < UI_BANKS; b++)
					for (u32 i = 0; i < 8 && text_roles[b][i]; i++)
						CHECK(pal[b * 16 + text_roles[b][i]] != a->base);
			// Text and marks read on cards, the paper and the pill (WCAG AA,
			// to the palette's own rounding)
			CHECK(contrast(a->ink, s->card) >= 450);
			CHECK(contrast(a->ink, s->bg) >= 450);
			CHECK(contrast(s->ink, a->tint) >= 450);
			CHECK(contrast(s->ink2, a->tint) >= 300);
			// Buttons: their label on the base fill, as palette.json has it
			CHECK(contrast(a->on, a->base) >= 420);
		}
		CHECK(contrast(s->ink, s->card) >= 700);
		CHECK(contrast(s->ink2, s->card) >= 450);
		CHECK(contrast(s->ink2, s->bg) >= 450);
		// B9's hint bar: labels in ink3 on the paper (and secondary text
		// in it on cards), key names in ink2 on the soft chip: 3:1
		// (B1's review found light ink3 on a card at 2.98)
		CHECK(contrast(s->ink3, s->bg) >= 300);
		CHECK(contrast(s->ink3, s->card) >= 300);
		CHECK(contrast(s->ink2, s->soft) >= 300);
		// Danger text (Delete, Format) on a floating card, a popup's or a
		// confirmation's: 4.5:1 (B1's review: 4.1 in the dark theme)
		u16 float_card = ui_colour_mix(s->card, s->ink, themes[t]->float_mix);
		CHECK(contrast(themes[t]->accent[PAL_DANGER].ink, float_card) >= 450);
		// A confirmation's destructive button, chosen: the card colour on a
		// danger fill (B11), and Cancel's on-colour on the item's base
		CHECK(contrast(float_card, themes[t]->accent[PAL_DANGER].ink) >= 450);
		// Its text line and the letter card's neighbours are faint (ink3) on
		// the floating card: 3:1
		CHECK(contrast(s->ink3, float_card) >= 300);
		// An item's ink on its own tint is for marks only (8x8 icons, the
		// star, the save dot on a selected row): 3:1. Text on the pill is
		// the neutral ink (above)
		for (u32 item = 0; item < PAL_ITEMS; item++)
			CHECK(contrast(pal_accents[t][item].ink, pal_accents[t][item].tint) >= 300);
	}
	// The light theme's weak bases are the ones the spec names
	const struct pal_surfaces *light = &pal_surfaces[PAL_LIGHT];
	CHECK(contrast(pal_accents[PAL_LIGHT][PAL_RECENT].base, light->card) < 300);
	CHECK(contrast(pal_accents[PAL_LIGHT][PAL_NOR].base, light->card) < 300);
	CHECK(contrast(pal_accents[PAL_LIGHT][PAL_INFO].base, light->card) < 300);
}

void test_theme_select(void)
{
	ui_theme_select(&ui_theme_light);
	CHECK(ui_theme() == &ui_theme_light);
	ui_theme_select(0);  // the default
	CHECK(ui_theme() == &ui_theme_dark);
	ui_item_select(PAL_NOR);
	CHECK_EQ(ui_item(), PAL_NOR);
	ui_item_select(PAL_ITEMS);
	CHECK_EQ(ui_item(), UI_ITEM_DEFAULT);
	ui_item_select(UI_ITEM_DEFAULT);
	// The shared look is spec.md's
	const struct ui_look *l = &ui_theme_dark.look;
	CHECK_EQ(l->list_pitch, 14);
	CHECK_EQ(l->list_rows * l->list_pitch, 112);
	CHECK(l->hint_y + l->hint_chip_h <= 160);
	CHECK(l->pill_h <= l->list_pitch && l->pill_radius * 2 <= l->pill_h);
	CHECK(l->hint_chip_radius * 2 <= l->hint_chip_h);
	const struct ui_motions *m = &ui_theme_dark.motion;
	CHECK_EQ(m->step.frames, 10);
	CHECK_EQ(m->open.frames, 12);
	CHECK_EQ(m->open_zoom.frames + m->open_card.frames, m->open.frames);
	CHECK_EQ(m->close_card.frames + m->close_zoom.frames, 10);
	CHECK(m->open_scale > 256 && m->open_scale < 512);  // 1.9: a 64 px object fits the double-size box
	CHECK_EQ(m->popup_open.frames, 6);
	CHECK_EQ(m->theme_wipe.frames, 16);
	CHECK(m->step.ease < EASE_COUNT && m->open_card.ease < EASE_COUNT && m->popup_open.ease < EASE_COUNT &&
	      m->popup_close.ease < EASE_COUNT && m->theme_wipe.ease < EASE_COUNT && m->open.ease < EASE_COUNT &&
	      m->open_zoom.ease < EASE_COUNT && m->close_card.ease < EASE_COUNT && m->close_zoom.ease < EASE_COUNT);
}
