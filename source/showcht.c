#include <gba_systemcalls.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gba_base.h>
#include <gba_input.h>
#include <gba_dma.h>
#include <gba_video.h>

#include "ezkernel.h"
#include "showcht.h"
#include "tlog.h"
#include "savename.h"
#include "engine/cover.h"
#include "engine/engine.h"
#include "ui/dialog.h"
#include "ui/ui.h"

FM_CHT_LINE tmpCHTFS;

// The cheat screen borrows pReadCache (owners: ez_define.h) until it returns:
// the parsed list from 0x2000, then the line and value buffers at its end.
#define CHT_LIST_OFFSET 0x2000
#define CHT_LINES_MAX ((READCACHE_CHEAT_WORK - CHT_LIST_OFFSET) / sizeof(FM_CHT_LINE))
_Static_assert(READCACHE_CHEAT_WORK % 4 == 0, "word-aligned buffers");
u8 *pCHTbuffer = (u8 *)(pReadCache + CHT_LIST_OFFSET); //patchbuffer

ST_entry pCHEAT[CHEAT_MAX] EWRAM_BSS;
u32 gl_cheat_count;

//------------------------------------------------------------------

extern FIL gfile;
static char *const buf = (char *)pReadCache + READCACHE_CHEAT_WORK;  // a line read
static char *const _paramv = (char *)pReadCache + READCACHE_CHEAT_WORK + MAX_BUF_LEN;  // a cheat's value
//------------------------------------------------------------------
//------------------------------------------------------------------
void Trim(char s[])
{
	int n;
	for (n = strlen(s) - 1; n >= 0; n--) {
		if (s[n] != ' ' && s[n] != '\t' && s[n] != '\n' && s[n] != '\r')//CRLF and LF files alike
			break;
		s[n] = '\0';
	}
}
//------------------------------------------------------------------
char *Read_line(char *line, int len, FIL *fp)
{
	int n = 0;
	UINT got;
	char c;

	while (n < len - 1) {
		if (f_read(fp, &c, 1, &got) != FR_OK || got != 1)
			break;
		line[n++] = c;
		if (c == '\n')
			break;
	}
	if (n == 0)
		return NULL;
	line[n] = 0;
	return line;
}
//------------------------------------------------------------------
static void Get_KEY_val(FIL *file, char *KEY_section, char *KEY_secval, char getbuff[])
{
	(void)file;
	int text_comment = 0;

	int in_section = 0;
	//int keyval_count=0;

	char section[MAX_KEY_LEN] = {0};

	f_lseek(&gfile, 0x0);
	while (Read_line(buf, MAX_KEY_LEN, &gfile) != NULL) {
		Trim(buf);
		// to skip text comment with flags /* ...*/
		if (buf[0] != '#' && (buf[0] != '/' || buf[1] != '/')) {
			if (strstr(buf, "/*") != NULL) {
				text_comment = 1;
				continue;
			} else if (strstr(buf, "*/") != NULL) {
				text_comment = 0;
				continue;
			}
		}
		if (text_comment == 1) {
			continue;
		}

		int buf_len = strlen(buf);

		// ignore and skip the line with first chracter '#', '=' or '/'
		if (buf_len <= 1 || buf[0] == '#' || buf[0] == '=' || buf[0] == '/') {
			in_section = 0;
			continue;
		}

		char _paramk[MAX_KEY_LEN] = {0};
		char key_value[MAX_KEY_LEN + 1] = {0};  // its NUL even at MAX_KEY_LEN bytes, for strcpy

		int _kv = 0, _klen = 0, _vlen = 0;
		int i = 0;

		int is_section = 0;
		int section_len = 0;
		//int _val_len=0;

		for (i = 0; i < buf_len; ++i) {
			if (buf[i] == ' ')
				continue;

			if (buf[i] == '[') {
				is_section = 1;
				in_section = 0;

				memset(section, 0, MAX_KEY_LEN);
				continue;
			}

			if (is_section == 1 && buf[i] != ']') {
				if (section_len < MAX_KEY_LEN - 1)
					section[section_len++] = buf[i];
				continue;
			} else if (buf[i] == ']') {
				is_section = 0;
				in_section = 1;
				//_val_len = 0;
				break;
			}

			if (in_section == 1) {
				// scan param key name
				if (_kv == 0 && buf[i] != '=') {
					if (_klen >= MAX_KEY_LEN)
						break;
					_paramk[_klen++] = buf[i];
					continue;
				} else if (buf[i] == '=') {
					_kv = 1;
					continue;
				}

				// scan param key value
				if (_vlen >= MAX_KEY_LEN || buf[i] == '#')
					break;

				key_value[_vlen++] = buf[i];
			}
		}
		//DEBUG_printf("KEY_section %s, section %s",KEY_section,section);
		if (strcmp(KEY_section, section) == 0) {
			if (strcmp(KEY_secval, _paramk) == 0) {
				strcpy(getbuff, key_value);

				return;
			}
		}
		if (strcmp(_paramk, "") == 0 || strcmp(key_value, "") == 0)
			continue;

		memset(buf, 0, MAX_KEY_LEN);
	}
}
//------------------------------------------------------------------
static u32 Get_CHT_val(FIL *file, char *KEY_section, char *KEY_secval /*,char getbuff[]*/)
{
	(void)file;
	int text_comment = 0;

	int in_section = 0;
	//int keyval_count=0;

	char section[MAX_KEY_LEN] = {0};

	f_lseek(&gfile, 0x0);
	while (Read_line(buf, MAX_BUF_LEN, &gfile) != NULL) {
		Trim(buf);
		// to skip text comment with flags /* ...*/
		if (buf[0] != '#' && (buf[0] != '/' || buf[1] != '/')) {
			if (strstr(buf, "/*") != NULL) {
				text_comment = 1;
				continue;
			} else if (strstr(buf, "*/") != NULL) {
				text_comment = 0;
				continue;
			}
		}
		if (text_comment == 1) {
			continue;
		}

		int buf_len = strlen(buf);
		//DEBUG_printf("cht buf_len %x",buf_len);

		// ignore and skip the line with first chracter '#', '=' or '/'
		if (buf_len <= 1 || buf[0] == '#' || buf[0] == '=' || buf[0] == '/') {
			in_section = 0;
			continue;
		}

		char _paramk[MAX_KEY_LEN] = {0};
		//char _paramv[/*MAX_VAL_LEN*/MAX_KEY_LEN] = {0};

		int _kv = 0, _klen = 0, _vlen = 0;
		int i = 0;

		int is_section = 0;
		int section_len = 0;
		//int _val_len=0;

		for (i = 0; i < buf_len; ++i) {
			if (buf[i] == ' ')
				continue;

			if (buf[i] == '[') {
				is_section = 1;
				in_section = 0;

				memset(section, 0, MAX_KEY_LEN);
				continue;
			}

			if (is_section == 1 && buf[i] != ']') {
				if (section_len < MAX_KEY_LEN - 1)
					section[section_len++] = buf[i];
				continue;
			} else if (buf[i] == ']') {
				is_section = 0;
				in_section = 1;
				//_val_len = 0;
				break;
			}

			if (in_section == 1) {
				// scan param key name
				if (_kv == 0 && buf[i] != '=') {
					if (_klen >= MAX_KEY_LEN)
						break;
					_paramk[_klen++] = buf[i];
					continue;
				} else if (buf[i] == '=') {
					_kv = 1;
					continue;
				}
				// scan param key value
				if (_vlen >= MAX_BUF_LEN || buf[i] == '#')
					break;

				_paramv[_vlen++] = buf[i];
			}
		}

		//DEBUG_printf("KEY_section %s, section %s",KEY_section,section);
		if (strcmp(KEY_section, section) == 0) {
			if (strcmp(KEY_secval, _paramk) == 0) {
				//0111 Multi-line
				while (Read_line(buf, MAX_BUF_LEN, &gfile) != NULL) {
					Trim(buf);
					// to skip text comment with flags /* ...*/
					if (buf[0] != '#' && (buf[0] != '/' || buf[1] != '/')) {
						if (strstr(buf, "/*") != NULL) {
							text_comment = 1;
							break;
						} else if (strstr(buf, "*/") != NULL) {
							text_comment = 0;
							break;
						}
					}
					if (text_comment == 1) {
						break;
					}

					int line_len = strlen(buf);
					//DEBUG_printf("cht line_len %x",line_len);

					// ignore and skip the line with first chracter '#', '=' or '/'
					if (line_len <= 1 || buf[0] == '#' || buf[0] == '=' || buf[0] == '/') {
						break;
					}
					for (i = 0; i < line_len; ++i) {
						if (buf[i] == '=')
							goto endcht;
					}
					for (i = 0; i < line_len && _vlen < MAX_BUF_LEN; ++i) {
						_paramv[_vlen++] = buf[i];
					}
					//DEBUG_printf("%x %x %x %x %x %x %x %x", buf[0],buf[1], buf[2],buf[3], buf[4],buf[5], buf[6],buf[7]);
					//wait_btn();
				}
			endcht:
				return _vlen;
			}
		}
		if (strcmp(_paramk, "") == 0 || strcmp(_paramv, "") == 0)
			continue;

		memset(buf, 0, MAX_KEY_LEN);
	}
	return 0;
}
//------------------------------------------------------------------
static u32 Get_all_Section_val(FIL *file)
{
	(void)file;
	char line_buf[MAX_sectionVAL_LEN];
	int text_comment = 0;

	int in_section = 0;
	//char keyval[10][25];
	//int keyval_count=0;
	u32 Line = 0;

	char section[MAX_KEY_LEN] = {0};

	//fseek(file,0x0,SEEK_SET);
	f_lseek(&gfile, 0x0);
	while (Read_line(line_buf, MAX_sectionVAL_LEN, &gfile) != NULL) {
		if (Line >= CHT_LINES_MAX) {  // the list would reach buf and _paramv
			tlog("cheats: the list is full at %lu lines; the rest are left out", (unsigned long)Line);
			break;
		}
		memset(tmpCHTFS.LINEname, 0x00, MAX_KEY_LEN);
		Trim(line_buf);
		// to skip text comment with flags /* ...*/
		if (line_buf[0] == '-' && line_buf[1] == '-')
			break;
		if (line_buf[0] != '#' && (line_buf[0] != '/' || line_buf[1] != '/')) {
			if (strstr(line_buf, "/*") != NULL) {
				text_comment = 1;
				continue;
			} else if (strstr(line_buf, "*/") != NULL) {
				text_comment = 0;
				continue;
			}
		}
		if (text_comment == 1) {
			continue;
		}

		int buf_len = strlen(line_buf);

		// ignore and skip the line with first chracter '#', '=' or '/'
		if (buf_len <= 1 || line_buf[0] == '#' || line_buf[0] == '=' || line_buf[0] == '/') {
			in_section = 0;
			continue;
		}
		char _paramk[MAX_KEY_LEN] = {0};
		//char _paramv[MAX_sectionVAL_LEN] = {0};

		int _kv = 0, _klen = 0, _vlen = 0;
		int i = 0;

		int is_section = 0;
		int section_len = 0;
		//int _val_len=0;

		for (i = 0; i < buf_len; ++i) {
			if (line_buf[i] == ' ')
				continue;

			if (line_buf[i] == '[') {
				is_section = 1;
				in_section = 0;

				memset(section, 0, MAX_KEY_LEN);
				continue;
			}

			if (is_section == 1 && line_buf[i] != ']') {
				if (section_len < MAX_KEY_LEN - 1)
					section[section_len++] = line_buf[i];
				continue;
			} else if (line_buf[i] == ']') {
				memcpy(tmpCHTFS.LINEname, section, section_len);
				tmpCHTFS.is_section = 1;
				tmpCHTFS.len = section_len;
				tmpCHTFS.select = 0;

				dmaCopy(&tmpCHTFS, &((FM_CHT_LINE *)pCHTbuffer)[Line], sizeof(FM_CHT_LINE));

				Line++;
				//if(Line==10)return;
				is_section = 0;
				in_section = 1;
				//_val_len = 0;
				break;
			}

			if (in_section == 1) {
				// scan param key name
				if (_kv == 0 && line_buf[i] != '=') {
					if (_klen >= MAX_KEY_LEN)
						break;
					_paramk[_klen++] = line_buf[i];
					continue;
				} else if (line_buf[i] == '=') {
					//DEBUG_printf(":%s ",_paramk);
					memcpy(tmpCHTFS.LINEname, _paramk, _klen);
					tmpCHTFS.is_section = 0;

					tmpCHTFS.select = 0;

					_kv = 1;
					continue;
				} else {
					/*
		      // scan param key value
		      if (_vlen >= MAX_VAL_LEN || line_buf[i] == '#')
		                break;
		                
		      _paramv[_vlen++] = line_buf[i];
		      */
				}
			}
		}
		if (_kv == 1 && Line < CHT_LINES_MAX) {  // "a=b[c]" adds a section, then this
			tmpCHTFS.len = _vlen;
			//memcpy(tmpCHTFS.KEY_val,_paramv,_vlen);
			dmaCopy(&tmpCHTFS, &((FM_CHT_LINE *)pCHTbuffer)[Line], sizeof(FM_CHT_LINE));
			Line++;
		}
	}
	return Line;
}
//------------------------------------------------------------------
static void Analyze_KEYVAL(FIL *file, u32 total)
{
	(void)file;
	u32 tol;
	u32 buflen;

	gl_cheat_count = 0;
	for (tol = 0; tol < total; tol++) {
		if (((FM_CHT_LINE *)pCHTbuffer)[tol].select == 1) {
			u32 current_select = tol - 1;
			while (((FM_CHT_LINE *)pCHTbuffer)[current_select].is_section != 1) {
				current_select--;
			}
			buflen = Get_CHT_val(&gfile, ((FM_CHT_LINE *)pCHTbuffer)[current_select].LINEname,
					     ((FM_CHT_LINE *)pCHTbuffer)[tol].LINEname);
			Parse_cheat_line(_paramv, buflen);
		}
	}
}
//------------------------------------------------------------------
static u32 Check_count(u32 all_count)
{
	u32 count = 0;
	u32 Line;
	for (Line = 0; Line < all_count; Line++) {
		if (strcmp(((FM_CHT_LINE *)pCHTbuffer)[Line].LINEname, "GameInfo") == 0)
			break;

		count++;
		//if(count>512)
		//break;
	}
	return count;
}
//------------------------------------------------------------------
static unsigned char HexToChar(unsigned char bChar)
{
	if ((bChar >= 0x30) && (bChar <= 0x39)) {
		bChar -= 0x30;
	} else if ((bChar >= 0x41) && (bChar <= 0x46)) // Capital
	{
		bChar -= 0x37;
	} else if ((bChar >= 0x61) && (bChar <= 0x66)) //littlecase
	{
		bChar -= 0x57;
	} else {
		bChar = 0xff;
	}
	return bChar;
}
//------------------------------------------------------------------
static u32 Change2cht_folder(u32 chtname)
{
	TCHAR chtnamebuf[100];
	u32 res;
	TCHAR *folder_name;
	TCHAR currentpath[256];
	memset(currentpath, 00, 256);

	memset(chtnamebuf, 0x00, 100);
	sprintf(chtnamebuf, "%d%d%d%d", HexToChar(((u8 *)&chtname)[0]), HexToChar(((u8 *)&chtname)[1]),
		HexToChar(((u8 *)&chtname)[2]), HexToChar(((u8 *)&chtname)[3]));
	u32 num = atoi(chtnamebuf);
	//DEBUG_printf("num =%d", num);
	if (num < 200) {
		folder_name = (TCHAR *)"0000";
	} else if (num < 400) {
		folder_name = (TCHAR *)"0200";
	} else if (num < 600) {
		folder_name = (TCHAR *)"0400";
	} else if (num < 800) {
		folder_name = (TCHAR *)"0600";
	} else if (num < 1000) {
		folder_name = (TCHAR *)"0800";
	} else if (num < 1200) {
		folder_name = (TCHAR *)"1000";
	} else if (num < 1400) {
		folder_name = (TCHAR *)"1200";
	} else if (num < 1600) {
		folder_name = (TCHAR *)"1400";
	} else if (num < 1800) {
		folder_name = (TCHAR *)"1600";
	} else if (num < 2000) {
		folder_name = (TCHAR *)"1800";
	} else if (num < 2200) {
		folder_name = (TCHAR *)"2000";
	} else if (num < 2400) {
		folder_name = (TCHAR *)"2200";
	} else if (num < 2600) {
		folder_name = (TCHAR *)"2400";
	} else if (num < 2800) {
		folder_name = (TCHAR *)"2600";
	} else {
		folder_name = (TCHAR *)"2800";
	}

	sprintf(currentpath, "/CHEAT/Eng/%s", folder_name);
	res = f_chdir(currentpath);
	return res;
}
//------------------------------------------------------------------
u32 Check_cheat_file(TCHAR *gamefilename)
{
	u32 res;
	UINT ret;
	TCHAR chtnamebuf[GAME_NAME_MAX + 1];
	u32 filesize;
	u32 GAMEID = 0;
	u32 i;

	res = f_open(&gfile, gamefilename, FA_READ);
	if (res == FR_OK) {
		f_lseek(&gfile, 0xAC);
		f_read(&gfile, &GAMEID, 4, (UINT *)&ret);
		f_close(&gfile);
		if (GAMEID == 0)
			return 0;
	}

	name_with_ext(chtnamebuf, gamefilename, "cht");

	res = f_chdir("/CHEAT");
	if (res != FR_OK) {
		return 0;
	}

	res = f_open(&gfile, chtnamebuf, FA_OPEN_EXISTING);
	//f_chdir(currentpath);
	if (res == FR_OK)//have a cht file
	{
		f_close(&gfile);
		return 0x0000FFFF;
	} else {
		res = f_open(&gfile, "GameID2cht.bin", FA_READ);
		u32 *tempbuff = (u32 *)(pReadCache);
		if (res == FR_OK)//have a file
		{
			filesize = f_size(&gfile);
			if (filesize > 0x10000)
				filesize = 0x10000;
			f_lseek(&gfile, 0x0);
			f_read(&gfile, tempbuff, filesize, &ret);//pReadCache max 0x20000 Byte

			for (i = 0; i < filesize / 4; i += 2) {
				if (tempbuff[i] == GAMEID) {
					f_close(&gfile);

					u32 chtname = ((u32 *)tempbuff)[i + 1];

					res = Change2cht_folder(chtname);
					if (res != 0)
						return 0;
					memset(chtnamebuf, 0x00, sizeof(chtnamebuf));
					sprintf(chtnamebuf, "%d%d%d%d.cht", HexToChar(((u8 *)&chtname)[0]),
						HexToChar(((u8 *)&chtname)[1]), HexToChar(((u8 *)&chtname)[2]),
						HexToChar(((u8 *)&chtname)[3]));
					res = f_open(&gfile, chtnamebuf, FA_OPEN_EXISTING);

					if (res == FR_OK)//have a cht file
					{
						f_close(&gfile);
						return chtname;
					} else {
						return 0;
					}
				}
			}
		}
		return 0;
	}
}
//------------------------------------------------------------------
// The cheat screen, on the v0.3 engine. Each section of a .cht file is a
// cheat, drawn as a heading, and its entries are the cheat's options: the
// cursor skips sections, A picks an entry (or drops it again) and drops the
// section's other entries, LEFT and RIGHT (and L and R) page, and releasing
// B leaves, applying what was picked.
//
// The list stays in pReadCache (pCHTbuffer) while the screen runs, inside
// the cover stage's range (engine/stage.h): the cover loader is stopped
// before the engine starts, and the screen starts no jobs.

