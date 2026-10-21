#include <gba_base.h>
#include "cheat.h"

#define MAX_BUF_LEN 6000
#define MAX_KEY_LEN 50
#define MAX_VAL_LEN 6000

#define MAX_sectionVAL_LEN 300

// Where the cheat screen's line and value buffers (MAX_BUF_LEN each) start in
// pReadCache: its last bytes.
#define READCACHE_CHEAT_WORK (MAX_pReadCache_size - 2 * MAX_BUF_LEN)

typedef struct CHT_LINE {
	char LINEname[MAX_KEY_LEN];
	//char KEY_val[256];
	u8 is_section;
	u8 section_val_count;
	u8 len;
	u8 select;
} FM_CHT_LINE;

//int Get_KEY_val(FIL* file,char*KEY_section,char*KEY_secval,char getbuff[]);
u32 Check_cheat_file(TCHAR *gamefilename);
// What the cheat screen ended with
enum {
	CHEATS_NONE,  // it didn't show (no cheat file, or no cheat in it)
	CHEATS_BACK,  // B: nothing picked counts (gl_cheat_count 0)
	CHEATS_PLAY,  // START: the picked cheats' codes are in pCHEAT (gl_cheat_count)
};
// Runs the cheat screen for the game file gamefilename, whose cheat file
// Check_cheat_file found (havecht), titled title (NULL: the cheat file's
// name for the game) with the 16x16 icon icon (ART_ICON16_*). Over an
// engine session (the menu's; pop its views first) it runs in it and
// returns with no view up and the screen gone (ui_leave); otherwise in one
// of its own, returning on a black screen.
u32 Open_cht_file(TCHAR *gamefilename, u32 havecht, const char *title, u16 icon);
// No cheats for the next launch (a launch that doesn't pass the cheat screen)
void cheat_menu_forget(void);
void Trim(char s[]);
// Reads a line into line like f_gets, at most len - 1 bytes with its \n, but
// byte for byte: FatFs's f_gets decodes UTF-8 and drops other bytes, which
// broke cheat files in Latin-1 or GBK
char *Read_line(char *line, int len, FIL *fp);
