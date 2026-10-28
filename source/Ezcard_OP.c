#include <gba_video.h>
#include <gba_interrupt.h>
#include <gba_systemcalls.h>
#include <gba_input.h>
#include <stdio.h>
#include <stdlib.h>
#include <gba_base.h>
#include <gba_dma.h>
#include <string.h>

#include "ez_define.h"
#include "setrecord.h"
#include "ezkernel.h"
#include "ui/dialog.h"
#include "ui/ui.h"
#include "Ezcard_OP.h"
#include "lang.h"
#include "tlog.h"
#include "waitcnt.h"
#include "fattable.h"
#include "fpga_board.h"
#include "rtssink.h"

extern u32 FAT_table_buffer[FAT_table_size / 4] EWRAM_BSS;

extern FIL gfile;
// --------------------------------------------------------------------
extern void HardReset(void);
extern void SoftReset_now(void);

// --------------------------------------------------------------------
void IWRAM_CODE SetSDControl(u16 control)
{
	u16 waitcnt = WAITCNT_SLOW();
	*(u16 *)0x9fe0000 = 0xd200;
	*(u16 *)0x8000000 = 0x1500;
	*(u16 *)0x8020000 = 0xd200;
	*(u16 *)0x8040000 = 0x1500;
	*(u16 *)0x9400000 = control;
	*(u16 *)0x9fc0000 = 0x1500;
	WAITCNT_RESTORE(waitcnt);
}
// --------------------------------------------------------------------
void IWRAM_CODE SD_Enable(void)
{
	SetSDControl(1);
}
// --------------------------------------------------------------------
void IWRAM_CODE SD_Read_state(void)
{
	SetSDControl(3);
}
// --------------------------------------------------------------------
void IWRAM_CODE SD_Disable(void)
{
	SetSDControl(0);
}
// --------------------------------------------------------------------
u16 IWRAM_CODE SD_Response(void)
{
	return *(vu16 *)0x9E00000;
}
// --------------------------------------------------------------------
u32 IWRAM_CODE Wait_SD_Response(void)
{
	vu16 res;
	u32 count = 0;
	while (1) {
		res = SD_Response();
		if (res != 0xEEE1) {
			return 0;
		}

		count++;
		if (count > 0x100000) {
			//DEBUG_printf("time out %x",res);
			//wait_btn();
			return 1;
		}
	}
}
// --------------------------------------------------------------------
u32 IWRAM_CODE Read_SD_sectors(u32 address, u16 count, u8 *SDbuffer)
{
	u16 waitcnt = WAITCNT_SLOW();
	SD_Enable();

	u16 i;
	u16 blocks;
	u32 res;
	u32 times;
	for (i = 0; i < count; i += 4) {
		blocks = (count - i > 4) ? 4 : (count - i);
		times = 2;  // Torii: each chunk gets its retry

	read_again:
		*(vu16 *)0x9fe0000 = 0xd200;
		*(vu16 *)0x8000000 = 0x1500;
		*(vu16 *)0x8020000 = 0xd200;
		*(vu16 *)0x8040000 = 0x1500;
		*(vu16 *)0x9600000 = ((address + i) & 0x0000FFFF);
		*(vu16 *)0x9620000 = ((address + i) & 0xFFFF0000) >> 16;
		*(vu16 *)0x9640000 = blocks;
		*(vu16 *)0x9fc0000 = 0x1500;
		SD_Read_state();
		res = Wait_SD_Response();
		SD_Enable();
		if (res == 1) {
			times--;
			if (times) {
				delay(5000);
				goto read_again;
			}
			// Torii: the card never answered, twice: a read error
			// (the official kernel copied the stale buffer and returned 0).
			// No other status value is known to mean an error.
			SD_Disable();
			WAITCNT_RESTORE(waitcnt);
			return 1;
		}
		dmaCopy((void *)0x9E00000, SDbuffer + i * 512, blocks * 512);
	}
	SD_Disable();
	WAITCNT_RESTORE(waitcnt);
	return 0;
}
// --------------------------------------------------------------------
u32 IWRAM_CODE Write_SD_sectors(u32 address, u16 count, const u8 *SDbuffer)
{
	u16 waitcnt = WAITCNT_SLOW();
	SD_Enable();
	SD_Read_state();
	u16 i;
	u16 blocks;
	u32 res;
	for (i = 0; i < count; i += 4) {
		blocks = (count - i > 4) ? 4 : (count - i);

		dmaCopy(SDbuffer + i * 512, (void *)0x9E00000, blocks * 512);
		*(vu16 *)0x9fe0000 = 0xd200;
		*(vu16 *)0x8000000 = 0x1500;
		*(vu16 *)0x8020000 = 0xd200;
		*(vu16 *)0x8040000 = 0x1500;
		*(vu16 *)0x9600000 = ((address + i) & 0x0000FFFF);
		*(vu16 *)0x9620000 = ((address + i) & 0xFFFF0000) >> 16;
		*(vu16 *)0x9640000 = 0x8000 + blocks;
		*(vu16 *)0x9fc0000 = 0x1500;

		res = Wait_SD_Response();
		if (res == 1) {
			SD_Disable();  // Torii: SD mode hides the ROM we return to
			WAITCNT_RESTORE(waitcnt);
			return 1;
		}
	}
	delay(3000);
	SD_Disable();
	WAITCNT_RESTORE(waitcnt);
	return 0;
}
// --------------------------------------------------------------------
// Torii: count sectors from address, each a copy of the 512 bytes at sector
// (FatFs's new folder clusters, all zeros). The commands go 4 blocks at a
// time as Write_SD_sectors' do, with its one settle delay at the end, where
// a sector per call took the delay each time (64 for a 32 KB cluster).
u32 IWRAM_CODE Write_SD_fill(u32 address, u16 count, const u8 *sector)
{
	u16 waitcnt = WAITCNT_SLOW();
	SD_Enable();
	SD_Read_state();
	u16 i;
	u16 blocks;
	u32 res;
	for (i = 0; i < count; i += 4) {
		blocks = (count - i > 4) ? 4 : (count - i);

		for (u16 b = 0; b < blocks; b++)
			dmaCopy(sector, (void *)(0x9E00000 + b * 512), 512);
		*(vu16 *)0x9fe0000 = 0xd200;
		*(vu16 *)0x8000000 = 0x1500;
		*(vu16 *)0x8020000 = 0xd200;
		*(vu16 *)0x8040000 = 0x1500;
		*(vu16 *)0x9600000 = ((address + i) & 0x0000FFFF);
		*(vu16 *)0x9620000 = ((address + i) & 0xFFFF0000) >> 16;
		*(vu16 *)0x9640000 = 0x8000 + blocks;
		*(vu16 *)0x9fc0000 = 0x1500;

		res = Wait_SD_Response();
		if (res == 1) {
			SD_Disable();
			WAITCNT_RESTORE(waitcnt);
			return 1;
		}
	}
	delay(3000);
	SD_Disable();
	WAITCNT_RESTORE(waitcnt);
	return 0;
}
// --------------------------------------------------------------------
u16 IWRAM_CODE Read_S71NOR_ID(void)
{
	u16 ID1;
	*((vu16 *)(FlashBase_S71)) = 0xF0;
	*((vu16 *)(FlashBase_S71 + 0x555 * 2)) = 0xAA;
	*((vu16 *)(FlashBase_S71 + 0x2AA * 2)) = 0x55;
	*((vu16 *)(FlashBase_S71 + 0x555 * 2)) = 0x90;
	ID1 = *((vu16 *)(FlashBase_S71 + 0xE * 2));
	*((vu16 *)(FlashBase_S71)) = 0xF0;
	return ID1;
}
// --------------------------------------------------------------------
u16 Read_S98NOR_ID(void)
{
	u16 ID1;
	*((vu16 *)(FlashBase_S98)) = 0xF0;
	*((vu16 *)(FlashBase_S98 + 0x555 * 2)) = 0xAA;
	*((vu16 *)(FlashBase_S98 + 0x2AA * 2)) = 0x55;
	*((vu16 *)(FlashBase_S98 + 0x555 * 2)) = 0x90;
	ID1 = *((vu16 *)(FlashBase_S98 + 0xE * 2));
	*((vu16 *)(FlashBase_S98)) = 0xF0;  //Torii: back to reading, or the NOR list reads the ID
	return ID1;
}
// --------------------------------------------------------------------
void IWRAM_CODE SetRompage(u16 page)
{
	u16 waitcnt = WAITCNT_SLOW();
	*(vu16 *)0x9fe0000 = 0xd200;
	*(vu16 *)0x8000000 = 0x1500;
	*(vu16 *)0x8020000 = 0xd200;
	*(vu16 *)0x8040000 = 0x1500;
	*(vu16 *)0x9880000 = page;//C4
	*(vu16 *)0x9fc0000 = 0x1500;
	WAITCNT_RESTORE(waitcnt);
}
// --------------------------------------------------------------------
void IWRAM_CODE SetbufferControl(u16 control)
{
	u16 waitcnt = WAITCNT_SLOW();
	*(u16 *)0x9fe0000 = 0xd200;
	*(u16 *)0x8000000 = 0x1500;
	*(u16 *)0x8020000 = 0xd200;
	*(u16 *)0x8040000 = 0x1500;
	*(u16 *)0x9420000 = control;//A1
	*(u16 *)0x9fc0000 = 0x1500;
	WAITCNT_RESTORE(waitcnt);
}
// --------------------------------------------------------------------
void IWRAM_CODE SetPSRampage(u16 page)
{
	u16 waitcnt = WAITCNT_SLOW();
	*(vu16 *)0x9fe0000 = 0xd200;
	*(vu16 *)0x8000000 = 0x1500;
	*(vu16 *)0x8020000 = 0xd200;
	*(vu16 *)0x8040000 = 0x1500;
	*(vu16 *)0x9860000 = page;//C3
	*(vu16 *)0x9fc0000 = 0x1500;
	WAITCNT_RESTORE(waitcnt);
}
// --------------------------------------------------------------------
void IWRAM_CODE SetRampage(u16 page)
{
	u16 waitcnt = WAITCNT_SLOW();
	*(vu16 *)0x9fe0000 = 0xd200;
	*(vu16 *)0x8000000 = 0x1500;
	*(vu16 *)0x8020000 = 0xd200;
	*(vu16 *)0x8040000 = 0x1500;
	*(vu16 *)0x9c00000 = page;//E0
	*(vu16 *)0x9fc0000 = 0x1500;
	WAITCNT_RESTORE(waitcnt);
}
// --------------------------------------------------------------------
// The send itself (Send_FATbuffer checks the table first)
void IWRAM_CODE Send_FATbuffer_now(u32 *buffer, u32 mode)
{
	u16 waitcnt = WAITCNT_SLOW();
	SetbufferControl(1);
	dmaCopy(buffer, (void *)0x9E00000, 0x400);
	if (mode == 2) {
		SetbufferControl(0);
		WAITCNT_RESTORE(waitcnt);
		return;
	}
	SetbufferControl(3);
	if (mode == 1) {
		SetbufferControl(0);
		WAITCNT_RESTORE(waitcnt);
		return;
	}

	u16 res;
	while (1) {
		res = SD_Response();
		if (res != 0x0000)
			break;
	}

	while (1) {
		res = SD_Response();
		if (res != 0x0001)
			break;
	}
	SetbufferControl(0);
	WAITCNT_RESTORE(waitcnt);
}

