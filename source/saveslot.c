#include "saveslot.h"

#include <string.h>

#include "crc32.h"

/* ---- UTF-8 ---- */

// Reads the character at s[*pos] (len bytes in all) and moves *pos past it.
// Returns the code point, or -1 for a broken, overlong, surrogate or
// too-large sequence, skipping one byte.
static long next_char(const u8 *s, u32 len, u32 *pos)
{
	u32 i = *pos;
	u32 c = s[i];
	u32 need, min;
	if (c < 0x80) {
		*pos = i + 1;
		return (long)c;
	} else if (c >= 0xC2 && c < 0xE0) {
		need = 1, min = 0x80, c &= 0x1F;
	} else if (c >= 0xE0 && c < 0xF0) {
		need = 2, min = 0x800, c &= 0x0F;
	} else if (c >= 0xF0 && c < 0xF5) {
		need = 3, min = 0x10000, c &= 0x07;
	} else {
		*pos = i + 1;
		return -1;
	}
	if (len - i <= need) {
		*pos = i + 1;
		return -1;
	}
	for (u32 k = 1; k <= need; k++) {
		if ((s[i + k] & 0xC0) != 0x80) {
			*pos = i + 1;
			return -1;
		}
		c = (c << 6) | (s[i + k] & 0x3F);
	}
	if (c < min || c > 0x10FFFF || (c >= 0xD800 && c < 0xE000)) {
		*pos = i + 1;
		return -1;
	}
	*pos = i + need + 1;
	return (long)c;
}

static void put_char(char *out, u32 *n, u32 c)
{
	if (c < 0x80) {
		out[(*n)++] = (char)c;
	} else if (c < 0x800) {
		out[(*n)++] = (char)(0xC0 | (c >> 6));
		out[(*n)++] = (char)(0x80 | (c & 0x3F));
	} else if (c < 0x10000) {
		out[(*n)++] = (char)(0xE0 | (c >> 12));
		out[(*n)++] = (char)(0x80 | ((c >> 6) & 0x3F));
		out[(*n)++] = (char)(0x80 | (c & 0x3F));
	} else {
		out[(*n)++] = (char)(0xF0 | (c >> 18));
		out[(*n)++] = (char)(0x80 | ((c >> 12) & 0x3F));
		out[(*n)++] = (char)(0x80 | ((c >> 6) & 0x3F));
		out[(*n)++] = (char)(0x80 | (c & 0x3F));
	}
}

/* ---- Slot names ---- */

static int forbidden(u32 c)
{
	if (c < 0x20 || c == 0x7F || (c >= 0x80 && c < 0xA0))
		return 1;
	switch (c) {
	case '\\':
	case '/':
	case ':':
	case '*':
	case '?':
	case '"':
	case '<':
	case '>':
	case '|':
		return 1;
	default:
		return 0;
	}
}

u32 saveslot_clean_name(char *out, const char *name)
{
	const u8 *s = (const u8 *)name;
	u32 len = (u32)strlen(name);
	u32 n = 0, chars = 0;
	// The bytes of out up to keep_n end in a character that is not a space or dot
	u32 keep_n = 0;
	u32 pos = 0;
	while (pos < len && chars < SAVESLOT_NAME_CHARS) {
		long c = next_char(s, len, &pos);
		if (c < 0 || forbidden((u32)c))
			continue;
		if ((c == ' ' || c == '.') && n == 0)
			continue;
		put_char(out, &n, (u32)c);
		chars++;
		if (c != ' ' && c != '.') {
			keep_n = n;
		}
	}
	out[keep_n] = 0;
	return keep_n;
}

// The code point as compared: ASCII and Latin-1 capitals as small letters
static u32 fold(u32 c)
{
	if (c >= 'A' && c <= 'Z')
		return c + 0x20;
	if (c >= 0xC0 && c <= 0xDE && c != 0xD7)
		return c + 0x20;
	return c;
}

