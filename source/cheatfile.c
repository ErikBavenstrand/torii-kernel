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
	KEY_RENAMED,
	KEY_KEYS,
	KEY_GUIDE,
	KEY_CHEAT,  // another cheatN_ key: kept, and numbered with its cheat
	KEY_COUNT,  // "cheats"
	KEY_OTHER,
};

const char *const cheatfile_format_names[CHEAT_FMT_VBA + 1] = {
	NULL, "GSAv1", "GSAv1 raw", "PARv3", "PARv3 raw", "CodeBreaker", "VBA",
};

// A key "cheatN_desc", "_code", "_enable", "_format", "_user", "_renamed",
// "_keys", "_guide" or another
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
	if (wl == 7 && !memcmp(what, "renamed", 7))
		return KEY_RENAMED;
	if (wl == 4 && !memcmp(what, "keys", 4))
		return KEY_KEYS;
	if (wl == 5 && !memcmp(what, "guide", 5))
		return KEY_GUIDE;
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

int cheatfile_line_key(const char *line, u32 len, u32 *number, int *on)
{
	u32 pos = 0, n = 0, digits;
	struct kv kv;
	*on = 0;
	if (!next_kv(line, len, &pos, &kv) || !kv.klen)
		return 0;
	u32 what = cheat_key(line + kv.key, kv.klen, 0x10000, &n, &digits);
	if (what > KEY_CHEAT || n >= 0x10000)
		return 0;
	*number = n;
	*on = what == KEY_ENABLE && is_true(line + kv.val, kv.vlen);
	return 1;
}

// The number past the last the table takes
static u32 top(const struct cheatfile *f)
{
	return (u32)f->base + f->max;
}

// A KEY = VALUE line, into the table by number
static void parse_kv(struct cheatfile *f, const struct kv *kv)
{
	u32 n = 0, digits;
	u32 what = cheat_key(f->text + kv->key, kv->klen, top(f), &n, &digits);
	const char *s = f->text + kv->val;
	u32 at = kv->val;
	u16 len = (u16)kv->vlen;
	if (kv->vlen > CHEATFILE_VALUE_MAX)
		return;  // no span of ours holds it
	if (what == KEY_COUNT) {
		f->count_at = at;
		f->count_len = len;
		return;
	}
	if (what == KEY_OTHER)
		return;
	if (n < f->base || n >= top(f)) {
		f->dropped += what == KEY_CODE;
		return;
	}
	if (n >= f->next)
		f->next = (u16)(n + 1);
	struct cheatfile_cheat *c = &f->cheats[n - f->base];
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
	} else if (what == KEY_RENAMED) {
		c->renamed = is_true(s, len) != 0;
	} else if (what == KEY_KEYS) {
		c->keys = cheatkeys_parse(s, len);
	} else if (what == KEY_GUIDE) {
		c->guide = len <= CHEATFILE_GUIDE_LEN_MAX ? at << CHEATFILE_GUIDE_LEN_BITS | len : CHEATFILE_NONE;
	} else if (what == KEY_ENABLE && len <= 0xFF) {  // a longer value isn't an enable line we could rewrite
		c->enable = at;
		c->enable_len = (u8)len;
		c->picked = c->was = c->saved = is_true(s, len) != 0;
	}
}

int cheatfile_rom_crc(const char *text, u32 len, u32 *crc)
{
	static const char tag[] = "# for crc32 ";
	const u32 tag_len = sizeof(tag) - 1;
	u32 pos = text_start(text, len);
	while (pos < len) {
		const char *nl = memchr(text + pos, '\n', len - pos);
		u32 end = nl ? (u32)(nl - text) : len;
		u32 s = pos;
		while (s < end && is_space(text[s]))
			s++;
		if (s + tag_len + 8 <= end && !memcmp(text + s, tag, tag_len)) {
			u32 v = 0, i;
			for (i = 0; i < 8; i++) {
				char c = text[s + tag_len + i];
				u32 d = c >= '0' && c <= '9'   ? (u32)(c - '0')
					: c >= 'A' && c <= 'F' ? (u32)(c - 'A' + 10)
					: c >= 'a' && c <= 'f' ? (u32)(c - 'a' + 10)
							       : 16;
				if (d == 16)
					break;
				v = v << 4 | d;
			}
			u32 rest = s + tag_len + 8;
			while (rest < end && is_space(text[rest]))
				rest++;
			if (i == 8 && rest == end) {
				*crc = v;
				return 1;
			}
		}
		pos = end + 1;
	}
	return 0;
}

int cheatfile_empty(const struct cheatfile *f)
{
	if (f->count)
		return 0;
	u32 pos = text_start(f->text, f->len), crc;
	struct kv kv;
	while (next_kv(f->text, f->len, &pos, &kv)) {
		if (kv.blank)
			continue;
		if (kv.klen) {
			u32 n, digits;
			if (cheat_key(f->text + kv.key, kv.klen, top(f), &n, &digits) != KEY_COUNT)
				return 0;
		} else if (!cheatfile_rom_crc(f->text + kv.start, kv.end - kv.start, &crc)) {
			return 0;  // a comment of the user's, or any other line
		}
	}
	return 1;
}

void cheatfile_parse(struct cheatfile *f, const char *text, u32 len, struct cheatfile_cheat *cheats, u32 max)
{
	cheatfile_parse_part(f, text, len, cheats, max, 0, 0);
}

