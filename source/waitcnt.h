// WAITCNT (0x04000204), the wait states of the cart bus. The BIOS leaves it
// at 0 (WS0 4/2, no prefetch), and games expect that when they start.
//
// The kernel menu runs at WAITCNT_MENU, WS0 3/1 with prefetch: the speed
// games run PSRAM at (Emerald's WAITCNT is 0x4317). At 2/1 (0x4018, v0.2 to
// v0.9.15) a real cart read PSRAM with bit 6 set at random, about once in
// 60 KB: a cheat file written from PSRAM came out with digits flipped in
// both copies, at other places in each (docs/research/omega-de-hardware.md). Everything never measured at that speed keeps the BIOS
// setting: the cart's control registers (the 0x9FE0000 unlock sequence),
// transfers through the SD buffer, NOR erases and writes, the clock's serial
// lines, and the delay() loops they time themselves with, which run from
// ROM. Those save WAITCNT, set WAITCNT_BIOS and put it back, inline (no call
// into ROM: some of them run while the kernel's NOR is busy). Launching, with
// its PSRAM writes, runs at WAITCNT_BIOS from the game's menu on, and the
// game starts with it (SetRompageWithHardReset).
#ifndef TORII_WAITCNT_H
#define TORII_WAITCNT_H

#include <gba_base.h>

#define REG_WAITCNT_CART (*(vu16 *)0x04000204)
#define WAITCNT_BIOS 0x0000
#define WAITCNT_MENU 0x4014  // SRAM 4, WS0 3/1, prefetch on

// u16 saved = WAITCNT_SLOW(); ... WAITCNT_RESTORE(saved);
#define WAITCNT_SLOW() \
	({ \
		u16 waitcnt_old = REG_WAITCNT_CART; \
		REG_WAITCNT_CART = WAITCNT_BIOS; \
		waitcnt_old; \
	})
#define WAITCNT_RESTORE(saved) (REG_WAITCNT_CART = (saved))

#endif
