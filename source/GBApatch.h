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
//extern void Wakeup_key(void);

extern void RTS_ReplaceIRQ_start(void);
extern void RTS_ReplaceIRQ_end(void);
extern void RTS_Return_address_L(void);
extern void RTS_Sleep_key(void);
extern void RTS_Reset_key(void);
//extern void RTS_Wakeup_key(void);
extern void RTS_switch(void);
extern void Debug_switch(void);
extern void Debug_blob_start(void);
extern void Debug_blob_end(void);
extern void Cheat_count(void);
extern void CHEAT(void);
extern void no_CHEAT_end(void);

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
// SAVESTATE on and no cheats (RTS_ReplaceIRQ_start to no_CHEAT_end); see
// SetTrimSize.
#define RTS_PATCH_LENGTH 0x1000

void GBApatch_Cleanrom(u32 *address, int filesize);
void GBApatch_PSRAM(u32 *address, int filesize);
void Patch_somegame(u32 *Data);

void GBApatch_Cleanrom_NOR(u32 *address, u32 offset);
void GBApatch_NOR(u32 *address, int filesize, u32 offset);
u32 Check_pat(TCHAR *gamefilename);
void Make_pat_file(char *filename);
u32 Check_RTS(TCHAR *gamefilename);
// A GBA game's /SAVER/<name>.mde file: byte 0 its save type (0 AUTO, 1-5),
// byte 1 its boot mode (Torii v0.4: ACTION_BOOT_*, actions.h; v0.3 and the
// official kernel read byte 0 only and write zeros past it), 16 bytes in all.
// Its save type (0 if it has no file) and, into boot_mode, its boot mode
u8 Check_mde_file(TCHAR *gamefilename, u8 *boot_mode);
// Write one of its bytes, keeping the other (from the current folder, which
// they come back to); 0 if written, 2 if there is no /SAVER
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
u32 Check_cheat_file(TCHAR *gamefilename);
void SetTrimSize(u8 *buffer, u32 romsize, u32 iSize, u32 mode, BYTE saveMODE);
void Patch_SpecialROM_TrimSize(void);
// Fills the cart's table of the file's cluster runs (game_save_rts 1: the
// game's, 3: the RTS file's). Returns 0, 0xffffffff if the file doesn't
// open, or RTS_FAT_FRAGMENTED if its runs don't fit or its chain breaks.
#define RTS_FAT_FRAGMENTED 0xfffffffe
u32 Check_game_RTS_FAT(TCHAR *filename, u32 game_save_rts);
void IWRAM_CODE PatchInternal(u32 *Data, int iSize, u32 offset);
void Patch_SpecialROM_sleepmode(void);
// IWRAM code must run from IWRAM while the ROM page changes, so it is never
// static: GCC would inline it into callers in ROM.
bool IWRAM_CODE PatchDragonBallZ(u32 *Data);
void IWRAM_CODE CheckNes(u32 *Data);
