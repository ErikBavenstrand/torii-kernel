// The in-game cheat search's compare (source/cheatsearch.c): each filter at
// each size, unsigned and signed, what a pass leaves, and the number edits
// the overlay makes.
#include <string.h>

#include "../../source/cheatsearch.h"
#include "../kernel/torii_test.h"
#include "tests.h"

#define T_BYTES 64

// Aligned as the overlay's chunks are: elements are read a word at a time
static uint8_t t_now[T_BYTES] __attribute__((aligned(4))), t_before[T_BYTES] __attribute__((aligned(4)));
static uint8_t t_work[T_BYTES] __attribute__((aligned(4)));
static uint8_t t_bits[T_BYTES / 8];

static void t_put(uint8_t *p, uint32_t at, uint32_t size, uint32_t v)
{
	for (uint32_t i = 0; i < size; i++)
		p[at + i] = (uint8_t)(v >> (8 * i));
}

// A pass from the candidates in t_bits, as the overlay makes it; now is
// the next pass's before
static uint32_t t_narrow(uint32_t size, uint32_t op, uint32_t sign, uint32_t a, uint32_t b)
{
	uint32_t left = cheatsearch_chunk(t_now, t_before, t_work, t_bits, T_BYTES, size, op, sign, a, b);
	memcpy(t_before, t_now, sizeof(t_now));
	return left;
}

// The same from all candidates, before kept as it was
static uint32_t t_pass(uint32_t size, uint32_t op, uint32_t sign, uint32_t a, uint32_t b)
{
	memset(t_bits, 0xFF, sizeof(t_bits));
	return cheatsearch_chunk(t_now, t_before, t_work, t_bits, T_BYTES, size, op, sign, a, b);
}

static int t_bit(uint32_t i)
{
	return t_bits[i >> 3] >> (i & 7) & 1;
}

// Element 1 went up to the size's largest (-1 signed), 2 went down from
// it, 3 changed in its top byte only; the rest stayed 0
static void t_setup(uint32_t size)
{
	uint32_t max = cheatsearch_max(size);
	memset(t_now, 0, sizeof(t_now));
	memset(t_before, 0, sizeof(t_before));
	t_put(t_now, 1 * size, size, max);
	t_put(t_before, 2 * size, size, max);
	t_put(t_now, 3 * size, size, 1u << (8 * (size - 1)));
}

