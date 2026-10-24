/*-----------------------------------------------------------------------*/
/* Low level disk I/O module SKELETON for FatFs     (C)ChaN, 2019        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/

#include "ff.h"			/* Obtains integer types */
#include "diskio.h"		/* Declarations of disk functions */
#include "../Ezcard_OP.h"
#include "../RTC.h"
#include "../tlog.h"
#include "../diskstat.h"
#include "../sdcache.h"
/* Definitions of physical drive number for each drive */
//#define DEV_RAM		0	/* Example: Map Ramdisk to physical drive 0 */
//#define DEV_MMC		1	/* Example: Map MMC/SD card to physical drive 1 */
//#define DEV_USB		2	/* Example: Map USB MSD to physical drive 2 */


/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status (
	BYTE pdrv		/* Physical drive nmuber to identify the drive */
)
{
	/*DSTATUS stat;
	int result;

	switch (pdrv) {
	case DEV_RAM :
		result = RAM_disk_status();

		// translate the reslut code here

		return stat;

	case DEV_MMC :
		result = MMC_disk_status();

		// translate the reslut code here

		return stat;

	case DEV_USB :
		result = USB_disk_status();

		// translate the reslut code here

		return stat;
	}
	return STA_NOINIT;*/
	return RES_OK;
}



/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (
	BYTE pdrv				/* Physical drive nmuber to identify the drive */
)
{
	/*DSTATUS stat;
	int result;

	switch (pdrv) {
	case DEV_RAM :
		result = RAM_disk_initialize();

		// translate the reslut code here

		return stat;

	case DEV_MMC :
		result = MMC_disk_initialize();

		// translate the reslut code here

		return stat;

	case DEV_USB :
		result = USB_disk_initialize();

		// translate the reslut code here

		return stat;
	}
	return STA_NOINIT;*/
	sdcache_clear();	/* Torii: a new mount reads the card afresh */
	return RES_OK;
}



/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read (
	BYTE pdrv,		/* Physical drive nmuber to identify the drive */
	BYTE *buff,		/* Data buffer to store read data */
	LBA_t sector,	/* Start sector in LBA */
	UINT count		/* Number of sectors to read */
)
{
	/*DRESULT res;
	int result;

	switch (pdrv) {
	case DEV_RAM :
		// translate the arguments here

		result = RAM_disk_read(buff, sector, count);

		// translate the reslut code here

		return res;

	case DEV_MMC :
		// translate the arguments here

		result = MMC_disk_read(buff, sector, count);

		// translate the reslut code here

		return res;

	case DEV_USB :
		// translate the arguments here

		result = USB_disk_read(buff, sector, count);

		// translate the reslut code here

		return res;
	}

	return RES_PARERR;*/
	/* Torii: a single sector may be in the cache (sdcache.h); 1 when the
	   card didn't answer (Read_SD_sectors) */
	if (count == 1 && sdcache_read(sector, buff)) {
		diskstat_total.cache_hits++;
		return RES_OK;
	}
	diskstat_total.reads++;
	diskstat_total.read_sectors += count;
	if (Read_SD_sectors(sector, count, buff)) {
		tlog_error("sd: read of %lu sectors at %lu failed", (unsigned long)count, (unsigned long)sector);
		return RES_ERROR;
	}
	if (count == 1)
		sdcache_fill(sector, buff);
	return RES_OK;
}



/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if FF_FS_READONLY == 0

DRESULT disk_write (
	BYTE pdrv,			/* Physical drive nmuber to identify the drive */
	const BYTE *buff,	/* Data to be written */
	LBA_t sector,		/* Start sector in LBA */
	UINT count			/* Number of sectors to write */
)
{
	/*DRESULT res;
	int result;

	switch (pdrv) {
	case DEV_RAM :
		// translate the arguments here

		result = RAM_disk_write(buff, sector, count);

		// translate the reslut code here

		return res;

	case DEV_MMC :
		// translate the arguments here

		result = MMC_disk_write(buff, sector, count);

		// translate the reslut code here

		return res;

	case DEV_USB :
		// translate the arguments here

		result = USB_disk_write(buff, sector, count);

		// translate the reslut code here

		return res;
	}

	return RES_PARERR;*/
	DRESULT res;
	/* Torii: ROM reads while SD mode is on return the cart's SD buffer, so
	   a source in ROM would write that instead: refuse it. */
	TLOG_ASSERT((DWORD)buff < 0x08000000);
	if ((DWORD)buff >= 0x08000000)
		return RES_PARERR;
	diskstat_total.writes++;
	diskstat_total.write_sectors += count;
	res = Write_SD_sectors(sector, count, buff) ? RES_ERROR : RES_OK;
	sdcache_wrote(sector, count, buff, 0, res == RES_OK);
	if (res != RES_OK)
		tlog_error("sd: write of %lu sectors at %lu failed", (unsigned long)count, (unsigned long)sector);
	return res;
}

#endif


/*-----------------------------------------------------------------------*/
/* Torii: Write count copies of one sector (a new folder's zeros)         */
/*-----------------------------------------------------------------------*/

DRESULT disk_write_fill (
	BYTE pdrv,
	const BYTE *buff,	/* The sector's 512 bytes, in RAM */
	LBA_t sector,
	UINT count
)
{
	(void)pdrv;
	TLOG_ASSERT((DWORD)buff < 0x08000000);
	if ((DWORD)buff >= 0x08000000)
		return RES_PARERR;
	diskstat_total.writes++;
	diskstat_total.write_sectors += count;
	if (Write_SD_fill(sector, count, buff)) {
		sdcache_wrote(sector, count, buff, 1, 0);
		tlog_error("sd: fill of %lu sectors at %lu failed", (unsigned long)count, (unsigned long)sector);
		return RES_ERROR;
	}
	sdcache_wrote(sector, count, buff, 1, 1);
	return RES_OK;
}

/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl (
	BYTE pdrv,		/* Physical drive nmuber (0..) */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
	/*DRESULT res;
	int result;

	switch (pdrv) {
	case DEV_RAM :

		// Process of the command for the RAM drive

		return res;

	case DEV_MMC :

		// Process of the command for the MMC/SD card

		return res;

	case DEV_USB :

		// Process of the command the USB drive

		return res;
	}

	return RES_PARERR;*/
	return RES_OK;
}
/*-----------------------------------------------------------------------*/
/* Get omega card time Functions                                               */
/*-----------------------------------------------------------------------*/
DWORD get_fattime (void)
{
		u8 datetime[7];
		rtc_enable();
		rtc_get(datetime);
		rtc_disenable();	
		return ((DWORD)(UNBCD(datetime[0])+20) << 25 | (DWORD)UNBCD(datetime[1]) << 21 | (DWORD)UNBCD(datetime[2]&0x3F) << 16 | (DWORD)UNBCD(datetime[4]&0x3F) << 11 | (DWORD)UNBCD(datetime[5]) << 5  | (DWORD)UNBCD(datetime[6]) >> 1   );
}

