#include <gba_base.h>

#include "ff.h"
#include "crc32.h"

void IWRAM_CODE SD_Disable(void);
void SetSDControl(u16 control);
u16 IWRAM_CODE SD_Response(void);
u32 Read_SD_sectors(u32 address, u16 count, u8 *SDbuffer);
u32 Write_SD_sectors(u32 address, u16 count, const u8 *SDbuffer);
u16 IWRAM_CODE Read_S71NOR_ID(void);
u16 Read_S98NOR_ID(void);
void IWRAM_CODE SetRompage(u16 page);
void IWRAM_CODE SetbufferControl(u16 control);
void SetPSRampage(u16 page);
void SetRampage(u16 page);
void IWRAM_CODE Progress(u16 x, u16 y, u16 w, u16 h, u16 c, u8 isDrawDirect);
void IWRAM_CODE Send_FATbuffer(u32 *buffer, u32 mode);
void IWRAM_CODE SetRompageWithHardReset(u16 page, u32 bootmode);
#define GAME_USES_CLOCK 1
#define GAME_USES_RUMBLE 2
extern u32 gl_game_uses;
u16 IWRAM_CODE Game_GPIO_status(void);
void ReadSram(u32 address, u8 *data, u32 size);
void WriteSram(u32 address, u8 *data, u32 size);
void IWRAM_CODE Save_sav_info(u16 *SAV_info_buffer, u32 buffersize);
void IWRAM_CODE Save_SET_info(u16 *SET_info_buffer, u32 buffersize);
u16 IWRAM_CODE Read_SET_info(u32 offset);
u32 Loadfile2PSRAM(TCHAR *filename);
u16 IWRAM_CODE Read_FPGA_ver(void);
void IWRAM_CODE Set_RTC_status(u16 status);
void IWRAM_CODE Set_LED_control(u16 status);
void IWRAM_CODE Set_64MROM_flag(u16 flag);
void IWRAM_CODE Check_FW_update(void);
u16 IWRAM_CODE Read_sav_info(u32 offset);
void IWRAM_CODE Bank_Switching(u8 bank);
// IWRAM code must run from IWRAM while the ROM page changes, so it is never
// static: GCC would inline it into callers in ROM.
void IWRAM_CODE SD_Enable(void);
void IWRAM_CODE SD_Read_state(void);
u32 IWRAM_CODE Wait_SD_Response(void);
void IWRAM_CODE Save_info(u32 info_offset, u16 *info_buffer, u32 buffersize);
void IWRAM_CODE SetSPIControl(u16 control);
void IWRAM_CODE SPI_Enable(void);
void IWRAM_CODE SPI_Disable(void);
void IWRAM_CODE SetSPIWrite(u16 control);
void IWRAM_CODE SPI_Write_Enable(void);
void IWRAM_CODE SPI_Write_Disable(void);
void IWRAM_CODE FW_update(u16 DEcard_FW_readver, u16 FW_built_in_ver, void *FWbinaddress, u32 FWsize, u32 build_crc32,
			  u32 FW_wirte_address);
