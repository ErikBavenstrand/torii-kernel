// See gamename.h.
#include "gamename.h"

#include <string.h>

// --- Tunable tables ---------------------------------------------------------

// Articles that No-Intro moves to the end of a title ("Legend of Zelda,
// The"), which cleaning moves back to the front. Matched with case; one
// ending in an apostrophe joins the title without a space ("L'Eclair").
static const char *const articles[] = {
	"The", "A", "An",  // English
	"Le", "La", "Les", "L'",  // French
	"Der", "Die", "Das",  // German
	"El", "Los", "Las",  // Spanish
	"Il", "Lo", "Gli",  // Italian
	"De", "Het", "Een",  // Dutch
};

// Tags worth showing when two games collide and differ in several tags:
// the first that differs and starts with one of these wins over earlier
// differences ("(Rev 1)" over a language list). A tag matches when its text
// starts with the entry, followed by something other than a letter if the
// entry ends in one ("Rev 1", "v1.1", "T+Eng", not "Virtual" for "v").
static const char *const kept_tags[] = {
	"Rev", "v", "Beta", "Proto", "Demo", "Sample", "Virtual Console", "T+", "T-",
};

// Folders in the card's root that hold the kernel's and the cart's own files.
static const char *const system_folders[] = {
	"torii", "SAVER", "RTS", "SYSTEM", "PATCH", "CHEAT", "System Volume Information",
};

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

// The most tags gamename_dedupe compares in one name.
#define TAGS_MAX 32

// --- Small helpers ----------------------------------------------------------

static int is_space(char c)
{
	return c == ' ' || c == '\t';
}

static int is_digit(char c)
{
	return c >= '0' && c <= '9';
}