void cheatfile_parse_part(struct cheatfile *f, const char *text, u32 len, struct cheatfile_cheat *cheats, u32 max,
			  u32 base, int part)
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
	f->out_len = len;  // every pick as its enable line says: written back as it is
	f->cheats = cheats;
	f->max = (u16)max;
	f->base = (u16)base;
	f->part = (u8)(part != 0);
	f->next = (u16)base;
	f->count_at = CHEATFILE_NONE;
	for (u32 i = 0; i < max; i++)
		cheats[i] = (struct cheatfile_cheat){.desc = CHEATFILE_NONE,
						     .code = CHEATFILE_NONE,
						     .enable = CHEATFILE_NONE,
						     .guide = CHEATFILE_NONE,
						     .status = CHEATFILE_NEW,
						     .number = (u16)(base + i)};
	u32 pos = text_start(text, len);
	struct kv kv;
	int first = 1;
	while (next_kv(text, len, &pos, &kv)) {
		if (first && text[kv.end - 1] == '\n') {
			f->crlf = kv.end - kv.start >= 2 && text[kv.end - 2] == '\r';
			first = 0;
		}
		if (kv.klen)
			parse_kv(f, &kv);
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
	w->last = CHEATFILE_NONE;
	w->last_shape = 0;
	memset(w->seen, 0, sizeof(w->seen));
}

// FNV-1a of the n words at p, on from h
static u32 hash_words(u32 h, const u32 *p, u32 n)
{
	for (u32 i = 0; i < n; i++)
		for (u32 b = 0; b < 32; b += 8)
			h = (h ^ ((p[i] >> b) & 0xFF)) * 16777619u;
	return h;
}

// The hash of w's table part and patches (an OK cheat's): never 0
static u32 program_hash(const struct cheatfile_work *w)
{
	_Static_assert(sizeof(struct cheat_patch) % 4 == 0, "patches hash as words");
	u32 h = hash_words(2166136261u, &w->n_prog, 1);
	h = hash_words(h, w->prog, w->n_prog);
	h = hash_words(h, (const u32 *)(const void *)w->patches, w->n_patches * (u32)sizeof(struct cheat_patch) / 4);
	return h ? h : 1;
}

// What n decoded ops do with the values they write left out: where and how
// they write, and what their conditions test. Two cheats of one shape write
// other values to the same places: variants of one thing.
static u32 shape_hash(const struct cheat_op *ops, u32 n)
{
	u32 h = hash_words(2166136261u, &n, 1);
	for (u32 i = 0; i < n; i++) {
		const struct cheat_op *op = &ops[i];
		int cond = op->type >= CHEAT_OP_IF_EQ && op->type <= CHEAT_OP_IF_NEVER;
		u32 words[8] = {op->address,
				cond ? op->value : 0,
				op->count,
				(u32)op->addr_step,
				(u32)op->else_count << 16 | (u32)op->type << 8 | op->width,
				(u32)op->value_step,
				op->key,
				(u32)op->key_offset << 8 | op->flags};
		h = hash_words(h, words, 8);
	}
	return h ? h : 1;
}

// 1 if hash h (an OK cheat's) is an earlier OK cheat's of this pass; else it
// is recorded. A full set records nothing more.
static int seen_before(struct cheatfile_work *w, u32 h)
{
	_Static_assert((CHEATFILE_SEEN_SLOTS & (CHEATFILE_SEEN_SLOTS - 1)) == 0, "a power of 2");
	for (u32 k = 0, s = h; k < CHEATFILE_SEEN_SLOTS; k++, s++) {
		u32 *slot = &w->seen[s & (CHEATFILE_SEEN_SLOTS - 1)];
		if (*slot == h)
			return 1;
		if (!*slot) {
			*slot = h;
			return 0;
		}
	}
	return 0;
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

// Cheat c's ops (n of them, lowered OK into w, act what they imply) gated
// by its cheatN_keys when those say Hold or Press and the code doesn't
// already: Hold puts its ops under one conditional, its buttons all held
// (KEYINPUT & buttons == 0, as CodeBreaker's key test); Press under two, on
// the interpreter's snapshots of the keys (cheatrun.s), its buttons all held
// now and not all at the run before, so it runs once a press. Lowered
// again; if that fails (it grows too big) it stays as it was. Returns 0 if
// code that runs in game can't be gated, whatever its line says: it patches
// the ROM too (a ROM write can't wait on a condition), there is no room for
// the ops, or lowering failed; else 1.
static int gate(struct cheatfile_work *w, u32 n, u16 keys, u16 act)
{
	if (act == CHEATKEYS_AT_LAUNCH || !w->n_prog)
		return 1;  // nothing runs in game: set at launch, or nothing at all
	if (w->n_patches || n + 2 > CHEATFILE_OPS_MAX)
		return 0;
	u32 mode = cheatkeys_mode(keys), g = mode == CHEATKEYS_PRESS ? 2 : 1;
	if ((mode != CHEATKEYS_HOLD && mode != CHEATKEYS_PRESS) || !cheatkeys_valid(keys) ||
	    (mode == CHEATKEYS_HOLD && keys == act))
		return 1;
	memmove(w->ops + g, w->ops, n * sizeof(w->ops[0]));
	memset(w->ops, 0, g * sizeof(w->ops[0]));
	u32 buttons = cheatkeys_buttons(keys);
	w->ops[0].type = CHEAT_OP_IF_NAND;
	w->ops[0].width = 2;
	w->ops[0].value = buttons;
	w->ops[0].count = n + g - 1;
	w->ops[0].address = mode == CHEATKEYS_PRESS ? CHEAT_KEYS_NOW : CHEAT_KEYINPUT;
	if (mode == CHEATKEYS_PRESS) {
		w->ops[1].type = CHEAT_OP_IF_AND;
		w->ops[1].width = 2;
		w->ops[1].value = buttons;
		w->ops[1].count = n;
		w->ops[1].address = CHEAT_KEYS_BEFORE;
	}
	u8 lowered = cheat_lower(w->ops, n + g, w->prog, CHEATFILE_PROG_MAX, &w->n_prog, &w->cost, w->patches,
				 CHEATFILE_PATCHES_MAX, &w->n_patches);
	memmove(w->ops, w->ops + g, n * sizeof(w->ops[0]));
	if (lowered == CHEAT_LOWER_OK) {
		w->prog[0] |= g << CHEAT_PROG_GATE_SHIFT;  // a run once steps past them
		return 1;
	}
	cheat_lower(w->ops, n, w->prog, CHEATFILE_PROG_MAX, &w->n_prog, &w->cost, w->patches, CHEATFILE_PATCHES_MAX,
		    &w->n_patches);
	return 0;
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
		f->patches -= c->patches;
	}
	u32 n_ops;
	u8 lowered;
	if (c->format != CHEAT_FMT_AUTO) {
		// Read alone in its own format: the file's key passes it by, its
		// GameShark seeds don't
		struct cheat_decoder own;
		u32 seeds[4];
		cheat_decoder_force_in(&own, &w->dec, c->format);
		memcpy(seeds, own.gs_seeds, sizeof(seeds));
		c->status = cheatfile_decode(w, &own, f->text + c->code, c->code_len, &n_ops, &lowered);
		cheat_decoder_force_out(&w->dec, &own, seeds);
	} else {
		c->status = cheatfile_decode(w, &w->dec, f->text + c->code, c->code_len, &n_ops, &lowered);
	}
	c->detected = c->format != CHEAT_FMT_AUTO ? c->format : w->dec.last;
	u16 act = c->status == CHEATFILE_OK ? cheatkeys_derive(w->ops, n_ops, w->n_patches != 0, w->n_prog != 0) : 0;
	if (c->status == CHEATFILE_OK && !gate(w, n_ops, c->keys, act))
		act |= CHEATFILE_ACT_UNGATED;
	c->hash = c->status == CHEATFILE_OK ? program_hash(w) : 0;
	if (c->status == CHEATFILE_OK && !cheat_has_effect(w->ops, n_ops))
		c->status = CHEATFILE_NOTHING;
	else if (c->status == CHEATFILE_OK && seen_before(w, c->hash))
		c->status = CHEATFILE_SAME;
	c->act = c->status == CHEATFILE_OK || c->status == CHEATFILE_SAME ? act : 0;
	u32 shape = c->status == CHEATFILE_OK ? shape_hash(w->ops, n_ops) : 0;
	c->variant = shape && w->last + 1 == i && shape == w->last_shape;
	w->last = i;
	w->last_shape = shape;
	if (c->status != CHEATFILE_OK)
		w->n_prog = w->n_patches = w->cost = 0;
	c->bytes = (u16)(w->n_prog * 4);
	c->cost = (u16)w->cost;
	c->patches = (u16)w->n_patches;
	if (c->status == CHEATFILE_OK && c->picked) {
		f->bytes += c->bytes;
		f->cost += c->cost;
		f->patches += c->patches;
	}
	return (s32)i;
}

