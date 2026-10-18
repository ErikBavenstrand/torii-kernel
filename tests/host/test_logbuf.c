// The SD log's ring buffer and file rules (source/logbuf.c).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../source/logbuf.h"
#include "../kernel/torii_test.h"
#include "tests.h"

// Everything a flush would take, in order, as one string.
static char *drain(log_ring *r)
{
	static char out[LOG_RING_SIZE + 1];
	u32 len = 0;
	const char *text;
	u32 n;
	while ((n = log_ring_peek(r, &text)) > 0) {
		CHECK(len + n <= LOG_RING_SIZE);
		memcpy(out + len, text, n);
		len += n;
		log_ring_drop(r, n);
	}
	out[len] = 0;
	return out;
}

static log_ring *new_ring(void)
{
	log_ring *r = malloc(sizeof(*r));
	memset(r, 0, sizeof(*r));
	return r;
}

void test_log_ring_keeps_order(void)
{
	log_ring *r = new_ring();
	CHECK_EQ(strlen(drain(r)), 0);
	log_ring_put(r, "one\n", 4);
	log_ring_put(r, "two\n", 4);
	CHECK(strcmp(drain(r), "one\ntwo\n") == 0);
	CHECK_EQ(r->lost, 0);
	// Around the end of the buffer, in two pieces.
	for (int i = 0; i < 1000; i++)
		log_ring_put(r, "abc\n", 4);
	drain(r);
	log_ring_put(r, "wrapped line\n", 13);
	CHECK(strcmp(drain(r), "wrapped line\n") == 0);
	free(r);
}

void test_log_ring_drops_whole_lines(void)
{
	log_ring *r = new_ring();
	char line[32];
	// 10-byte lines: 1000 of them overflow the 4096-byte ring.
	for (int i = 0; i < 1000; i++) {
		snprintf(line, sizeof(line), "line %04d\n", i);
		log_ring_put(r, line, 10);
	}
	CHECK(r->head - r->tail <= LOG_RING_SIZE);
	char *text = drain(r);
	// The oldest kept text starts a line, and the newest is all there.
	CHECK(strncmp(text, "line ", 5) == 0);
	CHECK(strcmp(text + strlen(text) - 10, "line 0999\n") == 0);
	CHECK_EQ(r->lost + strlen(text), 10000);
	free(r);
}

void test_log_ring_long_line(void)
{
	log_ring *r = new_ring();
	char *big = malloc(LOG_RING_SIZE * 2);
	memset(big, 'x', LOG_RING_SIZE * 2);
	big[LOG_RING_SIZE * 2 - 1] = '\n';
	log_ring_put(r, "first\n", 6);
	log_ring_put(r, big, LOG_RING_SIZE * 2);
	// A line longer than the ring leaves nothing whole: it all goes.
	CHECK(r->head - r->tail <= LOG_RING_SIZE);
	log_ring_put(r, "after\n", 6);
	char *text = drain(r);
	CHECK(strcmp(text + strlen(text) - 6, "after\n") == 0);
	free(big);
	free(r);
}

void test_log_ring_take_lines(void)
{
	log_ring *r = new_ring();
	char out[LOG_RING_SIZE];
	// Lines across the end of the buffer come out whole and in order.
	for (int i = 0; i < 400; i++)
		log_ring_put(r, "0123456789\n", 11);
	drain(r);
	log_ring_put(r, "line one, longer than the rest\nline two\n", 40);
	u32 n = log_ring_take_lines(r, out, 35);
	CHECK_EQ(n, 31);
	CHECK(memcmp(out, "line one, longer than the rest\n", 31) == 0);
	log_ring_drop(r, n);
	CHECK_EQ(log_ring_take_lines(r, out, sizeof(out)), 9);
	CHECK(memcmp(out, "line two\n", 9) == 0);
	log_ring_drop(r, 9);
	CHECK_EQ(log_ring_take_lines(r, out, sizeof(out)), 0);
	// A line longer than max comes out in max pieces.
	log_ring_put(r, "abcdefghij\n", 11);
	CHECK_EQ(log_ring_take_lines(r, out, 4), 4);
	free(r);
}