// The entries picked when the cheat screen last closed, if it has been
// opened since the game menu opened: the menu's CHEAT(n)
static u32 cheats_picked EWRAM_BSS;
static u8 cheats_opened EWRAM_BSS;

void cheat_menu_label(char *out, u32 size, const char *name)
{
	if (!cheats_opened)
		snprintf(out, size, "%s", name);
	else
		snprintf(out, size, "%s(%lu)", name, (unsigned long)cheats_picked);
}

void cheat_menu_forget(void)
{
	gl_cheat_count = 0;
	cheats_opened = 0;
}

enum {
	CS_BAR,
	CS_TITLE,
	CS_COUNT,
	CS_LIST,
	CS_SCROLL,
	CS_NODES,
};

// The screen's state, on Open_cht_file's stack while the screen runs
struct cheat_screen {
	FM_CHT_LINE *lines;
	u32 count;        // lines shown: sections and entries
	u32 entries;      // entries among them
	u32 cursor;       // the list's cursor, as last seen
	u32 ord_at, ord;  // line ord_at has ord entries up to it: counting goes on from there
	u8 list, label;   // node indices
	u8 b_down;        // B went down on this screen: its release leaves
	char counter[24];
	char row[MAX_KEY_LEN + 3];
	char title[MAX_KEY_LEN + 1];  // the game's name (GameInfo)
};

