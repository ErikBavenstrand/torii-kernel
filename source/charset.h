// Shows file names in the menu fonts: the mode 3 font has ASCII glyphs only,
// the tile fonts (engine/font.h) Latin-1.
#ifndef TORII_CHARSET_H
#define TORII_CHARSET_H

#include <gba_base.h>

// Reads the UTF-8 character at s[*pos], one of len bytes, moves *pos past
// it, and returns the ASCII glyph to draw for it: Latin letters lose their
// accents, a few dashes and quotes become their ASCII forms, and anything
// else is '?'. Returns 0, draw nothing, for a continuation byte with no lead
// byte, such as where a scrolled name starts mid-character.
char charset_next(const char *s, u32 len, u32 *pos);

#define CHARSET_NONE 0xFFFFFFFFu  // draw nothing
#define CHARSET_REPLACEMENT 0xFFFDu

// Like charset_next, for a Latin-1 font: returns the code point to draw,
// U+0000-U+00FF as it is. Latin Extended-A letters lose their accents,
// dashes and curly quotes become ASCII, and anything else, or a broken
// sequence, is CHARSET_REPLACEMENT. CHARSET_NONE for a stray continuation
// byte.
u32 charset_next_latin1(const char *s, u32 len, u32 *pos);

#endif
