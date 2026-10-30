// Torii test ROM for the in-game cheat search (tests/search.sh,
// docs/design/v0.9/cheat-search.md): a game with lives to find. It starts
// with 5; each press of A loses one (5 again after 0). The lives show as
// white squares on a blue screen, and every 16 frames the ROM logs
// "searchtest: lives N" (mGBA's debug output), read after the VBlank in
// which the cheats ran: a cheat that holds the lives shows here at once.
#include <stdint.h>

#define REG_DISPCNT (*(volatile uint16_t *)0x04000000)
#define REG_DISPSTAT (*(volatile uint16_t *)0x04000004)
#define REG_KEYINPUT (*(volatile uint16_t *)0x04000130)
#define REG_IE (*(volatile uint16_t *)0x04000200)
#define REG_IF (*(volatile uint16_t *)0x04000202)
#define REG_IME (*(volatile uint16_t *)0x04000208)
#define IRQ_HANDLER (*(void (*volatile *)(void))0x03007FFC)
#define BIOS_IF (*(volatile uint16_t *)0x03007FF8)
#define REG_DEBUG_ENABLE (*(volatile uint16_t *)0x04FFF780)
#define REG_DEBUG_FLAGS (*(volatile uint16_t *)0x04FFF700)
#define REG_DEBUG_STRING ((volatile char *)0x04FFF600)
#define VRAM ((volatile uint16_t *)0x06000000)
#define RGB(r, g, b) ((r) | ((g) << 5) | ((b) << 10))
#define KEY_A 1

static volatile unsigned frames;
static volatile uint8_t lives;

__attribute__((target("arm"))) static void on_irq(void)
{
	REG_IF = 1;
	BIOS_IF |= 1;
	++frames;
}

static void log_lives(unsigned n)
{
	static const char text[] = "searchtest: lives ";
	REG_DEBUG_ENABLE = 0xC0DE;
	if (REG_DEBUG_ENABLE != 0x1DEA)
		return;
	unsigned i = 0;
	for (; text[i]; i++)
		REG_DEBUG_STRING[i] = text[i];
	if (n >= 100)
		REG_DEBUG_STRING[i++] = (char)('0' + n / 100);
	if (n >= 10)
		REG_DEBUG_STRING[i++] = (char)('0' + n / 10 % 10);
	REG_DEBUG_STRING[i++] = (char)('0' + n % 10);
	REG_DEBUG_STRING[i] = 0;
	REG_DEBUG_FLAGS = 4 | 0x100;
}

static void draw_lives(unsigned n)
{
	for (unsigned k = 0; k < 10; k++) {
		uint16_t c = k < n ? RGB(31, 31, 31) : RGB(2, 6, 20);
		for (int y = 60; y < 76; y++)
			for (int x = 0; x < 16; x++)
				VRAM[y * 240 + 24 + k * 20 + x] = c;
	}
}

int main(void)
{
	REG_DISPCNT = 3 | (1 << 10);  // mode 3, BG2
	for (int i = 0; i < 240 * 160; i++)
		VRAM[i] = RGB(2, 6, 20);
	lives = 5;
	IRQ_HANDLER = on_irq;
	REG_DISPSTAT = 1 << 3;
	REG_IE = 1;
	REG_IME = 1;
	unsigned held = 0;
	for (;;) {
		for (unsigned last = frames; frames == last;)
			;
		unsigned n = lives;
		draw_lives(n);
		if (frames % 16 == 0)
			log_lives(n);
		unsigned keys = ~REG_KEYINPUT & 0x3FF;
		if ((keys & KEY_A) && !(held & KEY_A))
			lives = n ? n - 1 : 5;
		held = keys;
	}
}
