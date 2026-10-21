// See artpack.h. Hardware-free, so the host tests check it.
#include "artpack.h"

int artpack_check(const struct artpack_header *h, u32 file_bytes)
{
	if (h->magic != ARTPACK_MAGIC || h->version != ARTPACK_VERSION || h->record_bytes != ARTPACK_RECORD_BYTES)
		return 0;
	if (!h->entries || h->entries > ARTPACK_MAX || !h->images || h->images > h->entries)
		return 0;
	u32 want = ARTPACK_HEADER_BYTES + h->entries * ARTPACK_ENTRY_BYTES;
	return file_bytes == want + h->images * ARTPACK_RECORD_BYTES;
}

s32 artpack_find(const struct artpack_entry *index, u32 n, u32 crc)
{
	u32 lo = 0, hi = n;
	while (lo < hi) {
		u32 mid = lo + (hi - lo) / 2;
		u32 at = index[mid].crc;
		if (at == crc)
			return (s32)index[mid].image;
		if (at < crc)
			lo = mid + 1;
		else
			hi = mid;
	}
	return -1;
}

u32 artpack_image_offset(u32 entries, u32 image)
{
	return ARTPACK_HEADER_BYTES + entries * ARTPACK_ENTRY_BYTES + image * ARTPACK_RECORD_BYTES;
}
