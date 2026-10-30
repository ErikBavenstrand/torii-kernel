// See lang.h.
#include <string.h>

#include "lang.h"
#include "crc32.h"

// The strings in use: a pack's offsets and pool, for ids below count; the
// rest, and any a pack lacks, English's
static struct {
	const u16 *offsets;
	const char *pool;
	u32 count;  // 0: English alone
	u32 launch;  // 1 while only the launch's strings are kept
	u32 langs;  // lang_list's entries past English
	struct lang_choice list[LANG_LIST_MAX];
} lang EWRAM_BSS;

const char *S(u32 id)
{
	if (id >= STR_COUNT)
		return "";
	if (id < lang.count) {
		u16 at = lang.offsets[id];
		if (at)
			return lang.pool + at;
	}
	return lang_en[id];
}

static u32 rd16(const u8 *p)
{
	return p[0] | (u32)p[1] << 8;
}

static u32 rd32(const u8 *p)
{
	return rd16(p) | rd16(p + 2) << 16;
}

const char *const lang_pack_check_names[LANG_PACK_CHECKS] = {
	"ok", "too short", "not a pack", "for another Torii (stale)", "damaged (CRC)", "an offset past its strings",
};

static u32 check(const u8 *pack, u32 bytes, int header_only)
{
	if (bytes < LANG_HEADER)
		return LANG_PACK_SHORT;
	if (memcmp(pack, "TLNG", 4) != 0)
		return LANG_PACK_MAGIC;
	u32 count = rd16(pack + 6), size = rd32(pack + 12), pool = rd32(pack + 60);
	if (rd16(pack + 4) != LANG_FORMAT || count != STR_COUNT || rd32(pack + 8) != LANG_KEY_HASH)
		return LANG_PACK_STALE;
	if (size > LANG_PACK_MAX || pool != LANG_HEADER + 2 * count || size <= pool)
		return LANG_PACK_BAD;
	if (bytes == LANG_HEADER && header_only)
		return LANG_PACK_OK;
	if (bytes != size)
		return LANG_PACK_SHORT;
	if (crc32(pack + 20, size - 20) != rd32(pack + 16))
		return LANG_PACK_CRC;
	// Each string ends inside the pool: the pool's last byte past its
	// strings is a NUL (a pad byte after it may follow)
	u32 pool_bytes = size - pool;
	const u8 *strings = pack + pool;
	if (strings[0] != 0)
		return LANG_PACK_BAD;
	u32 last = pool_bytes - 1;
	if (strings[last] != 0)
		return LANG_PACK_BAD;
	for (u32 i = 0; i < count; i++)
		if (rd16(pack + LANG_HEADER + 2 * i) >= pool_bytes)
			return LANG_PACK_BAD;
	return LANG_PACK_OK;
}

u32 lang_pack_check(const u8 *pack, u32 bytes)
{
	return check(pack, bytes, 0);
}

u32 lang_pack_check_header(const u8 *pack)
{
	return check(pack, LANG_HEADER, 1);
}

static void copy_field(char *out, const u8 *in, u32 bytes)
{
	memcpy(out, in, bytes);
	out[bytes - 1] = 0;
}

void lang_pack_code(const u8 *pack, char out[LANG_CODE_BYTES])
{
	copy_field(out, pack + 20, LANG_CODE_BYTES);
}

void lang_pack_name(const u8 *pack, char out[LANG_NAME_BYTES])
{
	copy_field(out, pack + 20 + LANG_CODE_BYTES, LANG_NAME_BYTES);
}

void lang_pack_release(const u8 *pack, char out[LANG_RELEASE_BYTES])
{
	if (memcmp(pack, "TLNG", 4) == 0 && rd16(pack + 4) >= 2)
		copy_field(out, pack + 64, LANG_RELEASE_BYTES);
	else
		out[0] = 0;
}

