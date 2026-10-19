// HELP (settings.h): the menu's key combinations, the firmware version, the
// online manual's QR code and the credits. L goes back to SETTINGS page 2.
#include <stdio.h>
#include <gba_base.h>

#include "ez_define.h"
#include "lang.h"
#include "ezkernel.h"
#include "Ezcard_OP.h"
#include "helpwindow.h"
#include "settings.h"

static struct {
	char version[20];
	char help[160];
	char credits[96];
} help EWRAM_BSS;

static void help_enter(struct ui_view *view)
{
	u16 fpga = Read_FPGA_ver();
	snprintf(help.version, sizeof(help.version), "FW:%d K:1.06", fpga & 0xFF);
	u16 rev = fpga & 0xF000;
	snprintf(help.help, sizeof(help.help), "%s\n%s\n%s\n%s", gl_START_help, gl_SELECT_help, gl_L_A_help,
		 gl_LSTART_help);
	// The fonts' licence (OFL-1.1) asks for the credit
	snprintf(help.credits, sizeof(help.credits),
		 "Galmuri font \xC2\xA9 Lee Minseo,\nSIL Open Font License 1.1\nRev.%s",
		 ((rev == 0xB000) || (rev == 0xA000)) ? "B" : "A");
	settings_header(view, gl_help_title, help.version);
	ui_label_set((u8)(view->first + 4), help.help);
	ui_label_set((u8)(view->first + 5), gl_online_manual);
	ui_label_set((u8)(view->first + 7), help.credits);
}

static int help_event(struct ui_view *view, const struct ui_event *e)
{
	(void)view;
	if (e->type == UI_EV_KEYS && (settings_keys(e) & INPUT_L)) {  //return
		settings_open(SETTINGS_PAGE2);
		return 1;
	}
	return 0;
}

static const struct ui_label_config keys_config = {"START\nSELECT\nL + A\nL+START", UI_FONT_BODY, UI_ALIGN_LEFT, 0};
static const struct ui_label_config text_config = {"", UI_FONT_BODY, UI_ALIGN_LEFT, 0};
static const struct ui_label_config manual_config = {"", UI_FONT_BODY, UI_ALIGN_CENTER, 0};
static const u16 qr_palette[16] = {0, 0x7FFF, 0x0000};
static const struct ui_image_config qr_config = {help_qr_tiles, qr_palette, 0x4851};  // "HQ"

static const struct ui_spec help_nodes[] = {
	SETTINGS_HEADER,
	{.kind = UI_LABEL,
	 .id = 4,
	 .parent = UI_NONE,
	 .plane = 1,
	 .ink = UI_ROLE_ACCENT,
	 .x = 0,
	 .y = 24,
	 .w = 56,
	 .h = 48,
	 .data = &keys_config},
	{.kind = UI_LABEL,
	 .id = 5,
	 .parent = UI_NONE,
	 .plane = 1,
	 .x = 56,
	 .y = 24,
	 .w = 184,
	 .h = 48,
	 .data = &text_config},
	{.kind = UI_LABEL,
	 .id = 6,
	 .parent = UI_NONE,
	 .plane = 1,
	 .x = 160,
	 .y = 64,
	 .w = 80,
	 .h = 16,
	 .data = &manual_config},
	{.kind = UI_IMAGE,
	 .id = 7,
	 .parent = UI_NONE,
	 .plane = 1,
	 .x = 160,
	 .y = 80,
	 .w = 80,
	 .h = 80,
	 .data = &qr_config},
	{.kind = UI_LABEL,
	 .id = 8,
	 .parent = UI_NONE,
	 .plane = 1,
	 .ink = UI_ROLE_DIM,
	 .x = 0,
	 .y = 120,
	 .w = 160,
	 .h = 40,
	 .data = &text_config},
};

static const struct ui_view_spec help_view = {
	"help", help_nodes, sizeof(help_nodes) / sizeof(help_nodes[0]), 0, UI_NONE, 0, help_event};

const struct settings_page settings_help = {&help_view, help_enter, 0, 0, 0, 0};