static int is_ascii_letter(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// A letter for the scene-prefix rule: ASCII, or any byte of a multi-byte
// character (UTF-8 letters outside ASCII)
static int is_letter(char c)
{
	return is_ascii_letter(c) || (unsigned char)c >= 0x80;
}

static char lower(char c)
{
	return c >= 'A' && c <= 'Z' ? (char)(c + 'a' - 'A') : c;
}

static int is_opener(char c)
{
	return c == '(' || c == '[';
}

static int is_closer(char c)
{
	return c == ')' || c == ']';
}

// len, or less so the cut doesn't split a UTF-8 character, if len > max
static u32 utf8_cut(const char *s, u32 len, u32 max)
{
	if (len <= max)
		return len;
	len = max;
	while (len > 0 && ((unsigned char)s[len] & 0xC0) == 0x80)
		len--;
	return len;
}

// The part of path after its last '/'
static const char *base_of(const char *path)
{
	const char *slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

// The end of the bracket group opening at s[at] (the index of its closer),
// or -1 if it never closes. Groups nest: "(a (b))" and "[x (y)]" are one.
static int group_end(const char *s, u32 len, u32 at)
{
	u32 depth = 0;
	for (u32 i = at; i < len; i++) {
		if (is_opener(s[i])) {
			depth++;
		} else if (is_closer(s[i])) {
			if (--depth == 0)
				return (int)i;
		}
	}
	return -1;
}

// --- Cleaning ---------------------------------------------------------------

// Rule 1: name's first GAMENAME_MAX bytes into out, without an extension
// (the last '.' and 1 to 4 ASCII letters or digits after it, not at the
// start) when drop_ext; returns the length.
static u32 step_base(char *out, const char *name, int drop_ext)
{
	u32 len = utf8_cut(name, strlen(name), GAMENAME_MAX);
	memcpy(out, name, len);
	if (drop_ext) {
		u32 ext = 0;
		while (ext < len && ext <= 4 && (is_ascii_letter(name[len - 1 - ext]) || is_digit(name[len - 1 - ext])))
			ext++;
		if (ext >= 1 && ext <= 4 && ext + 1 < len && name[len - 1 - ext] == '.')
			len -= ext + 1;
	}
	return len;
}

// Rule 2: underscores are spaces in a name that has no spaces
static void step_underscores(char *s, u32 len)
{
	if (memchr(s, ' ', len))
		return;
	for (u32 i = 0; i < len; i++) {
		if (s[i] == '_')
			s[i] = ' ';
	}
}

// Rule 3: every (...) and [...] group becomes a space; an unclosed opener
// stays as text
static u32 step_groups(char *s, u32 len)
{
	u32 w = 0;
	for (u32 r = 0; r < len;) {
		int end = is_opener(s[r]) ? group_end(s, len, r) : -1;
		if (end >= 0) {
			s[w++] = ' ';
			r = (u32)end + 1;
		} else {
			s[w++] = s[r++];
		}
	}
	return w;
}

// Rule 4: drops a release-number prefix, "0001 - ": 4 or 5 digits, a dash,
// then a name with a letter. 4 digits that read as a year (1900 to 2099)
// stay: "1943 - The Battle of Midway" is a title.
static u32 step_scene_prefix(char *s, u32 len)
{
	u32 i = 0;
	while (i < len && is_space(s[i]))
		i++;
	u32 start = i, value = 0;
	while (i < len && is_digit(s[i]))
		value = value * 10 + (u32)(s[i++] - '0');
	u32 digits = i - start;
	if (digits < 4 || digits > 5 || (digits == 4 && value >= 1900 && value <= 2099))
		return len;
	while (i < len && is_space(s[i]))
		i++;
	if (i >= len || s[i] != '-')
		return len;
	i++;
	while (i < len && is_space(s[i]))
		i++;
	u32 letter = i;
	while (letter < len && !is_letter(s[letter]))
		letter++;
	if (letter >= len)
		return len;
	memmove(s, s + i, len - i);
	return len - i;
}

// Rule 5: runs of spaces and tabs become one space; none at either end
static u32 step_spaces(char *s, u32 len)
{
	u32 w = 0;
	for (u32 r = 0; r < len; r++) {
		if (is_space(s[r])) {
			if (w > 0 && s[w - 1] != ' ')
				s[w++] = ' ';
		} else {
			s[w++] = s[r];
		}
	}
	if (w > 0 && s[w - 1] == ' ')
		w--;
	return w;
}

// Rule 7: separators left over by removed tags: a trailing " -", a leading
// "- ", a lone "-", and " - - " as " - ". Inner " - " stay.
static u32 step_separators(char *s, u32 len)
{
	for (;;) {
		if (len == 1 && s[0] == '-') {
			len = 0;
		} else if (len >= 2 && s[len - 2] == ' ' && s[len - 1] == '-') {
			len -= 2;
		} else if (len >= 2 && s[0] == '-' && s[1] == ' ') {
			memmove(s, s + 2, len - 2);
			len -= 2;
		} else {
			break;
		}
	}
	for (u32 i = 0; i + 5 <= len; i++) {
		if (memcmp(s + i, " - - ", 5) == 0) {
			memmove(s + i + 1, s + i + 3, len - i - 3);
			len -= 2;
			i--;  // again here: " - - - "
		}
	}
	return len;
}

// Rule 6: a trailing article of the title part (up to the first " - ")
// moves to the front: "Legend of Zelda, The - A Link to the Past" becomes
// "The Legend of Zelda - A Link to the Past"
static u32 step_article(char *s, u32 len)
{
	u32 title = 0;
	while (title < len && !(title + 3 <= len && memcmp(s + title, " - ", 3) == 0))
		title++;
	for (u32 k = 0; k < COUNT(articles); k++) {
		const char *a = articles[k];
		u32 alen = (u32)strlen(a), cut = alen + 2;  // ", " and the article
		if (title <= cut || s[title - cut] != ',' || s[title - cut + 1] != ' ' ||
		    memcmp(s + title - alen, a, alen) != 0)
			continue;
		u32 rest = title - cut, space = a[alen - 1] != '\'';
		memmove(s + alen + space, s, rest);
		memcpy(s, a, alen);
		if (space)
			s[alen] = ' ';
		u32 moved = alen + space + rest;
		memmove(s + moved, s + title, len - title);
		return moved + len - title;
	}
	return len;
}

static u32 clean(char *out, const char *name, int drop_ext)
{
	u32 len = step_base(out, name, drop_ext);
	step_underscores(out, len);
	len = step_groups(out, len);
	len = step_scene_prefix(out, len);
	len = step_spaces(out, len);
	// Separators before articles, so "Game, The - (Disc 1)" still moves
	// its article (the research's order has them after)
	len = step_separators(out, len);
	len = step_article(out, len);
	len = step_spaces(out, step_separators(out, len));
	if (len == 0)  // Rule 8: nothing left but tags or digits
		len = step_base(out, name, drop_ext);
	out[len] = 0;
	return len;
}

u32 gamename_clean(char *out, const char *path)
{
	return clean(out, base_of(path), 1);
}

u32 gamename_clean_title(char *out, const char *name)
{
	return clean(out, name, 0);
}

int gamename_same(const char *a, const char *b)
{
	while (*a && lower(*a) == lower(*b)) {
		a++;
		b++;
	}
	return lower(*a) == lower(*b);
}

// --- Dedupe -----------------------------------------------------------------

// The tags of one name: where each (...) or [...] group starts and its
// length, brackets included
struct tags {
	const char *s;
	u32 count;
	u16 at[TAGS_MAX], len[TAGS_MAX];
};

static void tags_of(struct tags *t, const char *name)
{
	const char *s = base_of(name);
	u32 len = (u32)strlen(s);
	t->s = s;
	t->count = 0;
	for (u32 i = 0; i < len && t->count < TAGS_MAX; i++) {
		int end = is_opener(s[i]) ? group_end(s, len, i) : -1;
		if (end < 0)
			continue;
		t->at[t->count] = (u16)i;
		t->len[t->count++] = (u16)((u32)end + 1 - i);
		i = (u32)end;
	}
}

static int tag_differs(const struct tags *a, const struct tags *b, u32 k)
{
	if (k >= a->count || k >= b->count)
		return (k < a->count) != (k < b->count);
	return a->len[k] != b->len[k] || memcmp(a->s + a->at[k], b->s + b->at[k], a->len[k]) != 0;
}

static int tag_kept(const struct tags *t, u32 k)
{
	if (k >= t->count)
		return 0;
	const char *text = t->s + t->at[k] + 1;
	u32 len = t->len[k] - 2u;
	for (u32 i = 0; i < COUNT(kept_tags); i++) {
		const char *e = kept_tags[i];
		u32 elen = (u32)strlen(e);
		if (elen > len || memcmp(text, e, elen) != 0)
			continue;
		if (!is_ascii_letter(e[elen - 1]) || elen == len || !is_ascii_letter(text[elen]))
			return 1;
	}
	return 0;
}

// Appends len bytes of s to out (holding *n bytes), as far as GAMENAME_MAX
// allows, cut at a character boundary
static void append(char *out, u32 *n, const char *s, u32 len)
{
	len = utf8_cut(s, len, GAMENAME_MAX - *n);
	memcpy(out + *n, s, len);
	*n += len;
	out[*n] = 0;
}

u32 gamename_dedupe(char *out, const char *const *names, u32 count, u32 self)
{
	// Two struct tags (about 140 bytes each): the kernel's deepest call
	// chain doesn't come through here (the library is built from the menu)
	struct tags mine, other;
	tags_of(&mine, names[self]);
	u32 positions = 0;  // bit k: tag k tells this name from another
	int same_tags = 0;
	for (u32 h = 0; h < count; h++) {
		if (h == self)
			continue;
		tags_of(&other, names[h]);
		u32 most = mine.count > other.count ? mine.count : other.count;
		int first = -1, kept = -1;
		for (u32 k = 0; k < most; k++) {
			if (!tag_differs(&mine, &other, k))
				continue;
			if (first < 0)
				first = (int)k;
			if (kept < 0 && k < mine.count && k < other.count && (tag_kept(&mine, k) || tag_kept(&other, k)))
				kept = (int)k;
		}
		if (first < 0)
			same_tags = 1;
		else
			positions |= 1u << (kept >= 0 ? kept : first);
	}
	u32 n = (u32)strlen(out);
	for (u32 k = 0; k < mine.count; k++) {
		if (!(positions & (1u << k)))
			continue;
		append(out, &n, " ", 1);
		append(out, &n, mine.s + mine.at[k], mine.len[k]);
	}
	if (same_tags) {
		// The folder the name is in, if it isn't the root
		const char *name = names[self], *end = base_of(name);
		if (end > name + 1) {
			const char *folder = end - 1;
			while (folder > name && folder[-1] != '/')
				folder--;
			append(out, &n, " (", 2);
			append(out, &n, folder, (u32)(end - 1 - folder));
			append(out, &n, ")", 1);
		}
	}
	return n;
}

// --- Custom names -------------------------------------------------------------

// A line's end (its '\n' or the text's end) from its start
static u32 line_end(const struct gamename_custom *c, u32 at)
{
	const char *nl = memchr(c->text + at, '\n', c->len - at);
	return nl ? (u32)(nl - c->text) : c->len;
}

// Compares the path of the line at offset at (it ends at its tab) with
// the NUL-terminated key, without ASCII case
static int path_cmp_key(const struct gamename_custom *c, u32 at, const char *key)
{
	const char *p = c->text + at;
	for (;; p++, key++) {
		char a = *p == '\t' ? 0 : lower(*p), b = lower(*key);
		if (a != b)
			return (unsigned char)a < (unsigned char)b ? -1 : 1;
		if (a == 0)
			return 0;
	}
}

static int path_cmp_lines(const struct gamename_custom *c, u32 x, u32 y)
{
	const char *p = c->text + x, *q = c->text + y;
	for (;; p++, q++) {
		char a = *p == '\t' ? 0 : lower(*p), b = *q == '\t' ? 0 : lower(*q);
		if (a != b)
			return (unsigned char)a < (unsigned char)b ? -1 : 1;
		if (a == 0)
			return 0;
	}
}

// Line order: by path, then by place in the file
static int line_before(const struct gamename_custom *c, u32 x, u32 y)
{
	int d = path_cmp_lines(c, x, y);
	return d < 0 || (d == 0 && x < y);
}

static void sift_down(const struct gamename_custom *c, u32 *a, u32 root, u32 n)
{
	for (;;) {
		u32 child = 2 * root + 1;
		if (child >= n)
			return;
		if (child + 1 < n && line_before(c, a[child], a[child + 1]))
			child++;
		if (!line_before(c, a[root], a[child]))
			return;
		u32 t = a[root];
		a[root] = a[child];
		a[child] = t;
		root = child;
	}
}

// The name of the line at offset at, trimmed and cut to GAMENAME_MAX bytes
static const char *line_name(const struct gamename_custom *c, u32 at, u32 *len)
{
	u32 end = line_end(c, at);
	const char *tab = memchr(c->text + at, '\t', end - at);
	u32 from = (u32)(tab - c->text) + 1;
	while (from < end && ((unsigned char)c->text[from] <= ' ' || c->text[from] == 0x7F))
		from++;
	while (end > from && ((unsigned char)c->text[end - 1] <= ' ' || c->text[end - 1] == 0x7F))
		end--;
	*len = utf8_cut(c->text + from, end - from, GAMENAME_MAX);
	return c->text + from;
}

u32 gamename_custom_parse(struct gamename_custom *c, const char *text, u32 len, u32 *line, u32 line_cap)
{
	c->text = text;
	c->len = text ? len : 0;
	c->line = line;
	c->count = 0;
	c->complete = 1;
	u32 at = 0;
	if (c->len >= 3 && memcmp(text, "\xEF\xBB\xBF", 3) == 0)
		at = 3;
	while (at < c->len) {
		u32 end = line_end(c, at);
		const char *tab = memchr(text + at, '\t', end - at);
		if (text[at] == '/' && tab) {
			u32 name_len;
			line_name(c, at, &name_len);
			if (name_len > 0) {
				if (c->count < line_cap)
					line[c->count++] = at;
				else
					c->complete = 0;
			}
		}
		at = end + 1;
	}
	// Heap sort by path, then drop all but the last line of each path
	for (u32 i = c->count / 2; i-- > 0;)
		sift_down(c, line, i, c->count);
	for (u32 n = c->count; n > 1; n--) {
		u32 t = line[0];
		line[0] = line[n - 1];
		line[n - 1] = t;
		sift_down(c, line, 0, n - 1);
	}
	u32 kept = 0;
	for (u32 i = 0; i < c->count; i++) {
		if (i + 1 < c->count && path_cmp_lines(c, line[i], line[i + 1]) == 0)
			continue;
		line[kept++] = line[i];
	}
	c->count = kept;
	return kept;
}

const char *gamename_custom_get(const struct gamename_custom *c, const char *path, u32 *len)
{
	u32 lo = 0, hi = c->count;
	while (lo < hi) {
		u32 mid = lo + (hi - lo) / 2;
		int d = path_cmp_key(c, c->line[mid], path);
		if (d == 0)
			return line_name(c, c->line[mid], len);
		if (d < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	*len = 0;
	return NULL;
}

// Copies len bytes to out (holding *n of cap); 0 when they don't fit
static int put(char *out, u32 cap, u32 *n, const char *s, u32 len)
{
	if (len > cap - *n)
		return 0;
	memcpy(out + *n, s, len);
	*n += len;
	return 1;
}

static int put_line(char *out, u32 cap, u32 *n, const char *path, u32 path_len, const char *name, u32 name_len)
{
	if (!put(out, cap, n, path, path_len) || !put(out, cap, n, "\t", 1))
		return 0;
	u32 at = *n;
	if (!put(out, cap, n, name, name_len))
		return 0;
	for (u32 i = at; i < *n; i++) {
		if ((unsigned char)out[i] < ' ' || out[i] == 0x7F)
			out[i] = ' ';  // a tab or newline would break the line
	}
	return put(out, cap, n, "\n", 1);
}

u32 gamename_custom_set(const struct gamename_custom *c, const char *path, const char *name, char *out, u32 cap)
{
	u32 path_len = (u32)strlen(path);
	if (!c->complete || path[0] != '/' || strpbrk(path, "\t\r\n"))
		return GAMENAME_TOO_BIG;
	// The new name, trimmed of spaces and control characters at both ends
	u32 name_len = 0;
	if (name) {
		while ((unsigned char)*name <= ' ' && *name)
			name++;
		name_len = (u32)strlen(name);
		while (name_len > 0 && ((unsigned char)name[name_len - 1] <= ' ' || name[name_len - 1] == 0x7F))
			name_len--;
		name_len = utf8_cut(name, name_len, GAMENAME_MAX);
	}
	u32 n = 0;
	int placed = name_len == 0;  // removing: nothing to place
	for (u32 i = 0; i <= c->count; i++) {
		int d = i < c->count ? path_cmp_key(c, c->line[i], path) : 1;
		if (d > 0 && !placed) {
			if (!put_line(out, cap, &n, path, path_len, name, name_len))
				return GAMENAME_TOO_BIG;
			placed = 1;
		}
		if (i == c->count || d == 0)
			continue;
		u32 at = c->line[i], len;
		const char *tab = memchr(c->text + at, '\t', c->len - at);
		const char *old = line_name(c, at, &len);
		if (!put_line(out, cap, &n, c->text + at, (u32)(tab - (c->text + at)), old, len))
			return GAMENAME_TOO_BIG;
	}
	return n;
}

u32 gamename_custom_load(struct gamename_custom *c, const struct gamename_io *io, char *buf, u32 cap, u32 *line,
			 u32 line_cap)
{
	u32 len = io->read(buf, cap);
	if (len == GAMENAME_IO_FAIL || len > cap) {
		gamename_custom_parse(c, NULL, 0, line, line_cap);
		c->complete = 0;
		return 0;
	}
	return gamename_custom_parse(c, buf, len, line, line_cap);
}

int gamename_custom_store(const struct gamename_custom *c, const struct gamename_io *io, const char *path,
			  const char *name, char *out, u32 cap)
{
	u32 len = gamename_custom_set(c, path, name, out, cap);
	return len != GAMENAME_TOO_BIG && io->write(out, len);
}

// --- The display name -----------------------------------------------------------

enum gamename_source gamename_display(char *out, const char *path, const struct gamename_custom *custom,
				      const char *crc_name)
{
	u32 len;
	const char *name = custom ? gamename_custom_get(custom, path, &len) : NULL;
	if (name) {
		memcpy(out, name, len);
		out[len] = 0;
		return GAMENAME_CUSTOM;
	}
	if (crc_name && crc_name[0]) {
		gamename_clean_title(out, crc_name);
		return GAMENAME_CRC;
	}
	gamename_clean(out, path);
	return GAMENAME_CLEAN;
}

// --- Hidden entries -------------------------------------------------------------

int gamename_hidden(const char *dir, const char *name, u32 flags, int show_all)
{
	if (show_all)
		return 0;
	if (name[0] == '.' || (flags & GAMENAME_ATTR_HIDDEN))
		return 1;
	if (!(flags & GAMENAME_DIR))
		return !(flags & GAMENAME_OPENABLE);
	if (dir[0] == 0 || (dir[0] == '/' && dir[1] == 0)) {
		for (u32 i = 0; i < COUNT(system_folders); i++) {
			if (gamename_same(name, system_folders[i]))
				return 1;
		}
	}
	return 0;
}
