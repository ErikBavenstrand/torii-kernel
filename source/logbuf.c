#include <string.h>

#include "logbuf.h"

#define MASK (LOG_RING_SIZE - 1)

void log_ring_put(log_ring *r, const char *s, u32 n)
{
	if (n > LOG_RING_SIZE) {  // only the end of it fits
		r->lost += n - LOG_RING_SIZE;
		s += n - LOG_RING_SIZE;
		n = LOG_RING_SIZE;
	}
	for (u32 i = 0; i < n; i++)
		r->data[(r->head + i) & MASK] = s[i];
	r->head += n;
	if (r->head - r->tail <= LOG_RING_SIZE)
		return;
	// Full: drop the oldest text, then the rest of the line it cut. The byte
	// before head - LOG_RING_SIZE is already overwritten, so look forward.
	u32 tail = r->head - LOG_RING_SIZE;
	while (tail != r->head && r->data[tail & MASK] != '\n')
		tail++;
	if (tail != r->head)
		tail++;
	r->lost += tail - r->tail;
	r->tail = tail;
}

u32 log_ring_peek(const log_ring *r, const char **text)
{
	u32 n = r->head - r->tail;
	u32 start = r->tail & MASK;
	*text = &r->data[start];
	return (n < LOG_RING_SIZE - start) ? n : LOG_RING_SIZE - start;
}

void log_ring_drop(log_ring *r, u32 n)
{
	u32 have = r->head - r->tail;
	r->tail += (n < have) ? n : have;
}

u32 log_ring_take_lines(const log_ring *r, char *out, u32 max)
{
	u32 n = r->head - r->tail;
	if (n > max) {
		n = max;
		while (n && r->data[(r->tail + n - 1) & MASK] != '\n')
			n--;
		if (!n)  // no line ends within max: take max
			n = max;
	}
	for (u32 i = 0; i < n; i++)
		out[i] = r->data[(r->tail + i) & MASK];
	return n;
}

u32 log_ring_tail(const log_ring *r, char *out, u32 max)
{
	// Text a flush took stays in the ring until new text overwrites it.
	u32 kept = (r->head < LOG_RING_SIZE) ? r->head : LOG_RING_SIZE;
	u32 n = (kept < max) ? kept : max;
	u32 start = r->head - n;
	if (start != 0) {  // begin at a line, not inside one
		if (n < LOG_RING_SIZE) {
			while (start != r->head && r->data[(start - 1) & MASK] != '\n')
				start++;
		} else {  // the byte before start is overwritten: look forward
			while (start != r->head && r->data[start & MASK] != '\n')
				start++;
			if (start != r->head)
				start++;
		}
		n = r->head - start;
	}
	for (u32 i = 0; i < n; i++)
		out[i] = r->data[(start + i) & MASK];
	return n;
}

static const char header_prefix[] = "torii log ";
#define PREFIX_LEN (sizeof(header_prefix) - 1)
#define SEQ_DIGITS 8

void log_header_make(char *out, u32 seq)
{
	memcpy(out, header_prefix, PREFIX_LEN);
	for (int i = SEQ_DIGITS - 1; i >= 0; i--) {
		out[PREFIX_LEN + i] = '0' + seq % 10;
		seq /= 10;
	}
	memset(out + PREFIX_LEN + SEQ_DIGITS, ' ', LOG_HEADER_LEN - PREFIX_LEN - SEQ_DIGITS - 1);
	out[LOG_HEADER_LEN - 1] = '\n';
}

u32 log_header_seq(const char *buf, u32 n)
{
	if (n < LOG_HEADER_LEN || memcmp(buf, header_prefix, PREFIX_LEN) != 0 || buf[LOG_HEADER_LEN - 1] != '\n')
		return 0;
	u32 seq = 0;
	for (u32 i = 0; i < SEQ_DIGITS; i++) {
		char c = buf[PREFIX_LEN + i];
		if (c < '0' || c > '9')
			return 0;
		seq = seq * 10 + (c - '0');
	}
	return seq;
}

int log_pick_current(const u32 seqs[LOG_FILES])
{
	int best = -1;
	for (int i = 0; i < LOG_FILES; i++) {
		if (seqs[i] && (best < 0 || seqs[i] > seqs[best]))
			best = i;
	}
	return best;
}

u32 log_find_end(int (*byte_at)(void *ctx, u32 offset), void *ctx, u32 size)
{
	// The answer is the first offset holding zero, in [LOG_HEADER_LEN, size].
	u32 lo = LOG_HEADER_LEN, hi = size;
	while (lo < hi) {
		u32 mid = lo + (hi - lo) / 2;
		if (byte_at(ctx, mid) > 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo;
}