static u32 cs_name_len(const FM_CHT_LINE *line)
{
	u32 n = 0;
	while (n < MAX_KEY_LEN && line->LINEname[n])
		n++;
	return n;
}

static u32 cs_count(void *ctx)
{
	return ((struct cheat_screen *)ctx)->count;
}

static int cs_fetch(void *ctx, u32 i, struct ui_item *out)
{
	struct cheat_screen *cs = ctx;
	const FM_CHT_LINE *line = &cs->lines[i];
	u32 len = cs_name_len(line);
	if (line->is_section == 1) {
		out->text = len ? line->LINEname : " ";  // len 0 would mean "up to the NUL"
		out->len = (u16)(len ? len : 1);
		out->ink = UI_ROLE_DIM;
		return UI_ITEM_READY;
	}
	// An entry: indented under its section, and in the accent colour with
	// a check at the right when picked
	memcpy(cs->row, "   ", 3);
	memcpy(cs->row + 3, line->LINEname, len);
	out->text = cs->row;
	out->len = (u16)(3 + len);
	if (line->select) {
		out->ink = UI_ROLE_ACCENT;
		out->detail = "[x]";
	}
	return UI_ITEM_READY;
}

static const struct ui_list_model cs_model = {cs_count, cs_fetch, 0};

// The nearest entry from line i in direction dir (1 or -1), i included;
// count if there is none
static u32 cs_entry_from(const struct cheat_screen *cs, u32 i, int dir)
{
	while (i < cs->count) {
		if (cs->lines[i].is_section != 1)
			return i;
		if (dir < 0 && i == 0)
			break;
		i = (u32)((s32)i + dir);
	}
	return cs->count;
}