int saveslot_names_equal(const char *a, const char *b)
{
	const u8 *sa = (const u8 *)a, *sb = (const u8 *)b;
	u32 la = (u32)strlen(a), lb = (u32)strlen(b);
	u32 pa = 0, pb = 0;
	while (pa < la && pb < lb) {
		long ca = next_char(sa, la, &pa), cb = next_char(sb, lb, &pb);
		// A broken byte matches only itself
		if (ca < 0 || cb < 0) {
			if (ca != cb || sa[pa - 1] != sb[pb - 1])
				return 0;
			continue;
		}
		if (fold((u32)ca) != fold((u32)cb))
			return 0;
	}
	return pa == la && pb == lb;
}

int saveslot_name_taken(const char *name, const char *const *names, u32 count, u32 skip)
{
	for (u32 i = 0; i < count; i++)
		if (i != skip && saveslot_names_equal(name, names[i]))
			return 1;
	return 0;
}

/* ---- Paths ---- */

// Appends text to out at *len, keeping the NUL; returns 0 when it does not fit
static int add(char *out, u32 size, u32 *len, const char *text, u32 n)
{
	if (*len + n >= size)
		return 0;
	memcpy(out + *len, text, n);
	*len += n;
	out[*len] = 0;
	return 1;
}

static int add_str(char *out, u32 size, u32 *len, const char *text)
{
	return add(out, size, len, text, (u32)strlen(text));
}

// 1 if path is non-empty and has no empty or "." / ".." part. Leading '/' optional
static int parts_ok(const char *path)
{
	const char *p = path;
	if (*p == 0)
		return 0;
	for (;;) {
		const char *end = strchr(p, '/');
		u32 n = end ? (u32)(end - p) : (u32)strlen(p);
		if (n == 0 || (n == 1 && p[0] == '.') || (n == 2 && p[0] == '.' && p[1] == '.'))
			return 0;
		if (!end)
			return 1;
		p = end + 1;
	}
}

static int rom_ok(const char *rom_path)
{
	return rom_path[0] == '/' && parts_ok(rom_path + 1);
}

static int slot_ok(const char *slot)
{
	return parts_ok(slot) && strchr(slot, '/') == NULL;
}

static int finish(char *out, int ok, u32 len)
{
	if (!ok) {
		out[0] = 0;
		return -1;
	}
	return (int)len;
}

int saveslot_save_path(char *out, u32 size, int mode, const char *rom_path, const char *slot)
{
	u32 len = 0;
	if (size == 0)
		return -1;
	out[0] = 0;
	if (!rom_ok(rom_path) || !slot_ok(slot))
		return -1;
	int ok;
	if (mode == SAVESLOT_BESIDE_ROM) {
		const char *name = strrchr(rom_path, '/') + 1;
		const char *dot = strrchr(name, '.');
		u32 stem = dot && dot != name ? (u32)(dot - name) : (u32)strlen(name);
		ok = add(out, size, &len, rom_path, (u32)(name - rom_path)) && add(out, size, &len, name, stem) &&
		     add_str(out, size, &len, ".") && add_str(out, size, &len, slot) && add_str(out, size, &len, ".sav");
	} else {
		ok = add_str(out, size, &len, SAVESLOT_SAVES_DIR) && add_str(out, size, &len, rom_path) &&
		     add_str(out, size, &len, "/") && add_str(out, size, &len, slot) && add_str(out, size, &len, ".sav");
	}
	return finish(out, ok, len);
}

int saveslot_backup_dir(char *out, u32 size, const char *rom_path, const char *slot)
{
	u32 len = 0;
	if (size == 0)
		return -1;
	out[0] = 0;
	if (!rom_ok(rom_path) || !slot_ok(slot))
		return -1;
	int ok = add_str(out, size, &len, SAVESLOT_BACKUPS_DIR) && add_str(out, size, &len, rom_path) &&
		 add_str(out, size, &len, "/") && add_str(out, size, &len, slot);
	return finish(out, ok, len);
}

