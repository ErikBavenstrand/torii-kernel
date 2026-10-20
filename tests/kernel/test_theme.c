// The built-in themes on the hardware side (source/ui/ui_run.c): a theme or
// item switch loads the UI's palette banks and its sprite bank (bank 0's
// colours), and only those.
#include <string.h>

#include "torii_test.h"
#include "tests.h"
#include "../../source/engine/gfx.h"
#include "../../source/ui/ui.h"
#include "../../source/ui/ui_priv.h"

void test_theme_switch_loads_banks(void)
{
	u16 want[UI_BANKS * 16];
	gfx_init();
	gfx_dirty_pal = 0;
	memset(gfx_pal_unfaded, 0, sizeof(gfx_pal_unfaded));
	ui_theme_set(&ui_theme_light);
	CHECK(ui_theme() == &ui_theme_light);
	ui_theme_banks(&ui_theme_light, ui_item(), want);
	u32 obj = 1u << (16 + ui_obj_bank());
	CHECK(!memcmp(gfx_pal_unfaded, want, sizeof(want)));
	CHECK(!memcmp(gfx_pal_unfaded + 256 + ui_obj_bank() * 16, want, 32));
	CHECK_EQ(gfx_dirty_pal, ((1u << UI_BANKS) - 1) | obj);
	// An item: its accent, in the same theme
	gfx_dirty_pal = 0;
	ui_item_set(PAL_NOR);
	CHECK_EQ(ui_item(), PAL_NOR);
	ui_theme_banks(&ui_theme_light, PAL_NOR, want);
	CHECK(!memcmp(gfx_pal_unfaded, want, sizeof(want)));
	CHECK_EQ(gfx_pal_unfaded[UI_ROLE_ACCENT], pal_accents[PAL_LIGHT][PAL_NOR].base);
	CHECK_EQ(gfx_dirty_pal, 1u | obj);  // bank 0, and the sprites'
	CHECK_EQ(gfx_pal_unfaded[256 + ui_obj_bank() * 16 + UI_ROLE_TINT], pal_accents[PAL_LIGHT][PAL_NOR].tint);
	// Nothing past the UI's banks
	for (u32 i = UI_BANKS * 16; i < 256; i++)
		CHECK_EQ(gfx_pal_unfaded[i], 0);
	// Back to the defaults for the tests after
	ui_item_set(UI_ITEM_DEFAULT);
	ui_theme_set(&ui_theme_dark);
	gfx_init();
}
