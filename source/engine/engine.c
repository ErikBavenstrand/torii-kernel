// See engine.h.
#include "engine.h"

void engine_init(void)
{
	gfx_init();
	res_init();
	jobs_init();
	frame_init();
}
