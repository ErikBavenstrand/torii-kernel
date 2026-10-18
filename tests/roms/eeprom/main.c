// Torii EEPROM probe ROM: settles whether the cart's EEPROM save types take
// 6-bit or 14-bit addresses, and how many blocks they hold, which the kernel
// can't find out itself (the cart's EEPROM answers only in game mode).
//
// Launch it CLEAN BOOT with SAVE TYPE set to EEPROM 512 or EEPROM 8K. A runs
// the test with 6-bit addresses, B with 14-bit ones: one width per launch, so
// a request of the wrong width can't leave the chip half way through a
// command for the other. It writes and reads a few blocks, shows a verdict,
// and leaves every step in cart RAM page 0xF0 (source/eepromprobe.h); then A
// goes back to the kernel, whose boot logs the report. Its EEPROM is its own
// save, so nothing else is touched.
#include <stdint.h>
#include <string.h>

#include "../../../source/eepromprobe.h"

#define REG_DISPCNT (*(volatile uint16_t *)0x04000000)
#define REG_VCOUNT (*(volatile uint16_t *)0x04000006)
#define REG_KEYINPUT (*(volatile uint16_t *)0x04000130)
#define REG_IME (*(volatile uint16_t *)0x04000208)
#define REG_DMA3SAD (*(volatile uint32_t *)0x040000D4)
#define REG_DMA3DAD (*(volatile uint32_t *)0x040000D8)
#define REG_DMA3CNT (*(volatile uint32_t *)0x040000DC)
#define REG_WAITCNT (*(volatile uint16_t *)0x04000204)
#define VRAM ((volatile uint16_t *)0x06000000)
#define CART_RAM ((volatile uint8_t *)0x0E000000)
#define EEPROM ((volatile uint16_t *)0x0D000000)
#define RGB(r, g, b) ((r) | ((g) << 5) | ((b) << 10))

#define KEY_A 1
#define KEY_B 2

// Where the kernel looks (source/cartlog.h: CARTLOG_PAGE, CARTLOG_EEPROM_PROBE).
#define REPORT_PAGE 0xF0
#define REPORT_OFFSET 0x1800

// --- The screen: a 3x5 font, drawn twice its size ----------------------------

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
						VRAM[(y + row * 2 + i / 2) * 240 + x + col * 2 + i % 2] = colour;
	}
}

static void clear(void)
{
	for (int i = 0; i < 240 * 160; i++)
		VRAM[i] = RGB(2, 4, 10);
}

static uint16_t wait_key(uint16_t mask)
{
	while (~REG_KEYINPUT & 0x3FF)  // let go first
		;
	for (;;) {
		uint16_t down = ~REG_KEYINPUT & mask;
		if (down)
			return down;
	}
}

// --- EEPROM ------------------------------------------------------------------

static uint16_t bits[2 + 14 + 64 + 1];
static unsigned width;

// DMA reads and writes bits[] behind the compiler's back.
#define MEMORY_BARRIER() __asm__ volatile("" ::: "memory")

static void dma(volatile void *dst, const volatile void *src, int count)
{
	MEMORY_BARRIER();
	REG_DMA3SAD = (uint32_t)src;
	REG_DMA3DAD = (uint32_t)dst;
	REG_DMA3CNT = count | 0x80000000;
	while (REG_DMA3CNT & 0x80000000)
		;
	MEMORY_BARRIER();
}

static int put_address(unsigned block)
{
	for (unsigned i = 0; i < width; i++)
		bits[2 + i] = (block >> (width - 1 - i)) & 1;
	return 2 + width;
}

static void read_block(unsigned block, uint8_t out[8])
{
	bits[0] = 1;
	bits[1] = 1;
	int n = put_address(block);
	bits[n++] = 0;
	dma(EEPROM, bits, n);
	dma(bits, EEPROM, 68);
	for (int byte = 0; byte < 8; byte++) {
		uint8_t v = 0;
		for (int b = 0; b < 8; b++)
			v = (uint8_t)(v << 1 | (bits[4 + byte * 8 + b] & 1));
		out[byte] = v;
	}
}

// Returns how many polls the chip took to say it was ready.
static uint32_t write_block(unsigned block, const uint8_t in[8])
{
	bits[0] = 1;
	bits[1] = 0;
	int n = put_address(block);
	for (int byte = 0; byte < 8; byte++)
		for (int b = 0; b < 8; b++)
			bits[n + byte * 8 + b] = (in[byte] >> (7 - b)) & 1;
	n += 64;
	bits[n++] = 0;
	dma(EEPROM, bits, n);
	// A real EEPROM takes about 6 ms; a million polls is over a second.
	for (uint32_t polls = 0; polls < 1000000; polls++)
		if (EEPROM[0] & 1)
			return polls;
	return EEPROM_PROBE_TIMEOUT;
}

// --- The probe ---------------------------------------------------------------

static struct eeprom_probe report;

static void step_read(unsigned block)
{
	struct eeprom_probe_step *s = &report.step[report.steps++];
	s->op = EEPROM_PROBE_READ;
	s->width = (uint8_t)width;
	s->block = (uint16_t)block;
	read_block(block, s->data);
}

