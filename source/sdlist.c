// See sdlist.h. Hardware-free: tests/host builds it.
#include "sdlist.h"
#include "lang.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

void sdlist_begin(struct sdlist *l, u8 *base, u32 cap, lib_put_fn put, struct sdlist_scratch *scratch)
{
	l->base = base;
	l->cap = cap;
	l->put = put;
	l->scratch = scratch;
	l->top = SDLIST_ORDER_BYTES;
	l->count = l->folders = 0;
	l->truncated = l->full = 0;
}

int sdlist_add(struct sdlist *l, const char *name, u32 size, int is_dir)
{
	u32 len = (u32)strnlen(name, SDLIST_NAME_MAX + 1);
	if (!len || len > SDLIST_NAME_MAX)
		return 0;
	u32 files = l->count - l->folders;
	if (is_dir ? l->folders >= SDLIST_MAX : files >= SDLIST_MAX) {
		l->truncated = 1;
		l->full = l->folders >= SDLIST_MAX && files >= SDLIST_MAX;
		return 0;
	}
	struct sdlist_rec *r = (struct sdlist_rec *)l->scratch->rec;
	char *text = (char *)(r + 1);
	memcpy(text, name, len + 1);
	u32 key = (len + 2) & ~1u;  // the name's NUL, rounded up to a halfword
	// A file sorts by its name without the extension, so "Golden Sun.gba"
	// comes before "Golden Sun - The Lost Age.gba"; a folder by all of it
	const char *dot = is_dir ? NULL : strrchr(name, '.');
	u32 stem = dot && dot != name ? (u32)(dot - name) : len;
	char *k = text + key;
	text[stem] = 0;  // for the key's maker, which reads up to a NUL
	u32 key_len = library_sort_key(k, LIBRARY_KEY_MAX + 1, text);
	text[stem] = name[stem];
	u32 used = (u32)sizeof(*r) + key + ((key_len + 2) & ~1u),
	    bytes = (used + 3) & ~3u;  // the next starts on a word
	if (l->top + bytes > l->cap) {
		l->truncated = l->full = 1;
		return 0;
	}
	u32 console = is_dir ? LIB_CONSOLE_NONE : library_console_of(name);
	r->size = is_dir ? 0 : size;
	r->prefix = library_key_prefix(k);
	r->flags = (u16)((is_dir ? SDLIST_DIR : 0) |
			 ((console == LIB_CONSOLE_NONE ? 0xFu : console) << SDLIST_CONSOLE_SHIFT));
	r->count = SDLIST_COUNT_UNKNOWN;
	r->key = (u16)key;
	r->reserved = 0;
	if (key > len + 1)
		text[len + 1] = 0;  // the pad byte
	memset((u8 *)r + used - 1, 0, bytes - used + 1);  // the key's pad byte, if any, and the word's
	l->put(l->base + l->top, r, bytes);
	// The key's first bytes, for the sort
	u32 *w = l->scratch->key[l->count];
	for (u32 i = 0; i < SDLIST_SORT_WORDS; i++) {
		u32 v = 0;
		for (u32 j = 0; j < 4; j++) {
			u32 at = i * 4 + j;
			v = v << 8 | (at < key_len ? (u8)k[at] : 0u);
		}
		w[i] = v;
	}
	l->scratch->dir[l->count] = is_dir ? 1 : 0;
	l->scratch->offset[l->count++] = l->top;
	l->top += bytes;
	l->folders += is_dir ? 1u : 0u;
	return 1;
}

int sdlist_compare(const struct sdlist_rec *a, const struct sdlist_rec *b)
{
	if (a == b)
		return 0;
	int ad = sdlist_is_dir(a), bd = sdlist_is_dir(b);
	if (ad != bd)
		return ad ? -1 : 1;
	if (a->prefix != b->prefix)
		return a->prefix < b->prefix ? -1 : 1;
	int c = strcmp(sdlist_name(a) + a->key, sdlist_name(b) + b->key);
	if (c)
		return c;
	c = strcmp(sdlist_name(a), sdlist_name(b));
	return c ? c : (a < b ? -1 : 1);
}

