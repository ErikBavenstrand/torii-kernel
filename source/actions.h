// What the menu's pages do to games and the game NOR, with no screen of
// their own (v0.4 decisions, B10): launches, writes to NOR, deleting a file,
// and the NOR's launch, delete and format. Pages call these after the frame
// (from their loop, never from an event handler), and nothing launches
// through a screen.
//
// They ask nothing: a page asks first (a confirmation), then calls the
// action. They show what goes wrong in dialogs (ui/dialog.h): over the
// page when an engine session is up, or in one of their own.
//
// A launch, and a write to NOR, ends any engine session first (the screen
// fades to black): the launch's dialogs run their own, and it fills PSRAM
// and pReadCache. If no game starts it returns with no session up and the
// screen black, for the menu to come back.
#ifndef TORII_ACTIONS_H
#define TORII_ACTIONS_H

#include <gba_base.h>

// How a game starts (the v0.3 game menu's first four lines)
enum action_how {
	ACTION_CLEAN,      // CLEAN BOOT
	ACTION_ADDON,      // BOOT WITH ADDON: the addons Settings turns on
	ACTION_NOR,        // WRITE TO NOR, clean
	ACTION_NOR_ADDON,  // WRITE TO NOR with the addons
	ACTION_HOWS,
};

// The save type a GBA game starts with: what its .mde file says (the type
// it was last started with, AUTO if none), or a type to use and remember
// (the v0.3 game menu's SAVE TYPE: 0 AUTO, 1 SRAM, 2 EEPROM 8K, 3 EEPROM
// 512, 4 FLASH 64K, 5 FLASH 128K)
#define ACTION_SAVE_KEEP 0xFF
#define ACTION_SAVE_TYPES 6

// What action_launch returns when no game started
enum {
	ACTION_NOTHING,      // nothing changed (an error was shown, or it isn't a game)
	ACTION_NOR_MAYBE,    // a write to NOR failed: the game NOR may have changed
	ACTION_NOR_WRITTEN,  // the game was written to the game NOR
};

// Starts the game at path, an SD file's full path ("/GBA/Advance Wars.gba"),
// or "NOR:" and a game NOR game's name (a Recent entry). GB, GBC and NES
// games start clean whatever how says, and only GBA games are written to
// NOR (a page never offers the others that: they are only logged).
// hard_reset: the game starts through the BIOS's hard reset (L held).
// Returns only if no game started (see the top).
u32 action_launch(const char *path, u32 how, u32 save_type, u32 hard_reset);

// Deletes the file at path (a full path; a folder is refused). Its save stays (T11); it leaves
// Recent, the favourites and the library index. 1 if it was deleted.
int action_delete_file(const char *path);

// The game NOR's list, read into pNorFS and game_total_NOR (writes nothing)
void action_nor_read(void);
// Starts the game NOR's game index (from the list action_nor_read read).
// Returns only if it didn't start (or no such game), as
// action_launch does.
void action_nor_launch(u32 index, u32 hard_reset);
// Deletes the game NOR's latest game (the only one the NOR can delete). It
// leaves Recent unless another of its name is still on the NOR. 1 if it
// was deleted.
int action_nor_delete(void);
// Erases the whole game NOR, behind a progress dialog. Every NOR game
// leaves Recent.
void action_nor_format(void);

#endif
