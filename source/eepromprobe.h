// The EEPROM probe's report. The probe ROM (tests/roms/eeprom) runs in game
// mode with the save type the menu's SAVE TYPE picked, writes and reads
// EEPROM blocks with one address width (6 or 14 bits), and leaves what it
// saw in cart RAM page 0xF0 at CARTLOG_EEPROM_PROBE. The kernel's next boot
// logs it (source/cartlog.c). The ROM and the kernel share this header and
// eepromprobe.c, which need nothing from the hardware.
#ifndef TORII_EEPROMPROBE_H
#define TORII_EEPROMPROBE_H

#include <stdint.h>

#define EEPROM_PROBE_MAGIC "TORII-EEPROMPRB1"  // 16 bytes, written last
#define EEPROM_PROBE_STEPS 30
#define EEPROM_PROBE_TIMEOUT 0xFFFFFFFFu

enum {
	EEPROM_PROBE_READ = 'R',
	EEPROM_PROBE_WRITE = 'W',
};

struct eeprom_probe_step {
	uint8_t op;  // EEPROM_PROBE_READ or EEPROM_PROBE_WRITE
	uint8_t width;  // address bits sent: 6 or 14
	uint16_t block;
	uint32_t polls;  // a write: polls until the chip was ready, or EEPROM_PROBE_TIMEOUT
	uint8_t data[8];  // what was written, or read back
};

struct eeprom_probe {
	char magic[16];
	uint32_t width;
	uint32_t steps;
	uint32_t spare[2];
	struct eeprom_probe_step step[EEPROM_PROBE_STEPS];
};

// The bytes a width-bit run writes to block: different for every block and
// width, and never all 0x00 or 0xFF.
void eeprom_probe_pattern(uint8_t out[8], uint32_t width, uint32_t block);

// For read step i, the latest write step before it whose data it returned,
// or -1 if none.
int eeprom_probe_source(const struct eeprom_probe *p, uint32_t i);

enum {
	EEPROM_PROBE_WORKS = 1,  // the first block written read back as written
	EEPROM_PROBE_WRAPS = 2,  // a read returned a block 64 blocks away: 512 bytes
	EEPROM_PROBE_8K = 4,  // block 1023 read back as written, and nothing wrapped
	EEPROM_PROBE_TIMED_OUT = 8,  // a write never finished
};

// What the steps show, as EEPROM_PROBE_* flags.
uint32_t eeprom_probe_verdict(const struct eeprom_probe *p);

#endif
