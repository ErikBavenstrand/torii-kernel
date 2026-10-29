// A larger cheat file's index (cheatindex.h).
#include "cheatindex.h"

#include <stdio.h>
#include <string.h>

#include "cheatfile.h"

void cheatindex_cut_begin(struct cheatindex_cut *c)
{
	memset(c, 0, sizeof(*c));
	c->number = 0xFFFFFFFFu;
}

int cheatindex_cut_line(struct cheatindex_cut *c, const char *l, u32 len, u32 line_at)
{
	// "cheatN_" read here, the key parsed whole only for an enable line (its
	// value): most lines are a cheat's, and this runs for each
	u32 i = 0, n = 0, d = 0;
	int on = 0, cut = CHEATINDEX_LINE;
	while (i < len && (l[i] == ' ' || l[i] == '\t'))
		i++;
	if (i + 7 > len || memcmp(l + i, "cheat", 5) != 0)
		return CHEATINDEX_LINE;
	for (i += 5; i < len && l[i] >= '0' && l[i] <= '9' && d < 6; i++, d++)
		n = n * 10 + (u32)(l[i] - '0');
	if (!d || i >= len || l[i] != '_')
		return CHEATINDEX_LINE;
	if (len - i > 7 && !memcmp(l + i + 1, "enable", 6) && !cheatfile_line_key(l, len, &n, &on))
		return CHEATINDEX_LINE;
	int code = len - i > 5 && !memcmp(l + i + 1, "code", 4) && (l[i + 5] == ' ' || l[i + 5] == '=' || l[i + 5] == '\t');
	if (n >= CHEATINDEX_NUMBER_MAX)
		return CHEATINDEX_BAD;
	if (c->number != 0xFFFFFFFFu && n < c->number) {
		// An earlier cheat of this window's line: the kernel writes a picked
		// cheat's new enable line at the end of its window's text. One of
		// an earlier window's would put a cheat's lines apart.
		if (!c->any || n < c->first)
			return CHEATINDEX_BAD;
		c->picks += (u32)on;
		return CHEATINDEX_LINE;
	}
	if (n != c->number) {
		// A new cheat: a new window first if this one is full
		if (c->any && (c->count >= CHEATINDEX_CHEATS || n - c->first >= CHEATINDEX_CHEATS ||
			       line_at - c->at >= CHEATINDEX_BYTES)) {
			c->window++;
			c->at = line_at;
			c->count = c->picks = 0;
			c->any = 0;
			cut = CHEATINDEX_NEW;
		}
		if (!c->any)
			c->first = n;
		c->any = 1;
		c->number = n;
		c->counted = 0;
	}
	if (code && !c->counted) {
		c->count++;  // a cheat counts once it has a code, as cheatfile_parse counts it
		c->counted = 1;
	}
	c->picks += (u32)on;
	return cut;
}

// --- The index's lines -----------------------------------------------------------

u32 cheatindex_head(char *out, u32 file_size)
{
	return (u32)snprintf(out, CHEATINDEX_LINE_MAX, "%s\nfile %lX\n", CHEATINDEX_MAGIC, (unsigned long)file_size);
}

// The decoder's fields as hex, in a fixed order and width (no struct
// padding in it, so the PC's tools write what the kernel reads)
static u32 put_hex(char *out, const void *p, u32 bytes)
{
	static const char digits[] = "0123456789ABCDEF";
	const u8 *b = p;
	for (u32 i = 0; i < bytes; i++) {
		out[2 * i] = digits[b[i] >> 4];
		out[2 * i + 1] = digits[b[i] & 15];
	}
	return 2 * bytes;
}

static int get_hex(const char **s, const char *end, void *p, u32 bytes)
{
	u8 *b = p;
	for (u32 i = 0; i < 2 * bytes; i++) {
		if (*s >= end)
			return 0;
		char ch = *(*s)++;
		u32 v = ch >= '0' && ch <= '9' ? (u32)(ch - '0') : ch >= 'A' && ch <= 'F' ? (u32)(ch - 'A' + 10) : 16;
		if (v == 16)
			return 0;
		b[i / 2] = (u8)(i & 1 ? b[i / 2] | v : v << 4);
	}
	return 1;
}

// Words go as their bytes, low first: the same on the GBA and the PC
static u32 put_words(char *out, const u32 *w, u32 n)
{
	u32 k = 0;
	for (u32 i = 0; i < n; i++) {
		u8 b[4] = {(u8)w[i], (u8)(w[i] >> 8), (u8)(w[i] >> 16), (u8)(w[i] >> 24)};
		k += put_hex(out + k, b, 4);
	}
	return k;
}

