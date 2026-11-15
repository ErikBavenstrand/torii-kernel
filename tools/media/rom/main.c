// The showcase card's stand-in game (tools/media.py): a picture in mode 3,
// scene.bin (240x160 15-bit pixels, drawn by media.py), with a VBlank
// handler where games keep theirs, so the kernel's in-game menu opens over
// it. A save tag makes the kernel give it SRAM.
#include <stdint.h>

#define REG_DISPCNT (*(volatile uint16_t *)0x04000000)
#define REG_DISPSTAT (*(volatile uint16_t *)0x04000004)
#define REG_IE (*(volatile uint16_t *)0x04000200)
#define REG_IF (*(volatile uint16_t *)0x04000202)
#define REG_IME (*(volatile uint16_t *)0x04000208)
#define IRQ_HANDLER (*(void (*volatile *)(void))0x03007FFC)
#define BIOS_IF (*(volatile uint16_t *)0x03007FF8)
#define VRAM ((volatile uint16_t *)0x06000000)

const char save_tag[] __attribute__((used)) = "SRAM_V113";
extern const uint16_t scene[240 * 160];

static volatile unsigned frames;

__attribute__((target("arm"))) static void on_irq(void)
{
	REG_IF = 1;
	BIOS_IF |= 1;
	++frames;
}

int main(void)
{
	REG_DISPCNT = 3 | (1 << 10);
	for (int i = 0; i < 240 * 160; i++)
		VRAM[i] = scene[i];
	IRQ_HANDLER = on_irq;
	REG_DISPSTAT = 1 << 3;
	REG_IE = 1;
	REG_IME = 1;
	for (;;)
		for (unsigned last = frames; frames == last;)
			;
}
