// Torii rumble probe ROM: settles how the Omega DE's rumble motor is driven,
// in game mode, where Jagoomba drives it. Diagnostics, in kernel mode, got no
// rumble the way Jagoomba drives it (real cart, 2026-10-18).
//
// Launch it CLEAN BOOT. It drives the motor four ways (source/rumbleprobe.h),
// about a second each, and asks after each whether the cart rumbled: A yes,
// B no, SELECT skip. Each try also reads 0x08001000 before, after its first
// 2 and after its last 0, to tell whether those writes reach the PSRAM the
// ROM runs from, and puts the halfword back if they did. Then A goes back to
// the kernel, whose boot logs the answers from cart RAM page 0xF0. It has no
// save and touches no other page.
#include <stdint.h>

#include "../../../source/rumbleprobe.h"

#define REG16(a) (*(volatile uint16_t *)(a))
#define REG_DISPCNT REG16(0x04000000)
#define REG_VCOUNT REG16(0x04000006)
#define REG_KEYINPUT REG16(0x04000130)
#define REG_IME REG16(0x04000208)
#define REG_WAITCNT REG16(0x04000204)
#define VRAM ((volatile uint16_t *)0x06000000)
#define CART_RAM ((volatile uint8_t *)0x0E000000)
#define MOTOR REG16(0x08001000)
#define RGB(r, g, b) ((r) | ((g) << 5) | ((b) << 10))

#define KEY_A 1
#define KEY_B 2
#define KEY_SELECT 4

// Where the kernel looks (source/cartlog.h: CARTLOG_PAGE, CARTLOG_RUMBLE_PROBE).
#define REPORT_PAGE 0xF0
#define REPORT_OFFSET 0x1A00

#define FRAMES 60  // each try's length

// Runs from IWRAM: a try's writes may land in the PSRAM the ROM runs from.
#define IWRAM_FN __attribute__((section(".iwram"), long_call, noinline, target("arm")))

// A cart register, behind its unlock sequence (source/Ezcard_OP.c).
#define CART_REG(address, value)            \
	do {                                \
		REG16(0x09FE0000) = 0xD200; \
		REG16(0x08000000) = 0x1500; \
		REG16(0x08020000) = 0xD200; \
		REG16(0x08040000) = 0x1500; \
		REG16(address) = (value);   \
		REG16(0x09FC0000) = 0x1500; \
	} while (0)

#define WAIT_FRAME()                     \
	do {                             \
		while (REG_VCOUNT >= 160) \
			;                \
		while (REG_VCOUNT < 160)  \
			;                \
	} while (0)

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

// --- The motor ---------------------------------------------------------------

static struct rumble_probe report;

// One try, about a second, with interrupts off (none are on in this ROM).
IWRAM_FN static void drive(struct rumble_probe_try *t)
{
	uint32_t way = t->way;
	REG_IME = 0;
	CART_REG(0x096A0000, way == RUMBLE_PULSED_PORT_OFF ? 0 : 1);
	t->before = MOTOR;
	if (way == RUMBLE_GPIO_PIN3) {
		REG16(0x080000C8) = 1;
		REG16(0x080000C6) = 8;
		REG16(0x080000C4) = 8;
		for (int f = 0; f < FRAMES; f++)
			WAIT_FRAME();
		REG16(0x080000C4) = 0;
		t->on = t->off = MOTOR;
	} else {
		for (int f = 0; f < FRAMES; f++) {
			if (f == 0 || way != RUMBLE_HELD) {
				CART_REG(0x09E20000, 0xF1);
				MOTOR = 2;
				if (f == 0)
					t->on = MOTOR;
			}
			if (way != RUMBLE_HELD)
				MOTOR = 0;
			WAIT_FRAME();
		}
		MOTOR = 0;
		t->off = MOTOR;
		if (t->off != t->before)
			MOTOR = t->before;  // the write reached the PSRAM: put the ROM back
	}
	CART_REG(0x096A0000, 1);
}

static const struct {
	uint8_t way;
	const char *name[2];
} tries[] = {
	{RUMBLE_PULSED, {"PULSED AS JAGOOMBA DOES", ""}},
	{RUMBLE_HELD, {"0X08001000 HELD AT 2", ""}},
	{RUMBLE_PULSED_PORT_OFF, {"PULSED WITH 0X096A0000", "AT 0: GPIO PORT OFF"}},
	{RUMBLE_GPIO_PIN3, {"GPIO PIN 3 HELD ON", "AS DRILL DOZER DOES"}},
};
#define TRIES (sizeof(tries) / sizeof(*tries))
_Static_assert(TRIES <= RUMBLE_PROBE_TRIES, "report size");

// --- The report --------------------------------------------------------------

static void set_ram_page(uint16_t page)
{
	CART_REG(0x09C00000, page);
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
		CART_RAM[REPORT_OFFSET + i] = RUMBLE_PROBE_MAGIC[i];
	set_ram_page(0);
}

// Back to the kernel, as the in-game menu's RESET does: the ROM page to the
// kernel's, then a soft reset. It runs from IWRAM, as the ROM goes away.
__attribute__((section(".iwram"), long_call, noinline, noreturn, target("arm"))) static void back_to_kernel(void)
{
	REG_IME = 0;
	CART_REG(0x09880000, 0x8002);
	*(volatile uint8_t *)0x03007FFA = 0;  // the soft reset starts at 0x08000000
	__asm__ volatile("mov r0, #0xFC\n\tswi 0x010000\n\tswi 0x000000" ::: "r0", "r1", "r2", "r3", "memory");
	for (;;)
		;
}

int main(void)
{
	REG_WAITCNT = 0x4317;  // the cart waitstates commercial games use
	REG_DISPCNT = 3 | (1 << 10);
	uint16_t white = RGB(31, 31, 31), yellow = RGB(31, 31, 8);
	for (unsigned i = 0; i < TRIES; i++) {
		struct rumble_probe_try *t = &report.t[i];
		char title[] = "TRY 0 OF 0";
		title[4] = (char)('1' + i);
		title[9] = (char)('0' + TRIES);
		clear();
		text(8, 8, yellow, "TORII RUMBLE PROBE");
		text(8, 28, yellow, title);
		text(8, 44, white, tries[i].name[0]);
		text(8, 56, white, tries[i].name[1]);
		text(8, 80, white, "HOLD THE CONSOLE.");
		text(8, 92, white, "A: RUN THE MOTOR A SECOND");
		wait_key(KEY_A);
		t->way = tries[i].way;
		drive(t);
		report.tries = i + 1;
		text(8, 116, white, "DID IT RUMBLE:");
		text(8, 128, white, "A: YES  B: NO  SELECT: SKIP");
		uint16_t key = wait_key(KEY_A | KEY_B | KEY_SELECT);
		t->answer = (key & KEY_A) ? RUMBLE_ANSWER_YES : (key & KEY_B) ? RUMBLE_ANSWER_NO : RUMBLE_ANSWER_SKIP;
	}
	save_report();
	clear();
	text(8, 8, yellow, "TORII RUMBLE PROBE");
	text(8, 32, white, "A: BACK TO THE KERNEL,");
	text(8, 44, white, "WHICH LOGS THE ANSWERS.");
	wait_key(KEY_A);
	back_to_kernel();
}
