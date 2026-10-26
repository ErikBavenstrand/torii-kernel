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
	KEY_FORMAT,
	KEY_USER,
	KEY_CHEAT,  // another cheatN_ key: kept, and numbered with its cheat
	KEY_COUNT,  // "cheats"
	KEY_OTHER,
};

const char *const cheatfile_format_names[CHEAT_FMT_VBA + 1] = {
	NULL, "GSAv1", "GSAv1 raw", "PARv3", "PARv3 raw", "CodeBreaker", "VBA",
};

// A key "cheatN_desc", "_code", "_enable", "_format", "_user" or another
// "cheatN_" one: which, with N in *number (0 to max - 1, else max) and its
// digits' count in *digits; KEY_COUNT for "cheats", KEY_OTHER for any other
// key
static u32 cheat_key(const char *k, u32 len, u32 max, u32 *number, u32 *digits)
{
	if (len == 6 && !memcmp(k, "cheats", 6))
		return KEY_COUNT;
	if (len < 7 || memcmp(k, "cheat", 5) != 0)
		return KEY_OTHER;
	u32 i = 5, n = 0, d = 0;
	for (; i < len && k[i] >= '0' && k[i] <= '9'; i++, d++)
		n = n < max ? n * 10 + (u32)(k[i] - '0') : max;
	if (!d || i >= len || k[i] != '_')
		return KEY_OTHER;
	const char *what = k + i + 1;
	u32 wl = len - i - 1;
	*number = n < max ? n : max;
	*digits = d;
	if (wl == 4 && !memcmp(what, "desc", 4))
		return KEY_DESC;
	if (wl == 4 && !memcmp(what, "code", 4))
		return KEY_CODE;
	if (wl == 6 && !memcmp(what, "enable", 6))
		return KEY_ENABLE;
	if (wl == 6 && !memcmp(what, "format", 6))
		return KEY_FORMAT;
	if (wl == 4 && !memcmp(what, "user", 4))
		return KEY_USER;
	return KEY_CHEAT;
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

// A cheatN_format value's format, in any case; CHEAT_FMT_AUTO for one
// unknown
static u32 format_of(const char *v, u32 len)
{
	for (u32 f = CHEAT_FMT_GSAV1; f <= CHEAT_FMT_VBA; f++) {
		const char *name = cheatfile_format_names[f];
		u32 i = 0;
		if (strlen(name) != len)
			continue;
		while (i < len && (v[i] | 0x20) == (name[i] | 0x20))
			i++;
		if (i == len)
			return f;
	}
	return CHEAT_FMT_AUTO;
}

// One line of a text: [start, end), its '\n' included; a KEY = VALUE line's
// key and value (without a quote at each end), else klen 0
struct kv {
	u32 start, end;
	u32 key, klen;
	u32 val, vlen;
	u8 blank;  // nothing but spaces
};

// Where the lines start: past a BOM from a Windows editor (kept, but not
// part of the first key)
static u32 text_start(const char *text, u32 len)
{
	return len >= 3 && !memcmp(text, "\xEF\xBB\xBF", 3) ? 3 : 0;
}

// The line at *pos into kv, *pos past it; 0 at the text's end
static int next_kv(const char *text, u32 len, u32 *pos, struct kv *kv)
{
	if (*pos >= len)
		return 0;
	const char *p = text + *pos, *end = text + len;
	const char *nl = memchr(p, '\n', (size_t)(end - p));
	const char *s = p, *e = nl ? nl : end;
	kv->start = *pos;
	kv->end = (u32)((nl ? nl + 1 : end) - text);
	kv->klen = 0;
	*pos = kv->end;
	if (e > s && e[-1] == '\r')
		e--;
	while (s < e && is_space(*s))
		s++;
	kv->blank = s == e;
	if (s == e || *s == '#')
		return 1;  // a comment, or nothing
	const char *k = s;
	while (s < e && is_key_char(*s))
		s++;
	u32 klen = (u32)(s - k);
	while (s < e && is_space(*s))
		s++;
	if (!klen || s == e || *s != '=')
		return 1;
	s++;
	while (s < e && is_space(*s))
		s++;
	while (e > s && is_space(e[-1]))
		e--;
	if (s < e && *s == '"')
		s++;
	if (s < e && e[-1] == '"')
		e--;
	kv->key = (u32)(k - text);
	kv->klen = klen;
	kv->val = (u32)(s - text);
	kv->vlen = (u32)(e - s);
	return 1;
}

// A KEY = VALUE line, into the table by number
static void parse_kv(struct cheatfile *f, const struct kv *kv, u32 max)
{
	u32 n = 0, digits;
	u32 what = cheat_key(f->text + kv->key, kv->klen, max, &n, &digits);
	const char *s = f->text + kv->val;
	u16 at = (u16)kv->val, len = (u16)kv->vlen;
	if (what == KEY_COUNT) {
		f->count_at = at;
		f->count_len = len;
		return;
	}
	if (what == KEY_OTHER)
		return;
	if (n >= max) {
		f->dropped += what == KEY_CODE;
		return;
	}
	if (n >= f->next)
		f->next = (u16)(n + 1);
	struct cheatfile_cheat *c = &f->cheats[n];
	if (what == KEY_DESC) {
		c->desc = at;
		c->desc_len = len;
	} else if (what == KEY_CODE) {
		c->code = at;
		c->code_len = len;
	} else if (what == KEY_FORMAT) {
		c->format = format_of(s, len) & 7;
	} else if (what == KEY_USER) {
		c->own = is_true(s, len) != 0;
	} else if (what == KEY_ENABLE && len <= 0xFF) {  // a longer value isn't an enable line we could rewrite
		c->enable = at;
		c->enable_len = (u8)len;
		c->picked = c->was = c->saved = is_true(s, len) != 0;
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
	f->max = (u16)max;
	f->count_at = CHEATFILE_NONE;
	for (u32 i = 0; i < max; i++)
		cheats[i] = (struct cheatfile_cheat){.desc = CHEATFILE_NONE,
						     .code = CHEATFILE_NONE,
						     .enable = CHEATFILE_NONE,
						     .status = CHEATFILE_NEW,
						     .number = (u16)i};
	u32 pos = text_start(text, len);
	struct kv kv;
	int first = 1;
	while (next_kv(text, len, &pos, &kv)) {
		if (first && text[kv.end - 1] == '\n') {
			f->crlf = kv.end - kv.start >= 2 && text[kv.end - 2] == '\r';
			first = 0;
		}
		if (kv.klen)
			parse_kv(f, &kv, max);
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

u8 cheatfile_decode(struct cheatfile_work *w, struct cheat_decoder *dec, const char *code, u32 len, u32 *ops,
		    u8 *lowered)
{
	u32 n_ops = 0;
	w->n_prog = w->n_patches = w->cost = 0;
	*lowered = CHEATFILE_NOT_LOWERED;
	u8 decoded = cheat_decode(dec, code, len, w->ops, CHEATFILE_OPS_MAX, &n_ops);
	*ops = n_ops;
	switch (decoded) {
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
	*lowered = cheat_lower(w->ops, n_ops, w->prog, CHEATFILE_PROG_MAX, &w->n_prog, &w->cost, w->patches,
			       CHEATFILE_PATCHES_MAX, &w->n_patches);
	switch (*lowered) {
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
	u32 n_ops;
	u8 lowered;
	if (c->format != CHEAT_FMT_AUTO) {
		// Read alone in its own format: the file's key and seeds pass it by
		struct cheat_decoder own;
		cheat_decoder_force(&own, c->format);
		c->status = cheatfile_decode(w, &own, f->text + c->code, c->code_len, &n_ops, &lowered);
	} else {
		c->status = cheatfile_decode(w, &w->dec, f->text + c->code, c->code_len, &n_ops, &lowered);
	}
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

void cheatfile_written(struct cheatfile *f)
{
	for (u32 i = 0; i < f->count; i++)
		f->cheats[i].saved = f->cheats[i].picked;
	f->edited = 0;
}

int cheatfile_changed(const struct cheatfile *f)
{
	if (f->edited)
		return 1;
	for (u32 i = 0; i < f->count; i++)
		if (f->cheats[i].picked != f->cheats[i].saved)
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

// --- Edits --------------------------------------------------------------------------

// Room an add keeps for the count line's change
#define COUNT_ROOM 24

// The picks by number, kept over a read again
struct picks {
	u8 bits[CHEATFILE_MAX / 8];
};

static void picks_save(const struct cheatfile *f, struct picks *p)
{
	memset(p, 0, sizeof(*p));
	for (u32 i = 0; i < f->count; i++)
		if (f->cheats[i].picked)
			p->bits[f->cheats[i].number >> 3] |= (u8)(1u << (f->cheats[i].number & 7));
}

// After a read again: each cheat's pick as it was, cheat number removed
// gone (CHEATFILE_NONE: none) and the numbers after it one lower
static void picks_restore(struct cheatfile *f, const struct picks *p, u32 removed)
{
	for (u32 i = 0; i < f->count; i++) {
		struct cheatfile_cheat *c = &f->cheats[i];
		u32 n = removed != CHEATFILE_NONE && c->number >= removed ? c->number + 1u : c->number;
		c->picked = n < CHEATFILE_MAX && (p->bits[n >> 3] >> (n & 7)) & 1;
	}
}

// The text in buf, len bytes, read again after an edit
static void reread(struct cheatfile *f, const char *buf, u32 len)
{
	cheatfile_parse(f, buf, len, f->cheats, f->max);
	f->edited = 1;
}

// Text written into place, or only counted (p NULL)
struct out {
	char *p;
	u32 n;
};

static void put(struct out *o, const char *s, u32 len)
{
	if (o->p)
		memcpy(o->p + o->n, s, len);
	o->n += len;
}

static void put_s(struct out *o, const char *s)
{
	put(o, s, (u32)strlen(s));
}

static void put_u(struct out *o, u32 v)
{
	char d[12];
	u32 n = (u32)snprintf(d, sizeof(d), "%lu", (unsigned long)v);
	put(o, d, n);
}

// A desc as a value: no control characters, '"' as '\''
static void put_desc(struct out *o, const char *s)
{
	for (; *s; s++) {
		char c = *s == '"' ? '\'' : *s;
		if ((u8)c >= 0x20 && c != 0x7F)
			put(o, &c, 1);
	}
}

// What goes in at a splice
enum {
	PIECE_NONE,   // nothing: the bytes go
	PIECE_VALUE,  // a value as it is
	PIECE_DESC,   // a desc's value
	PIECE_LINE,   // a line "cheatN_KEY = VALUE" (quoted unless bare), a line end after it
	PIECE_COUNT,  // the count's value
};

struct piece {
	u32 at, del;
	u8 kind;
	u8 brk;   // line ends before it
	u8 bare;  // PIECE_LINE: the value unquoted
	u8 desc;  // PIECE_LINE: the value a desc
	const char *key;
	const char *s;
	u32 number;
};

static void put_piece(struct out *o, const struct cheatfile *f, const struct piece *pc)
{
	const char *eol = f->crlf ? "\r\n" : "\n";
	for (u32 i = 0; i < pc->brk; i++)
		put_s(o, eol);
	switch (pc->kind) {
	case PIECE_VALUE:
		put_s(o, pc->s);
		break;
	case PIECE_DESC:
		put_desc(o, pc->s);
		break;
	case PIECE_LINE:
		put_s(o, "cheat");
		put_u(o, pc->number);
		put_s(o, "_");
		put_s(o, pc->key);
		put_s(o, pc->bare ? " = " : " = \"");
		if (pc->desc)
			put_desc(o, pc->s);
		else
			put_s(o, pc->s);
		put_s(o, pc->bare ? "" : "\"");
		put_s(o, eol);
		break;
	case PIECE_COUNT:
		put_u(o, pc->number);
		break;
	default:
		break;
	}
}

// The bytes the pieces add (less those they take), as a signed count
static s32 pieces_delta(const struct cheatfile *f, const struct piece *pcs, u32 n)
{
	s32 delta = 0;
	for (u32 i = 0; i < n; i++) {
		struct out o = {NULL, 0};
		put_piece(&o, f, &pcs[i]);
		delta += (s32)o.n - (s32)pcs[i].del;
	}
	return delta;
}

// The bytes the growing pieces add: apply takes them highest first, so the
// text can pass its final length by this much before a lower piece shrinks
static u32 pieces_grow(const struct cheatfile *f, const struct piece *pcs, u32 n)
{
	u32 grow = 0;
	for (u32 i = 0; i < n; i++) {
		struct out o = {NULL, 0};
		put_piece(&o, f, &pcs[i]);
		if (o.n > pcs[i].del)
			grow += o.n - pcs[i].del;
	}
	return grow;
}

// 1 if the text with delta more bytes, the picks written in, fits buf and
// the cheat file's cap
static int fits(const struct cheatfile *f, u32 cap, s32 delta)
{
	s32 picks = (s32)cheatfile_text_len(f) - (s32)f->len;
	s32 len = (s32)f->len + delta, out = len + picks;
	return len >= 0 && (u32)len <= cap && out <= CHEATFILE_TEXT_MAX;
}

// Applies the pieces (none overlapping, in any order) to buf, highest first
// so the lower ones' offsets hold; the text's new length
static u32 apply(const struct cheatfile *f, char *buf, struct piece *pcs, u32 n)
{
	u32 len = f->len;
	for (u32 i = 1; i < n; i++)
		for (u32 j = i; j > 0 && pcs[j - 1].at < pcs[j].at; j--) {
			struct piece t = pcs[j];
			pcs[j] = pcs[j - 1];
			pcs[j - 1] = t;
		}
	for (u32 i = 0; i < n; i++) {
		const struct piece *pc = &pcs[i];
		struct out o = {NULL, 0};
		put_piece(&o, f, pc);
		memmove(buf + pc->at + o.n, buf + pc->at + pc->del, len - pc->at - pc->del);
		o.p = buf + pc->at;
		o.n = 0;
		put_piece(&o, f, pc);
		len = len - pc->del + o.n;
	}
	return len;
}

// The count line's piece, to follow the cheats' numbers: 0 if it needs none
// (or there is none, and add is 0)
static u32 count_piece(const struct cheatfile *f, int add, struct piece *pc)
{
	*pc = (struct piece){0, 0, PIECE_COUNT, 0, 0, 0, NULL, NULL, f->next};
	if (f->count_at != CHEATFILE_NONE) {
		u32 v = 0, i;
		for (i = 0; i < f->count_len && f->text[f->count_at + i] >= '0' && f->text[f->count_at + i] <= '9'; i++)
			v = v < 100000 ? v * 10 + (u32)(f->text[f->count_at + i] - '0') : v;
		if (i == f->count_len && v == f->next)
			return 0;
		pc->at = f->count_at;
		pc->del = f->count_len;
		return 1;
	}
	if (!add || !f->next)
		return 0;
	// "cheats = N" and a blank line, at the top
	pc->at = text_start(f->text, f->len);
	pc->kind = PIECE_VALUE;
	return 1;
}

// The count line made to follow the numbers, the text read again
static void set_count(struct cheatfile *f, char *buf, u32 cap, int add)
{
	struct piece pc;
	if (!count_piece(f, add, &pc))
		return;
	char line[32];
	if (pc.kind == PIECE_VALUE) {
		const char *eol = f->crlf ? "\r\n" : "\n";
		snprintf(line, sizeof(line), "cheats = %u%s%s", (unsigned)f->next, eol, f->len ? eol : "");
		pc.s = line;
	}
	if (!fits(f, cap, pieces_delta(f, &pc, 1)))
		return;  // an add keeps room for it (COUNT_ROOM); a read takes the file without it all the same
	reread(f, buf, apply(f, buf, &pc, 1));
}

// 1 if code may go in a quoted value: no quote, no control character
static int code_ok(const char *code)
{
	if (!*code)
		return 0;
	for (; *code; code++)
		if (*code == '"' || (u8)*code < 0x20 || *code == 0x7F)
			return 0;
	return 1;
}

u32 cheatfile_add(struct cheatfile *f, char *buf, u32 cap, const char *desc, const char *code, u8 format, u32 *index)
{
	if (!code_ok(code) || format > CHEAT_FMT_VBA)
		return CHEATFILE_EDIT_BAD;
	u32 n = f->next;
	if (n >= f->max)
		return CHEATFILE_EDIT_FULL;
	// After a blank line (and the last line's end, if it has none), its
	// lines, in order at the end
	struct piece pcs[5];
	u32 k = 0, at = f->len;
	int any = f->len > text_start(f->text, f->len);
	int open = any && f->text[f->len - 1] != '\n';
	pcs[k++] = (struct piece){at, 0, PIECE_LINE, (u8)(any + open), 0, 1, "desc", desc, n};
	pcs[k++] = (struct piece){at, 0, PIECE_LINE, 0, 0, 0, "code", code, n};
	pcs[k++] = (struct piece){at, 0, PIECE_LINE, 0, 1, 0, "enable", "false", n};
	if (format != CHEAT_FMT_AUTO)
		pcs[k++] = (struct piece){at, 0, PIECE_LINE, 0, 0, 0, "format", cheatfile_format_names[format], n};
	pcs[k++] = (struct piece){at, 0, PIECE_LINE, 0, 1, 0, "user", "true", n};
	if (!fits(f, cap, pieces_delta(f, pcs, k) + COUNT_ROOM))
		return CHEATFILE_EDIT_LONG;
	struct picks p;
	picks_save(f, &p);
	u32 len = f->len;
	for (u32 i = 0; i < k; i++) {
		struct out o = {buf + len, 0};
		put_piece(&o, f, &pcs[i]);
		len += o.n;
	}
	reread(f, buf, len);
	set_count(f, buf, cap, 1);
	picks_restore(f, &p, CHEATFILE_NONE);
	f->edited = 1;
	*index = f->count - 1;
	return CHEATFILE_EDIT_OK;
}

// The text offset of the start of the line at, and past its end
static u32 line_start(const char *text, u32 at)
{
	while (at && text[at - 1] != '\n')
		at--;
	return at;
}

static u32 line_end(const char *text, u32 len, u32 at)
{
	const char *nl = memchr(text + at, '\n', len - at);
	return nl ? (u32)(nl - text) + 1 : len;
}

u32 cheatfile_set_code(struct cheatfile *f, char *buf, u32 cap, u32 i, const char *desc, const char *code, u8 format)
{
	if (i >= f->count || !code_ok(code) || format > CHEAT_FMT_VBA)
		return CHEATFILE_EDIT_BAD;
	const struct cheatfile_cheat *c = &f->cheats[i];
	u32 n = c->number;
	struct piece pcs[3];
	u32 k = 0;
	pcs[k++] = (struct piece){c->code, c->code_len, PIECE_VALUE, 0, 0, 0, NULL, code, n};
	if (desc && c->desc != CHEATFILE_NONE)
		pcs[k++] = (struct piece){c->desc, c->desc_len, PIECE_DESC, 0, 0, 0, NULL, desc, n};
	else if (desc) {
		// Never before a BOM, if the code line is the file's first
		u32 at = line_start(f->text, c->code), top = text_start(f->text, f->len);
		pcs[k++] = (struct piece){at > top ? at : top, 0, PIECE_LINE, 0, 0, 1, "desc", desc, n};
	}
	// Its format line, the last if it repeats (the one a read takes)
	u32 pos = text_start(f->text, f->len), found = 0;
	struct kv kv, fmt = {0, 0, 0, 0, 0, 0, 0};
	while (next_kv(f->text, f->len, &pos, &kv)) {
		u32 m = 0, d;
		if (kv.klen && cheat_key(f->text + kv.key, kv.klen, f->max, &m, &d) == KEY_FORMAT && m == n) {
			fmt = kv;
			found = 1;
		}
	}
	if (found && format == CHEAT_FMT_AUTO)
		pcs[k++] = (struct piece){fmt.start, fmt.end - fmt.start, PIECE_NONE, 0, 0, 0, NULL, NULL, n};
	else if (found)
		pcs[k++] = (struct piece){fmt.val, fmt.vlen, PIECE_VALUE, 0, 0, 0, NULL, cheatfile_format_names[format],
					  n};
	else if (format != CHEAT_FMT_AUTO) {
		// After its code line (a line end first if that is the last, without one)
		u32 end = line_end(f->text, f->len, c->code);
		u8 brk = (u8)(f->text[end - 1] != '\n');
		pcs[k++] = (struct piece){end, 0, PIECE_LINE, brk, 0, 0, "format", cheatfile_format_names[format], n};
	}
	if (!fits(f, cap, pieces_delta(f, pcs, k)) || f->len + pieces_grow(f, pcs, k) > cap)
		return CHEATFILE_EDIT_LONG;
	struct picks p;
	picks_save(f, &p);
	reread(f, buf, apply(f, buf, pcs, k));
	picks_restore(f, &p, CHEATFILE_NONE);
	f->edited = 1;
	return CHEATFILE_EDIT_OK;
}

// Moves [from, from + len) to w (w <= from); w past it
static u32 move_to(char *buf, u32 w, u32 from, u32 len)
{
	memmove(buf + w, buf + from, len);
	return w + len;
}

u32 cheatfile_remove(struct cheatfile *f, char *buf, u32 cap, u32 i)
{
	if (i >= f->count)
		return CHEATFILE_EDIT_BAD;
	u32 n = f->cheats[i].number;
	struct picks p;
	picks_save(f, &p);
	// One pass, the text moving down: the cheat's lines go, with a blank
	// line just before one; the cheats after it are numbered one lower (a
	// number never gets longer)
	u32 len = f->len, pos = text_start(buf, len), w = pos;
	u32 held = CHEATFILE_NONE, held_len = 0;  // a blank line not moved yet
	struct kv kv;
	while (next_kv(buf, len, &pos, &kv)) {
		u32 m = 0, d = 0, what = kv.klen ? cheat_key(buf + kv.key, kv.klen, f->max, &m, &d) : KEY_OTHER;
		int cheat_line = what <= KEY_CHEAT && m < f->max;
		if (cheat_line && m == n) {
			held = CHEATFILE_NONE;
			continue;
		}
		if (held != CHEATFILE_NONE)
			w = move_to(buf, w, held, held_len);
		held = CHEATFILE_NONE;
		if (kv.blank) {
			held = kv.start;
			held_len = kv.end - kv.start;
			continue;
		}
		if (cheat_line && m > n) {
			char digits[8];
			u32 dl = (u32)snprintf(digits, sizeof(digits), "%lu", (unsigned long)(m - 1));
			u32 after = kv.key + 5 + d;
			w = move_to(buf, w, kv.start, kv.key + 5 - kv.start);
			memcpy(buf + w, digits, dl);  // over the old digits at most
			w = move_to(buf, w + dl, after, kv.end - after);
		} else {
			w = move_to(buf, w, kv.start, kv.end - kv.start);
		}
	}
	if (held != CHEATFILE_NONE)
		w = move_to(buf, w, held, held_len);
	reread(f, buf, w);
	set_count(f, buf, cap, 0);
	picks_restore(f, &p, n);
	f->edited = 1;
	return CHEATFILE_EDIT_OK;
}
