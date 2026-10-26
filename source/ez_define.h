// pReadCache, 128 KB of EWRAM, is the kernel's scratch buffer. It holds
// nothing between two passes of a menu loop: each owner below borrows it for
// one operation and needs nothing in it afterwards. Its owners, by offset:
// - READCACHE_CHEATS, 96 KB from 0x8000 to the end, clear of the stage: a
//   game's cheat file (struct cheatstore, cheatstore.h, about 46 KB): its
//   cheats, the decoder's work and its text's parts on their way to and
//   within PSRAM (the text itself is in PSRAM), while the cheat screen runs
//   (screens/cheats.c, which starts no job and calls nothing else that
//   borrows past the stage; its code keypad, screens/keypad.c, checks codes
//   in the decoder's work, and the rename keyboard borrows nothing), within
//   the game popup's count of them, and at a
//   launch within cheatstore_launch's first pass, before the game is loaded
//   (the second, once it is in PSRAM, writes the ROM patches the first kept
//   in EWRAM: the load takes all of pReadCache).
// - All of it: bulk SD, NOR, SRAM and PSRAM transfers inside one operation:
//   starting a game (chunks, patch files, the save), writing a game to NOR,
//   the NOR list's record, save backup and restore,
//   Diagnostics, and the unit tests (in Diagnostics, and at boot in a test build).
// - READCACHE_STAGE: the engine's asset jobs (engine/stage.h), within one job
//   step: an SD chunk on its way to PSRAM, the path being opened, box art's
//   folder entry and zeros and a chunk of the art pack's index (boxart.c). Nothing in it outlives the step, and steps run only from
//   jobs_run, never inside one of the operations above, so it can overlap
//   them all. An owner that wants to keep data here across frames that run
//   jobs must stop the asset jobs first (cover_shutdown).
// - READCACHE_USERDATA, 32 KB: the user's lists (userdata_store.c), within
//   one call: a list on its way between the card and PSRAM, the line read.
// - READCACHE_LIBRARY, 1 KB: library_renamed's name and key on their way to
//   the index (library_scan.c), within the call.
// - READCACHE_SAVE, 64 KB: the SRAM on its way to a .sav (Save_savefile),
//   clear of everything a frame writes, so its progress dialog can draw.
// - READCACHE_SLOTS, 16 KB in READCACHE_SAVE's room, clear of the save copy's
//   2 x 2 KB at its start: the slot store's work area (slotstore.c), within
//   one slotstore_* call (which may run inside Save_savefile, after its
//   chunks are done): file objects, paths, a directory's names and two copy
//   buffers. Nothing outlives the call; no listing (SDLIST) may run across it.
// - READCACHE_ORPHAN, the same 16 KB: the global save manager's work area
//   (saveorphan.c), within one saveorphan_* call, which runs no slotstore_*
//   call inside it (they take turns). The Saves screen holds its list in EWRAM.
// - READCACHE_SAVES, 8 KB from 0x16000, clear of the slot store's and the save
//   copy's (and past the 2 x 2 KB at READCACHE_SAVE's start): the Saves screen's
//   state (screens/saves.c) while it runs, in a deferred action, where nothing
//   else borrows pReadCache between its calls.
// - READCACHE_GSAVES, the same 8 KB as READCACHE_SAVES (Game saves takes
//   its view down before opening a game's page, and walks the card again
//   after): a game's saves page's state (screens/game_saves.c) while it
//   runs, from a page's deferred action; and READCACHE_FLOW, the slot flows'
//   (screens/slot_flow.c), within one of their calls. Neither overlaps the
//   slot store's or the save copy's, and no SD listing runs meanwhile.
// - READCACHE_SDLIST, 43 KB in READCACHE_SAVE's room (no save runs during a
//   listing): the SD card page's folder listing on its way to PSRAM
//   (sdlist.h's scratch), within one listing.
// - READCACHE_GAMES, 96 KB from READCACHE_USERDATA (its bytes, the save's
//   and the SD listing's): a game list's build as its page opens
//   (screens/games.c), between userdata calls: Recent's and the favourites' records, and the
//   favourites' A-Z order on its way to PSRAM, within the call.
// - READCACHE_BOXART, 1 KB: box art's row path and a file name on its way to
//   PSRAM (boxart.c), within boxart_select or boxart_poll, between frames'
//   job steps.
// - READCACHE_NAMES, 124 KB from the start: /torii/names.txt changed by
//   Rename (gamename_store.c), within one call: a line read, the lines'
//   offsets and the new text on its way to the card (the old one waits in
//   PSRAM).
// - READCACHE_RENAME, the last 4 KB: an SD file's rename (actions.c), within
//   the call: its paths and its other files' names, kept while it calls
//   gamename_store_move, userdata_rename and library_renamed (whose shares
//   end before it); at boot, action_rename_recover reads the rename journal's
//   line from the start, nothing else running.
// - READCACHE_DEDUPE, 4 KB from the start: the paths of a game list row's
//   games that clean to one name (screens/games.c record_name), within the
//   call; it never runs inside the operations here that borrow the start.
// - READCACHE_SHARE, 4 KB from the start: the rename's walk of the card for
//   another game with its save (actions.c), within action_rename_file,
//   before it calls anything else that borrows pReadCache.
#define MAX_pReadCache_size 0x20000
#define READCACHE_STAGE 0x0
#define READCACHE_STAGE_SIZE 0x8000
#define READCACHE_SDLIST 0x10000  // sizeof(struct sdlist_scratch), to the end at most
#define READCACHE_USERDATA 0x8000
#define READCACHE_USERDATA_SIZE 0x8000
#define READCACHE_LIBRARY 0x1800  // sizeof(struct lib_build), up to 0x1C00
#define READCACHE_BOXART 0x1C00
#define READCACHE_BOXART_SIZE 0x400
#define READCACHE_SAVE 0x10000  // 64 KB, to the end
#define READCACHE_SLOTS 0x12000
#define READCACHE_SLOTS_SIZE 0x4000
#define READCACHE_SAVES 0x16000
#define READCACHE_SAVES_SIZE 0x2000
#define READCACHE_ORPHAN READCACHE_SLOTS
#define READCACHE_ORPHAN_SIZE READCACHE_SLOTS_SIZE
#define READCACHE_GSAVES READCACHE_SAVES
#define READCACHE_GSAVES_SIZE 0x1800
#define READCACHE_FLOW (READCACHE_SAVES + READCACHE_GSAVES_SIZE)
#define READCACHE_FLOW_SIZE (READCACHE_SAVES_SIZE - READCACHE_GSAVES_SIZE)
#define READCACHE_GAMES 0x8000
#define READCACHE_GAMES_SIZE 0x18000
#define READCACHE_NAMES 0x0
#define READCACHE_NAMES_SIZE 0x1F000
#define READCACHE_RENAME 0x1F000
#define READCACHE_RENAME_SIZE 0x1000
#define READCACHE_SHARE 0x0
#define READCACHE_DEDUPE 0x0
#define READCACHE_DEDUPE_SIZE 0x1000
#define READCACHE_SHARE_SIZE 0x1000
#define READCACHE_CHEATS 0x8000
#define READCACHE_CHEATS_SIZE 0x18000

