// The Info page (v0.4 decisions, Pages; ticket B17; mockups #21 and #22):
// rows for Controls, In game, Your cart and About, each opening a reading
// page (ui/ui_reading.h) that UP and DOWN scroll and B leaves, and a row
// that runs Diagnostics, which comes back here on B.
//
// The texts are en_* strings (lang.c, gl_info). In game, Your cart and
// About are filled in when they open: the hotkeys as Settings has them, what
// the card and the NOR hold, the cart's revision and the card's free space.
#ifndef TORII_SCREENS_INFO_H
#define TORII_SCREENS_INFO_H

#include "nav.h"

extern const struct nav_page info_page;

// The strings (lang.c's en_info, gl_info): the rows' names and what each
// holds, the reading pages' texts and the parts the filled-in ones are
// made of. "%s", the only placeholder, stands for what the page fills
// in, in order.
enum info_string {
	INFO_S_CONTROLS,  // the rows' names
	INFO_S_INGAME,
	INFO_S_CART,
	INFO_S_ABOUT,
	INFO_S_DIAG,
	INFO_S_CONTROLS_DETAIL,  // what each row holds, right-aligned
	INFO_S_INGAME_DETAIL,
	INFO_S_CART_DETAIL,  // %s: the game NOR's MB
	INFO_S_ABOUT_DETAIL,  // %s: the version
	INFO_S_DIAG_DETAIL,
	INFO_S_SCROLL,  // the reading pages' hint
	INFO_S_CONTROLS_TEXT,  // Controls, whole
	// In game: the start, then by what Settings has on
	INFO_S_INGAME_START,
	INFO_S_INGAME_MENU,    // Save states with others: %s opens the in-game menu
	INFO_S_INGAME_RESET,   // %s: back to the menu
	INFO_S_INGAME_RESET_OFF,
	INFO_S_INGAME_CHEATS,  // Cheats without Save states: %s opens the menu in a game with cheats
	INFO_S_INGAME_SLEEP,   // %s: sleep
	INFO_S_INGAME_SLEEP_OFF,
	INFO_S_INGAME_END,
	// Your cart
	INFO_S_CART_START,  // %s: the game NOR's MB
	INFO_S_CART_GAMES,  // "# Games on the card"
	INFO_S_CART_CONSOLE,  // %s: a console, %s: its count ("176 games")
	INFO_S_CART_FILES,    // %s: files on the card ("1,204 files")
	INFO_S_CART_COUNTING,  // in their place while the library isn't loaded
	INFO_S_CART_NOR,      // %s: its games ("3 games"), %s MB used, %s MB in all
	INFO_S_CART_NOR_EMPTY,
	INFO_S_CART_LISTS,  // %s favourites, %s on Recent, %s: Recent's most
	INFO_S_CART_END,
	// About
	INFO_S_ABOUT_TORII,  // %s: the version, %s: the build ID
	INFO_S_ABOUT_CART,   // %s: the revision, %s: the FPGA firmware
	INFO_S_ABOUT_REV_A,  // the revisions' names
	INFO_S_ABOUT_REV_B,
	INFO_S_ABOUT_CARD,   // %s: its size, %s: free, %s: its file system
	INFO_S_ABOUT_CARD_NO_FREE,  // %s: its size, %s: its file system (no free count on the card)
	INFO_S_ABOUT_CARD_UNKNOWN,
	INFO_S_ABOUT_CREDITS,
	INFO_S_ABOUT_NAMES,  // with /torii/names.db on the card
	INFO_STRINGS
};

// What Info shows as Torii's version (the About row, the About page) and
// build ID: filled from TORII_VERSION and the build ID the first time the
// Info page opens, unless a golden wrote them first (mgba-shot -P), so its
// screens don't change with every build and release.
extern char info_version[16];
extern char info_build[12];

#endif
