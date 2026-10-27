// The in-game screenshots' packing (shotpack.h).
#include "shotpack.h"

static int put(struct shotpack_out *o, uint32_t h)
{
	o->buf[o->n++] = (uint16_t)h;
	if (o->n < o->size)
		return 1;
	if (!o->flush(o))
		return 0;
	o->total += o->n;
	o->n = 0;
	return 1;
}

// The literals from..to-1 of in, in runs of at most SHOTPACK_RUN
static int literals(const volatile uint16_t *in, uint32_t from, uint32_t to, struct shotpack_out *o)
{
	while (from < to) {
		uint32_t n = to - from < SHOTPACK_RUN ? to - from : SHOTPACK_RUN;
		if (!put(o, n))
			return 0;
		for (uint32_t i = 0; i < n; i++)
			if (!put(o, in[from + i]))
				return 0;
		from += n;
	}
	return 1;
}

static uint32_t hash(uint32_t a, uint32_t b)
{
	return ((a << 16 | b) * 0x9E3779B1u) >> 22;
}
_Static_assert(SHOTPACK_HASH == 1 << 10, "hash() gives 10 bits");

// The length of the match of in[at...] at in[from...], up to max
static uint32_t match(const volatile uint16_t *in, uint32_t from, uint32_t at, uint32_t max)
{
	uint32_t len = 0;
	while (len < max && in[from + len] == in[at + len])
		len++;
	return len;
}

// Greedy: the table gives the last place the next two halfwords started at;
// a repeat of the halfword before is tried too
int shotpack_pack(const volatile uint16_t *in, uint32_t n, uint16_t *head, struct shotpack_out *out)
{
	for (uint32_t i = 0; i < SHOTPACK_HASH; i++)
		head[i] = 0;
	uint32_t i = 0, lit = 0;
	while (i + 1 < n) {
		uint32_t a = in[i], b = in[i + 1];
		uint32_t h = hash(a, b);
		uint32_t cand = head[h];  // its place + 1, 0 for none
		head[h] = (uint16_t)(i + 1);
		uint32_t max = n - i < SHOTPACK_RUN ? n - i : SHOTPACK_RUN;
		uint32_t len = 0, from = 0;
		if (cand && i - (cand - 1) <= 0xFFFF) {
			from = cand - 1;
			len = match(in, from, i, max);
		}
		if (len < SHOTPACK_MIN && i && in[i - 1] == a && a == b) {
			from = i - 1;
			len = match(in, from, i, max);
		}
		if (len < SHOTPACK_MIN) {
			i++;
			continue;
		}
		if (!literals(in, lit, i, out) || !put(out, 0x8000u | len) || !put(out, i - from))
			return 0;
		i += len;
		lit = i;
	}
	return literals(in, lit, n, out);
}

void shotpack_unpack_start(struct shotpack_unpack *u)
{
	u->at = u->made = u->literals = u->copy = u->need_dist = 0;
}

int shotpack_unpack_feed(struct shotpack_unpack *u, const uint16_t *in, uint32_t n)
{
	for (uint32_t k = 0; k < n; k++) {
		if (u->at >= u->parts)
			return 1;
		uint32_t h = in[k];
		uint16_t *p = u->part[u->at];
		uint32_t left = u->halves[u->at] - u->made;
		if (u->literals) {
			p[u->made++] = (uint16_t)h;
			u->literals--;
		} else if (u->need_dist) {
			u->need_dist = 0;
			if (h == 0 || h > u->made || u->copy > left)
				return -1;
			for (uint32_t i = 0; i < u->copy; i++, u->made++)
				p[u->made] = p[u->made - h];
			u->copy = 0;
		} else if (h & 0x8000u) {
			u->copy = h & 0x7FFFu;
			u->need_dist = 1;
			if (u->copy < SHOTPACK_MIN)
				return -1;
			continue;
		} else {
			if (h == 0 || h > left)
				return -1;
			u->literals = h;
			continue;
		}
		if (!u->literals && u->made == u->halves[u->at]) {
			u->at++;
			u->made = 0;
		}
	}
	return u->at >= u->parts;
}
