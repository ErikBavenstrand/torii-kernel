// The built-in themes and accents, and theme maths (ui.h). Hardware-free:
// ui/ui_run.c loads the palette this builds.
#include "ui.h"

#define RGB(r, g, b) ((u16)((r) | (g) << 5 | (b) << 10))

// Icon colours, shared by the built-ins: folder, GBA, GB, NES, light, dark
// Screen transitions, the same in both
#define TRANSITIONS \
	{ \
		[UI_TR_TAB] = {UI_TR_FADES | UI_TR_SLIDE_X, 3, 8, 16}, \
		[UI_TR_DEEPER] = {UI_TR_FADES | UI_TR_SLIDE_Y, 3, 8, 12}, \
		[UI_TR_FADE] = {UI_TR_FADES, 3, 6, 0}, \
		[UI_TR_LEAVE] = {UI_TR_FADES, 3, 8, 0}, \
	}

#define ICONS RGB(31, 24, 8), RGB(20, 14, 31), RGB(12, 26, 12), RGB(30, 10, 10), RGB(31, 31, 31), RGB(4, 4, 6)

const struct ui_theme ui_theme_dark = {
	.name = "Dark",
	.colour =
		{
			[UI_C_BG] = RGB(3, 3, 4),
			[UI_C_PANEL] = RGB(6, 7, 9),
			[UI_C_TEXT] = RGB(29, 29, 30),
			[UI_C_DIM] = RGB(16, 17, 20),
			[UI_C_ACCENT] = RGB(9, 19, 31),
			[UI_C_ACCENT_TEXT] = RGB(31, 31, 31),
			[UI_C_BORDER] = RGB(11, 12, 15),
			[UI_C_TRACK] = RGB(13, 14, 18),
			[UI_C_SHADOW] = RGB(1, 1, 2),
			[UI_C_SELECTION] = RGB(9, 19, 31),
			[UI_C_SELECTION_TEXT] = RGB(31, 31, 31),
			ICONS,
		},
	.body = &font_body,
	.heading = &font_heading,
	.row_h = 16,
	.pad = 6,
	.modal_dim = 8,
	.idle_dim = 11,
	.fade_frames = 8,
	.slide = 24,
	.marquee_delay = 45,
	.marquee_speed = 60,
	.marquee_pause = 60,
	.marquee_gap = 8,
	.page_fade = 4,
	.dialog_in = 12,
	.dialog_out = 10,
	.transition = TRANSITIONS,
	.input = {15, 4, 60, 2, 1, 20, 4, 15},
};

const struct ui_theme ui_theme_light = {
	.name = "Light",
	.colour =
		{
			[UI_C_BG] = RGB(28, 28, 29),
			[UI_C_PANEL] = RGB(31, 31, 31),
			[UI_C_TEXT] = RGB(3, 3, 5),
			[UI_C_DIM] = RGB(13, 14, 16),
			[UI_C_ACCENT] = RGB(4, 13, 28),
			[UI_C_ACCENT_TEXT] = RGB(31, 31, 31),
			[UI_C_BORDER] = RGB(21, 22, 24),
			[UI_C_TRACK] = RGB(23, 24, 26),
			[UI_C_SHADOW] = RGB(19, 19, 21),
			[UI_C_SELECTION] = RGB(4, 13, 28),
			[UI_C_SELECTION_TEXT] = RGB(31, 31, 31),
			ICONS,
		},
	.body = &font_body,
	.heading = &font_heading,
	.row_h = 16,
	.pad = 6,
	.modal_dim = 6,
	.idle_dim = 11,
	.fade_frames = 8,
	.slide = 24,
	.marquee_delay = 45,
	.marquee_speed = 60,
	.marquee_pause = 60,
	.marquee_gap = 8,
	.page_fade = 4,
	.dialog_in = 12,
	.dialog_out = 10,
	.transition = TRANSITIONS,
	.input = {15, 4, 60, 2, 1, 20, 4, 15},
};

const struct ui_accent ui_accents[] = {
	{"Blue", RGB(9, 19, 31)}, {"Teal", RGB(4, 22, 22)},    {"Green", RGB(8, 22, 10)}, {"Orange", RGB(31, 15, 4)},
	{"Red", RGB(28, 7, 8)},   {"Purple", RGB(18, 10, 28)}, {"Pink", RGB(30, 11, 20)},
};
const u32 ui_accent_count = sizeof(ui_accents) / sizeof(ui_accents[0]);

static struct {
	const struct ui_theme *theme;
	u16 accent;
} current EWRAM_BSS;

void ui_theme_select(const struct ui_theme *theme, u16 accent)
{
	current.theme = theme ? theme : &ui_theme_dark;
	current.accent = accent;
}

const struct ui_theme *ui_theme(void)
{
	return current.theme ? current.theme : &ui_theme_dark;
}

u16 ui_accent(void)
{
	return current.theme ? current.accent : UI_ACCENT_THEME;
}

void ui_theme_palette(const struct ui_theme *theme, u16 accent, u16 out[32])
{
	u16 c[UI_COLOURS];
	for (u32 i = 0; i < UI_COLOURS; i++)
		c[i] = theme->colour[i] & 0x7FFF;
	if (accent != UI_ACCENT_THEME) {
		c[UI_C_ACCENT] = accent & 0x7FFF;
		c[UI_C_SELECTION] = accent & 0x7FFF;
	}
	u16 *normal = out, *selected = out + 16;
	normal[0] = c[UI_C_BG];  // the backdrop
	for (u32 role = UI_ROLE_BG; role < UI_ROLES; role++)
		normal[role] = c[role - UI_ROLE_BG];
	for (u32 i = 0; i < 6; i++)
		normal[UI_ROLE_ICON + i] = c[UI_C_ICON + i];
	for (u32 i = 0; i < 16; i++)
		selected[i] = normal[i];
	selected[UI_ROLE_BG] = selected[UI_ROLE_PANEL] = c[UI_C_SELECTION];
	selected[UI_ROLE_TEXT] = selected[UI_ROLE_ACCENT] = c[UI_C_SELECTION_TEXT];
	selected[UI_ROLE_ACCENT_TEXT] = c[UI_C_SELECTION];
	// Dim text on the selection: a third of the way to the selection colour
	palette_fade(&selected[UI_ROLE_DIM], &c[UI_C_SELECTION_TEXT], 1, c[UI_C_SELECTION], 88);
}

int ui_theme_patch(const struct ui_theme *theme, u8 view, u8 node, u8 field, s16 *value)
{
	int found = 0;
	u32 count = theme->patch_count < UI_PATCH_CAP ? theme->patch_count : UI_PATCH_CAP;
	for (u32 i = 0; i < count; i++) {
		const struct ui_patch *p = &theme->patches[i];
		if (p->view == view && p->node == node && p->field == field && field < UI_FIELDS) {
			*value = p->value;
			found = 1;
		}
	}
	return found;
}
