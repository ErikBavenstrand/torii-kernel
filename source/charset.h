// Shows file names in the menu font, which has ASCII glyphs only.
#ifndef TORII_CHARSET_H
#define TORII_CHARSET_H

#include <gba_base.h>

// The ASCII character to draw for byte c of a name. FatFs hands names over
// in code page 850 (FF_CODE_PAGE): its accented Latin letters lose their
// accents, other symbols become '?'. FatFs hands over a name with characters
// code page 850 lacks as its 8.3 short name on FAT, so it still opens; exFAT
// has no short names, so there it is "?" and doesn't open.
char charset_ascii(u8 c);

#endif