int saveslot_game_dir(char *out, u32 size, const char *rom_path)
{
	u32 len = 0;
	if (size == 0)
		return -1;
	out[0] = 0;
	if (!rom_ok(rom_path))
		return -1;
	int ok = add_str(out, size, &len, SAVESLOT_SAVES_DIR) && add_str(out, size, &len, rom_path);
	return finish(out, ok, len);
}

static u32 put_digits(char *out, u32 value, u32 width)
{
	char tmp[10];
	u32 n = 0;
	do {
		tmp[n++] = (char)('0' + value % 10);
		value /= 10;
	} while (value);
	u32 total = n > width ? n : width;
	for (u32 i = 0; i < total; i++)
		out[i] = i < total - n ? '0' : tmp[total - 1 - i];
	return total;
}

static u32 put_stamp(char *out, const struct saveslot_stamp *t)
{
	u32 n = put_digits(out, t->year, 4);
	n += put_digits(out + n, t->month, 2);
	n += put_digits(out + n, t->day, 2);
	out[n++] = '-';
	n += put_digits(out + n, t->hour, 2);
	n += put_digits(out + n, t->min, 2);
	n += put_digits(out + n, t->sec, 2);
	out[n] = 0;
	return n;
}

int saveslot_trash_path(char *out, u32 size, const char *rom_path, const char *slot, const struct saveslot_stamp *when,
			u32 n)
{
	u32 len = 0;
	char part[SAVESLOT_STAMP_BYTES + 12];
	if (size == 0)
		return -1;
	out[0] = 0;
	if (!rom_ok(rom_path) || !slot_ok(slot) || n == 0 || n > SAVESLOT_COUNTER_MAX)
		return -1;
	u32 k = put_stamp(part, when);
	part[k++] = '-';
	k += put_digits(part + k, n, 1);
	part[k] = 0;
	int ok = add_str(out, size, &len, SAVESLOT_TRASH_DIR "/") && add_str(out, size, &len, part) &&
		 add_str(out, size, &len, rom_path) && add_str(out, size, &len, "/") && add_str(out, size, &len, slot) &&
		 add_str(out, size, &len, ".sav");
	return finish(out, ok, len);
}

u32 saveslot_save_path_len(u32 rom_len, u32 slot_len)
{
	// "/torii/saves" + rom path + "/" + slot + ".sav"
	return (u32)strlen(SAVESLOT_SAVES_DIR) + rom_len + 1 + slot_len + 4;
}

u32 saveslot_longest_save_path(u32 rom_len)
{
	return saveslot_save_path_len(rom_len, SAVESLOT_NAME_BYTES - 1);
}

u32 saveslot_max_rom_path_for(u32 limit, u32 slot_len)
{
	u32 fixed = saveslot_save_path_len(0, slot_len);
	return limit > fixed ? limit - fixed : 0;
}

/* ---- History ---- */

// Reads exactly n digits; -1 if one is not
static long digits(const char *s, u32 n)
{
	long v = 0;
	for (u32 i = 0; i < n; i++) {
		if (s[i] < '0' || s[i] > '9')
			return -1;
		v = v * 10 + (s[i] - '0');
	}
	return v;
}

// Reads "YYYYMMDD-HHMMSS" at s; 0 if it is not one. All zero is the unknown time
static int parse_stamp(const char *s, struct saveslot_stamp *t)
{
	long y = digits(s, 4), mo = digits(s + 4, 2), d = digits(s + 6, 2);
	long h = digits(s + 9, 2), mi = digits(s + 11, 2), se = digits(s + 13, 2);
	if (y < 0 || mo < 0 || d < 0 || h < 0 || mi < 0 || se < 0 || s[8] != '-')
		return 0;
	int unknown = y == 0 && mo == 0 && d == 0 && h == 0 && mi == 0 && se == 0;
	if (!unknown && (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || se > 59))
		return 0;
	t->year = (u16)y;
	t->month = (u8)mo;
	t->day = (u8)d;
	t->hour = (u8)h;
	t->min = (u8)mi;
	t->sec = (u8)se;
	return 1;
}

