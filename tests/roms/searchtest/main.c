// Torii test ROM for the in-game cheat search (tests/search.sh,
// docs/design/v0.9/cheat-search.md): a game with lives to find. It starts
// with 5; each press of A loses one (5 again after 0). The lives show as
// white squares on a blue screen, and every 16 frames the ROM logs
// "searchtest: lives N" (mGBA's debug output), read after the VBlank in
// which the cheats ran: a cheat that holds the lives shows here at once.
//
// And coins for the pointer scan: a block in EWRAM, which a pointer in
// IWRAM points to, holds them as BCD (150 as 0x0150) at offset 0x24.
// SELECT adds 25; B moves the block 0x100 bytes on (its old copy left
// behind), as games that shuffle their save in memory do. The log line
// also says "coins N" (the BCD's digits).
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
#define KEY_B 2
#define KEY_SELECT 4

static volatile unsigned frames;
static volatile uint8_t lives;

#define BLOCK_BASE 0x02010000u
#define COINS_AT 0x24  // the coins' offset in the block
static volatile uint8_t *volatile block;

__attribute__((target("arm"))) static void on_irq(void)
{
	REG_IF = 1;
	BIOS_IF |= 1;
	++frames;
}

static uint16_t coins(void)
{
	return (uint16_t)(block[COINS_AT] | block[COINS_AT + 1] << 8);
}

static void set_coins(uint16_t v)
{
	block[COINS_AT] = (uint8_t)v;
	block[COINS_AT + 1] = (uint8_t)(v >> 8);
}

// BCD a + b, four digits
static uint16_t bcd_add(uint16_t a, uint16_t b)
{
	uint16_t out = 0, carry = 0;
	for (unsigned sh = 0; sh < 16; sh += 4) {
		unsigned d = (a >> sh & 15) + (b >> sh & 15) + carry;
		carry = d > 9;
		out |= (uint16_t)((carry ? d - 10 : d) << sh);
	}
	return out;
}

static void log_lives(unsigned n)
{
	static const char text[] = "searchtest: lives ", text2[] = " coins ";
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
	for (unsigned k = 0; text2[k]; k++)
		REG_DEBUG_STRING[i++] = text2[k];
	uint16_t c = coins();
	for (int sh = 12; sh >= 0; sh -= 4)
		REG_DEBUG_STRING[i++] = (char)('0' + (c >> sh & 15));
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
	block = (volatile uint8_t *)BLOCK_BASE;
	set_coins(0x0150);
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
		if ((keys & KEY_SELECT) && !(held & KEY_SELECT))
			set_coins(bcd_add(coins(), 0x0025));
		if ((keys & KEY_B) && !(held & KEY_B)) {  // the block moves on, its old copy left
			volatile uint8_t *to = block + 0x100;
			for (unsigned k = 0; k < 0x40; k++)
				to[k] = block[k];
			block = to;
		}
		held = keys;
	}
}
