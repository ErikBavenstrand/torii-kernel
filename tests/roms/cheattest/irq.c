// Torii test ROM for when the cheats run (tests/cheat_safety.sh irq): an IRQ
// handler that serves one interrupt an entry, HBlank before VBlank, as the
// AGB SDK's intr_main and pokeemerald's IntrMain do; built with VBLANK_ONLY,
// one that enables the VBlank IRQ alone. A cheat adds 1 to the halfword at
// 0x0203F000 once a frame, and its VBlank service counts the frames in which
// it didn't move by exactly 1. Every 60 frames the main loop logs
// "cheatirq: frame NNNN bad B hblank H" (H the HBlank IRQs served). A
// cheat set that costs more than a scanline used to keep the HBlank IRQ
// always pending after it, so VBlank was never served: no more lines.
#include <stdint.h>

#define REG_DISPCNT (*(volatile uint16_t *)0x04000000)
#define REG_DISPSTAT (*(volatile uint16_t *)0x04000004)
#define REG_IE (*(volatile uint16_t *)0x04000200)
#define REG_IF (*(volatile uint16_t *)0x04000202)
#define REG_IME (*(volatile uint16_t *)0x04000208)
#define IRQ_HANDLER (*(void (*volatile *)(void))0x03007FFC)
#define BIOS_IF (*(volatile uint16_t *)0x03007FF8)
#define REG_DEBUG_ENABLE (*(volatile uint16_t *)0x04FFF780)
#define REG_DEBUG_FLAGS (*(volatile uint16_t *)0x04FFF700)
#define REG_DEBUG_STRING ((volatile char *)0x04FFF600)
#define COUNTER (*(volatile uint16_t *)0x0203F000)

#define IRQ_VBLANK 1
#define IRQ_HBLANK 2

static volatile uint32_t frames, hblanks, bad;
static uint16_t last;

__attribute__((target("arm"))) static void on_irq(void)
{
	uint16_t pending = REG_IE & REG_IF;
	if (pending & IRQ_HBLANK) {
		REG_IF = IRQ_HBLANK;
		++hblanks;
		return;
	}
	if (pending & IRQ_VBLANK) {
		REG_IF = IRQ_VBLANK;
		BIOS_IF |= IRQ_VBLANK;
		uint16_t now = COUNTER;
		if (frames >= 2 && (uint16_t)(now - last) != 1)
			++bad;
		last = now;
		++frames;
	}
}

static void debug_line(const char *text)
{
	int i = 0;
	for (; text[i] && i < 250; i++)
		REG_DEBUG_STRING[i] = text[i];
	REG_DEBUG_STRING[i] = 0;
	REG_DEBUG_FLAGS = 3 | 0x100;  // info, send
}

static char *put_number(char *at, uint32_t n, int digits)
{
	for (int d = digits - 1; d >= 0; d--) {
		at[d] = (char)('0' + n % 10);
		n /= 10;
	}
	return at + digits;
}

int main(void)
{
	REG_DEBUG_ENABLE = 0xC0DE;
	REG_DISPCNT = 0;
	IRQ_HANDLER = on_irq;
#ifdef VBLANK_ONLY
	REG_DISPSTAT = 1 << 3;
	REG_IE = IRQ_VBLANK;
#else
	REG_DISPSTAT = 1 << 3 | 1 << 4;  // VBlank and HBlank IRQs
	REG_IE = IRQ_VBLANK | IRQ_HBLANK;
#endif
	REG_IME = 1;
	debug_line("cheatirq: start");
	for (uint32_t logged = 0;;) {
		uint32_t n = frames;
		if (n >= logged + 60) {
			logged = n - n % 60;
			char line[64] = "cheatirq: frame ";
			char *p = put_number(line + 16, logged, 4);
			const char *s = " bad ";
			while (*s)
				*p++ = *s++;
			p = put_number(p, bad, 4);
			s = " hblank ";
			while (*s)
				*p++ = *s++;
			p = put_number(p, hblanks, 6);
			*p = 0;
			debug_line(line);
		}
	}
}
