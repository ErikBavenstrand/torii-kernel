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

static const uint8_t ones[16] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};

// Whether a lane of size bytes of d is 0. Inlined, as narrow_size is: the
// overlay runs the compare from IWRAM, the rest of this file from EWRAM.
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
// only equal is kept; else lane by lane, the sign bit flipped for a signed
// compare. One copy per size, so the loop's values stay in registers (the
// overlay's stack is in EWRAM).
static inline __attribute__((always_inline)) uint32_t narrow_size(const uint32_t *now, const uint32_t *before,
								  uint8_t *bits, uint32_t words, uint32_t size,
								  uint32_t want, uint32_t flip)
{
	const uint32_t per = 4 / size, lanes = (1u << per) - 1, max = cheatsearch_max(size);
	uint32_t left = 0;
	for (uint32_t j = 0; j < words; j++) {
		uint32_t e = j * per, sh = e & 7, cand = (uint32_t)bits[e >> 3] >> sh & lanes;
		if (!cand)
			continue;
		uint32_t w = now[j], b = before[j], d = w ^ b, keep = 0;
		if (!d) {
			keep = want & CHEATSEARCH_EQ ? cand : 0;
		} else if (want != CHEATSEARCH_EQ || zero_lane(d, size)) {
			for (uint32_t l = 0; l < per; l++) {
				uint32_t x = (w >> (8 * size * l) & max) ^ flip, y = (b >> (8 * size * l) & max) ^ flip;
				if ((cand >> l & 1) && (want & (x == y  ? CHEATSEARCH_EQ
								: x < y ? CHEATSEARCH_LT
									: CHEATSEARCH_GT)))
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
			    uint32_t want, uint32_t flip)
{
	const uint32_t *now32 = (const uint32_t *)(const void *)now, *before32 = (const uint32_t *)(const void *)before;
	if (size == 1)
		return narrow_size(now32, before32, bits, n / 4, 1, want, flip);
	if (size == 2)
		return narrow_size(now32, before32, bits, n / 4, 2, want, flip);
	return narrow_size(now32, before32, bits, n / 4, 4, want, flip);
}

void cheatsearch_fill(uint8_t *out, uint32_t n, uint32_t size, uint32_t value)
{
	value &= cheatsearch_max(size);
	for (uint32_t k = size; k < 4; k *= 2)
		value |= value << (8 * k);
	for (uint32_t i = 0; i < n / 4; i++)
		((uint32_t *)(void *)out)[i] = value;
}

void cheatsearch_shift(uint8_t *out, const uint8_t *in, uint32_t n, uint32_t size, uint32_t delta)
{
	const uint32_t max = cheatsearch_max(size);
	for (uint32_t i = 0; i < n; i += size) {
		uint32_t v = (cheatsearch_get(in + i, size) + delta) & max;
		for (uint32_t k = 0; k < size; k++)
			out[i + k] = (uint8_t)(v >> (8 * k));
	}
}

int cheatsearch_from_bcd(uint32_t value, uint32_t size, uint32_t *out)
{
	uint32_t n = 0;
	value &= cheatsearch_max(size);
	for (int sh = 8 * (int)size - 4; sh >= 0; sh -= 4) {
		uint32_t d = value >> sh & 15;
		if (d > 9)
			return 0;
		n = n * 10 + d;
	}
	*out = n;
	return 1;
}

uint32_t cheatsearch_to_bcd(uint32_t number, uint32_t size)
{
	uint32_t v = 0;
	for (uint32_t sh = 0; sh < 8 * size; sh += 4) {
		v |= number % 10 << sh;
		number /= 10;
	}
	return v;
}

// Drops from bits the candidates whose element of in (n bytes, a word at
// a time) isn't BCD: a nibble past 9 has its 8 bit and its 4 or 2 bit set.
// The overlay runs it from IWRAM with the compare.
void cheatsearch_bcd_drop(const uint8_t *in, uint8_t *bits, uint32_t n, uint32_t size)
{
	const uint32_t *w32 = (const uint32_t *)(const void *)in;
	const uint32_t per = 4 / size, lanes = (1u << per) - 1, lane = cheatsearch_max(size);
	for (uint32_t j = 0; j < n / 4; j++) {
		uint32_t e = j * per, sh = e & 7, cand = (uint32_t)bits[e >> 3] >> sh & lanes;
		uint32_t w = w32[j], bad = (w & 0x88888888u) >> 3 & ((w & 0x44444444u) >> 2 | (w & 0x22222222u) >> 1);
		if (!cand || !bad)
			continue;
		for (uint32_t l = 0; l < per; l++)
			if (bad >> (8 * size * l) & lane)
				cand &= ~(1u << l);
		bits[e >> 3] = (uint8_t)((bits[e >> 3] & ~(lanes << sh)) | cand << sh);
	}
}

// By on BCD: each candidate's earlier number (its digits) plus or minus a,
// as BCD into out; one past the size's digits, or below 0, as a value no
// BCD element is (all nibbles 15)
static void bcd_shift(uint8_t *out, const uint8_t *prev, const uint8_t *bits, uint32_t n, uint32_t size, uint32_t a,
		      int up)
{
	const uint32_t top = cheatsearch_largest(size, CHEATSEARCH_BCD);
	for (uint32_t e = 0; e < n / size; e++) {
		if (!(bits[e >> 3] >> (e & 7) & 1))
			continue;
		uint32_t v = 0, t = cheatsearch_max(size);
		cheatsearch_from_bcd(cheatsearch_get(prev + e * size, size), size, &v);
		if (up ? a <= top - v : a <= v)
			t = cheatsearch_to_bcd(up ? v + a : v - a, size);
		for (uint32_t k = 0; k < size; k++)
			out[e * size + k] = (uint8_t)(t >> (8 * k));
	}
}

uint32_t cheatsearch_chunk(const uint8_t *now, const uint8_t *prev, uint8_t *work, uint8_t *bits, uint32_t n,
			   uint32_t size, uint32_t op, uint32_t mode, uint32_t a, uint32_t b)
{
	if (mode == CHEATSEARCH_BCD) {
		// Elements that aren't BCD go; the rest compare as their bytes do,
		// in the digits' order, against the numbers as BCD (By: the earlier
		// numbers moved by a)
		const uint32_t top = cheatsearch_largest(size, CHEATSEARCH_BCD);
		cheatsearch_bcd_drop(now, bits, n, size);
		if (cheatsearch_relative(op))
			cheatsearch_bcd_drop(prev, bits, n, size);
		if (op == CHEATSEARCH_UP_BY || op == CHEATSEARCH_DOWN_BY) {
			bcd_shift(work, prev, bits, n, size, a, op == CHEATSEARCH_UP_BY);
			return cheatsearch_narrow(now, work, bits, n, size, CHEATSEARCH_EQ, 0);
		}
		a = cheatsearch_to_bcd(a < top ? a : top, size);
		b = cheatsearch_to_bcd(b < top ? b : top, size);
		mode = CHEATSEARCH_UNSIGNED;
	}
	const uint32_t flip = mode == CHEATSEARCH_SIGNED ? 1u << (8 * size - 1) : 0;
	switch (op) {
	case CHEATSEARCH_SAME:
		return cheatsearch_narrow(now, prev, bits, n, size, CHEATSEARCH_EQ, flip);
	case CHEATSEARCH_CHANGED:
		return cheatsearch_narrow(now, prev, bits, n, size, CHEATSEARCH_LT | CHEATSEARCH_GT, flip);
	case CHEATSEARCH_UP:
		return cheatsearch_narrow(now, prev, bits, n, size, CHEATSEARCH_GT, flip);
	case CHEATSEARCH_DOWN:
		return cheatsearch_narrow(now, prev, bits, n, size, CHEATSEARCH_LT, flip);
	case CHEATSEARCH_UP_BY:
	case CHEATSEARCH_DOWN_BY:
		cheatsearch_shift(work, prev, n, size, op == CHEATSEARCH_UP_BY ? a : 0u - a);
		return cheatsearch_narrow(now, work, bits, n, size, CHEATSEARCH_EQ, flip);
	case CHEATSEARCH_EQUALS:
	case CHEATSEARCH_MORE:
	case CHEATSEARCH_LESS:
		cheatsearch_fill(work, n, size, a);
		return cheatsearch_narrow(now, work, bits, n, size,
					  op == CHEATSEARCH_EQUALS ? CHEATSEARCH_EQ
					  : op == CHEATSEARCH_MORE ? CHEATSEARCH_GT
								   : CHEATSEARCH_LT,
					  flip);
	case CHEATSEARCH_BETWEEN:
		cheatsearch_fill(work, n, size, a);
		cheatsearch_narrow(now, work, bits, n, size, CHEATSEARCH_EQ | CHEATSEARCH_GT, flip);
		cheatsearch_fill(work, n, size, b);
		return cheatsearch_narrow(now, work, bits, n, size, CHEATSEARCH_EQ | CHEATSEARCH_LT, flip);
	default:
		return cheatsearch_narrow(now, prev, bits, n, size, 0, 0);  // none kept
	}
}

uint32_t cheatsearch_count(const uint8_t *bits, uint32_t bytes)
{
	uint32_t n = 0;
	for (uint32_t i = 0; i < bytes; i++)
		for (uint32_t b = bits[i]; b; b &= b - 1)
			n++;
	return n;
}

uint32_t cheatsearch_digits(uint32_t size, uint32_t mode)
{
	return mode == CHEATSEARCH_BCD ? 2 * size : size == 1 ? 3 : size == 2 ? 5 : 10;
}

uint32_t cheatsearch_largest(uint32_t size, uint32_t mode)
{
	if (mode == CHEATSEARCH_SIGNED)
		return cheatsearch_max(size) / 2;
	if (mode != CHEATSEARCH_BCD)
		return cheatsearch_max(size);
	uint32_t v = 0;
	for (uint32_t i = 0; i < 2 * size; i++)
		v = v * 10 + 9;
	return v;
}

void cheatsearch_decimal(uint32_t value, uint8_t *out, uint32_t n)
{
	for (uint32_t i = n; i--;) {
		out[i] = (uint8_t)(value % 10);
		value /= 10;
	}
}

// A number's digits (n of them) with the one at place stepped, as
// cheatsearch_edit says, up to limit
static uint32_t edit_digits(uint32_t value, uint32_t n, uint32_t place, int step, uint32_t limit)
{
	uint32_t weight = 1;  // at most 10^9: no 64-bit division (the overlay links no room for it)
	for (uint32_t i = place + 1; i < n; i++)
		weight *= 10;
	uint32_t digit = value / weight % 10;
	uint32_t to = step > 0 ? (digit + 1) % 10 : (digit + 9) % 10;
	uint64_t v = (uint64_t)value - (uint64_t)digit * weight + (uint64_t)to * weight;
	if (v > limit)
		v = step > 0 ? value - digit * weight : limit;
	return (uint32_t)v;
}

uint32_t cheatsearch_edit(uint32_t value, uint32_t size, uint32_t mode, uint32_t place, int step)
{
	const uint32_t n = cheatsearch_digits(size, mode), max = cheatsearch_max(size);
	value &= max;
	if (mode != CHEATSEARCH_SIGNED) {
		const uint32_t top = cheatsearch_largest(size, mode);
		value = value < top ? value : top;
		return place < n ? edit_digits(value, n, place, step, top) : value;
	}
	const uint32_t half = max / 2 + 1;  // 128, 32768, 2^31: the most negative's size
	uint32_t neg = (value & half) != 0, mag = neg ? (0u - value) & max : value;
	if (place == 0) {  // the sign
		if (!mag)
			return 0;
		if (neg && mag > half - 1)
			mag = half - 1;
		neg = !neg;
	} else if (place <= n) {
		mag = edit_digits(mag, n, place - 1, step, neg ? half : half - 1);
	}
	return neg ? (0u - mag) & max : mag;
}
