#include "orphanplan.h"

#include <stdio.h>
#include <string.h>

#include "crc32.h"

#define HEADER "torii-orphanop 1\n"

static char lower(char c)
{
	return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
}

static int ends_nocase(const char *s, u32 n, const char *suffix)
{
	u32 m = (u32)strlen(suffix);
	if (n < m)
		return 0;
	for (u32 i = 0; i < m; i++)
		if (lower(s[n - m + i]) != suffix[i])
			return 0;
	return 1;
}

u32 orphanplan_slot_of(const char *name, const char **rest)
{
	static const char *const endings[] = {"", ".tmp", ".bak", ".new", ".ren"};
	u32 n = (u32)strlen(name);
	for (u32 e = 0; e < sizeof(endings) / sizeof(endings[0]); e++) {
		u32 el = (u32)strlen(endings[e]);
		if (n < el + 4 + 1)
			continue;
		u32 end = n - el;
		if (el && !ends_nocase(name, n, endings[e]))
			continue;
		if (!ends_nocase(name, end, ".sav"))
			continue;
		if (rest)
			*rest = name + end - 4;
		return end - 4;
	}
	return 0;
}

/* ---- Names ---- */

// Copies the first chars characters of s (whole UTF-8 sequences) into out
static void copy_chars(char *out, const char *s, u32 chars)
{
	u32 n = 0;
	while (*s && chars) {
		u32 len = 1;
		while ((s[len] & 0xC0) == 0x80)
			len++;
		memcpy(out + n, s, len);
		n += len;
		s += len;
		chars--;
	}
	out[n] = 0;
}

int orphanplan_pick_name(char *out, const char *name, orphanplan_taken_fn taken, void *ctx)
{
	if (!taken(ctx, name)) {
		snprintf(out, SAVESLOT_NAME_BYTES, "%s", name);
		return 1;
	}
	for (u32 k = 2; k < 100; k++) {
		char suffix[4];
		u32 sl = (u32)snprintf(suffix, sizeof(suffix), " %u", (unsigned)k);
		char base[SAVESLOT_NAME_BYTES];
		copy_chars(base, name, SAVESLOT_NAME_CHARS - sl);
		// A cut can leave a space at the end ("Boss fight" cut after "Boss "):
		// trim it so the clean name keeps the suffix apart
		u32 bl = (u32)strlen(base);
		while (bl && base[bl - 1] == ' ')
			base[--bl] = 0;
		memcpy(out, base, bl);
		memcpy(out + bl, suffix, sl + 1);
		if (!taken(ctx, out))
			return 1;
	}
	out[0] = 0;
	return 0;
}

/* ---- The journal ---- */

static int text_ok(const char *s)
{
	if (!*s)
		return 0;
	for (; *s; s++)
		if ((unsigned char)*s < 0x20 || *s == 0x7F)
			return 0;
	return 1;
}

u32 orphanplan_write(char *out, u32 size, char kind, const char *from, const char *to, const char *const *x,
		     const char *const *y, u32 n)
{
	if (n > ORPHANPLAN_MAX || size < 64 || (kind != ORPHANPLAN_RELINK && kind != ORPHANPLAN_TRASH))
		return 0;
	if (!text_ok(from) || !text_ok(to))
		return 0;
	int len = snprintf(out, size - 10, HEADER "%c\n%s\n%s\n%u\n", kind, from, to, (unsigned)n);
	if (len <= 0 || (u32)len >= size - 10)
		return 0;
	for (u32 i = 0; i < n; i++) {
		if (!text_ok(x[i]) || !text_ok(y[i]))
			return 0;
		int k = snprintf(out + len, size - 10 - (u32)len, "%s\t%s\n", x[i], y[i]);
		if (k <= 0 || (u32)k >= size - 10 - (u32)len)
			return 0;
		len += k;
	}
	len += snprintf(out + len, 10, "%08lx\n", (unsigned long)crc32(out, (u32)len));
	return (u32)len;
}

// The next line of the text at *p, cut at its newline; NULL at the end
static char *next_line(char **p)
{
	char *s = *p;
	char *nl = strchr(s, '\n');
	if (!nl)
		return NULL;
	*nl = 0;
	*p = nl + 1;
	return s;
}

