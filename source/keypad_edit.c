// The code keypad's model (keypad_edit.h).
#include "keypad_edit.h"

#include <string.h>

int keypad_is_mark(char c)
{
	return c == '?' || c == 'X' || c == 'x' || c == 'Y' || c == 'y' || c == 'Z' || c == 'z';
}

static int is_hex(char c)
{
	return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

static int is_sep(char c)
{
	return c == '+' || c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// AnyGBA's characters in a raw line ('+' within one is a pointer's offset's)
static int is_raw(char c)
{
	return c == '[' || c == ']' || c == '^' || c == '|';
}

// 1 if line i holds more than digits: VBA's ':' or AnyGBA's characters
static int is_raw_line(const struct keypad_edit *e, u32 i)
{
	for (u32 c = 0; c < e->len[i]; c++)
		if (e->text[i][c] == ':' || is_raw(e->text[i][c]))
			return 1;
	return 0;
}

// A part's characters, hex digits and stand-ins (':' not counted)
static int all_digits(const char *p, u32 n)
{
	for (u32 i = 0; i < n; i++)
		if (!is_hex(p[i]) && !keypad_is_mark(p[i]))
			return 0;
	return 1;
}

static char upper(char c)
{
	return c >= 'a' && c <= 'z' ? (char)(c - 32) : c;
}

// Puts n characters of p at the end of e's line i, in upper case
static void append(struct keypad_edit *e, u32 i, const char *p, u32 n)
{
	for (u32 k = 0; k < n; k++)
		e->text[i][e->len[i]++] = upper(p[k]);
	e->text[i][e->len[i]] = 0;
}

// The caret's line's first stand-in from column col on, else its end; 1 if
// on one
static int caret_to_mark(struct keypad_edit *e, u32 col)
{
	u32 l = e->line;
	for (u32 c = col; c < e->len[l]; c++)
		if (keypad_is_mark(e->text[l][c])) {
			e->caret = (u8)c;
			return 1;
		}
	e->caret = e->len[l];
	return 0;
}

int keypad_edit_key(char c)
{
	return is_hex(c) || c == ':' || c == '+' || is_raw(c);
}

int keypad_edit_typable(const char *code, u32 len)
{
	for (u32 i = 0; i < len; i++)
		if (!is_sep(code[i]) && !keypad_edit_key(code[i]) && !keypad_is_mark(code[i]))
			return 0;
	return 1;
}

int keypad_edit_init(struct keypad_edit *e, const char *code, u32 len)
{
	memset(e, 0, sizeof(*e));
	e->lines = 1;
	u32 pos = 0;
	while (pos < len) {
		while (pos < len && is_sep(code[pos]))
			pos++;
		if (pos == len)
			break;
		u32 start = pos;
		// To the line's end: a '+' after a pointer's ']' is its own
		while (pos < len && (!is_sep(code[pos]) || cheat_plus_joins(code + start, len - start, pos - start)))
			pos++;
		u32 n = pos - start;
		// An address's 8 digits and a value of 4 or 8 after them: one line
		u32 next = pos, vn = 0;
		while (next < len && is_sep(code[next]))
			next++;
		while (next + vn < len && !is_sep(code[next + vn]))
			vn++;
		int pair = n == 8 && all_digits(code + start, 8) && (vn == 4 || vn == 8) && all_digits(code + next, vn);
		u32 total = n + (pair ? vn : 0);
		u32 line = e->len[0] ? e->lines : 0;
		int typable = 1;
		for (u32 i = start; i < pos; i++)
			if (!keypad_edit_key(code[i]) && !keypad_is_mark(code[i]))
				typable = 0;
		if (!typable || total > KEYPAD_LINE_MAX || line >= KEYPAD_LINES) {
			memset(e, 0, sizeof(*e));
			e->lines = 1;
			return 0;
		}
		e->lines = (u8)(line + 1);
		append(e, line, code + start, n);
		if (pair) {
			append(e, line, code + next, vn);
			pos = next + vn;
		}
	}
	// On the first stand-in, else at the end
	for (u32 i = 0; i < e->lines; i++) {
		e->line = (u8)i;
		if (caret_to_mark(e, 0))
			return 1;
	}
	return 1;
}

int keypad_edit_type(struct keypad_edit *e, char c)
{
	if (!keypad_edit_key(c))
		return 0;
	c = upper(c);
	u32 l = e->line;
	char *t = e->text[l];
	if (c == '+' && (!e->caret || t[e->caret - 1] != ']'))
		return 0;  // a '+' in a line is a pointer's offset's; Line starts another
	if (is_hex(c) && e->caret < e->len[l] && keypad_is_mark(t[e->caret])) {
		t[e->caret] = c;
		// On to the next stand-in, on a later line too
		if (caret_to_mark(e, e->caret + 1u))
			return 1;
		for (u32 i = l + 1; i < e->lines; i++) {
			e->line = (u8)i;
			if (caret_to_mark(e, 0))
				return 1;
		}
		e->line = (u8)l;
		caret_to_mark(e, e->caret);
		return 1;
	}
	if (e->len[l] >= KEYPAD_LINE_MAX)
		return 0;
	memmove(t + e->caret + 1, t + e->caret, e->len[l] - e->caret + 1u);  // the NUL too
	t[e->caret++] = c;
	e->len[l]++;
	return 1;
}

int keypad_edit_delete(struct keypad_edit *e)
{
	u32 l = e->line;
	char *t = e->text[l];
	if (e->caret) {
		memmove(t + e->caret - 1, t + e->caret, e->len[l] - e->caret + 1u);
		e->caret--;
		e->len[l]--;
		return 1;
	}
	if (!l || e->len[l - 1] + e->len[l] > KEYPAD_LINE_MAX)
		return 0;
	// Joined to the line before; the lines after move up
	u32 at = e->len[l - 1];
	append(e, l - 1, t, e->len[l]);
	for (u32 i = l; i + 1 < e->lines; i++) {
		memcpy(e->text[i], e->text[i + 1], sizeof(e->text[i]));
		e->len[i] = e->len[i + 1];
	}
	e->lines--;
	e->text[e->lines][0] = 0;
	e->len[e->lines] = 0;
	e->line = (u8)(l - 1);
	e->caret = (u8)at;
	return 1;
}

int keypad_edit_break(struct keypad_edit *e)
{
	u32 l = e->line;
	if (e->lines >= KEYPAD_LINES || !e->len[l])
		return 0;
	for (u32 i = e->lines; i > l + 1; i--) {
		memcpy(e->text[i], e->text[i - 1], sizeof(e->text[i]));
		e->len[i] = e->len[i - 1];
	}
	e->lines++;
	u32 rest = e->len[l] - e->caret;
	memcpy(e->text[l + 1], e->text[l] + e->caret, rest + 1u);
	e->len[l + 1] = (u8)rest;
	e->text[l][e->caret] = 0;
	e->len[l] = e->caret;
	e->line = (u8)(l + 1);
	e->caret = 0;
	return 1;
}

int keypad_edit_line(struct keypad_edit *e, s32 dir)
{
	s32 to = (s32)e->line + dir;
	if (to < 0 || to >= (s32)e->lines)
		return 0;
	e->line = (u8)to;
	caret_to_mark(e, 0);
	return 1;
}

u32 keypad_edit_marks(const struct keypad_edit *e)
{
	u32 n = 0;
	for (u32 i = 0; i < e->lines; i++)
		for (u32 c = 0; c < e->len[i]; c++)
			n += (u32)keypad_is_mark(e->text[i][c]);
	return n;
}

u32 keypad_edit_digits(const struct keypad_edit *e, u32 i, u32 format, u32 *expect)
{
	*expect = 0;
	if (i >= e->lines)
		return 0;
	u32 n = e->len[i];
	if (is_raw_line(e, i) || format == CHEAT_FMT_VBA)
		return n;
	if (format == CHEAT_FMT_CODEBREAKER)
		*expect = 12;
	else if (format != CHEAT_FMT_AUTO)
		*expect = 16;
	else
		*expect = n <= 12 ? 12 : 16;
	return n;
}

// 1 if line i is a CodeBreaker or GameShark line typed whole: written in
// two parts
static int two_parts(const struct keypad_edit *e, u32 i)
{
	return (e->len[i] == 12 || e->len[i] == 16) && all_digits(e->text[i], e->len[i]);
}

u32 keypad_edit_code(const struct keypad_edit *e, char *out, u32 size)
{
	u32 n = 0;
	if (!size)
		return 0;
	for (u32 i = 0; i < e->lines; i++) {
		if (!e->len[i])
			continue;
		// A key's pointer ending the line, a line after it: "+0", or the
		// '+' between them would be its offset's
		int more = 0;
		for (u32 k = i + 1; k < e->lines; k++)
			more |= e->len[k] != 0;
		int zero = more && e->text[i][e->len[i] - 1] == ']' && memchr(e->text[i], '^', e->len[i]);
		u32 need = e->len[i] + (n ? 1u : 0) + (two_parts(e, i) ? 1u : 0) + (zero ? 2u : 0);
		if (n + need + 1 > size)
			break;
		if (n)
			out[n++] = '+';
		for (u32 c = 0; c < e->len[i]; c++) {
			if (c == 8 && two_parts(e, i))
				out[n++] = ' ';
			out[n++] = e->text[i][c];
		}
		if (zero) {
			out[n++] = '+';
			out[n++] = '0';
		}
	}
	out[n] = 0;
	return n;
}

u32 keypad_edit_show(const struct keypad_edit *e, u32 i, char *out, u32 *caret)
{
	u32 n = 0;
	int spaced = !is_raw_line(e, i);
	for (u32 c = 0; c <= e->len[i]; c++) {
		if (c == 8 && spaced && e->len[i] > 8)
			out[n++] = ' ';
		if (caret && i == e->line && c == e->caret)
			*caret = n;
		if (c < e->len[i])
			out[n++] = e->text[i][c];
	}
	out[n] = 0;
	return n;
}

void keypad_check(const struct keypad_edit *e, u8 format, const struct cheat_decoder *ctx, struct cheatfile_work *w,
		  char *scratch, struct keypad_check *out)
{
	memset(out, 0, sizeof(*out));
	out->format = format;
	u32 len = keypad_edit_code(e, scratch, KEYPAD_CODE_MAX + 1);
	if (!len) {
		out->status = KEYPAD_EMPTY;
		return;
	}
	// Every line a code line's shape first; the first's tells the format
	int first = 1;
	for (u32 i = 0; i < e->lines; i++) {
		if (!e->len[i])
			continue;
		s32 score = 0;
		u8 shape = cheat_detect(e->text[i], e->len[i], &score);
		if (first) {
			out->score = score;
			if (format == CHEAT_FMT_AUTO)
				out->format = shape;
			first = 0;
		}
		if (shape == CHEAT_FMT_AUTO) {
			out->status = KEYPAD_SHAPE;
			out->line = (u8)i;
			return;
		}
	}
	struct cheat_decoder dec;
	if (format != CHEAT_FMT_AUTO)
		cheat_decoder_force(&dec, format);
	else if (ctx)
		dec = *ctx;
	else
		cheat_decoder_init(&dec);
	u8 lowered;
	u8 status = cheatfile_decode(w, &dec, scratch, len, &out->ops, &lowered);
	if (format == CHEAT_FMT_AUTO && dec.last != CHEAT_FMT_AUTO)
		out->format = dec.last;  // as it decoded: a GameShark format by the file's seeds, say
	switch (status) {
	case CHEATFILE_OK:
		out->status = KEYPAD_OK;
		break;
	case CHEATFILE_PLACEHOLDER:
		out->status = KEYPAD_FILL;
		break;
	case CHEATFILE_MASTER:
		out->status = KEYPAD_MASTER;
		break;
	case CHEATFILE_BIG:
		out->status = KEYPAD_BIG;
		break;
	case CHEATFILE_CANT_RUN:
		out->status = KEYPAD_CANT_RUN;
		break;
	default:
		out->status = lowered == CHEAT_LOWER_BAD_ADDRESS ? KEYPAD_ADDRESS : KEYPAD_WRONG;
		break;
	}
}
