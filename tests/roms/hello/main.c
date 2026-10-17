// Torii test ROM: proves a game booted from the cart and that SRAM
// saves persist. Draws one bar per boot (stored in SRAM) on a blue screen.
// A VBlank interrupt moves a marker along the bottom edge, so the kernel's
// in-game menu, which hooks that interrupt, can be tested (tests/menu.sh).
#include <stdint.h>

#define REG_DISPCNT (*(volatile uint16_t *)0x04000000)
#define REG_DISPSTAT (*(volatile uint16_t *)0x04000004)
#define REG_IE (*(volatile uint16_t *)0x04000200)
#define REG_IF (*(volatile uint16_t *)0x04000202)
#define REG_IME (*(volatile uint16_t *)0x04000208)
// Where games store their IRQ handler, and the flags the BIOS's
// VBlankIntrWait checks; the kernel looks for this address in the ROM.
#define IRQ_HANDLER (*(void (*volatile *)(void))0x03007FFC)
#define BIOS_IF (*(volatile uint16_t *)0x03007FF8)
#define VRAM ((volatile uint16_t *)0x06000000)
#define SRAM ((volatile uint8_t *)0x0E000000)
#define RGB(r, g, b) ((r) | ((g) << 5) | ((b) << 10))

// Lets save-type detectors (including the kernel's) classify this as SRAM.
const char save_tag[] __attribute__((used)) = "SRAM_V113";

static const uint8_t MAGIC[4] = {'O', 'M', 'R', 'X'};

static volatile unsigned frames;

// The BIOS calls this in ARM mode for every interrupt.
__attribute__((target("arm"))) static void on_irq(void)
{
	REG_IF = 1;
	BIOS_IF |= 1;
	++frames;
}

int main(void)
{
	unsigned boots = 0;
	int valid = 1;
	for (int i = 0; i < 4; i++)
		valid &= SRAM[i] == MAGIC[i];
	if (valid)
		boots = SRAM[4];
	boots = (boots + 1) & 0xFF;
	for (int i = 0; i < 4; i++)
		SRAM[i] = MAGIC[i];
	SRAM[4] = boots;

	REG_DISPCNT = 3 | (1 << 10);  // mode 3, BG2
	for (int i = 0; i < 240 * 160; i++)
		VRAM[i] = RGB(2, 6, 20);
	// White frame marks a successful boot.
	for (int x = 0; x < 240; x++) {
		VRAM[x] = VRAM[159 * 240 + x] = RGB(31, 31, 31);
	}
	for (int y = 0; y < 160; y++) {
		VRAM[y * 240] = VRAM[y * 240 + 239] = RGB(31, 31, 31);
	}
	// One yellow bar per boot, up to 20.
	for (unsigned b = 0; b < boots && b < 20; b++)
		for (int y = 40; y < 120; y++)
			for (int x = 0; x < 8; x++)
				VRAM[y * 240 + 12 + b * 11 + x] = RGB(31, 28, 4);
	IRQ_HANDLER = on_irq;
	REG_DISPSTAT = 1 << 3;  // VBlank interrupt
	REG_IE = 1;
	REG_IME = 1;
	for (;;) {
		__asm__ volatile("swi 0x05" ::: "r0", "r1", "r2", "r3", "memory");  // VBlankIntrWait
		unsigned x = 1 + frames % 238;
		VRAM[158 * 240 + x] = RGB(31, 0, 0);
		VRAM[158 * 240 + (x == 1 ? 238 : x - 1)] = RGB(2, 6, 20);
	}
}
