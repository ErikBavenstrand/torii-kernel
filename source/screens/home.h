// Home: the menu's items (screens/registry.h) on the carousel
// (screens/carousel.h). The title bar has the red torii glyph at the left
// and the clock at the right (nothing while it isn't set); the hint bar
// says LEFT/RIGHT turn and A opens. The screen takes the facing item's
// accent, and logs "home: facing WORD" each time a turn comes to rest.
#ifndef TORII_SCREENS_HOME_H
#define TORII_SCREENS_HOME_H

#include <gba_base.h>

struct ui_view;

// Pushes home listing count items (enum nav_item, in order), facing
// items[facing]; NULL if it didn't fit
struct ui_view *home_enter(const u8 *items, u32 count, u32 facing);
// The item home faces
u32 home_facing(void);
// The items or their counts changed (the library's walk found a console's
// first game): shows them again, facing face if it shows (NAV_ITEMS: the
// same item), or the nearest if it went; only counts changing redraw the
// label, and a turn goes on
void home_items(const u8 *items, u32 count, u32 face);
void home_clock(const char *text);
// The zoom that opens item's page (UI_TR_OPEN): its object as home shows
// it; 0 if home isn't up facing item, or the object doesn't show
struct ui_zoom;
int home_zoom_open(u32 item, struct ui_zoom *zoom);
// The zoom that closes it (UI_TR_CLOSE): its object as home will show it,
// facing item at rest
void home_zoom_close(u32 item, struct ui_zoom *zoom);

// screens/nav.c: home's A, which opens item's page after the frame
void nav_open(u32 item);

#endif
