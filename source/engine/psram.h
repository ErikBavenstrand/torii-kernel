// PSRAM for assets, through the 0x08800000 window under the kernel's ROM page
// (docs/research/omega-de-hardware.md). The window shows one 8 MB page of
// the 32 MB PSRAM, chosen with SetPSRampage.
//
// Assets use page 0, the page the kernel keeps in the window whenever the
// menu runs (every other PSRAM user puts it back to 0), so nothing has to
// switch pages back. psram_begin() selects it once per batch of writes all
// the same, in case something left another page selected. Within it the
// cover cache (engine/cover.h) uses COVER_PSRAM_BYTES from
// PSRAM_ASSET_BASE; the 512 KB from PSRAM_RESERVED_BASE (the SD themes'
// until v0.4) are reserved; the first 4 MB stay free for other menu data
// (a name index, T9).
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
#define PSRAM_ASSET_BASE (PSRAM_WINDOW + 0x00400000u)
// Reserved, 512 KB past the cover cache's start
#define PSRAM_RESERVED_BASE (PSRAM_ASSET_BASE + 0x00080000u)

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