// The entry nearest to line i, looking in direction dir first; i itself if
// the list has no entry
static u32 cs_settle(const struct cheat_screen *cs, u32 i, int dir)
{
	u32 to = cs_entry_from(cs, i, dir);
	if (to == cs->count)
		to = cs_entry_from(cs, i, -dir);
	return to == cs->count ? i : to;
}

// Shows "[ord/entries]": which entry the cursor is on, of how many
static void cs_show_count(struct cheat_screen *cs)
{
	u32 ord = 0;
	if (cs->cursor < cs->count && cs->lines[cs->cursor].is_section != 1) {
		u32 i = cs->ord_at, n = cs->ord;
		while (i < cs->cursor)
			n += cs->lines[++i].is_section != 1;
		while (i > cs->cursor)
			n -= cs->lines[i--].is_section != 1;
		cs->ord_at = i;
		cs->ord = n;
		ord = n;
	}
	snprintf(cs->counter, sizeof(cs->counter), "[%03lu/%03lu]", (unsigned long)ord, (unsigned long)cs->entries);
	ui_label_set(cs->label, cs->counter);
}

// Moves the cursor to the entry nearest to line to, on side dir first
static void cs_move(struct cheat_screen *cs, u32 to, int dir)
{
	to = cs_settle(cs, to, dir);
	ui_list_set_cursor(cs->list, to);
	cs->cursor = to;
	cs_show_count(cs);
}