static void test_filters(void)
{
	for (uint32_t size = 1; size <= 4; size *= 2) {
		uint32_t elements = T_BYTES / size, max = cheatsearch_max(size);
		t_setup(size);
		uint8_t now[T_BYTES];
		memcpy(now, t_now, sizeof(now));

		CHECK_EQ(t_pass(size, CHEATSEARCH_SAME, 0, 0, 0), elements - 3);
		CHECK(!t_bit(1) && !t_bit(2) && !t_bit(3) && t_bit(0) && t_bit(elements - 1));
		CHECK(memcmp(t_now, now, sizeof(now)) == 0);  // now is left alone
		CHECK_EQ(t_pass(size, CHEATSEARCH_CHANGED, 0, 0, 0), 3);
		CHECK(t_bit(1) && t_bit(2) && t_bit(3) && !t_bit(0));
		CHECK_EQ(t_pass(size, CHEATSEARCH_UP, 0, 0, 0), 2);  // 1 and 3: unsigned
		CHECK(t_bit(1) && t_bit(3));
		CHECK_EQ(t_pass(size, CHEATSEARCH_DOWN, 0, 0, 0), 1);
		CHECK(t_bit(2));
		// Signed, element 1 went from 0 to -1 (down), 2 from -1 to 0 (up),
		// 3 from 0 to a positive (up)
		CHECK_EQ(t_pass(size, CHEATSEARCH_UP, 1, 0, 0), 2);
		CHECK(t_bit(2) && t_bit(3) && !t_bit(1));
		CHECK_EQ(t_pass(size, CHEATSEARCH_DOWN, 1, 0, 0), 1);
		CHECK(t_bit(1));
		// By N wraps, signed or not: 0 to -1 is down by 1, -1 to 0 up by 1
		CHECK_EQ(t_pass(size, CHEATSEARCH_DOWN_BY, 0, 1, 0), 1);
		CHECK(t_bit(1));
		CHECK_EQ(t_pass(size, CHEATSEARCH_UP_BY, 1, 1, 0), size == 1 ? 2 : 1);  // 3 is 1 at 8 bits
		CHECK(t_bit(2));
		CHECK_EQ(t_pass(size, CHEATSEARCH_UP_BY, 0, 2, 0), 0);

		CHECK_EQ(t_pass(size, CHEATSEARCH_EQUALS, 0, max, 0), 1);
		CHECK(t_bit(1));
		// A value past the size is cut to it
		CHECK_EQ(t_pass(size, CHEATSEARCH_EQUALS, 0, size < 4 ? max + 1 : 0, 0),
			 elements - 2);  // all but 1 and 3
		// More, Less and Between: unsigned, -1 is the largest; signed, the least
		CHECK_EQ(t_pass(size, CHEATSEARCH_MORE, 0, 0, 0), 2);
		CHECK_EQ(t_pass(size, CHEATSEARCH_MORE, 1, 0, 0), 1);
		CHECK(t_bit(3));
		CHECK_EQ(t_pass(size, CHEATSEARCH_LESS, 1, 0, 0), 1);
		CHECK(t_bit(1));
		CHECK_EQ(t_pass(size, CHEATSEARCH_LESS, 0, 1, 0), elements - 2);
		CHECK_EQ(t_pass(size, CHEATSEARCH_BETWEEN, 1, max, 0), elements - 1);  // -1 to 0: all but 3
		CHECK(t_bit(1) && t_bit(0));
		CHECK_EQ(t_pass(size, CHEATSEARCH_BETWEEN, 0, 1, max), 2);  // 1 to the largest: 1 and 3
		CHECK(t_bit(1) && t_bit(3) && !t_bit(0));
		CHECK_EQ(t_pass(size, CHEATSEARCH_BETWEEN, 0, 5, 4), 0);  // an empty range
	}
}

// A dropped candidate stays dropped; elements are aligned and little-endian
static void test_narrowing(void)
{
	memset(t_now, 0, sizeof(t_now));
	memset(t_before, 0, sizeof(t_before));
	t_put(t_now, 6, 2, 0x1234);
	CHECK_EQ(t_pass(2, CHEATSEARCH_EQUALS, 0, 0x1234, 0), 1);
	CHECK(t_bit(3));
	CHECK_EQ(cheatsearch_get(t_now + 6, 2), 0x1234);
	memcpy(t_before, t_now, sizeof(t_now));
	// The value goes back to 0: element 3 changed, the others stayed but are gone
	t_put(t_now, 6, 2, 0);
	CHECK_EQ(t_narrow(2, CHEATSEARCH_CHANGED, 0, 0, 0), 1);
	CHECK_EQ(cheatsearch_count(t_bits, T_BYTES / 2 / 8), 1);
	CHECK_EQ(t_narrow(2, CHEATSEARCH_CHANGED, 0, 0, 0), 0);
	CHECK_EQ(cheatsearch_count(t_bits, T_BYTES / 2 / 8), 0);
	// 32-bit elements at 4-byte steps
	memset(t_now, 0, sizeof(t_now));
	t_put(t_now, 8, 4, 0xDEADBEEF);
	CHECK_EQ(t_pass(4, CHEATSEARCH_EQUALS, 0, 0xDEADBEEF, 0), 1);
	CHECK(t_bit(2));
	// Lives 5 to 3: down by 2, against the first values a later pass passes
	memset(t_now, 0, sizeof(t_now));
	memset(t_before, 0, sizeof(t_before));
	t_put(t_before, 9, 1, 5);
	t_put(t_now, 9, 1, 3);
	CHECK_EQ(t_pass(1, CHEATSEARCH_DOWN_BY, 0, 2, 0), 1);
	CHECK(t_bit(9));
	// An unknown filter keeps none
	CHECK_EQ(t_pass(1, CHEATSEARCH_OPS, 0, 0, 0), 0);
}

