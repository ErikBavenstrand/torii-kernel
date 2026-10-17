#include <string.h>

#include "cheat.h"

// Hex digits to a number; other characters are skipped, more than 8 give 0.
static unsigned long str2hex(char *str)
{
	unsigned long sum = 0;
	unsigned long i;
	int len = strlen(str);

	if (len > 8)
		return 0;
	for (i = 0; i < (unsigned long)len; i++) {
		if (str[i] >= '0' && str[i] <= '9')
			sum = sum * 16 + str[i] - '0';
		else if (str[i] >= 'a' && str[i] <= 'f')
			sum = sum * 16 + str[i] - 0x57;
		else if (str[i] >= 'A' && str[i] <= 'F')
			sum = sum * 16 + str[i] - 0x37;
	}
	return sum;
}

// Adds one cheat write, dropping it once the table is full: the in-game
// handler and every entry must fit in CHEAT_PATCH_LENGTH.
static void Add_cheat(u32 address, u32 val)
{
	if (gl_cheat_count >= CHEAT_MAX)
		return;
	ST_entry cheat = {address, val};
	pCHEAT[gl_cheat_count++] = cheat;
}

void Parse_cheat_line(const char *line, u32 len)
{
	u32 i;
	u32 address_len = 0;
	u32 val_len = 0;
	char address_buf[9];
	char val_buf[3];
	u32 is_val = 0;
	u32 is_address = 1;
	u32 address_add = 0;

	memset(address_buf, 0x00, sizeof(address_buf));
	memset(val_buf, 0x00, sizeof(val_buf));
	for (i = 0; i < len; i++) {
		if (line[i] == ' ') {
			continue;
		} else if (line[i] == ',') {
			if (is_address == 1) {  // first ','
				is_address = 0;
			} else {  // next ','
				Add_cheat(str2hex(address_buf) + address_add, str2hex(val_buf));
				address_add++;
			}
			val_len = 0;
			memset(val_buf, 0x00, sizeof(val_buf));
			is_val = 1;
			continue;
		} else if (line[i] == ';') {
			Add_cheat(str2hex(address_buf) + address_add, str2hex(val_buf));
			is_val = 0;
			is_address = 1;
			address_add = 0;
			memset(address_buf, 0x00, sizeof(address_buf));
			address_len = 0;
			continue;
		}

		if (is_val) {
			if (val_len < sizeof(val_buf) - 1)
				val_buf[val_len++] = line[i];
		} else {
			if (address_len < sizeof(address_buf) - 1)
				address_buf[address_len++] = line[i];
		}
	}
	if (address_len)  // a line ending in ';' has no last entry
		Add_cheat(str2hex(address_buf) + address_add, str2hex(val_buf));
}
