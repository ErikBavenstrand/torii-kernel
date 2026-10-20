// The checks behind safe file writes (source/safefile_scan.c).
#include <stdio.h>
#include <string.h>

#include "../../source/crc32.h"
#include "../../source/safefile_scan.h"
#include "../kernel/torii_test.h"
#include "tests.h"

// text with its CRC line, as safefile_write writes it, into out
static u32 sealed(char *out, u32 size, const char *text)
{
	u32 n = (u32)snprintf(out, size, "%s", text);
	return n + safefile_crc_line(out + n, crc32(text, n));
}

// Scans bytes in pieces of piece bytes
static void scan_in(struct safefile_scan *s, const char *bytes, u32 len, u32 piece)
{
	safefile_scan_begin(s);
	for (u32 at = 0; at < len; at += piece)
		safefile_scan_feed(s, bytes + at, len - at < piece ? len - at : piece);
	safefile_scan_end(s);
}

static void scan(struct safefile_scan *s, const char *bytes, u32 len)
{
	scan_in(s, bytes, len, len ? len : 1);
}

void test_safefile_crc_line(void)
{
	char line[32], copy[32];
	u32 crc;
	CHECK_EQ(safefile_crc_line(line, 0x1a2b3c4d), 16);
	CHECK(strcmp(line, "#crc32 1a2b3c4d\n") == 0);
	CHECK(safefile_parse_crc_line(line, 15, &crc) && crc == 0x1a2b3c4d);
	CHECK(safefile_parse_crc_line("#crc32 1A2B3C4D\r ", 17, &crc) && crc == 0x1a2b3c4d);
	CHECK(!safefile_parse_crc_line("#crc32 1a2b3c4", 14, &crc));
	CHECK(!safefile_parse_crc_line("#crc32 1a2b3c4dd", 16, &crc));
	CHECK(!safefile_parse_crc_line("#crc32 1a2b3c4g", 15, &crc));
	CHECK(!safefile_parse_crc_line("#crc64 1a2b3c4d", 15, &crc));
	snprintf(copy, sizeof(copy), " #crc32 1a2b3c4d");
	CHECK(!safefile_parse_crc_line(copy, 16, &crc));
}

void test_safefile_scan(void)
{
	char buf[256];
	struct safefile_scan s, t;
	const char *text = "100\t/a.gba\nNOR:b.gba\n";
	u32 len = sealed(buf, sizeof(buf), text);
	// Whole, and in every piece size: the same result
	scan(&s, buf, len);
	CHECK(s.valid && s.crc_line && !s.has_nul);
	CHECK_EQ(s.body, strlen(text));
	CHECK_EQ(s.size, len);
	CHECK_EQ(s.crc, crc32(buf, len));
	for (u32 piece = 1; piece < len; piece++) {
		scan_in(&t, buf, len, piece);
		CHECK(t.valid && t.body == s.body && t.crc == s.crc);
	}
	// Blank lines and a lost newline after the CRC line still hold
	char more[300];
	snprintf(more, sizeof(more), "%s\r\n\n", buf);
	scan(&t, more, (u32)strlen(more));
	CHECK(t.valid);
	scan(&t, buf, len - 1);
	CHECK(t.valid);
	// A changed byte, or a line after the CRC line, doesn't
	snprintf(more, sizeof(more), "%s", buf);
	more[1] = '9';
	scan(&t, more, len);
	CHECK(t.crc_line && !t.valid);
	snprintf(more, sizeof(more), "%s/c.gba\n", buf);
	scan(&t, more, (u32)strlen(more));
	CHECK(!t.crc_line && !t.valid);
	// No CRC line, an empty file, a NUL
	scan(&t, text, (u32)strlen(text));
	CHECK(!t.crc_line && !t.valid && t.size == strlen(text));
	scan(&t, "", 0);
	CHECK(!t.valid && t.size == 0);
	scan(&t, "a\0b\n", 4);
	CHECK(t.has_nul);
	// A CRC line in the middle isn't the file's
	snprintf(more, sizeof(more), "#crc32 00000000\n");
	scan(&t, more, (u32)strlen(more));
	CHECK(t.crc_line && t.valid && t.body == 0);  // the empty text's CRC is 0
	// A very long line doesn't overrun the scan's line
	memset(more, 'x', sizeof(more) - 1);
	more[sizeof(more) - 1] = 0;
	scan(&t, more, (u32)strlen(more));
	CHECK(!t.crc_line);
}

