#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <gba_base.h>

#include "ff.h"
#include "ez_define.h"
#include "Ezcard_OP.h"
#include "logbuf.h"
#include "tlog.h"

// mGBA's debug registers: write 0xC0DE to enable (it then reads 0x1DEA),
// fill the string, then write the level with the send bit (0x100).
#define REG_DEBUG_ENABLE (*(vu16 *)0x04FFF780)
#define REG_DEBUG_FLAGS (*(vu16 *)0x04FFF700)
#define REG_DEBUG_STRING ((char *)0x04FFF600)
#define DEBUG_SEND 0x100
#define MGBA_ERROR 1
#define MGBA_INFO 3

u8 tlog_level;
static u8 tlog_emu;

static log_ring ring EWRAM_BSS;
// One formatted line: "   12.34 E " and up to 240 bytes of text (mGBA's
// console takes 255).
#define PREFIX_LEN 11
#define TEXT_MAX 240
static char line[PREFIX_LEN + TEXT_MAX + 2] EWRAM_BSS;

u32 tlog_level_setting(void)
{
	u16 value = Read_SET_info(assress_logging);
	return (value <= TLOG_DEBUG) ? value : TLOG_OFF;
}

void tlog_init(void)
{
	REG_DEBUG_ENABLE = 0xC0DE;
	tlog_emu = REG_DEBUG_ENABLE == 0x1DEA;
	tlog_level = tlog_level_setting();
}

const char *tlog_level_name(u32 level)
{
	static const char *const names[] = {"off", "errors", "info", "debug"};
	return (level <= TLOG_DEBUG) ? names[level] : "?";
}

extern int main(void);
extern const u8 __rom_end__;  // the linker's: the end of the kernel's ROM image

#define MARKER "Torii kernel boot: %s, logging %s, build main %08lX end %08lX"

void tlog_marker(const char *why, int force)
{
	// tools/logs.py finds sessions by this line and checks the build with it,
	// so it goes to the card at any level but off.
	u8 level = tlog_level;
	if (force) {
		tlog_force(TLOG_INFO, MARKER, why, tlog_level_name(level), (unsigned long)main,
			   (unsigned long)&__rom_end__);
		return;
	}
	if (tlog_level != TLOG_OFF)
		tlog_level = TLOG_INFO;
	tlog(MARKER, why, tlog_level_name(level), (unsigned long)main, (unsigned long)&__rom_end__);
	tlog_level = level;
}

void tlog_set_level(u32 level)
{
	tlog_level = level;
}

int tlog_in_emulator(void)
{
	return tlog_emu;
}

int tlog_wants(u32 level)
{
	return tlog_emu || level <= tlog_level;
}

static void tlog_send(u32 level, const char *format, va_list args)
{
	int to_card = level <= tlog_level;
	if (!to_card && !tlog_emu)
		return;
	int n = vsnprintf(line + PREFIX_LEN, TEXT_MAX + 1, format, args);
	if (n < 0)
		n = 0;
	if (n > TEXT_MAX)
		n = TEXT_MAX;
	if (tlog_emu) {
		memcpy(REG_DEBUG_STRING, line + PREFIX_LEN, n + 1);
		REG_DEBUG_FLAGS = (level == TLOG_ERRORS ? MGBA_ERROR : MGBA_INFO) | DEBUG_SEND;
	}
	if (!to_card)
		return;
	// Seconds since boot, at about 60 frames a second, and the level.
	u32 centis = torii_frames * 5 / 3;
	char prefix[PREFIX_LEN + 8];
	snprintf(prefix, sizeof(prefix), "%5lu.%02lu %c ", (unsigned long)(centis / 100 % 100000),
		 (unsigned long)(centis % 100), "?EID"[level]);
	memcpy(line, prefix, PREFIX_LEN);
	for (int i = PREFIX_LEN; i < PREFIX_LEN + n; i++) {
		if (line[i] == 0 || line[i] == '\n')  // the files hold text, and zeros end it
			line[i] = ' ';
	}
	line[PREFIX_LEN + n] = '\n';
	log_ring_put(&ring, line, PREFIX_LEN + n + 1);
}

void tlog(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	tlog_send(TLOG_INFO, format, args);
	va_end(args);
}

void tlog_error(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	tlog_send(TLOG_ERRORS, format, args);
	va_end(args);
}

void tlog_debug(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	tlog_send(TLOG_DEBUG, format, args);
	va_end(args);
}

int tlog_all_written(void)
{
	return ring.head == ring.tail && !ring.lost;
}

u32 tlog_recent(char *out, u32 max)
{
	return log_ring_tail(&ring, out, max);
}

// --- The card ---------------------------------------------------------------

enum {
	SD_FAILED = -1,
	SD_NONE = 0,  // not mounted yet
	SD_MOUNTED = 1,  // mounted; the current file not looked for yet
	SD_READY = 2,
};
static s8 sd_state;
static u8 force_pending;
static u8 cur_slot;
static u32 cur_seq;
static u32 cur_end;
static FIL log_file EWRAM_BSS;
static char prev_last[TEXT_MAX + PREFIX_LEN + 1] EWRAM_BSS;