// A on an entry: picks it, or drops it if it was picked, and drops the other
// entries of its section
static void cs_pick(struct cheat_screen *cs, u32 at)
{
	FM_CHT_LINE *l = cs->lines;
	if (at >= cs->count || l[at].is_section == 1)
		return;
	u32 lo = at, hi = at;
	while (lo > 0 && l[lo - 1].is_section != 1)
		l[--lo].select = 0;
	while (hi + 1 < cs->count && l[hi + 1].is_section != 1)
		l[++hi].select = 0;
	l[at].select = !l[at].select;
	ui_list_refresh(cs->list, lo, hi);
}

static int cs_event(struct ui_view *v, const struct ui_event *e)
{
	struct cheat_screen *cs = v->ctx;
	switch (e->type) {
	case UI_EV_CURSOR:
		// The list moved a step or a page onto e->index: from a section,
		// on to the nearest entry the way it went
		cs_move(cs, e->index, e->index >= cs->cursor ? 1 : -1);
		return 1;
	case UI_EV_ACTIVATE:
		cs_pick(cs, e->index);
		return 1;
	case UI_EV_KEYS: {
		if (e->keys & INPUT_B) {
			cs->b_down = 1;
			return 1;
		}
		if (!(e->keys & (INPUT_LEFT | INPUT_RIGHT)) || !cs->count)
			return 0;
		// LEFT and RIGHT page, as L and R do
		u32 page = (u32)ui_node(cs->list)->h / ui_theme()->row_h;
		u32 to;
		if (e->keys & INPUT_LEFT)
			to = cs->cursor > page ? cs->cursor - page : 0;
		else
			to = cs->cursor + page < cs->count ? cs->cursor + page : cs->count - 1;
		cs_move(cs, to, (e->keys & INPUT_LEFT) ? -1 : 1);
		return 1;
	}
	default:
		return 0;
	}
}

