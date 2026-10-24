// Builds and fills emulated SD card images with the kernel's own FatFs, so
// tests can use exFAT (which mtools can't write) and UTF-8 names. Built and
// run by tools/sdimg.sh inside the torii-emu container.
//
//   sdimg IMG mkfs fat16|fat32|exfat MEGABYTES
//   sdimg IMG mkdir PATH
//   sdimg IMG put LOCAL_FILE PATH
//   sdimg IMG put-fragmented LOCAL_FILE PATH RUNS  (in RUNS runs of clusters)
//   sdimg IMG get PATH LOCAL_FILE
//   sdimg IMG ls PATH
//   sdimg IMG rm PATH
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ff.h"
#include "diskio.h"

static FILE *img;

DSTATUS disk_initialize(BYTE pdrv)
{
	(void)pdrv;
	return img ? 0 : STA_NOINIT;
}

DSTATUS disk_status(BYTE pdrv)
{
	(void)pdrv;
	return img ? 0 : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
	(void)pdrv;
	if (fseek(img, (long)sector * 512, SEEK_SET) || fread(buff, 512, count, img) != count)
		return RES_ERROR;
	return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
	(void)pdrv;
	if (fseek(img, (long)sector * 512, SEEK_SET) || fwrite(buff, 512, count, img) != count)
		return RES_ERROR;
	return RES_OK;
}

// The kernel's diskio.c writes a new folder's zeros with one call (ff.c's
// dir_clear): count copies of the sector at buff
DRESULT disk_write_fill(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
	for (UINT i = 0; i < count; i++) {
		if (disk_write(pdrv, buff, sector + i, 1) != RES_OK)
			return RES_ERROR;
	}
	return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
	(void)pdrv;
	switch (cmd) {
	case CTRL_SYNC:
		return fflush(img) ? RES_ERROR : RES_OK;
	case GET_SECTOR_COUNT:
		fseek(img, 0, SEEK_END);
		*(LBA_t *)buff = (LBA_t)(ftell(img) / 512);
		return RES_OK;
	case GET_BLOCK_SIZE:
		*(DWORD *)buff = 1;
		return RES_OK;
	}
	return RES_PARERR;
}

static int fail(const char *what, FRESULT res)
{
	fprintf(stderr, "sdimg: %s failed (FatFs error %d)\n", what, (int)res);
	return 1;
}

static int mkfs(const char *path, const char *type, long megabytes)
{
	static BYTE work[FF_MAX_SS * 64];
	img = fopen(path, "wb+");
	if (!img)
		return fail("creating the image", FR_DENIED);
	if (fseek(img, megabytes * 1024 * 1024 - 1, SEEK_SET) || fputc(0, img) == EOF)
		return fail("sizing the image", FR_DISK_ERR);
	MKFS_PARM opt = {.fmt = strcmp(type, "exfat") == 0 ? FM_EXFAT : strcmp(type, "fat16") == 0 ? FM_FAT : FM_FAT32};
	FRESULT res = f_mkfs("", &opt, work, sizeof(work));
	if (res != FR_OK)
		return fail("f_mkfs", res);
	FATFS fs;
	f_mount(&fs, "", 1);
	f_setlabel("OMEGA");
	return 0;
}

static int put(const char *local, const char *path)
{
	FILE *in = fopen(local, "rb");
	if (!in) {
		perror(local);
		return 1;
	}
	FIL f;
	FRESULT res = f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS);
	if (res != FR_OK)
		return fail("f_open", res);
	static BYTE buf[65536];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
		UINT done;
		res = f_write(&f, buf, (UINT)n, &done);
		if (res != FR_OK || done != n)
			return fail("f_write", res);
	}
	fclose(in);
	return f_close(&f) == FR_OK ? 0 : fail("f_close", FR_DISK_ERR);
}