// FatFs hands whole sectors straight to the cart's SD code, which copies
// them with 16-bit DMA: so every buffer given to f_write is word-aligned.
// The one buffer, in RAM: it carries text to the card, zeros into a new
// file, and the file in when looking for its end. (On the cart, ROM reads
// while SD mode is on return the SD buffer, so a source in ROM writes junk.)
static u32 bounce[128] EWRAM_BSS;

static void log_path(char *path, u32 slot)
{
	snprintf(path, 32, "/torii/logs/torii-%lu.log", (unsigned long)(slot % LOG_FILES));
}

static void sd_failed(const char *what, FRESULT res)
{
	sd_state = SD_FAILED;
	f_close(&log_file);
	// Not to the card: it just failed. Logging stops until the next boot.
	u8 level = tlog_level;
	tlog_level = TLOG_OFF;
	tlog_error("log: %s failed (FatFs %d); no more SD logging until the next boot", what, res);
	tlog_level = level;
}

// Free space a new log file must leave on the card, for saves and states.
#define LOG_CARD_RESERVE 0x100000u

// Makes log file slot full size and zero, then writes its header. A rotation
// cut off by power loss leaves a file with no header, which counts as unused.
// It fills bounce with zeros, so call it with nothing waiting there.

static FRESULT start_file(u32 slot, u32 seq)
{
	char path[32];
	char header[LOG_HEADER_LEN];
	UINT done;
	FRESULT res;

	log_path(path, slot);
	res = f_open(&log_file, path, FA_WRITE | FA_OPEN_ALWAYS);
	if (res != FR_OK)
		return res;
	if (f_size(&log_file) < LOG_FILE_SIZE) {
		// A file to make or grow: only if the card keeps room to spare.
		DWORD clusters;
		FATFS *fs;
		res = f_getfree("", &clusters, &fs);
		if (res == FR_OK && (unsigned long long)clusters * fs->csize * 512 <
					    LOG_FILE_SIZE - f_size(&log_file) + LOG_CARD_RESERVE)
			res = FR_DENIED;
		if (res != FR_OK) {
			u32 made = f_size(&log_file) == 0;
			f_close(&log_file);
			if (made)
				f_unlink(path);
			return res;
		}
	}
	memset(bounce, 0, sizeof(bounce));
	for (u32 offset = 0; offset < LOG_FILE_SIZE; offset += sizeof(bounce)) {
		res = f_write(&log_file, bounce, sizeof(bounce), &done);
		if (res == FR_OK && done != sizeof(bounce))
			res = FR_DENIED;  // the card is full
		if (res != FR_OK) {
			// Give the space back: the card's other files need it more.
			f_close(&log_file);
			f_unlink(path);
			return res;
		}
	}
	if (f_size(&log_file) > LOG_FILE_SIZE)
		res = f_truncate(&log_file);
	if (res == FR_OK)
		res = f_sync(&log_file);
	log_header_make(header, seq);
	if (res == FR_OK)
		res = f_lseek(&log_file, 0);
	if (res == FR_OK)
		res = f_write(&log_file, header, LOG_HEADER_LEN, &done);
	FRESULT closed = f_close(&log_file);
	if (res == FR_OK)
		res = closed;
	if (res != FR_OK)
		return res;
	cur_slot = slot;
	cur_seq = seq;
	cur_end = LOG_HEADER_LEN;
	return FR_OK;
}

// Finds the file to append to and where its text ends, and keeps the last
// line the previous session wrote.
static FRESULT open_current(void)
{
	char path[32];
	char buf[LOG_HEADER_LEN];
	u32 seqs[LOG_FILES];
	UINT done;
	FRESULT res;

	f_mkdir("/torii");
	f_mkdir("/torii/logs");
	for (u32 i = 0; i < LOG_FILES; i++) {
		seqs[i] = 0;
		log_path(path, i);
		if (f_open(&log_file, path, FA_READ) != FR_OK)
			continue;
		if (f_size(&log_file) == LOG_FILE_SIZE && f_read(&log_file, buf, sizeof(buf), &done) == FR_OK)
			seqs[i] = log_header_seq(buf, done);
		f_close(&log_file);
	}
	int cur = log_pick_current(seqs);
	if (cur < 0)
		return start_file(0, 1);

	log_path(path, cur);
	res = f_open(&log_file, path, FA_READ);
	if (res != FR_OK)
		return res;
	cur_slot = cur;
	cur_seq = seqs[cur];
	log_end end;
	log_end_start(&end);
	for (u32 offset = 0; offset < LOG_FILE_SIZE && !end.junk; offset += sizeof(bounce)) {
		res = f_read(&log_file, bounce, sizeof(bounce), &done);
		if (res != FR_OK) {
			f_close(&log_file);
			return res;
		}
		if (!done)
			break;
		log_end_feed(&end, (const char *)bounce, done);
	}
	if (end.junk) {
		// Its end can't be found: keep the file as it is, and go on in
		// the next one.
		f_close(&log_file);
		tlog_error("log: torii-%lu.log has junk after its text; going on in the next file",
			   (unsigned long)cur_slot);
		return start_file((cur_slot + 1) % LOG_FILES, cur_seq + 1);
	}
	cur_end = log_end_offset(&end);

	// The previous session's last line, without its newline.
	u32 len = sizeof(prev_last) - 1;
	if (len > cur_end - LOG_HEADER_LEN)
		len = cur_end - LOG_HEADER_LEN;
	if (len > 1 && f_lseek(&log_file, cur_end - len) == FR_OK &&
	    f_read(&log_file, prev_last, len, &done) == FR_OK && done == len) {
		prev_last[--len] = 0;  // the final '\n'
		char *start = strrchr(prev_last, '\n');
		if (start)
			memmove(prev_last, start + 1, strlen(start + 1) + 1);
	}
	return f_close(&log_file);
}

