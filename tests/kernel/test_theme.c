// SD themes (source/ui/ui_sdtheme.c) on the emulated cart: loading, the
// refusals and fallbacks, listing, the choice file, and the wallpaper's
// upload and reclaim. They write under /torii/themes and change the loaded SD
// theme, so they run only in tests/assets.sh (asset_tests in run.c), never in
// Diagnostics on a user's card.
#include <stdio.h>
#include <string.h>

#include "torii_test.h"
#include "tests.h"
#include "ff.h"
#include "../../source/Ezcard_OP.h"
#include "../../source/crc32.h"
#include "../../source/engine/engine.h"
#include "../../source/engine/stage.h"
#include "../../source/ui/sdtheme.h"

extern FATFS EZcardFs;

static int write_file(const char *path, const void *data, u32 len)
{
	// f_write from word-aligned RAM
	memcpy(stage_buffer(), data, len);
	FIL f;
	UINT done = 0;
	if (f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
		return 0;
	int ok = f_write(&f, stage_buffer(), len, &done) == FR_OK && done == len;
	return (f_close(&f) == FR_OK) & ok;
}

static int write_text(const char *path, const char *text)
{
	return write_file(path, text, (u32)strlen(text));
}

static void make_theme(const char *name, const char *ini)
{
	char path[64];
	f_mkdir("/torii");
	f_mkdir(THEME_DIR);
	snprintf(path, sizeof(path), THEME_DIR "/%s", name);
	f_mkdir(path);
	snprintf(path, sizeof(path), THEME_DIR "/%s/theme.ini", name);
	CHECK(write_text(path, ini));
}

static void remove_theme(const char *name, const char *file)
{
	char path[64];
	if (file) {
		snprintf(path, sizeof(path), THEME_DIR "/%s/%s", name, file);
		f_unlink(path);
	}
	snprintf(path, sizeof(path), THEME_DIR "/%s/theme.ini", name);
	f_unlink(path);
	snprintf(path, sizeof(path), THEME_DIR "/%s", name);
	f_unlink(path);
}

static int contains(const char *s, const char *part)
{
	return strstr(s, part) != 0;
}

void test_theme_load(void)
{
	if (!EZcardFs.fs_type && f_mount(&EZcardFs, "", 1) != FR_OK) {
		CHECK(tlog_in_emulator());
		tlog("  (no card: skipped)");
		return;
	}
	res_errors_quiet = 1;
	// A good theme with a wallpaper
	make_theme("t-good", "version = 1\nname = Good\nbase = light\n[colours]\nbg = #FF0000\n"
			     "[wallpaper]\nfile = w.tas\n[patches]\nui-demo.4.x = 16\n");
	CHECK(write_file(THEME_DIR "/t-good/w.tas", test_asset_small, test_asset_small_bytes));
	const struct ui_theme *t = ui_theme_load("t-good");
	CHECK(t != 0 && t == ui_theme_sd());
	if (t) {
		CHECK(!strcmp(t->name, "Good"));
		CHECK_EQ(t->colour[UI_C_BG], 31);
		CHECK_EQ(t->colour[UI_C_TEXT], ui_theme_light.colour[UI_C_TEXT]);
		CHECK_EQ(t->patch_count, 1);
	}
	CHECK_EQ(ui_theme_warnings(), 0);
	CHECK(!strcmp(ui_theme_sd_folder(), "t-good"));

	// Refused: the theme loaded before stays
	CHECK(ui_theme_load("t-missing") == 0);
	CHECK(contains(ui_theme_error(), "has no theme.ini"));
	CHECK(ui_theme_load("../torii") == 0);
	CHECK(ui_theme_load("") == 0);
	make_theme("t-v2", "version = 2\n");
	CHECK(ui_theme_load("t-v2") == 0);
	CHECK(contains(ui_theme_error(), "version 2"));
	CHECK(ui_theme_sd() == t && !strcmp(ui_theme_sd_folder(), "t-good"));
	// A broken wallpaper, and an 8bpp one, refuse the theme
	make_theme("t-broken", "version = 1\n[wallpaper]\nfile = w.tas\n");
	static const u32 junk[4] = {0x31534154, 0x00FF0004, 0, 0};
	CHECK(write_file(THEME_DIR "/t-broken/w.tas", junk, sizeof(junk)));
	CHECK(ui_theme_load("t-broken") == 0);
	CHECK(contains(ui_theme_error(), "wallpaper w.tas"));
	u32 wide[17];
	memcpy(wide, test_asset_small, sizeof(wide));
	wide[1] = 0x00020108;  // 8bpp, 1x2 tiles: the same 128 bytes decoded
	CHECK(write_file(THEME_DIR "/t-broken/w.tas", wide, test_asset_small_bytes));
	CHECK(ui_theme_load("t-broken") == 0);
	CHECK(contains(ui_theme_error(), "must be 4bpp"));
	CHECK(ui_theme_sd() == t);
	// A missing wallpaper is only a warning
	make_theme("t-nowall", "version = 1\n[wallpaper]\nfile = gone.tas\n[layout]\npad = 99\n");
	CHECK(ui_theme_load("t-nowall") != 0);
	CHECK_EQ(ui_theme_warnings(), 2);
	CHECK_EQ(ui_theme_sd()->pad, 16);

	// The list: sorted, names that can't be themes left out
	make_theme("t-a", "version = 1\n");
	f_mkdir(THEME_DIR "/.t-hidden");
	char names[16][THEME_NAME_MAX];
	u32 n = ui_theme_list(names, 16);
	u32 found = 0;
	for (u32 i = 0; i < n; i++) {
		CHECK(names[i][0] != '.');
		if (i)
			CHECK(strcmp(names[i - 1], names[i]) < 0);
		found += !strcmp(names[i], "t-a") || !strcmp(names[i], "t-good") || !strcmp(names[i], "t-v2");
	}
	CHECK_EQ(found, 3);
	CHECK_EQ(ui_theme_list(names, 1), 1);
	f_unlink(THEME_DIR "/.t-hidden");

	// The choice file
	char name[THEME_NAME_MAX];
	CHECK(ui_theme_choice_write("t-good"));
	CHECK(ui_theme_choice_read(name) && !strcmp(name, "t-good"));
	CHECK(write_text(THEME_CHOICE_FILE, "\xEF\xBB\xBF  t-a \r\nignored\n"));
	CHECK(ui_theme_choice_read(name) && !strcmp(name, "t-a"));
	CHECK(write_text(THEME_CHOICE_FILE, "../x\n"));
	CHECK(!ui_theme_choice_read(name));
	CHECK(!ui_theme_choice_write("a/b"));
	f_unlink(THEME_CHOICE_FILE);
	CHECK(!ui_theme_choice_read(name));

	// The wallpaper: uploaded cached, shown, and dropped once reclaimed
	CHECK(ui_theme_load("t-good") != 0);
	gfx_init();
	res_init();
	ui_theme_select(ui_theme_sd(), UI_ACCENT_THEME);
	// As a dialog's ui_init can run it, with another PSRAM page left selected by
	// a PSRAM loader: it selects page 0, where the file is
	SetPSRampage(0x1000);
	ui_wallpaper_setup();
	u16 bgcnt = 0;
	CHECK(ui_wallpaper_bgcnt(&bgcnt));
	CHECK_EQ(bgcnt & 3, 3);
	const u32 *tiles = (const u32 *)(0x06000000 + ((bgcnt >> 2) & 3) * 0x4000u);
	const u16 *map = (const u16 *)(0x06000000 + (bgcnt >> 8 & 31) * 0x800u);
	tiles += (map[0] & 0x3FF) * 8;
	CHECK_EQ(crc32(tiles, 128), test_asset_small_crc);
	CHECK_EQ(map[1] & 0x3FF, (map[0] & 0x3FF) + 1);   // 2 tiles wide, repeated
	CHECK_EQ(map[2] & 0x3FF, map[0] & 0x3FF);
	CHECK_EQ(map[32] & 0x3FF, (map[0] & 0x3FF) + 2);  // the next row
	// Where (T18): only character block 0 reaches it, so lists on planes 1
	// and 3 and every map get all the rest of BG VRAM with it still up
	CHECK_EQ((bgcnt >> 2) & 3, 0);
	CHECK((bgcnt >> 8 & 31) < UI_WALL_VRAM_END / RES_MAP_UNITS);
	CHECK((map[0] & 0x3FF) < UI_WALL_VRAM_END);
	u8 rest = res_alloc(RES_BG_VRAM, RES_BG_UNITS - UI_WALL_VRAM_END, 1, UI_WALL_VRAM_END, RES_BG_UNITS, 0,
			    RES_NOLOG);
	CHECK(rest != RES_NONE);
	CHECK(ui_wallpaper_bgcnt(&bgcnt));
	res_unref(RES_BG_VRAM, rest);
	// Cached: the budget sees the room as free, and taking it all reclaims it
	CHECK_EQ(pool_free_units(res_pool(RES_BG_VRAM), 1), RES_BG_UNITS);
	u8 all = res_alloc(RES_BG_VRAM, RES_BG_UNITS, 1, 0, RES_BG_UNITS, 0, RES_NOLOG);
	CHECK(all != RES_NONE);
	CHECK(!ui_wallpaper_bgcnt(&bgcnt));
	res_unref(RES_BG_VRAM, all);
	// Another theme current: none
	ui_wallpaper_setup();
	CHECK(ui_wallpaper_bgcnt(&bgcnt));
	ui_theme_select(&ui_theme_dark, UI_ACCENT_THEME);
	CHECK(!ui_wallpaper_bgcnt(&bgcnt));
	gfx_init();
	res_init();

	remove_theme("t-good", "w.tas");
	remove_theme("t-broken", "w.tas");
	remove_theme("t-v2", 0);
	remove_theme("t-nowall", 0);
	remove_theme("t-a", 0);
	res_errors_quiet = 0;
}