// Writes the file one cluster at a time with a one-cluster filler file
// (/FILLnnnn.BIN) after each, so its first RUNS - 1 clusters are separate runs
static int put_fragmented(const char *local, const char *path, long runs, FATFS *fs)
{
	FILE *in = fopen(local, "rb");
	if (!in) {
		perror(local);
		return 1;
	}
	UINT cluster = fs->csize * 512;
	FIL f;
	FRESULT res = f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS);
	if (res != FR_OK)
		return fail("f_open", res);
	static BYTE buf[65536];
	size_t n;
	for (long run = 1; (n = fread(buf, 1, run < runs ? cluster : sizeof(buf), in)) > 0; run++) {
		UINT done;
		res = f_write(&f, buf, (UINT)n, &done);
		if (res != FR_OK || done != n)
			return fail("f_write", res);
		if (run < runs) {
			char filler[32];
			FIL g;
			snprintf(filler, sizeof(filler), "/FILL%04ld.BIN", run);
			if ((res = f_open(&g, filler, FA_WRITE | FA_CREATE_ALWAYS)) != FR_OK ||
			    (res = f_write(&g, buf, 1, &done)) != FR_OK || (res = f_close(&g)) != FR_OK)
				return fail("writing a filler file", res);
		}
	}
	fclose(in);
	return f_close(&f) == FR_OK ? 0 : fail("f_close", FR_DISK_ERR);
}

static int get(const char *path, const char *local)
{
	FIL f;
	FRESULT res = f_open(&f, path, FA_READ);
	if (res != FR_OK)
		return fail("f_open", res);
	FILE *out = fopen(local, "wb");
	if (!out) {
		perror(local);
		return 1;
	}
	static BYTE buf[65536];
	UINT n;
	while ((res = f_read(&f, buf, sizeof(buf), &n)) == FR_OK && n > 0)
		fwrite(buf, 1, n, out);
	fclose(out);
	f_close(&f);
	return res == FR_OK ? 0 : fail("f_read", res);
}

static int ls(const char *path)
{
	DIR dir;
	FILINFO info;
	FRESULT res = f_opendir(&dir, path);
	if (res != FR_OK)
		return fail("f_opendir", res);
	while (f_readdir(&dir, &info) == FR_OK && info.fname[0])
		printf("%s%s\t%lu\n", info.fname, (info.fattrib & AM_DIR) ? "/" : "", (unsigned long)info.fsize);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: sdimg IMG mkfs|mkdir|put|get|ls|rm ARGS...\n");
		return 2;
	}
	const char *cmd = argv[2];
	if (strcmp(cmd, "mkfs") == 0 && argc == 5)
		return mkfs(argv[1], argv[3], atol(argv[4]));

	img = fopen(argv[1], "rb+");
	if (!img) {
		perror(argv[1]);
		return 1;
	}
	FATFS fs;
	FRESULT res = f_mount(&fs, "", 1);
	if (res != FR_OK)
		return fail("f_mount", res);
	int status = 2;
	if (strcmp(cmd, "mkdir") == 0 && argc == 4) {
		res = f_mkdir(argv[3]);
		status = (res == FR_OK || res == FR_EXIST) ? 0 : fail("f_mkdir", res);
	} else if (strcmp(cmd, "put") == 0 && argc == 5) {
		status = put(argv[3], argv[4]);
	} else if (strcmp(cmd, "put-fragmented") == 0 && argc == 6) {
		status = put_fragmented(argv[3], argv[4], atol(argv[5]), &fs);
	} else if (strcmp(cmd, "get") == 0 && argc == 5) {
		status = get(argv[3], argv[4]);
	} else if (strcmp(cmd, "ls") == 0 && argc == 4) {
		status = ls(argv[3]);
	} else if (strcmp(cmd, "rm") == 0 && argc == 4) {
		res = f_unlink(argv[3]);
		status = res == FR_OK ? 0 : fail("f_unlink", res);
	} else {
		fprintf(stderr, "sdimg: bad command\n");
	}
	f_unmount("");
	fclose(img);
	return status;
}
