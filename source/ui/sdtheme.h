// Themes from the SD card (v0.3.1): /torii/themes/<name>/theme.ini (parsed by
// ui/theme_ini.h) plus its assets, today an optional wallpaper. Kernel only.
//
// One SD theme is loaded at a time, into EWRAM (a struct ui_theme and its
// name), with its wallpaper file kept in PSRAM. The THEME setting picks it
// with UI_THEME_SD; the theme's folder name is kept on the card in
// /torii/theme.txt, so nothing new is written to the kernel NOR.
//
// Loading uses FatFs (engine/sdread.h steps, run to the end in one call): call
// it where the kernel's ROM is mapped and no other FatFs call or
// tlog_flush is under way, never from an IRQ. It borrows pReadCache for the
// call (READCACHE_STAGE and READCACHE_THEME, source/ez_define.h).
#ifndef TORII_UI_SDTHEME_H
#define TORII_UI_SDTHEME_H

#include <gba_base.h>

#include "theme_ini.h"

#define THEME_DIR "/torii/themes"
#define THEME_CHOICE_FILE "/torii/theme.txt"
// The THEME setting's value (settings slot assress_theme) for the SD theme
// named in THEME_CHOICE_FILE; 1 is the light theme, anything else dark
#define UI_THEME_SD 2

// Loads THEME_DIR/name/ and makes it the SD theme: theme.ini, then its
// wallpaper (missing: none). Returns the theme, or NULL with the reason in
// ui_theme_error() (a missing folder or theme.ini, a version other than 1,
// a wallpaper that is broken, not 4bpp or over budget); the SD theme loaded
// before, if any, stays. Warnings (keys ignored or clamped) are logged and
// counted in ui_theme_warnings(). Takes some tens of ms.
// To show it, ui_theme_set(theme, accent) or ui_init: the SD theme in use
// before stays unchanged until then (two buffers alternate), but its
// wallpaper goes off.
const struct ui_theme *ui_theme_load(const char *name);
// The loaded SD theme, or NULL.
const struct ui_theme *ui_theme_sd(void);
// The loaded SD theme's folder name ("" none).
const char *ui_theme_sd_folder(void);
// Why the last ui_theme_load or ui_theme_boot failed, or what the last
// screen push that fell back to a built-in theme hit (a sentence, "" none).
const char *ui_theme_error(void);
// Warnings the last successful ui_theme_load logged.
u32 ui_theme_warnings(void);

// Fills names with the first cap folder names found in THEME_DIR that can
// be themes (theme_name_ok; others are skipped), sorted bytewise. Returns how
// many; 0 if the folder is missing. Whether each holds a valid theme.ini
// is only known once loaded.
u32 ui_theme_list(char (*names)[THEME_NAME_MAX], u32 cap);

// The theme name THEME_CHOICE_FILE holds (its first line, trimmed) into
// name; 0 if there is none or it isn't a valid name.
int ui_theme_choice_read(char name[THEME_NAME_MAX]);
// Writes name to THEME_CHOICE_FILE (making /torii if needed); 0 on failure.
// The settings screen calls it, then stores UI_THEME_SD in the THEME slot.
int ui_theme_choice_write(const char *name);

// At boot, once the card is mounted: if the THEME setting is UI_THEME_SD,
// loads the theme THEME_CHOICE_FILE names. Returns 1 if there was nothing to
// do or it loaded; 0 if it failed (the reason in ui_theme_error(); the menu
// then uses the dark theme).
int ui_theme_boot(void);

// --- For ui/ui_run.c ---------------------------------------------------------

// The wallpaper's map and tiles go at the top of BG VRAM units
// [0, UI_WALL_VRAM_END), under its map: the part only character block 0
// reaches. Lists on planes 1 and 3 take their tiles from blocks 1 and 2
// (units 512 up) and maps come from the top of VRAM, so neither ever needs
// that room, and the wallpaper never moves their blocks either (T18: put
// among the maps, it pushed every view's maps down a screenblock and the
// game menu's plane 3 list no longer fit). Only planes 0 and 2 share it,
// taking tiles from the bottom up.
#define UI_WALL_VRAM_END 512

// Uploads the wallpaper of the current theme (if it has one and it isn't
// up already): ui_init and ui_theme_set call it, and the frame after a view
// is popped (once nothing popped shows). Its tiles, map and palette bank are
// left cached (engine/res.h), so they never count against a screen's
// budget: a screen whose planes 0 and 2 need the room reclaims them, and the
// wallpaper is off until a view is popped and it fits again.
void ui_wallpaper_setup(void);
// 1 if the wallpaper can show on plane 3 now, with its BGCNT in *bgcnt.
int ui_wallpaper_bgcnt(u16 *bgcnt);
// When a screen fails to push while the SD theme is current, ui_push tries
// it again in the theme's base built-in theme (this returns it; NULL if the
// SD theme isn't current). If that works, the menu stays in the base theme
// and ui_theme_fell_back(why) records it; if not, the theme wasn't the cause
// and ui_push puts the SD theme back.
const struct ui_theme *ui_theme_fallback_base(void);
void ui_theme_fell_back(const char *why);

#endif
