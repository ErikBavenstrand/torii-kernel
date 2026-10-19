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
#include "ezkernel.h"
#include "ui/dialog.h"
#include "ui/ui.h"
#include "Ezcard_OP.h"
#include "tlog.h"
#include "waitcnt.h"

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
	u32 times = 2;
	for (i = 0; i < count; i += 4) {
		blocks = (count - i > 4) ? 4 : (count - i);

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
void IWRAM_CODE Send_FATbuffer(u32 *buffer, u32 mode)
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
	// boot loader, and below KERNEL_NOR_DATA the kernel (bootloader.h)
	if (!kernel_nor_record_ok(info_offset, buffersize)) {
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

void IWRAM_CODE Save_sav_info(u16 *SAV_info_buffer, u32 buffersize)
{
	log_info_write(SAV_info_buffer[0] == 0x11 ? "the pending-save record" : "an empty pending-save record",
		       buffersize, Save_info(SAVE_info_offset, SAV_info_buffer, buffersize));
}
// --------------------------------------------------------------------
void IWRAM_CODE Save_SET_info(u16 *SET_info_buffer, u32 buffersize)
{
	log_info_write("the settings", buffersize, Save_info(SET_info_offset, SET_info_buffer, buffersize));
}
// --------------------------------------------------------------------
u16 IWRAM_CODE Read_SET_info(u32 offset)
{
	return *((vu16 *)(FlashBase_S71 + SET_info_offset + offset * 2));
}
// --------------------------------------------------------------------
u16 IWRAM_CODE Read_sav_info(u32 offset)
{
	return *((vu16 *)(FlashBase_S71 + SAVE_info_offset + offset * 2));
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
// The cart's rumble motor, driven as EZ-Flash's Goomba (emusrc/src/mappers.s)
// and the bundled Jagoomba drive it: 0xF1 at 0x09E20000 gives 0x08001000 to
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
// (ui/dialog.h), with the official texts; the CRC check, the SPI writes and
// their order are the official ones. Keys act on release, not on the press.
void IWRAM_CODE FW_update(u16 DEcard_FW_readver, u16 FW_built_in_ver, void *FWbinaddress, u32 FWsize, u32 build_crc32,
			  u32 FW_wirte_address)
{
	vu16 busy;
	vu32 offset;
	char title[32];
	char msg[128];
	char status[16];

	sprintf(title, "Rev.%s FIRMWARE UPDATE", ((DEcard_FW_readver & 0xA000) == 0xA000) ? "B" : "A");
	dialog_open();

	u32 get_crc32 = crc32(FWbinaddress, FWsize);
	//DEBUG_printf("get_crc32 %x ",get_crc32);

	if (get_crc32 != build_crc32) {
		dialog_message(title, "check crc32 error!\npress [B] to return", DIALOG_ERROR);
		dialog_close();
		return;
	}

	// No key hint: the language isn't loaded yet, and the text names the keys
	sprintf(msg,
		"Current firmware version: V%02d\nWill be updated to version: V%02d\nPress [A] to update\nPress [B] to "
		"cancel",
		DEcard_FW_readver & 0xFF, FW_built_in_ver);
	if (dialog_confirm(title, msg, DIALOG_INFO)) {
		SPI_Write_Disable();
		sprintf(msg, "Current firmware version: V%02d\nWill be updated to version: V%02d",
			DEcard_FW_readver & 0xFF, FW_built_in_ver);
		dialog_progress_begin(title, msg);

		for (offset = 0x0000; offset < FWsize; offset += 256) {
			sprintf(status, "progress: %lu%%", (offset * 100 / FWsize + 1));
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
		dialog_progress_status("update finished,power off manual");
		tlog("firmware: updated to V%02d, waiting for the power-off", FW_built_in_ver);

		while (1)
			ui_frame();
	}
	dialog_close();
}
// --------------------------------------------------------------------
void IWRAM_CODE Check_FW_update(/*u16 Current_FW_ver,u16 Built_in_ver*/)
{
	u16 DEcard_FW_readver = Read_FPGA_ver();
	u16 DEcard_FW_ver = DEcard_FW_readver & 0x00FF;

	if ((DEcard_FW_readver & 0xF000) == 0xB000 ||
	    (DEcard_FW_readver & 0xF000) == 0xA000) { //lx16  note that, 0xA000 is initially,fix here
		if (DEcard_FW_ver < LX16_FW_built_in_ver) {
			FW_update(DEcard_FW_readver, LX16_FW_built_in_ver, LX16_newomega_top_bin_address,
				  LX16_newomega_top_bin_size, LX16_FW_crc32, LX16_wirte_address);
		}
	} else {//lx9
		if (DEcard_FW_ver < LX9_FW_built_in_ver) {
			FW_update(DEcard_FW_readver, LX9_FW_built_in_ver, LX9_newomega_top_bin_address,
				  LX9_newomega_top_bin_size, LX9_FW_crc32, LX9_wirte_address);
		}
	}
}
