// See library.h. Hardware-free: tests/host builds it.
#include "library.h"

#include <stddef.h>
#include <string.h>

#include "charset.h"
#include "crc32.h"
#include "gamename.h"

// --- Names ------------------------------------------------------------------

static int lower(int c)
{
	return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static int same_name(const char *a, const char *b)
{
	while (*a && lower((u8)*a) == lower((u8)*b)) {
		a++;
		b++;
	}
	return !*a && !*b;
}

u32 library_console_of(const char *name)
{
	// Extensions to consoles; v0.6's systems add rows
	static const struct {
		char ext[4];
		u8 console;
	} exts[] = {
		{"gba", LIB_GBA},
		{"gbc", LIB_GBC},
		{"gb", LIB_GB},
		{"nes", LIB_NES},
	};
	const char *dot = strrchr(name, '.');
	if (!dot || dot == name)
		return LIB_CONSOLE_NONE;
	for (u32 i = 0; i < sizeof(exts) / sizeof(exts[0]); i++)
		if (same_name(dot + 1, exts[i].ext))
			return exts[i].console;
	return LIB_CONSOLE_NONE;
}

// U+00C0-U+00FF without accents, lower case; 0 keeps the character (x, ÷)
static const char *const latin1_fold[64] = {
	"a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i",  "i",  // U+00C0
	"d", "n", "o", "o", "o", "o", "o",  0,   "o", "u", "u", "u", "u", "y", "th", "ss",  // U+00D0
	"a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i",  "i",  // U+00E0
	"d", "n", "o", "o", "o", "o", "o",  0,   "o", "u", "u", "u", "u", "y", "th", "y",  // U+00F0
};

u32 library_sort_key(char *out, u32 size, const char *display)
{
	u32 n = 0, len = (u32)strlen(display), pos = 0;
	u32 room = size ? size - 1 : 0;
	if (room > LIBRARY_KEY_MAX)
		room = LIBRARY_KEY_MAX;
#define PUT(ch) \
	do { \
		if (n >= room) \
			goto full; \
		out[n++] = (char)(ch); \
	} while (0)
	while (pos < len) {
		u8 c = (u8)display[pos];
		if (c >= '0' && c <= '9') {
			// A number: 0x01, the count of its digits after leading zeros, the digits
			while (pos < len && display[pos] == '0')
				pos++;
			u32 first = pos;
			while (pos < len && display[pos] >= '0' && display[pos] <= '9')
				pos++;
			if (first == pos)  // all zeros: one 0
				first = pos - 1;
			u32 digits = pos - first;
			PUT(1);
			PUT(digits > 0xFF ? 0xFF : digits);
			for (u32 i = first; i < pos; i++)
				PUT(display[i]);
			continue;
		}
		if (c < 0x80) {  // ASCII without the decoder's call: most names are all ASCII
			PUT(lower(c));
			pos++;
			continue;
		}
		u32 at = pos;
		u32 cp = charset_next_latin1(display, len, &pos);
		if (cp < 0x80) {
			PUT(lower((int)cp));
		} else if (cp >= 0xC0 && cp <= 0xFF && latin1_fold[cp - 0xC0]) {
			for (const char *f = latin1_fold[cp - 0xC0]; *f; f++)
				PUT(*f);
		} else if (cp == 0xA0) {
			PUT(' ');
		} else {
			// Anything else as it is in UTF-8: lead bytes from 0xC2 sort after z
			for (u32 i = at; i < pos; i++)
				PUT(display[i]);
		}
	}
full:
#undef PUT
	if (size)
		out[n] = 0;
	return n;
}

u32 library_key_prefix(const char *key)
{
	u32 p = 0;
	for (u32 i = 0; i < 4; i++) {
		p = p << 8 | (u8)*key;
		if (*key)
			key++;
	}
	return p;
}

// --- Building ---------------------------------------------------------------

static u32 even(u32 n)
{
	return (n + 1) & ~1u;
}

static u32 four(u32 n)
{
	return (n + 3) & ~3u;
}

// Appends bytes of text (even) to the pool; the offset, or LIB_POOL_MAX if
// full
static u32 pool_add(u8 *slot, lib_put_fn put, u32 *pool, const char *text, u32 bytes)
{
	if (*pool + bytes > LIB_POOL_MAX)
		return LIB_POOL_MAX;
	u32 at = *pool;
	put(slot + LIB_OFF_POOL + at, text, bytes);
	*pool += bytes;
	return at;
}

void lib_build_begin(struct lib_build *b, u8 *slot, lib_put_fn put)
{
	memset(b, 0, sizeof(*b));
	b->slot = slot;
	b->put = put;
}

u16 lib_build_folder(struct lib_build *b, const char *path)
{
	u32 len = (u32)strnlen(path, LIBRARY_NAME_MAX + 1);
	if (len > LIBRARY_NAME_MAX)
		return LIBRARY_NONE;
	if (b->folders >= LIBRARY_FOLDERS_MAX) {
		b->flags |= LIB_H_TRUNCATED;
		return LIBRARY_NONE;
	}
	memset(b->text, 0, even(len + 1));
	memcpy(b->text, path, len);
	u32 at = pool_add(b->slot, b->put, &b->pool, b->text, even(len + 1));
	if (at == LIB_POOL_MAX) {
		b->flags |= LIB_H_TRUNCATED;
		return LIBRARY_NONE;
	}
	b->put(b->slot + LIB_OFF_FOLDERS + b->folders * 4u, &at, 4);
	return b->folders++;
}

// Writes name, its key and its display name into text as the pool keeps
// them, each NUL-ended and padded with a NUL to a halfword; their bytes.
// display may be lib_build_display(b)'s buffer. *gap: the key's offset from
// the name's, halved (lib_rec.name's top byte).
static u32 name_and_key(struct lib_build *b, const char *name, const char *display, u32 *prefix, u32 *gap)
{
	char *shown = lib_build_display(b);
	if (!display)
		gamename_clean(shown, name);
	else if (display != shown) {
		u32 n = (u32)strnlen(display, LIBRARY_NAME_MAX);
		memcpy(shown, display, n);
		shown[n] = 0;
	}
	u32 len = (u32)strlen(name);
	u32 name_bytes = even(len + 1);
	memcpy(b->text, name, len);
	b->text[len] = b->text[name_bytes - 1] = 0;
	char *key = b->text + name_bytes;
	u32 key_len = library_sort_key(key, LIBRARY_KEY_MAX + 1, shown);
	u32 key_bytes = even(key_len + 1);
	key[key_bytes - 1] = 0;
	// The display name after the key (it was past both)
	u32 shown_len = (u32)strnlen(shown, LIBRARY_NAME_MAX);
	u32 shown_bytes = even(shown_len + 1);
	char *to = key + key_bytes;
	memmove(to, shown, shown_len);
	to[shown_len] = to[shown_bytes - 1] = 0;
	*prefix = library_key_prefix(key);
	*gap = name_bytes / 2;
	return name_bytes + key_bytes + shown_bytes;
}

int lib_build_game(struct lib_build *b, u16 folder, const char *name, const char *display, u32 size, u32 console,
		   u32 flags)
{
	if (console >= LIB_CONSOLES || strnlen(name, LIBRARY_NAME_MAX + 1) > LIBRARY_NAME_MAX)
		return LIB_BAD_NAME;
	b->games_seen++;
	if (b->records >= LIBRARY_GAMES_MAX || folder == LIBRARY_NONE) {
		b->flags |= LIB_H_TRUNCATED;
		return LIB_FULL;
	}
	struct lib_rec r;
	u32 gap;
	u32 bytes = name_and_key(b, name, display, &r.prefix, &gap);
	u32 at = pool_add(b->slot, b->put, &b->pool, b->text, bytes);
	if (at == LIB_POOL_MAX) {
		b->flags |= LIB_H_TRUNCATED;
		return LIB_FULL;
	}
	r.name = at | gap << 24;
	r.folder = folder;
	r.flags = (u16)(console | (flags & (LIB_F_SAVE | LIB_F_CUSTOM)));
	r.size = size;
	b->put(b->slot + LIB_OFF_RECORDS + b->records * 16u, &r, sizeof(r));
	b->records++;
	b->counts[console]++;
	return LIB_ADDED;
}

void lib_build_header(struct lib_build *b, struct lib_header *h, u32 walk_hash, u32 volume_serial, u32 total_files)
{
	memset(h, 0, sizeof(*h));
	h->magic = LIBRARY_MAGIC;
	h->version = LIBRARY_VERSION;
	h->header_bytes = sizeof(struct lib_header);
	h->record_bytes = sizeof(struct lib_rec);
	h->flags = b->flags;
	h->records = b->records;
	h->folders = b->folders;
	memcpy(h->counts, b->counts, sizeof(h->counts));
	h->total_files = total_files;
	h->pool_bytes = b->pool;
	h->walk_hash = walk_hash;
	h->volume_serial = volume_serial;
	h->games_seen = b->games_seen;
	b->put(b->slot + LIB_OFF_HEADER, h, sizeof(*h));
}

// --- Sorting ----------------------------------------------------------------

const char *lib_name(const u8 *slot, u32 rec)
{
	return lib_str(slot, lib_rec_at(slot, rec)->name & LIB_NAME_OFFSET);
}

const char *lib_key(const u8 *slot, u32 rec)
{
	u32 name = lib_rec_at(slot, rec)->name;
	return lib_str(slot, (name & LIB_NAME_OFFSET) + (name >> 24) * 2);
}

const char *lib_display(const u8 *slot, u32 rec)
{
	const char *key = lib_key(slot, rec);
	return key + even((u32)strlen(key) + 1);
}

const char *lib_folder(const u8 *slot, u32 rec)
{
	const u32 *folders = (const u32 *)(slot + LIB_OFF_FOLDERS);
	return lib_str(slot, folders[lib_rec_at(slot, rec)->folder]);
}

static int bytes_cmp(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return (int)(u8)*a - (int)(u8)*b;
}

int lib_compare(const u8 *slot, u32 a, u32 b)
{
	if (a == b)
		return 0;
	const struct lib_rec *ra = lib_rec_at(slot, a), *rb = lib_rec_at(slot, b);
	u32 ca = ra->flags & LIB_F_CONSOLE, cb = rb->flags & LIB_F_CONSOLE;
	if (ca != cb)
		return ca < cb ? -1 : 1;
	if (ra->prefix != rb->prefix)
		return ra->prefix < rb->prefix ? -1 : 1;
	int c = bytes_cmp(lib_key(slot, a), lib_key(slot, b));
	if (!c)
		c = bytes_cmp(lib_name(slot, a), lib_name(slot, b));
	if (!c && ra->folder != rb->folder)
		c = bytes_cmp(lib_folder(slot, a), lib_folder(slot, b));
	if (!c)
		c = a < b ? -1 : 1;
	return c;
}

static void sort_flush(struct lib_sort *s)
{
	if (!s->buffered)
		return;
	u8 *dst = s->in_tmp ? (u8 *)s->slot + LIB_OFF_ORDER : s->tmp;
	s->put(dst + (s->k - s->buffered) * 2u, s->buf, s->buffered * 2u);
	s->buffered = 0;
}

static void sort_merge_at(struct lib_sort *s, u32 lo)
{
	s->lo = lo;
	s->mid = lo + s->width < s->n ? lo + s->width : s->n;
	s->hi = lo + 2 * s->width < s->n ? lo + 2 * s->width : s->n;
	s->i = s->lo;
	s->j = s->mid;
}

void lib_sort_begin(struct lib_sort *s, u8 *slot, u8 *tmp, u32 n, lib_put_fn put)
{
	memset(s, 0, sizeof(*s));
	s->slot = slot;
	s->tmp = tmp;
	s->put = put;
	s->n = n;
	s->width = 1;
	// The passes alternate between the order and tmp: start where an even
	// count of them ends in the order
	u32 passes = 0;
	for (u32 w = 1; w < n; w *= 2)
		passes++;
	s->in_tmp = passes & 1;
	u8 *start = s->in_tmp ? tmp : slot + LIB_OFF_ORDER;
	for (u32 i = 0; i < n; i += LIB_SORT_BUF) {
		u32 m = n - i < LIB_SORT_BUF ? n - i : LIB_SORT_BUF;
		for (u32 k = 0; k < m; k++)
			s->buf[k] = (u16)(i + k);
		put(start + i * 2, s->buf, m * 2);
	}
	s->done = passes == 0;
	sort_merge_at(s, 0);
}

int lib_sort_step(struct lib_sort *s, u32 budget)
{
	while (!s->done && budget--) {
		const u16 *src = (const u16 *)(s->in_tmp ? s->tmp : s->slot + LIB_OFF_ORDER);
		u16 next;
		if (s->i < s->mid && (s->j >= s->hi || lib_compare(s->slot, src[s->i], src[s->j]) < 0))
			next = src[s->i++];
		else
			next = src[s->j++];
		s->buf[s->buffered++] = next;
		s->k++;
		if (s->buffered == LIB_SORT_BUF)
			sort_flush(s);
		if (s->i < s->mid || s->j < s->hi)
			continue;
		if (s->hi < s->n) {
			sort_merge_at(s, s->hi);
			continue;
		}
		// The pass is done: the next reads what it wrote
		sort_flush(s);
		s->in_tmp ^= 1;
		s->width *= 2;
		s->k = 0;
		if (s->width >= s->n)
			s->done = 1;
		else
			sort_merge_at(s, 0);
	}
	return s->done;
}

// --- The file image ---------------------------------------------------------

static u32 order_len(const struct lib_header *h)
{
	u32 n = 0;
	for (u32 i = 0; i < LIBRARY_CONSOLE_SLOTS; i++)
		n += h->counts[i];
	return n;
}

void lib_sections(const struct lib_header *h, struct lib_section out[LIB_SECTIONS])
{
	out[0].off = LIB_OFF_RECORDS;
	out[0].bytes = h->records * 16u;
	out[1].off = LIB_OFF_ORDER;
	out[1].bytes = four(order_len(h) * 2u);
	out[2].off = LIB_OFF_FOLDERS;
	out[2].bytes = h->folders * 4u;
	out[3].off = LIB_OFF_POOL;
	out[3].bytes = four(h->pool_bytes);
}

u32 lib_file_bytes(const struct lib_header *h)
{
	struct lib_section s[LIB_SECTIONS];
	lib_sections(h, s);
	u32 n = sizeof(struct lib_header);
	for (u32 i = 0; i < LIB_SECTIONS; i++)
		n += s[i].bytes;
	return n;
}

u32 lib_header_crc(const struct lib_header *h)
{
	return crc32(h, offsetof(struct lib_header, header_crc));
}

int lib_header_ok(const struct lib_header *h)
{
	return h->magic == LIBRARY_MAGIC && h->version == LIBRARY_VERSION &&
	       h->header_bytes == sizeof(struct lib_header) && h->record_bytes == sizeof(struct lib_rec) &&
	       h->records <= LIBRARY_GAMES_MAX && h->folders <= LIBRARY_FOLDERS_MAX && order_len(h) <= h->records &&
	       h->pool_bytes <= LIB_POOL_MAX && !(h->pool_bytes & 1) && h->header_crc == lib_header_crc(h);
}

// --- Queries -----------------------------------------------------------------

u32 lib_console_start(const u8 *slot, u32 console)
{
	const struct lib_header *h = lib_hdr(slot);
	u32 n = 0;
	for (u32 i = 0; i < console && i < LIBRARY_CONSOLE_SLOTS; i++)
		n += h->counts[i];
	return n;
}

u32 lib_nth(const u8 *slot, u32 console, u32 n)
{
	if (console >= LIBRARY_CONSOLE_SLOTS || n >= lib_hdr(slot)->counts[console])
		return LIBRARY_NONE;
	return lib_order(slot)[lib_console_start(slot, console) + n];
}

u32 lib_position(const u8 *slot, u32 rec)
{
	if (rec >= lib_hdr(slot)->records || (lib_rec_at(slot, rec)->flags & LIB_F_GONE))
		return LIBRARY_NONE;
	u32 console = lib_rec_at(slot, rec)->flags & LIB_F_CONSOLE, start = lib_console_start(slot, console);
	u32 lo = 0, hi = lib_hdr(slot)->counts[console];
	const u16 *order = lib_order(slot) + start;
	while (lo < hi) {
		u32 mid = lo + (hi - lo) / 2;
		int c = lib_compare(slot, order[mid], rec);
		if (!c)
			return mid;
		if (c < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	return LIBRARY_NONE;
}

u32 lib_path(const u8 *slot, u32 rec, char *out, u32 size)
{
	const char *folder = lib_folder(slot, rec), *name = lib_name(slot, rec);
	u32 f = (u32)strlen(folder), n = (u32)strlen(name);
	if (f == 1 && folder[0] == '/')
		f = 0;  // the root: "/name"
	if (f + 1 + n + 1 > size)
		return 0;
	memcpy(out, folder, f);
	out[f] = '/';
	memcpy(out + f + 1, name, n + 1);
	return f + 1 + n;
}

u32 lib_find(const u8 *slot, const char *path)
{
	const char *slash = strrchr(path, '/');
	if (!slash)
		return LIBRARY_NONE;
	const char *name = slash + 1;
	u32 folder_len = (u32)(slash - path);
	const struct lib_header *h = lib_hdr(slot);
	for (u32 i = 0; i < h->records; i++) {
		const struct lib_rec *r = lib_rec_at(slot, i);
		if ((r->flags & LIB_F_GONE) || bytes_cmp(lib_name(slot, i), name))
			continue;
		const char *folder = lib_folder(slot, i);
		u32 len = (u32)strlen(folder);
		if (len == 1 && folder[0] == '/')
			len = 0;
		if (len == folder_len && !memcmp(folder, path, len))
			return i;
	}
	return LIBRARY_NONE;
}

// Where rec is in the order, or LIBRARY_NONE
static u32 order_pos(const u8 *slot, u32 rec)
{
	const struct lib_rec *r = lib_rec_at(slot, rec);
	u32 console = r->flags & LIB_F_CONSOLE, start = lib_console_start(slot, console);
	u32 end = start + lib_hdr(slot)->counts[console];
	const u16 *order = lib_order(slot);
	for (u32 i = start; i < end; i++)
		if (order[i] == rec)
			return i;
	return LIBRARY_NONE;
}

// Moves order entries [from, to) by one: down (to from - 1) or up (to from + 1)
static void order_shift(u8 *slot, lib_put_fn put, u32 from, u32 to, int up)
{
	u16 buf[32];
	const u16 *order = lib_order(slot);
	u8 *base = slot + LIB_OFF_ORDER;
	if (up) {
		// From the end, so nothing is read after it is overwritten
		while (to > from) {
			u32 m = to - from < 32 ? to - from : 32;
			to -= m;
			memcpy(buf, order + to, m * 2);
			put(base + (to + 1) * 2, buf, m * 2);
		}
	} else {
		for (u32 i = from; i < to; i += 32) {
			u32 m = to - i < 32 ? to - i : 32;
			memcpy(buf, order + i, m * 2);
			put(base + (i - 1) * 2, buf, m * 2);
		}
	}
}

static void put_header(u8 *slot, lib_put_fn put, struct lib_header *h)
{
	put(slot + LIB_OFF_HEADER, h, sizeof(*h));
}

// Takes rec out of the order and its console's count
static int order_remove(u8 *slot, lib_put_fn put, u32 rec)
{
	u32 at = order_pos(slot, rec);
	if (at == LIBRARY_NONE)
		return 0;
	struct lib_header h = *lib_hdr(slot);
	order_shift(slot, put, at + 1, order_len(&h), 0);
	h.counts[lib_rec_at(slot, rec)->flags & LIB_F_CONSOLE]--;
	put_header(slot, put, &h);
	return 1;
}

int lib_forget(u8 *slot, lib_put_fn put, u32 rec)
{
	if (rec >= lib_hdr(slot)->records)
		return 0;
	struct lib_rec r = *lib_rec_at(slot, rec);
	if (r.flags & LIB_F_GONE)
		return 0;
	order_remove(slot, put, rec);
	r.flags |= LIB_F_GONE;
	put(slot + LIB_OFF_RECORDS + rec * 16u, &r, sizeof(r));
	return 1;
}

int lib_rename(u8 *slot, lib_put_fn put, u32 rec, const char *new_name, struct lib_build *scratch)
{
	if (rec >= lib_hdr(slot)->records || (lib_rec_at(slot, rec)->flags & LIB_F_GONE))
		return 0;
	u32 console = library_console_of(new_name);
	struct lib_rec r = *lib_rec_at(slot, rec);
	if (console != (r.flags & LIB_F_CONSOLE) || strnlen(new_name, LIBRARY_NAME_MAX + 1) > LIBRARY_NAME_MAX)
		return 0;
	u32 gap;
	u32 bytes = name_and_key(scratch, new_name, NULL, &r.prefix, &gap);
	struct lib_header h = *lib_hdr(slot);
	u32 pool = h.pool_bytes;
	u32 at = pool_add(slot, put, &pool, scratch->text, bytes);
	if (at == LIB_POOL_MAX)
		return 0;
	order_remove(slot, put, rec);
	h = *lib_hdr(slot);
	h.pool_bytes = pool;
	r.name = at | gap << 24;
	r.flags &= (u16)~LIB_F_CUSTOM;  // its cleaned name until the walk reads names.txt again
	put(slot + LIB_OFF_RECORDS + rec * 16u, &r, sizeof(r));
	// Its place among the rest of its console's games
	u32 start = lib_console_start(slot, console), lo = start, hi = start + h.counts[console];
	const u16 *order = lib_order(slot);
	while (lo < hi) {
		u32 mid = (lo + hi) / 2;
		if (lib_compare(slot, order[mid], rec) < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	order_shift(slot, put, lo, order_len(&h), 1);
	u16 entry = (u16)rec;
	put(slot + LIB_OFF_ORDER + lo * 2, &entry, 2);
	h.counts[console]++;
	put_header(slot, put, &h);
	return 1;
}

int lib_body_ok(const u8 *slot, u32 *seen)
{
	const struct lib_header *h = lib_hdr(slot);
	const u32 *folders = (const u32 *)(slot + LIB_OFF_FOLDERS);
	if (h->pool_bytes && *lib_str(slot, h->pool_bytes - 1))
		return 0;  // every string ends inside the pool
	for (u32 i = 0; i < h->folders; i++)
		if (folders[i] >= h->pool_bytes)
			return 0;
	u32 counts[LIBRARY_CONSOLE_SLOTS] = {0};
	for (u32 i = 0; i < h->records; i++) {
		const struct lib_rec *r = lib_rec_at(slot, i);
		u32 name = r->name & LIB_NAME_OFFSET, key = name + (r->name >> 24) * 2;
		if (r->folder >= h->folders || key <= name || key >= h->pool_bytes ||
		    (r->flags & LIB_F_CONSOLE) >= LIB_CONSOLES)
			return 0;
		// The display name after the key, inside the pool too, and ended there
		u32 shown = key + even((u32)strnlen(lib_str(slot, key), h->pool_bytes - key) + 1);
		if (shown >= h->pool_bytes ||
		    strnlen(lib_str(slot, shown), h->pool_bytes - shown) >= h->pool_bytes - shown)
			return 0;
		if (!(r->flags & LIB_F_GONE))
			counts[r->flags & LIB_F_CONSOLE]++;
	}
	// The order lists each game once, in its console's run
	const u16 *order = lib_order(slot);
	memset(seen, 0, LIBRARY_GAMES_MAX / 8);
	for (u32 c = 0, at = 0; c < LIBRARY_CONSOLE_SLOTS; c++) {
		if (counts[c] != h->counts[c])
			return 0;
		for (u32 k = 0; k < h->counts[c]; k++, at++) {
			u32 r = order[at];
			if (r >= h->records || (lib_rec_at(slot, r)->flags & (LIB_F_CONSOLE | LIB_F_GONE)) != c ||
			    (seen[r / 32] >> (r % 32) & 1))
				return 0;
			seen[r / 32] |= 1u << (r % 32);
		}
	}
	return 1;
}
