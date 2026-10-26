// Torii in-game probe ROM (v0.8 P0, source/ingameprobe.h): tries in game
// mode, where the in-game menu runs, what the menu may want there and no one
// has proven on the real cart, and leaves what it saw in cart RAM page 0xF0
// for the kernel's next boot to log.
//
// Launch it CLEAN BOOT, once for each save type to try (SAVE TYPE in the
// game's popup; flash 128K's bank test means something only with a save
// whose two banks differ). It runs by itself, a few seconds; then A goes
// back to the kernel. What it writes it puts back: cart SRAM, the PSRAM
// halfword, and the kernel's scratch sector (INGAME_PROBE_FILE).
#include <stdint.h>

#include "../../../source/cartlog.h"
#include "../../../source/ingameprobe.h"

#define REG16(a) (*(volatile uint16_t *)(a))
#define REG_DISPCNT REG16(0x04000000)
#define REG_VCOUNT REG16(0x04000006)
#define REG_KEYINPUT REG16(0x04000130)
#define REG_IME REG16(0x04000208)
#define REG_WAITCNT REG16(0x04000204)
#define SCREEN ((volatile uint16_t *)0x06000000)
#define CART_RAM ((volatile uint8_t *)0x0E000000)
#define SD_BUFFER ((volatile uint16_t *)0x09E00000)
#define RTC_DATA REG16(0x080000C4)
#define RTC_RW REG16(0x080000C6)
#define RTC_ENABLE REG16(0x080000C8)
#define PSRAM_TEST REG16(0x08010000)
#define RGB(r, g, b) ((r) | ((g) << 5) | ((b) << 10))

#define KEY_A 1

#define REPORT_OFFSET (CARTLOG_INGAME_PROBE - 0x0E000000)

// Runs from IWRAM: SD mode hides the ROM, and the PSRAM test writes it
#define IWRAM_FN __attribute__((section(".iwram"), long_call, noinline, target("arm")))

// A cart register, behind its unlock sequence (source/Ezcard_OP.c), at the
// BIOS's wait states as the kernel writes them
#define CART_REG(address, value) \
	do { \
		uint16_t w_ = REG_WAITCNT; \
		REG_WAITCNT = 0; \
		REG16(0x09FE0000) = 0xD200; \
		REG16(0x08000000) = 0x1500; \
		REG16(0x08020000) = 0xD200; \
		REG16(0x08040000) = 0x1500; \
		REG16(address) = (value); \
		REG16(0x09FC0000) = 0x1500; \
		REG_WAITCNT = w_; \
	} while (0)

// --- The screen: a 3x5 font, drawn twice its size (as tests/roms/rumble) ------

static const uint16_t font[] = {
	['0' - ' '] = 0x7B6F, ['1' - ' '] = 0x2C97, ['2' - ' '] = 0x73E7, ['3' - ' '] = 0x73CF, ['4' - ' '] = 0x5BC9,
	['5' - ' '] = 0x79CF, ['6' - ' '] = 0x79EF, ['7' - ' '] = 0x7249, ['8' - ' '] = 0x7BEF, ['9' - ' '] = 0x7BCF,
	['A' - ' '] = 0x2BED, ['B' - ' '] = 0x6BAE, ['C' - ' '] = 0x3923, ['D' - ' '] = 0x6B6E, ['E' - ' '] = 0x79A7,
	['F' - ' '] = 0x79A4, ['G' - ' '] = 0x396B, ['H' - ' '] = 0x5BED, ['I' - ' '] = 0x7497, ['J' - ' '] = 0x126A,
	['K' - ' '] = 0x5BAD, ['L' - ' '] = 0x4927, ['M' - ' '] = 0x5FED, ['N' - ' '] = 0x6B6D, ['O' - ' '] = 0x2B6A,
	['P' - ' '] = 0x6BA4, ['Q' - ' '] = 0x2B73, ['R' - ' '] = 0x6BAD, ['S' - ' '] = 0x388E, ['T' - ' '] = 0x7492,
	['U' - ' '] = 0x5B6F, ['V' - ' '] = 0x5B6A, ['W' - ' '] = 0x5BFD, ['X' - ' '] = 0x5AAD, ['Y' - ' '] = 0x5A92,
	['Z' - ' '] = 0x72A7, [':' - ' '] = 0x0410, ['-' - ' '] = 0x01C0, ['.' - ' '] = 0x0002,
};

