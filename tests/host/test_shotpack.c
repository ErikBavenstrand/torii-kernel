// The screenshots' packing (source/shotpack.c): what the overlay packs, the
// kernel unpacks as it was, fed in pieces of any size.
#include <stdlib.h>
#include <string.h>

#include "../../source/shotpack.h"
#include "../kernel/torii_test.h"
#include "tests.h"

#define T_MAX 0xC000

static uint16_t t_packed[SHOTPACK_MAX_OUT(T_MAX) + 64];
static uint32_t t_room;  // halfwords a flush may still take

static int t_flush(struct shotpack_out *o)
{
	if (o->total + o->n > t_room)
		return 0;
	memcpy(t_packed + o->total, o->buf, o->n * 2);
	return 1;
}

// Packs the parts one after another; returns the packed halfwords, or 0 if
// a flush stopped it
static uint32_t t_pack(uint16_t *const *parts, const uint32_t *halves, uint32_t n, uint32_t room)
{
	static uint16_t head[SHOTPACK_HASH], buf[100];  // a buffer of an odd size
	struct shotpack_out o = {buf, sizeof(buf) / 2, 0, 0, t_flush};
	t_room = room;
	for (uint32_t k = 0; k < n; k++)
		if (!shotpack_pack(parts[k], halves[k], head, &o))
			return 0;
	if (o.n && !t_flush(&o))
		return 0;
	return o.total + o.n;
}

static int t_round_trip(uint16_t *const *parts, const uint32_t *halves, uint32_t n, uint32_t piece)
{
	uint32_t packed = t_pack(parts, halves, n, sizeof(t_packed) / 2);
	if (!packed)
		return 0;
	static uint16_t out[SHOTPACK_PARTS_MAX][T_MAX];
	struct shotpack_unpack u;
	u.parts = n;
	for (uint32_t k = 0; k < n; k++) {
		u.part[k] = out[k];
		u.halves[k] = halves[k];
		memset(out[k], 0xEE, sizeof(out[k]));
	}
	shotpack_unpack_start(&u);
	int done = 0;
	for (uint32_t at = 0; at < packed && !done; at += piece) {
		done = shotpack_unpack_feed(&u, t_packed + at, packed - at < piece ? packed - at : piece);
		if (done < 0)
			return 0;
	}
	if (!done)
		return 0;
	for (uint32_t k = 0; k < n; k++)
		if (memcmp(out[k], parts[k], halves[k] * 2))
			return 0;
	return (int)packed;
}

void test_shotpack(void)
{
	static uint16_t vram[T_MAX], pal[0x200], oam[0x200];
	// VRAM-like: blank tiles, a map of few tiles, noise, a long run
	srand(7);
	for (uint32_t i = 0; i < T_MAX; i++)
		vram[i] = i < 0x2000   ? (uint16_t)((i / 16) % 7 == 0 ? rand() : 0)
			  : i < 0x3000 ? (uint16_t)(0x1000 | (i % 32) | ((i / 64) % 3) << 5)
			  : i < 0x5000 ? (uint16_t)rand()
				       : 0;
	for (uint32_t i = 0; i < 0x200; i++) {
		pal[i] = (uint16_t)(i < 0x40 ? rand() & 0x7FFF : 0);
		oam[i] = (uint16_t)(i % 4 == 0 ? 0x200 : i);
	}
	uint16_t *const parts[3] = {pal, oam, vram};
	const uint32_t halves[3] = {0x200, 0x200, T_MAX};
	int packed = t_round_trip(parts, halves, 3, 1);
	CHECK(packed > 0);
	CHECK(t_round_trip(parts, halves, 3, 4097) == packed);
	CHECK(t_round_trip(parts, halves, 3, 100000) == packed);
	// It packs: the noise alone (0x2000 halfwords) stays as it is
	CHECK((uint32_t)packed < 0x2000 + 0x1000);
	// All noise: no bigger than SHOTPACK_MAX_OUT
	for (uint32_t i = 0; i < T_MAX; i++)
		vram[i] = (uint16_t)rand();
	packed = t_round_trip(parts + 2, halves + 2, 1, 333);
	CHECK(packed > 0 && (uint32_t)packed <= SHOTPACK_MAX_OUT(T_MAX));
	// A run longer than a token takes, and a part of one halfword
	for (uint32_t i = 0; i < T_MAX; i++)
		vram[i] = 0x1234;
	CHECK(t_round_trip(parts + 2, halves + 2, 1, 7) > 0);
	const uint32_t one[1] = {1};
	CHECK(t_round_trip(parts + 2, one, 1, 1) > 0);
	// No room: the packing stops
	CHECK_EQ(t_pack(parts + 2, halves + 2, 1, 4), 0u);  // the run packs into 6
	// A copy from before the part's start doesn't unpack
	const uint16_t bad[] = {1, 5, 0x8003, 2};
	uint16_t out[8];
	struct shotpack_unpack u = {{out}, {8}, 1, 0, 0, 0, 0, 0};
	shotpack_unpack_start(&u);
	CHECK_EQ(shotpack_unpack_feed(&u, bad, 4), -1);
}