// --- Variants ------------------------------------------------------------------

u32 cheatfile_group(const struct cheatfile *f, u32 i, u32 *end)
{
	u32 start = i, k = i + 1;
	while (start > 0 && f->cheats[start].variant)
		start--;
	while (k < f->count && f->cheats[k].variant)
		k++;
	*end = k;
	return start;
}

// 1 if c ends a word in a cheat's name: a space or a mark that joins a name
// to its variant's part
static int name_break(char c)
{
	return c == ' ' || c == '-' || c == ':' || c == '=' || c == '|' || c == ',' || c == '/' || c == '(' || c == '[';
}

u32 cheatfile_group_name(const struct cheatfile *f, u32 start, u32 end, u32 *own)
{
	*own = 0;
	const struct cheatfile_cheat *first = &f->cheats[start];
	if (first->desc == CHEATFILE_NONE)
		return 0;
	const char *a = f->text + first->desc;
	u32 n = first->desc_len;
	for (u32 i = start + 1; i < end && n; i++) {
		const struct cheatfile_cheat *c = &f->cheats[i];
		if (c->desc == CHEATFILE_NONE)
			return 0;
		const char *b = f->text + c->desc;
		u32 k = 0;
		while (k < n && k < c->desc_len && a[k] == b[k])
			k++;
		n = k;
	}
	// Back to a word's end: every name ends there, or goes on past a break
	int whole = 1;
	for (u32 i = start; i < end && whole; i++) {
		const struct cheatfile_cheat *c = &f->cheats[i];
		whole = c->desc_len == n || name_break(f->text[c->desc + n]);
	}
	if (!whole)
		while (n && !name_break(a[n - 1]))
			n--;
	u32 part = n;
	while (n && (name_break(a[n - 1]) || a[n - 1] == ')' || a[n - 1] == ']'))
		n--;
	if (n < 3)
		return 0;
	*own = part;
	return n;
}

u32 cheatfile_own_part(const char *name, u32 len, u32 own)
{
	u32 k = own < len ? own : len;
	while (k < len && name_break(name[k]) && name[k] != '(' && name[k] != '[')
		k++;
	return k < len ? k : 0;  // nothing of its own: the whole name
}

// --- Activation (cheatkeys.h) ------------------------------------------------

// The buttons' names in the file, by enum cheatkeys_button
static const char *const key_words[CHEATKEYS_BUTTON_COUNT] = {
	"L", "R", "Select", "Start", "A", "B", "Up", "Down", "Left", "Right",
};
// The modes' words, by enum cheatkeys_mode (NONE has none)
static const char *const mode_words[CHEATKEYS_MODES] = {NULL, "always", "hold", "press", "launch"};

// 1 if the len bytes at s are word, in any case
static int word_is(const char *s, u32 len, const char *word)
{
	if (strlen(word) != len)
		return 0;
	for (u32 i = 0; i < len; i++) {
		char a = s[i], b = word[i];
		if (a >= 'A' && a <= 'Z')
			a = (char)(a + 32);
		if (b >= 'A' && b <= 'Z')
			b = (char)(b + 32);
		if (a != b)
			return 0;
	}
	return 1;
}