u32 saveslot_backup_name(char *out, u32 n, const struct saveslot_stamp *when)
{
	if (n == 0 || n > SAVESLOT_COUNTER_MAX)
		return 0;
	u32 k = put_digits(out, n, 4);
	out[k++] = '-';
	k += put_stamp(out + k, when);
	memcpy(out + k, ".sav", 5);
	return k + 4;
}

int saveslot_parse_backup_name(const char *name, u32 *n, struct saveslot_stamp *when)
{
	u32 d = 0;
	u32 v = 0;
	while (name[d] >= '0' && name[d] <= '9' && d < 10) {
		v = v * 10 + (u32)(name[d] - '0');
		d++;
	}
	if (d < 4 || d > 9 || v == 0 || name[d] != '-')
		return 0;
	const char *rest = name + d + 1;
	if (strlen(rest) != 15 + 4 || strcmp(rest + 15, ".sav") != 0)
		return 0;
	struct saveslot_stamp t;
	if (!parse_stamp(rest, &t))
		return 0;
	if (n)
		*n = v;
	if (when)
		*when = t;
	return 1;
}

u32 saveslot_next_counter(const char *const *names, u32 count)
{
	u32 top = 0;
	for (u32 i = 0; i < count; i++) {
		u32 n;
		if (saveslot_parse_backup_name(names[i], &n, NULL) && n > top)
			top = n;
	}
	return top >= SAVESLOT_COUNTER_MAX ? 0 : top + 1;
}

// 1 if backup a is older than b: by counter, then by name
static int older(const char *a, const char *b)
{
	u32 na = 0, nb = 0;
	saveslot_parse_backup_name(a, &na, NULL);
	saveslot_parse_backup_name(b, &nb, NULL);
	return na != nb ? na < nb : strcmp(a, b) < 0;
}

u32 saveslot_prune(const char *const *names, u32 count, u32 keep, u32 *out)
{
	// out takes the valid names oldest first (insertion sort); the first
	// valid - keep of them are the result
	u32 valid = 0;
	for (u32 i = 0; i < count; i++) {
		if (!saveslot_parse_backup_name(names[i], NULL, NULL))
			continue;
		u32 j = valid++;
		while (j > 0 && older(names[i], names[out[j - 1]])) {
			out[j] = out[j - 1];
			j--;
		}
		out[j] = i;
	}
	return valid > keep ? valid - keep : 0;
}

int saveslot_trash_expired(const struct saveslot_stamp *then, const struct saveslot_stamp *now)
{
	if (then->year == 0 || now->year == 0)
		return 0;
	// Days from a calendar date (Howard Hinnant's days_from_civil)
	long days[2];
	const struct saveslot_stamp *t[2] = {then, now};
	for (int i = 0; i < 2; i++) {
		long y = t[i]->year, m = t[i]->month;
		y -= m <= 2;
		long era = y / 400;
		long yoe = y - era * 400;
		long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + t[i]->day - 1;
		long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
		days[i] = era * 146097 + doe;
	}
	long old = days[0] * 86400L + t[0]->hour * 3600L + t[0]->min * 60L + t[0]->sec;
	long cur = days[1] * 86400L + t[1]->hour * 3600L + t[1]->min * 60L + t[1]->sec;
	return cur - old > (long)SAVESLOT_TRASH_DAYS * 86400L;
}

/* ---- Rename journal ---- */

#define JOURNAL_HEADER "torii-rename 1\n"

static u32 journal_crc(const char *old_name, const char *new_name)
{
	u32 crc = crc32(old_name, (u32)strlen(old_name) + 1);
	return crc32_update(crc, new_name, (u32)strlen(new_name) + 1);
}

static int is_clean(const char *name)
{
	char tmp[SAVESLOT_NAME_BYTES];
	if (strlen(name) >= SAVESLOT_NAME_BYTES)
		return 0;
	u32 n = saveslot_clean_name(tmp, name);
	return n != 0 && strcmp(tmp, name) == 0;
}

