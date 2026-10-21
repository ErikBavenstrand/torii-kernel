// PSRAM for assets, through the 0x08800000 window under the kernel's ROM page
// (docs/research/omega-de-hardware.md). The window shows one 8 MB page of
// the 32 MB PSRAM, chosen with SetPSRampage.
//
// Assets use page 0, the page the kernel keeps in the window whenever the
// menu runs (every other PSRAM user puts it back to 0), so nothing has to
// switch pages back. psram_begin() selects it once per batch of writes all
// the same, in case something left another page selected. The map of page 0
// while the menu runs (docs/plans/v0.4-decisions.md, PSRAM map):
//
//	0 MB      the SD card page's folder listing (sdlist.h), PSRAM_NAMES_BYTES
//	0.81 MB   the library walk's scratch: its save table, sort buffer and
//	          a copy of /torii/names.txt
//	1 MB      the library index: two slots of LIB_SLOT_BYTES (library.h),
//	          the one the menu reads and the one a walk builds, to 3.5 MB
//	3.5 MB    favourites and recents (B5), PSRAM_USERDATA_BASE
//	4 MB      the cover cache (engine/cover.h), COVER_PSRAM_BYTES (204 KB;
//	          cover.c asserts it ends below 4.5 MB)
//	4.5 MB    the Favourites page's A-Z order (screens/games.c), PSRAM_GAMES_BASE,
//	          16 KB; the rest reserved, PSRAM_RESERVED_BASE (the SD themes'
//	          until v0.4)
//	5 MB      /torii/names.db (namesdb.h), up to PSRAM_NAMES_DB_BYTES
//	6.75 MB   the CRC cache's table (namesdb.h), NAMES_CACHE_BYTES
//	7.5 MB    box art's lookups (boxart.c): the set of art files on the card
//	          and the paths of the covers wanted, PSRAM_ART_BYTES
//	7.625-8 MB reserved (v0.9 language tables)
//
// A game launch fills PSRAM with the game, so everything here is lost then:
// the launch calls cover_cache_invalidate first.
//
// Speed: reads run at the menu's WAITCNT (WS0 2/1, measured on the cart);
// writes at the BIOS's (WAITCNT_SLOW), since writes at the menu's speed
// aren't confirmed on the cart yet.
#ifndef TORII_ENGINE_PSRAM_H
#define TORII_ENGINE_PSRAM_H

#include <gba_base.h>

#define PSRAM_WINDOW 0x08800000u
#define PSRAM_WINDOW_BYTES 0x00800000u
#define PSRAM_ASSET_PAGE 0
#define PSRAM_NAMES_BYTES 0x000D0000u
#define PSRAM_LIBRARY_SCRATCH (PSRAM_WINDOW + PSRAM_NAMES_BYTES)
#define PSRAM_LIBRARY_SCRATCH_BYTES 0x00030000u
#define PSRAM_LIBRARY_BASE (PSRAM_WINDOW + 0x00100000u)
#define PSRAM_ASSET_BASE (PSRAM_WINDOW + 0x00400000u)
// Reserved, 512 KB past the cover cache's start
#define PSRAM_RESERVED_BASE (PSRAM_ASSET_BASE + 0x00080000u)
// The Favourites page's order: a word a favourite (screens/games.c)
#define PSRAM_GAMES_BASE PSRAM_RESERVED_BASE
#define PSRAM_GAMES_BYTES 0x4000u
// Recent and the favourites (userdata_store.c), up to the cover cache
#define PSRAM_USERDATA_BYTES 0x00080000u
#define PSRAM_USERDATA_BASE (PSRAM_ASSET_BASE - PSRAM_USERDATA_BYTES)
// The library's slots end where the user's lists start (library_scan.c asserts it)
#define PSRAM_LIBRARY_END PSRAM_USERDATA_BASE
// The names database and the CRC cache (names_store.c asserts the sizes)
#define PSRAM_NAMES_DB_BASE (PSRAM_WINDOW + 0x00500000u)
#define PSRAM_NAMES_DB_BYTES 0x001C0000u
#define PSRAM_NAMES_CACHE_BASE (PSRAM_NAMES_DB_BASE + PSRAM_NAMES_DB_BYTES)
#define PSRAM_NAMES_CACHE_BYTES 0x000C0000u
// Box art's lookups (boxart.c asserts its use fits)
#define PSRAM_ART_BASE (PSRAM_NAMES_CACHE_BASE + PSRAM_NAMES_CACHE_BYTES)
#define PSRAM_ART_BYTES 0x00020000u

// 1 if [p, p + bytes) lies in the PSRAM window.
static inline int psram_holds(const void *p, u32 bytes)
{
	u32 a = (u32)p;
	return a >= PSRAM_WINDOW && bytes <= PSRAM_WINDOW_BYTES && a - PSRAM_WINDOW <= PSRAM_WINDOW_BYTES - bytes;
}

// 1 if p is in cart space (ROM, PSRAM, NOR: 0x08000000 to 0x0DFFFFFF).
static inline int psram_cart_space(const void *p)
{
	return (u32)p - 0x08000000u < 0x06000000u;
}

// Selects the asset page (SetPSRampage, once per batch of writes).
void psram_begin(void);
// Copies bytes (rounded up to halfwords) from RAM to PSRAM at dst, at the
// slow WAITCNT, with DMA3 in 1 KB pieces so a VBlank IRQ waits at most a few
// lines. dst must be in the window, src in RAM (not cart space: on the cart
// a DMA from the kernel's ROM into PSRAM misses its target, probably as
// source and destination share the cart's bus; copy const data to RAM
// first), and both
// halfword-aligned; a bad call writes nothing and logs why (0), so a stray
// address never reaches the cart.
int psram_write(void *dst, const void *src, u32 bytes);

#endif
