// The rename keyboard's model (rename_edit.h).
#include <string.h>

#include "rename_edit.h"

// engine/input.h's d-pad bits (this file builds for the host without it)
#define KEY_RIGHT 0x010
#define KEY_LEFT 0x020
#define KEY_UP 0x040
#define KEY_DOWN 0x080

// --- The name ------------------------------------------------------------------

static int continuation(char c)
{
	return ((u8)c & 0xC0) == 0x80;
}

// The bytes of the character at s[at] (a lone continuation byte counts one)
static u32 char_bytes(const char *s, u32 at, u32 len)
{
	u32 n = 1;
	while (at + n < len && continuation(s[at + n]))
		n++;
	return n;
}

static u32 count_chars(const char *s, u32 len)
{
	u32 n = 0;
	for (u32 i = 0; i < len; i++)
		n += !continuation(s[i]);
	return n;
}

void rename_edit_init(struct rename_edit *e, const char *text, u32 max)
{
	u32 len = text ? (u32)strnlen(text, RENAME_TEXT_MAX + 1) : 0;
	if (len > RENAME_TEXT_MAX) {
		len = RENAME_TEXT_MAX;
		while (len && continuation(text[len]))
			len--;  // cut before a character, not inside it
	}
	memcpy(e->text, text ? text : "", len);
	e->text[len] = 0;
	e->len = (u16)len;
	e->caret = (u16)len;
	e->max = (u8)(max < 255 ? max : 255);
	e->bytes = 0;
}

u32 rename_edit_chars(const struct rename_edit *e)
{
	if (!e->bytes)
		return count_chars(e->text, e->len);
	u32 n = e->len;
	for (u32 i = 0; i < e->len; i++)
		n += e->text[i] == '\n';
	return n;
}

u32 rename_edit_caret_chars(const struct rename_edit *e)
{
	return count_chars(e->text, e->caret);
}

int rename_edit_type(struct rename_edit *e, u32 c)
{
	u8 bytes[2];
	u32 n = 0;
	if ((c < 0x20 && c != '\n') || c > 0xFF || c == 0x7F)
		return 0;
	if (c < 0x80) {
		bytes[n++] = (u8)c;
	} else {
		bytes[n++] = (u8)(0xC0 | c >> 6);
		bytes[n++] = (u8)(0x80 | (c & 0x3F));
	}
	u32 units = e->bytes ? n + (c == '\n') : 1;
	if (rename_edit_chars(e) + units > e->max || e->len + n > RENAME_TEXT_MAX)
		return 0;
	memmove(e->text + e->caret + n, e->text + e->caret, (u32)(e->len - e->caret) + 1);
	memcpy(e->text + e->caret, bytes, n);
	e->len = (u16)(e->len + n);
	e->caret = (u16)(e->caret + n);
	return 1;
}

// The start of the character before the caret
static u32 before(const struct rename_edit *e)
{
	u32 at = e->caret;
	if (at > 0)
		at--;
	while (at > 0 && continuation(e->text[at]))
		at--;
	return at;
}

int rename_edit_delete(struct rename_edit *e)
{
	if (!e->caret)
		return 0;
	u32 at = before(e);
	memmove(e->text + at, e->text + e->caret, (u32)(e->len - e->caret) + 1);
	e->len = (u16)(e->len - (e->caret - at));
	e->caret = (u16)at;
	return 1;
}

int rename_edit_move(struct rename_edit *e, s32 dir)
{
	if (dir < 0) {
		if (!e->caret)
			return 0;
		e->caret = (u16)before(e);
		return 1;
	}
	if (e->caret >= e->len)
		return 0;
	e->caret = (u16)(e->caret + char_bytes(e->text, e->caret, e->len));
	return 1;
}

// A space, or a line break in a text of lines: trimmed at the ends
static int blank(char c, u32 flags)
{
	return c == ' ' || (c == '\n' && (flags & RENAME_LINES));
}

u32 rename_edit_result(const struct rename_edit *e, char *out, u32 flags)
{
	u32 n = 0;
	for (u32 i = 0; i < e->len; i++) {
		u8 c = (u8)e->text[i];
		if (c == '\n' && (flags & RENAME_LINES)) {
			out[n++] = (char)c;
			continue;
		}
		if (c < 0x20 || c == 0x7F || ((flags & RENAME_FILE) && strchr("\\/:*?\"<>|", c)))
			continue;
		out[n++] = (char)c;
	}
	out[n] = 0;
	// Spaces at the ends
	u32 start = 0;
	while (start < n && blank(out[start], flags))
		start++;
	while (n > start && blank(out[n - 1], flags))
		n--;
	memmove(out, out + start, n - start);
	n -= start;
	out[n] = 0;
	return n;
}

// --- The keys ------------------------------------------------------------------

static const u8 func_spans[2][RENAME_FUNCS] = {
	{2, 2, 2, 0, 1, 2, 1},  // a name's: no Enter
	{2, 1, 2, 1, 1, 2, 1},  // a text of lines'
};
const u8 *rename_func_span = func_spans[0];

