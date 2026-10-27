// The in-game screenshots' packing (v0.8): the overlay packs the game's
// palettes, OAM and VRAM into cart SRAM as it takes a screenshot, the kernel
// unpacks them at its next boot. Hardware-free: the overlay, the kernel and
// the host tests build it.
//
// Halfwords in, halfwords out. A token is a halfword c: below 0x8000, c
// literal halfwords follow it (1 to 0x7FFF); else a copy of c & 0x7FFF
// halfwords (SHOTPACK_MIN to 0x7FFF) from d halfwords back, d the halfword
// after c (1 to 0xFFFF), which may overlap what it makes (d 1 repeats a
// halfword). A copy never reaches before the start of the part it is in.
#ifndef TORII_SHOTPACK_H
#define TORII_SHOTPACK_H

#include <stdint.h>

#define SHOTPACK_HASH 1024  // the packer's table, in halfwords
#define SHOTPACK_MIN 3  // the shortest copy
#define SHOTPACK_RUN 0x7FFF  // the longest literal run or copy
#define SHOTPACK_PARTS_MAX 4
// The most halfwords n halfwords pack into (all literals)
#define SHOTPACK_MAX_OUT(n) ((n) + (n) / SHOTPACK_RUN + 1)

// Where packed halfwords go: buf holds size halfwords; flush takes the n in
// it and returns 0 to stop the packing (no room left)
struct shotpack_out {
	uint16_t *buf;
	uint32_t size, n;
	uint32_t total;  // halfwords flushed so far
	int (*flush)(struct shotpack_out *o);
};

// Packs n halfwords (at most 0xFFFF) of in, a part on its own, through out
// (its buffer flushed only when full); head is SHOTPACK_HASH halfwords of
// scratch. Returns 0 if a flush stopped it.
int shotpack_pack(const volatile uint16_t *in, uint32_t n, uint16_t *head, struct shotpack_out *out);

// Unpacking, fed the packed halfwords a piece at a time into the parts, in
// the order they were packed
struct shotpack_unpack {
	uint16_t *part[SHOTPACK_PARTS_MAX];
	uint32_t halves[SHOTPACK_PARTS_MAX];
	uint32_t parts;
	// where it is: the part, halfwords made in it, and a token not finished
	uint32_t at, made, literals, copy, need_dist;
};

void shotpack_unpack_start(struct shotpack_unpack *u);
// n packed halfwords; returns 1 once every part is whole (what follows is
// padding), 0 for more, -1 for a token that doesn't fit the parts
int shotpack_unpack_feed(struct shotpack_unpack *u, const uint16_t *in, uint32_t n);

#endif
