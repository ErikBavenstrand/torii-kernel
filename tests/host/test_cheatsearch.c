// The in-game cheat search's compare (source/cheatsearch.c): each filter at
// each size, what a pass leaves as the next one's before, and the number
// edits the overlay makes.
#include <string.h>

#include "../../source/cheatsearch.h"
#include "../kernel/torii_test.h"
#include "tests.h"

#define T_BYTES 64

// Aligned as the overlay's chunks are: elements are read at their size
static uint8_t t_now[T_BYTES] __attribute__((aligned(4))), t_before[T_BYTES] __attribute__((aligned(4)));
static uint8_t t_bits[T_BYTES / 8];

static void t_put(uint8_t *p, uint32_t at, uint32_t size, uint32_t v)
{
	for (uint32_t i = 0; i < size; i++)
		p[at + i] = (uint8_t)(v >> (8 * i));
}

// A pass over the whole buffer from all candidates, as the overlay makes
// it (Equals against before filled with the value); now is the next before
static uint32_t t_narrow(uint32_t size, uint32_t op, uint32_t value)
{
	if (op == CHEATSEARCH_EQUALS)
		cheatsearch_fill(t_before, T_BYTES, size, value);
	uint32_t left = cheatsearch_narrow(t_now, t_before, t_bits, T_BYTES, size, op);
	memcpy(t_before, t_now, sizeof(t_now));
	return left;
}

static uint32_t t_pass(uint32_t size, uint32_t op, uint32_t value)
{
	memset(t_bits, 0xFF, sizeof(t_bits));
	return t_narrow(size, op, value);
}

static int t_bit(uint32_t i)
{
	return t_bits[i >> 3] >> (i & 7) & 1;
}

static void test_filters(void)
{
	for (uint32_t size = 1; size <= 4; size *= 2) {
		uint32_t elements = T_BYTES / size, max = cheatsearch_max(size);
		memset(t_now, 0, sizeof(t_now));
		memset(t_before, 0, sizeof(t_before));
		// Element 1 went up to the size's largest, 2 went down from it, 3
		// changed in its top byte only; the rest stayed 0
		t_put(t_now, 1 * size, size, max);
		t_put(t_before, 2 * size, size, max);
		t_put(t_now, 3 * size, size, 1u << (8 * (size - 1)));
		uint8_t now[T_BYTES];
		memcpy(now, t_now, sizeof(now));
		uint8_t before[T_BYTES];
		memcpy(before, t_before, sizeof(before));

		CHECK_EQ(t_pass(size, CHEATSEARCH_SAME, 0), elements - 3);
		CHECK(!t_bit(1) && !t_bit(2) && !t_bit(3) && t_bit(0) && t_bit(elements - 1));
		CHECK(memcmp(t_now, now, sizeof(now)) == 0);  // now is left alone

		memcpy(t_before, before, sizeof(before));
		CHECK_EQ(t_pass(size, CHEATSEARCH_CHANGED, 0), 3);
		CHECK(t_bit(1) && t_bit(2) && t_bit(3) && !t_bit(0));

		memcpy(t_before, before, sizeof(before));
		CHECK_EQ(t_pass(size, CHEATSEARCH_UP, 0), 2);  // 1 and 3: unsigned, the top byte counts
		CHECK(t_bit(1) && t_bit(3));

		memcpy(t_before, before, sizeof(before));
		CHECK_EQ(t_pass(size, CHEATSEARCH_DOWN, 0), 1);
		CHECK(t_bit(2));

		CHECK_EQ(t_pass(size, CHEATSEARCH_EQUALS, max), 1);
		CHECK(t_bit(1));
		// A value past the size is cut to it
		CHECK_EQ(t_pass(size, CHEATSEARCH_EQUALS, size < 4 ? max + 1 : 0), elements - 2);  // all but 1 and 3
	}
}

// A dropped candidate stays dropped; elements are aligned and little-endian
static void test_narrowing(void)
{
	memset(t_now, 0, sizeof(t_now));
	memset(t_before, 0, sizeof(t_before));
	t_put(t_now, 6, 2, 0x1234);
	CHECK_EQ(t_pass(2, CHEATSEARCH_EQUALS, 0x1234), 1);
	CHECK(t_bit(3));
	CHECK_EQ(cheatsearch_get(t_now + 6, 2), 0x1234);
	// The value goes back to 0: element 3 changed, the others stayed but are gone
	t_put(t_now, 6, 2, 0);
	CHECK_EQ(t_narrow(2, CHEATSEARCH_CHANGED, 0), 1);
	CHECK_EQ(cheatsearch_count(t_bits, T_BYTES / 2 / 8), 1);
	CHECK_EQ(t_narrow(2, CHEATSEARCH_CHANGED, 0), 0);
	CHECK_EQ(cheatsearch_count(t_bits, T_BYTES / 2 / 8), 0);
	// 32-bit elements at 4-byte steps
	memset(t_now, 0, sizeof(t_now));
	t_put(t_now, 8, 4, 0xDEADBEEF);
	CHECK_EQ(t_pass(4, CHEATSEARCH_EQUALS, 0xDEADBEEF), 1);
	CHECK(t_bit(2));
}

static void test_run(void)
{
	CHECK_EQ(cheatsearch_address(0), 0x02000000);
	CHECK_EQ(cheatsearch_address(0x3FFFF), 0x0203FFFF);
	CHECK_EQ(cheatsearch_address(0x40000), 0x03000000);
	CHECK_EQ(cheatsearch_address(CHEATSEARCH_BYTES - 1), 0x03007DFF);
	CHECK_EQ(cheatsearch_bits_bytes(1), 36800);
	CHECK_EQ(cheatsearch_bits_bytes(4), 9200);
}

static void test_numbers(void)
{
	uint8_t d[10];
	cheatsearch_decimal(4294967295u, d, 10);
	static const uint8_t big[10] = {4, 2, 9, 4, 9, 6, 7, 2, 9, 5};
	CHECK(memcmp(d, big, 10) == 0);
	cheatsearch_decimal(7, d, 3);
	CHECK(d[0] == 0 && d[1] == 0 && d[2] == 7);
	CHECK_EQ(cheatsearch_digits(1), 3);
	CHECK_EQ(cheatsearch_digits(2), 5);
	CHECK_EQ(cheatsearch_digits(4), 10);

	CHECK_EQ(cheatsearch_edit(0, 1, 2, 1), 1);
	CHECK_EQ(cheatsearch_edit(9, 1, 2, 1), 0);  // the digit wraps
	CHECK_EQ(cheatsearch_edit(0, 1, 2, -1), 9);
	CHECK_EQ(cheatsearch_edit(200, 1, 0, 1), 0);  // 300 passes 255: the digit to 0
	CHECK_EQ(cheatsearch_edit(155, 1, 0, 1), 255);
	CHECK_EQ(cheatsearch_edit(55, 1, 0, -1), 255);  // 955 passes it: the largest
	CHECK_EQ(cheatsearch_edit(100, 1, 0, 1), 200);
	CHECK_EQ(cheatsearch_edit(4294967295u, 4, 0, 1), 294967295u);
	CHECK_EQ(cheatsearch_edit(65535, 2, 4, 1), 65530);
	CHECK_EQ(cheatsearch_edit(12, 1, 3, 1), 12);  // no such digit
}

void test_cheatsearch(void)
{
	test_filters();
	test_narrowing();
	test_run();
	test_numbers();
}
