// The Settings page (ticket B16; docs/design/v0.4/mockups-b16-b18.md): one
// scrolling page of section cards drawn from the settings model
// (settings_model.h), the selected row's explanation in the hint bar.
//
// Keys: UP/DOWN pick a row, L/R jump a section, B goes back home. A edits
// the row: a value in place as « value » (LEFT/RIGHT change it, A keeps it,
// B puts it back), Time and Date in a field card, a hotkey by pressing the
// combination, an action (Rescan library, Identify library) runs. The theme and the LED
// preview live while edited. A value is written to the kernel NOR as it is
// confirmed (A, or a hotkey accepted), and only if it changed
// (settings_commit): one write a change, with "Saving..." in the hint bar.
#ifndef TORII_SCREENS_SETTINGS_PAGE_H
#define TORII_SCREENS_SETTINGS_PAGE_H

#include "nav.h"

// Its row (nav_page.row) is the selected row's enum settings_id
extern const struct nav_page settings_page;

#endif