// Torii: a launch table goes only with the game's runs ended and the save-state
// buffer's write-back pointed at /torii/rts.bin (fattable.h); mode 2 sends
// firmware pages, which the FPGA doesn't take as a table
u32 Send_FATbuffer(u32 *buffer, u32 mode)
{
	const char *why = NULL;
	if (mode != 2 && !fat_table_safe(buffer, rts_sink_crc(), &why)) {
		tlog_error("launch: the cart's table not sent: %s", why);
		TLOG_ASSERT(0);
		return 1;
	}
	Send_FATbuffer_now(buffer, mode);
	return 0;
}
// --------------------------------------------------------------------
#define RESET_EWRAM (1 << 0) /*!< Clear 256K on-board WRAM			*/
#define RESET_IWRAM (1 << 1) /*!< Clear 32K in-chip WRAM				*/
#define RESET_PALETTE (1 << 2) /*!< Clear Palette						*/
#define RESET_VRAM (1 << 3) /*!< Clear VRAM							*/
#define RESET_OAM (1 << 4) /*!< Clear OAM							*/
#define RESET_SIO (1 << 5) /*!< Switches to general purpose mode	*/
#define RESET_SOUND (1 << 6) /*!< Reset Sound registers				*/
#define RESET_OTHER (1 << 7) /*!< all other registers				*/
// --------------------------------------------------------------------
extern u16 gl_game_clock;
extern u16 gl_game_rumble;
u32 gl_game_uses;//GAME_USES_* of the game being launched, from Game_hardware

