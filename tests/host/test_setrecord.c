// The settings record's two copies (source/setrecord.c), and a power cut at
// every erase and program of a save sequence.
#include <string.h>

#include "../../source/setrecord.h"
#include "../kernel/torii_test.h"
#include "tests.h"

#define KERNEL_SLOTS 65  // assress_max

static u16 copy_a[SET_RECORD_SLOTS], copy_b[SET_RECORD_SLOTS];

static void record(u16 *r, u16 value, u32 seq)
{
	for (u32 i = 0; i < KERNEL_SLOTS; i++)
		r[i] = (u16)(value + i);
	set_record_seal(r, KERNEL_SLOTS, seq);
}

// An older kernel's write: its slots, the rest 0
static void legacy(u16 *r, u16 value)
{
	memset(r, 0, SET_RECORD_BYTES);
	for (u32 i = 0; i < KERNEL_SLOTS; i++)
		r[i] = (u16)(value + i);
}

// What the kernel reads: the picked copy, or A as it is
static u16 read_value(void)
{
	u32 seq;
	u32 pick = set_record_pick(copy_a, copy_b, &seq);
	return pick == SET_COPY_B ? copy_b[0] : copy_a[0];
}

// A save as Save_SET_info runs it, cut off after `steps` operations (an
// erase, or one halfword programmed); the last programmed one may be cut
// half way (`half`). Returns 1 if it ran to the end.
static u32 save(u16 value, u32 steps, u32 half)
{
	u32 seq;
	u32 pick = set_record_pick(copy_a, copy_b, &seq);
	u16 *targets[2];
	u32 n = 0;
	if (pick == SET_COPY_A)
		targets[n++] = copy_b;
	else if (pick == SET_COPY_B)
		targets[n++] = copy_a;
	else {
		targets[n++] = copy_b;
		targets[n++] = copy_a;
	}
	u16 next[SET_RECORD_SLOTS];
	record(next, value, seq + 1);
	for (u32 t = 0; t < n; t++) {
		if (!steps--)
			return 0;
		memset(targets[t], 0xFF, SET_RECORD_BYTES);  // the erase
		for (u32 i = 0; i < SET_RECORD_SLOTS; i++) {
			if (!steps--) {
				if (half)  // some of the word's bits programmed
					targets[t][i] &= (u16)(next[i] | 0x5555);
				return 0;
			}
			targets[t][i] = next[i];
		}
	}
	return 1;
}

void test_settings_record(void)
{
	u32 seq;
	record(copy_a, 100, 7);
	CHECK(set_record_check(copy_a, &seq));
	CHECK_EQ(seq, 7);
	copy_a[3] ^= 1;
	CHECK(!set_record_check(copy_a, &seq));

	// The newer whole copy; A on a tie; across the numbers' wrap
	record(copy_a, 100, 7);
	record(copy_b, 200, 8);
	CHECK_EQ(set_record_pick(copy_a, copy_b, &seq), SET_COPY_B);
	CHECK_EQ(seq, 8);
	record(copy_b, 200, 7);
	CHECK_EQ(set_record_pick(copy_a, copy_b, &seq), SET_COPY_A);
	record(copy_a, 100, 0xFFFFFFFF);
	record(copy_b, 200, 0);
	CHECK_EQ(set_record_pick(copy_a, copy_b, &seq), SET_COPY_B);

	// An older kernel's A after Torii's copies wins; a blank A doesn't
	legacy(copy_a, 300);
	CHECK_EQ(set_record_pick(copy_a, copy_b, &seq), SET_COPY_NONE);
	CHECK_EQ(read_value(), 300);
	memset(copy_a, 0xFF, SET_RECORD_BYTES);
	CHECK_EQ(set_record_pick(copy_a, copy_b, &seq), SET_COPY_B);
	memset(copy_b, 0xFF, SET_RECORD_BYTES);
	CHECK_EQ(set_record_pick(copy_a, copy_b, &seq), SET_COPY_NONE);

	// A power cut at every step of a save leaves the old values or the new,
	// from each starting point: a new cart, an older kernel's A, after one
	// save, after two
	for (u32 start = 0; start < 4; start++) {
		for (u32 half = 0; half < 2; half++) {
			for (u32 steps = 0;; steps++) {
				memset(copy_a, 0xFF, SET_RECORD_BYTES);
				memset(copy_b, 0xFF, SET_RECORD_BYTES);
				u16 old = 0xFFFF;
				if (start == 1) {
					legacy(copy_a, 300);
					old = 300;
				}
				for (u32 s = 2; s <= start; s++) {
					old = (u16)(400 + s);
					save(old, ~0u, 0);
				}
				u32 done = save(500, steps, half);
				u16 got = read_value();
				if (!(got == old || got == 500)) {
					CHECK_EQ(got, old);
					return;
				}
				// ... and the next save, cut nowhere, is read back
				save(600, ~0u, 0);
				CHECK_EQ(read_value(), 600);
				if (done)
					break;
			}
		}
	}
}
