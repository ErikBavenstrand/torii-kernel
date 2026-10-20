// Home: the menu's items (screens/registry.h) as a plain list on a card,
// until B12's carousel replaces it. The title bar has the torii glyph and
// the clock; UP/DOWN pick an item, whose accent the screen takes, and A
// opens it.
#ifndef TORII_SCREENS_HOME_H
#define TORII_SCREENS_HOME_H

#include <gba_base.h>

struct ui_view;

// Pushes home listing count items (enum nav_item, in order), facing
// items[facing]; NULL if it didn't fit
struct ui_view *home_enter(const u8 *items, u32 count, u32 facing);
// The item home faces
u32 home_facing(void);
// The items changed while home is up (the library's walk found a console's
// first game): lists them again, facing the same item, or the nearest if
// it went
void home_items(const u8 *items, u32 count);
void home_clock(const char *text);

// screens/nav.c: home's A, which opens item's page after the frame
void nav_open(u32 item);

#endif
