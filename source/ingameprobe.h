// The in-game probe's report (v0.8 P0, docs/design/v0.8/ingame-menu.md).
// The probe ROM (tests/roms/ingame) runs in game mode, where the in-game
// menu runs, and tries what the menu may want there but no one has proven on
// the real cart: cart SRAM pages 0x20-0xEF, the RAM page register's
// readback, a flash 128K save's bank across page changes, a PSRAM write, SD
// reads and writes, the clock through 0x096A0000, and SRAM's wait states.
// It shows every result on its screen (to photograph), and leaves them in
// cart RAM page 0xF0 at CARTLOG_INGAME_PROBE and in its own save at
// INGAME_PROBE_SAVE_OFFSET; the kernel's next boot logs them
// (source/cartlog.c). Page 0xF0 has no battery and keeps only through the
// soft reset back to the kernel; the save is battery-backed FRAM, so the
// report outlives a power cycle (on the real cart, v0.8's first run, the
// kernel's boot said "microSD card error" after the probe, and the power-off
// that took lost page 0xF0). The SD test goes last, only when A asks for it,
// after the rest is saved. Before launching the probe the kernel makes a
// scratch file, /torii/probe/sd.bin, and puts its first sector here for the
// SD write test (ingame_probe_prepare, source/ingame.c).
#ifndef TORII_INGAMEPROBE_H
#define TORII_INGAMEPROBE_H

#include <stdint.h>

#define INGAME_PROBE_MAGIC "TORII-INGAMEPRB1"  // 16 bytes, written last
#define INGAME_PROBE_CODE "TIGP"  // the probe ROM's game code
#define INGAME_PROBE_FILE "/torii/probe/sd.bin"
#define INGAME_PROBE_SECTOR 512
#define INGAME_PROBE_FIRST_PAGE 0x20
#define INGAME_PROBE_LAST_PAGE 0xEF
// The report's copy in the probe's save (cart SRAM page 0, FRAM): 4 KB in,
// a flash save's second sector, clear of the bank test's offset 0
#define INGAME_PROBE_SAVE_OFFSET 0x1000
// scratch_check's value for a scratch_lba the kernel put there for this
// launch: anything else (a launch that didn't prepare one) means no sector
#define INGAME_PROBE_CHECK(lba) ((lba) ^ 0x7E57C0DEu)

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
	INGAME_PROBE_SD_PENDING = 7,  // not run yet: the report was saved before it, and nothing after
	INGAME_PROBE_SD_SKIPPED = 8,  // B skipped it
};

// Where the probe put the report's copy in its save
enum {
	INGAME_PROBE_SAVED_NOT = 0,  // neither way read back
	INGAME_PROBE_SAVED_DIRECT = 1,  // byte writes, as SRAM takes them
	INGAME_PROBE_SAVED_FLASH = 2,  // flash commands: sector erase, then byte programs
};

struct ingame_probe {
	char magic[16];
	// In, from the kernel
	uint32_t scratch_lba;  // the scratch file's first sector, 0: none
	uint32_t scratch_check;  // INGAME_PROBE_CHECK(scratch_lba)
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
	uint8_t saved;  // INGAME_PROBE_SAVED_*: how the copy in the save went (the one before this, in a copy)
	// PSRAM at 0x08010000 (past the probe's code): before, after writing
	// 0xA55A, after writing the first value back
	uint16_t psram_before, psram_written, psram_after;
	// The clock's date and time (7 bytes, BCD) with 0x096A0000 at 1, then at 0
	uint8_t rtc_on[8];
	uint8_t rtc_off[8];
	// How the run ended, for a report read from the save after a power cycle:
	// 1 once A started the soft reset back to the kernel (in the save's copy
	// only), and how many boots since found the card wouldn't mount
	uint8_t returning;
	uint8_t mount_failed;
	uint8_t spare[2];
};

#endif
