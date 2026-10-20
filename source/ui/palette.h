// The menu's palette, written by tools/palette.py from docs/design/v0.4/palette.json:
// don't edit by hand; edit the JSON and run `just palette`.
#ifndef TORII_UI_PALETTE_H
#define TORII_UI_PALETTE_H

#include <gba_base.h>

enum pal_theme {
	PAL_LIGHT,
	PAL_DARK,
	PAL_THEMES,
};

// The items with an accent, in palette.json's order
enum pal_item {
	PAL_RECENT,  // Recent
	PAL_FAV,  // Favourites
	PAL_GBA,  // Game Boy Advance
	PAL_GBC,  // Game Boy Color
	PAL_GB,  // Game Boy
	PAL_NES,  // NES
	PAL_SD,  // SD card
	PAL_NOR,  // NOR
	PAL_SET,  // Settings
	PAL_INFO,  // Info
	PAL_TORII,  // Torii (app)
	PAL_ITEMS,
};

// The item whose ink is the danger colour (Delete, Format, failures)
#define PAL_DANGER PAL_NES

// A theme's surfaces (RGB15)
struct pal_surfaces {
	u16 bg, card, edge, soft, ink, ink2, ink3, shadow;
};

// An item's accent in one theme (RGB15): base for icons, bars and solid
// buttons; tint for the selection pill; ink for marks and labels on the
// paper; on for text on a base fill
struct pal_accent {
	u16 base, tint, ink, on;
};

extern const struct pal_surfaces pal_surfaces[PAL_THEMES];
extern const struct pal_accent pal_accents[PAL_THEMES][PAL_ITEMS];

// How much of an accent's base its tint mixes into a card, in 256ths
#define PAL_LIGHT_PILL_MIX 44
#define PAL_DARK_PILL_MIX 77

#endif
