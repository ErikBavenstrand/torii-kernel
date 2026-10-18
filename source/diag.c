// Diagnostics (see diag.h). Every result goes to the log as a "diag:" line;
// the screen shows the same lines in the menu's font, a page at a time. The
// steps that need the user come last: rumble, the checklist from
// docs/release-checklist.md, checked against the log where it can be, and a
// crash test.
//
// It never writes to the game saves: it reads the FRAM only, writes test
// patterns to a free SRAM page (CARTLOG_TEST_PAGE), and checks the FRAM's
// checksum is the same afterwards.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <gba_base.h>
#include <gba_compression.h>
#include <gba_dma.h>
#include <gba_input.h>
#include <gba_interrupt.h>
#include <gba_systemcalls.h>
#include <gba_timers.h>
#include <gba_video.h>

#include "ff.h"
#include "ez_define.h"
#include "ezkernel.h"
#include "Ezcard_OP.h"
#include "bootloader.h"
#include "build_id.h"
#include "RTC.h"
#include "cartlog.h"
#include "diag.h"
#include "draw.h"
#include "logbuf.h"
#include "logcheck.h"
#include "lz77.h"
#include "tlog.h"
#include "waitcnt.h"
#include "../tests/kernel/torii_test.h"

#define REG_WAITCNT_IO (*(vu16 *)0x04000204)

extern const u8 __rom_end__;
extern void SoftReset_now(void);
extern u32 gl_currentpage;

#define WHITE RGB(31, 31, 31)
#define GREY RGB(20, 20, 20)
#define GREEN RGB(8, 31, 8)
#define RED RGB(31, 8, 8)
#define YELLOW RGB(31, 31, 8)

// --- Screen and keys ---------------------------------------------------------

static u32 diag_y;
static u32 diag_step_no;
static char diag_text[64];

static void screen_title(const char *title)
{
	Clear(0, 0, 240, 160, RGB(0, 0, 0), 1);
	snprintf(diag_text, sizeof(diag_text), "DIAGNOSTICS: %s", title);
	DrawHZText12(diag_text, 39, 2, 2, YELLOW, 1);
	diag_y = 18;
}

// Shows a line (cut to the screen's 39 characters) and logs it whole.
static void __attribute__((format(printf, 2, 3))) say(u16 color, const char *format, ...)
{
	char text[160];
	va_list args;
	va_start(args, format);
	vsnprintf(text, sizeof(text), format, args);
	va_end(args);
	if (color == RED)
		tlog_error("diag: %s", text);
	else
		tlog("diag: %s", text);
	if (diag_y > 160 - 13) {
		Clear(0, 18, 240, 142, RGB(0, 0, 0), 1);
		diag_y = 18;
	}
	DrawHZText12(text, 39, 2, diag_y, color, 1);
	diag_y += 12;
}

static void step(const char *name)
{
	diag_step_no++;
	cartlog_phase(PHASE_DIAG, diag_step_no, NULL);
	tlog("diag: step %lu: %s", (unsigned long)diag_step_no, name);
	tlog_flush();
	screen_title(name);
}

static void wait_release(void)
{
	do {
		VBlankIntrWait();
		scanKeys();
	} while (keysHeld());
}

// Called every frame while wait_key waits, if set: the live clock.
static void (*wait_tick)(void);

static u16 wait_key(u16 mask)
{
	wait_release();
	for (;;) {
		VBlankIntrWait();
		if (wait_tick)
			wait_tick();
		scanKeys();
		u16 keys = keysDown() & mask;
		if (keys)
			return keys;
	}
}

static void pause_frames(u32 frames)
{
	while (frames--)
		VBlankIntrWait();
}

enum {
	ANSWER_SKIP = -1,
	ANSWER_NO = 0,
	ANSWER_YES = 1
};

enum {
	ASK_CHECK,  // no is a failure, logged as an error
	ASK_PROBE,  // no is a finding, logged as info
	ASK_TRIED,  // a check of something the user may not have tried
};

// Asks a yes/no question about what the user saw: A yes, B no, SELECT skip.
static int ask(const char *topic, const char *question, int kind)
{
	say(WHITE, "%s", question);
	say(GREY, kind == ASK_TRIED ? "A: yes   B: no   SELECT: didn't try" : "A: yes   B: no   SELECT: skip");
	u16 key = wait_key(KEY_A | KEY_B | KEY_SELECT);
	int answer = (key & KEY_A) ? ANSWER_YES : (key & KEY_B) ? ANSWER_NO : ANSWER_SKIP;
	u16 colour = GREY;
	const char *said = kind == ASK_TRIED ? "not tried" : "skipped";
	if (answer == ANSWER_YES) {
		colour = GREEN;
		said = "yes";
	} else if (answer == ANSWER_NO) {
		colour = kind == ASK_PROBE ? WHITE : RED;
		said = "no";
	}
	say(colour, "%s: %s", topic, said);
	tlog_flush();
	return answer;
}

