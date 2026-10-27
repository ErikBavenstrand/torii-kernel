// The in-game menu's screenshots on the card (v0.8 P4,
// docs/design/v0.8/ingame-menu.md): the overlay packs the game's screen
// into cart SRAM, up to INGAME_SHOTS_MAX a session (struct ingame_shot,
// source/ingame_abi.h; source/shotpack.h), and the kernel's next boot after
// the game draws each (source/ppu_render.c) into
// /torii/screenshots/<game> <date> <time>.bmp, -2, -3 and on in one second.
//
// Power-cut safe without a rename (FatFs's can leave two names on one
// cluster chain): the file is written with a blank BMP head, its pixels,
// then its head, under a journal holding its name. A power cut loses the
// dump (cart SRAM past the save has no battery), so the next boot
// (shotstore_recover) deletes a file the journal names that isn't whole:
// the card has the screenshot whole or not at all. A write the card
// refuses keeps the dump and the name it picked for the next boot.
#ifndef TORII_SHOTSTORE_H
#define TORII_SHOTSTORE_H

#include <gba_base.h>

#define SHOTSTORE_DIR "/torii/screenshots"
#define SHOTSTORE_JOURNAL SHOTSTORE_DIR "/.writing"

// At boot, before anything else borrows pReadCache: a screenshot a power
// cut left broken goes (the journal's), and the journal
void shotstore_recover(void);

// The screenshots in cart SRAM to the card, under a notice; game is the
// name the files take (the session's). Each filed is marked so; once all
// are, the list is cleared. Returns 1 then or if there are none, 0 if the
// card didn't take one (the rest stay, for the next boot). Borrows
// pReadCache whole.
int shotstore_file(const char *game);

#endif
