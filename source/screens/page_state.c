#include <string.h>

#include "page_state.h"

u32 page_state[PAGE_STATE_BYTES / 4] EWRAM_BSS;
static u32 page_state_owner EWRAM_BSS;
// Each page's name for the block (its own struct type there)
__asm__(".global page_games\n.set page_games, page_state\n"
	".global page_sd\n.set page_sd, page_state\n"
	".global page_info\n.set page_info, page_state\n"
	".global page_nor\n.set page_nor, page_state\n");

void page_state_claim(u32 owner)
{
	if (page_state_owner == owner)
		return;
	memset(page_state, 0, sizeof(page_state));
	page_state_owner = owner;
}
