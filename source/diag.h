// Diagnostics: tests and probes for a real cart, run from SETTINGS (DIAGNOSE)
// or by holding DIAG_BOOT_KEYS at power-on. It logs at Debug level for its
// own run and writes its report to the card through the log.
#ifndef TORII_DIAG_H
#define TORII_DIAG_H

#include <gba_base.h>
#include <gba_input.h>

// L + R + SELECT, held while the cart powers on
#define DIAG_BOOT_KEYS (KEY_L | KEY_R | KEY_SELECT)

// Runs Diagnostics, then restarts the kernel.
void Diagnostics(void) __attribute__((noreturn));

// IWRAM code must run from IWRAM while the ROM page or wait states change, so
// it is never static: GCC would inline it into callers in ROM.
void IWRAM_CODE diag_timer_start(void);
u32 IWRAM_CODE diag_timer_stop(void);
u32 IWRAM_CODE diag_read_timed(const vu32 *src, u32 words, u16 waitcnt, u32 *sum);
void IWRAM_CODE diag_eeprom_read(u32 block, u32 addr_bits);
void IWRAM_CODE diag_loader_read(u32 offset, u16 *dst, u32 halfwords);

#endif
