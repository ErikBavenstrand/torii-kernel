// The in-game cheat search's compare (docs/design/v0.9/cheat-search.md):
// narrowing the candidates a chunk of RAM at a time against the snapshot
// of it the last pass kept, and the numbers the overlay draws and edits.
// Hardware-free: the overlay and the host tests build it.
//
// An element is an aligned unit of 1, 2 or 4 bytes, little-endian, compared
// unsigned or signed (two's complement). The candidates are a bit per
// element, element i at bit i & 7 of byte i >> 3.
#ifndef TORII_CHEATSEARCH_H
#define TORII_CHEATSEARCH_H

#include <stdint.h>

// What is searched, as one run of bytes: EWRAM, then IWRAM up to the BIOS's
// and the hook's last 512 bytes
#define CHEATSEARCH_EWRAM 0x02000000u
#define CHEATSEARCH_EWRAM_BYTES 0x40000u
#define CHEATSEARCH_IWRAM 0x03000000u
#define CHEATSEARCH_IWRAM_BYTES 0x7E00u
#define CHEATSEARCH_BYTES (CHEATSEARCH_EWRAM_BYTES + CHEATSEARCH_IWRAM_BYTES)

// The filters, in the search page's order: the first six compare with an
// earlier value of the element (the last scan's or the first's), the rest
// with numbers alone (a, and b for Between)
enum cheatsearch_op {
	CHEATSEARCH_SAME,  // now == before
	CHEATSEARCH_CHANGED,  // now != before
	CHEATSEARCH_UP,  // now > before
	CHEATSEARCH_DOWN,  // now < before
	CHEATSEARCH_UP_BY,  // now == before + a, wrapping at the size
	CHEATSEARCH_DOWN_BY,  // now == before - a, wrapping
	CHEATSEARCH_EQUALS,  // now == a
	CHEATSEARCH_MORE,  // now > a
	CHEATSEARCH_LESS,  // now < a
	CHEATSEARCH_BETWEEN,  // a <= now <= b
	CHEATSEARCH_OPS
};

// Whether op compares with an earlier value of the element
static inline int cheatsearch_relative(uint32_t op)
{
	return op <= CHEATSEARCH_DOWN_BY;
}

// The outcomes cheatsearch_narrow keeps, as a mask
#define CHEATSEARCH_EQ 1u
#define CHEATSEARCH_LT 2u
#define CHEATSEARCH_GT 4u

// The address of byte offset at of the searched run
static inline uint32_t cheatsearch_address(uint32_t at)
{
	return at < CHEATSEARCH_EWRAM_BYTES ? CHEATSEARCH_EWRAM + at : CHEATSEARCH_IWRAM + at - CHEATSEARCH_EWRAM_BYTES;
}

// The candidates' bytes for the whole run at size
static inline uint32_t cheatsearch_bits_bytes(uint32_t size)
{
	return (CHEATSEARCH_BYTES / size + 7) / 8;
}

// The largest value of size bytes
static inline uint32_t cheatsearch_max(uint32_t size)
{
	return size >= 4 ? 0xFFFFFFFFu : (1u << (size * 8)) - 1;
}

// The element of size 1, 2 or 4 bytes at p, little-endian
uint32_t cheatsearch_get(const uint8_t *p, uint32_t size);

// value of size bytes as a signed number
static inline int32_t cheatsearch_signed(uint32_t value, uint32_t size)
{
	uint32_t sign = 1u << (8 * size - 1);
	value &= cheatsearch_max(size);
	return (int32_t)((value ^ sign) - sign);
}

// A filter over a chunk of n bytes (a multiple of 8 * size: whole bytes of
// bits), every buffer word-aligned: each candidate in bits whose element of
// now fails op is dropped. prev holds the earlier values the relative
// filters compare with; work is n bytes of scratch. Signed (sign 1)
// compares as two's complement; By wraps either way. The candidates left.
// The caller keeps now as the next pass's earlier values.
uint32_t cheatsearch_chunk(const uint8_t *now, const uint8_t *prev, uint8_t *work, uint8_t *bits, uint32_t n,
			   uint32_t size, uint32_t op, uint32_t sign, uint32_t a, uint32_t b);

// cheatsearch_chunk's compare (the overlay runs it from IWRAM): each
// candidate whose element of now against before's has an outcome (EQ, LT,
// GT) not in want is dropped; flip is the size's sign bit for a signed
// compare, else 0. The candidates left.
uint32_t cheatsearch_narrow(const uint8_t *now, const uint8_t *before, uint8_t *bits, uint32_t n, uint32_t size,
			    uint32_t want, uint32_t flip);

// n bytes of out (word-aligned) as value in every element of size bytes
void cheatsearch_fill(uint8_t *out, uint32_t n, uint32_t size, uint32_t value);

// n bytes of out as in's elements plus delta, each wrapping at the size
void cheatsearch_shift(uint8_t *out, const uint8_t *in, uint32_t n, uint32_t size, uint32_t delta);

// The candidates in bytes of bits
uint32_t cheatsearch_count(const uint8_t *bits, uint32_t bytes);

// The decimal digits a value of size bytes is edited in: 3, 5 or 10
uint32_t cheatsearch_digits(uint32_t size);

// The places a number is edited in: its digits, after a sign when signed
static inline uint32_t cheatsearch_places(uint32_t size, uint32_t sign)
{
	return cheatsearch_digits(size) + (sign != 0);
}

// value's decimal digits into out, the most significant first, padded with
// zeros to n digits (n at most 10); a digit is 0-9
void cheatsearch_decimal(uint32_t value, uint8_t *out, uint32_t n);

// value with place (0 the leftmost of cheatsearch_places) one up (step 1)
// or down (step -1). A digit wraps 9 to 0 and 0 to 9; a value past the
// largest the size takes becomes 0 in that digit going up, the largest
// going down. Signed, place 0 is the sign, which UP and DOWN flip (-128
// becomes 127), and the digits are the number's without its sign, up to
// 127 (32767, ...) or 128 negative.
uint32_t cheatsearch_edit(uint32_t value, uint32_t size, uint32_t sign, uint32_t place, int step);

#endif