void test_log_ring_tail(void)
{
	log_ring *r = new_ring();
	char out[64];
	log_ring_put(r, "aaaa\nbbbb\ncccc\n", 15);
	CHECK_EQ(log_ring_tail(r, out, sizeof(out)), 15);
	CHECK_EQ(log_ring_tail(r, out, 12), 10);  // starts at a line
	CHECK(memcmp(out, "bbbb\ncccc\n", 10) == 0);
	CHECK_EQ(log_ring_tail(r, out, 4), 0);
	// A flush leaves the text there for a crash dump.
	drain(r);
	CHECK_EQ(log_ring_tail(r, out, sizeof(out)), 15);
	// Past a wrap, only what the ring still holds.
	for (int i = 0; i < 2000; i++)
		log_ring_put(r, "0123456789\n", 11);
	char *big = malloc(LOG_RING_SIZE * 2);
	u32 n = log_ring_tail(r, big, LOG_RING_SIZE * 2);
	CHECK(n <= LOG_RING_SIZE);
	CHECK_EQ(n % 11, 0);
	CHECK(memcmp(big, "0123456789\n", 11) == 0);
	free(big);
	free(r);
}

void test_log_header(void)
{
	char h[LOG_HEADER_LEN];
	log_header_make(h, 1);
	CHECK(memcmp(h, "torii log 00000001", 18) == 0);
	CHECK_EQ(h[LOG_HEADER_LEN - 1], '\n');
	CHECK_EQ(log_header_seq(h, sizeof(h)), 1);
	log_header_make(h, 12345678);
	CHECK_EQ(log_header_seq(h, sizeof(h)), 12345678);
	CHECK_EQ(log_header_seq(h, sizeof(h) - 1), 0);  // too short
	h[12] = 'x';
	CHECK_EQ(log_header_seq(h, sizeof(h)), 0);
	char zeros[LOG_HEADER_LEN] = {0};  // a rotation cut off after zeroing
	CHECK_EQ(log_header_seq(zeros, sizeof(zeros)), 0);
}

void test_log_pick_current(void)
{
	u32 none[LOG_FILES] = {0};
	CHECK_EQ(log_pick_current(none), -1);
	u32 seqs[LOG_FILES] = {5, 8, 0, 7};
	CHECK_EQ(log_pick_current(seqs), 1);
	u32 first[LOG_FILES] = {0, 0, 3, 0};
	CHECK_EQ(log_pick_current(first), 2);
	// Across the wrap, 1 and 2 come after 99999998 and 99999999.
	u32 wrapped[LOG_FILES] = {LOG_SEQ_MAX - 1, LOG_SEQ_MAX, 1, 2};
	CHECK_EQ(log_pick_current(wrapped), 3);
	u32 just_wrapped[LOG_FILES] = {LOG_SEQ_MAX - 2, 1, LOG_SEQ_MAX - 1, LOG_SEQ_MAX};
	CHECK_EQ(log_pick_current(just_wrapped), 1);
	u32 at_max[LOG_FILES] = {LOG_SEQ_MAX - 3, LOG_SEQ_MAX - 1, LOG_SEQ_MAX - 2, LOG_SEQ_MAX};
	CHECK_EQ(log_pick_current(at_max), 3);
}

void test_log_next_seq(void)
{
	CHECK_EQ(log_next_seq(1), 2);
	CHECK_EQ(log_next_seq(LOG_SEQ_MAX - 1), LOG_SEQ_MAX);
	CHECK_EQ(log_next_seq(LOG_SEQ_MAX), 1);
	// Every sequence it gives fits the header's 8 digits.
	char h[LOG_HEADER_LEN];
	u32 seq = LOG_SEQ_MAX - 2;
	for (int i = 0; i < 5; i++) {
		seq = log_next_seq(seq);
		log_header_make(h, seq);
		CHECK_EQ(log_header_seq(h, sizeof(h)), seq);
	}
}

// Feeds file to a log_end in pieces of step bytes, from offset skew of a
// buffer, so the word path meets every alignment.
static log_end scan(const u8 *file, u32 size, u32 step, u32 skew)
{
	static u8 piece[600 + 4];
	log_end e;
	log_end_start(&e);
	for (u32 at = 0; at < size; at += step) {
		u32 n = (size - at < step) ? size - at : step;
		memcpy(piece + skew, file + at, n);
		log_end_feed(&e, (const char *)piece + skew, n);
	}
	return e;
}

