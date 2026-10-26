// A game's cheat file in libretro's format (cheatfile.h).
#include "cheatfile.h"

#include <stdio.h>
#include <string.h>

static int is_space(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f';
}

static int is_key_char(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

enum {
	KEY_DESC,
	KEY_CODE,
	KEY_ENABLE,
	KEY_OTHER,
};

// A key "cheatN_desc", "_code" or "_enable": which, with N in *number (0 to
// max - 1, else max); KEY_OTHER for any other key
static u32 cheat_key(const char *k, u32 len, u32 max, u32 *number)
{
	if (len < 7 || memcmp(k, "cheat", 5) != 0)
		return KEY_OTHER;
	u32 i = 5, n = 0, digits = 0;
	for (; i < len && k[i] >= '0' && k[i] <= '9'; i++, digits++)
		n = n < max ? n * 10 + (u32)(k[i] - '0') : max;
	if (!digits || i >= len || k[i] != '_')
		return KEY_OTHER;
	const char *what = k + i + 1;
	u32 wl = len - i - 1;
	*number = n < max ? n : max;
	if (wl == 4 && !memcmp(what, "desc", 4))
		return KEY_DESC;
	if (wl == 4 && !memcmp(what, "code", 4))
		return KEY_CODE;
	if (wl == 6 && !memcmp(what, "enable", 6))
		return KEY_ENABLE;
	return KEY_OTHER;
}

// 1 if the value is "true" in any case, or "1" (RetroArch reads its bools
// so)
static int is_true(const char *v, u32 len)
{
	static const char t[] = "true";
	if (len == 1)
		return v[0] == '1';
	if (len != 4)
		return 0;
	for (u32 i = 0; i < 4; i++)
		if ((v[i] | 0x20) != t[i])
			return 0;
	return 1;
}

// One line, s to e (its '\n' left out): a KEY = VALUE line, into the table
// by number
static void parse_line(struct cheatfile *f, const char *s, const char *e, u32 max)
{
	if (e > s && e[-1] == '\r')
		e--;
	while (s < e && is_space(*s))
		s++;
	if (s == e || *s == '#')
		return;  // a comment, or nothing
	const char *k = s;
	while (s < e && is_key_char(*s))
		s++;
	u32 klen = (u32)(s - k);
	while (s < e && is_space(*s))
		s++;
	if (!klen || s == e || *s != '=')
		return;
	s++;
	while (s < e && is_space(*s))
		s++;
	while (e > s && is_space(e[-1]))
		e--;
	if (s < e && *s == '"')
		s++;
	if (s < e && e[-1] == '"')
		e--;
	u32 n, what = cheat_key(k, klen, max, &n);
	if (what == KEY_OTHER)
		return;
	if (n >= max) {
		f->dropped += what == KEY_CODE;
		return;
	}
	struct cheatfile_cheat *c = &f->cheats[n];
	u16 at = (u16)(s - f->text), len = (u16)(e - s);
	if (what == KEY_DESC) {
		c->desc = at;
		c->desc_len = len;
	} else if (what == KEY_CODE) {
		c->code = at;
		c->code_len = len;
	} else if (len <= 0xFF) {  // a longer value isn't an enable line we could rewrite
		c->enable = at;
		c->enable_len = (u8)len;
		c->picked = c->was = (u8)is_true(s, len);
	}
}

void cheatfile_parse(struct cheatfile *f, const char *text, u32 len, struct cheatfile_cheat *cheats, u32 max)
{
	u32 cut = len > CHEATFILE_TEXT_MAX;
	if (cut)
		len = CHEATFILE_TEXT_MAX;
	if (max > CHEATFILE_MAX)
		max = CHEATFILE_MAX;
	memset(f, 0, sizeof(*f));
	f->cut = cut;
	f->text = text;
	f->len = len;
	f->cheats = cheats;
	for (u32 i = 0; i < max; i++)
		cheats[i] = (struct cheatfile_cheat){
			CHEATFILE_NONE, 0, CHEATFILE_NONE, 0, CHEATFILE_NONE, 0, CHEATFILE_NEW, (u16)i, 0, 0, 0, 0, 0};
	const char *p = text, *end = text + len;
	if (len >= 3 && !memcmp(text, "\xEF\xBB\xBF", 3))
		p += 3;  // a BOM from a Windows editor: kept, but not part of the first key
	int first = 1;
	while (p < end) {
		const char *nl = memchr(p, '\n', (size_t)(end - p));
		const char *e = nl ? nl : end;
		if (first && nl) {
			f->crlf = e > p && e[-1] == '\r';
			first = 0;
		}
		parse_line(f, p, e, max);
		p = nl ? nl + 1 : end;
	}
	// The cheats with a code, in their numbers' order
	u32 n = 0;
	for (u32 i = 0; i < max; i++)
		if (cheats[i].code != CHEATFILE_NONE)
			cheats[n++] = cheats[i];
	f->count = n;
}

void cheatfile_begin(struct cheatfile_work *w)
{
	cheat_decoder_init(&w->dec);
	w->next = 0;
	w->n_prog = w->n_patches = w->cost = 0;
}

static u8 status_of(struct cheatfile_work *w, const char *code, u32 len)
{
	u32 n_ops = 0;
	w->n_prog = w->n_patches = w->cost = 0;
	switch (cheat_decode(&w->dec, code, len, w->ops, CHEATFILE_OPS_MAX, &n_ops)) {
	case CHEAT_OK:
		break;
	case CHEAT_MASTER_ONLY:
		return CHEATFILE_MASTER;
	case CHEAT_PLACEHOLDER:
		return CHEATFILE_PLACEHOLDER;
	case CHEAT_TOO_BIG:
		return CHEATFILE_BIG;
	default:
		return CHEATFILE_UNREADABLE;
	}
	u8 lowered = cheat_lower(w->ops, n_ops, w->prog, CHEATFILE_PROG_MAX, &w->n_prog, &w->cost, w->patches,
				 CHEATFILE_PATCHES_MAX, &w->n_patches);
	switch (lowered) {
	case CHEAT_LOWER_OK:
		return CHEATFILE_OK;
	case CHEAT_LOWER_TOO_BIG:
	case CHEAT_LOWER_TOO_MANY_PATCHES:
		return CHEATFILE_BIG;
	case CHEAT_LOWER_CANT_RUN:
		return CHEATFILE_CANT_RUN;
	default:
		return CHEATFILE_UNREADABLE;
	}
}

s32 cheatfile_next(struct cheatfile *f, struct cheatfile_work *w)
{
	if (w->next >= f->count)
		return -1;
	u32 i = w->next++;
	struct cheatfile_cheat *c = &f->cheats[i];
	if (c->status == CHEATFILE_OK && c->picked) {
		f->bytes -= c->bytes;  // decoded again
		f->cost -= c->cost;
	}
	c->status = status_of(w, f->text + c->code, c->code_len);
	if (c->status != CHEATFILE_OK)
		w->n_prog = w->n_patches = w->cost = 0;
	c->bytes = (u16)(w->n_prog * 4);
	c->cost = (u16)w->cost;
	c->patches = (u16)w->n_patches;
	if (c->status == CHEATFILE_OK && c->picked) {
		f->bytes += c->bytes;
		f->cost += c->cost;
	}
	return (s32)i;
}

u32 cheatfile_why(const struct cheatfile *f, u32 i)
{
	if (i >= f->count)
		return CHEATFILE_WHY_UNREADABLE;
	const struct cheatfile_cheat *c = &f->cheats[i];
	if (c->picked)
		return CHEATFILE_PICKABLE;
	switch (c->status) {
	case CHEATFILE_OK:
		return f->bytes + c->bytes > CHEATFILE_BYTES_MAX || f->cost + c->cost > CHEATFILE_CYCLES_MAX
			       ? CHEATFILE_WHY_FULL
			       : CHEATFILE_PICKABLE;
	case CHEATFILE_BIG:
		return CHEATFILE_WHY_FULL;
	case CHEATFILE_PLACEHOLDER:
		return CHEATFILE_WHY_EDIT;
	case CHEATFILE_CANT_RUN:
		return CHEATFILE_WHY_CANT_RUN;
	default:
		return CHEATFILE_WHY_UNREADABLE;
	}
}

int cheatfile_toggle(struct cheatfile *f, u32 i)
{
	if (cheatfile_why(f, i) != CHEATFILE_PICKABLE)
		return 0;
	struct cheatfile_cheat *c = &f->cheats[i];
	c->picked = !c->picked;
	if (c->status == CHEATFILE_OK) {
		if (c->picked) {
			f->bytes += c->bytes;
			f->cost += c->cost;
		} else {
			f->bytes -= c->bytes;
			f->cost -= c->cost;
		}
	}
	return 1;
}

int cheatfile_changed(const struct cheatfile *f)
{
	for (u32 i = 0; i < f->count; i++)
		if (f->cheats[i].picked != f->cheats[i].was)
			return 1;
	return 0;
}

u32 cheatfile_picks(const struct cheatfile *f)
{
	u32 n = 0;
	for (u32 i = 0; i < f->count; i++)
		n += f->cheats[i].picked;
	return n;
}

// --- Writing back ---------------------------------------------------------------------

static const char *const pick_text[2] = {"false", "true"};

// A cheat whose enable line's value changes
static int edited(const struct cheatfile_cheat *c)
{
	return c->enable != CHEATFILE_NONE && c->picked != c->was;
}

// A cheat that gets an enable line at the end
static int added(const struct cheatfile_cheat *c)
{
	return c->enable == CHEATFILE_NONE && c->picked;
}

// The text offset of the first enable value to change at or after t; past
// the text if none
static u32 next_edit(const struct cheatfile *f, u32 t, u16 *cheat)
{
	u32 best = f->len + 1;
	for (u32 i = 0; i < f->count; i++) {
		const struct cheatfile_cheat *c = &f->cheats[i];
		if (edited(c) && c->enable >= t && c->enable < best) {
			best = c->enable;
			*cheat = (u16)i;
		}
	}
	return best;
}

// 1 if the text doesn't end its last line: an added line starts a new one
static int open_end(const struct cheatfile *f)
{
	return f->len && f->text[f->len - 1] != '\n';
}

// cheat i's added line into out (40 bytes), with a line break before it
// when brk; its length
static u32 added_line(const struct cheatfile *f, u32 i, int brk, char *out)
{
	const char *eol = f->crlf ? "\r\n" : "\n";
	return (u32)snprintf(out, 40, "%scheat%u_enable = true%s", brk ? eol : "", (unsigned)f->cheats[i].number, eol);
}

u32 cheatfile_text_len(const struct cheatfile *f)
{
	u32 len = f->len, any = 0;
	char line[40];
	for (u32 i = 0; i < f->count; i++) {
		const struct cheatfile_cheat *c = &f->cheats[i];
		if (edited(c))
			len = len - c->enable_len + (u32)strlen(pick_text[c->picked]);
		if (added(c)) {
			len += added_line(f, i, !any && open_end(f), line);
			any = 1;
		}
	}
	return len;
}

u32 cheatfile_text(void *ctx, u32 pos, char *buf, u32 max)
{
	struct cheatfile *f = ctx;
	struct cheatfile_out *o = &f->out;
	if (pos == 0 || pos != o->pos) {
		memset(o, 0, sizeof(*o));
		o->cheat = CHEATFILE_NONE;
		o->edit = next_edit(f, 0, &o->cheat);
		if (pos) {
			// Only ever asked for from 0, then on from the last call: anything
			// else gets nothing
			o->pos = pos;
			o->tail = 2;
			return 0;
		}
	}
	u32 n = 0;
	while (n < max) {
		if (o->tail == 2)
			break;
		if (o->tail) {
			// The added lines, one at a time (tail 1 before the first, 3 after)
			if (o->k == o->n) {
				while (o->added < f->count && !added(&f->cheats[o->added]))
					o->added++;
				if (o->added >= f->count) {
					o->tail = 2;
					break;
				}
				o->n = (u8)added_line(f, o->added, o->tail == 1 && open_end(f), o->line);
				o->k = 0;
				o->added++;
				o->tail = 3;  // a line given: the next needs no break before it
			}
			buf[n++] = o->line[o->k++];
			continue;
		}
		if (o->t == o->edit) {
			// The new value, then on past the old one
			const char *v = pick_text[f->cheats[o->cheat].picked];
			u32 vl = (u32)strlen(v);
			while (o->k < vl && n < max)
				buf[n++] = v[o->k++];
			if (o->k < vl)
				break;
			o->k = 0;
			o->t += f->cheats[o->cheat].enable_len;
			o->edit = next_edit(f, o->t, &o->cheat);
			continue;
		}
		if (o->t >= f->len) {
			o->tail = 1;
			o->k = o->n = 0;
			o->added = 0;
			continue;
		}
		u32 stop = o->edit < f->len ? o->edit : f->len, take = stop - o->t;
		if (take > max - n)
			take = max - n;
		memcpy(buf + n, f->text + o->t, take);
		n += take;
		o->t += take;
	}
	o->pos = pos + n;
	return n;
}
