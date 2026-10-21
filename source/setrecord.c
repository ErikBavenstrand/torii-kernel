#include "setrecord.h"

#include "crc32.h"

static u32 slot32(const u16 *record, u32 slot)
{
	return record[slot] | (u32)record[slot + 1] << 16;
}

static void set32(u16 *record, u32 slot, u32 value)
{
	record[slot] = (u16)value;
	record[slot + 1] = (u16)(value >> 16);
}

void set_record_seal(u16 *record, u32 first, u32 seq)
{
	for (u32 slot = first; slot < SET_RECORD_SEQ; slot++)
		record[slot] = 0;
	set32(record, SET_RECORD_SEQ, seq);
	set32(record, SET_RECORD_SEQ + 2, 0);
	set32(record, SET_RECORD_CRC, crc32(record, SET_RECORD_CRC * 2));
	set32(record, SET_RECORD_MAGIC_SLOT, SET_RECORD_MAGIC);
}

u32 set_record_check(const u16 *record, u32 *seq)
{
	if (slot32(record, SET_RECORD_MAGIC_SLOT) != SET_RECORD_MAGIC ||
	    slot32(record, SET_RECORD_CRC) != crc32(record, SET_RECORD_CRC * 2))
		return 0;
	*seq = slot32(record, SET_RECORD_SEQ);
	return 1;
}

// Whether record is what an older kernel writes: its slots, not all 0, then
// only 0. A torn Torii program leaves the last slot erased or partly
// programmed, never 0; a torn erase leaves it all 0, or 0s and 1s at random.
static u32 set_record_legacy(const u16 *record)
{
	u32 any = 0;
	for (u32 slot = 0; slot < SET_RECORD_LEGACY_SLOTS; slot++)
		any |= record[slot];
	for (u32 slot = SET_RECORD_LEGACY_SLOTS; slot < SET_RECORD_SLOTS; slot++)
		if (record[slot])
			return 0;
	return any != 0;
}

u32 set_record_pick(const u16 *a, const u16 *b, u32 *seq)
{
	u32 seq_a, seq_b;
	u32 has_a = set_record_check(a, &seq_a);
	u32 has_b = set_record_check(b, &seq_b);
	*seq = 0;
	// An older kernel wrote A after Torii's copies
	if (!has_a && set_record_legacy(a))
		return SET_COPY_NONE;
	if (has_a && (!has_b || (s32)(seq_b - seq_a) <= 0)) {
		*seq = seq_a;
		return SET_COPY_A;
	}
	if (has_b) {
		*seq = seq_b;
		return SET_COPY_B;
	}
	return SET_COPY_NONE;
}