static const struct ui_label_config cs_title = {"", UI_FONT_HEADING, UI_ALIGN_LEFT, 0};
static const struct ui_label_config cs_counter = {"", UI_FONT_BODY, UI_ALIGN_RIGHT, UI_LABEL_SWAP};
static const struct ui_list_config cs_list = {&cs_model, 0, 0, UI_LIST_MARQUEE, 24};
static const struct ui_scrollbar_config cs_bar = {CS_LIST};

static const struct ui_spec cs_nodes[CS_NODES] = {
	[CS_BAR] = {.kind = UI_PANEL,
		    .id = 1,
		    .parent = UI_NONE,
		    .plane = 2,
		    .anchor = UI_HFILL,
		    .paper = UI_ROLE_PANEL,
		    .h = 16},
	[CS_TITLE] = {.kind = UI_LABEL,
		      .id = 2,
		      .parent = CS_BAR,
		      .plane = 2,
		      .anchor = UI_HFILL | UI_VFILL,
		      .paper = UI_ROLE_PANEL,
		      .w = 72,
		      .data = &cs_title},
	[CS_COUNT] = {.kind = UI_LABEL,
		      .id = 3,
		      .parent = CS_BAR,
		      .plane = 2,
		      .anchor = UI_RIGHT | UI_VFILL,
		      .ink = UI_ROLE_DIM,
		      .paper = UI_ROLE_PANEL,
		      .w = 72,
		      .data = &cs_counter},
	[CS_LIST] = {.kind = UI_LIST,
		     .id = 4,
		     .parent = UI_NONE,
		     .plane = 1,
		     .anchor = UI_FILL,
		     .flags = UI_FOCUSABLE,
		     .paper = UI_ROLE_BG,
		     .y = 16,
		     .w = 8,
		     .data = &cs_list},
	[CS_SCROLL] = {.kind = UI_SCROLLBAR,
		       .id = 5,
		       .parent = UI_NONE,
		       .plane = 2,
		       .anchor = UI_RIGHT | UI_VFILL,
		       .y = 16,
		       .w = 8,
		       .data = &cs_bar},
};