// --- Timing ------------------------------------------------------------------

// Timers 2 and 3 cascaded: CPU cycles (16.78 MHz) in 32 bits.
void IWRAM_CODE diag_timer_start(void)
{
	REG_TM2CNT_H = 0;
	REG_TM3CNT_H = 0;
	REG_TM2CNT_L = 0;
	REG_TM3CNT_L = 0;
	REG_TM3CNT_H = TIMER_START | TIMER_COUNT;
	REG_TM2CNT_H = TIMER_START;
}

u32 IWRAM_CODE diag_timer_stop(void)
{
	REG_TM2CNT_H = 0;
	u32 cycles = REG_TM2CNT_L | (u32)REG_TM3CNT_L << 16;
	REG_TM3CNT_H = 0;
	return cycles;
}

// KB per second from bytes moved in cycles.
static u32 kb_per_s(u32 bytes, u32 cycles)
{
	if (!cycles)
		return 0;
	return (u32)((unsigned long long)bytes * 16777216ULL / cycles / 1024);
}

// Sums words of ROM-space memory with WAITCNT set to waitcnt, from IWRAM, so
// the kernel's own code isn't read at the wait states under test. Returns
// the cycles taken.
u32 IWRAM_CODE diag_read_timed(const vu32 *src, u32 words, u16 waitcnt, u32 *sum)
{
	u16 ime = REG_IME;
	u16 old = REG_WAITCNT_IO;
	u32 s = 0;
	REG_IME = 0;
	REG_WAITCNT_IO = waitcnt;
	diag_timer_start();
	for (u32 i = 0; i < words; i++)
		s += src[i];
	u32 cycles = diag_timer_stop();
	REG_WAITCNT_IO = old;
	REG_IME = ime;
	*sum = s;
	return cycles;
}

// Copies halfwords of EZ-Flash's boot loader from OFFSET into DST, reading
// only: ROM page 0x8000 shows the loader where the kernel's code was, so this
// runs from IWRAM with interrupts off, copies with a plain loop (no ROM
// helper), and puts the kernel's page back before anything else runs.
void IWRAM_CODE diag_loader_read(u32 offset, u16 *dst, u32 halfwords)
{
	u16 ime = REG_IME;
	REG_IME = 0;
	u16 waitcnt = WAITCNT_SLOW();
	SetRompage(ROM_PAGE_LOADER);
	const vu16 *src = (const vu16 *)(0x08000000 + offset);
	for (u32 i = 0; i < halfwords; i++)
		dst[i] = src[i];
	SetRompage(gl_currentpage ? gl_currentpage : 0x8002);
	WAITCNT_RESTORE(waitcnt);
	REG_IME = ime;
}

// --- Steps -------------------------------------------------------------------

// The summary's lines: each failed check, then each yes the log can't back.
static const char *summary[24];
static u32 summary_count;
static const char *unconfirmed[16];
static u32 unconfirmed_count;

static void add_failure(const char *what)
{
	if (summary_count < sizeof(summary) / sizeof(*summary))
		summary[summary_count++] = what;
}

// Shows a check's result; failures go in the summary. what is a literal.
static void result(int ok, const char *what)
{
	say(ok ? GREEN : RED, "%s: %s", what, ok ? "pass" : "FAIL");
	if (!ok)
		add_failure(what);
}

