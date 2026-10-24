// The kernel's printf formatting (source/fmt.c) against the host's snprintf,
// for every kind of format the kernel uses and a random sweep of numbers
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../source/fmt.h"
#include "../kernel/torii_test.h"
#include "tests.h"

static int ours(char *out, size_t room, const char *format, ...) __attribute__((format(printf, 3, 4)));
static int theirs(char *out, size_t room, const char *format, ...) __attribute__((format(printf, 3, 4)));

static int theirs(char *out, size_t room, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	int n = vsnprintf(out, room, format, ap);
	va_end(ap);
	return n;
}

static int ours(char *out, size_t room, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	int n = fmt_vsnprintf(out, room, format, ap);
	va_end(ap);
	return n;
}

#define SAME(room, ...) \
	do { \
		char a[64], b[64]; \
		memset(a, 'x', sizeof(a)); \
		memset(b, 'x', sizeof(b)); \
		int na = ours(a, room, __VA_ARGS__); \
		int nb = theirs(b, room, __VA_ARGS__); \
		CHECK_EQ(na, nb); \
		if (memcmp(a, b, sizeof(a))) \
			printf("  fmt: \"%s\" is \"%.*s\"\n", b, (int)sizeof(a), a); \
		CHECK(!memcmp(a, b, sizeof(a))); \
	} while (0)

void test_fmt_formats(void)
{
	const char *null = NULL;
	(void)null;
	SAME(64, "plain");
	SAME(64, "%s and %s", "a", "");
	SAME(64, "%d %d %d %i", 0, -1, 2147483647, -2147483647 - 1);
	SAME(64, "%u %lu %ld", 4000000000u, 4294967295ul, -5l);
	SAME(64, "%x %X %lx %lX", 0xabcu, 0xabcu, 0xdeadbeeful, 0xdeadbeeful);
	SAME(64, "%08lX %04X %02X %2X", 0x1234ul, 0xau, 0xffu, 0xau);
	SAME(64, "%02u %04u %5lu %-6lu| %-4d| %05d", 7u, 12u, 42ul, 9ul, -3, -42);
	SAME(64, "%.*s|%.4s|%.12s|%.99s", 3, "abcdef", "ab", "abcdefghijklmnop", "x");
	SAME(64, "%c%c %3c|%-3c|", 'a', 'b', 'c', 'd');
	SAME(64, "%10s|%-10s|%*s|%-*s|", "r", "l", 5, "s", 4, "t");
	SAME(64, "100%% %d%%", 5);
	SAME(64, "%.0d|%.3d|%5.3d", 0, 7, -7);
	SAME(64, "%zu %hu %hhu", (size_t)77, (unsigned short)65535, (unsigned char)200);
	SAME(64, "%llu %lld", 18446744073709551615ull, -9223372036854775807ll);
	// Truncation: the length is the whole result's, the text is cut and ended
	SAME(5, "%s", "truncated text");
	SAME(1, "%d", 12345);
	SAME(8, "%08lX%s", 0x1ul, "tail");
	char z[4] = {'q', 'q', 'q', 'q'};
	CHECK_EQ(ours(z, 0, "%d", 99), 2);
	CHECK_EQ(z[0], 'q');
}

#pragma GCC diagnostic ignored "-Wformat-nonliteral"
void test_fmt_random(void)
{
	static const char *const forms[] = {"%d",  "%u",   "%x",   "%X",  "%08X", "%-9d|",
					    "%5u", "%03d", "%.4u", "%lu", "%ld"};
	unsigned seed = 12345;
	for (int i = 0; i < 20000; i++) {
		seed = seed * 1103515245u + 12345u;
		unsigned v = seed ^ (seed >> 7) * 2654435761u;
		if (i % 5 == 0)
			v %= 1000;
		const char *f = forms[i % (sizeof(forms) / sizeof(forms[0]))];
		char a[48], b[48];
		int na, nb;
		if (f[1] == 'l' || f[2] == 'l') {
			long lv = (long)(int)v;
			na = ours(a, sizeof(a), f, lv);
			nb = theirs(b, sizeof(b), f, lv);
		} else {
			na = ours(a, sizeof(a), f, v);
			nb = theirs(b, sizeof(b), f, v);
		}
		if (na != nb || strcmp(a, b)) {
			printf("  fmt: %s of %u is \"%s\", not \"%s\"\n", f, v, a, b);
			CHECK(0);
			return;
		}
	}
}

// fmt_strtoul against the host's strtoul: its value and where it stops
void test_fmt_strtoul(void)
{
	static const char *const in[] = {"0",          "12",       "  42\t",    "+7",          "-1",
					 "ffffffff",   "FFFFFFFF", "100000000", "99999999999", "0x1f",
					 "0X1F",       "0x",       "0xg",       "x1",          "",
					 "  ",         "-",        "12ab",      "deadBEEF\t3", "4294967295",
					 "4294967296", "000123",   "\n9",       "1a2b3c4d",    "7fffffff"};
	static const int bases[] = {10, 16, 8, 2, 36};
	for (unsigned i = 0; i < sizeof(in) / sizeof(*in); i++)
		for (unsigned b = 0; b < sizeof(bases) / sizeof(*bases); b++) {
			char *e1, *e2;
			unsigned long a = fmt_strtoul(in[i], &e1, bases[b]);
			unsigned long t = strtoul(in[i], &e2, bases[b]);
			if (a != t || e1 != e2)
				printf("  fmt_strtoul(\"%s\", %d): %lu at +%d, strtoul %lu at +%d\n", in[i], bases[b],
				       a, (int)(e1 - in[i]), t, (int)(e2 - in[i]));
			CHECK(a == t && e1 == e2);
		}
}
