// The hardware side of input.c: kept apart so tests/host builds the rest.
#include "input.h"

#include <gba_input.h>
#include <gba_interrupt.h>

// Every key held at a VBlank since the last poll, set by the IRQ handler
// (source/crash.s; bits above INPUT_ALL are noise): a write that blocks the
// menu for frames (the settings' NOR write, a save copy step) would drop a
// tap made during it, as the keys are read once a frame
volatile u16 input_keys_seen;

void input_poll(InputState *s, u32 dt)
{
	u16 ime = REG_IME;
	REG_IME = 0;
	u16 seen = input_keys_seen;
	input_keys_seen = 0;
	REG_IME = ime;
	input_step(s, (u16)((~REG_KEYINPUT | seen) & INPUT_ALL), dt);
}
