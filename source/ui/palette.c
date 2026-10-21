// The menu's palette, written by tools/palette.py from docs/design/v0.4/palette.json:
// don't edit by hand; edit the JSON and run `just palette`.
#include "palette.h"

const struct pal_surfaces pal_surfaces[PAL_THEMES] = {
	[PAL_LIGHT] = {.bg = 0x73BE, .card = 0x7FFF, .edge = 0x6B7C, .soft = 0x633A, .ink = 0x18A5, .ink2 = 0x2D8D, .ink3 = 0x3E11, .shadow = 0x675B},
	[PAL_DARK] = {.bg = 0x1063, .card = 0x18A5, .edge = 0x1CE7, .soft = 0x2509, .ink = 0x73BD, .ink2 = 0x52B6, .ink3 = 0x39F0, .shadow = 0x0842},
};

const struct pal_accent pal_accents[PAL_THEMES][PAL_ITEMS] = {
	[PAL_LIGHT] =
		{
			[PAL_RECENT] = {.base = 0x11FD, .tint = 0x6FBF, .ink = 0x0D53, .on = 0x1063},
			[PAL_FAV] = {.base = 0x4D5C, .tint = 0x779F, .ink = 0x3D16, .on = 0x1063},
			[PAL_GBA] = {.base = 0x58E9, .tint = 0x7B7C, .ink = 0x58E9, .on = 0x7FFF},
			[PAL_GBC] = {.base = 0x4A61, .tint = 0x77BA, .ink = 0x39E1, .on = 0x1063},
			[PAL_GB] = {.base = 0x0A6D, .tint = 0x6BBC, .ink = 0x09CA, .on = 0x1063},
			[PAL_NES] = {.base = 0x1499, .tint = 0x6F7E, .ink = 0x1499, .on = 0x7FFF},
			[PAL_SD] = {.base = 0x6DE5, .tint = 0x7FBB, .ink = 0x61A4, .on = 0x7FFF},
			[PAL_NOR] = {.base = 0x0A7C, .tint = 0x6BBF, .ink = 0x0992, .on = 0x1063},
			[PAL_SET] = {.base = 0x4DEC, .tint = 0x77BC, .ink = 0x45AB, .on = 0x7FFF},
			[PAL_INFO] = {.base = 0x0ADB, .tint = 0x6BDF, .ink = 0x09B0, .on = 0x1063},
			[PAL_TORII] = {.base = 0x153C, .tint = 0x6F9F, .ink = 0x10F7, .on = 0x1063},
		},
	[PAL_DARK] =
		{
			[PAL_RECENT] = {.base = 0x11FD, .tint = 0x150C, .ink = 0x11FD, .on = 0x1063},
			[PAL_FAV] = {.base = 0x4D5C, .tint = 0x28CC, .ink = 0x517D, .on = 0x1063},
			[PAL_GBA] = {.base = 0x61AF, .tint = 0x2CE8, .ink = 0x6A33, .on = 0x7FFF},
			[PAL_GBC] = {.base = 0x4A61, .tint = 0x2523, .ink = 0x4E82, .on = 0x1063},
			[PAL_GB] = {.base = 0x0A6D, .tint = 0x1127, .ink = 0x0E8E, .on = 0x1063},
			[PAL_NES] = {.base = 0x20FA, .tint = 0x18AB, .ink = 0x41FD, .on = 0x7FFF},
			[PAL_SD] = {.base = 0x6DE5, .tint = 0x3105, .ink = 0x724A, .on = 0x7FFF},
			[PAL_NOR] = {.base = 0x0A7C, .tint = 0x112C, .ink = 0x0A7C, .on = 0x1063},
			[PAL_SET] = {.base = 0x4DEC, .tint = 0x2907, .ink = 0x5650, .on = 0x7FFF},
			[PAL_INFO] = {.base = 0x0ADB, .tint = 0x114B, .ink = 0x0ADB, .on = 0x1063},
			[PAL_TORII] = {.base = 0x153C, .tint = 0x14CC, .ink = 0x259D, .on = 0x1063},
		},
};

const u16 pal_success[PAL_THEMES] = {[PAL_LIGHT] = 0x1DE5, [PAL_DARK] = 0x32CA};
