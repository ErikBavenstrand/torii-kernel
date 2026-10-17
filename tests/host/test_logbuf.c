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
}

static int reads;
static int byte_at(void *ctx, u32 offset)
{
	reads++;
	return ((const u8 *)ctx)[offset];
}

void test_log_find_end(void)
{
	u8 *file = malloc(LOG_FILE_SIZE);
	u32 ends[] = {LOG_HEADER_LEN, LOG_HEADER_LEN + 1, 1000, 0x8000, LOG_FILE_SIZE - 1, LOG_FILE_SIZE};
	for (u32 i = 0; i < sizeof(ends) / sizeof(*ends); i++) {
		memset(file, 0, LOG_FILE_SIZE);
		memset(file, 'a', ends[i]);
		reads = 0;
		CHECK_EQ(log_find_end(byte_at, file, LOG_FILE_SIZE), ends[i]);
		CHECK(reads <= 17);  // a binary search over 64 KB
	}
	free(file);
}
