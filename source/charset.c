#include "charset.h"

// U+0100-U+017F, Latin Extended-A, folded to ASCII
static const char latin_a_ascii[128] = {
	'A', 'a', 'A', 'a', 'A', 'a', 'C', 'c', 'C', 'c', 'C', 'c', 'C', 'c', 'D', 'd', // U+0100
	'D', 'd', 'E', 'e', 'E', 'e', 'E', 'e', 'E', 'e', 'E', 'e', 'G', 'g', 'G', 'g', // U+0110
	'G', 'g', 'G', 'g', 'H', 'h', 'H', 'h', 'I', 'i', 'I', 'i', 'I', 'i', 'I', 'i', // U+0120
	'I', 'i', 'I', 'i', 'J', 'j', 'K', 'k', 'k', 'L', 'l', 'L', 'l', 'L', 'l', 'L', // U+0130
	'l', 'L', 'l', 'N', 'n', 'N', 'n', 'N', 'n', 'n', 'N', 'n', 'O', 'o', 'O', 'o', // U+0140
	'O', 'o', 'O', 'o', 'R', 'r', 'R', 'r', 'R', 'r', 'S', 's', 'S', 's', 'S', 's', // U+0150
	'S', 's', 'T', 't', 'T', 't', 'T', 't', 'U', 'u', 'U', 'u', 'U', 'u', 'U', 'u', // U+0160
	'U', 'u', 'U', 'u', 'W', 'w', 'Y', 'y', 'Y', 'Z', 'z', 'Z', 'z', 'Z', 'z', 's', // U+0170
};

// Dashes and curly quotes, which Latin-1 lacks: their ASCII forms, or 0
static char punctuation_of(u32 c)
{
	switch (c) {
	case 0x2010: // hyphens and dashes
	case 0x2011:
	case 0x2012:
	case 0x2013:
	case 0x2014:
	case 0x2015:
	case 0x2212: // minus
		return '-';
	case 0x2018: // single quotes
	case 0x2019:
	case 0x201A:
	case 0x201B:
		return '\'';
	case 0x201C: // double quotes
	case 0x201D:
	case 0x201E:
	case 0x201F:
		return '"';
	default:
		return 0;
	}
}

u32 charset_next_latin1(const char *s, u32 len, u32 *pos)
{
	const u8 *p = (const u8 *)s;
	u8 lead = p[*pos];
	u32 count, c;

	(*pos)++;
	if (lead < 0x80)
		return lead;
	if (lead < 0xC0)
		return CHARSET_NONE;
	if (lead < 0xE0) {
		count = 1;
		c = lead & 0x1F;
	} else if (lead < 0xF0) {
		count = 2;
		c = lead & 0x0F;
	} else if (lead < 0xF8) {
		count = 3;
		c = lead & 0x07;
	} else {
		return CHARSET_REPLACEMENT;
	}
	while (count--) {
		if (*pos >= len || (p[*pos] & 0xC0) != 0x80)
			return CHARSET_REPLACEMENT;
		c = (c << 6) | (p[*pos] & 0x3F);
		(*pos)++;
	}
	if (c < 0x100)
		return c;
	if (c < 0x180)
		return (u8)latin_a_ascii[c - 0x100];
	char q = punctuation_of(c);
	return q ? (u32)(u8)q : CHARSET_REPLACEMENT;
}

u32 charset_cut(const char *s, u32 max)
{
	u32 n = 0;
	while (n < max && s[n])
		n++;
	if (n < max || !s[n])
		return n;
	while (n && ((u8)s[n] & 0xC0) == 0x80)
		n--;
	return n;
}
