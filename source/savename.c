#include "savename.h"

#include <string.h>

void save_name(char *out, const char *game, u32 is_emu)
{
	u32 len = strnlen(game, SAVE_NAME_MAX - 2);
	u32 letters = is_emu == 2 ? 2 : 3;
	const char *ext = is_emu ? "esv" : "sav";

	memcpy(out, game, len);
	if (len < letters)  // no room for an extension to replace: add one
		letters = 0;
	memcpy(out + len - letters, ext, 4);
}