// The list order of entries a and b, by the scratch's key words first
static int compare_at(const struct sdlist *l, u32 a, u32 b)
{
	if (l->scratch->dir[a] != l->scratch->dir[b])
		return l->scratch->dir[a] ? -1 : 1;
	const u32 *ka = l->scratch->key[a], *kb = l->scratch->key[b];
	for (u32 i = 0; i < SDLIST_SORT_WORDS; i++)
		if (ka[i] != kb[i])
			return ka[i] < kb[i] ? -1 : 1;
	return sdlist_compare((const struct sdlist_rec *)(l->base + l->scratch->offset[a]),
			      (const struct sdlist_rec *)(l->base + l->scratch->offset[b]));
}

void sdlist_finish(struct sdlist *l)
{
	// A bottom-up merge sort of the entries, between order and tmp
	u32 n = l->count;
	u16 *src = l->scratch->order, *dst = l->scratch->tmp;
	for (u32 i = 0; i < n; i++)
		src[i] = (u16)i;
	for (u32 width = 1; width < n; width *= 2) {
		for (u32 lo = 0; lo < n; lo += 2 * width) {
			u32 mid = lo + width < n ? lo + width : n, hi = lo + 2 * width < n ? lo + 2 * width : n;
			u32 i = lo, j = mid, k = lo;
			while (i < mid && j < hi)
				dst[k++] = compare_at(l, src[j], src[i]) < 0 ? src[j++] : src[i++];
			while (i < mid)
				dst[k++] = src[i++];
			while (j < hi)
				dst[k++] = src[j++];
		}
		u16 *t = src;
		src = dst;
		dst = t;
	}
	// The order as record offsets (into the key words' room: done with)
	u32 *offsets = &l->scratch->key[0][0];
	for (u32 i = 0; i < n; i++)
		offsets[i] = l->scratch->offset[src[i]];
	if (n)
		l->put(l->base, offsets, n * 4u);
	l->scratch = NULL;
}

u32 sdlist_find(const struct sdlist *l, const char *name)
{
	for (u32 i = 0; i < l->count; i++)
		if (!strcmp(sdlist_name(sdlist_at(l, i)), name))
			return i;
	return SDLIST_NONE;
}

void sdlist_set_count(struct sdlist *l, u32 i, u32 count)
{
	if (i >= l->count)
		return;
	u16 c = (u16)(count == SDLIST_COUNT_FAILED ? count : count > SDLIST_MAX ? SDLIST_COUNT_MORE : count);
	u32 at = ((const u32 *)l->base)[i] + (u32)offsetof(struct sdlist_rec, count);
	l->put(l->base + at, &c, 2);
}

// --- Texts ------------------------------------------------------------------

void sdlist_number(char *out, u32 size, u32 n)
{
	lang_number(out, size, n);
}

void sdlist_size_text(char *out, u32 size, u32 bytes)
{
	u32 unit = 0, whole = bytes, tenths = 0;
	while (whole >= 1024 && unit < STR_SIZE_GB - STR_SIZE_B) {
		u32 rest = whole % 1024;
		whole /= 1024;
		tenths = rest * 10 / 1024;
		unit++;
	}
	if (unit && whole < 10 && tenths)
		SF(out, size, STR_SIZE_B + unit, LT(whole * 10 + tenths));
	else
		SF(out, size, STR_SIZE_B + unit, LN(whole));
}

void sdlist_count_text(char *out, u32 size, u32 count)
{
	if (!size)
		return;
	if (count == SDLIST_COUNT_UNKNOWN || count == SDLIST_COUNT_FAILED) {
		out[0] = 0;
	} else if (count == 0) {
		snprintf(out, size, "%s", S(STR_SD_ITEMS_NONE));
	} else {
		char n[16];
		int more = count == SDLIST_COUNT_MORE;
		u32 len = lang_number(n, sizeof(n) - 1, more ? SDLIST_MAX : count);
		if (more)
			n[len++] = '+', n[len] = 0;
		SF(out, size, STR_N(STR_SD_ITEMS, count), LS(n));
	}
}