#define MAX_NOR 0x40

#define MAX_path_len 0x100

#define FAT_table_size 0x400
//#define FAT_table_SAV_offset 0x200
#define FAT_table_RTS_offset 0x300

#define DEBUG

#define RGB(r, g, b) ((r) + ((g) << 5) + ((b) << 10))

#define PSRAMBase_S98 (void *)0x08800000
#define FlashBase_S98 (u32)0x09000000
#define FlashBase_S98_end (u32)0x09800000

#define SAVE_sram_base (u32)0x0E000000
#define SRAMSaver (u32)0x0E000000

#define UNBCD(x) (((x) & 0xF) + (((x) >> 4) * 10))
#define _BCD(x) ((((x) / 10) << 4) + ((x) % 10))
#define _YEAR 0
#define _MONTH 1
#define _DAY 2
#define _WKD 3
#define _HOUR 4
#define _MIN 5
#define _SEC 6

#define _UnusedVram (void *)0x06012c00

#define FlashBase_S71 (void *)0x08000000
// Torii: where the kernel NOR may be written, and the boot loader it must
// never reach: bootloader.h
#include "bootloader.h"

#define SAVE_info_offset 0x790000
#define NOR_info_offset 0x7A0000
#define SET_info_offset 0x7B0000

#define DMA_COPY_MODE 0X1
#define SET_PARAMETER_MODE 0x2

// After the kernel's 1 MB, from 0x08100000: the FPGA firmware Check_FW_update
// writes and Diagnostics' LZ77 sample, from EZ-Flash's image.bin
#include "image_layout.h"

#define assress_language 0
#define assress_v_reset 1
#define assress_v_rts 2
#define assress_v_sleep 3
//4 was the global Cheats switch until v0.7 (a game's picks are in its cheat file now)
#define assress_edit_sleephotkey_0 5
#define assress_edit_sleephotkey_1 6
#define assress_edit_sleephotkey_2 7
#define assress_edit_rtshotkey_0 8
#define assress_edit_rtshotkey_1 9
#define assress_edit_rtshotkey_2 10
#define assress_engine_sel 11
//12 was the official kernel's thumbnail switch (slot 34 is the Box art row's)
#define assress_game_clock 13  //games can read the cart's clock
#define assress_auto_save_sel 14
#define assress_ModeB_INIT 15  //Mode B: the cart reads it in mode B (the A/B switch), the kernel only stores it
#define assress_led_open_sel 16
#define assress_Breathing_R 17
#define assress_Breathing_G 18
#define assress_Breathing_B 19
#define assress_SD_R 20
#define assress_SD_G 21
#define assress_SD_B 22
//23 and 24 are DS Style's reset and backup toggles
#define assress_game_rumble 25  //games can use the cart's rumble motor
#define assress_logging 26  //Torii: LOGGING level, source/tlog.h (0xFFFF: off)
#define assress_dim 27  //Torii: DIM, idle minutes before the screen dims, 1-60 (else off), source/ui/ui.h
#define assress_theme 28  //Torii: THEME, 1 light (else dark; 2, the SD theme until v0.4, too), source/ui/ui.h
//29 was Torii's ACCENT until v0.4: never reuse it, old carts hold a value there
// Torii v0.4's (source/settings_model.h, which reads an unknown value as the default)
#define assress_clock_24h 30  //the clock shows 24 hours: 0 12-hour, else 24-hour
#define assress_game_view 31  //game lists: 1 list with art, else list
#define assress_start_on 32  //where the menu opens at power-on: enum settings_start (else last place)
#define assress_show_all 33  //the SD card page shows hidden files: 1 on, else off
#define assress_box_art 34  //list with art shows box art from /torii/art: 0 off, else on (source/boxart.h)
#define assress_boot_mode \
	35  //how A, Play and Launch start a GBA game: 1 clean, else with the addons (source/actions.h)
#define assress_history \
	37  //versions of a save kept per slot: an index into settings_history_keep, else 5 (source/slotstore.h)

#define assress_saveMODE 64
#define assress_max 65

#define LX9_FW_built_in_ver 5
#define LX16_FW_built_in_ver 7

// The firmware's address, size and CRC32: image_layout.h
#define LX9_wirte_address 0x80000
#define LX16_wirte_address 0x100000