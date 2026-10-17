#include <gba_base.h>


#define MAX_BUF_LEN 6000
#define MAX_KEY_LEN 50
#define MAX_VAL_LEN 6000

#define MAX_sectionVAL_LEN 300

typedef struct CHT_LINE{
	char LINEname[MAX_KEY_LEN];
	//char KEY_val[256];
	u8 is_section ;
	u8 section_val_count ;
	u8 len;
	u8 select ;
} FM_CHT_LINE;

typedef struct ST_entry_{	
	u32  address;		
	u32  VAL;	
} ST_entry;

// The cheat handler and its table share the patch area appended to the ROM:
// the handler takes about 0xD04 bytes, and each entry 8.
#define CHEAT_PATCH_LENGTH 0x2000
#define CHEAT_MAX 600
extern ST_entry pCHEAT[CHEAT_MAX];
extern u32 gl_cheat_count;

//int Get_KEY_val(FIL* file,char*KEY_section,char*KEY_secval,char getbuff[]);
int Show_all_KEY_val(FIL* file);
u32 Check_cht_file(TCHAR *gamefilename);
void Open_cht_file(TCHAR *gamefilename,u32 havecht);
void Trim(char s[]);
void Parse_cheat_line(const char *line,u32 len);