static void test_run(void)
{
	CHECK_EQ(cheatsearch_address(0), 0x02000000);
	CHECK_EQ(cheatsearch_address(0x3FFFF), 0x0203FFFF);
	CHECK_EQ(cheatsearch_address(0x40000), 0x03000000);
	CHECK_EQ(cheatsearch_address(CHEATSEARCH_BYTES - 1), 0x03007DFF);
	CHECK_EQ(cheatsearch_bits_bytes(1), 36800);
	CHECK_EQ(cheatsearch_bits_bytes(4), 9200);
	CHECK(cheatsearch_relative(CHEATSEARCH_DOWN_BY) && !cheatsearch_relative(CHEATSEARCH_EQUALS));
	CHECK_EQ(cheatsearch_signed(0xFF, 1), -1);
	CHECK_EQ(cheatsearch_signed(0x7F, 1), 127);
	CHECK_EQ(cheatsearch_signed(0x8000, 2), -32768);
	CHECK_EQ(cheatsearch_signed(0x80000000u, 4), (int32_t)0x80000000u);
	CHECK_EQ(cheatsearch_signed(0x1FF, 1), -1);  // cut to the size
	memset(t_work, 0, sizeof(t_work));
	cheatsearch_fill(t_now, 8, 2, 0xABCD);
	cheatsearch_shift(t_work, t_now, 8, 2, 0x5433);
	CHECK_EQ(cheatsearch_get(t_work + 6, 2), 0);  // wraps
}

static void test_numbers(void)
{
	uint8_t d[10];
	cheatsearch_decimal(4294967295u, d, 10);
	static const uint8_t big[10] = {4, 2, 9, 4, 9, 6, 7, 2, 9, 5};
	CHECK(memcmp(d, big, 10) == 0);
	cheatsearch_decimal(7, d, 3);
	CHECK(d[0] == 0 && d[1] == 0 && d[2] == 7);
	CHECK_EQ(cheatsearch_digits(1, CHEATSEARCH_UNSIGNED), 3);
	CHECK_EQ(cheatsearch_digits(2, CHEATSEARCH_SIGNED), 5);
	CHECK_EQ(cheatsearch_digits(4, CHEATSEARCH_UNSIGNED), 10);
	CHECK_EQ(cheatsearch_digits(4, CHEATSEARCH_BCD), 8);
	CHECK_EQ(cheatsearch_places(1, CHEATSEARCH_SIGNED), 4);
	CHECK_EQ(cheatsearch_largest(2, CHEATSEARCH_BCD), 9999);
	CHECK_EQ(cheatsearch_largest(1, CHEATSEARCH_SIGNED), 127);
	// BCD's editor: 2 digits a byte, up to 99
	CHECK_EQ(cheatsearch_edit(95, 1, CHEATSEARCH_BCD, 0, 1), 5);  // 105 passes 99: the digit to 0
	CHECK_EQ(cheatsearch_edit(5, 1, CHEATSEARCH_BCD, 0, -1), 95);
	CHECK_EQ(cheatsearch_edit(200, 1, CHEATSEARCH_BCD, 1, 1), 90);  // out of range: 99 first, then 9 to 0
	CHECK_EQ(cheatsearch_edit(9999, 2, CHEATSEARCH_BCD, 3, 1), 9990);

	CHECK_EQ(cheatsearch_edit(0, 1, 0, 2, 1), 1);
	CHECK_EQ(cheatsearch_edit(9, 1, 0, 2, 1), 0);  // the digit wraps
	CHECK_EQ(cheatsearch_edit(0, 1, 0, 2, -1), 9);
	CHECK_EQ(cheatsearch_edit(200, 1, 0, 0, 1), 0);  // 300 passes 255: the digit to 0
	CHECK_EQ(cheatsearch_edit(155, 1, 0, 0, 1), 255);
	CHECK_EQ(cheatsearch_edit(55, 1, 0, 0, -1), 255);  // 955 passes it: the largest
	CHECK_EQ(cheatsearch_edit(100, 1, 0, 0, 1), 200);
	CHECK_EQ(cheatsearch_edit(4294967295u, 4, 0, 0, 1), 294967295u);
	CHECK_EQ(cheatsearch_edit(65535, 2, 0, 4, 1), 65530);
	CHECK_EQ(cheatsearch_edit(12, 1, 0, 3, 1), 12);  // no such digit

	// Signed: place 0 the sign, then the digits without it
	CHECK_EQ(cheatsearch_edit(5, 1, 1, 0, 1), 0xFB);  // 5 to -5
	CHECK_EQ(cheatsearch_edit(0xFB, 1, 1, 0, -1), 5);  // and back
	CHECK_EQ(cheatsearch_edit(0, 1, 1, 0, 1), 0);  // no -0
	CHECK_EQ(cheatsearch_edit(0x80, 1, 1, 0, 1), 127);  // -128 to 127
	CHECK_EQ(cheatsearch_edit(100, 1, 1, 1, 1), 0);  // 200 passes 127: the digit to 0
	CHECK_EQ(cheatsearch_edit(28, 1, 1, 1, -1), 127);  // 928: the largest
	CHECK_EQ(cheatsearch_edit(0xE4, 1, 1, 1, -1), 0x80);  // -28 to -928: -128
	CHECK_EQ(cheatsearch_edit(0xFF, 1, 1, 3, 1), 0xFE);  // -1 to -2
	CHECK_EQ(cheatsearch_edit(0xFFFFFFFFu, 4, 1, 10, 1), 0xFFFFFFFEu);
	CHECK_EQ(cheatsearch_edit(0x80000000u, 4, 1, 0, 1), 0x7FFFFFFFu);
}

