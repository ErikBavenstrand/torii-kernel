// Which game an in-game screenshot is (shotname.h).
#include "shotname.h"

#include <string.h>

void shotname_stem(char *out, u32 size, const char *path)
{
	const char *base = strrchr(path, '/');
	base = base ? base + 1 : path;
	const char *dot = strrchr(base, '.');
	u32 n = dot && dot != base ? (u32)(dot - base) : (u32)strlen(base);
	u32 most = (size < SHOTNAME_STEM_BYTES ? size : SHOTNAME_STEM_BYTES) - 1;
	if (n > most) {
		n = most;
		while (n && (base[n] & 0xC0) == 0x80)  // never a cut UTF-8 sequence at the end
			n--;
	}
	memcpy(out, base, n);
	out[n] = 0;
}

// n digits at s as a number; -1 if one isn't a digit
static s32 digits(const char *s, u32 n)
{
	s32 v = 0;
	for (u32 i = 0; i < n; i++) {
		if (s[i] < '0' || s[i] > '9')
			return -1;
		v = v * 10 + (s[i] - '0');
	}
	return v;
}

// "-N" with N from 2 to 999, no leading zero: N, else 0
static u32 number(const char *s, u32 len)
{
	if (len < 2 || len > 4 || s[0] != '-' || s[1] == '0')
		return 0;
	s32 v = digits(s + 1, len - 1);
	return v >= 2 ? (u32)v : 0;
}

#define DATED 20  // " YYYY-MM-DD HH-MM-SS"

static int dated(const char *s, struct shotname_when *w)
{
	static const char form[] = " dddd-dd-dd dd-dd-dd";
	for (u32 i = 0; i < DATED; i++)
		if (form[i] != 'd' && s[i] != form[i])
			return 0;
	s32 y = digits(s + 1, 4), mo = digits(s + 6, 2), d = digits(s + 9, 2), h = digits(s + 12, 2),
	    mi = digits(s + 15, 2), sec = digits(s + 18, 2);
	if (y < 1 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59 || sec < 0 ||
	    sec > 59)
		return 0;
	*w = (struct shotname_when){(u16)y, (u8)mo, (u8)d, (u8)h, (u8)mi, (u8)sec, 1};
	return 1;
}

int shotname_suffix(const char *rest, u32 len, struct shotname_when *when)
{
	struct shotname_when w;
	if (len == DATED && dated(rest, &w)) {
		*when = w;
		return 1;
	}
	if (len > DATED && dated(rest, &w)) {
		w.n = (u16)number(rest + DATED, len - DATED);
		*when = w;
		return w.n != 0;
	}
	u32 n = number(rest, len);
	if (!n)
		return 0;
	*when = (struct shotname_when){0, 0, 0, 0, 0, 0, (u16)n};
	return 1;
}

// The name's length without ".bmp" (any case), or 0
static u32 base_len(const char *name)
{
	u32 len = (u32)strlen(name);
	if (len < 5 || name[len - 4] != '.')
		return 0;
	const char *e = name + len - 3;
	if ((e[0] | 0x20) != 'b' || (e[1] | 0x20) != 'm' || (e[2] | 0x20) != 'p')
		return 0;
	return len - 4;
}

int shotname_is_bmp(const char *name)
{
	return base_len(name) != 0;
}

u32 shotname_split(const char *name, struct shotname_when *when)
{
	u32 len = base_len(name);
	// The dated forms first: a dated name's seconds read as a "-N" too
	for (u32 tail = DATED; tail <= DATED + 4; tail++)
		if (len > tail && shotname_suffix(name + len - tail, tail, when) && when->year)
			return len - tail;
	for (u32 tail = 2; tail <= 4; tail++)
		if (len > tail && shotname_suffix(name + len - tail, tail, when))
			return len - tail;
	return 0;
}

int shotname_match(const char *name, const char *stem, struct shotname_when *when)
{
	u32 len = base_len(name), s = (u32)strlen(stem);
	return s && len > s && memcmp(name, stem, s) == 0 && shotname_suffix(name + s, len - s, when);
}

int shotname_cmp(const struct shotname_when *a, const struct shotname_when *b)
{
	const u8 ka[] = {a->year != 0, (u8)(a->year >> 8), (u8)a->year, a->month, a->day, a->hour, a->min,
			 a->sec,       (u8)(a->n >> 8),    (u8)a->n};
	const u8 kb[] = {b->year != 0, (u8)(b->year >> 8), (u8)b->year, b->month, b->day, b->hour, b->min,
			 b->sec,       (u8)(b->n >> 8),    (u8)b->n};
	return memcmp(ka, kb, sizeof(ka));
}