u16 cheatkeys_parse(const char *s, u32 len)
{
	u32 i = 0, mode = CHEATKEYS_NONE, held = 0;
	while (i < len && is_space(s[i]))
		i++;
	u32 w = i;
	while (i < len && !is_space(s[i]) && s[i] != '+')
		i++;
	for (u32 m = CHEATKEYS_ALWAYS; m < CHEATKEYS_MODES; m++)
		if (word_is(s + w, i - w, mode_words[m]))
			mode = m;
	if (mode == CHEATKEYS_NONE)
		return 0;
	int buttons = mode == CHEATKEYS_HOLD || mode == CHEATKEYS_PRESS;
	if (buttons && (i == len || !is_space(s[i])))
		return 0;  // "hold" alone, or "hold+L"
	// The buttons: each a word, '+' between, spaces around either
	for (int want = buttons; want;) {
		while (i < len && is_space(s[i]))
			i++;
		w = i;
		while (i < len && !is_space(s[i]) && s[i] != '+')
			i++;
		u32 b = 0;
		while (b < CHEATKEYS_BUTTON_COUNT && !word_is(s + w, i - w, key_words[b]))
			b++;
		if (b == CHEATKEYS_BUTTON_COUNT)
			return 0;
		held |= cheatkeys_bit(b);
		while (i < len && is_space(s[i]))
			i++;
		want = i < len && s[i] == '+';
		i += (u32)want;
	}
	while (i < len && is_space(s[i]))
		i++;
	return i == len ? CHEATKEYS(mode, held) : 0;
}

u32 cheatkeys_format(u16 v, char *out, u32 size)
{
	if (!size)
		return 0;
	out[0] = 0;
	if (!v || !cheatkeys_valid(v))
		return 0;
	u32 n = (u32)snprintf(out, size, "%s", mode_words[cheatkeys_mode(v)]);
	const char *sep = " ";
	for (u32 b = 0; b < CHEATKEYS_BUTTON_COUNT && n < size; b++)
		if (v & cheatkeys_bit(b)) {
			n += (u32)snprintf(out + n, size - n, "%s%s", sep, key_words[b]);
			sep = "+";
		}
	return n < size ? n : size - 1;
}

// The buttons a condition on KEYINPUT needs held: 1 with them in *held, 0
// for a condition that isn't on KEYINPUT, -1 for one on KEYINPUT that
// cheatkeys_derive can't read
static int key_condition(const struct cheat_op *op, u32 *held)
{
	if (op->type == CHEAT_OP_IF_BUTTON) {
		*held = CHEAT_BUTTON_KEYS;
		return op->else_count ? -1 : 1;
	}
	if (op->type < CHEAT_OP_IF_EQ || op->type > CHEAT_OP_IF_NEVER || !op->width)
		return 0;
	u32 lo = op->address, hi = op->address + op->width;
	if (hi <= CHEAT_KEYINPUT || lo >= CHEAT_KEYINPUT + 2)
		return 0;
	if (op->else_count)
		return -1;
	if (op->type == CHEAT_OP_IF_EQ && op->address == CHEAT_KEYINPUT && op->width == 2)
		*held = ~op->value & CHEATKEYS_BUTTONS;
	else if (op->type == CHEAT_OP_IF_NAND && op->address == CHEAT_KEYINPUT)
		*held = op->value & CHEATKEYS_BUTTONS;
	else if (op->type == CHEAT_OP_IF_NAND && op->address == CHEAT_KEYINPUT + 1 && op->width == 1)
		*held = op->value << 8 & CHEATKEYS_BUTTONS;
	else
		return -1;
	return *held ? 1 : -1;
}

u16 cheatkeys_derive(const struct cheat_op *ops, u32 n, int has_patches, int runs_in_game)
{
	u32 combo = 0;
	for (u32 i = 0; i < n; i++) {
		u32 held = 0;
		int k = key_condition(&ops[i], &held);
		if (k < 0 || (k > 0 && combo && held != combo))
			return 0;
		if (k > 0)
			combo = held;
	}
	if (combo)
		return CHEATKEYS(CHEATKEYS_HOLD, combo);
	if (runs_in_game)
		return CHEATKEYS_ALWAYS_ON;
	return has_patches ? CHEATKEYS_AT_LAUNCH : 0;
}

// --- The written text's length, as picks change ------------------------------

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

// 1 if the text doesn't end its last line: an added line starts a new one
static int open_end(const struct cheatfile *f)
{
	return f->len && f->text[f->len - 1] != '\n';
}

static u32 eol_len(const struct cheatfile *f)
{
	return f->crlf ? 2 : 1;
}

// The bytes c's pick changes the written text by: its enable value's change,
// or its added line (added_line's, without a break before it)
static s32 pick_delta(const struct cheatfile *f, const struct cheatfile_cheat *c)
{
	if (edited(c))
		return (s32)strlen(pick_text[c->picked]) - (s32)c->enable_len;
	if (!added(c))
		return 0;
	u32 digits = 1;
	for (u32 n = c->number; n >= 10; n /= 10)
		digits++;
	return (s32)(sizeof("cheat") - 1 + digits + sizeof("_enable = true") - 1 + eol_len(f));
}

// The line break before the added lines, with n of them
static u32 added_break(const struct cheatfile *f, u32 n)
{
	return n && open_end(f) ? eol_len(f) : 0;
}

// The written text's length with c's pick flipped (c a cheat of f's)
static u32 len_flipped(const struct cheatfile *f, const struct cheatfile_cheat *c)
{
	struct cheatfile_cheat t = *c;
	t.picked = !t.picked;
	u32 n = f->n_added - (u32)added(c) + (u32)added(&t);
	return (u32)((s32)f->out_len - pick_delta(f, c) + pick_delta(f, &t) - (s32)added_break(f, f->n_added) +
		     (s32)added_break(f, n));
}

// f->out_len and f->n_added from every cheat's pick
static void out_sync(struct cheatfile *f)
{
	s32 len = (s32)f->len;
	f->n_added = 0;
	for (u32 i = 0; i < f->count; i++) {
		len += pick_delta(f, &f->cheats[i]);
		f->n_added = (u16)(f->n_added + added(&f->cheats[i]));
	}
	f->out_len = (u32)len + added_break(f, f->n_added);
}

