// The pages' states share one block of EWRAM: the game lists' (games.c),
// the SD card's (sdcard.c), Info's (info.c) and the NOR page's (nor.c).
// One page is up at a time, and none of their jobs or callbacks outlives
// its view (jobs end with their owner view, box art polls from the games
// page's poll), so each state lives only while its page is: the 7 KB the
// other three took is free for v0.9's language work.
//
// A page's enter claims the block first. Claimed by another page since, it
// comes back zeroed, as at boot; claimed by the same page (back from an
// action or a launch that didn't start), it is as the page left it. What a
// page keeps across other pages (the SD card page's cursor per folder, the
// favourites' A-Z cache key) stays in statics of its own.
#ifndef TORII_SCREENS_PAGE_STATE_H
#define TORII_SCREENS_PAGE_STATE_H

#include <gba_base.h>

#define PAGE_STATE_BYTES 0xF10

enum page_state_owner {
	PAGE_STATE_NONE,
	PAGE_STATE_GAMES,
	PAGE_STATE_SD,
	PAGE_STATE_INFO,
	PAGE_STATE_NOR,
};

extern u32 page_state[PAGE_STATE_BYTES / 4];

// The block for owner, zeroed if another page had it since
void page_state_claim(u32 owner);

#endif
