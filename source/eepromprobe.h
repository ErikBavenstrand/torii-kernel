// The EEPROM probe's report. The probe ROM (tests/roms/eeprom) runs in game
// mode with the save type the menu's SAVE TYPE picked, writes and reads
// EEPROM blocks with one address width (6 or 14 bits), and leaves what it
// saw in cart RAM page 0xF0 at CARTLOG_EEPROM_PROBE. The kernel's next boot
// logs it (source/cartlog.c). The ROM and the kernel share this header and
// eepromprobe.c, which need nothing from the hardware.
#ifndef TORII_EEPROMPROBE_H
#define TORII_EEPROMPROBE_H

#include <stdint.h>

#define EEPROM_PROBE_MAGIC "TORII-EEPROMPRB2"  // 16 bytes, written last
#define EEPROM_PROBE_STEPS 24
#define EEPROM_PROBE_TIMEOUT 0xFFFFFFFFu

enum {
	EEPROM_PROBE_READ = 'R',
	EEPROM_PROBE_WRITE = 'W',
	EEPROM_PROBE_WAIT = 'F',  // the probe waited a frame before the reads after it
};

// How a write waited for the chip. On the real cart, writes polled at once
// lost their last byte (docs/research/omega-de-hardware.md), so the
// probe tries each way on its own block.
enum {
	EEPROM_PROBE_POLL_AT_ONCE = 1,  // polled right after the stop bit, as v0.2.1's probe did
	EEPROM_PROBE_WAIT_THEN_POLL = 2,  // waited a scanline, polled until ready, then about 1 ms
	EEPROM_PROBE_NO_POLL = 3,  // never polled: waited 10 ms
	EEPROM_PROBE_EXTRA_BIT = 4,  // one more 0 bit after the stop bit, then as WAIT_THEN_POLL
};

struct eeprom_probe_step {
	uint8_t op;  // EEPROM_PROBE_READ, EEPROM_PROBE_WRITE or EEPROM_PROBE_WAIT
	uint8_t width;  // address bits sent: 6 or 14
	uint16_t block;
	uint32_t polls;  // a write: polls until the chip was ready, or EEPROM_PROBE_TIMEOUT
	uint8_t data[8];  // what was written, or read back
	uint8_t variant;  // a write: EEPROM_PROBE_POLL_AT_ONCE...
	uint8_t spare[3];
};

struct eeprom_probe {
	char magic[16];
	uint32_t width;
	uint32_t steps;
	uint32_t nonce;  // this run's, in its patterns
	uint32_t spare;
	struct eeprom_probe_step step[EEPROM_PROBE_STEPS];
};

// The bytes a width-bit run writes to block: different for every block,
// width and run (nonce), and never all 0x00 or 0xFF. The nonce keeps an
// earlier run's data, still in the save, from passing for this run's.
void eeprom_probe_pattern(uint8_t out[8], uint32_t width, uint32_t block, uint32_t nonce);

// For read step i, the latest write step before it whose data it returned,
// or -1 if none. A read that matches a write in bytes 0-6 (which hold the
// block and the run's nonce) but not byte 7 counts too, and sets *near if
// near isn't NULL: the cart lost the write's last byte.
int eeprom_probe_source(const struct eeprom_probe *p, uint32_t i, int *near);

enum {
	EEPROM_PROBE_WORKS = 1,  // the first block written read back as written
	EEPROM_PROBE_WRAPS = 2,  // a read returned a block 64 blocks away: 512 bytes
	EEPROM_PROBE_8K = 4,  // block 1023 read back (byte 7 aside), and nothing wrapped
	EEPROM_PROBE_TIMED_OUT = 8,  // a write never finished
	EEPROM_PROBE_LAST_BYTE_LOST = 16,  // a read matched a write but for byte 7
};

// What the steps show, as EEPROM_PROBE_* flags.
uint32_t eeprom_probe_verdict(const struct eeprom_probe *p);

#endif