// Latin-1, ten a row: every Latin-1 letter the menu's fonts draw (but the
// multiplication and division signs), then symbols. A file name's take only
// those FAT takes in names; another name's add : ? / on the Latin-1 page's
// upper case; a text of lines' (prose) put , . ? ' on the letters' page and
// the rest of a sentence's marks on the Latin-1 page.
typedef char key_rows[RENAME_PAGES][2][RENAME_CHAR_ROWS][RENAME_COLS + 1];
#define LATIN_LOWER \
	"\xE0\xE1\xE2\xE3\xE4\xE5\xE6\xE7\xE8\xE9", "\xEA\xEB\xEC\xED\xEE\xEF\xF0\xF1\xF2\xF3", \
		"\xF4\xF5\xF6\xF8\xF9\xFA\xFB\xFC\xFD\xFE"
#define LATIN_UPPER \
	"\xC0\xC1\xC2\xC3\xC4\xC5\xC6\xC7\xC8\xC9", "\xCA\xCB\xCC\xCD\xCE\xCF\xD0\xD1\xD2\xD3", \
		"\xD4\xD5\xD6\xD8\xD9\xDA\xDB\xDC\xDD\xDE"
static const key_rows key_sets[3] = {
	{
		// A file name's
		{
			{"1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm._'"},
			{"1234567890", "QWERTYUIOP", "ASDFGHJKL-", "ZXCVBNM._'"},
		},
		{{LATIN_LOWER, "\xDF\xFF!&()+,;@"}, {LATIN_UPPER, "#$%=[]^~{}"}},
	},
	{
		// Another name's (a game's, a cheat's)
		{
			{"1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm._'"},
			{"1234567890", "QWERTYUIOP", "ASDFGHJKL-", "ZXCVBNM._'"},
		},
		{{LATIN_LOWER, "\xDF\xFF!&()+,;@"}, {LATIN_UPPER, "#$%=:?/[]~"}},
	},
	{
		// A text of lines'
		{
			{"1234567890", "qwertyuiop", "asdfghjkl'", "zxcvbnm,.?"},
			{"1234567890", "QWERTYUIOP", "ASDFGHJKL'", "ZXCVBNM,.?"},
		},
		{{LATIN_LOWER, "\xDF\xFF!:;-/()&"}, {LATIN_UPPER, "#$%=+@_[]~"}},
	},
};
static const key_rows *keys = &key_sets[0];

void rename_layout(u32 flags)
{
	rename_func_span = func_spans[(flags & RENAME_LINES) != 0];
	keys = &key_sets[flags & RENAME_LINES ? 2 : flags & RENAME_FILE ? 0 : 1];
}

u32 rename_key(u32 page, int upper, u32 row, u32 col)
{
	if (page >= RENAME_PAGES || row >= RENAME_CHAR_ROWS || col >= RENAME_COLS)
		return 0;
	return (u8)(*keys)[page][upper != 0][row][col];
}

int rename_is_letter(u32 c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xFF && c != 0xD7 && c != 0xF7);
}

u32 rename_func_at(u32 col)
{
	u32 c = 0;
	for (u32 k = 0; k < RENAME_FUNCS; k++) {
		c += rename_func_span[k];
		if (col < c)
			return k;
	}
	return RENAME_FUNCS - 1;
}

u32 rename_func_col(u32 k)
{
	u32 c = 0;
	for (u32 i = 0; i < k && i < RENAME_FUNCS; i++)
		c += rename_func_span[i];
	return c;
}

int rename_sel_move(struct rename_sel *sel, u16 key, int fresh)
{
	u32 row = sel->row, col = sel->col;
	if (key & (KEY_UP | KEY_DOWN)) {
		if (key & KEY_UP)
			row = row ? row - 1 : fresh ? RENAME_ROWS - 1 : 0;
		else
			row = row + 1 < RENAME_ROWS ? row + 1 : fresh ? 0 : row;
	} else if (key & (KEY_LEFT | KEY_RIGHT)) {
		s32 dir = key & KEY_LEFT ? -1 : 1;
		if (row == RENAME_FUNC_ROW) {
			// From key to key, to its first column
			u32 k = rename_func_at(col), last = RENAME_FUNCS - 1, was = k;
			do {
				if (dir < 0)
					k = k ? k - 1 : fresh ? last : k;
				else
					k = k < last ? k + 1 : fresh ? 0 : k;
			} while (!rename_func_span[k] && k != was);  // past a key the layout lacks
			col = rename_func_col(k);
		} else if (dir < 0) {
			col = col ? col - 1 : fresh ? RENAME_COLS - 1 : 0;
		} else {
			col = col + 1 < RENAME_COLS ? col + 1 : fresh ? 0 : col;
		}
	}
	int moved = row != sel->row ||
		    (row == RENAME_FUNC_ROW ? rename_func_at(col) != rename_func_at(sel->col) : col != sel->col);
	sel->row = (u8)row;
	sel->col = (u8)col;
	return moved;
}