// The cart's clock and rumble motor share its GPIO port, and one register
// opens the whole port to the game. It opens when the game uses either and
// that one is switched on in CART.
u16 IWRAM_CODE Game_GPIO_status(void)
{
	return ((gl_game_uses & GAME_USES_CLOCK) && gl_game_clock) ||
	       ((gl_game_uses & GAME_USES_RUMBLE) && gl_game_rumble);
}

u16 Game_clock_on(void)
{
	return (gl_game_uses & GAME_USES_CLOCK) && gl_game_clock;
}

void IWRAM_CODE SetRompageWithHardReset(u16 page, u32 bootmode)
{
	Set_RTC_status(Game_GPIO_status());
	REG_WAITCNT_CART = WAITCNT_BIOS;  // what games expect at start (waitcnt.h)
	SetRompage(page);
	RegisterRamReset(RESET_EWRAM | RESET_PALETTE | RESET_VRAM | RESET_OAM | RESET_SIO | RESET_SOUND | RESET_OTHER);
	if (bootmode == 1) {
		HardReset();
	} else {
		SoftReset_now();
	}
}
// --------------------------------------------------------------------
void IWRAM_CODE ReadSram(u32 address, u8 *data, u32 size)
{
	register unsigned int i;
	for (i = 0; i < size; i++) {
		data[i] = *(u8 *)(address + i);
	}
}
// --------------------------------------------------------------------
void IWRAM_CODE WriteSram(u32 address, u8 *data, u32 size)
{
	register unsigned int i;
	for (i = 0; i < size; i++) {
		*(vu8 *)(address + i) = data[i];
	}
}
// --------------------------------------------------------------------
void IWRAM_CODE Bank_Switching(u8 bank)
{
	*((vu8 *)(SAVE_sram_base + 0x5555)) = 0xAA;
	*((vu8 *)(SAVE_sram_base + 0x2AAA)) = 0x55;
	*((vu8 *)(SAVE_sram_base + 0x5555)) = 0xB0;
	*((vu8 *)(SAVE_sram_base + 0x0000)) = bank;
}
// --------------------------------------------------------------------
// Erases one block of the kernel's NOR and writes buffersize bytes to it.
// Returns how many halfwords read back wrong; all of them, writing nothing,
// for a block outside the kernel's data sectors (bootloader.h).
// Save_info's next write: SAVE_INFO_ERASE (the sector erased, then the
// record written), or programmed in place over what is there (NOR turns 1s
// to 0s with no erase): SAVE_INFO_CLEAR, zeros over the pending-save
// record with its flag first, or SAVE_INFO_NEW, a record into an erased
// slot with its flag last. In place, a write that would need an erase is
// refused (all halfwords wrong): an erase here would take the sector's
// other slots with it.
enum {
	SAVE_INFO_ERASE,
	SAVE_INFO_CLEAR,
	SAVE_INFO_NEW
};
static u8 save_info_mode;