void lang_use(const u8 *pack)
{
	lang.launch = 0;
	if (!pack) {
		lang.count = 0;
		lang.offsets = NULL;
		lang.pool = NULL;
		return;
	}
	lang.offsets = (const u16 *)(const void *)(pack + LANG_HEADER);
	lang.pool = (const char *)pack + LANG_HEADER + 2 * STR_COUNT;
	lang.count = STR_COUNT;
}

int lang_using_pack(void)
{
	return lang.count != 0;
}

int lang_keep_launch(void *room, u32 bytes)
{
	if (!lang.count || lang.launch)
		return 1;
	u16 *offsets = room;
	char *pool = (char *)room + 2 * STR_LAUNCH_END;
	u32 at = 1, room_pool = bytes > 2 * STR_LAUNCH_END ? bytes - 2 * STR_LAUNCH_END : 0;
	if (!room_pool) {
		lang_use(NULL);
		return 0;
	}
	pool[0] = 0;
	for (u32 id = 0; id < STR_LAUNCH_END; id++) {
		u16 from = lang.offsets[id];
		offsets[id] = 0;
		if (!from)
			continue;
		u32 len = (u32)strlen(lang.pool + from) + 1;
		if (at + len > room_pool || at > 0xFFFF) {
			lang_use(NULL);
			return 0;
		}
		memcpy(pool + at, lang.pool + from, len);
		offsets[id] = (u16)at;
		at += len;
	}
	lang.offsets = offsets;
	lang.pool = pool;
	lang.count = STR_LAUNCH_END;
	lang.launch = 1;
	return 1;
}

// --- Filling in ---------------------------------------------------------------

// Appends n bytes of s to out at *at (below room - 1), cut to what fits on
// a whole character
static void put(char *out, u32 room, u32 *at, const char *s, u32 n)
{
	if (*at + n >= room) {
		n = room - 1 - *at;
		while (n && ((u8)s[n] & 0xC0) == 0x80)
			n--;
	}
	memcpy(out + *at, s, n);
	*at += n;
}

// n in base (10 or 16), at least width digits
static u32 digits(char *buf, u32 n, u32 base, u32 width)
{
	char rev[12];
	u32 len = 0;
	do {
		u32 d = n % base;
		rev[len++] = (char)(d < 10 ? '0' + d : 'A' + d - 10);
		n /= base;
	} while (n && len < sizeof(rev));
	while (len < width && len < sizeof(rev))
		rev[len++] = '0';
	for (u32 i = 0; i < len; i++)
		buf[i] = rev[len - 1 - i];
	return len;
}

static void put_group(char *out, u32 room, u32 *at, u32 n)
{
	char d[12];
	u32 len = digits(d, n, 10, 0);
	const char *sep = S(STR_FORMAT_THOUSANDS);
	u32 sep_len = (u32)strlen(sep);
	for (u32 i = 0; i < len; i++) {
		if (i && (len - i) % 3 == 0)
			put(out, room, at, sep, sep_len);
		put(out, room, at, d + i, 1);
	}
}

static void put_arg(char *out, u32 room, u32 *at, const struct lang_arg *a)
{
	char d[12];
	switch (a->kind) {
	case LANG_ARG_STR:
		if (a->s)
			put(out, room, at, a->s, (u32)strlen(a->s));
		break;
	case LANG_ARG_CUT: {
		if (!a->s)
			break;
		// At most n bytes, ending on a whole character
		u32 n = (u32)strnlen(a->s, a->n);
		if (n < (u32)strlen(a->s))
			while (n && ((u8)a->s[n] & 0xC0) == 0x80)
				n--;
		put(out, room, at, a->s, n);
		break;
	}
	case LANG_ARG_HEX:
		put(out, room, at, d, digits(d, a->n, 16, a->width));
		break;
	case LANG_ARG_GROUP:
		put_group(out, room, at, a->n);
		break;
	case LANG_ARG_TENTHS: {
		put_group(out, room, at, a->n / 10);
		const char *sep = S(STR_FORMAT_DECIMAL);
		put(out, room, at, sep, (u32)strlen(sep));
		d[0] = (char)('0' + a->n % 10);
		put(out, room, at, d, 1);
		break;
	}
	default:  // LANG_ARG_NUM, LANG_ARG_ZERO
		put(out, room, at, d, digits(d, a->n, 10, a->kind == LANG_ARG_ZERO ? a->width : 0));
		break;
	}
}