// BCD: a score of 1234 as 0x1234; an element with a nibble past 9 isn't
// one, and no filter keeps it
static void test_bcd(void)
{
	uint32_t v = 0;
	CHECK(cheatsearch_from_bcd(0x1234, 2, &v) && v == 1234);
	CHECK(!cheatsearch_from_bcd(0x12A4, 2, &v));
	CHECK(cheatsearch_from_bcd(0x99999999u, 4, &v) && v == 99999999u);
	CHECK_EQ(cheatsearch_to_bcd(1234, 2), 0x1234);
	CHECK_EQ(cheatsearch_to_bcd(7, 1), 0x07);
	memset(t_now, 0, sizeof(t_now));
	memset(t_before, 0, sizeof(t_before));
	t_put(t_before, 2, 2, 0x0995);  // 995 to 1000: up by 5
	t_put(t_now, 2, 2, 0x1000);
	t_put(t_now, 4, 2, 0x00FA);  // not BCD
	t_put(t_now, 6, 2, 0x1000);  // 1000, as it was 0: up by 1000
	CHECK_EQ(t_pass(2, CHEATSEARCH_UP_BY, CHEATSEARCH_BCD, 5, 0), 1);
	CHECK(t_bit(1));
	CHECK_EQ(t_pass(2, CHEATSEARCH_EQUALS, CHEATSEARCH_BCD, 1000, 0), 2);
	CHECK(t_bit(1) && t_bit(3));
	CHECK_EQ(t_pass(2, CHEATSEARCH_CHANGED, CHEATSEARCH_BCD, 0, 0), 2);  // 0xFA isn't a number: dropped
	CHECK(!t_bit(2));
	CHECK_EQ(t_pass(2, CHEATSEARCH_MORE, CHEATSEARCH_BCD, 999, 0), 2);
	CHECK_EQ(t_pass(2, CHEATSEARCH_LESS, CHEATSEARCH_BCD, 1, 0), T_BYTES / 2 - 3);  // the zeros
	// Down by past 0, or up by past 9999: none
	CHECK_EQ(t_pass(2, CHEATSEARCH_DOWN_BY, CHEATSEARCH_BCD, 1, 0), 0);
	t_put(t_before, 2, 2, 0x9999);
	CHECK_EQ(t_pass(2, CHEATSEARCH_UP_BY, CHEATSEARCH_BCD, 1, 0), 0);
	// 8-bit: 0x99 is 99, 0x9A isn't BCD
	memset(t_now, 0, sizeof(t_now));
	t_put(t_now, 5, 1, 0x99);
	t_put(t_now, 6, 1, 0x9A);
	CHECK_EQ(t_pass(1, CHEATSEARCH_MORE, CHEATSEARCH_BCD, 98, 0), 1);
	CHECK(t_bit(5));
	CHECK_EQ(t_pass(1, CHEATSEARCH_EQUALS, CHEATSEARCH_BCD, 100, 0), 1);  // past 99: 99
}

void test_cheatsearch(void)
{
	test_bcd();
	test_filters();
	test_narrowing();
	test_run();
	test_numbers();
}
