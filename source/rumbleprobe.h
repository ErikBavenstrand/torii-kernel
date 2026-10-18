// The rumble probe's report. The probe ROM (tests/roms/rumble) runs in game
// mode, as Jagoomba does, drives the cart's rumble motor several ways, asks
// after each whether it rumbled, and leaves the answers in cart RAM page 0xF0
// at CARTLOG_RUMBLE_PROBE. The kernel's next boot logs them (source/cartlog.c).
#ifndef TORII_RUMBLEPROBE_H
#define TORII_RUMBLEPROBE_H

#include <stdint.h>

#define RUMBLE_PROBE_MAGIC "TORII-RUMBLEPRB1"  // 16 bytes, written last
#define RUMBLE_PROBE_TRIES 8

// How a try drove the motor; each runs about a second.
enum {
	RUMBLE_PULSED = 1,  // Jagoomba's sequence each frame: 0xF1 at 0x09E20000, 2 then 0 at 0x08001000
	RUMBLE_HELD = 2,  // 0xF1 at 0x09E20000, 2 at 0x08001000 for the second, then 0
	RUMBLE_PULSED_PORT_OFF = 3,  // as RUMBLE_PULSED with 0x096A0000 at 0
	RUMBLE_GPIO_PIN3 = 4,  // Drill Dozer's way: GPIO enable 1, direction 8, data 8 for the second, then 0
};

enum {
	RUMBLE_ANSWER_SKIP = 0,
	RUMBLE_ANSWER_YES = 1,
	RUMBLE_ANSWER_NO = 2,
};

struct rumble_probe_try {
	uint8_t way;  // RUMBLE_PULSED...
	uint8_t answer;  // RUMBLE_ANSWER_*
	uint16_t before;  // 0x08001000 read before the try; GPIO pin 3 leaves it be
	uint16_t on;  // read right after the first 2 written there
	uint16_t off;  // read after the last 0, before putting `before` back
};

struct rumble_probe {
	char magic[16];
	uint32_t tries;
	uint32_t spare;
	struct rumble_probe_try t[RUMBLE_PROBE_TRIES];
};

#endif
