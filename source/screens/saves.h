// The global save manager's screen, Clean up (Settings > Saves > Clean up
// saves; the card side is saveorphan.h): the space the saves and their
// backups use, the saves without a game (the folders of games whose ROM is no
// longer where they were), and Empty trash. A on a save without a game opens
// a popup: Move to a game (a list of the library's games, the ones with its
// file name first, then its console's, then the rest; A on one asks, then
// moves the save and backup folders there, saveorphan_relink) and Delete (to
// the trash for 30 days, asks first). Empty trash asks; with nothing in the
// trash it is dimmed and A does nothing.
//
// Keys: UP/DOWN and L/R move, A opens a row, B goes back (from the picker to
// the list, from the list to Settings).
#ifndef TORII_SCREENS_SAVES_H
#define TORII_SCREENS_SAVES_H

// Walks the card (a progress dialog), then shows the screen in the menu's
// session in place of the page that opened it, and returns when B leaves it,
// with no view up. Call it from a page's deferred action; the page shows
// itself again (nav_reshow).
void saves_run(void);

#endif
