// The menu's lists on the v0.3 engine: the SD list, the NOR list and
// Recently Played, with the game menu, the NOR game menu and their dialogs.
#ifndef TORII_SCREENS_LISTS_H
#define TORII_SCREENS_LISTS_H

// The menu, from the SD card's root folder; never returns. Launches, NOR
// writes, SETTINGS and the cheat screen run engine sessions of their own:
// the lists leave the engine for them and come back after.
void lists_run(void) __attribute__((noreturn));

#endif
