// Torii test ROM: crashes on purpose, so tests can check the emulator's crash
// report. main calls into a named chain of functions; the last one jumps to
// cart SRAM, which can't run code.
#include <stdint.h>

#define REG_DISPCNT (*(volatile uint16_t *)0x04000000)

__attribute__((noinline)) static void jump_to_sram(void)
{
	((void (*)(void))0x0E000001)();
}

__attribute__((noinline)) void crash_here(int depth)
{
	if (depth > 0)
		crash_here(depth - 1);
	else
		jump_to_sram();
	REG_DISPCNT = depth;  // keeps the recursion from becoming a tail call
}

int main(void)
{
	REG_DISPCNT = 0;
	for (volatile int i = 0; i < 100000; i++)
		;
	crash_here(2);
	for (;;)
		;
}
