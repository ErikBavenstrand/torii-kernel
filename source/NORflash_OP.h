#include <gba_base.h>

#include "ff.h"
//---------------------------------------------------------------
void Chip_Reset(void);
void Block_Erase(u32 blockAdd);
void Chip_Erase(void);
void FormatNor(void);
void WriteFlash(u32 address, u8 *buffer, u32 size);
void IWRAM_CODE WriteFlash_with32word(u32 address, u8 *buffer, u32 size);
u32 Loadfile2NOR(TCHAR *filename, u32 NORaddress, u16 have_patch, u8 SAVEMODE);
// Torii: reads the list (norlist.h) into pNorFS: the newer whole copy, or an
// older kernel's. Writes nothing.
void Read_NOR_info(void);
// Checks pNorFS against the game NOR and returns how many games it lists: an
// entry whose game isn't there ends the list, and games with a completion
// mark but no entry are added. Sets gl_norOffset to where the next one goes.
u32 GetFileListFromNor(void);
// At boot, after GetFileListFromNor: if Read_NOR_info read an older kernel's
// list and GetFileListFromNor found all of its count games, saves it in
// Torii's format, once. Crash-safe: B is written first, and A, the older
// list, only once B is whole. If GetFileListFromNor dropped the list's last
// entry only, saves the count games it found, so the drop isn't logged again.
void Migrate_NOR_list(u32 count);
// Saves the first count games of pNorFS as both list copies; 0 when both
// read back whole. Uses pReadCache.
u32 Save_NOR_list(u32 count);
// Deletes the last game on the list.
void Delete_latest_NOR(void);