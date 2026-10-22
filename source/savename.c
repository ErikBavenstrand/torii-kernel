#include "savename.h"

#include <string.h>

void name_with_ext(char *out, const char *game, const char *ext)
{
	u32 len = strnlen(game, GAME_NAME_MAX);
	if (len < 3) {  // no room for an extension to replace: add one, if it fits
		memcpy(out, game, len);
		memcpy(out + len, ext, 4);
		return;
	}
	memcpy(out, game, len - 3);
	memcpy(out + len - 3, ext, 3);
	out[len] = 0;
}
