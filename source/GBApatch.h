#include <gba_base.h>

#include "ff.h"

enum {
	EMax = 64 //Torii: was 32; Kirby: Nightmare in Dream Land has 44 IRQ handler literals
};
typedef struct SPatchInfo {
	u32 iOffset;
	u32 iValue;
} SPatchInfo2;

extern FIL gfile;
extern void Sleep_ReplaceIRQ_start(void);
extern void Sleep_ReplaceIRQ_end(void);
extern void Return_address_L(void);
extern void Sleep_key(void);
extern void Reset_key(void);

extern void RTS_ReplaceIRQ_start(void);
extern void RTS_ReplaceIRQ_end(void);
extern void RTS_Return_address_L(void);
extern void RTS_Sleep_key(void);
extern void RTS_Reset_key(void);
extern void RTS_Accent(void);
extern void RTS_Refuse_colours(void);  // the stub's INGAME_REFUSED_COLOURS
extern void RTS_switch(void);
extern void Cheat_switch(void);
// The cheat interpreter (cheatrun.s), copied after the menu's code; the
// table follows it
extern void Cheat_blob_start(void);
extern void Cheat_blob_end(void);
// The live block's address in the interpreter (cheatrun.s), the kernel's at launch
extern void Cheat_live(void);
// Where this launch's live block is (ingame_abi.h): 0 when it has none (no
// interpreter, or no spend area found), and before any launch patched
extern u32 cheat_live_at;
// Runs the table once, but the cheats whose bit of the off mask is set
void Cheat_run(const u32 *table, u32 off_lo, u32 off_hi, u32 once);

extern void Fire_Emblem_0378_patch_start(void);
extern void Fire_Emblem_0378_patch_end(void);
extern void Fire_Emblem_1692_patch_start(void);
extern void Fire_Emblem_1692_patch_end(void);
extern void Fire_Emblem_A_patch_start(void);
extern void Fire_Emblem_A_patch_end(void);
extern void Modify_address_A(void);
extern void Fire_Emblem_B_patch_start(void);
extern void Fire_Emblem_B_patch_end(void);
extern void Modify_address_B(void);
extern void Fire_Emblem_iQue_patch_start(void);
extern void Fire_Emblem_iQue_patch_end(void);

extern u32 gl_cheat_count;

// Bytes kept free at the ROM's end for the in-game menu's code with
// SAVESTATE on and no cheats (RTS_ReplaceIRQ_start to RTS_ReplaceIRQ_end); see
// SetTrimSize.
#define RTS_PATCH_LENGTH 0x1000

void GBApatch_Cleanrom(u32 *address, int filesize);
void GBApatch_PSRAM(u32 *address, int filesize);
void Patch_somegame(u32 *Data);

void GBApatch_Cleanrom_NOR(u32 *address, u32 offset);
void GBApatch_NOR(u32 *address, int filesize, u32 offset);
// 1 if PATCH/'s cache for the game (rom_bytes long, head_crc the CRC32 of
// its first 0x200 bytes) holds its patches; else the ROM is scanned again
u32 Check_pat(TCHAR *gamefilename, u32 rom_size, u32 head_crc);
// Why a .pat file's words (EMax slots, then 16 words) can't be used for this
// ROM with patch_length bytes kept at its end, or NULL if they can
const char *pat_cache_check(const u32 *words, u32 rom_size, u32 head_crc, u32 patch_length);
void Make_pat_file(char *filename);
// A GBA game's save type (0 AUTO, 1-5) and its boot mode (ACTION_BOOT_*,
// actions.h) are in its record, game.ini in its folder of saves (gamerec.h,
// gamesave.h). gamefilename is in the folder FatFs is in.
// Its save type (0 if it has no record) and, into boot_mode, its boot mode
u8 Check_mde_file(TCHAR *gamefilename, u8 *boot_mode);
// Write one of them, keeping the rest; 0 if written
u8 Make_mde_file(TCHAR *gamefilename, u8 Save_num);
u8 Make_mde_boot(TCHAR *gamefilename, u8 boot_mode);

u32 use_internal_engine(u8 gamecode[], TCHAR *gamefilename);
void Apply_rom_patches(void);
// The ROM words the in-game patches change (the IRQ hook's literals and code
// patches), as word offsets, and how many there are.
extern SPatchInfo2 iPatchInfo2[EMax];
extern u32 iCount2;
// Logs which in-game code went into the ROM, where, and every patched word:
// part of the launch record.
void Log_patches(void);
// Places the patch area (iTrimSize) after the ROM's data, buffer being its
// last iSize bytes (mode 1: a NOR game's). Where a size cap pulls it back
// onto the game's data (a 32 MB ROM, a full 16 MB EEPROM one) with cheats
// picked, the area is cut to what the picks need; if even that would land
// on the data, the cheats that run in game are left out (gl_cheat_on and
// gl_cheat_count 0, logged) and the area is what it is without them.
void SetTrimSize(u8 *buffer, u32 romsize, u32 iSize, u32 mode, BYTE saveMODE);
// The ROM bytes this launch's patches wrote (the entry branch, the IRQ
// hook's words, code patches) and the patch area: the picked cheats' ROM
// patches must keep off them (cheatstore_launch). Reset at a launch's start.
void rom_guard_reset(void);
// 1 if [offset, offset + bytes) touches one of them
int rom_guard_hits(u32 offset, u32 bytes);
void Patch_SpecialROM_TrimSize(void);
// Fills the cart's table of the file's cluster runs (game_save_rts 1: the
// game's, 3: the RTS file's). Returns 0, 0xffffffff if the file doesn't
// open, or RTS_FAT_FRAGMENTED if its runs don't fit or its chain breaks.
// sectors (if not NULL) gets the sectors the chain covers, once walked.
#define RTS_FAT_FRAGMENTED 0xfffffffe
u32 Check_game_RTS_FAT(TCHAR *filename, u32 game_save_rts, u32 *sectors);
void IWRAM_CODE PatchInternal(u32 *Data, int iSize, u32 offset);
void Patch_SpecialROM_sleepmode(void);
// IWRAM code must run from IWRAM while the ROM page changes, so it is never
// static: GCC would inline it into callers in ROM.
bool IWRAM_CODE PatchDragonBallZ(u32 *Data);
void IWRAM_CODE CheckNes(u32 *Data);