u32 IWRAM_CODE Save_info(u32 info_offset, u16 *info_buffer, u32 buffersize)
{
	u32 offset;
	u32 written;
	u32 wrong = 0;
	u16 waitcnt = WAITCNT_SLOW();
	vu16 *buf = (vu16 *)info_buffer;
	register u32 loopwrite;
	vu16 v1, v2;
	// Torii: only the kernel's data sectors; past KERNEL_NOR_WINDOW is the
	// boot loader, and below KERNEL_NOR_DATA the kernel (bootloader.h). A
	// write in place never erases: it may start anywhere inside them
	u8 mode = save_info_mode;
	save_info_mode = SAVE_INFO_ERASE;
	if (!(mode == SAVE_INFO_ERASE ? kernel_nor_record_ok(info_offset, buffersize)
				      : kernel_nor_program_ok(info_offset, buffersize))) {
		tlog_error("nor: refused to write %lu bytes at kernel NOR %06lX, outside its data sectors",
			   (unsigned long)buffersize, (unsigned long)info_offset);
		WAITCNT_RESTORE(waitcnt);
		return buffersize / 2;
	}
	// The offsets are the kernel page's: under another page they could be
	// the loader (bootloader.h)
	SetRompage(ROM_PAGE_KERNEL);
	u16 S71id = Read_S71NOR_ID();
	*((vu16 *)(FlashBase_S71)) = 0xF0;

	offset = info_offset;//0x7A0000/0x7B0000 ;

	// Torii: the pending-save record's slots (Save_sav_info) are programmed
	// in place, with no erase (about 0.7 s each): only the halfwords that
	// change, a clear's flag first (cut short, the record reads as gone) and
	// a new record's flag last (cut short, it never reads as pending). Every
	// other record is erased and written whole, as before.
	if (mode != SAVE_INFO_ERASE) {
		written = buffersize / 2;
		for (loopwrite = 0; loopwrite < written; loopwrite++) {
			u16 old = *((vu16 *)(FlashBase_S71 + offset + loopwrite * 2));
			if ((old & buf[loopwrite]) != buf[loopwrite]) {
				*((vu16 *)(FlashBase_S71)) = 0xF0;
				WAITCNT_RESTORE(waitcnt);
				return written;  // it needs an erase: refused
			}
		}
		for (u32 k = 0; k < written; k++) {
			// SAVE_INFO_NEW: 1, 2, ..., the flag at 0 last
			loopwrite = mode == SAVE_INFO_NEW ? (k + 1) % written : k;
			if (*((vu16 *)(FlashBase_S71 + offset + loopwrite * 2)) == buf[loopwrite])
				continue;
			*((vu16 *)(FlashBase_S71 + 0x555 * 2)) = 0xAA;
			*((vu16 *)(FlashBase_S71 + 0x2AA * 2)) = 0x55;
			*((vu16 *)(FlashBase_S71 + 0x555 * 2)) = 0xA0;
			*((vu16 *)(FlashBase_S71 + offset + loopwrite * 2)) = buf[loopwrite];
			do {
				v1 = *((vu16 *)(FlashBase_S71 + offset + loopwrite * 2));
				v2 = *((vu16 *)(FlashBase_S71 + offset + loopwrite * 2));
			} while (v1 != v2);
		}
		goto verify;
	}

	*((vu16 *)(FlashBase_S71 + 0x555 * 2)) = 0xAA;
	*((vu16 *)(FlashBase_S71 + 0x2AA * 2)) = 0x55;
	*((vu16 *)(FlashBase_S71 + 0x555 * 2)) = 0x80;
	*((vu16 *)(FlashBase_S71 + 0x555 * 2)) = 0xAA;
	*((vu16 *)(FlashBase_S71 + 0x2AA * 2)) = 0x55;
	*((vu16 *)(FlashBase_S71 + offset)) = 0x30;//erase
	do {
		v1 = *((vu16 *)(FlashBase_S71 + offset));
		v2 = *((vu16 *)(FlashBase_S71 + offset));
	} while (v1 != v2);
	//erase finish
	if (S71id == 0x2202) //PL064
	{
		written = buffersize / 2;
		for (loopwrite = 0; loopwrite < buffersize / 2; loopwrite++) {
			*((vu16 *)(FlashBase_S71 + 0x555 * 2)) = 0xAA;
			*((vu16 *)(FlashBase_S71 + 0x2AA * 2)) = 0x55;
			*((vu16 *)(FlashBase_S71 + 0x555 * 2)) = 0xA0;
			*((vu16 *)(FlashBase_S71 + offset + loopwrite * 2)) = buf[loopwrite];
			do {
				v1 = *((vu16 *)(FlashBase_S71 + offset + loopwrite * 2));
				v2 = *((vu16 *)(FlashBase_S71 + offset + loopwrite * 2));
			} while (v1 != v2);
		}
	} else { //GL064
		u32 i;
		written = buffersize / 32 * 16;
		for (loopwrite = 0; loopwrite < (buffersize / 32); loopwrite++) {
			*((vu16 *)(FlashBase_S71 + 0x555 * 2)) = 0xAA;
			*((vu16 *)(FlashBase_S71 + 0x2AA * 2)) = 0x55;
			*((vu16 *)(FlashBase_S71 + offset + loopwrite * 32)) = 0x25;
			*((vu16 *)(FlashBase_S71 + offset + loopwrite * 32)) = 15;
			for (i = 0; i <= 15; i++) {
				*((vu16 *)(FlashBase_S71 + offset + loopwrite * 32 + 2 * i)) = buf[loopwrite * 16 + i];
			}
			*((vu16 *)(FlashBase_S71 + offset + loopwrite * 32)) = 0x29;

			do {
				v1 = *((vu16 *)(FlashBase_S71 + offset + loopwrite * 32));
				v2 = *((vu16 *)(FlashBase_S71 + offset + loopwrite * 32));
			} while (v1 != v2);
		}
	}

verify:
	*((vu16 *)(FlashBase_S71)) = 0xF0;
	// Torii: read it back
	for (loopwrite = 0; loopwrite < written; loopwrite++)
		if (*((vu16 *)(FlashBase_S71 + offset + loopwrite * 2)) != buf[loopwrite])
			wrong++;
	WAITCNT_RESTORE(waitcnt);
	return wrong;
}

