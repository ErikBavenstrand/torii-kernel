// The in-game cheat search's compare (cheatsearch.h)
#include "cheatsearch.h"

uint32_t cheatsearch_get(const uint8_t *p, uint32_t size)
{
	if (size == 1)
		return p[0];
	if (size == 2)
		return p[0] | (uint32_t)p[1] << 8;
	return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

// The outcomes each filter keeps: bit 0 equal, 1 less, 2 greater
static const uint8_t kept[CHEATSEARCH_OPS] = {1, 6, 4, 2, 1};
static const uint8_t ones[16] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};

// Whether a lane of size bytes of d is 0. Inlined, as narrow_size is: the
// overlay runs the pass from IWRAM, the rest of this file from EWRAM.
static inline __attribute__((always_inline)) int zero_lane(uint32_t d, uint32_t size)
{
	if (size == 1)
		return ((d - 0x01010101u) & ~d & 0x80808080u) != 0;
	if (size == 2)
		return ((d - 0x00010001u) & ~d & 0x80008000u) != 0;
	return d == 0;
}

// A word at a time, its elements (lanes) the candidates in cand: a word
// equal to before's decides them all, as does one with no lane equal when
// only equal is kept; else lane by lane. One copy per size, so the loop's
// values stay in registers (the overlay's stack is in EWRAM).
static inline __attribute__((always_inline)) uint32_t narrow_size(const uint32_t *now, const uint32_t *before,
								  uint8_t *bits, uint32_t words, uint32_t size,
								  uint32_t want)
{
	const uint32_t per = 4 / size, lanes = (1u << per) - 1, max = cheatsearch_max(size);
	uint32_t left = 0;
	for (uint32_t j = 0; j < words; j++) {
		uint32_t e = j * per, sh = e & 7, cand = (uint32_t)bits[e >> 3] >> sh & lanes;
		if (!cand)
			continue;
		uint32_t w = now[j], b = before[j], d = w ^ b, keep = 0;
		if (!d) {
			keep = want & 1 ? cand : 0;
		} else if (want != 1 || zero_lane(d, size)) {
			for (uint32_t l = 0; l < per; l++) {
				uint32_t x = w >> (8 * size * l) & max, y = b >> (8 * size * l) & max;
				if ((cand >> l & 1) && (want & (x == y ? 1u : x < y ? 2u : 4u)))
					keep |= 1u << l;
			}
		}
		if (keep != cand)
			bits[e >> 3] = (uint8_t)((bits[e >> 3] & ~(lanes << sh)) | keep << sh);
		left += ones[keep];
	}
	return left;
}

uint32_t cheatsearch_narrow(const uint8_t *now, const uint8_t *before, uint8_t *bits, uint32_t n, uint32_t size,
			    uint32_t op)
{
	const uint32_t want = op < CHEATSEARCH_OPS ? kept[op] : 0;
	const uint32_t *now32 = (const uint32_t *)(const void *)now, *before32 = (const uint32_t *)(const void *)before;
	if (size == 1)
		return narrow_size(now32, before32, bits, n / 4, 1, want);
	if (size == 2)
		return narrow_size(now32, before32, bits, n / 4, 2, want);
	return narrow_size(now32, before32, bits, n / 4, 4, want);
}

void cheatsearch_fill(uint8_t *before, uint32_t n, uint32_t size, uint32_t value)
{
	value &= cheatsearch_max(size);
	for (uint32_t k = size; k < 4; k *= 2)
		value |= value << (8 * k);
	for (uint32_t i = 0; i < n / 4; i++)
		((uint32_t *)(void *)before)[i] = value;
}

uint32_t cheatsearch_count(const uint8_t *bits, uint32_t bytes)
{
	uint32_t n = 0;
	for (uint32_t i = 0; i < bytes; i++)
		for (uint32_t b = bits[i]; b; b &= b - 1)
			n++;
	return n;
}

uint32_t cheatsearch_digits(uint32_t size)
{
	return size == 1 ? 3 : size == 2 ? 5 : 10;
}

void cheatsearch_decimal(uint32_t value, uint8_t *out, uint32_t n)
{
	for (uint32_t i = n; i--;) {
		out[i] = (uint8_t)(value % 10);
		value /= 10;
	}
}

uint32_t cheatsearch_edit(uint32_t value, uint32_t size, uint32_t place, int step)
{
	uint32_t n = cheatsearch_digits(size), max = cheatsearch_max(size);
	if (place >= n)
		return value;
	uint32_t weight = 1;  // at most 10^9: no 64-bit division (the overlay links no room for it)
	for (uint32_t i = place + 1; i < n; i++)
		weight *= 10;
	uint32_t digit = value / weight % 10;
	uint32_t to = step > 0 ? (digit + 1) % 10 : (digit + 9) % 10;
	uint64_t v = (uint64_t)value - (uint64_t)digit * weight + (uint64_t)to * weight;
	if (v > max)
		v = step > 0 ? value - digit * weight : max;
	return (uint32_t)v;
}
