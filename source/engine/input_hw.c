// The hardware side of input.c: kept apart so tests/host builds the rest.
#include "input.h"

#include <gba_input.h>

void input_poll(InputState *s, u32 dt)
{
	input_step(s, (u16)(~REG_KEYINPUT & INPUT_ALL), dt);
}
