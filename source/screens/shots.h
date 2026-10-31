// The screenshot viewer (v0.9.2, docs/design/v0.9/shot-viewer.md): a
// picture full screen, LEFT and RIGHT to the next and the one before (a
// fresh press at either end wraps), an overlay strip with its date and
// time (or its file name) and "3 of 12" that A shows or hides and that goes
// by itself after a while, START to delete it (a confirmation first), B
// back. Pictures come from BMP files (picture.h), cut to 176 colours on an
// 8bpp tile plane (UI_PICTURE).
//
// Each runs in the menu's session: it leaves the caller's screen
// (ui_leave) and pops its views, and returns with no view up and the
// viewer gone; the caller shows its page again. Borrows pReadCache whole
// (READCACHE_SHOTS) and PSRAM page 2 while it runs.
#ifndef TORII_SCREENS_SHOTS_H
#define TORII_SCREENS_SHOTS_H

#include <gba_base.h>

struct sdlist;

// game's screenshots (an SD path, or a NOR entry), newest first
void shots_run_game(const char *game);

// The .bmp files of folder as the SD card page lists them (list, its
// listing of folder), from the one named name
void shots_run_folder(const char *folder, const struct sdlist *list, const char *name);

#endif
