#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <gba_base.h>

#include "ff.h"
#include "ez_define.h"
#include "Ezcard_OP.h"
#include "build_id.h"
#include "logbuf.h"
#include "tlog.h"
#include "engine/cycles.h"
#include "engine/jobs.h"
#include "ui/ui.h"
#include "version.h"

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
_Static_assert(PREFIX_LEN + TEXT_MAX + 1 <= LOG_LINE_MAX, "log_end_is_junk takes longer lines for junk");

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

#define MARKER "Torii kernel boot: %s, logging %s, build main %08lX end %08lX id %08lX"
// Right after it: the version (version.h) with the build ID
#define VERSION_LINE "Torii " TORII_VERSION ", build id %08lX"

void tlog_marker(const char *why, int force)
{
	// tools/logs.py finds sessions by this line and checks the build with it,
	// so it goes to the card at any level but off.
	u8 level = tlog_level;
	if (force) {
		tlog_force(TLOG_INFO, MARKER, why, tlog_level_name(level), (unsigned long)main,
			   (unsigned long)&__rom_end__, (unsigned long)torii_build_id);
		tlog_force(TLOG_INFO, VERSION_LINE, (unsigned long)torii_build_id);
		return;
	}
	if (tlog_level != TLOG_OFF)
		tlog_level = TLOG_INFO;
	tlog(MARKER, why, tlog_level_name(level), (unsigned long)main, (unsigned long)&__rom_end__,
	     (unsigned long)torii_build_id);
	tlog(VERSION_LINE, (unsigned long)torii_build_id);
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

int tlog_in_emulator_now(void)
{
	// mGBA answers 0x1DEA only while enabled: off, it reads 0; on again, the
	// answer comes back. A cart's open bus (the prefetched code) can't follow
	// both writes
	REG_DEBUG_ENABLE = 0;
	int off = REG_DEBUG_ENABLE != 0x1DEA;
	REG_DEBUG_ENABLE = 0xC0DE;
	return tlog_emu && off && REG_DEBUG_ENABLE == 0x1DEA;
}

int tlog_wants(u32 level)
{
	return tlog_emu || level <= tlog_level;
}

// card: 0 for a line only mGBA's console takes (tlog_console)
static void tlog_send(u32 level, int card, const char *format, va_list args)
{
	int to_card = card && level <= tlog_level;
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
	tlog_send(TLOG_INFO, 1, format, args);
	va_end(args);
}

void tlog_error(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	tlog_send(TLOG_ERRORS, 1, format, args);
	va_end(args);
}

void tlog_debug(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	tlog_send(TLOG_DEBUG, 1, format, args);
	va_end(args);
}

void tlog_console(const char *format, ...)
{
	if (!tlog_emu)
		return;
	va_list args;
	va_start(args, format);
	tlog_send(TLOG_INFO, 0, format, args);
	va_end(args);
}

u32 tlog_pending(void)
{
	return ring.head - ring.tail;
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
// The card had no room for the log at the last flush (FR_DENIED, the card's
// or a folder's room): not a failure. The lists' idle flush waits until a
// file is deleted (tlog_card_freed); any other flush tries again.
static u8 no_room;
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

int tlog_card_failed(void)
{
	return sd_state == SD_FAILED || no_room;
}

void tlog_card_freed(void)
{
	no_room = 0;
}

static void log_path(char *path, u32 slot)
{
	snprintf(path, 32, "/torii/logs/torii-%lu.log", (unsigned long)(slot % LOG_FILES));
}

static void sd_failed(const char *what, FRESULT res)
{
	f_close(&log_file);
	if (res == FR_DENIED) {
		// The card is full: the text waits in the ring for room. Said
		// once, and not to the card
		if (!no_room) {
			u8 level = tlog_level;
			tlog_level = TLOG_OFF;
			tlog_error("log: %s: the card is full; the log waits for room", what);
			tlog_level = level;
		}
		no_room = 1;
		return;
	}
	sd_state = SD_FAILED;
	// Not to the card: it just failed. Logging stops until the next boot.
	u8 level = tlog_level;
	tlog_level = TLOG_OFF;
	tlog_error("log: %s failed (FatFs %d); no more SD logging until the next boot", what, res);
	tlog_level = level;
}

// Free space a new log file must leave on the card, for saves and states.
#define LOG_CARD_RESERVE 0x100000u

// Opening the log: finding the current file and where its text ends, or
// making a new one (full size and zero, then its header). sd_open does it
// at once; tlog_flush_soon's job a sector at a time, so turning LOGGING on
// in SETTINGS doesn't stop the menu for a second or more. The steps:
// open_look makes the folders and reads the files' headers, an item a step;
// open_pick then either opens the current file,
// which scan_step reads a sector at a time and open_finish closes, or asks
// for a new one, as open_finish may too (junk); start_begin, start_fill (a
// sector of zeros) and start_end make it. A file made only in part, by a
// rotation cut off by power loss or an open given up, has no header and
// counts as unused.
enum {
	OPEN_LOOK,
	OPEN_PICK,
	OPEN_SCAN,
	OPEN_BEGIN,
	OPEN_FILL,
	OPEN_END,
};
static struct {
	log_end end;  // what scan_step found so far
	u32 seq;  // the new file's
	u32 offset;  // start_fill's
	u32 gen;  // the newest job's ctx: an older one stops
	u32 seqs[LOG_FILES];  // the files' sequence numbers, from open_look
	u32 stepped;  // torii_frames at open_job's last step
	u32 steps;  // open_job's so far
	u8 slot;  // the new file's
	u8 state;  // the next step
	u8 item;  // open_look's next
	u8 fresh;  // a new file is wanted
	u8 on;  // under way: it holds log_file and bounce, and flushes wait
	u8 job;  // tlog_flush_soon's
} opening EWRAM_BSS;

static void open_new(u32 slot, u32 seq)
{
	opening.fresh = 1;
	opening.slot = (u8)slot;
	opening.seq = seq;
}

// Opens log file slot to make it full size and zero, and fills bounce with
// zeros for start_fill: so call it with nothing waiting there.
static FRESULT start_begin(u32 slot)
{
	char path[32];
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
		// Its clusters, all at once, and its size and first cluster on the
		// card now (f_sync), with a zero first sector: the clusters may hold
		// an old log's header. Filling it takes seconds in the background
		// (tlog_flush_soon), and other code's FatFs calls write the FAT out
		// meanwhile: cut off by a reset or power loss, a file grown as it was
		// filled left its clusters lost to the card. Now it is a full-size
		// file without a header, which counts as unused and is made again.
		u32 made = f_size(&log_file) == 0;
		UINT done;
		memset(bounce, 0, sizeof(bounce));
		if (res == FR_OK)
			res = f_lseek(&log_file, LOG_FILE_SIZE);
		if (res == FR_OK && f_tell(&log_file) != LOG_FILE_SIZE)
			res = FR_DENIED;  // the card filled up
		if (res == FR_OK)
			res = f_lseek(&log_file, 0);
		if (res == FR_OK)
			res = f_write(&log_file, bounce, sizeof(bounce), &done);
		if (res == FR_OK)
			res = f_sync(&log_file);
		if (res == FR_OK)
			res = f_lseek(&log_file, 0);
		if (res != FR_OK) {
			f_close(&log_file);
			if (made)
				f_unlink(path);
			return res;
		}
	}
	memset(bounce, 0, sizeof(bounce));
	return FR_OK;
}

// Writes the next sector of zeros to log file slot
static FRESULT start_fill(u32 slot)
{
	char path[32];
	UINT done;
	FRESULT res = f_write(&log_file, bounce, sizeof(bounce), &done);
	if (res == FR_OK && done != sizeof(bounce))
		res = FR_DENIED;  // the card is full
	if (res != FR_OK) {
		// Give the space back: the card's other files need it more.
		f_close(&log_file);
		log_path(path, slot);
		f_unlink(path);
	}
	return res;
}

// Writes the zeroed file's header, closes it, and makes it the current one
static FRESULT start_end(u32 slot, u32 seq)
{
	char header[LOG_HEADER_LEN];
	UINT done;
	FRESULT res = FR_OK;

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
	cur_slot = (u8)slot;
	cur_seq = seq;
	cur_end = LOG_HEADER_LEN;
	return FR_OK;
}

// Makes log file slot full size and zero, then writes its header, at once.
// It fills bounce with zeros (see start_begin).
static FRESULT start_file(u32 slot, u32 seq)
{
	FRESULT res = start_begin(slot);
	for (u32 offset = 0; res == FR_OK && offset < LOG_FILE_SIZE; offset += sizeof(bounce))
		res = start_fill(slot);
	return res == FR_OK ? start_end(slot, seq) : res;
}

// Makes the folders, then reads the files' headers, an item a call: 1 once
// all are done
static int open_look(void)
{
	char path[32];
	char buf[LOG_HEADER_LEN];
	UINT done;
	u32 item = opening.item++;

	if (item == 0) {
		f_mkdir("/torii");
	} else if (item == 1) {
		f_mkdir("/torii/logs");
	} else {
		u32 i = item - 2;
		opening.seqs[i] = 0;
		log_path(path, i);
		if (f_open(&log_file, path, FA_READ) == FR_OK) {
			if (f_size(&log_file) == LOG_FILE_SIZE && f_read(&log_file, buf, sizeof(buf), &done) == FR_OK)
				opening.seqs[i] = log_header_seq(buf, done);
			f_close(&log_file);
		}
	}
	return opening.item >= 2 + LOG_FILES;
}

// From the headers open_look read: opens the current file for scan_step, or
// asks for a new one (open_new).
static FRESULT open_pick(void)
{
	char path[32];

	opening.fresh = 0;
	int cur = log_pick_current(opening.seqs);
	if (cur < 0) {
		open_new(0, 1);
		return FR_OK;
	}
	log_path(path, cur);
	FRESULT res = f_open(&log_file, path, FA_READ | FA_WRITE);
	if (res != FR_OK)
		return res;
	cur_slot = (u8)cur;
	cur_seq = opening.seqs[cur];
	log_end_start(&opening.end);
	return FR_OK;
}

// Reads the next sector of the current file; 1 once its end is found (or
// the read failed: *res)
static int scan_step(FRESULT *res)
{
	UINT done;
	*res = f_read(&log_file, bounce, sizeof(bounce), &done);
	if (*res != FR_OK) {
		f_close(&log_file);
		return 1;
	}
	log_end_feed(&opening.end, (const char *)bounce, done);
	return !done || opening.end.junk || opening.end.seen >= LOG_FILE_SIZE;
}

// Once scan_step is done: sets where the current file's text ends, keeps the
// last line the previous session wrote, and closes the file; or, if its end
// can't be found, asks for the next file.
static FRESULT open_finish(void)
{
	UINT done = 0;
	FRESULT res;

	if (log_end_is_junk(&opening.end)) {
		// Keep the file as it is, and go on in the next one.
		f_close(&log_file);
		tlog_error("log: torii-%lu.log has junk after its text; going on in the next file",
			   (unsigned long)cur_slot);
		open_new((cur_slot + 1) % LOG_FILES, log_next_seq(cur_seq));
		return FR_OK;
	}
	cur_end = log_end_offset(&opening.end);
	if (opening.end.line < cur_end) {
		// Power loss cut off a flush inside a line: zero what it wrote of
		// it, so the next line starts on its own.
		u32 cut = cur_end - opening.end.line;
		memset(bounce, 0, cut);
		res = f_lseek(&log_file, opening.end.line);
		if (res == FR_OK)
			res = f_write(&log_file, bounce, cut, &done);
		if (res == FR_OK && done != cut)
			res = FR_DENIED;
		if (res != FR_OK) {
			f_close(&log_file);
			return res;
		}
		cur_end = opening.end.line;
		tlog("log: dropped %lu bytes of a line cut off by power loss", (unsigned long)cut);
	}

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

// One step of opening the log (see above), from opening.state. Returns 1
// while steps remain; once done, sd_state says how it went.
static int open_step(void)
{
	FRESULT res = FR_OK;
	switch (opening.state) {
	case OPEN_LOOK:
		if (open_look())
			opening.state = OPEN_PICK;
		break;
	case OPEN_PICK:
		res = open_pick();
		opening.state = opening.fresh ? OPEN_BEGIN : OPEN_SCAN;
		break;
	case OPEN_SCAN:
		if (scan_step(&res) && res == FR_OK) {
			res = open_finish();
			opening.state = opening.fresh ? OPEN_BEGIN : OPEN_END;
		}
		break;
	case OPEN_BEGIN:
		res = start_begin(opening.slot);
		opening.offset = 0;
		opening.state = OPEN_FILL;
		break;
	case OPEN_FILL:
		res = start_fill(opening.slot);
		opening.offset += sizeof(bounce);
		if (opening.offset >= LOG_FILE_SIZE)
			opening.state = OPEN_END;
		break;
	default:  // OPEN_END
		if (opening.fresh)
			res = start_end(opening.slot, opening.seq);
		if (res != FR_OK)
			break;
		opening.on = 0;
		sd_state = SD_READY;
		no_room = 0;
		return 0;
	}
	if (res != FR_OK) {
		opening.on = 0;
		sd_failed("opening the log", res);  // full: looked for again next flush
		return 0;
	}
	return 1;
}

// Starts opening the log from its first step
static void open_start(void)
{
	opening.state = OPEN_LOOK;
	opening.item = 0;
	opening.on = 1;
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
	FRESULT res = start_file((cur_slot + 1) % LOG_FILES, log_next_seq(cur_seq));
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

// Opens the log if it isn't yet: at once, or finishes tlog_flush_soon's
// open at once. 1 if it is open.
static int sd_open(void)
{
	if (sd_state == SD_MOUNTED && !opening.on)
		open_start();
	while (opening.on && open_step())
		;
	return sd_state == SD_READY;
}

#define OPEN_WAIT_MAX 30

// Stops the open (a cancel, LOGGING turned off): the file closes, and what
// it made of a new one counts as unused (start_begin)
static void open_stop(void)
{
	f_close(&log_file);
	opening.on = 0;
}

u32 tlog_open_steps_test EWRAM_BSS;

// tlog_flush_soon's job: a step of the open a frame, until it is done
static int open_job(struct job *job)
{
	// A cancel gives the open up: tlog_flush starts it again
	if ((u32)job->ctx != opening.gen || !opening.on || job->state == JOB_CANCEL) {
		if (job->state == JOB_CANCEL && (u32)job->ctx == opening.gen && opening.on)
			open_stop();
		return JOB_DONE;
	}
	// LOGGING turned off meanwhile: the card is left alone from now on
	if (tlog_level == TLOG_OFF && !force_pending) {
		open_stop();
		tlog_debug("log: LOGGING is off: the open stops");
		return JOB_DONE;
	}
	// A step's SD writes take about a frame: the screen's transitions and
	// drawing go first, for up to half a second at a time
	if (!ui_settled() && torii_frames - opening.stepped < OPEN_WAIT_MAX)
		return JOB_WAIT;
	if (tlog_open_steps_test && opening.steps + 1 >= tlog_open_steps_test)
		return JOB_WAIT;
	opening.stepped = torii_frames;
	opening.steps++;
	if (open_step())
		return JOB_MORE;
	if (sd_state == SD_READY)  // sd_failed logged a failure
		tlog_debug("log: opened in the background");
	return JOB_DONE;
}

void tlog_flush_soon(void)
{
	if (!opening.on && sd_state == SD_MOUNTED && tlog_level != TLOG_OFF && ui_active()) {
		opening.gen++;
		opening.job = job_start(open_job, (void *)opening.gen, JOB_OWNER_NONE);
		if (opening.job != JOB_NONE) {
			open_start();
			// The first step waits for the screen too
			opening.stepped = torii_frames;
			opening.steps = 0;
			return;
		}
	}
	tlog_flush();
}

void tlog_open_now(void)
{
	if (!opening.on)
		return;
	if (tlog_level == TLOG_OFF && !force_pending) {
		open_stop();
		return;
	}
	sd_open();
	tlog_debug("log: opened at once");
}

void tlog_sd_ready(void (*between)(void))
{
	if (sd_state == SD_NONE)
		sd_state = SD_MOUNTED;
	// Look for the current file now, so the boot report can see how the
	// previous session ended. With logging off the card stays untouched.
	if (tlog_level == TLOG_OFF || sd_state != SD_MOUNTED || opening.on)
		return;
	open_start();
	while (opening.on && open_step())
		if (between)
			between();
}

void tlog_flush(void)
{
	char path[32];
	u32 n;
	FRESULT res;

	if (tlog_level == TLOG_OFF && !force_pending)
		return;
	cycles_summary();
	if (ring.head == ring.tail && !ring.lost)
		return;
	// While tlog_flush_soon's job opens the log, the text waits for it, as
	// long as the engine session it runs in goes on; otherwise the rest of
	// the open is done now (sd_open), as before a launch.
	if (opening.on && job_running(opening.job) && ui_active())
		return;
	if (!sd_open())
		return;
	log_path(path, cur_slot);
	res = f_open(&log_file, path, FA_WRITE | FA_OPEN_EXISTING);
	if (res == FR_NO_FILE || res == FR_NO_PATH) {
		// The file or its folder was deleted meanwhile (the SD page): the
		// files are looked for again, a new one made if need be
		sd_state = SD_MOUNTED;
		tlog("log: %s was deleted: opening the log again", path);
		if (ui_active()) {
			// A new file is made a sector a frame; the text waits in the ring
			tlog_flush_soon();
			return;
		}
		if (!sd_open())
			return;
		log_path(path, cur_slot);
		res = f_open(&log_file, path, FA_WRITE | FA_OPEN_EXISTING);
	}
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
	no_room = 0;
	force_pending = 0;
}

void tlog_force(u32 level, const char *format, ...)
{
	va_list args;
	u8 kept = tlog_level;
	if (tlog_level < level)
		tlog_level = level;
	va_start(args, format);
	tlog_send(level, 1, format, args);
	va_end(args);
	tlog_level = kept;
	force_pending = 1;
}

// The test kernel (just test-kernel) replaces this to run tests/kernel at boot.
__attribute__((weak)) void torii_run_tests(void)
{
}