void test_log_end(void)
{
	u8 *file = malloc(LOG_FILE_SIZE);
	u32 ends[] = {LOG_HEADER_LEN, LOG_HEADER_LEN + 1, 1000, 0x8000, LOG_FILE_SIZE - 1, LOG_FILE_SIZE};
	u32 steps[] = {1, 7, 512, 600};
	for (u32 i = 0; i < sizeof(ends) / sizeof(*ends); i++) {
		for (u32 j = 0; j < sizeof(steps) / sizeof(*steps); j++) {
			for (u32 skew = 0; skew < 4; skew++) {
				memset(file, 0, LOG_FILE_SIZE);
				memset(file, 'a', ends[i]);
				log_end e = scan(file, LOG_FILE_SIZE, steps[j], skew);
				CHECK_EQ(log_end_offset(&e), ends[i]);
				CHECK(!e.junk);
				if (ends[i] < LOG_FILE_SIZE - 1) {
					// One junk byte anywhere after the end.
					u32 at = ends[i] + 1 + (LOG_FILE_SIZE - ends[i] - 2) * j / 3;
					file[at] = 0x5A;
					e = scan(file, LOG_FILE_SIZE, steps[j], skew);
					CHECK(e.junk);
					CHECK_EQ(log_end_offset(&e), ends[i]);
				}
			}
		}
	}
	// Zeros in the header don't end the text.
	memset(file, 0, LOG_FILE_SIZE);
	memset(file + 4, 'a', 100);
	log_end e = scan(file, LOG_FILE_SIZE, 512, 0);
	CHECK_EQ(log_end_offset(&e), 104);
	CHECK(!e.junk);
	// What v0.2.0 left on the real cart: header, text, then the SD buffer's
	// leftovers, with zeros among them.
	srand(1);
	for (u32 i = 0; i < LOG_FILE_SIZE; i++)
		file[i] = (i % 97 < 40) ? 0 : rand();
	memset(file, 'a', 3000);
	e = scan(file, LOG_FILE_SIZE, 512, 0);
	CHECK(e.junk);
	// The last line's start, also when a flush was cut off inside a line.
	memset(file, 0, LOG_FILE_SIZE);
	memset(file, 'a', 3000);
	file[999] = '\n';
	file[2499] = '\n';
	for (u32 j = 0; j < sizeof(steps) / sizeof(*steps); j++) {
		for (u32 skew = 0; skew < 4; skew++) {
			e = scan(file, LOG_FILE_SIZE, steps[j], skew);
			CHECK_EQ(log_end_offset(&e), 3000);
			CHECK_EQ(e.line, 2500);
		}
	}
	file[2999] = '\n';  // whole lines: the last starts at the end
	e = scan(file, LOG_FILE_SIZE, 512, 0);
	CHECK_EQ(e.line, 3000);
	memset(file, 0, LOG_FILE_SIZE);
	memset(file, 'a', 100);  // no line ends after the header
	e = scan(file, LOG_FILE_SIZE, 512, 0);
	CHECK_EQ(e.line, LOG_HEADER_LEN);
	// What a cut-off flush leaves (less than a line after the last '\n') is
	// not junk; junk with no zero byte in it, here 0xFF to the end, is.
	memset(file, 0, LOG_FILE_SIZE);
	memset(file, 'a', 3000);
	file[2899] = '\n';
	e = scan(file, LOG_FILE_SIZE, 512, 0);
	CHECK(!log_end_is_junk(&e));
	file[2899] = 'a';
	file[3000 - LOG_LINE_MAX - 1] = '\n';
	e = scan(file, LOG_FILE_SIZE, 512, 0);
	CHECK(!log_end_is_junk(&e));  // a partial line of LOG_LINE_MAX bytes
	file[3000 - LOG_LINE_MAX - 1] = 'a';
	file[3000 - LOG_LINE_MAX - 2] = '\n';
	e = scan(file, LOG_FILE_SIZE, 512, 0);
	CHECK(log_end_is_junk(&e));
	memset(file + 2900, 0xFF, LOG_FILE_SIZE - 2900);
	e = scan(file, LOG_FILE_SIZE, 512, 0);
	CHECK_EQ(log_end_offset(&e), LOG_FILE_SIZE);
	CHECK(log_end_is_junk(&e));
	file[LOG_FILE_SIZE - 1] = '\n';  // a full file of whole lines
	memset(file + 2500, 'b', LOG_FILE_SIZE - 2501);
	for (u32 i = 2500 + 100; i < LOG_FILE_SIZE; i += 100)
		file[i] = '\n';
	e = scan(file, LOG_FILE_SIZE, 512, 0);
	CHECK(!log_end_is_junk(&e));
	// Nothing fed: the end is the header's.
	log_end_start(&e);
	CHECK_EQ(log_end_offset(&e), LOG_HEADER_LEN);
	free(file);
}