void test_safefile_pick(void)
{
	char a[256], b[256];
	struct safefile_scan m, k;
	const char *old_text = "1\t/old.gba\n", *new_text = "2\t/new.gba\n1\t/old.gba\n";
	u32 a_len = sealed(a, sizeof(a), new_text);
	u32 b_len = sealed(b, sizeof(b), new_text);
	// Both whole: NAME
	scan(&m, a, a_len);
	scan(&k, b, b_len);
	CHECK_EQ(safefile_pick(&m, &k), SAFEFILE_MAIN);
	CHECK_EQ(safefile_pick(&m, NULL), SAFEFILE_MAIN);
	// A cut during NAME leaves it empty: NAME.bak. Lines trimmed off its end
	// by hand (the start of NAME.bak) are the user's
	scan(&m, a, 0);
	scan(&k, b, b_len);
	CHECK_EQ(safefile_pick(&m, &k), SAFEFILE_BAK);
	for (u32 cut = 1; cut <= strlen(new_text); cut++) {
		scan(&m, a, cut);
		CHECK_EQ(safefile_pick(&m, &k), SAFEFILE_HAND);
	}
	// A cut during NAME.bak (written first): NAME holds
	scan(&m, a, a_len);
	scan(&k, b, 5);
	CHECK_EQ(safefile_pick(&m, &k), SAFEFILE_MAIN);
	// Edited by hand: no CRC line, or one left stale; even with no NAME.bak
	scan(&m, "3\t/hand.gba\n", 12);
	scan(&k, b, b_len);
	CHECK_EQ(safefile_pick(&m, &k), SAFEFILE_HAND);
	CHECK_EQ(safefile_pick(&m, NULL), SAFEFILE_HAND);
	char stale[256];
	snprintf(stale, sizeof(stale), "/hand.gba\n%s", a + strlen(new_text));
	scan(&m, stale, (u32)strlen(stale));
	CHECK(m.crc_line && !m.valid);
	CHECK_EQ(safefile_pick(&m, NULL), SAFEFILE_HAND);
	// NAME longer than NAME.bak's text, or different, is a hand edit too
	u32 old_len = sealed(b, sizeof(b), old_text);
	scan(&m, new_text, (u32)strlen(new_text));
	scan(&k, b, old_len);
	CHECK_EQ(safefile_pick(&m, &k), SAFEFILE_HAND);
	// Not text, or empty, with no NAME.bak: nothing
	scan(&m, "\0\0\0\0", 4);
	CHECK_EQ(safefile_pick(&m, NULL), SAFEFILE_NONE);
	scan(&k, b, old_len);
	CHECK_EQ(safefile_pick(&m, &k), SAFEFILE_BAK);
	scan(&m, "", 0);
	CHECK_EQ(safefile_pick(&m, NULL), SAFEFILE_NONE);
	CHECK_EQ(safefile_pick(NULL, NULL), SAFEFILE_NONE);
	// Only NAME.bak, or a NAME.bak that doesn't hold
	scan(&k, b, old_len);
	CHECK_EQ(safefile_pick(NULL, &k), SAFEFILE_BAK);
	scan(&k, b, old_len - 3);
	CHECK_EQ(safefile_pick(NULL, &k), SAFEFILE_NONE);
}

static const char *const lines[] = {"first\n", "a much longer second line\n", "\n", "last\n"};

static u32 line_of(void *ctx, u32 n, char *buf, u32 size)
{
	(void)ctx;
	if (n >= sizeof(lines) / sizeof(lines[0]))
		return 0;
	return (u32)snprintf(buf, size, "%s", lines[n]);
}

void test_safefile_lines(void)
{
	static struct safefile_lines l;
	char want[128] = "", got[128];
	for (u32 i = 0; i < sizeof(lines) / sizeof(lines[0]); i++)
		strcat(want, lines[i]);
	l.line = line_of;
	for (u32 piece = 1; piece <= 40; piece++) {
		for (int pass = 0; pass < 2; pass++) {  // twice, as safefile_write reads it
			u32 pos = 0, n;
			memset(got, 0, sizeof(got));
			while ((n = safefile_lines_fill(&l, pos, got + pos, piece)) != 0) {
				CHECK(n <= piece);
				pos += n;
			}
			CHECK_EQ(pos, strlen(want));
			CHECK(strcmp(got, want) == 0);
		}
	}
	// A position that isn't where the last call ended gives nothing
	safefile_lines_fill(&l, 0, got, 3);
	CHECK_EQ(safefile_lines_fill(&l, 5, got, 3), 0);
}
