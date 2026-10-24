// EZ-Flash's boot loader and the part of the kernel NOR Torii may write.
// Hardware-free: Save_info (Ezcard_OP.c) and Diagnostics (diag.c) do the
// cart's side.
//
// The kernel NOR is the S71GL064A08's 8 MB flash. Its chip offset 0 holds
// EZ-Flash's boot loader (64 KB, shown at 0x08000000 under ROM page 0x8000),
// and the kernel starts at chip 0x40000. In kernel mode (ROM page 0x8002)
// 0x08000000 shows the chip from 0x40000, so the window's offsets from
// KERNEL_NOR_WINDOW wrap onto the loader: erasing there bricks the cart, which
// then starts in EZ-Flash's safe mode ("No_Loader_found", docs/recovery.md).
// v0.2.1 and v0.2.2 did, writing their NOR list's second copy at 0x7C0000.
//
// So the kernel writes only its data sectors, KERNEL_NOR_DATA up to
// KERNEL_NOR_WINDOW (window offsets): the save info and both NOR list copies,
// one 64 KB-aligned record each, and the settings' two copies, in the 8 KB
// boot sectors from KERNEL_NOR_SMALL (setrecord.h).
#ifndef TORII_BOOTLOADER_H
#define TORII_BOOTLOADER_H

#include <gba_base.h>

#define KERNEL_NOR_DATA 0x780000
#define KERNEL_NOR_WINDOW 0x7C0000
// The window's last 64 KB (chip 0x7F0000) are eight 8 KB boot sectors on the
// S29PL064J, so a record there must fit in one: an erase clears only 8 KB.
#define KERNEL_NOR_SMALL 0x7B0000
#define KERNEL_NOR_SMALL_SECTOR 0x2000

// The ROM pages that show the boot loader and the kernel at 0x08000000.
// Only Diagnostics' read-only check (diag_loader_read) sets the loader's.
#define ROM_PAGE_LOADER 0x8000
#define ROM_PAGE_KERNEL 0x8002
#define LOADER_SIZE 0x10000
// EZ-Flash's loader image, DE.BIN (SHA-1 0c13deff...2217, with a GBA header):
// its size and CRC32. Safe mode's "Update loader" writes it at chip 0.
#define DE_BIN_SIZE 13444
#define DE_BIN_CRC32 0x89544B57

// Whether Save_info may erase the sector at window offset OFFSET and program
// SIZE bytes there: a 64 KB-aligned sector of the kernel's data, or an 8 KB-
// aligned one from KERNEL_NOR_SMALL, the record fitting in the sector the
// erase clears (on a chip whose top 64 KB is one sector, an erase there
// clears all of it: still the kernel's data).
int kernel_nor_record_ok(u32 offset, u32 size);
// Whether Save_info may program SIZE bytes at window offset OFFSET in place,
// with no erase (the pending-save record's slots): halfword-aligned and
// wholly inside the kernel's data sectors
int kernel_nor_program_ok(u32 offset, u32 size);

enum loader_state {
	LOADER_BLANK,  // all 0xFF: the cart starts in safe mode
	LOADER_DAMAGED,  // its first 8 KB sector erased, nothing programmed since
	LOADER_NO_HEADER,  // no GBA header (DE.BIN has one): overwritten, as v0.2.2 left it
	LOADER_OK,  // a GBA header: 0x96 at 0xB2 and a matching complement
};

// What the LOADER_SIZE bytes of the boot loader at IMAGE look like.
enum loader_state loader_check(const u8 *image);
// Its name in the log
const char *loader_state_name(enum loader_state state);

#endif
