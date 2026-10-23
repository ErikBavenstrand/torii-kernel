// The global save manager's screen (Settings > Library > Saves; the card side
// is saveorphan.h): the space the saves, their history and the trash use, an
// "Empty trash now" row, and the orphaned saves, the folders of games whose
// ROM is no longer where they were. A on an orphan opens a popup: Relink
// (a list of the library's games, the ones with the orphan's file name first,
// then its console's, then the rest; A on one asks, then moves the save and
// history folders there, saveorphan_relink), Keep (leaves it, dimmed until
// the screen closes) and Delete (to the trash for 30 days, asks first).
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
