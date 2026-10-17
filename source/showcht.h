#include <gba_base.h>
#include "cheat.h"

#define MAX_BUF_LEN 6000
#define MAX_KEY_LEN 50
#define MAX_VAL_LEN 6000

#define MAX_sectionVAL_LEN 300

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
void Open_cht_file(TCHAR *gamefilename, u32 havecht);
void Trim(char s[]);
// Reads a line into line like f_gets, at most len - 1 bytes with its \n, but
// byte for byte: FatFs's f_gets decodes UTF-8 and drops other bytes, which
// broke cheat files in Latin-1 or GBK
char *Read_line(char *line, int len, FIL *fp);