static void text(int x, int y, uint16_t colour, const char *s)
{
	for (; *s; s++, x += 8) {
		unsigned c = (unsigned)(*s - ' ');
		uint16_t g = c < sizeof(font) / sizeof(*font) ? font[c] : 0;
		for (int row = 0; row < 5; row++)
			for (int col = 0; col < 3; col++)
				if (g & (1 << (14 - row * 3 - col)))
					for (int i = 0; i < 4; i++)
						SCREEN[(y + row * 2 + i / 2) * 240 + x + col * 2 + i % 2] = colour;
	}
}

static void clear(void)
{
	for (int i = 0; i < 240 * 160; i++)
		SCREEN[i] = RGB(2, 4, 10);
}

static void wait_key(uint16_t mask)
{
	while (~REG_KEYINPUT & 0x3FF)  // let go first
		;
	while (!(~REG_KEYINPUT & mask))
		;
}

static struct ingame_probe report;

static void ram_page(uint16_t page)
{
	CART_REG(0x09C00000, page);
}

// --- Cart SRAM ----------------------------------------------------------------

// 4 bytes written and read back at each page's start, then put back
static void test_pages(void)
{
	for (uint32_t p = INGAME_PROBE_FIRST_PAGE; p <= INGAME_PROBE_LAST_PAGE; p++) {
		ram_page((uint16_t)p);
		uint8_t keep[4], want[4] = {(uint8_t)p, (uint8_t)~p, 0x5A, 0xA5};
		int ok = 1;
		for (int i = 0; i < 4; i++)
			keep[i] = CART_RAM[i];
		for (int i = 0; i < 4; i++)
			CART_RAM[i] = want[i];
		for (int i = 0; i < 4; i++)
			ok &= CART_RAM[i] == want[i];
		for (int i = 0; i < 4; i++)
			CART_RAM[i] = keep[i];
		report.pages_tried++;
		if (ok)
			report.pages_ok++;
		else if (!report.first_bad_page)
			report.first_bad_page = (uint8_t)p;
	}
	ram_page(0x20);
	report.page_readback[0] = REG16(0x09C00000);
	ram_page(0x55);
	report.page_readback[1] = REG16(0x09C00000);
	ram_page(0);
}

// 256 bytes at each of WAITCNT's SRAM settings, page 0x20, put back after
static void test_waits(void)
{
	static uint8_t keep[256];
	uint16_t waitcnt = REG_WAITCNT;
	ram_page(0x20);
	for (int i = 0; i < 256; i++)
		keep[i] = CART_RAM[0x100 + i];
	for (uint32_t w = 0; w < 4; w++) {
		REG_WAITCNT = (uint16_t)((waitcnt & ~3u) | w);
		int ok = 1;
		for (int i = 0; i < 256; i++)
			CART_RAM[0x100 + i] = (uint8_t)(i * 13 + w);
		for (int i = 0; i < 256; i++)
			ok &= CART_RAM[0x100 + i] == (uint8_t)(i * 13 + w);
		REG_WAITCNT = waitcnt;
		if (ok)
			report.sram_waits_ok |= (uint8_t)(1 << w);
	}
	for (int i = 0; i < 256; i++)
		CART_RAM[0x100 + i] = keep[i];
	ram_page(0);
}

// A flash 128K save's bank 1 across a page change and back (flash commands
// at page 0, as games send them)
static void flash_bank(uint8_t bank)
{
	CART_RAM[0x5555] = 0xAA;
	CART_RAM[0x2AAA] = 0x55;
	CART_RAM[0x5555] = 0xB0;
	CART_RAM[0x0000] = bank;
}

static void test_bank(void)
{
	report.bank0 = CART_RAM[0];
	flash_bank(1);
	report.bank1 = CART_RAM[0];
	ram_page(0x20);
	(void)CART_RAM[0];
	ram_page(0);
	report.bank1_after = CART_RAM[0];
	flash_bank(0);
}

// --- PSRAM --------------------------------------------------------------------

IWRAM_FN static void test_psram(void)
{
	report.psram_before = PSRAM_TEST;
	PSRAM_TEST = 0xA55A;
	report.psram_written = PSRAM_TEST;
	PSRAM_TEST = report.psram_before;
	report.psram_after = PSRAM_TEST;
}

