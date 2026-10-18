#include "norlist.h"

#include <string.h>

#include "crc32.h"

_Static_assert(sizeof(struct nor_list_trailer) == 32, "the trailer is one buffer write");
_Static_assert(sizeof(struct nor_mark) % 32 == 0 && sizeof(struct nor_mark) <= NOR_MARK_SPACE,
	       "the mark is whole buffer writes and fits its space");

void nor_list_seal(void *record, u32 seq, u32 count)
{
	struct nor_list_trailer *t = (struct nor_list_trailer *)((u8 *)record + NOR_LIST_BYTES);
	memset(t, 0, sizeof(*t));
	t->magic = NOR_LIST_MAGIC;
	t->seq = seq;
	t->count = count;
	t->crc = crc32(record, NOR_LIST_RECORD - sizeof(t->crc));
}

u32 nor_list_check(const void *record, u32 *seq)
{
	const struct nor_list_trailer *t = (const struct nor_list_trailer *)((const u8 *)record + NOR_LIST_BYTES);
	if (t->magic != NOR_LIST_MAGIC || t->count > NOR_LIST_ENTRIES ||
	    t->crc != crc32(record, NOR_LIST_RECORD - sizeof(t->crc)))
		return 0;
	*seq = t->seq;
	return 1;
}

u32 nor_list_pick(const void *a, const void *b)
{
	u32 seq_a, seq_b;
	u32 has_a = nor_list_check(a, &seq_a);
	u32 has_b = nor_list_check(b, &seq_b);
	if (has_a && has_b)  // the newer; a save leaves both with the same number
		return (s32)(seq_b - seq_a) > 0 ? NOR_COPY_B : NOR_COPY_A;
	return has_a ? NOR_COPY_A : has_b ? NOR_COPY_B : NOR_COPY_NONE;
}

void nor_mark_make(struct nor_mark *mark, u32 offset, u32 size, const u8 *header, const void *entry)
{
	memset(mark, 0, sizeof(*mark));
	mark->magic = NOR_MARK_MAGIC;
	mark->offset = offset;
	mark->size = size;
	memcpy(mark->header, header, sizeof(mark->header));
	memcpy(mark->entry, entry, sizeof(mark->entry));
	mark->crc = crc32(mark, sizeof(*mark) - sizeof(mark->crc));
}

u32 nor_mark_check(const struct nor_mark *mark, u32 offset, u32 end, const u8 *header)
{
	return mark->magic == NOR_MARK_MAGIC && mark->offset == offset && mark->size == end - offset && mark->size &&
	       !(mark->size & 0x1FFFF) && memcmp(mark->header, header, sizeof(mark->header)) == 0 &&
	       mark->crc == crc32(mark, sizeof(*mark) - sizeof(mark->crc));
}
