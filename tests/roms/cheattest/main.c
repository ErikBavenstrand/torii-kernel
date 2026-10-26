// Torii test ROM for cheats (tests/cheat_engine.sh): the same cheat codes run
// on it in mGBA's own cheat engine and through Torii, and the memory they
// change has to match.
//
// Each frame, once the VBlank interrupt has come (and the cheats with it: mGBA
// runs them as the frame ends, Torii in the VBlank interrupt, both before the
// main loop wakes), the ROM calls frame_hook (a master code's hook address
// for mGBA), copies every watched region into the snapshot at 0x02020000 on
// a checkpoint frame (every 8th), logs "cheattest: checkpoint NNNN" (the
// frame number, 4 digits), then rewrites every watched region from the frame
// number and the keys, so a cheat's effect shows in the next snapshot only
// if the cheat ran again since. The snapshot is what the test compares: it
// holds still for 8 frames, so it reads the same however late in the frame
// each emulator ran the cheats. Nothing here depends on anything but the
// frame number and the keys held. The layout (tools/cheat_cases.py has it
// too):
//
//   0x02030000  0x100  writes: halfword j = (n & 0xFF) << 8 | j
//   0x02030100         counters, from the phase p = n >> 3: u8 p, u16 p *
//                      0x123, u32 p * 0x1234567, s16 and s8 (p & 15) - 8,
//                      u16 1 << (p & 15), u16 KEYINPUT, u32 0x7FFFFFF8 + p
//   0x02030200  0x100  conditionals' targets: pattern(n, 3, j)
//   0x02030300  0x100  add/or/and targets: pattern(n, 5, j)
//   0x02030400         pointers: u32 to 0x02030480 + (p & 3) * 16, u32 to
//                      0x03004080 + (p & 1) * 8; targets at 0x02030480,
//                      0x40 bytes: pattern(n, 7, j)
//   0x02030500  0x100  fills and slides: pattern(n, 9, j)
//   0x02030600         ROM results: u32 get_magic(), u32 get_magic2(), then
//                      at 0x02030610 rom_table and 0x02030620 rom_table2 (16
//                      bytes each) copied
//   0x03004000  0x100  IWRAM: pattern(n, 11, j)
//   0x05000000  0x20   palette: halfword j = (n * 0x21 + j * 0x111) & 0x7FFF
//   0x06000000  0x20   VRAM (BG tiles; no BG shows): halfword j = n << 8 | j * 3
//   0x07000000  0x20   OAM: halfword j = n * 5 + j
//   0x0400000C  4      BG2CNT, BG3CNT: halfword j = n + j * 0x101
//   0x04000048  4      WININ, WINOUT: halfword j = (n + j) & 0x3F3F
//
// pattern(n, s, j) = (n * s + j * 7) & 0xFF, a byte. The snapshot holds, in
// order, 0x02030000-0x020306FF, IWRAM's 0x100, palette, VRAM, OAM, BG2CNT to
// WINOUT (8 bytes, 4 more 0), the frame number (u32) and KEYINPUT (u16).
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

#define EW ((volatile uint8_t *)0x02030000)
#define IW ((volatile uint8_t *)0x03004000)
#define PAL ((volatile uint16_t *)0x05000000)
#define VRAM ((volatile uint16_t *)0x06000000)
#define OAM ((volatile uint16_t *)0x07000000)
#define IO_BG ((volatile uint16_t *)0x0400000C)
#define IO_WIN ((volatile uint16_t *)0x04000048)
#define SNAP ((volatile uint8_t *)0x02020000)

#define U16(p) (*(volatile uint16_t *)(p))
#define U32(p) (*(volatile uint32_t *)(p))

// hook.s: Thumb routines a ROM patch changes, and the hook
uint32_t get_magic(void);
uint32_t get_magic2(void);
void frame_hook(void);

// Known ROM data, read every frame; ROM patches change it. Its words
// avoid 0x03007FFC, which the kernel patches in a game's ROM.
const uint8_t rom_table[16] __attribute__((aligned(4))) = {0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87,
							   0x98, 0xA9, 0xBA, 0xCB, 0xDC, 0xED, 0xFE, 0x0F};
const uint8_t rom_table2[16] __attribute__((aligned(4))) = {0xF0, 0xE1, 0xD2, 0xC3, 0xB4, 0xA5, 0x96, 0x87,
							    0x78, 0x69, 0x5A, 0x4B, 0x3C, 0x2D, 0x1E, 0x0F};

static volatile uint32_t frames;

__attribute__((target("arm"))) static void on_irq(void)
{
	REG_IF = 1;
	BIOS_IF |= 1;
	++frames;
}

static void debug_line(const char *text)
{
	int i = 0;
	for (; text[i] && i < 250; i++)
		REG_DEBUG_STRING[i] = text[i];
	REG_DEBUG_STRING[i] = 0;
	REG_DEBUG_FLAGS = 3 | 0x100;  // info, send
}

