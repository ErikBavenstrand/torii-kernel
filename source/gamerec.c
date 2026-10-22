#include "gamerec.h"

#include <stdio.h>
#include <string.h>

void gamerec_init(struct gamerec *r)
{
	r->save_type = 0;
	r->boot_mode = 0;
	memcpy(r->slot, SAVESLOT_MAIN, sizeof(SAVESLOT_MAIN));
}

// A decimal number at s (up to len bytes) that fits a byte; 0 if it doesn't
static int parse_byte(const char *s, u32 len, u8 *out)
{
	if (!len || len > 3)
		return 0;
	u32 v = 0;
	for (u32 i = 0; i < len; i++) {
		if (s[i] < '0' || s[i] > '9')
			return 0;
		v = v * 10 + (u32)(s[i] - '0');
	}
	if (v > 255)
		return 0;
	*out = (u8)v;
	return 1;
}

static void parse_line(struct gamerec *r, const char *line, u32 len)
{
	u32 eq = 0;
	while (eq < len && line[eq] != '=')
		eq++;
	if (eq == len)
		return;
	u32 k0 = 0, k1 = eq, v0 = eq + 1, v1 = len;
	while (k0 < k1 && (line[k0] == ' ' || line[k0] == '\t'))
		k0++;
	while (k1 > k0 && (line[k1 - 1] == ' ' || line[k1 - 1] == '\t'))
		k1--;
	while (v0 < v1 && (line[v0] == ' ' || line[v0] == '\t'))
		v0++;
	while (v1 > v0 && (line[v1 - 1] == ' ' || line[v1 - 1] == '\t' || line[v1 - 1] == '\r'))
		v1--;
	u32 kl = k1 - k0;
	if (kl == 9 && !memcmp(line + k0, "save_type", 9)) {
		parse_byte(line + v0, v1 - v0, &r->save_type);
	} else if (kl == 9 && !memcmp(line + k0, "boot_mode", 9)) {
		parse_byte(line + v0, v1 - v0, &r->boot_mode);
	} else if (kl == 4 && !memcmp(line + k0, "slot", 4)) {
		char raw[SAVESLOT_NAME_BYTES * 2], clean[SAVESLOT_NAME_BYTES];
		u32 n = v1 - v0 < sizeof(raw) - 1 ? v1 - v0 : (u32)sizeof(raw) - 1;
		memcpy(raw, line + v0, n);
		raw[n] = 0;
		// Not a name saveslot_clean_name would keep as it is: the record was
		// edited, or is damaged; the slot stays as it was
		if (saveslot_clean_name(clean, raw))
			memcpy(r->slot, clean, strlen(clean) + 1);
	}
}

void gamerec_parse(struct gamerec *r, const char *text, u32 len)
{
	u32 start = 0;
	// A UTF-8 BOM, from an editor
	if (len >= 3 && !memcmp(text, "\xEF\xBB\xBF", 3))
		start = 3;
	for (u32 i = start; i <= len; i++) {
		if (i == len || text[i] == '\n') {
			parse_line(r, text + start, i - start);
			start = i + 1;
		}
	}
}

u32 gamerec_format(char *out, u32 size, const struct gamerec *r)
{
	int n = snprintf(out, size, "save_type = %u\nboot_mode = %u\nslot = %s\n", (unsigned)r->save_type,
			 (unsigned)r->boot_mode, r->slot);
	return n > 0 && (u32)n < size ? (u32)n : 0;
}