void cheatfile_picks_from(struct cheatfile *f, int (*on)(void *ctx, u32 number), void *ctx)
{
	for (u32 i = 0; i < f->count; i++) {
		struct cheatfile_cheat *c = &f->cheats[i];
		c->picked = c->saved = on(ctx, c->number) != 0;
	}
	out_sync(f);
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
		if (f->bytes + f->more_bytes + c->bytes > CHEATFILE_BYTES_MAX ||
		    f->cost + f->more_cost + c->cost > CHEATFILE_CYCLES_MAX)
			return CHEATFILE_WHY_FULL;
		if (f->patches + f->more_patches + c->patches > CHEATFILE_PICKED_PATCHES_MAX)
			return CHEATFILE_WHY_PATCHES;
		return len_flipped(f, c) > CHEATFILE_TEXT_MAX ? CHEATFILE_WHY_ROOM : CHEATFILE_PICKABLE;
	case CHEATFILE_BIG:
		return CHEATFILE_WHY_FULL;
	case CHEATFILE_PLACEHOLDER:
		return CHEATFILE_WHY_EDIT;
	case CHEATFILE_CANT_RUN:
		return CHEATFILE_WHY_CANT_RUN;
	case CHEATFILE_NOTHING:
		return CHEATFILE_WHY_NOTHING;
	case CHEATFILE_SAME:
		return CHEATFILE_WHY_SAME;
	case CHEATFILE_MASTER:
		return CHEATFILE_WHY_MASTER;
	default:
		return CHEATFILE_WHY_UNREADABLE;
	}
}

