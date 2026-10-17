// Shows file names in the menu font, which has ASCII glyphs only.
#ifndef TORII_CHARSET_H
#define TORII_CHARSET_H

#include <gba_base.h>

// Reads the UTF-8 character at s[*pos], one of len bytes, moves *pos past
// it, and returns the ASCII glyph to draw for it: Latin letters lose their
// accents, a few dashes and quotes become their ASCII forms, and anything
// else is '?'. Returns 0, draw nothing, for a continuation byte with no lead
// byte, such as where a scrolled name starts mid-character.
char charset_next(const char *s, u32 len, u32 *pos);

#endif
