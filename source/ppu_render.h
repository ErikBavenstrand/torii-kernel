// A software copy of the GBA's picture unit, for the in-game menu's
// screenshots (v0.8 P4, docs/design/v0.8/ingame-menu.md): it draws a frame
// from a dump of VRAM, the palettes, OAM and the display registers, a line
// at a time, the way the hardware composes it. Hardware-free, so the host
// tests check it, against mGBA's own screens too (tests/ppu_render.sh).
//
// What it draws: modes 0-5; text BGs in 4 and 8 bits a pixel, every map
// size, flips and scroll; affine BGs (modes 1 and 2) and the bitmap modes
// through BG2's matrix, wrapping or not; sprites, plain and affine (double
// size too), 1D and 2D tile mapping, semi-transparent ones; priorities
// between BGs and sprites; alpha blending and the brightness fades.
// Left out: the windows (all layers show as DISPCNT says), mosaic, and
// anything a game changes in the middle of a frame (HBlank effects).
//
// The dump can't hold what the hardware won't read back: the BG scroll and
// affine registers, BLDY and the windows' bounds are write-only, so the
// overlay records them as 0. The renderer takes a scroll of 0 as it comes
// (a scrolled tile screen shifts), an all-zero BG matrix as the identity
// (the bitmap modes come out whole) and a BLDY of 0 as no fade.
#ifndef TORII_PPU_RENDER_H
#define TORII_PPU_RENDER_H

#include <gba_base.h>

#define PPU_W 240
#define PPU_H 160
#define PPU_VRAM_BYTES 0x18000
#define PPU_IO_HALVES 0x30  // 0x04000000-0x0400005F: the display's registers

struct ppu_frame {
	const u8 *vram;  // PPU_VRAM_BYTES
	const u16 *pal;  // 512 colours: the BG palette, then the OBJ palette
	const u16 *oam;  // 512 halfwords
	const u16 *io;  // PPU_IO_HALVES from 0x04000000 (DISPCNT first)
};

// A line's layers on their way (the caller's: 2.9 KB is a lot of stack)
struct ppu_obj_px {
	u16 colour;  // 0xFFFF: no sprite here
	u8 prio;
	u8 semi : 1;  // semi-transparent
	u8 win : 1;  // in the OBJ window
};
struct ppu_scratch {
	u16 bg[4][PPU_W];
	struct ppu_obj_px obj[PPU_W];
};

// Line y (0-159) of the frame into out, RGB15 (bit 15 clear)
void ppu_render_line(const struct ppu_frame *f, u32 y, u16 *out, struct ppu_scratch *s);

// A 24-bit BMP of PPU_W x PPU_H stored top-down (a negative height), as
// ppu_bmp_row writes them one after the other
#define PPU_BMP_HEAD 54
#define PPU_BMP_ROW (PPU_W * 3)
#define PPU_BMP_BYTES (PPU_BMP_HEAD + PPU_BMP_ROW * PPU_H)
void ppu_bmp_head(u8 *out);
// A line of RGB15 as the BMP's bytes (blue, green, red), 5 bits to 8 as
// mGBA shows them
void ppu_bmp_row(const u16 *line, u8 *out);

#endif
