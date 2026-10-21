// The settings record as stored in the kernel NOR. Hardware-free: the
// kernel side is in Ezcard_OP.c (Read_SET_info, Save_SET_info).
//
// The record is 256 16-bit slots (ez_define.h names them) in two copies, each
// in its own 8 KB boot sector: A at SET_RECORD_A, where the official kernel
// keeps its settings, and B at SET_RECORD_B. The last slots hold a trailer: a
// sequence number, a CRC and a magic number whose second half is the last
// slot, programmed last. A save writes the copy the settings weren't read
// from, so a power cut leaves the other whole; the reader takes the whole
// copy with the higher number.
//
// Older kernels (the official one, Torii before v0.4.2) write only A, with
// every slot from SET_RECORD_LEGACY_SLOTS 0. Such an A wins over any copy, as
// the newer write, and with no whole copy the settings are read from A as
// they are. The first save after either writes B, then A. An erase cut off
// can leave A all 0 (the chip programs a sector to 0 before erasing it) or
// with bits of both: neither counts as an older kernel's, as its own slots
// are never all 0 (the hotkeys).
//
// On a chip whose top 64 KB is one sector (a bottom-boot S29GL064A) both
// copies share it: each erase clears both, as the single copy before did.
#ifndef TORII_SETRECORD_H
#define TORII_SETRECORD_H

#include <gba_base.h>

#define SET_RECORD_A 0x7B0000  // SET_info_offset
#define SET_RECORD_B 0x7B2000  // the next 8 KB boot sector (bootloader.h)
#define SET_RECORD_BYTES 0x200
#define SET_RECORD_SLOTS (SET_RECORD_BYTES / 2)
#define SET_RECORD_MAGIC 0x31455354  // "TSE1"
// The trailer's slots: the sequence number, a reserved pair (0), the CRC of
// every slot before it, the magic
#define SET_RECORD_SEQ 248
#define SET_RECORD_CRC 252
#define SET_RECORD_MAGIC_SLOT 254
// The slots older kernels write (assress_max): they write the rest as 0
#define SET_RECORD_LEGACY_SLOTS 65

// Zeroes the slots from first up to the trailer, and fills in the trailer.
void set_record_seal(u16 *record, u32 first, u32 seq);
// 1 if record is a whole copy; sets *seq.
u32 set_record_check(const u16 *record, u32 *seq);

enum {
	SET_COPY_NONE,  // no whole copy: read A as it is, write B then A
	SET_COPY_A,  // read A, write B
	SET_COPY_B,  // read B, write A
};
// The copy to read; *seq is its number (0 for SET_COPY_NONE).
u32 set_record_pick(const u16 *a, const u16 *b, u32 *seq);

#endif
