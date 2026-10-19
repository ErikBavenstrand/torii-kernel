// Reads file names for the menu fonts (engine/font.h), which cover Latin-1.
#ifndef TORII_CHARSET_H
#define TORII_CHARSET_H

#include <gba_base.h>

#define CHARSET_NONE 0xFFFFFFFFu  // draw nothing
#define CHARSET_REPLACEMENT 0xFFFDu

// Reads the UTF-8 character at s[*pos], one of len bytes, moves *pos past
// it, and returns the code point to draw: U+0000-U+00FF as it is. Latin
// Extended-A letters lose their accents, dashes and curly quotes become
// ASCII, and anything else, or a broken sequence, is CHARSET_REPLACEMENT.
// CHARSET_NONE for a stray continuation byte, such as where a cut name
// starts mid-character.
u32 charset_next_latin1(const char *s, u32 len, u32 *pos);
// How many bytes of the NUL-terminated s to keep for at most max: all of it
// if it fits, else max or fewer, cut where a character starts.
u32 charset_cut(const char *s, u32 max);

#endif
