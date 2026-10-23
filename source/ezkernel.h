#ifndef EZKERNEL_HEADER
#define EZKERNEL_HEADER

#include "ff.h"
#include "ez_define.h"
#include "savename.h"

typedef struct FM_NOR_FILE_SECT {////save to nor
	char filename[100];
	u16 rompage;
	u16 have_patch;
	u16 have_RTS;
	u16 is_64MBrom;//reserved;
	u32 filesize;
	u8 savemode;
	u8 reserved1;
	u16 reserved2;
	char gamename[0x10];
} FM_NOR_FS;

//----------------------------
extern DWORD Get_NextCluster(FFOBJID *obj, DWORD clst);
extern DWORD ClustToSect(FATFS *fs, DWORD clst);

extern FM_NOR_FS pNorFS[MAX_NOR] EWRAM_BSS;
extern u8 pReadCache[MAX_pReadCache_size] EWRAM_BSS;
// Says pReadCache holds nothing now (ez_define.h). In the emulator,
// mgba-shot then fills it with 0xFF, so code that reads what an earlier owner
// left there (a stale function pointer, length or count) fails in the tests
// instead of on a cart. On a cart it does nothing.
void readcache_poison(void);
extern u8 __attribute__((aligned(4))) GAMECODE[4];

extern u16 gl_reset_on;
extern u16 gl_rts_on;
extern u16 gl_sleep_on;
extern u16 gl_cheat_on;

void CheckLanguage(void);
void CheckSwitch(void);
u32 Game_hardware(u32 is_EMU, const u8 *gamecode, u8 gb_cart_type);
// The game NOR list's game named name, or game_total_NOR if none.
u32 NOR_game_named(const char *name);
// The text of error error_num (gl_error_*), as Show_error_num shows it.
const char *Error_message(u8 error_num);
u8 Check_saveMODE(u8 gamecode[]);
u8 Get_saveMODE(u8 Save_num, u32 gamefilesize);
u32 Check_file_type(TCHAR *pfilename);
u32 Get_savefilesize(BYTE saveMODE);
// Empties FAT_table_buffer, the cart's table of a file's cluster runs and parameters.
void init_FAT_table(void);
// IWRAM code must run from IWRAM while the ROM page changes, so it is never
// static: GCC would inline it into callers in ROM.
u32 Loadsavefile(TCHAR *filename);
// The save copy's results (Save_check, Save_savefile, pending_save_copy)
#define SAVE_FAILED 0    // no file made, or the write fell short (a full card): the old save is as it was
#define SAVE_WRITTEN 1   // the chip is on the card, verified, and is the save
#define SAVE_NOTHING 2   // savesize 0, or the user said no to the mismatch: no file touched
#define SAVE_SKIPPED 3   // the chip is blank or identical to the file: nothing to copy
#define SAVE_MISMATCH 4  // Save_check: the chip doesn't look like the file; ask first
#define SAVE_WRITE 5     // Save_check: copy the chip
u32 Save_check(TCHAR *filename, u32 savesize);
// hist_dir, if not NULL and the History setting is not Off, is the folder the
// replaced version of an existing file goes to first (slotstore_push_file)
u32 Save_savefile(TCHAR *filename, u32 savesize, const char *hist_dir);
// Finishes or undoes a copy a power cut interrupted (the .tmp and .bak files
// beside filename); run at boot and before a copy or launch
void Save_recover(const TCHAR *filename);
// The pending-save record in the kernel's NOR: 0x11, the size in 512-byte
// units, then the save file's path from halfword 2. Torii keeps the save
// type at halfword SAV_INFO_TYPE, past the longest name, for the log (0
// from older kernels).
#define SAV_INFO_TYPE 0x80
// The slot's history folder (/torii/backups/<rom path>/<slot>), a NUL-ended
// path from halfword SAV_INFO_HIST to the record's end, for the copy to
// push the replaced version into (zero from older kernels: no history). In
// the buffer pending_save() fills, which starts at record halfword 2, it is
// at byte (SAV_INFO_HIST - 2) * 2.
#define SAV_INFO_HIST 0x82
#define SAV_INFO_HIST_MAX (0x200 - SAV_INFO_HIST * 2 - 1)
// The longest save file path the record holds, from byte 4 to SAV_INFO_TYPE
#define SAV_INFO_NAME_MAX (SAV_INFO_TYPE * 2 - 4 - 1)
// 1 if the pending-save record (a save waiting to be copied from cart SRAM
// to the card, the next power-on or launch tries again) holds a save path
// that starts with prefix: a rename must leave that file's path alone.
int pending_save_under(const char *prefix);
u32 IWRAM_CODE LoadEMU2PSRAM(TCHAR *filename, u32 is_EMU);

void delay(u32 R0);
u32 LoadRTSfile(TCHAR *filename);

// The file name the launch path opens, in the folder currentpath
// (TCHAR[MAX_path_len], declared where it is used, as other files name
// locals so). actions.c sets both for a launch, and puts the folder back
// after: a copy in RAM, as the library's names are in PSRAM, which a launch
// fills.
extern TCHAR current_filename[GAME_NAME_MAX + 1];
extern u32 game_total_NOR;

// A game on its way to its launch (actions.c).
struct game_pick {
	TCHAR *name;      // its file name in currentpath (current_filename)
	u32 is_emu;       // Check_file_type: 0 GBA, 1 GBC, 2 GB, 3 NES
	u32 havecht;      // Check_cheat_file: 0 no cheat file
	u8 save_num;      // the SAVE TYPE line: 0 AUTO, 1-5 a type
	u8 old_save_num;  // what its record says
	u8 boot_mode;     // its record's ACTION_BOOT_* (actions.h): its own boot mode
};
// Fills g for g->name: 0 if it isn't a game. A GBA game reads its cheat and
// save type files; an emulated one starts only with Game_launch's line 0.
u32 Game_pick(struct game_pick *g);
// The bytes of the save g starts with (the save type a launch would use, by
// its header and record), 0 if it has none. Reads the ROM's header from
// currentpath.
u32 Game_save_bytes(const struct game_pick *g);
// Launches g as the v0.3 game menu's line menu_line did (0 CLEAN BOOT, 1 BOOT WITH ADDON,
// 2 WRITE TO NOR CLEAN, 3 WRITE TO NOR ADDON), L held for a hard reset,
// with no engine session up (its dialogs run their own). Returns only if it
// didn't start a game: 0 nothing written, 1 the game NOR may have changed,
// 2 a game was written to the game NOR.
u8 Game_launch(struct game_pick *g, u32 menu_line, u32 key_L);
// Starts NOR list game index; returns only if it failed. No engine session
// up, as for Game_launch.
void NOR_launch(u32 index, u32 key_L);
#endif