// --------------------------------------------------------------------
// Torii: the three NOR records log what they wrote, after the write, when
// the kernel's NOR reads again.
static void log_info_write(const char *what, u32 buffersize, u32 wrong)
{
	if (wrong)
		tlog_error("nor: writing %s: %lu of %lu halfwords read back wrong", what, (unsigned long)wrong,
			   (unsigned long)buffersize / 2);
	else
		tlog("nor: wrote %s, %lu bytes", what, (unsigned long)buffersize);
}

// Torii: the pending-save record's 64 KB sector holds SAV_SLOTS records of
// SAV_SLOT_BYTES one after another, so a launch writes its record with no
// erase (about 0.5 s each before) and only every SAV_SLOTS launches erase.
// A slot is erased (its flag 0xFFFF), live (0x11) or used (0x0000, zeros
// over it). The record is the first slot not used: 0x11 is a save waiting
// to be copied, anything else none. Older kernels wrote slot 0 only, so
// their record reads the same.
#define SAV_SLOT_BYTES 0x200
#define SAV_SLOTS (0x10000 / SAV_SLOT_BYTES)
_Static_assert(SAVE_info_offset % 0x10000 == 0 && SAVE_info_offset + 0x10000 <= KERNEL_NOR_SMALL,
	       "the record's slots fill one 64 KB sector of the kernel's data");

// The first slot not used, SAV_SLOTS if all are; read from the NOR each time
// (the ROM page is the kernel's): nothing to go stale
static u32 IWRAM_CODE sav_slot(void)
{
	u32 k;
	for (k = 0; k < SAV_SLOTS; k++) {
		if (*((vu16 *)(FlashBase_S71 + SAVE_info_offset + k * SAV_SLOT_BYTES)) != 0x0000)
			break;
	}
	return k;
}

static int IWRAM_CODE sav_slot_erased(u32 k)
{
	for (u32 i = 0; i < SAV_SLOT_BYTES / 2; i++) {
		if (*((vu16 *)(FlashBase_S71 + SAVE_info_offset + k * SAV_SLOT_BYTES + i * 2)) != 0xFFFF)
			return 0;
	}
	return 1;
}

// Zeros over slot k: used from now on. The halfwords wrong
static u32 IWRAM_CODE sav_slot_use(u32 k, u16 *zeros)
{
	memset(zeros, 0, SAV_SLOT_BYTES);
	save_info_mode = SAVE_INFO_CLEAR;
	return Save_info(SAVE_info_offset + k * SAV_SLOT_BYTES, zeros, SAV_SLOT_BYTES);
}

void IWRAM_CODE Save_sav_info(u16 *SAV_info_buffer, u32 buffersize)
{
	static u16 zeros[SAV_SLOT_BYTES / 2] EWRAM_BSS;
	u32 k = sav_slot(), wrong = 0;
	if (SAV_info_buffer[0] != 0x11) {
		// A clear: the live record's slot is used up (an erased one stays),
		// and so is any slot after it that isn't erased (damage, or a cut
		// in the sector's erase): it would read as the record once the
		// slots before it are used
		if (k < SAV_SLOTS && !sav_slot_erased(k))
			wrong = sav_slot_use(k, zeros);
		for (u32 j = k + 1; j < SAV_SLOTS && !wrong; j++) {
			if (!sav_slot_erased(j)) {
				tlog("nor: a stray pending-save record slot %lu used up", (unsigned long)j);
				wrong = sav_slot_use(j, zeros);
			}
		}
		log_info_write("an empty pending-save record", buffersize, wrong);
		return;
	}
	// A new record goes into the first erased slot: one still live (a launch
	// the record's save was given up for) or one a cut left half written is
	// used up first
	while (k < SAV_SLOTS && !sav_slot_erased(k) && !wrong) {
		wrong = sav_slot_use(k, zeros);
		k++;
	}
	if (!wrong && k < SAV_SLOTS) {
		save_info_mode = SAVE_INFO_NEW;
		wrong = Save_info(SAVE_info_offset + k * SAV_SLOT_BYTES, SAV_info_buffer, buffersize);
	} else if (!wrong) {
		// Every slot used: the sector is erased (written with 0xFFFF, which
		// programs nothing), then the record goes into slot 0 in place, its
		// flag last as in any slot
		tlog("nor: the pending-save record's %u slots are used: the sector is erased", SAV_SLOTS);
		k = 0;
		memset(zeros, 0xFF, SAV_SLOT_BYTES);
		wrong = Save_info(SAVE_info_offset, zeros, SAV_SLOT_BYTES);
		if (!wrong) {
			save_info_mode = SAVE_INFO_NEW;
			wrong = Save_info(SAVE_info_offset, SAV_info_buffer, buffersize);
		}
	}
	tlog_debug("nor: the pending-save record in slot %lu", (unsigned long)k);
	log_info_write("the pending-save record", buffersize, wrong);
}
// --------------------------------------------------------------------
// Torii: the settings record's two copies (setrecord.h). set_copy is the
// window offset Read_SET_info reads, 0 until set_pick runs; in IWRAM, as
// set_staging.
_Static_assert(SET_RECORD_LEGACY_SLOTS == assress_max && assress_max < SET_RECORD_SEQ, "the slots older kernels write");
_Static_assert(SET_RECORD_A == SET_info_offset && SET_RECORD_A >= KERNEL_NOR_SMALL &&
		       SET_RECORD_B + SET_RECORD_BYTES <= KERNEL_NOR_WINDOW &&
		       SET_RECORD_B - SET_RECORD_A == KERNEL_NOR_SMALL_SECTOR,
	       "the settings' copies are 8 KB boot sectors of the kernel's data (bootloader.h)");
