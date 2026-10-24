// The built-in emulators (source/emulators.s): Jagoomba Color for GB and
// GBC, PocketNES for NES. Sizes are multiples of 4, and each emulator looks
// for its game right after its last byte.
#ifndef TORII_EMULATORS_H
#define TORII_EMULATORS_H

#include <gba_types.h>

extern const u8 jagoombacolor_gba[];
extern const u32 jagoombacolor_gba_size;
extern const u8 pocketnes_gba[];
extern const u32 pocketnes_gba_size;

#endif
