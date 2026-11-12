// Script fonts: glyphs for characters beyond Latin-1, in tools/font.py's
// format. The kernel has its own in ROM, font_body_extra (font_extra.c, made
// by `just font` from Galmuri9: extended Latin, Greek, Cyrillic, punctuation
// and symbols), which the menu fonts draw whenever no pack's font is in use;
// the heading font borrows the body's. A language pack can bring a blob of its
// own (tools/lang.py's `font`), which the loader (lang_load.c) puts in PSRAM
// (PSRAM_LANG_FONT_BASE) and which then replaces the kernel's: a character it
// lacks shows the box or folds to Latin-1. A game launch fills PSRAM, so
// script_drop runs first and goes back to the kernel's glyphs.
//
// A script glyph has an index from TORII_FONT_SCRIPT (font.h) on, in the
// lines text_layout makes and the blitter draws. Its table entry is a
// struct torii_glyph whose `offset` counts 4-byte units into the font's bits
// (so 256 KB of bitmaps are in reach), `top` is in the menu font's own line,
// and rows follow the same 1 or 2 byte rule as the built-in glyphs.
//
// The blob (little-endian; every offset a multiple of 4):
//   0   "TSCF"
//   4   u16 version (1), u16 fonts (1 or 2)
//   8   u32 CRC32 of the bytes from 12 to its end
//   12  u32 the blob's bytes
//   16  u32 offset of the body font's record, u32 the heading font's (0: none,
//       the heading font then draws the body font's glyphs, dropped by the
//       difference of the fonts' baselines)
// A font record:
//   0   u16 ranges, u16 glyphs
//   4   u32 offset of the glyph table, u32 offset of the bitmaps (from the record)
//   12  ranges: u16 first, u16 last, u16 index of `first`'s glyph in the table;
//       sorted, apart, from U+0100 up
//   then the glyph table (6 bytes a glyph) and the bitmaps
#ifndef TORII_SCRIPT_H
#define TORII_SCRIPT_H

#include <gba_base.h>
#include <stddef.h>

#include "font.h"

#define SCRIPT_HEADER 24
// The PSRAM a blob has: PSRAM_LANG_FONT_BASE to the window's end (320 KB)
#define SCRIPT_MAX 0x50000u

// One menu font's script glyphs, all pointers into the blob
struct torii_script {
	const struct torii_glyph *glyphs;
	const u8 *bits;
	const u8 *ranges;  // each range 3 halfwords
	u32 range_count;
	u32 glyph_count;
	s32 dy;  // rows added to a glyph's `top` (the heading font borrowing the body's)
};

// Checks a blob of bytes at blob (4-aligned, in PSRAM or anywhere readable)
// and uses it for font_body and font_heading from now on; 0 if it is not one
// this kernel takes (nothing changes then)
int script_use(const u8 *blob, u32 bytes);
// What script_use found in the blob's header: its CRC32 and size, for a
// loader that checks the bytes as it copies them (bytes 12 on)
u32 script_blob_crc(const u8 *header);
u32 script_blob_size(const u8 *header);
// The blob's header says it is one (a loader reads this much first)
int script_header_ok(const u8 *header);
// Drops a pack's script font: the kernel's own glyphs (font_body_extra) from now on
void script_drop(void);

// The kernel's own script glyphs, in ROM (font_extra.c)
extern const u32 font_body_extra[];
extern const u32 font_body_extra_bytes;

// 1 while a pack's script font is in use (script_use, script_drop)
extern u8 script_active;
// The script glyphs in use for font_body and font_heading (NULL until the
// first script_find reads the kernel's own)
extern const struct torii_script *script_body, *script_heading;
// font's script glyphs: the pack's, else the kernel's own; NULL for a font
// other than font_body and font_heading
const struct torii_script *script_find(const struct torii_font *font);
// script_find, a compare and a load once the kernel's glyphs are read (every layout asks)
static inline const struct torii_script *script_of(const struct torii_font *font)
{
	if (font == &font_body && script_body)
		return script_body;
	if (font == &font_heading && script_heading)
		return script_heading;
	return script_find(font);
}

// The glyph index for code point cp (U+0100 and up) in script sc, or -1 if it has none
int script_glyph(const struct torii_script *sc, u32 cp);

// Glyph g's entry in font, for any g a layout gives (an index in glyphs[], or a script glyph's)
const struct torii_glyph *torii_font_script_glyph(const struct torii_font *font, u32 g);
static inline const struct torii_glyph *torii_font_glyph(const struct torii_font *font, u32 g)
{
	return g < TORII_FONT_SCRIPT ? &font->glyphs[g] : torii_font_script_glyph(font, g);
}

#endif