static int get_words(const char **s, const char *end, u32 *w, u32 n)
{
	for (u32 i = 0; i < n; i++) {
		u8 b[4];
		if (!get_hex(s, end, b, 4))
			return 0;
		w[i] = b[0] | (u32)b[1] << 8 | (u32)b[2] << 16 | (u32)b[3] << 24;
	}
	return 1;
}

static u32 put_state(char *out, const struct cheat_decoder *d)
{
	u8 small[4] = {d->forced, d->gs_version, d->last, d->last_alone};
	u32 key2 = d->cb_key2, k = 0;
	k += put_hex(out + k, small, 4);
	k += put_hex(out + k, d->cb_table, sizeof(d->cb_table));
	k += put_words(out + k, &key2, 1);
	k += put_words(out + k, d->gs_seeds, 4);
	k += put_words(out + k, &d->cb_master, 1);
	k += put_words(out + k, d->cb_seeds, 4);
	k += put_words(out + k, &d->cb_rng, 1);
	return k;
}

static int get_state(const char **s, const char *end, struct cheat_decoder *d)
{
	u8 small[4];
	u32 key2;
	memset(d, 0, sizeof(*d));
	if (!get_hex(s, end, small, 4) || !get_hex(s, end, d->cb_table, sizeof(d->cb_table)) ||
	    !get_words(s, end, &key2, 1) || !get_words(s, end, d->gs_seeds, 4) || !get_words(s, end, &d->cb_master, 1) ||
	    !get_words(s, end, d->cb_seeds, 4) || !get_words(s, end, &d->cb_rng, 1))
		return 0;
	d->forced = small[0];
	d->gs_version = small[1];
	d->last = small[2];
	d->last_alone = small[3];
	d->cb_key2 = (u16)key2;
	return 1;
}

u32 cheatindex_window_line(char *out, const struct cheatindex_window *w)
{
	u32 k = (u32)snprintf(out, CHEATINDEX_LINE_MAX, "w %lX %lX %X %X %08lX ", (unsigned long)w->at,
			      (unsigned long)w->len, w->first, w->count, (unsigned long)w->crc);
	k += put_state(out + k, &w->dec);
	out[k++] = '\n';
	out[k] = 0;
	return k;
}

u32 cheatindex_pick_line(char *out, u32 number, u32 hash, u32 bytes, u32 cost, u32 patches)
{
	return (u32)snprintf(out, CHEATINDEX_LINE_MAX, "p %lX %08lX %lX %lX %lX\n", (unsigned long)number,
			     (unsigned long)hash, (unsigned long)bytes, (unsigned long)cost, (unsigned long)patches);
}

// A hex field and the space after it
static int get_field(const char **s, const char *end, u32 *v)
{
	u32 n = 0, d = 0;
	for (; *s < end && **s != ' '; (*s)++, d++) {
		char ch = **s;
		u32 x = ch >= '0' && ch <= '9' ? (u32)(ch - '0') : ch >= 'A' && ch <= 'F' ? (u32)(ch - 'A' + 10) : 16;
		if (x == 16 || d == 8)
			return 0;
		n = n << 4 | x;
	}
	if (!d)
		return 0;
	if (*s < end)
		(*s)++;
	*v = n;
	return 1;
}

int cheatindex_read_line(const char *line, u32 len, struct cheatindex_line *out)
{
	const char *s = line + 2, *end = line + len;
	u32 v[5];
	if (len == sizeof(CHEATINDEX_MAGIC) - 1 && !memcmp(line, CHEATINDEX_MAGIC, len))
		return CHEATINDEX_MAGIC_LINE;
	if (len > 5 && !memcmp(line, "file ", 5)) {
		s = line + 5;
		return get_field(&s, end, &out->size) && s == end ? CHEATINDEX_FILE : CHEATINDEX_OTHER;
	}
	if (len < 3 || line[1] != ' ')
		return CHEATINDEX_OTHER;
	switch (line[0]) {
	case 'w':
		for (u32 i = 0; i < 5; i++)
			if (!get_field(&s, end, &v[i]))
				return CHEATINDEX_OTHER;
		if (v[2] > 0xFFFF || v[3] > 0xFFFF || !get_state(&s, end, &out->w.dec) || s != end)
			return CHEATINDEX_OTHER;
		out->w.at = v[0];
		out->w.len = v[1];
		out->w.first = (u16)v[2];
		out->w.count = (u16)v[3];
		out->w.crc = v[4];
		return CHEATINDEX_WINDOW;
	case 'p':
		if (!get_field(&s, end, &out->number) || !get_field(&s, end, &out->hash) ||
		    !get_field(&s, end, &out->bytes) || !get_field(&s, end, &out->cost) ||
		    !get_field(&s, end, &out->patches) || s != end)
			return CHEATINDEX_OTHER;
		return CHEATINDEX_PICK;
	default:
		return CHEATINDEX_OTHER;
	}
}