static u32 set_copy;
static u32 set_seq;
static u8 set_read;  // SET_COPY_A, SET_COPY_B, or SET_COPY_NONE: A as it is

// Not inlined into Read_SET_info: it runs once, from ROM
static __attribute__((noinline)) void set_pick(void)
{
	const u16 *a = (const u16 *)(FlashBase_S71 + SET_RECORD_A);
	const u16 *b = (const u16 *)(FlashBase_S71 + SET_RECORD_B);
	set_read = (u8)set_record_pick(a, b, &set_seq);
	set_copy = set_read == SET_COPY_B ? SET_RECORD_B : SET_RECORD_A;
	tlog("nor: settings read from %s (%lu)",
	     set_read == SET_COPY_A   ? "copy A"
	     : set_read == SET_COPY_B ? "copy B"
				      : "A as it is (no whole copy)",
	     (unsigned long)set_seq);
}

// Seals the record in buffer with seq and writes it at offset; 1 if it
// read back whole
static u32 set_write(u32 offset, u16 *buffer, u32 seq)
{
	set_record_seal(buffer, assress_max, seq);
	u32 wrong = Save_info(offset, buffer, SET_RECORD_BYTES);
	if (wrong)
		tlog_error("nor: writing the settings' copy %c: %lu of %lu halfwords read back wrong",
			   offset == SET_RECORD_A ? 'A' : 'B', (unsigned long)wrong, (unsigned long)SET_RECORD_SLOTS);
	return !wrong;
}

u32 Save_SET_info(u16 *SET_info_buffer)
{
	if (!set_copy)
		set_pick();
	u32 seq = set_seq + 1;
	// Both copies, the one not read from first, so a cut leaves the other
	// whole; A last, so older kernels, which read only A, read these values
	u32 first = set_read == SET_COPY_B ? SET_RECORD_A : SET_RECORD_B;
	u32 second = first == SET_RECORD_A ? SET_RECORD_B : SET_RECORD_A;
	if (!set_write(first, SET_info_buffer, seq))
		return 0;
	set_read = first == SET_RECORD_A ? SET_COPY_A : SET_COPY_B;
	set_copy = first;
	set_seq = seq;
	if (!set_write(second, SET_info_buffer, seq)) {
		// Read again: where both share a sector, the second's erase took
		// the first too
		set_pick();
		if (set_seq != seq || set_read == SET_COPY_NONE)
			return 0;
		tlog("nor: wrote the settings, copy %c (%lu); the other next time", first == SET_RECORD_A ? 'A' : 'B',
		     (unsigned long)seq);
		return 1;
	}
	// Both whole with one number: A is read, as older kernels do
	set_read = SET_COPY_A;
	set_copy = SET_RECORD_A;
	tlog("nor: wrote the settings, copies %c and %c (%lu)", first == SET_RECORD_A ? 'A' : 'B',
	     second == SET_RECORD_A ? 'A' : 'B', (unsigned long)seq);
	return 1;
}
// --------------------------------------------------------------------
// Torii: SETTINGS' saves, staged (Stage_SET_info) until Commit_SET_info writes them
static u16 set_staged[assress_max] EWRAM_BSS;
static u8 set_staging;  // in IWRAM: a launch may fill EWRAM, then read the settings