int cheatfile_toggle(struct cheatfile *f, u32 i)
{
	if (cheatfile_why(f, i) != CHEATFILE_PICKABLE)
		return 0;
	struct cheatfile_cheat *c = &f->cheats[i];
	f->out_len = len_flipped(f, c);
	f->n_added = (u16)(f->n_added - added(c));
	c->picked = !c->picked;
	f->n_added = (u16)(f->n_added + added(c));
	if (c->status == CHEATFILE_OK) {
		if (c->picked) {
			f->bytes += c->bytes;
			f->cost += c->cost;
			f->patches += c->patches;
		} else {
			f->bytes -= c->bytes;
			f->cost -= c->cost;
			f->patches -= c->patches;
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

// The text offset of the first enable value to change at or after t; past
// the text if none
static u32 next_edit(const struct cheatfile *f, u32 t, u32 *cheat)
{
	u32 best = f->len + 1;
	for (u32 i = 0; i < f->count; i++) {
		const struct cheatfile_cheat *c = &f->cheats[i];
		if (edited(c) && c->enable >= t && c->enable < best) {
			best = c->enable;
			*cheat = i;
		}
	}
	return best;
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
	return f->out_len;
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
		if (f->cheats[i].picked) {
			u32 n = f->cheats[i].number - f->base;
			p->bits[n >> 3] |= (u8)(1u << (n & 7));
		}
}

// After a read again: each cheat's pick as it was, cheat number removed
// gone (CHEATFILE_NONE: none) and the numbers after it one lower
static void picks_restore(struct cheatfile *f, const struct picks *p, u32 removed)
{
	for (u32 i = 0; i < f->count; i++) {
		struct cheatfile_cheat *c = &f->cheats[i];
		u32 n = (removed != CHEATFILE_NONE && c->number >= removed ? c->number + 1u : c->number) - f->base;
		c->picked = n < CHEATFILE_MAX && (p->bits[n >> 3] >> (n & 7)) & 1;
	}
	out_sync(f);
}

// The text in buf, len bytes, read again after an edit
static void reread(struct cheatfile *f, const char *buf, u32 len)
{
	cheatfile_parse_part(f, buf, len, f->cheats, f->max, f->base, f->part);
	f->edited = 1;
}

// Text written into place from p through the room, or only counted (room
// NULL): staged, and put in a few calls (out_end puts the rest)
#define OUT_STAGE 64
struct out {
	const struct cheatfile_room *room;
	char *p;  // where the staged bytes go
	u32 n;    // bytes given in all
	u32 k;    // bytes staged
	char stage[OUT_STAGE];
};

static void out_begin(struct out *o, const struct cheatfile_room *room, char *p)
{
	o->room = room;
	o->p = p;
	o->n = o->k = 0;
}

static void out_end(struct out *o)
{
	if (o->room && o->k)
		o->room->put(o->p, o->stage, o->k);
	o->p += o->k;
	o->k = 0;
}

static void put(struct out *o, const char *s, u32 len)
{
	o->n += len;
	if (!o->room)
		return;
	while (len) {
		u32 take = OUT_STAGE - o->k < len ? OUT_STAGE - o->k : len;
		memcpy(o->stage + o->k, s, take);
		o->k += take;
		s += take;
		len -= take;
		if (o->k == OUT_STAGE)
			out_end(o);
	}
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
		struct out o;
		out_begin(&o, NULL, NULL);
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
		struct out o;
		out_begin(&o, NULL, NULL);
		put_piece(&o, f, &pcs[i]);
		if (o.n > pcs[i].del)
			grow += o.n - pcs[i].del;
	}
	return grow;
}

// 1 if the text with delta more bytes, the picks written in, fits the room
// (cap bytes) and the cheat file's cap
static int fits(const struct cheatfile *f, u32 cap, s32 delta)
{
	s32 picks = (s32)cheatfile_text_len(f) - (s32)f->len;
	s32 len = (s32)f->len + delta, out = len + picks;
	return len >= 0 && (u32)len <= cap && out <= CHEATFILE_TEXT_MAX;
}

// Applies the pieces (none overlapping, in any order) to the room, highest
// first so the lower ones' offsets hold; the text's new length
static u32 apply(const struct cheatfile *f, const struct cheatfile_room *room, struct piece *pcs, u32 n)
{
	char *buf = room->base;
	u32 len = f->len;
	for (u32 i = 1; i < n; i++)
		for (u32 j = i; j > 0 && pcs[j - 1].at < pcs[j].at; j--) {
			struct piece t = pcs[j];
			pcs[j] = pcs[j - 1];
			pcs[j - 1] = t;
		}
	for (u32 i = 0; i < n; i++) {
		const struct piece *pc = &pcs[i];
		struct out o;
		out_begin(&o, NULL, NULL);
		put_piece(&o, f, pc);
		if (o.n != pc->del && len > pc->at + pc->del)
			room->move(buf + pc->at + o.n, buf + pc->at + pc->del, len - pc->at - pc->del);
		out_begin(&o, room, buf + pc->at);
		put_piece(&o, f, pc);
		out_end(&o);
		len = len - pc->del + o.n;
	}
	return len;
}

// The count line's piece, to follow the cheats' numbers: 0 if it needs none
// (or there is none, and add is 0)
static u32 count_piece(const struct cheatfile *f, int add, struct piece *pc)
{
	*pc = (struct piece){0, 0, PIECE_COUNT, 0, 0, 0, NULL, NULL, f->next};
	if (f->part)
		return 0;  // the whole file's count: a window can't tell it
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
static void set_count(struct cheatfile *f, const struct cheatfile_room *room, int add)
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
	if (!fits(f, room->cap, pieces_delta(f, &pc, 1)))
		return;  // an add keeps room for it (COUNT_ROOM); a read takes the file without it all the same
	reread(f, room->base, apply(f, room, &pc, 1));
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

// A new cheat's lines into pcs (5 at most): after a blank line (and the last
// line's end, if it has none), in order at the end. Their count.
static u32 add_pieces(const struct cheatfile *f, const char *desc, const char *code, u8 format, struct piece *pcs)
{
	u32 k = 0, at = f->len, n = f->next;
	int any = f->len > text_start(f->text, f->len);
	int open = any && f->text[f->len - 1] != '\n';
	pcs[k++] = (struct piece){at, 0, PIECE_LINE, (u8)(any + open), 0, 1, "desc", desc, n};
	pcs[k++] = (struct piece){at, 0, PIECE_LINE, 0, 0, 0, "code", code, n};
	pcs[k++] = (struct piece){at, 0, PIECE_LINE, 0, 1, 0, "enable", "false", n};
	if (format != CHEAT_FMT_AUTO)
		pcs[k++] = (struct piece){at, 0, PIECE_LINE, 0, 0, 0, "format", cheatfile_format_names[format], n};
	pcs[k++] = (struct piece){at, 0, PIECE_LINE, 0, 1, 0, "user", "true", n};
	return k;
}

u32 cheatfile_can_add(const struct cheatfile *f, u32 cap, const char *desc)
{
	if (f->next >= top(f))
		return CHEATFILE_EDIT_FULL;
	struct piece pcs[5];
	u32 k = add_pieces(f, desc, CHEATFILE_SHORT_CODE, CHEAT_FMT_AUTO, pcs);
	return fits(f, cap, pieces_delta(f, pcs, k) + COUNT_ROOM) ? CHEATFILE_EDIT_OK : CHEATFILE_EDIT_LONG;
}

u32 cheatfile_add(struct cheatfile *f, const struct cheatfile_room *room, const char *desc, const char *code, u8 format,
		  u32 *index)
{
	if (!code_ok(code) || format > CHEAT_FMT_VBA)
		return CHEATFILE_EDIT_BAD;
	if (f->next >= top(f))
		return CHEATFILE_EDIT_FULL;
	struct piece pcs[5];
	u32 k = add_pieces(f, desc, code, format, pcs);
	if (!fits(f, room->cap, pieces_delta(f, pcs, k) + COUNT_ROOM))
		return CHEATFILE_EDIT_LONG;
	struct picks p;
	picks_save(f, &p);
	struct out o;
	out_begin(&o, room, room->base + f->len);
	for (u32 i = 0; i < k; i++)
		put_piece(&o, f, &pcs[i]);
	out_end(&o);
	reread(f, room->base, f->len + o.n);
	set_count(f, room, 1);
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

u32 cheatfile_set_code(struct cheatfile *f, const struct cheatfile_room *room, u32 i, const char *desc,
		       const char *code, u8 format)
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
		u32 at = line_start(f->text, c->code), first = text_start(f->text, f->len);
		pcs[k++] = (struct piece){at > first ? at : first, 0, PIECE_LINE, 0, 0, 1, "desc", desc, n};
	}
	// Its format line, the last if it repeats (the one a read takes)
	u32 pos = text_start(f->text, f->len), found = 0;
	struct kv kv, fmt = {0, 0, 0, 0, 0, 0, 0};
	while (next_kv(f->text, f->len, &pos, &kv)) {
		u32 m = 0, d;
		if (kv.klen && cheat_key(f->text + kv.key, kv.klen, top(f), &m, &d) == KEY_FORMAT && m == n) {
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
	if (!fits(f, room->cap, pieces_delta(f, pcs, k)) || f->len + pieces_grow(f, pcs, k) > room->cap)
		return CHEATFILE_EDIT_LONG;
	struct picks p;
	picks_save(f, &p);
	reread(f, room->base, apply(f, room, pcs, k));
	picks_restore(f, &p, CHEATFILE_NONE);
	f->edited = 1;
	return CHEATFILE_EDIT_OK;
}

// Moves [from, from + len) to w (w <= from); w past it
static u32 move_to(const struct cheatfile_room *room, u32 w, u32 from, u32 len)
{
	if (w != from && len)
		room->move(room->base + w, room->base + from, len);
	return w + len;
}

u32 cheatfile_rename(struct cheatfile *f, const struct cheatfile_room *room, u32 i, const char *desc)
{
	if (i >= f->count || !*desc)
		return CHEATFILE_EDIT_BAD;
	const struct cheatfile_cheat *c = &f->cheats[i];
	u32 n = c->number, k = 0;
	struct piece pcs[2];
	if (c->desc != CHEATFILE_NONE) {
		pcs[k++] = (struct piece){c->desc, c->desc_len, PIECE_DESC, 0, 0, 0, NULL, desc, n};
	} else {
		u32 at = line_start(f->text, c->code), first = text_start(f->text, f->len);
		pcs[k++] = (struct piece){at > first ? at : first, 0, PIECE_LINE, 0, 0, 1, "desc", desc, n};
	}
	if (!c->own && !c->renamed) {
		// A renamed line saying false (the last, the one a read takes) says
		// true; else one goes after its code line (a line end first if that
		// is the last, without one)
		u32 pos = text_start(f->text, f->len), found = 0;
		struct kv kv, was = {0, 0, 0, 0, 0, 0, 0};
		while (next_kv(f->text, f->len, &pos, &kv)) {
			u32 m = 0, d;
			if (kv.klen && cheat_key(f->text + kv.key, kv.klen, top(f), &m, &d) == KEY_RENAMED && m == n) {
				was = kv;
				found = 1;
			}
		}
		if (found) {
			pcs[k++] = (struct piece){was.val, was.vlen, PIECE_VALUE, 0, 0, 0, NULL, "true", n};
		} else {
			u32 end = line_end(f->text, f->len, c->code);
			u8 brk = (u8)(f->text[end - 1] != '\n');
			pcs[k++] = (struct piece){end, 0, PIECE_LINE, brk, 1, 0, "renamed", "true", n};
		}
	}
	if (!fits(f, room->cap, pieces_delta(f, pcs, k)) || f->len + pieces_grow(f, pcs, k) > room->cap)
		return CHEATFILE_EDIT_LONG;
	struct picks p;
	picks_save(f, &p);
	reread(f, room->base, apply(f, room, pcs, k));
	picks_restore(f, &p, CHEATFILE_NONE);
	f->edited = 1;
	return CHEATFILE_EDIT_OK;
}

// The most lines of one key of a cheat an edit replaces or removes: past
// them, a repeat stays (a file written by hand with more is refused)
#define SAME_KEY_MAX 8

// Cheat number n's lines of key what (KEY_KEYS or KEY_GUIDE) into kvs, in
// order; how many (SAME_KEY_MAX + 1 if more)
static u32 key_lines(const struct cheatfile *f, u32 n, u32 what, struct kv *kvs)
{
	u32 pos = text_start(f->text, f->len), found = 0;
	struct kv kv;
	while (next_kv(f->text, f->len, &pos, &kv)) {
		u32 m = 0, d;
		if (kv.klen && cheat_key(f->text + kv.key, kv.klen, top(f), &m, &d) == what && m == n) {
			if (found == SAME_KEY_MAX)
				return SAME_KEY_MAX + 1;
			kvs[found++] = kv;
		}
	}
	return found;
}

// Sets cheat i's lines of key what (named key) to value (NULL removes
// them): the last one's value replaced and the others removed, or a line
// added after its code line
static u32 set_key_line(struct cheatfile *f, const struct cheatfile_room *room, u32 i, u32 what, const char *key,
			const char *value)
{
	if (i >= f->count)
		return CHEATFILE_EDIT_BAD;
	const struct cheatfile_cheat *c = &f->cheats[i];
	u32 n = c->number, k = 0;
	struct kv kvs[SAME_KEY_MAX];
	struct piece pcs[SAME_KEY_MAX + 1];
	u32 found = key_lines(f, n, what, kvs);
	if (found > SAME_KEY_MAX)
		return CHEATFILE_EDIT_BAD;
	if (!value && !found)
		return CHEATFILE_EDIT_OK;  // nothing to remove
	for (u32 j = 0; j < found; j++) {
		if (value && j == found - 1)
			pcs[k++] = (struct piece){kvs[j].val, kvs[j].vlen, PIECE_VALUE, 0, 0, 0, NULL, value, n};
		else
			pcs[k++] = (struct piece){
				kvs[j].start, kvs[j].end - kvs[j].start, PIECE_NONE, 0, 0, 0, NULL, NULL, n};
	}
	if (value && !found) {
		// After its code line (a line end first if that is the last, without one)
		u32 end = line_end(f->text, f->len, c->code);
		u8 brk = (u8)(f->text[end - 1] != '\n');
		pcs[k++] = (struct piece){end, 0, PIECE_LINE, brk, 0, 0, key, value, n};
	}
	if (!fits(f, room->cap, pieces_delta(f, pcs, k)) || f->len + pieces_grow(f, pcs, k) > room->cap)
		return CHEATFILE_EDIT_LONG;
	struct picks p;
	picks_save(f, &p);
	reread(f, room->base, apply(f, room, pcs, k));
	picks_restore(f, &p, CHEATFILE_NONE);
	f->edited = 1;
	return CHEATFILE_EDIT_OK;
}

u32 cheatfile_set_keys(struct cheatfile *f, const struct cheatfile_room *room, u32 i, u16 keys)
{
	char text[CHEATKEYS_TEXT_MAX];
	if (!cheatkeys_valid(keys))
		return CHEATFILE_EDIT_BAD;
	cheatkeys_format(keys, text, sizeof(text));
	return set_key_line(f, room, i, KEY_KEYS, "keys", keys ? text : NULL);
}

// The bytes of the UTF-8 character starting with byte c (1 for a stray
// continuation byte)
static u32 utf8_len(u8 c)
{
	return c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
}

u32 cheatfile_set_guide(struct cheatfile *f, const struct cheatfile_room *room, u32 i, const char *guide)
{
	char value[CHEATFILE_GUIDE_MAX + 1];
	u32 n = 0;
	for (const char *s = guide; *s;) {
		if (*s == '\n' || (u8)*s < 0x20 || *s == 0x7F) {
			// A line break as "\n"; any other control character dropped
			if (*s++ == '\n') {
				if (n + 2 > CHEATFILE_GUIDE_MAX)
					break;
				value[n++] = '\\';
				value[n++] = 'n';
			}
			continue;
		}
		u32 cl = utf8_len((u8)*s), j = 1;
		while (j < cl && s[j])
			j++;
		if (j < cl || n + cl > CHEATFILE_GUIDE_MAX)
			break;  // a character cut short at the string's end, or past the cap
		for (j = 0; j < cl; j++, s++)
			value[n++] = *s == '"' ? '\'' : *s;
	}
	value[n] = 0;
	return set_key_line(f, room, i, KEY_GUIDE, "guide", n ? value : NULL);
}

u32 cheatfile_guide(const struct cheatfile *f, u32 i, char *out, u32 size)
{
	if (!size)
		return 0;
	out[0] = 0;
	if (i >= f->count || f->cheats[i].guide == CHEATFILE_NONE)
		return 0;
	const char *g = f->text + cheatfile_guide_at(&f->cheats[i]);
	u32 len = cheatfile_guide_len(&f->cheats[i]), n = 0;
	for (u32 r = 0; r < len;) {
		if (g[r] == '\\' && r + 1 < len && g[r + 1] == 'n') {
			if (n + 1 >= size)
				break;
			out[n++] = '\n';
			r += 2;
			continue;
		}
		u32 cl = utf8_len((u8)g[r]);
		if (cl > len - r)
			cl = len - r;
		if (n + cl >= size)
			break;
		for (u32 j = 0; j < cl; j++)
			out[n++] = g[r++];
	}
	out[n] = 0;
	return n;
}

u32 cheatfile_remove(struct cheatfile *f, const struct cheatfile_room *room, u32 i)
{
	if (i >= f->count)
		return CHEATFILE_EDIT_BAD;
	const char *buf = room->base;
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
		u32 m = 0, d = 0, what = kv.klen ? cheat_key(buf + kv.key, kv.klen, top(f), &m, &d) : KEY_OTHER;
		int cheat_line = what <= KEY_CHEAT && m >= f->base && m < top(f);
		if (cheat_line && m == n) {
			held = CHEATFILE_NONE;
			continue;
		}
		if (held != CHEATFILE_NONE)
			w = move_to(room, w, held, held_len);
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
			w = move_to(room, w, kv.start, kv.key + 5 - kv.start);
			room->put(room->base + w, digits, dl);  // over the old digits at most
			w = move_to(room, w + dl, after, kv.end - after);
		} else {
			w = move_to(room, w, kv.start, kv.end - kv.start);
		}
	}
	if (held != CHEATFILE_NONE)
		w = move_to(room, w, held, held_len);
	reread(f, buf, w);
	set_count(f, room, 0);
	picks_restore(f, &p, n);
	f->edited = 1;
	return CHEATFILE_EDIT_OK;
}

// How many entries before entry i drop says go
static u32 dropped_below(const u8 *drop, u32 i)
{
	u32 n = 0;
	for (u32 b = 0; b < i; b++)
		n += (drop[b >> 3] >> (b & 7)) & 1;
	return n;
}

u32 cheatfile_remove_many(struct cheatfile *f, const struct cheatfile_room *room, const u8 *drop)
{
	const char *buf = room->base;
	// The picks of the cheats that stay, in their order (renumbering keeps it)
	u8 kept[CHEATFILE_MAX / 8];
	u32 k = 0, gone = 0;
	memset(kept, 0, sizeof(kept));
	for (u32 i = 0; i < f->count; i++) {
		u32 e = f->cheats[i].number - f->base;
		if ((drop[e >> 3] >> (e & 7)) & 1) {
			gone++;
			continue;
		}
		kept[k >> 3] |= (u8)(f->cheats[i].picked << (k & 7));
		k++;
	}
	if (!gone)
		return 0;
	// One pass, as cheatfile_remove's: the dropped cheats' lines go, each
	// with a blank line just before one; the rest are numbered lower by the
	// dropped ones before them
	u32 len = f->len, pos = text_start(buf, len), w = pos;
	u32 held = CHEATFILE_NONE, held_len = 0;
	struct kv kv;
	while (next_kv(buf, len, &pos, &kv)) {
		u32 m = 0, d = 0, what = kv.klen ? cheat_key(buf + kv.key, kv.klen, top(f), &m, &d) : KEY_OTHER;
		int cheat_line = what <= KEY_CHEAT && m >= f->base && m < top(f);
		u32 e = cheat_line ? m - f->base : 0;
		if (cheat_line && ((drop[e >> 3] >> (e & 7)) & 1)) {
			held = CHEATFILE_NONE;
			continue;
		}
		if (held != CHEATFILE_NONE)
			w = move_to(room, w, held, held_len);
		held = CHEATFILE_NONE;
		if (kv.blank) {
			held = kv.start;
			held_len = kv.end - kv.start;
			continue;
		}
		u32 below = cheat_line ? dropped_below(drop, e) : 0;
		if (below) {
			char digits[8];
			u32 dl = (u32)snprintf(digits, sizeof(digits), "%lu", (unsigned long)(m - below));
			u32 after = kv.key + 5 + d;
			w = move_to(room, w, kv.start, kv.key + 5 - kv.start);
			room->put(room->base + w, digits, dl);  // over the old digits at most
			w = move_to(room, w + dl, after, kv.end - after);
		} else {
			w = move_to(room, w, kv.start, kv.end - kv.start);
		}
	}
	if (held != CHEATFILE_NONE)
		w = move_to(room, w, held, held_len);
	reread(f, buf, w);
	set_count(f, room, 0);
	for (u32 i = 0; i < f->count; i++)
		f->cheats[i].picked = i < k && ((kept[i >> 3] >> (i & 7)) & 1);
	out_sync(f);
	f->edited = 1;
	return gone;
}