u32 saveslot_journal_write(char *out, u32 size, const char *old_name, const char *new_name)
{
	if (!is_clean(old_name) || !is_clean(new_name))
		return 0;
	u32 len = 0;
	char hex[9];
	u32 crc = journal_crc(old_name, new_name);
	for (int i = 0; i < 8; i++)
		hex[i] = "0123456789abcdef"[(crc >> (28 - 4 * i)) & 15];
	hex[8] = 0;
	// The room check leaves the NUL for add()
	if (!(add_str(out, size, &len, JOURNAL_HEADER) && add_str(out, size, &len, old_name) &&
	      add_str(out, size, &len, "\n") && add_str(out, size, &len, new_name) && add_str(out, size, &len, "\n") &&
	      add_str(out, size, &len, hex) && add_str(out, size, &len, "\n"))) {
		if (size)
			out[0] = 0;
		return 0;
	}
	return len;
}

// The line at *pos (up to len) into out (max bytes with the NUL); 0 if it has no
// newline or is too long. Moves *pos past the newline
static int read_line(const char *data, u32 len, u32 *pos, char *out, u32 max)
{
	u32 i = *pos, n = 0;
	while (i < len && data[i] != '\n') {
		if (n + 1 >= max)
			return 0;
		out[n++] = data[i++];
	}
	if (i >= len)
		return 0;
	out[n] = 0;
	*pos = i + 1;
	return 1;
}

int saveslot_journal_read(const char *data, u32 len, char *old_name, char *new_name)
{
	char line[SAVESLOT_NAME_BYTES] = "";
	char o[SAVESLOT_NAME_BYTES] = "", n[SAVESLOT_NAME_BYTES] = "";
	u32 pos = 0;
	if (!read_line(data, len, &pos, line, sizeof(line)) || strcmp(line, "torii-rename 1") != 0)
		return 0;
	if (!read_line(data, len, &pos, o, sizeof(o)) || !read_line(data, len, &pos, n, sizeof(n)))
		return 0;
	if (!read_line(data, len, &pos, line, sizeof(line)) || pos != len)
		return 0;
	if (!is_clean(o) || !is_clean(n))
		return 0;
	u32 crc = 0;
	if (strlen(line) != 8)
		return 0;
	for (int i = 0; i < 8; i++) {
		char c = line[i];
		u32 v = c >= '0' && c <= '9' ? (u32)(c - '0') : c >= 'a' && c <= 'f' ? (u32)(c - 'a' + 10) : 16;
		if (v == 16)
			return 0;
		crc = (crc << 4) | v;
	}
	if (crc != journal_crc(o, n))
		return 0;
	memcpy(old_name, o, strlen(o) + 1);
	memcpy(new_name, n, strlen(n) + 1);
	return 1;
}

enum saveslot_recovery saveslot_recover(u32 exists)
{
	int old_save = (exists & SAVESLOT_OLD_SAVE) != 0, new_save = (exists & SAVESLOT_NEW_SAVE) != 0;
	int old_hist = (exists & SAVESLOT_OLD_HIST) != 0, new_hist = (exists & SAVESLOT_NEW_HIST) != 0;
	// Only a save under the new name alone shows the first rename done
	if (old_save || !new_save || !old_hist)
		return SAVESLOT_RECOVER_NOTHING;
	return new_hist ? SAVESLOT_RECOVER_UNDO : SAVESLOT_RECOVER_FINISH;
}

/* ---- Sanity checks ---- */

u32 saveslot_blank_scan(u32 state, const void *data, u32 size)
{
	const u8 *p = (const u8 *)data;
	for (u32 i = 0; i < size && state; i++) {
		if (p[i] != 0xFF)
			state &= ~1u;
		if (p[i] != 0x00)
			state &= ~2u;
	}
	return state;
}

int saveslot_is_blank(const void *data, u32 size)
{
	return saveslot_blank_scan(SAVESLOT_BLANK_START, data, size) != 0;
}

int saveslot_identical(const void *a, const void *b, u32 size)
{
	return memcmp(a, b, size) == 0;
}
