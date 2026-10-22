// The power-cut rig's scenario (tests/cutpoint.sh): a few FatFs steps with a
// cutpoint after each, run alone by the test kernel started with SELECT and A
// held. The card holds /CUT/old.txt ("OLD"), made by the test. Not a unit
// test: it is not in run.c's lists, since a cut or a missing card is its
// point.
#include <string.h>

#include "ff.h"
#include "tests.h"
#include "torii_test.h"
#include "../../source/cutpoint.h"

extern FATFS EZcardFs;

// Word-aligned: FatFs hands whole sectors to the card driver
static u32 buf[4];

static void write_file(const char *path, const char *text, int sync)
{
	FIL f;
	UINT n;
	strcpy((char *)buf, text);
	if (f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
		return;
	f_write(&f, buf, strlen(text), &n);
	if (sync)
		f_sync(&f);
	f_close(&f);
}

void cut_scenario(void)
{
	if (!EZcardFs.fs_type && f_mount(&EZcardFs, "", 1) != FR_OK) {
		tlog_error("cut scenario: no card");
		return;
	}
	// 1: a file written whole (f_close writes the data and its entry)
	write_file("/CUT/new.tmp", "NEW", 1);
	cutpoint("new.tmp written");
	// 2: the old file moved aside
	f_rename("/CUT/old.txt", "/CUT/old.bak");
	cutpoint("old.txt renamed to old.bak");
	// 3: the new file in its place
	f_rename("/CUT/new.tmp", "/CUT/old.txt");
	cutpoint("new.tmp renamed to old.txt");
	// 4: a file written and not closed: FatFs holds its data in the file's
	// own sector buffer and its entry in the window, and a cut loses both
	FIL f;
	UINT n;
	if (f_open(&f, "/CUT/open.txt", FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
		strcpy((char *)buf, "OPEN");
		f_write(&f, buf, 4, &n);
		cutpoint("open.txt written, not closed");
		f_close(&f);
	}
	cutpoint("open.txt closed");
	tlog("CUT SCENARIO DONE");
}
