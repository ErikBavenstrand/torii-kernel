#include "charset.h"

// U+00A0-U+00FF, Latin-1's letters and symbols, folded to ASCII
static const char latin1_ascii[96] = {
	' ', '!', 'c', 'L', 'o',  'Y', '|', 'S', '"', 'C', 'a', '"', '-', '-', 'R', '-', // U+00A0
	'o', '+', '2', '3', '\'', 'u', 'P', '.', ',', '1', 'o', '"', '?', '?', '?', '?', // U+00B0
	'A', 'A', 'A', 'A', 'A',  'A', 'A', 'C', 'E', 'E', 'E', 'E', 'I', 'I', 'I', 'I', // U+00C0
	'D', 'N', 'O', 'O', 'O',  'O', 'O', 'x', 'O', 'U', 'U', 'U', 'U', 'Y', 'P', 's', // U+00D0
	'a', 'a', 'a', 'a', 'a',  'a', 'a', 'c', 'e', 'e', 'e', 'e', 'i', 'i', 'i', 'i', // U+00E0
	'd', 'n', 'o', 'o', 'o',  'o', 'o', '/', 'o', 'u', 'u', 'u', 'u', 'y', 'p', 'y', // U+00F0
};

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

static char glyph_of(u32 c)
{
	if (c < 0x80)
		return c;
	if (c >= 0xA0 && c < 0x100)
		return latin1_ascii[c - 0xA0];
	if (c >= 0x100 && c < 0x180)
		return latin_a_ascii[c - 0x100];
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
	}
	return '?';
}

char charset_next(const char *s, u32 len, u32 *pos)
{
	const u8 *p = (const u8 *)s;
	u8 lead = p[*pos];
	u32 count, c;

	(*pos)++;
	if (lead < 0x80)
		return lead;
	if (lead < 0xC0)
		return 0; // a continuation byte without its lead byte
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
		return '?';
	}
	// A truncated or broken sequence draws one '?' and ends at the bad byte
	while (count--) {
		if (*pos >= len || (p[*pos] & 0xC0) != 0x80)
			return '?';
		c = (c << 6) | (p[*pos] & 0x3F);
		(*pos)++;
	}
	return glyph_of(c);
}