// --- SD (source/Ezcard_OP.c's, from IWRAM) -------------------------------------

static uint16_t sd_buf[INGAME_PROBE_SECTOR / 2];

IWRAM_FN static int sd_wait(void)
{
	for (uint32_t n = 0; n < 0x100000; n++)
		if (SD_BUFFER[0] != 0xEEE1)
			return 0;
	return 1;
}

IWRAM_FN static void sd_command(uint32_t lba, uint16_t count)
{
	REG_WAITCNT = 0;
	REG16(0x09FE0000) = 0xD200;
	REG16(0x08000000) = 0x1500;
	REG16(0x08020000) = 0xD200;
	REG16(0x08040000) = 0x1500;
	REG16(0x09600000) = (uint16_t)lba;
	REG16(0x09620000) = (uint16_t)(lba >> 16);
	REG16(0x09640000) = count;
	REG16(0x09FC0000) = 0x1500;
}

IWRAM_FN static void sd_control(uint16_t value)
{
	REG_WAITCNT = 0;
	REG16(0x09FE0000) = 0xD200;
	REG16(0x08000000) = 0x1500;
	REG16(0x08020000) = 0xD200;
	REG16(0x08040000) = 0x1500;
	REG16(0x09400000) = value;
	REG16(0x09FC0000) = 0x1500;
}

// One sector into sd_buf; 0, or 1 if the card didn't answer
IWRAM_FN static int sd_read(uint32_t lba)
{
	uint16_t waitcnt = REG_WAITCNT;
	sd_control(1);
	sd_command(lba, 1);
	sd_control(3);
	int bad = sd_wait();
	sd_control(1);
	if (!bad)
		for (int i = 0; i < INGAME_PROBE_SECTOR / 2; i++)
			sd_buf[i] = SD_BUFFER[i];
	sd_control(0);
	REG_WAITCNT = waitcnt;
	return bad;
}

// sd_buf to one sector; 0, or 1 if the card didn't answer
IWRAM_FN static int sd_write(uint32_t lba)
{
	uint16_t waitcnt = REG_WAITCNT;
	sd_control(1);
	sd_control(3);
	for (int i = 0; i < INGAME_PROBE_SECTOR / 2; i++)
		SD_BUFFER[i] = sd_buf[i];
	sd_command(lba, 0x8001);
	int bad = sd_wait();
	for (volatile int d = 0; d < 30000; d++)  // as Write_SD_sectors' settle
		;
	sd_control(0);
	REG_WAITCNT = waitcnt;
	return bad;
}

static int sd_holds(uint32_t seed)
{
	const uint8_t *b = (const uint8_t *)sd_buf;
	for (uint32_t i = 0; i < INGAME_PROBE_SECTOR; i++)
		if (b[i] != ingame_probe_byte(i, seed))
			return 0;
	return 1;
}

static void sd_fill(uint32_t seed)
{
	uint8_t *b = (uint8_t *)sd_buf;
	for (uint32_t i = 0; i < INGAME_PROBE_SECTOR; i++)
		b[i] = ingame_probe_byte(i, seed);
}

static void test_sd(void)
{
	if (!sd_read(0))
		report.sd_mbr = ((const uint8_t *)sd_buf)[510] == 0x55 && ((const uint8_t *)sd_buf)[511] == 0xAA;
	uint32_t lba = report.scratch_lba;
	if (!lba)
		return;
	if (sd_read(lba)) {
		report.sd_scratch = INGAME_PROBE_SD_READ_FAILED;
		return;
	}
	if (!sd_holds(INGAME_PROBE_SEED_KERNEL)) {
		report.sd_scratch = INGAME_PROBE_SD_NOT_KERNELS;
		return;
	}
	sd_fill(INGAME_PROBE_SEED_PROBE);
	if (sd_write(lba)) {
		report.sd_scratch = INGAME_PROBE_SD_WRITE_FAILED;
		return;
	}
	if (sd_read(lba) || !sd_holds(INGAME_PROBE_SEED_PROBE))
		report.sd_scratch = INGAME_PROBE_SD_WRITE_LOST;
	sd_fill(INGAME_PROBE_SEED_KERNEL);
	if (sd_write(lba) || sd_read(lba) || !sd_holds(INGAME_PROBE_SEED_KERNEL)) {
		report.sd_scratch = INGAME_PROBE_SD_RESTORE_LOST;
		return;
	}
	if (!report.sd_scratch)
		report.sd_scratch = INGAME_PROBE_SD_OK;
}