static void step_info(void)
{
	step("the cart");
	u16 fpga = Read_FPGA_ver();
	say(WHITE, "FPGA firmware %u (%s)", fpga & 0xFF,
	    ((fpga & 0xF000) == 0xA000 || (fpga & 0xF000) == 0xB000) ? "LX16, Rev.B" : "LX9, Rev.A");
	tlog("diag: FPGA version register %04X", fpga);
	say(WHITE, "kernel NOR id %04X, game NOR id %04X", Read_S71NOR_ID(), Read_S98NOR_ID());
	say(WHITE, "WAITCNT %04X in the menu (games start at 0000)", REG_WAITCNT_IO);
	say(WHITE, "kernel ROM %08lX-%08lX", 0x08000000ul, (unsigned long)&__rom_end__);
	u32 id = build_id_compute();
	say(id == torii_build_id ? WHITE : RED, "build id %08lX%s", (unsigned long)torii_build_id,
	    id == torii_build_id ? ", matches the ROM" : ", NOT the ROM's");
	for (u32 slot = 0; slot < 32; slot += 8) {
		u16 v[8];
		for (u32 i = 0; i < 8; i++)
			v[i] = Read_SET_info(slot + i);
		tlog_debug("diag: settings %2lu-%2lu: %04X %04X %04X %04X %04X %04X %04X %04X", (unsigned long)slot,
			   (unsigned long)slot + 7, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
	}
	pause_frames(60);
}

static void step_unit_tests(void)
{
	step("unit tests");
	say(WHITE, "running the kernel's unit tests...");
	int failed = torii_unit_tests();
	say(failed ? RED : GREEN, "%d unit tests failed (see the log)", failed);
	result(!failed, "unit tests");
	pause_frames(60);
}

static int bcd_ok(u8 v, u8 max)
{
	return (v & 0xF) <= 9 && (v >> 4) <= 9 && UNBCD(v) <= max;
}

static void step_rtc(void)
{
	step("clock");
	u8 a[7], b[7];
	rtc_enable();
	rtc_get(a);
	rtc_disenable();
	pause_frames(130);  // a bit over two seconds
	rtc_enable();
	rtc_get(b);
	rtc_disenable();
	say(WHITE, "20%02X-%02X-%02X %02X:%02X:%02X", a[0], a[1], a[2], a[4] & 0x3F, a[5], a[6]);
	say(WHITE, "then %02X:%02X:%02X", b[4] & 0x3F, b[5], b[6]);
	tlog_debug("diag: clock raw %02X %02X %02X %02X %02X %02X %02X", a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
	int valid = bcd_ok(a[0], 99) && bcd_ok(a[1], 12) && a[1] && bcd_ok(a[2], 31) && a[2] &&
		    bcd_ok(a[4] & 0x3F, 23) && bcd_ok(a[5], 59) && bcd_ok(a[6], 59);
	u32 s1 = UNBCD(a[6]) + 60 * UNBCD(a[5]), s2 = UNBCD(b[6]) + 60 * UNBCD(b[5]);
	u32 passed = (s2 + 3600 - s1) % 3600;
	say(WHITE, "%lu seconds passed in about 2.2", (unsigned long)passed);
	result(valid, "clock reads a valid date");
	if (passed == 0 && tlog_in_emulator())
		say(GREY, "clock runs: the emulator can't tell");
	else
		result(passed >= 1 && passed <= 4, "clock runs");
	pause_frames(60);
}

// The last 64 KB of PSRAM, a part only a 32 MB game uses: the kernel fills
// PSRAM again at every launch.
#define PSRAM_TEST_PAGE 0x3000
#define PSRAM_TEST_OFFSET 0x7F0000
static void step_psram(void)
{
	step("PSRAM");
	vu16 *p = (vu16 *)(PSRAMBase_S98 + PSRAM_TEST_OFFSET);
	u32 errors = 0, first = 0;
	SetPSRampage(PSRAM_TEST_PAGE);
	for (u32 pass = 0; pass < 2; pass++) {
		u16 flip = pass ? 0xFFFF : 0;
		diag_timer_start();
		for (u32 i = 0; i < 0x8000; i++)
			p[i] = (u16)(i * 0x9E37 + 0x1234) ^ flip;
		u32 write_cycles = diag_timer_stop();
		diag_timer_start();
		for (u32 i = 0; i < 0x8000; i++) {
			if (p[i] != ((u16)(i * 0x9E37 + 0x1234) ^ flip)) {
				if (!errors)
					first = i;
				errors++;
			}
		}
		u32 read_cycles = diag_timer_stop();
		if (!pass)
			say(WHITE, "64 KB: write %lu KB/s, check %lu KB/s",
			    (unsigned long)kb_per_s(0x10000, write_cycles),
			    (unsigned long)kb_per_s(0x10000, read_cycles));
	}
	SetPSRampage(0);
	if (errors)
		say(RED, "%lu halfwords wrong, the first at +%lX", (unsigned long)errors, (unsigned long)first * 2);
	result(!errors, "PSRAM holds a pattern");
	pause_frames(60);
}

static void step_nor(void)
{
	step("NOR reads");
	u32 size = (u32)&__rom_end__ - 0x08000000;
	u32 a = crc32((u8 *)0x08000000, size), b = crc32((u8 *)0x08000000, size);
	say(WHITE, "kernel CRC %08lX twice: %s", (unsigned long)a, a == b ? "same" : "DIFFERENT");
	result(a == b, "kernel NOR reads the same twice");
	// The FPGA images in the upgrade file have known CRCs (ez_define.h).
	u32 lx9 = crc32((u8 *)LX9_newomega_top_bin_address, LX9_newomega_top_bin_size);
	u32 lx16 = crc32((u8 *)LX16_newomega_top_bin_address, LX16_newomega_top_bin_size);
	say(WHITE, "FPGA image CRCs %08lX %08lX", (unsigned long)lx9, (unsigned long)lx16);
	result(lx9 == LX9_FW_crc32 && lx16 == LX16_FW_crc32, "800 KB of NOR read correctly");
	pause_frames(60);
}

// Reads EZ-Flash's boot loader (never writes it) and logs its CRC and
// whether it looks whole. A blank or damaged loader means the cart starts in
// safe mode at the next power-on: docs/recovery.md.
static void step_loader(void)
{
	step("boot loader");
	u16 *image = (u16 *)pReadCache;
	for (u32 at = 0; at < LOADER_SIZE; at += 0x1000)
		diag_loader_read(at, image + at / 2, 0x1000 / 2);
	enum loader_state state = loader_check((const u8 *)image);
	u32 crc = crc32(image, LOADER_SIZE);
	const char *title = (const char *)image + 0xA0;
	int whole = state == LOADER_OK;
	say(whole ? WHITE : RED, "boot loader CRC %08lX, %s", (unsigned long)crc, loader_state_name(state));
	if (whole) {
		int de_bin = crc32(image, DE_BIN_SIZE) == DE_BIN_CRC32;
		tlog("diag: boot loader title %.12s, code %.4s, %s", title, title + 12,
		     de_bin ? "EZ-Flash's DE.BIN" : "not DE.BIN (another loader version?)");
	} else {
		tlog_error("diag: the boot loader is %s: the cart will start in EZ-Flash's safe mode at the next "
			   "power-on (docs/recovery.md)",
			   loader_state_name(state));
		say(RED, "DON'T POWER OFF before reading");
		say(RED, "docs/recovery.md on the Torii site");
	}
	// Whether EZ-Flash's loader has a GBA header isn't known yet: only a blank
	// or erased one fails
	result(whole, "boot loader present");
	pause_frames(whole ? 60 : 600);
}

static u32 fram_crc(void)
{
	u32 crc;
	SetRampage(0x00);
	ReadSram(SRAMSaver, pReadCache, 0x10000);
	SetRampage(0x10);
	ReadSram(SRAMSaver, pReadCache + 0x10000, 0x10000);
	SetRampage(0x00);
	crc = crc32(pReadCache, 0x20000);
	return crc;
}

static void step_save_chip(void)
{
	step("save chip and cart RAM");
	say(WHITE, "reading the 128 KB save chip twice...");
	u32 before = fram_crc();
	u32 again = fram_crc();
	say(WHITE, "save chip CRC %08lX, then %08lX", (unsigned long)before, (unsigned long)again);
	result(before == again, "save chip reads the same twice");

	// Patterns in a free 64 KB page of cart SRAM, never the save chip.
	u32 errors = 0;
	u8 *buf = pReadCache, *back = pReadCache + 0x10000;
	for (u32 pass = 0; pass < 2; pass++) {
		for (u32 i = 0; i < 0x10000; i++)
			buf[i] = (u8)(i * 37 + (i >> 8)) ^ (pass ? 0xFF : 0);
		SetRampage(CARTLOG_TEST_PAGE);
		WriteSram(SRAMSaver, buf, 0x10000);
		ReadSram(SRAMSaver, back, 0x10000);
		SetRampage(0);
		for (u32 i = 0; i < 0x10000; i++)
			errors += back[i] != buf[i];
	}
	say(errors ? RED : WHITE, "SRAM page %02X: %lu bytes wrong", CARTLOG_TEST_PAGE, (unsigned long)errors);
	result(!errors, "cart SRAM holds a pattern");

	// Two pages hold different bytes: paging works.
	u8 x = 0x5A, y = 0xA5, rx = 0, ry = 0;
	SetRampage(CARTLOG_TEST_PAGE);
	WriteSram(SRAMSaver, &x, 1);
	SetRampage(CARTLOG_TEST_PAGE + 0x10);
	WriteSram(SRAMSaver, &y, 1);
	SetRampage(CARTLOG_TEST_PAGE);
	ReadSram(SRAMSaver, &rx, 1);
	SetRampage(CARTLOG_TEST_PAGE + 0x10);
	ReadSram(SRAMSaver, &ry, 1);
	SetRampage(0);
	result(rx == x && ry == y, "cart SRAM pages are separate");

	u32 after = fram_crc();
	say(after == before ? WHITE : RED, "save chip CRC after: %08lX", (unsigned long)after);
	result(after == before, "save chip untouched by the tests");
	pause_frames(60);
}

// WS0 settings to try: first access 4, 3 or 2 cycles, then 2 or 1, with the
// prefetch buffer on, and the BIOS default.
static const u16 waitcnts[] = {0x0000, 0x4000, 0x4010, 0x4004, 0x4014, 0x4008, 0x4018};
static const char *const waitcnt_names[] = {"4/2", "4/2+pf", "4/1+pf", "3/2+pf", "3/1+pf", "2/2+pf", "2/1+pf"};

static void timed_reads(const char *what, const vu32 *src)
{
	u32 sum0 = 0;
	for (u32 i = 0; i < sizeof(waitcnts) / sizeof(*waitcnts); i++) {
		u32 sum;
		u32 cycles = diag_read_timed(src, 0x4000, waitcnts[i], &sum);
		if (i == 0)
			sum0 = sum;
		say(sum == sum0 ? WHITE : RED, "%s %04X %s: %lu KB/s%s", what, waitcnts[i], waitcnt_names[i],
		    (unsigned long)kb_per_s(0x10000, cycles), sum == sum0 ? "" : ", BAD DATA");
	}
}

static void step_timing(void)
{
	step("timings");
	say(WHITE, "64 KB read at each WAITCNT (WS0 N/S):");
	timed_reads("NOR", (const vu32 *)0x08000000);
	SetPSRampage(0);
	timed_reads("PSRAM", (const vu32 *)PSRAMBase_S98);
	tlog_flush();

	// Glyphs: one line of 39 characters, drawn ten times.
	static const char sample[] = "The quick brown fox jumps over the dog.";
	diag_timer_start();
	for (u32 i = 0; i < 10; i++)
		DrawHZText12((char *)sample, 39, 2, 146, GREY, 1);
	u32 glyph_cycles = diag_timer_stop() / (10 * 39);
	Clear(0, 146, 240, 14, RGB(0, 0, 0), 1);
	say(WHITE, "a glyph takes %lu cycles (%lu us)", (unsigned long)glyph_cycles,
	    (unsigned long)(glyph_cycles * 1000000ull / 16777216));

	// BIOS LZ77: 16 KB of the SD list's background picture.
	u8 *packed = pReadCache, *out = pReadCache + 0x8000;
	const u8 *picture = (const u8 *)gImage_SD;
	u32 packed_size = lz77_compress(picture, 0x4000, packed);
	diag_timer_start();
	LZ77UnCompWram(packed, out);
	u32 lz_cycles = diag_timer_stop();
	int lz_ok = memcmp(out, picture, 0x4000) == 0;
	say(lz_ok ? WHITE : RED, "LZ77 to WRAM: %lu KB/s (from %lu B)", (unsigned long)kb_per_s(0x4000, lz_cycles),
	    (unsigned long)packed_size);
	result(lz_ok, "BIOS LZ77 gives back the data");

	// SD reads: 256 KB of raw sectors from the card's start.
	diag_timer_start();
	for (u32 lba = 0; lba < 512; lba += 4)
		Read_SD_sectors(lba, 4, pReadCache);
	u32 sd_cycles = diag_timer_stop();
	say(WHITE, "SD reads: %lu KB/s", (unsigned long)kb_per_s(0x40000, sd_cycles));

	// SD writes: 128 KB rewritten in a file of Diagnostics' own, made once.
	FIL f;
	UINT done;
	int ok = f_open(&f, "/torii/diag.bin", FA_READ | FA_WRITE | FA_OPEN_ALWAYS) == FR_OK;
	memset(pReadCache, 0, 0x800);
	if (ok && f_size(&f) != 0x20000) {
		for (u32 i = 0; ok && i < 0x20000; i += 0x800)
			ok = f_write(&f, pReadCache, 0x800, &done) == FR_OK && done == 0x800;
		ok = ok && f_sync(&f) == FR_OK;
	}
	if (ok) {
		ok = f_lseek(&f, 0) == FR_OK;
		diag_timer_start();
		for (u32 i = 0; ok && i < 0x20000; i += 0x800)
			ok = f_write(&f, pReadCache, 0x800, &done) == FR_OK && done == 0x800;
		ok = ok && f_sync(&f) == FR_OK;
		u32 cycles = diag_timer_stop();
		if (ok)
			say(WHITE, "SD writes: %lu KB/s", (unsigned long)kb_per_s(0x20000, cycles));
	}
	f_close(&f);
	result(ok, "SD write test");
	pause_frames(60);
}

// --- The log on the card ------------------------------------------------------

// What the log shows happened since the last Diagnostics run (logcheck.h).
static u32 log_seen;
static struct logcheck log_check EWRAM_BSS;

// Reads the log files, oldest first, into pReadCache one at a time.
static void read_log(void)
{
	char path[40];
	u32 seqs[LOG_FILES];
	FIL f;
	UINT got;
	memset(&log_check, 0, sizeof(log_check));
	tlog_flush();
	for (u32 i = 0; i < LOG_FILES; i++) {
		seqs[i] = 0;
		snprintf(path, sizeof(path), "/torii/logs/torii-%lu.log", (unsigned long)i);
		if (f_open(&f, path, FA_READ) != FR_OK)
			continue;
		if (f_read(&f, pReadCache, LOG_HEADER_LEN, &got) == FR_OK)
			seqs[i] = log_header_seq((const char *)pReadCache, got);
		f_close(&f);
	}
	for (;;) {
		int next = -1;
		for (u32 i = 0; i < LOG_FILES; i++)
			if (seqs[i] && (next < 0 || seqs[i] < seqs[next]))
				next = (int)i;
		if (next < 0)
			break;
		seqs[next] = 0;
		snprintf(path, sizeof(path), "/torii/logs/torii-%lu.log", (unsigned long)next);
		if (f_open(&f, path, FA_READ) != FR_OK)
			continue;
		if (f_read(&f, pReadCache, LOG_FILE_SIZE, &got) != FR_OK)
			got = 0;
		f_close(&f);
		// Line by line, to the first zero byte: the end of the text.
		pReadCache[got] = 0;  // got <= LOG_FILE_SIZE, half the buffer
		for (char *text = (char *)pReadCache; *text;) {
			char *nl = strchr(text, '\n');
			if (nl)
				*nl = 0;
			logcheck_line(&log_check, text);
			if (!nl)
				break;
			text = nl + 1;
		}
	}
	log_seen = log_check.seen;
	tlog("diag: the log since the last run shows %03lX (see source/logcheck.h)", (unsigned long)log_seen);
}

// --- EEPROM 512 or 8K ----------------------------------------------------------

// The cart's EEPROM answers only in game mode: tests/roms/eeprom asks it.
static void step_eeprom(void)
{
	step("EEPROM 512 or 8K");
	if (log_seen & SEEN_EEPROM_PROBE_FAILED) {
		say(RED, "An EEPROM probe run failed: its");
		say(RED, "verdict and steps are in the log.");
		add_failure("the EEPROM probe");
	} else if (log_seen & SEEN_EEPROM_PROBE) {
		say(GREEN, "EEPROM probe runs since the last run");
		say(GREEN, "read back what they wrote: pass");
	} else {
		say(WHITE, "Not tested since the last run. The");
		say(WHITE, "EEPROM answers only in a game: launch");
		say(WHITE, "eeprom.gba (tests/roms/eeprom) CLEAN");
		say(WHITE, "BOOT, SAVE TYPE EEPROM 512, then 8K,");
		say(WHITE, "with A, then B, each time: four runs.");
		say(WHITE, "Each goes back to the kernel and logs.");
	}
	pause_frames(180);
}

// --- Rumble ------------------------------------------------------------------

// The motor is not on the GPIO port: Set_rumble drives it as Jagoomba does,
// which pulses it, on and off at once, each frame a game asks for rumble.
// Driven that way in kernel mode, it didn't rumble (real cart, 2026-10-18).
static void rumble_pulses(u32 frames)
{
	while (frames--) {
		Set_rumble(1);
		Set_rumble(0);
		VBlankIntrWait();
	}
}

#define ROM_PAGE_PSRAM 0x200  // the page games from PSRAM, Jagoomba among them, run under

// Runs the motor for a second, pulsed or held, with the ROM page at PSRAM as
// under Jagoomba, where 0x08001000 is PSRAM rather than the kernel NOR. The
// kernel's code is gone meanwhile, so this runs from IWRAM with interrupts
// off, waits on VCOUNT, and puts the kernel's page back before returning.
// read gets 0x08001000 before, after the first 2 and after the last 0: what
// changed reached the PSRAM, which the next launch loads afresh anyway.
void IWRAM_CODE diag_rumble_psram(u32 held, u16 *read)
{
	u16 ime = REG_IME;
	REG_IME = 0;
	SetRompage(ROM_PAGE_PSRAM);
	read[0] = *(vu16 *)0x08001000;
	for (u32 f = 0; f < 60; f++) {
		if (f == 0 || !held) {
			Set_rumble(1);
			if (f == 0)
				read[1] = *(vu16 *)0x08001000;
		}
		if (!held)
			Set_rumble(0);
		while (REG_VCOUNT >= 160)
			;
		while (REG_VCOUNT < 160)
			;
	}
	Set_rumble(0);
	read[2] = *(vu16 *)0x08001000;
	SetRompage(gl_currentpage ? gl_currentpage : 0x8002);
	REG_IME = ime;
}

static void rumble_psram(u32 held, const char *topic)
{
	u16 read[3];
	pause_frames(30);
	diag_rumble_psram(held, read);
	tlog("diag: %s: 0x08001000 read %04X, %04X after 2, %04X after 0", topic, read[0], read[1], read[2]);
	ask(topic, "Did it rumble just now?", ASK_PROBE);
}

// One question per way of driving the motor, each a second long, with the
// GPIO port on (0x096A0000 at 1) as Jagoomba has it. Kernel mode first; then
// with the ROM page at PSRAM, pulsed and held. What else a launch sets, the
// save type above all, stays for the rumble probe ROM (tests/roms/rumble),
// which runs in game mode.
static void step_rumble(void)
{
	step("rumble");
	say(WHITE, "Hold the console: the motor runs three");
	say(WHITE, "times, a second each.");
	Set_RTC_status(1);
	pause_frames(30);
	rumble_pulses(60);
	ask("rumble pulsed in kernel mode", "Did it rumble just now?", ASK_PROBE);
	rumble_psram(0, "rumble pulsed, ROM page PSRAM");
	rumble_psram(1, "rumble held, ROM page PSRAM");
	say(WHITE, "For game mode, launch rumble.gba");
	say(WHITE, "(tests/roms/rumble) CLEAN BOOT.");
	pause_frames(120);
}

// --- Checklist ---------------------------------------------------------------

// docs/release-checklist.md, "In Diagnostics": things only a person can see.
// Each question fits the screen; name is a literal for the summary. A yes to
// something the log should show (needs) is "unconfirmed" without it.
struct check {
	const char *question;
	const char *name;
	u32 needs;
	const char *proof;  // what the log lacks, for the unconfirmed line
};
#define CHECK_PREFIX "checklist: "
#define ITEM(question, name, needs, proof) {question, CHECK_PREFIX name, needs, proof}

static const struct check checklist[] = {
	ITEM("The bars are red, green, blue, white.", "colour bars", 0, NULL),
	ITEM("The LED breathes red and blue now.", "LED", 0, NULL),
	ITEM("The clock above shows the right time.", "clock", 0, NULL),
	ITEM("Since the last run, a GBA game started.", "a GBA game started", SEEN_GBA, "GBA launch"),
	ITEM("...its save survived a power cycle.", "a save survived a power cycle", SEEN_GBA, "GBA launch"),
	ITEM("...the in-game menu opened.", "the in-game menu", SEEN_MENU,
	     "BOOT WITH ADDON launch with RESET or SAVESTATE on"),
	ITEM("...a save state saved and loaded.", "save states", SEEN_SAVESTATE, "launch with SAVESTATE on"),
	ITEM("...sleep slept and woke.", "sleep", SEEN_SLEEP, "launch with SLEEP on"),
	ITEM("...a GB or GBC game ran.", "a GB or GBC game", SEEN_GB, "GB or GBC launch"),
	ITEM("...a NES game ran.", "a NES game", SEEN_NES, "NES launch"),
	ITEM("...a game written to NOR started.", "a game from NOR", SEEN_NOR, "launch from NOR"),
	ITEM("...the clock kept time while off.", "the clock while off", 0, NULL),
	ITEM("...DEBUG in a game reached the log.", "DEBUG", SEEN_DEBUG, "debug info"),
};

#define CLOCK_ITEM 2
#define CLOCK_Y 30

static u8 clock_shown;  // the second on screen

static void clock_read(u8 t[7])
{
	rtc_enable();
	rtc_get(t);
	rtc_disenable();
}

static void clock_draw(void)
{
	u8 t[7];
	clock_read(t);
	if (t[6] == clock_shown)
		return;
	clock_shown = t[6];
	char line[24];
	snprintf(line, sizeof(line), "20%02X-%02X-%02X %02X:%02X:%02X", t[0], t[1], t[2], t[4] & 0x3F, t[5], t[6]);
	Clear(0, CLOCK_Y, 240, 12, RGB(0, 0, 0), 1);
	DrawHZText12(line, 39, 2, CLOCK_Y, YELLOW, 1);
}

// The live clock on the clock item: read four times a second, drawn when
// the second changes.
static void clock_tick(void)
{
	if (torii_frames % 15 == 0)
		clock_draw();
}

static void show_colour_bars(void)
{
	static const u16 colours[] = {RGB(31, 0, 0), RGB(0, 31, 0), RGB(0, 0, 31), RGB(31, 31, 31)};
	for (u32 i = 0; i < 4; i++)
		Clear(2 + i * 59, 120, 56, 24, colours[i], 1);
}

static void step_keys(void)
{
	step("buttons");
	say(WHITE, "Press every button once. Hold START and");
	say(WHITE, "SELECT for two seconds to give up.");
	u16 seen = 0;
	u32 held = 0;
	wait_release();
	while (seen != 0x3FF && held < 120) {
		VBlankIntrWait();
		scanKeys();
		seen |= keysDown();
		held = ((keysHeld() & (KEY_START | KEY_SELECT)) == (KEY_START | KEY_SELECT)) ? held + 1 : 0;
		static const char *const names[] = {"A", "B", "SEL", "START", "RIGHT", "LEFT", "UP", "DOWN", "R", "L"};
		if (torii_frames % 8 == 0) {
			char line[64];
			u32 len = 0;
			line[0] = 0;
			for (u32 k = 0; k < 10; k++) {
				if (!(seen & (1 << k)))
					len += snprintf(line + len, sizeof(line) - len, "%s ", names[k]);
			}
			Clear(0, 60, 240, 13, RGB(0, 0, 0), 1);
			DrawHZText12(line[0] ? line : "all seen", 39, 2, 60, YELLOW, 1);
		}
	}
	diag_y = 74;
	if (seen != 0x3FF)
		tlog("diag: keys never pressed: %03X", (unsigned)(~seen & 0x3FF));
	result(seen == 0x3FF, "every button");
	if (~seen & (KEY_L | KEY_R)) {
		say(YELLOW, "The in-game menu and sleep hotkeys use");
		say(YELLOW, "L and R by default: they need both.");
	}
	wait_release();
}

static void step_checklist(void)
{
	const u32 count = sizeof(checklist) / sizeof(*checklist);
	for (u32 i = 0; i < count; i++) {
		const struct check *c = &checklist[i];
		step("checklist");
		say(GREY, "item %lu of %lu", (unsigned long)i + 1, (unsigned long)count);
		if (i == 0)
			show_colour_bars();
		if (i == 1)
			Set_LED_control(0x80 | 0x20 | 0x08);  // breathing red and blue
		if (i == CLOCK_ITEM) {
			diag_y = CLOCK_Y + 12;
			clock_shown = 0xFF;  // not a BCD second: drawn at once
			clock_draw();
			wait_tick = clock_tick;
		}
		char topic[48];
		snprintf(topic, sizeof(topic), "checklist %lu (%s)", (unsigned long)i + 1,
			 c->name + sizeof(CHECK_PREFIX) - 1);
		int answer = ask(topic, c->question, i > CLOCK_ITEM ? ASK_TRIED : ASK_CHECK);
		wait_tick = NULL;
		if (i == 1)
			CheckSwitch();  // the LED as the settings have it
		if (i == CLOCK_ITEM) {
			// To compare with the real time the user notes: the clock's drift.
			u8 t[7];
			clock_read(t);
			tlog("diag: %s: the clock read 20%02X-%02X-%02X %02X:%02X:%02X at the answer", topic, t[0],
			     t[1], t[2], t[4] & 0x3F, t[5], t[6]);
		}
		if (answer == ANSWER_NO) {
			add_failure(c->name);
		} else if (answer == ANSWER_YES && c->needs) {
			if (log_seen & c->needs) {
				tlog("diag: %s: the log shows it", topic);
			} else {
				say(YELLOW, "unconfirmed: the log doesn't show it");
				tlog("diag: %s: unconfirmed: the log has no %s since the last run", topic, c->proof);
				if (unconfirmed_count < sizeof(unconfirmed) / sizeof(*unconfirmed))
					unconfirmed[unconfirmed_count++] = c->name;
				pause_frames(90);
			}
		}
	}
}

// --- The run -----------------------------------------------------------------

static u32 crash_loop[1] EWRAM_BSS;

void Diagnostics(void)
{
	screen_title("start?");
	say(WHITE, "Diagnostics tests the cart and writes");
	say(WHITE, "a report to /torii/logs. It never");
	say(WHITE, "writes your saves. It takes a minute,");
	say(WHITE, "then restarts the kernel.");
	say(WHITE, "A: start   B: back");
	if (wait_key(KEY_A | KEY_B) & KEY_B)
		SoftReset_now();
	if (tlog_level == TLOG_OFF) {  // this session has no line on the card yet
		tlog_set_level(TLOG_DEBUG);
		tlog_marker("Diagnostics", 0);
	}
	tlog_set_level(TLOG_DEBUG);
	diag_step_no = 0;
	summary_count = 0;
	unconfirmed_count = 0;
	tlog("diag: Diagnostics started, logging at debug for this run");

	step_info();
	step_unit_tests();
	step_rtc();
	step_psram();
	step_nor();
	step_loader();
	step_save_chip();
	step_timing();
	read_log();
	step_eeprom();

	step("questions");
	say(WHITE, "The rest needs you: rumble, buttons and");
	say(WHITE, "a short checklist.");
	say(WHITE, "A: go on   START: skip them");
	if (wait_key(KEY_A | KEY_START) & KEY_A) {
		step_rumble();
		step_keys();
		step_checklist();
	} else {
		tlog("diag: questions skipped");
	}

	step("summary");
	if (summary_count == 0)
		say(GREEN, unconfirmed_count ? "no check failed" : "everything passed");
	for (u32 i = 0; i < summary_count; i++)
		say(RED, "failed: %s", summary[i]);
	for (u32 i = 0; i < unconfirmed_count; i++)
		say(YELLOW, "unconfirmed: %s", unconfirmed[i]);
	say(WHITE, "The report is in /torii/logs.");
	say(WHITE, "A: restart the kernel");
	say(WHITE, "SELECT: crash it on purpose, to test");
	say(WHITE, "the crash dump, then restart");
	tlog("diag: done");
	cartlog_phase(PHASE_MENU, 0, NULL);
	tlog_flush();
	u16 key = wait_key(KEY_A | KEY_SELECT);
	if (key & KEY_SELECT) {
		cartlog_phase(PHASE_CRASH_TEST, 0, NULL);
		tlog("diag: crash test: running from EWRAM, outside the kernel's code");
		tlog_flush();
		crash_loop[0] = 0xEAFFFFFE;  // ARM: b .
		((void (*)(void))crash_loop)();
	}
	SoftReset_now();
	for (;;)
		;
}
