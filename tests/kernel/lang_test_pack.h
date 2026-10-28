// A language pack built in memory for the tests (tests/host/test_lang.c,
// tests/kernel/test_lang.c), as tools/lang.py builds one: lang.h's format.
#ifndef TORII_LANG_TEST_PACK_H
#define TORII_LANG_TEST_PACK_H

#include <string.h>

#include "../../source/crc32.h"
#include "../../source/lang.h"

struct lang_test_string {
	u16 id;
	const char *text;
};

static inline void lang_test_put16(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
}

static inline void lang_test_put32(u8 *p, u32 v)
{
	lang_test_put16(p, v & 0xFFFF);
	lang_test_put16(p + 2, v >> 16);
}

// The pack with code, name and n strings into out (room bytes, zeroed
// first); its bytes, or 0 if it doesn't fit
static inline u32 lang_test_pack(u8 *out, u32 room, const char *code, const char *name,
				 const struct lang_test_string *strings, u32 n)
{
	u32 pool = LANG_HEADER + 2 * STR_COUNT, at = 1;
	if (room < pool + 2)
		return 0;
	memset(out, 0, room);
	memcpy(out, "TLNG", 4);
	lang_test_put16(out + 4, LANG_FORMAT);
	lang_test_put16(out + 6, STR_COUNT);
	lang_test_put32(out + 8, LANG_KEY_HASH);
	strncpy((char *)out + 20, code, LANG_CODE_BYTES - 1);
	strncpy((char *)out + 20 + LANG_CODE_BYTES, name, LANG_NAME_BYTES - 1);
	lang_test_put32(out + 60, pool);
	for (u32 i = 0; i < n; i++) {
		u32 len = (u32)strlen(strings[i].text) + 1;
		if (pool + at + len > room)
			return 0;
		memcpy(out + pool + at, strings[i].text, len);
		lang_test_put16(out + LANG_HEADER + 2 * strings[i].id, at);
		at += len;
	}
	u32 size = (pool + at + 1) & ~1u;
	lang_test_put32(out + 12, size);
	lang_test_put32(out + 16, crc32(out + 20, size - 20));
	return size;
}

// Its CRC again after a test changed its bytes
static inline void lang_test_pack_seal(u8 *pack, u32 size)
{
	lang_test_put32(pack + 16, crc32(pack + 20, size - 20));
}

#endif
