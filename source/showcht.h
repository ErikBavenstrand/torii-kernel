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
int Show_all_KEY_val(FIL *file);
u32 Check_cht_file(TCHAR *gamefilename);
u32 Check_cheat_file(TCHAR *gamefilename);
// Runs the cheat screen; 1 if it showed (the caller repaints its mode 3 screen)
int Open_cht_file(TCHAR *gamefilename, u32 havecht);
// The game menu's CHEAT row into out: name alone until the cheat screen has
// been opened, then name(n), n the cheats picked there
void cheat_menu_label(char *out, u32 size, const char *name);
// Leaving the game menu: no cheats, and the cheat screen counts as unopened
void cheat_menu_forget(void);
void Trim(char s[]);
// Reads a line into line like f_gets, at most len - 1 bytes with its \n, but
// byte for byte: FatFs's f_gets decodes UTF-8 and drops other bytes, which
// broke cheat files in Latin-1 or GBK
char *Read_line(char *line, int len, FIL *fp);