// --- The clock (source/RTC.c's serial protocol) --------------------------------

static void rtc_cmd(int v)
{
	v <<= 1;
	for (int l = 7; l >= 0; l--) {
		uint16_t b = (uint16_t)((v >> l) & 2);
		RTC_DATA = b | 4;
		RTC_DATA = b | 4;
		RTC_DATA = b | 4;
		RTC_DATA = b | 5;
	}
}

static int rtc_byte(void)
{
	int v = 0;
	for (int l = 0; l < 8; l++) {
		for (int j = 0; j < 5; j++)
			RTC_DATA = 4;
		RTC_DATA = 5;
		v |= (RTC_DATA & 2) << l;
	}
	return v >> 1;
}

static void rtc_get(uint8_t *data)
{
	uint16_t waitcnt = REG_WAITCNT;
	REG_WAITCNT = 0;
	RTC_ENABLE = 1;
	RTC_DATA = 1;
	RTC_RW = 7;
	RTC_DATA = 1;
	RTC_DATA = 5;
	rtc_cmd(0x65);  // date and time
	RTC_RW = 5;
	for (int i = 0; i < 7; i++)
		data[i] = (uint8_t)rtc_byte();
	RTC_ENABLE = 0;
	REG_WAITCNT = waitcnt;
}

static void test_rtc(void)
{
	CART_REG(0x096A0000, 1);
	rtc_get(report.rtc_on);
	CART_REG(0x096A0000, 0);
	rtc_get(report.rtc_off);
	CART_REG(0x096A0000, 1);  // as the kernel leaves it
}

// --- The report ---------------------------------------------------------------

// Cart RAM takes bytes only. The magic goes last, so a report cut off by
// power loss counts as none.
static void save_report(void)
{
	const uint8_t *r = (const uint8_t *)&report;
	ram_page(CARTLOG_PAGE);
	for (unsigned i = 0; i < 16; i++)
		CART_RAM[REPORT_OFFSET + i] = 0;
	for (unsigned i = 16; i < sizeof(report); i++)
		CART_RAM[REPORT_OFFSET + i] = r[i];
	for (unsigned i = 0; i < 16; i++)
		CART_RAM[REPORT_OFFSET + i] = (uint8_t)INGAME_PROBE_MAGIC[i];
	ram_page(0);
}

// The kernel's scratch sector, from the report's place
static uint32_t scratch_lba(void)
{
	ram_page(CARTLOG_PAGE);
	uint32_t lba = 0;
	for (int i = 0; i < 4; i++)
		lba |= (uint32_t)CART_RAM[REPORT_OFFSET + 16 + i] << (8 * i);
	ram_page(0);
	return lba;
}

// Back to the kernel, as the in-game menu's Back to Torii does
__attribute__((section(".iwram"), long_call, noinline, noreturn, target("arm"))) static void back_to_kernel(void)
{
	REG_IME = 0;
	CART_REG(0x09880000, 0x8002);
	*(volatile uint8_t *)0x03007FFA = 0;
	__asm__ volatile("mov r0, #0xFC\n\tswi 0x010000\n\tswi 0x000000" ::: "r0", "r1", "r2", "r3", "memory");
	for (;;)
		;
}

int main(void)
{
	REG_WAITCNT = 0x4317;  // the cart wait states commercial games use
	REG_DISPCNT = 3 | (1 << 10);
	uint16_t white = RGB(31, 31, 31), yellow = RGB(31, 31, 8);
	clear();
	text(8, 8, yellow, "TORII IN-GAME PROBE");
	text(8, 32, white, "TRYING THE CART IN GAME MODE.");
	report.scratch_lba = scratch_lba();
	test_pages();
	test_waits();
	test_bank();
	test_psram();
	test_sd();
	test_rtc();
	save_report();
	text(8, 56, white, "DONE.");
	text(8, 80, white, "A: BACK TO THE KERNEL,");
	text(8, 92, white, "WHICH LOGS WHAT IT SAW.");
	wait_key(KEY_A);
	back_to_kernel();
}
