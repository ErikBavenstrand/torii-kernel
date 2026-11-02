// Script fonts: the glyphs a language pack brings for characters beyond
// Latin-1 (tools/lang.py's `font`, tools/font.py's format). The pack holds one
// per menu font, a blob the loader (lang_load.c) puts in PSRAM
// (PSRAM_LANG_FONT_BASE); the menu fonts then draw those characters instead
// of the replacement box. A game launch fills PSRAM, so script_drop runs
// first and what the launch draws after that shows the box again.
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
// No script fonts: every character past Latin-1 draws the replacement box
void script_drop(void);

// 1 while script fonts are in use (script_use, script_drop)
extern u8 script_active;
const struct torii_script *script_find(const struct torii_font *font);
// font's script glyphs, or NULL if none are in use (font is font_body or
// font_heading; any other font has none). With none in use, a byte read.
static inline const struct torii_script *script_of(const struct torii_font *font)
{
	return script_active ? script_find(font) : NULL;
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