u16 IWRAM_CODE Read_SET_info(u32 offset)
{
	if (set_staging && offset < assress_max)
		return set_staged[offset];
	if (!set_copy)
		set_pick();
	return *((vu16 *)(FlashBase_S71 + set_copy + offset * 2));
}
// --------------------------------------------------------------------
void Stage_SET_info(const u16 *SET_info_buffer)
{
	memcpy(set_staged, SET_info_buffer, sizeof(set_staged));
	set_staging = 1;
	tlog_debug("settings: staged");
}
// --------------------------------------------------------------------
void Discard_SET_info(void)
{
	set_staging = 0;
}
// --------------------------------------------------------------------
u32 Commit_SET_info(u16 *SET_info_buffer)
{
	if (!set_staging)
		return SET_COMMIT_NONE;
	set_staging = 0;  // Read_SET_info reads the NOR again
	u32 changed = 0;
	for (u32 address = 0; address < assress_max; address++)
		if (Read_SET_info(address) != set_staged[address])
			changed++;
	if (!changed) {
		tlog_debug("settings: nothing changed, not written");
		return SET_COMMIT_NONE;
	}
	memcpy(SET_info_buffer, set_staged, sizeof(set_staged));
	if (!Save_SET_info(SET_info_buffer)) {
		set_staging = 1;  // still what Read_SET_info reads, for a retry
		return SET_COMMIT_FAILED;
	}
	return SET_COMMIT_WRITTEN;
}
// --------------------------------------------------------------------
u16 IWRAM_CODE Read_sav_info(u32 offset)
{
	u32 k = sav_slot();
	if (k == SAV_SLOTS)
		return 0;  // every slot used: nothing waits
	return *((vu16 *)(FlashBase_S71 + SAVE_info_offset + k * SAV_SLOT_BYTES + offset * 2));
}
// --------------------------------------------------------------------
void IWRAM_CODE SetSPIControl(u16 control)
{
	u16 waitcnt = WAITCNT_SLOW();
	*(u16 *)0x9fe0000 = 0xd200;
	*(u16 *)0x8000000 = 0x1500;
	*(u16 *)0x8020000 = 0xd200;
	*(u16 *)0x8040000 = 0x1500;
	*(u16 *)0x9660000 = control;
	*(u16 *)0x9fc0000 = 0x1500;
	WAITCNT_RESTORE(waitcnt);
}
// --------------------------------------------------------------------
void IWRAM_CODE SPI_Enable(void)
{
	SetSPIControl(1);
}
// --------------------------------------------------------------------
void IWRAM_CODE SPI_Disable(void)
{
	SetSPIControl(0);
}
// --------------------------------------------------------------------
u16 IWRAM_CODE Read_FPGA_ver(void)
{
	u16 Read_SPI;
	SPI_Enable();
	Read_SPI = *(vu16 *)0x9E00000;
	SPI_Disable();
	return Read_SPI;
}
// --------------------------------------------------------------------
void IWRAM_CODE SetSPIWrite(u16 control)
{
	u16 waitcnt = WAITCNT_SLOW();
	*(u16 *)0x9fe0000 = 0xd200;
	*(u16 *)0x8000000 = 0x1500;
	*(u16 *)0x8020000 = 0xd200;
	*(u16 *)0x8040000 = 0x1500;
	*(u16 *)0x9680000 = control;
	*(u16 *)0x9fc0000 = 0x1500;
	WAITCNT_RESTORE(waitcnt);
}
// --------------------------------------------------------------------
void IWRAM_CODE SPI_Write_Enable(void)
{
	SetSPIWrite(1);
}
// --------------------------------------------------------------------
void IWRAM_CODE SPI_Write_Disable(void)
{
	SetSPIWrite(0);
}
// --------------------------------------------------------------------
void IWRAM_CODE Set_RTC_status(u16 status)
{
	u16 waitcnt = WAITCNT_SLOW();
	*(u16 *)0x9fe0000 = 0xd200;
	*(u16 *)0x8000000 = 0x1500;
	*(u16 *)0x8020000 = 0xd200;
	*(u16 *)0x8040000 = 0x1500;
	*(u16 *)0x96A0000 = status;
	*(u16 *)0x9fc0000 = 0x1500;
	WAITCNT_RESTORE(waitcnt);
}
// --------------------------------------------------------------------
// The cart's rumble motor, driven as Jagoomba's Omega DE build does
// (MBC5RAMB in its src/mappers.s): 0xF1 at 0x09E20000 gives 0x08001000 to
// the motor, where 2 starts it and 0 stops it. Jagoomba pulses it, on then off
// at once, each frame the game asks for rumble. The motor also needs the GPIO
// port on (0x096A0000 bit 0), and takes the write only while the ROM page
// isn't the kernel's: under it, the write goes to the kernel NOR, which
// ignores it as no flash command (real cart, 2026-10-18). Holding 2 works too.
void IWRAM_CODE Set_rumble(u16 on)
{
	u16 waitcnt = WAITCNT_SLOW();
	*(u16 *)0x9fe0000 = 0xd200;
	*(u16 *)0x8000000 = 0x1500;
	*(u16 *)0x8020000 = 0xd200;
	*(u16 *)0x8040000 = 0x1500;
	*(u16 *)0x9E20000 = 0xF1;
	*(u16 *)0x9fc0000 = 0x1500;
	*(vu16 *)0x8001000 = on ? 2 : 0;
	WAITCNT_RESTORE(waitcnt);
}
// --------------------------------------------------------------------
/*void IWRAM_CODE Set_AUTO_save(u16  mode)
{
	*(u16 *)0x9fe0000 = 0xd200;
	*(u16 *)0x8000000 = 0x1500;
	*(u16 *)0x8020000 = 0xd200;
	*(u16 *)0x8040000 = 0x1500;
	*(u16 *)0x96C0000 = mode;
	*(u16 *)0x9fc0000 = 0x1500;
}*/
// --------------------------------------------------------------------
void IWRAM_CODE Set_LED_control(u16 status)
{
	u16 waitcnt = WAITCNT_SLOW();
	*(u16 *)0x9fe0000 = 0xd200;
	*(u16 *)0x8000000 = 0x1500;
	*(u16 *)0x8020000 = 0xd200;
	*(u16 *)0x8040000 = 0x1500;
	*(u16 *)0x96E0000 = status;
	*(u16 *)0x9fc0000 = 0x1500;
	WAITCNT_RESTORE(waitcnt);
}
// --------------------------------------------------------------------
void IWRAM_CODE Set_64MROM_flag(u16 flag)
{
	u16 waitcnt = WAITCNT_SLOW();
	*(u16 *)0x9fe0000 = 0xd200;
	*(u16 *)0x8000000 = 0x1500;
	*(u16 *)0x8020000 = 0xd200;
	*(u16 *)0x8040000 = 0x1500;
	*(u16 *)0x9700000 = flag;
	*(u16 *)0x9fc0000 = 0x1500;
	WAITCNT_RESTORE(waitcnt);
}
// --------------------------------------------------------------------
// Torii: the screens are dialogs on the engine, in one session of their own
// (ui/dialog.h), with texts from lang/en.ini's [firmware], in English: no pack is loaded yet; the CRC check, the SPI writes and
// their order are the official ones. Two screens come before a write: the
// versions with a warning about power cuts (A goes on), then L+R+A held for
// two seconds; B on either, or the hold let go early, cancels and the boot
// goes on. Keys act on release, not on the press, so one A never writes.
void IWRAM_CODE FW_update(u16 DEcard_FW_readver, const char *rev, u16 FW_built_in_ver, void *FWbinaddress, u32 FWsize,
			  u32 build_crc32, u32 FW_wirte_address)
{
	vu16 busy;
	vu32 offset;
	char title[32];
	char msg[160];
	char status[24];

	SF(title, sizeof(title), STR_FIRMWARE_TITLE, LS(rev));
	dialog_open();

	// A notice while the CRC check reads the image (about half a second)
	dialog_notice(title, S(STR_FIRMWARE_CHECKING), DIALOG_INFO);
	u32 get_crc32 = crc32(FWbinaddress, FWsize);
	dialog_notice_end();

	if (get_crc32 != build_crc32) {
		tlog_error("firmware: the kernel's V%02d image fails its CRC32 (%08lX, not %08lX): not written",
			   FW_built_in_ver, (unsigned long)get_crc32, (unsigned long)build_crc32);
		dialog_message(title, S(STR_FIRMWARE_CRC_ERROR), DIALOG_ERROR);
		dialog_close();
		return;
	}

	// No key hint: the language isn't loaded yet, and the text names the keys
	SF(msg, sizeof(msg), STR_FIRMWARE_WARNING, LZ(DEcard_FW_readver & 0xFF, 2), LZ(FW_built_in_ver, 2));
	if (dialog_confirm(title, msg, DIALOG_INFO) &&
	    dialog_hold(title, S(STR_FIRMWARE_HOLD), KEY_L | KEY_R | KEY_A, FW_HOLD_FRAMES)) {
		tlog("firmware: writing V%02d over V%02d", FW_built_in_ver, DEcard_FW_readver & 0xFF);
		SPI_Write_Disable();
		SF(msg, sizeof(msg), STR_FIRMWARE_UPDATING, LZ(DEcard_FW_readver & 0xFF, 2), LZ(FW_built_in_ver, 2));
		dialog_progress_begin(title, msg);

		for (offset = 0x0000; offset < FWsize; offset += 256) {
			SF(status, sizeof(status), STR_FIRMWARE_PROGRESS, LN(offset * 100 / FWsize + 1));
			dialog_progress_set(offset, FWsize, status);  // drawn a few times a second

			FAT_table_buffer[0] = (FW_wirte_address + offset);//omega DE 0x80000

			dmaCopy(FWbinaddress + offset, &FAT_table_buffer[1], 256);
			Send_FATbuffer(FAT_table_buffer, 2);

			SPI_Write_Enable();
			while (1) {
				busy = SD_Response();
				if (busy == 0)
					break;
			}
			SPI_Write_Disable();
			//DEBUG_printf("count %x ",count);
			//break;
		}
		dialog_progress_set(FWsize, FWsize, NULL);
		dialog_progress_status(S(STR_FIRMWARE_DONE));
		tlog("firmware: updated to V%02d, waiting for the power-off", FW_built_in_ver);

		while (1)
			ui_frame();
	}
	tlog("firmware: update cancelled, V%02d kept", DEcard_FW_readver & 0xFF);
	dialog_close();
}
// --------------------------------------------------------------------
// Torii: writes only to a board on the allowlist (fpga_board.h): an LX16
// whose firmware is older than the kernel's. Any other word writes nothing
// and shows nothing; it is logged. EZ-Flash's source took every word it
// didn't know as LX9; the LX9 path stays, unreachable until a real Rev.A's
// word is measured and allowlisted.
void IWRAM_CODE Check_FW_update(/*u16 Current_FW_ver,u16 Built_in_ver*/)
{
	u16 DEcard_FW_readver = Read_FPGA_ver();
	u16 DEcard_FW_ver = fpga_firmware(DEcard_FW_readver);
	u16 again = Read_FPGA_ver();

	// Two reads that differ: a noisy bus, so no board is known
	if (again != DEcard_FW_readver) {
		tlog("firmware: FPGA version word read as %04X, then %04X: no update", DEcard_FW_readver, again);
		return;
	}
	switch (fpga_board(DEcard_FW_readver)) {
	case FPGA_BOARD_LX16:
		tlog("firmware: FPGA version word %04X, LX16 on V%02d, the kernel has V%02d", DEcard_FW_readver,
		     DEcard_FW_ver, LX16_FW_built_in_ver);
		if (fpga_update_due(DEcard_FW_readver, LX16_FW_built_in_ver)) {
			FW_update(DEcard_FW_readver, "B", LX16_FW_built_in_ver, LX16_newomega_top_bin_address,
				  LX16_newomega_top_bin_size, LX16_FW_crc32, LX16_wirte_address);
		}
		break;
	case FPGA_BOARD_LX9:  // never: no Rev.A word is on the allowlist yet
		if (fpga_update_due(DEcard_FW_readver, LX9_FW_built_in_ver)) {
			FW_update(DEcard_FW_readver, "A", LX9_FW_built_in_ver, LX9_newomega_top_bin_address,
				  LX9_newomega_top_bin_size, LX9_FW_crc32, LX9_wirte_address);
		}
		break;
	default:
		tlog("firmware: FPGA version word %04X isn't a known board: no update", DEcard_FW_readver);
		break;
	}
}
