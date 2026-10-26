// The in-game probe's report (v0.8 P0, docs/design/v0.8/ingame-menu.md).
// The probe ROM (tests/roms/ingame) runs in game mode, where the in-game
// menu runs, and tries what the menu may want there but no one has proven on
// the real cart: cart SRAM pages 0x20-0xEF, the RAM page register's
// readback, a flash 128K save's bank across page changes, a PSRAM write, SD
// reads and writes, the clock through 0x096A0000, and SRAM's wait states.
// It leaves what it saw in cart RAM page 0xF0 at CARTLOG_INGAME_PROBE; the
// kernel's next boot logs it (source/cartlog.c). Before launching the probe
// the kernel makes a scratch file, /torii/probe/sd.bin, and puts its first
// sector here for the SD write test (ingame_probe_prepare, source/ingame.c).
#ifndef TORII_INGAMEPROBE_H
#define TORII_INGAMEPROBE_H

#include <stdint.h>

#define INGAME_PROBE_MAGIC "TORII-INGAMEPRB1"  // 16 bytes, written last
#define INGAME_PROBE_CODE "TIGP"  // the probe ROM's game code
#define INGAME_PROBE_FILE "/torii/probe/sd.bin"
#define INGAME_PROBE_SECTOR 512
#define INGAME_PROBE_FIRST_PAGE 0x20
#define INGAME_PROBE_LAST_PAGE 0xEF

// The scratch sector's bytes as the kernel writes them, and the probe's
static inline uint8_t ingame_probe_byte(uint32_t i, uint32_t seed)
{
	return (uint8_t)((i * 7 + seed) ^ (i >> 3));
}
#define INGAME_PROBE_SEED_KERNEL 0x5A
#define INGAME_PROBE_SEED_PROBE 0xC3

// What the SD write test saw
enum {
	INGAME_PROBE_SD_UNTRIED = 0,  // no scratch sector given
	INGAME_PROBE_SD_OK = 1,  // read the kernel's bytes, wrote and read back the probe's, put the kernel's back
	INGAME_PROBE_SD_READ_FAILED = 2,  // a read didn't answer
	INGAME_PROBE_SD_NOT_KERNELS = 3,  // the sector didn't hold the kernel's bytes
	INGAME_PROBE_SD_WRITE_FAILED = 4,  // a write didn't answer
	INGAME_PROBE_SD_WRITE_LOST = 5,  // the probe's bytes didn't read back
	INGAME_PROBE_SD_RESTORE_LOST = 6,  // the kernel's bytes didn't read back after
};

struct ingame_probe {
	char magic[16];
	// In, from the kernel
	uint32_t scratch_lba;  // the scratch file's first sector, 0: none
	// Cart SRAM pages 0x20-0xEF: 4 bytes written and read back at each 4 KB page's start
	uint16_t pages_tried;
	uint16_t pages_ok;
	uint8_t first_bad_page;  // 0 if none
	uint8_t sram_waits_ok;  // bit n: WAITCNT's SRAM setting n (4, 3, 2, 8 cycles) read back 256 bytes right
	uint16_t page_readback[2];  // 0x09C00000 read after writing 0x20, then 0x55
	// A flash 128K save's bank (meaningful with that save type and a save
	// whose banks differ): offset 0's byte in bank 0, in bank 1, and in bank 1
	// after the page register went to 0x20 and back to 0
	uint8_t bank0, bank1, bank1_after;
	uint8_t sd_mbr;  // 1: sector 0 read with the 0x55AA signature
	uint8_t sd_scratch;  // INGAME_PROBE_SD_*
	uint8_t spare;
	// PSRAM at 0x08010000 (past the probe's code): before, after writing
	// 0xA55A, after writing the first value back
	uint16_t psram_before, psram_written, psram_after;
	// The clock's date and time (7 bytes, BCD) with 0x096A0000 at 1, then at 0
	uint8_t rtc_on[8];
	uint8_t rtc_off[8];
};

#endif