static const struct ui_view_spec cs_view = {"cheats", cs_nodes, CS_NODES, 34, CS_LIST, 0, cs_event};

// Runs the screen until B is released, and returns with it still up; 0 if
// it couldn't be shown
static int cs_run(struct cheat_screen *cs)
{
	struct ui_view *v = ui_push(&cs_view, cs);
	if (!v) {
		tlog_error("cheats: %s", ui_error());
		return 0;
	}
	cs->list = (u8)(v->first + CS_LIST);
	cs->label = (u8)(v->first + CS_COUNT);
	ui_label_set((u8)(v->first + CS_TITLE), cs->title);
	cs_move(cs, 0, 1);
	while (ui_depth()) {
		ui_frame();
		if (cs->b_down && (ui_input()->released & INPUT_B))
			break;
	}
	return 1;
}

int Open_cht_file(TCHAR *gamefilename, u32 havecht)
{
	int shown = 0;
	struct cheat_screen cs;
	u32 res;
	TCHAR chtnamebuf[GAME_NAME_MAX + 1];

	// As zeroed as they were in EWRAM of their own, before the first file
	memset(buf, 0, 2 * MAX_BUF_LEN);
	if (havecht == 0x0000FFFF) {
		res = f_chdir("/CHEAT");
		if (res != FR_OK) {
			return 0;
		}
		name_with_ext(chtnamebuf, gamefilename, "cht");
	} else {
		Change2cht_folder(havecht);
		u8 *chtmode;
		chtmode = (u8 *)&havecht;
		sprintf(chtnamebuf, "%d%d%d%d.cht", HexToChar(chtmode[0]), HexToChar(chtmode[1]), HexToChar(chtmode[2]),
			HexToChar(chtmode[3]));
	}
	res = f_open(&gfile, chtnamebuf, FA_READ);

	if (res == FR_OK)  //have a cht file
	{
		memset(&cs, 0, sizeof(cs));
		Get_KEY_val(&gfile, "GameInfo", "Name", cs.title);

		u32 all_count = Get_all_Section_val(&gfile);
		if (all_count) {
			cs.lines = (FM_CHT_LINE *)pCHTbuffer;
			cs.count = Check_count(all_count);  //cut from "GameInfo"
			for (u32 i = 0; i < cs.count; i++)
				cs.entries += cs.lines[i].is_section != 1;
			cs.ord = cs.count && cs.lines[0].is_section != 1;  // entries up to line 0

			// The list sits in the cover stage's range: no asset job may run
			cover_cache_invalidate();
			// Over the menu's session if it runs one (its views are popped)
			int own = !ui_active();
			if (own) {
				engine_init();
				ui_init(NULL);
			}
			cs_run(&cs);
			shown = 1;  // the engine ran: the caller's screen starts again either way
			// Reading the picks' codes can take seconds in a big file: the
			// cheat screen stays up meanwhile, rather than a black one
			Analyze_KEYVAL(&gfile, cs.count);
			if (own) {
				ui_shutdown();
				dialog_blank();  // back to the caller, on a black screen
			} else {
				ui_leave(UI_TR_DEEPER, -1);  // back up to the game menu
				while (ui_depth())
					ui_pop();
			}
			cheats_opened = 1;
			cheats_picked = 0;
			for (u32 i = 0; i < cs.count; i++)
				cheats_picked += cs.lines[i].is_section != 1 && cs.lines[i].select == 1;
		}
	}
	f_close(&gfile);
	return shown;
}