static void step_write(unsigned block)
{
	struct eeprom_probe_step *s = &report.step[report.steps++];
	s->op = EEPROM_PROBE_WRITE;
	s->width = (uint8_t)width;
	s->block = (uint16_t)block;
	eeprom_probe_pattern(s->data, width, block);
	s->polls = write_block(block, s->data);
}

static void probe(void)
{
	report.width = width;
	step_read(1);  // what an earlier run left
	step_write(1);
	step_read(1);
	step_write(2);
	step_read(2);
	step_read(1);
	step_write(63);
	step_read(63);
	if (width == 14) {
		// 512 bytes are blocks 0-63: past them, block 65 lands on block 1
		// and block 1023 on block 63 if the chip wraps.
		step_write(65);
		step_read(65);
		step_read(1);
		step_write(1023);
		step_read(1023);
		step_read(63);
	}
	step_read(2);
}

// The cart's RAM page register, behind its unlock sequence (as the in-game
// menu's SetRampage in source/gba_rts_patch.s).
static void set_ram_page(uint16_t page)
{
	*(volatile uint16_t *)0x09FE0000 = 0xD200;
	*(volatile uint16_t *)0x08000000 = 0x1500;
	*(volatile uint16_t *)0x08020000 = 0xD200;
	*(volatile uint16_t *)0x08040000 = 0x1500;
	*(volatile uint16_t *)0x09C00000 = page;
	*(volatile uint16_t *)0x09FC0000 = 0x1500;
}

// Cart RAM takes bytes only. The magic goes last, so a report cut off by
// power loss counts as none.
static void save_report(void)
{
	const uint8_t *r = (const uint8_t *)&report;
	set_ram_page(REPORT_PAGE);
	for (unsigned i = 0; i < 16; i++)
		CART_RAM[REPORT_OFFSET + i] = 0;
	for (unsigned i = 16; i < sizeof(report); i++)
		CART_RAM[REPORT_OFFSET + i] = r[i];
	for (unsigned i = 0; i < 16; i++)
		CART_RAM[REPORT_OFFSET + i] = EEPROM_PROBE_MAGIC[i];
	set_ram_page(0);
}

// Back to the kernel, as the in-game menu's RESET does: the ROM page to the
// kernel's, then a soft reset. It runs from IWRAM, as the ROM goes away.
__attribute__((section(".iwram"), long_call, noinline, noreturn, target("arm"))) static void back_to_kernel(void)
{
	REG_IME = 0;
	*(volatile uint16_t *)0x09FE0000 = 0xD200;
	*(volatile uint16_t *)0x08000000 = 0x1500;
	*(volatile uint16_t *)0x08020000 = 0xD200;
	*(volatile uint16_t *)0x08040000 = 0x1500;
	*(volatile uint16_t *)0x09880000 = 0x8002;
	*(volatile uint16_t *)0x09FC0000 = 0x1500;
	*(volatile uint8_t *)0x03007FFA = 0;  // the soft reset starts at 0x08000000
	__asm__ volatile("mov r0, #0xFC\n\tswi 0x010000\n\tswi 0x000000" ::: "r0", "r1", "r2", "r3", "memory");
	for (;;)
		;
}

int main(void)
{
	REG_WAITCNT = 0x4317;  // the cart waitstates commercial games use
	REG_DISPCNT = 3 | (1 << 10);
	clear();
	text(8, 8, RGB(31, 31, 8), "TORII EEPROM PROBE");
	text(8, 32, RGB(31, 31, 31), "LAUNCHED WITH SAVE TYPE");
	text(8, 44, RGB(31, 31, 31), "EEPROM 512 OR 8K.");
	text(8, 68, RGB(31, 31, 31), "A: 6-BIT ADDRESSES");
	text(8, 80, RGB(31, 31, 31), "B: 14-BIT ADDRESSES");
	width = (wait_key(KEY_A | KEY_B) & KEY_A) ? 6 : 14;

	probe();
	save_report();
	uint32_t v = eeprom_probe_verdict(&report);

	clear();
	text(8, 8, RGB(31, 31, 8), width == 6 ? "6-BIT RUN" : "14-BIT RUN");
	uint16_t good = RGB(8, 31, 8), bad = RGB(31, 8, 8);
	text(8, 32, (v & EEPROM_PROBE_WORKS) ? good : bad,
	     (v & EEPROM_PROBE_WORKS) ? "READS BACK WHAT IT WROTE" : "DOES NOT READ BACK");
	if (v & EEPROM_PROBE_TIMED_OUT)
		text(8, 44, bad, "A WRITE NEVER FINISHED");
	if (width == 14)
		text(8, 56, RGB(31, 31, 31),
		     (v & EEPROM_PROBE_WRAPS) ? "WRAPS AT 64 BLOCKS: 512 B"
		     : (v & EEPROM_PROBE_8K)  ? "1024 BLOCKS: 8 KB"
					      : "SIZE UNKNOWN");
	text(8, 92, RGB(31, 31, 31), "A: BACK TO THE KERNEL,");
	text(8, 104, RGB(31, 31, 31), "WHICH LOGS EVERY STEP.");
	wait_key(KEY_A);
	back_to_kernel();
}