static void log_checkpoint(uint32_t n)
{
	char line[] = "cheattest: checkpoint 0000";
	for (int d = 0; d < 4; d++) {
		line[25 - d] = (char)('0' + n % 10);
		n /= 10;
	}
	debug_line(line);
}

static uint8_t pattern(uint32_t n, uint32_t seed, uint32_t j)
{
	return (uint8_t)(n * seed + j * 7);
}

static void fill(volatile uint8_t *at, uint32_t len, uint32_t n, uint32_t seed)
{
	for (uint32_t j = 0; j < len; j += 4)
		U32(at + j) = pattern(n, seed, j) | (uint32_t)pattern(n, seed, j + 1) << 8 |
			      (uint32_t)pattern(n, seed, j + 2) << 16 | (uint32_t)pattern(n, seed, j + 3) << 24;
}

static void copy(volatile uint8_t *to, const volatile void *from, uint32_t len)
{
	for (uint32_t j = 0; j < len; j += 2)
		U16(to + j) = U16((const volatile uint8_t *)from + j);
}

static void snapshot(uint32_t n)
{
	copy(SNAP, EW, 0x700);
	copy(SNAP + 0x700, IW, 0x100);
	copy(SNAP + 0x800, PAL, 0x20);
	copy(SNAP + 0x820, VRAM, 0x20);
	copy(SNAP + 0x840, OAM, 0x20);
	copy(SNAP + 0x860, IO_BG, 4);
	copy(SNAP + 0x864, IO_WIN, 4);
	U32(SNAP + 0x868) = 0;
	U32(SNAP + 0x86C) = 0;
	U32(SNAP + 0x870) = n;
	U16(SNAP + 0x874) = REG_KEYINPUT;
	U16(SNAP + 0x876) = 0;
}

static void rewrite(uint32_t n)
{
	uint32_t p = n >> 3;
	for (uint32_t j = 0; j < 0x80; j++)
		U16(EW + j * 2) = (uint16_t)((n & 0xFF) << 8 | j);
	EW[0x100] = (uint8_t)p;
	EW[0x101] = 0;
	U16(EW + 0x102) = (uint16_t)(p * 0x123);
	U32(EW + 0x104) = p * 0x1234567;
	U16(EW + 0x108) = (uint16_t)((int)(p & 15) - 8);
	EW[0x10A] = (uint8_t)((int)(p & 15) - 8);
	EW[0x10B] = 0;
	U16(EW + 0x10C) = (uint16_t)(1u << (p & 15));
	U16(EW + 0x10E) = REG_KEYINPUT;
	U32(EW + 0x110) = 0x7FFFFFF8 + p;
	for (uint32_t j = 0x114; j < 0x200; j += 4)
		U32(EW + j) = 0;
	fill(EW + 0x200, 0x100, n, 3);
	fill(EW + 0x300, 0x100, n, 5);
	U32(EW + 0x400) = 0x02030480 + (p & 3) * 16;
	U32(EW + 0x404) = 0x03004080 + (p & 1) * 8;
	for (uint32_t j = 0x408; j < 0x480; j += 4)
		U32(EW + j) = 0;
	fill(EW + 0x480, 0x40, n, 7);
	for (uint32_t j = 0x4C0; j < 0x500; j += 4)
		U32(EW + j) = 0;
	fill(EW + 0x500, 0x100, n, 9);
	U32(EW + 0x600) = get_magic();
	U32(EW + 0x604) = get_magic2();
	U32(EW + 0x608) = 0;
	U32(EW + 0x60C) = 0;
	copy(EW + 0x610, rom_table, 16);
	copy(EW + 0x620, rom_table2, 16);
	for (uint32_t j = 0x630; j < 0x700; j += 4)
		U32(EW + j) = 0;
	fill(IW, 0x100, n, 11);
	for (uint32_t j = 0; j < 16; j++) {
		PAL[j] = (uint16_t)((n * 0x21 + j * 0x111) & 0x7FFF);
		VRAM[j] = (uint16_t)(n << 8 | j * 3);
		OAM[j] = (uint16_t)(n * 5 + j);
	}
	IO_BG[0] = (uint16_t)n;
	IO_BG[1] = (uint16_t)(n + 0x101);
	IO_WIN[0] = (uint16_t)(n & 0x3F3F);
	IO_WIN[1] = (uint16_t)((n + 1) & 0x3F3F);
}

int main(void)
{
	REG_DEBUG_ENABLE = 0xC0DE;
	REG_DISPCNT = 0;  // mode 0, nothing shown: the ROM shows no picture
	rewrite(0);
	snapshot(0);
	IRQ_HANDLER = on_irq;
	REG_DISPSTAT = 1 << 3;  // VBlank interrupt
	REG_IE = 1;
	REG_IME = 1;
	debug_line("cheattest: start");
	for (;;) {
		// Poll rather than VBlankIntrWait, as many games do
		for (uint32_t last = frames; frames == last;)
			;
		uint32_t n = frames;
		frame_hook();
		if (n % 8 == 0) {
			snapshot(n);
			log_checkpoint(n);
		}
		rewrite(n);
		if (frames != n)
			debug_line("cheattest: frame overrun");
	}
}
