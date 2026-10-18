// Log lines as evidence for Diagnostics' checklist (see logcheck.h). The
// texts are the kernel's own launch lines (source/ezkernelnew.c), the boot
// report's (source/cartlog.c) and Diagnostics' last line.
#include <string.h>

#include "logcheck.h"

u32 logcheck_line(u32 seen, const char *line)
{
	if (strstr(line, "diag: done"))
		return 0;
	if (strstr(line, "debug info from the in-game menu"))
		return seen | SEEN_DEBUG;
	if (strstr(line, "eeprom probe: "))
		return seen | SEEN_EEPROM_PROBE;
	if (!strstr(line, "launch: loading "))
		return seen;
	if (strstr(line, " with Jagoomba Color"))
		return seen | SEEN_GB;
	if (strstr(line, " with PocketNES"))
		return seen | SEEN_NES;
	if (strstr(line, " from NOR at "))
		return seen | SEEN_GBA | SEEN_NOR;
	if (strstr(line, ", CLEAN BOOT"))
		return seen | SEEN_GBA;
	// "BOOT WITH ADDON: reset R, savestate S, sleep L, cheats C (N)",
	// after the file name.
	const char *addon = strstr(line, ", BOOT WITH ADDON: ");
	if (!addon)
		return seen;
	seen |= SEEN_GBA;
	if (strstr(addon, " savestate 1,"))
		seen |= SEEN_SAVESTATE | SEEN_MENU;
	if (strstr(addon, " reset 1,"))
		seen |= SEEN_MENU;
	if (strstr(addon, " sleep 1,"))
		seen |= SEEN_SLEEP;
	return seen;
}
