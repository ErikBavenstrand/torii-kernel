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

static int is_hex(char c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// Only hex digits count: spaces, tabs, CRs, NULs and other bytes are skipped,
// so a field without digits is empty. An entry needs an address and a value;
// empty ones (";;", "ADDR;", a trailing ",") add nothing.
void Parse_cheat_line(const char *line, u32 len)
{
	u32 i;
	u32 address_len = 0;
	u32 val_len = 0;
	char address_buf[9];
	char val_buf[3];
	u32 is_val = 0;  // past the address's ','
	u32 address_add = 0;

	memset(address_buf, 0x00, sizeof(address_buf));
	memset(val_buf, 0x00, sizeof(val_buf));
	for (i = 0; i <= len; i++) {
		char c = i < len ? line[i] : ';';  // the end of the line ends an entry

		if (c == ',' || c == ';') {
			if (is_val && address_len && val_len) {
				Add_cheat(str2hex(address_buf) + address_add, str2hex(val_buf));
				address_add++;
			}
			val_len = 0;
			memset(val_buf, 0x00, sizeof(val_buf));
			if (c == ',') {
				is_val = 1;
			} else {
				is_val = 0;
				address_add = 0;
				address_len = 0;
				memset(address_buf, 0x00, sizeof(address_buf));
			}
		} else if (!is_hex(c)) {
			continue;
		} else if (is_val) {
			if (val_len < sizeof(val_buf) - 1)
				val_buf[val_len++] = c;
		} else {
			if (address_len < sizeof(address_buf) - 1)
				address_buf[address_len++] = c;
		}
	}
}
