// See artkey.h.
#include <string.h>

#include "artkey.h"

#define EXT_LEN (sizeof(ARTKEY_EXT) - 1)

static u32 hex_digit(u8 c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	c |= 0x20;
	return c >= 'a' && c <= 'f' ? c - 'a' + 10u : 16u;
}

static u8 lower(u8 c)
{
	return c >= 'A' && c <= 'Z' ? (u8)(c + 32) : c;
}

u32 artkey_custom_len(const char *name)
{
	u32 len = strlen(name);
	if (len <= EXT_LEN)
		return 0;
	for (u32 i = 0; i < EXT_LEN; i++)
		if (lower((u8)name[len - EXT_LEN + i]) != (u8)ARTKEY_EXT[i])
			return 0;
	return len - EXT_LEN;
}

u32 artkey_parse(const char *name, u32 *key)
{
	u32 stem = artkey_custom_len(name);
	u32 value = 0;
	if (stem == 8) {
		for (u32 i = 0; i < 8; i++) {
			u32 d = hex_digit((u8)name[i]);
			if (d > 15)
				return ARTKEY_NONE;
			value = value << 4 | d;
		}
		*key = value;
		return ARTKEY_CRC;
	}
	return ARTKEY_NONE;
}

u32 artkey_name(const char *name, u32 len)
{
	u32 h = 2166136261u;
	for (u32 i = 0; i < len; i++)
		h = (h ^ lower((u8)name[i])) * 16777619u;
	return h;
}

u32 artkey_id(u32 kind, u32 key)
{
	// murmur3's 32-bit finaliser over the key and its kind
	u32 h = key ^ (kind * 0x9E3779B9u);
	h ^= h >> 16;
	h *= 0x85EBCA6Bu;
	h ^= h >> 13;
	h *= 0xC2B2AE35u;
	h ^= h >> 16;
	return h ? h : 1;
}

int artset_has(const u32 *slots, u32 id)
{
	// The table is never more than half full, so an empty slot ends the probe
	for (u32 i = id & (ARTSET_SLOTS - 1);; i = (i + 1) & (ARTSET_SLOTS - 1)) {
		u32 v = slots[i];
		if (v == id)
			return 1;
		if (!v)
			return 0;
	}
}

int artset_add(struct artset *s, u32 id)
{
	u32 i = id & (ARTSET_SLOTS - 1);
	for (;; i = (i + 1) & (ARTSET_SLOTS - 1)) {
		u32 v = s->slots[i];
		if (v == id)
			return 1;
		if (!v)
			break;
	}
	if (s->count >= ARTSET_MAX)
		return 0;
	s->put(&s->slots[i], id);
	s->count++;
	return 1;
}
