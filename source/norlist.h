// The game NOR's list and completion marks, as stored. Hardware-free: the
// kernel side is in NORflash_OP.c.
//
// The list lives in the kernel NOR in two copies, each a 64 KB sector: A at
// NOR_LIST_A (where the official kernel keeps its list) and B at NOR_LIST_B.
// A copy is the 64 entries the official kernel stores, then a trailer with a
// sequence number and a CRC, programmed last. A save writes the copy the list
// wasn't read from first, then the other, so a power cut leaves one whole
// copy. A list from an older kernel has no trailer; it is read from A.
//
// Each game Torii writes ends with a completion mark: the last NOR_MARK_SPACE
// bytes of its NOR area hold its list entry, programmed after all its data.
// A listed game whose mark is missing is dropped, and a game with a mark that
// the list lacks (a power cut before the list was saved, or both copies lost)
// is put back.
#ifndef TORII_NORLIST_H
#define TORII_NORLIST_H

#include <gba_base.h>

#define NOR_LIST_A 0x7A0000
#define NOR_LIST_B 0x7C0000
#define NOR_ENTRY_SIZE 132  // sizeof(FM_NOR_FS)
#define NOR_LIST_ENTRIES 0x40  // MAX_NOR
#define NOR_LIST_BYTES (NOR_ENTRY_SIZE * NOR_LIST_ENTRIES)
#define NOR_LIST_MAGIC 0x324C4E54  // "TNL2"

struct nor_list_trailer {
	u32 magic;
	u32 seq;
	u32 count;
	u32 reserved[4];
	u32 crc;  // of the entries and the trailer before it
};

// A copy as written: whole 32-byte buffer writes
#define NOR_LIST_RECORD (NOR_LIST_BYTES + sizeof(struct nor_list_trailer))

// Fills in the trailer after the entries in record.
void nor_list_seal(void *record, u32 seq, u32 count);
// 1 if record is a whole copy; sets *seq.
u32 nor_list_check(const void *record, u32 *seq);
// The copy to read: NOR_COPY_A or NOR_COPY_B, or NOR_COPY_NONE if neither is
// whole (the list is then read from A as an older kernel's).
enum {
	NOR_COPY_NONE,
	NOR_COPY_A,
	NOR_COPY_B
};
u32 nor_list_pick(const void *a, const void *b);

#define NOR_MARK_SPACE 0x200
#define NOR_MARK_MAGIC 0x314D4E54  // "TNM1"
// FM_NOR_FS.reserved1 of an entry whose game has a completion mark
#define NOR_ENTRY_MARKED 'T'

struct nor_mark {
	u32 magic;
	u32 offset;  // of the game in the game NOR
	u32 size;  // of its NOR area, the mark included
	u8 header[0x20];  // its ROM header from 0xA0, as written to NOR
	u8 entry[NOR_ENTRY_SIZE];  // its list entry
	u8 reserved[12];
	u32 crc;  // of everything before it
};

void nor_mark_make(struct nor_mark *mark, u32 offset, u32 size, const u8 *header, const void *entry);
// 1 if mark is whole and belongs to the game at offset whose area ends at end
// and whose header is header.
u32 nor_mark_check(const struct nor_mark *mark, u32 offset, u32 end, const u8 *header);

#endif
