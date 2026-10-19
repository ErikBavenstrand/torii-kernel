//#ifndef	EZKERNEL_HEADER
//#define EZKERNEL_HEADER

#include "ff.h"
#include "ez_define.h"

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

typedef struct FM_Folder_SECT {
	char filename[100];
} FM_Folder_FS;

typedef struct FM_FILE_SECT {
	char filename[100];
	u32 filesize;
} FM_FILE_FS;

typedef enum {
	SD_list = 0,
	NOR_list = 1,
	SET_win = 2,
	SET2_win = 3,
	HELP = 4,
} PAGE_NUM;
//----------------------------
extern DWORD Get_NextCluster(FFOBJID *obj, DWORD clst);
extern DWORD ClustToSect(FATFS *fs, DWORD clst);

extern FM_NOR_FS pNorFS[MAX_NOR] EWRAM_BSS;
extern u8 pReadCache[MAX_pReadCache_size] EWRAM_BSS;
extern u8 __attribute__((aligned(4))) GAMECODE[4];

extern u16 gl_reset_on;
extern u16 gl_rts_on;
extern u16 gl_sleep_on;
extern u16 gl_cheat_on;

/*
extern u16 gl_color_selected;
extern u16 gl_color_text;
extern u16 gl_color_selectBG_sd;
extern u16 gl_color_selectBG_nor;
extern u16 gl_color_MENU_btn;
extern u16 gl_color_cheat_count;
extern u16 gl_color_cheat_black;
extern u16 gl_color_NORFULL;
extern u16 gl_color_btn_clean;
*/

void CheckLanguage(void);
void CheckSwitch(void);
u32 Game_hardware(u32 is_EMU, const u8 *gamecode, u8 gb_cart_type);
u32 Recently_play_add(const char *entry, u32 count);
#define RECENT_NOR "NOR:"
// A Recently Played row holds the longest entry get_count keeps and the
// kernel writes: a folder under MAX_path_len, '/', and a list entry's name.
#define RECENT_LEN (MAX_path_len + sizeof(((FM_FILE_FS *)0)->filename))
extern char p_recently_play[10][RECENT_LEN];
const char *Recently_play_nor(const char *entry);
u8 Check_saveMODE(u8 gamecode[]);
u8 Get_saveMODE(u8 Save_num, u32 gamefilesize);
u32 Check_file_type(TCHAR *pfilename);
u32 Get_savefilesize(BYTE saveMODE);
// Empties FAT_table_buffer, the cart's table of a file's cluster runs and parameters.
void init_FAT_table(void);
// IWRAM code must run from IWRAM while the ROM page changes, so it is never
// static: GCC would inline it into callers in ROM.
void IWRAM_CODE Refresh_filename(u32 show_offset, u32 file_select, u32 updown, u32 haveThumbnail);
u32 Loadsavefile(TCHAR *filename);
u32 Save_savefile(TCHAR *filename, u32 savesize);
u32 IWRAM_CODE LoadEMU2PSRAM(TCHAR *filename, u32 is_EMU);

void delay(u32 R0);
u32 LoadRTSfile(TCHAR *filename);
void ShowTime(u32 page_num, u32 page_mode);
u8 NOR_list_MENU(u32 show_offset, u32 file_select);
u8 SD_list_MENU(u32 show_offset, u32 file_select, u32 play_re);
//#endif