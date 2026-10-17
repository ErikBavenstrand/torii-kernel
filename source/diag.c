// Diagnostics (see diag.h). Every result goes to the log as a "diag:" line;
// the screen shows the same lines in the menu's font, a page at a time. The
// steps that need the user come last: rumble, the checklist from
// docs/release-checklist.md, and a crash test.
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

#include "ff.h"
#include "ez_define.h"
#include "ezkernel.h"
#include "Ezcard_OP.h"
#include "RTC.h"
#include "cartlog.h"
#include "diag.h"
#include "draw.h"
#include "lz77.h"
#include "tlog.h"
#include "../tests/kernel/torii_test.h"

#define REG_WAITCNT_IO (*(vu16 *)0x04000204)

extern u32 FAT_table_buffer[FAT_table_size / 4];
extern u32 gl_currentpage;
extern FATFS EZcardFs;
extern const u8 __rom_end__;
extern void SoftReset_now(void);

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

static u16 wait_key(u16 mask)
{
	wait_release();
	for (;;) {
		VBlankIntrWait();
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

// Asks a yes/no question about what the user saw: A yes, B no, SELECT skip.
static int ask(const char *topic, const char *question)
{
	say(WHITE, "%s", question);
	say(GREY, "A: yes   B: no   SELECT: skip");
	u16 key = wait_key(KEY_A | KEY_B | KEY_SELECT);
	int answer = (key & KEY_A) ? ANSWER_YES : (key & KEY_B) ? ANSWER_NO : ANSWER_SKIP;
	say(answer == ANSWER_YES  ? GREEN
	    : answer == ANSWER_NO ? RED
				  : GREY,
	    "%s: %s", topic,
	    answer == ANSWER_YES  ? "yes"
	    : answer == ANSWER_NO ? "no"
				  : "skipped");
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

// --- Steps -------------------------------------------------------------------

static const char *summary[16];
static u32 summary_count;

// Shows a check's result; failures go in the summary. what is a literal.
static void result(int ok, const char *what)
{
	say(ok ? GREEN : RED, "%s: %s", what, ok ? "pass" : "FAIL");
	if (!ok && summary_count < sizeof(summary) / sizeof(*summary))
		summary[summary_count++] = what;
}

static void step_info(void)
{
	step("the cart");
	u16 fpga = Read_FPGA_ver();
	say(WHITE, "FPGA firmware %u (%s)", fpga & 0xFF,
	    ((fpga & 0xF000) == 0xA000 || (fpga & 0xF000) == 0xB000) ? "LX16, Rev.B" : "LX9, Rev.A");
	tlog("diag: FPGA version register %04X", fpga);
	say(WHITE, "kernel NOR id %04X, game NOR id %04X", Read_S71NOR_ID(), Read_S98NOR_ID());
	say(WHITE, "WAITCNT %04X (the kernel never sets it)", REG_WAITCNT_IO);
	say(WHITE, "kernel ROM %08lX-%08lX", 0x08000000ul, (unsigned long)&__rom_end__);
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

// --- EEPROM 512 or 8K ----------------------------------------------------------

static u16 eeprom_request[2 + 14 + 1] EWRAM_BSS;
static u16 eeprom_reply[68] EWRAM_BSS;

// Reads EEPROM block `block`, sending addr_bits address bits, in game mode:
// with the ROM page at 0x200 the FPGA presents the save type the parameter
// block set. It only reads; a write would need the request to start 1, 0.
void IWRAM_CODE diag_eeprom_read(u32 block, u32 addr_bits)
{
	u32 n = 0;
	eeprom_request[n++] = 1;
	eeprom_request[n++] = 1;
	for (u32 bit = addr_bits; bit-- > 0;)
		eeprom_request[n++] = (block >> bit) & 1;
	eeprom_request[n++] = 0;

	u16 ime = REG_IME;
	REG_IME = 0;
	SetRompage(0x200);
	REG_DMA3SAD = (u32)eeprom_request;
	REG_DMA3DAD = 0x0DFFFF00;
	REG_DMA3CNT = DMA_ENABLE | DMA16 | n;
	REG_DMA3SAD = 0x0DFFFF00;
	REG_DMA3DAD = (u32)eeprom_reply;
	REG_DMA3CNT = DMA_ENABLE | DMA16 | 68;
	SetRompage(gl_currentpage);
	REG_IME = ime;
}

static void set_save_type(u8 type, u32 size)
{
	init_FAT_table();
	FAT_table_buffer[0x1F4 / 4] = SET_PARAMETER_MODE;
	FAT_table_buffer[0x1F8 / 4] = EZcardFs.csize;
	FAT_table_buffer[0x1FC / 4] = ((u32)type << 24) | size;
	Send_FATbuffer(FAT_table_buffer, 1);
}

static void step_eeprom(void)
{
	step("EEPROM 512 or 8K");
	// A block of the save chip that isn't blank, to compare the replies with.
	u8 fram[0x200];
	SetRampage(0);
	ReadSram(SRAMSaver, fram, sizeof(fram));
	u32 block = 64;
	// Blocks 0-15 only: the address bits a wrong-width request leaves over
	// then start 0, 0, never 1, 0, the start of a write.
	for (u32 b = 0; b < 16 && block == 64; b++) {
		u32 ones = 0, zeros = 0;
		for (u32 i = 0; i < 8; i++) {
			ones += fram[b * 8 + i] == 0xFF;
			zeros += fram[b * 8 + i] == 0x00;
		}
		if (ones < 8 && zeros < 8)
			block = b;
	}
	if (block == 64) {
		say(GREY, "The save chip's first 128 bytes are");
		say(GREY, "blank, so replies can't be told apart:");
		say(GREY, "save in an EEPROM game, then run this.");
		result(1, "EEPROM probe (inconclusive)");
		pause_frames(120);
		return;
	}
	const u8 *want = &fram[block * 8];
	say(WHITE, "block %lu: %02X%02X%02X%02X%02X%02X%02X%02X", (unsigned long)block, want[0], want[1], want[2],
	    want[3], want[4], want[5], want[6], want[7]);
	static const u8 types[] = {0x21, 0x22};
	static const u32 sizes[] = {0x200, 0x2000};
	static const u32 widths[] = {6, 14};
	for (u32 t = 0; t < 2; t++) {
		set_save_type(types[t], sizes[t]);
		for (u32 n = 0; n < 2; n++) {
			u32 w = t ? 1 - n : n;  // the type's own width first
			diag_eeprom_read(block, widths[w]);
			u8 got[8] = {0};
			for (u32 i = 0; i < 64; i++)
				got[i / 8] |= (eeprom_reply[4 + i] & 1) << (7 - i % 8);
			u8 rev[8];
			for (u32 i = 0; i < 8; i++)
				rev[i] = got[7 - i];
			const char *verdict =
				!memcmp(got, want, 8)   ? "matches"
				: !memcmp(rev, want, 8) ? "matches, reversed"
				: (got[0] & got[1] & got[2] & got[3] & got[4] & got[5] & got[6] & got[7]) == 0xFF
					? "all ones: no answer"
					: "differs";
			say(WHITE, "type %02X, %2lu-bit request: %s", types[t], (unsigned long)widths[w], verdict);
			tlog("diag: eeprom type %02X width %lu reply %02X%02X%02X%02X%02X%02X%02X%02X", types[t],
			     (unsigned long)widths[w], got[0], got[1], got[2], got[3], got[4], got[5], got[6], got[7]);
		}
	}
	// End with a request of the right width, then SRAM as the save type,
	// so no EEPROM command is left half sent; the next launch sets its own.
	diag_eeprom_read(block, 14);
	set_save_type(0x11, 0x8000);
	init_FAT_table();
	pause_frames(120);
}

// --- Rumble ------------------------------------------------------------------

// The cart's GPIO port: pin 3 drives the rumble motor (Drill Dozer's
// wiring), pins 0-2 the clock.
static void rumble_pulse(void)
{
	*RTC_ENABLE = 1;
	*RTC_RW = 0xF;
	*RTC_DATA = 0x9;
	pause_frames(40);
	*RTC_DATA = 0x1;
	*RTC_RW = 0x7;
}

static int clock_answers(void)
{
	u8 t[7];
	rtc_enable();
	rtc_get(t);
	rtc_disenable();
	return bcd_ok(t[1], 12) && t[1] && bcd_ok(t[2], 31) && t[2] && bcd_ok(t[5], 59) && bcd_ok(t[6], 59);
}

static void step_rumble(void)
{
	step("rumble");
	say(WHITE, "Hold the console: the motor pulses with");
	say(WHITE, "GPIO register 0x096A0000 at 1, 0, 2, 3.");
	static const u16 values[] = {1, 0, 2, 3};
	for (u32 i = 0; i < 4; i++) {
		Set_RTC_status(values[i]);
		pause_frames(10);
		int clock = clock_answers();
		say(WHITE, "register %u: clock %s", values[i], clock ? "answers" : "silent");
		rumble_pulse();
		Set_RTC_status(1);  // the clock back before the answer is logged with a time
		char topic[32];
		snprintf(topic, sizeof(topic), "rumble at register %u", values[i]);
		ask(topic, "Did it rumble just now?");
	}
	Set_RTC_status(1);
}

// --- Checklist ---------------------------------------------------------------

// docs/release-checklist.md, "In Diagnostics": things only a person can see.
// Each line fits the screen.
static const char *const checklist[] = {
	"The bars are red, green, blue, white.",
	"The LED breathes red and blue now.",
	"The clock above shows the right time.",
	"Since the last run, a GBA game started.",
	"...its save survived a power cycle.",
	"...the in-game menu opened.",
	"...a save state saved and loaded.",
	"...sleep slept and woke.",
	"...a GB or GBC game ran.",
	"...a NES game ran.",
	"...a game written to NOR started.",
	"...the clock kept time while off.",
	"...DEBUG in a game reached the log.",
};

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
	wait_release();
}

static void step_checklist(void)
{
	u32 fails = 0;
	for (u32 i = 0; i < sizeof(checklist) / sizeof(*checklist); i++) {
		step("checklist");
		say(GREY, "item %lu of %lu", (unsigned long)i + 1,
		    (unsigned long)(sizeof(checklist) / sizeof(*checklist)));
		if (i == 0)
			show_colour_bars();
		if (i == 1)
			Set_LED_control(0x80 | 0x20 | 0x08);  // breathing red and blue
		if (i == 2) {
			u8 t[7];
			rtc_enable();
			rtc_get(t);
			rtc_disenable();
			say(YELLOW, "20%02X-%02X-%02X %02X:%02X:%02X", t[0], t[1], t[2], t[4] & 0x3F, t[5], t[6]);
		}
		char topic[16];
		snprintf(topic, sizeof(topic), "checklist %lu", (unsigned long)i + 1);
		fails += ask(topic, checklist[i]) == ANSWER_NO;
		if (i == 1)
			CheckSwitch();  // the LED as the settings have it
	}
	result(!fails, "checklist");
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
	tlog("diag: Diagnostics started, logging at debug for this run");

	step_info();
	step_unit_tests();
	step_rtc();
	step_psram();
	step_nor();
	step_save_chip();
	step_timing();
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
	if (summary_count == 0) {
		say(GREEN, "everything passed");
	} else {
		for (u32 i = 0; i < summary_count; i++)
			say(RED, "failed: %s", summary[i]);
	}
	say(WHITE, "The report is in /torii/logs.");
	say(WHITE, "A: restart the kernel");
	say(WHITE, "SELECT: crash it on purpose, to test");
	say(WHITE, "the crash dump, then restart");
	tlog("diag: done");
	cartlog_phase(PHASE_MENU, 0, NULL);
	tlog_flush();
	u16 key = wait_key(KEY_A | KEY_SELECT);
	if (key & KEY_SELECT) {
		tlog("diag: crash test: running from EWRAM, outside the kernel's code");
		tlog_flush();
		crash_loop[0] = 0xEAFFFFFE;  // ARM: b .
		((void (*)(void))crash_loop)();
	}
	SoftReset_now();
	for (;;)
		;
}
