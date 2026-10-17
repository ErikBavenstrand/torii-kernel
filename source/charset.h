// Shows file names in the menu font, which has ASCII glyphs only.
#ifndef TORII_CHARSET_H
#define TORII_CHARSET_H

#include <gba_base.h>

// The ASCII character to draw for byte c of a name. FatFs hands names over
// in code page 850 (FF_CODE_PAGE): its accented Latin letters lose their
// accents, other symbols become '?'. Names with characters code page 850
// lacks come from FatFs as their 8.3 short name, so they still open.
char charset_ascii(u8 c);

#endif