u32 lang_fill_args(char *out, u32 room, const char *text, u32 argc, const struct lang_arg *args)
{
	if (!room)
		return 0;
	u32 at = 0;
	const char *run = text;
	while (*text) {
		if (text[0] == '{' && text[1] >= '1' && text[1] <= '9' && text[2] == '}' &&
		    (u32)(text[1] - '1') < argc) {
			put(out, room, &at, run, (u32)(text - run));
			put_arg(out, room, &at, &args[text[1] - '1']);
			text += 3;
			run = text;
		} else {
			text++;
		}
	}
	put(out, room, &at, run, (u32)(text - run));
	out[at] = 0;
	return at;
}

u32 lang_number(char *out, u32 room, u32 n)
{
	return lang_fill(out, room, "{1}", LG(n));
}

// A [format] text's {day} and {month} filled in
static u32 fill_date(char *out, u32 room, const char *format, const char *day, const char *month)
{
	if (!room)
		return 0;
	u32 at = 0;
	while (*format) {
		const char *with = NULL;
		u32 skip = 0;
		if (!strncmp(format, "{day}", 5))
			with = day, skip = 5;
		else if (!strncmp(format, "{month}", 7))
			with = month, skip = 7;
		if (with) {
			put(out, room, &at, with, (u32)strlen(with));
			format += skip;
		} else {
			u32 len = 1;
			while (format[len] && ((u8)format[len] & 0xC0) == 0x80)
				len++;
			put(out, room, &at, format, len);
			format += len;
		}
	}
	out[at] = 0;
	return at;
}

u32 lang_date(char *out, u32 room, u32 day, u32 month)
{
	char d[4];
	d[digits(d, day % 100, 10, 0)] = 0;
	return fill_date(out, room, S(STR_FORMAT_DATE), d,
			 month >= 1 && month <= 12 ? S(STR_MONTH_JAN + month - 1) : "");
}

u32 lang_date_numeric(char *out, u32 room, u32 day, u32 month)
{
	char d[4], m[4];
	d[digits(d, day % 100, 10, 2)] = 0;
	m[digits(m, month % 100, 10, 2)] = 0;
	return fill_date(out, room, S(STR_FORMAT_DATE_NUMERIC), d, m);
}

// --- The languages --------------------------------------------------------

u16 lang_code_id(const char *code)
{
	u32 h = 0x811C9DC5u;
	for (; *code; code++)
		h = (h ^ (u8)*code) * 0x01000193u;
	u32 v = (h ^ (h >> 16)) & 0xFFFF;
	if (v == 0 || v == 0xFFFF || v == LANG_ENGLISH_ID)
		v ^= 0x5A5A;
	return (u16)v;
}

void lang_list_reset(void)
{
	lang.langs = 0;
}

int lang_list_add(const char *code, const char *name)
{
	u16 id = lang_code_id(code);
	if (lang.langs + 1 >= LANG_LIST_MAX || lang_index_of(id))
		return 0;
	struct lang_choice *c = &lang.list[lang.langs++];
	c->id = id;
	strncpy(c->code, code, sizeof(c->code) - 1);
	c->code[sizeof(c->code) - 1] = 0;
	strncpy(c->name, name, sizeof(c->name) - 1);
	c->name[sizeof(c->name) - 1] = 0;
	return 1;
}

u32 lang_count(void)
{
	return 1 + lang.langs;
}

const struct lang_choice *lang_choice(u32 i)
{
	static const struct lang_choice english = {LANG_ENGLISH_ID, "en", "English"};
	return i && i <= lang.langs ? &lang.list[i - 1] : &english;
}

u32 lang_index_of(u16 id)
{
	for (u32 i = 0; i < lang.langs; i++)
		if (lang.list[i].id == id)
			return i + 1;
	return 0;
}