int orphanplan_read(char *data, u32 len, struct orphanplan_job *job)
{
	if (len < sizeof(HEADER) + 10 || data[len - 1] != '\n')
		return 0;
	u32 body = len - 9;
	char hex[9], want[9];
	memcpy(hex, data + body, 8);
	hex[8] = 0;
	snprintf(want, sizeof(want), "%08lx", (unsigned long)crc32(data, body));
	if (strcmp(hex, want) || memcmp(data, HEADER, sizeof(HEADER) - 1))
		return 0;
	data[body] = 0;
	char *p = data + sizeof(HEADER) - 1;
	char *kind = next_line(&p);
	job->from = next_line(&p);
	job->to = next_line(&p);
	char *count = next_line(&p);
	if (!kind || !job->from || !job->to || !count || kind[1] || (kind[0] != ORPHANPLAN_RELINK && kind[0] != ORPHANPLAN_TRASH))
		return 0;
	job->kind = kind[0];
	if (job->from[0] != '/' || job->to[0] != '/' || count[0] < '0' || count[0] > '9')
		return 0;
	u32 n = 0;
	for (const char *c = count; *c; c++) {
		if (*c < '0' || *c > '9' || n > ORPHANPLAN_MAX)
			return 0;
		n = n * 10 + (u32)(*c - '0');
	}
	if (n > ORPHANPLAN_MAX)
		return 0;
	job->n = n;
	for (u32 i = 0; i < n; i++) {
		char *line = next_line(&p);
		if (!line)
			return 0;
		char *tab = strchr(line, '\t');
		if (!tab || tab == line || !tab[1])
			return 0;
		*tab = 0;
		job->x[i] = line;
		job->y[i] = tab + 1;
	}
	return *p == 0;
}

/* ---- The relink picker ---- */

void orphanpick_begin(struct orphanpick *p, u32 total, u32 cs, u32 ce)
{
	memset(p, 0, sizeof(*p));
	p->total = total;
	if (ce > total)
		ce = total;
	if (cs > ce)
		cs = ce;
	p->cs = cs;
	p->ce = ce;
}

void orphanpick_add(struct orphanpick *p, u32 pos)
{
	if (p->nn >= ORPHANPICK_NAMES || pos >= p->total)
		return;
	for (u32 i = 0; i < p->nn; i++)
		if (p->first[i] == pos)
			return;
	p->first[p->nn] = pos;
	u32 i = p->nn++;
	while (i && p->sorted[i - 1] > pos) {
		p->sorted[i] = p->sorted[i - 1];
		i--;
	}
	p->sorted[i] = pos;
}

// The listed positions within [lo, hi)
static u32 listed_in(const struct orphanpick *p, u32 lo, u32 hi)
{
	u32 n = 0;
	for (u32 i = 0; i < p->nn; i++)
		n += p->sorted[i] >= lo && p->sorted[i] < hi;
	return n;
}

// The kth position from lo that is not listed
static u32 nth_free(const struct orphanpick *p, u32 lo, u32 k)
{
	u32 q = lo + k;
	for (u32 i = 0; i < p->nn; i++)
		if (p->sorted[i] >= lo && p->sorted[i] <= q)
			q++;
	return q;
}

u32 orphanpick_pos(const struct orphanpick *p, u32 row)
{
	if (row < p->nn)
		return p->first[row];
	u32 k = row - p->nn;
	u32 la = (p->ce - p->cs) - listed_in(p, p->cs, p->ce);
	if (k < la)
		return nth_free(p, p->cs, k);
	k -= la;
	u32 lb = p->cs - listed_in(p, 0, p->cs);
	if (k < lb)
		return nth_free(p, 0, k);
	return nth_free(p, p->ce, k - lb);
}

u32 orphanpick_group(const struct orphanpick *p, u32 row)
{
	if (row < p->nn)
		return 0;
	return row - p->nn < (p->ce - p->cs) - listed_in(p, p->cs, p->ce) ? 1 : 2;
}