const char *tlog_previous_line(void)
{
	return prev_last[0] ? prev_last : NULL;
}

// Starts the next log file and opens it for writing. It fills bounce with
// zeros (see start_file).
static FRESULT rotate(void)
{
	char path[32];
	f_close(&log_file);
	FRESULT res = start_file((cur_slot + 1) % LOG_FILES, cur_seq + 1);
	if (res != FR_OK)
		return res;
	log_path(path, cur_slot);
	return f_open(&log_file, path, FA_WRITE | FA_OPEN_EXISTING);
}

// Writes as many whole lines of the n bytes of text as fit in the current
// file, and sets *written to their length: 0 once the file is full, whose
// rest then stays zeros. text is word-aligned (see bounce).
static FRESULT write_text(const char *text, u32 n, u32 *written)
{
	UINT done = 0;
	u32 k = n;
	u32 room = (cur_end < LOG_FILE_SIZE) ? LOG_FILE_SIZE - cur_end : 0;
	*written = 0;
	if (k > room) {
		k = room;
		while (k && text[k - 1] != '\n')
			k--;
	}
	if (!k)
		return FR_OK;
	FRESULT res = f_lseek(&log_file, cur_end);
	if (res == FR_OK)
		res = f_write(&log_file, text, k, &done);
	if (res == FR_OK && done != k)
		res = FR_DENIED;
	if (res != FR_OK)
		return res;
	cur_end += k;
	*written = k;
	return FR_OK;
}

static int sd_open(void)
{
	if (sd_state == SD_MOUNTED) {
		FRESULT res = open_current();
		if (res != FR_OK) {
			sd_failed("opening the log", res);
			return 0;
		}
		sd_state = SD_READY;
	}
	return sd_state == SD_READY;
}

void tlog_sd_ready(void)
{
	if (sd_state == SD_NONE)
		sd_state = SD_MOUNTED;
	// Look for the current file now, so the boot report can see how the
	// previous session ended. With logging off the card stays untouched.
	if (tlog_level != TLOG_OFF)
		sd_open();
}

void tlog_flush(void)
{
	char path[32];
	u32 n;
	FRESULT res;

	if (tlog_level == TLOG_OFF && !force_pending)
		return;
	if (ring.head == ring.tail && !ring.lost)
		return;
	if (!sd_open())
		return;
	log_path(path, cur_slot);
	res = f_open(&log_file, path, FA_WRITE | FA_OPEN_EXISTING);
	if (res != FR_OK) {
		sd_failed("opening the log", res);
		return;
	}
	// Text waits in the ring until it is on the card: a file that fills up
	// takes what fits, and the next file is started before bounce is
	// filled again.
	int rotated = 0;
	while (res == FR_OK) {
		u32 lost = ring.lost;
		if (lost) {
			u32 centis = torii_frames * 5 / 3;
			n = snprintf((char *)bounce, sizeof(bounce),
				     "%5lu.%02lu E (%lu bytes of log lost: the buffer filled up)\n",
				     (unsigned long)(centis / 100 % 100000), (unsigned long)(centis % 100),
				     (unsigned long)lost);
		} else {
			n = log_ring_take_lines(&ring, (char *)bounce, sizeof(bounce));
			if (!n)
				break;
		}
		u32 written;
		res = write_text((const char *)bounce, n, &written);
		if (res != FR_OK)
			break;
		if (!lost)
			log_ring_drop(&ring, written);
		else if (written)
			ring.lost = 0;
		if (written < n)
			res = (written || !rotated) ? rotate() : FR_INT_ERR;  // a new file takes any n
		rotated = written < n;
	}
	if (res != FR_OK) {
		sd_failed("writing the log", res);
		return;
	}
	res = f_close(&log_file);
	if (res != FR_OK) {
		sd_failed("closing the log", res);
		return;
	}
	force_pending = 0;
}

void tlog_force(u32 level, const char *format, ...)
{
	va_list args;
	u8 kept = tlog_level;
	if (tlog_level < level)
		tlog_level = level;
	va_start(args, format);
	tlog_send(level, format, args);
	va_end(args);
	tlog_level = kept;
	force_pending = 1;
}

// The test kernel (just test-kernel) replaces this to run tests/kernel at boot.
__attribute__((weak)) void torii_run_tests(void)
{
}
