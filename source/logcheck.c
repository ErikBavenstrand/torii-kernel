// Log lines as evidence for Diagnostics' checklist (see logcheck.h). The
// texts are the kernel's own launch lines (source/ezkernelnew.c), the boot
// report's (source/cartlog.c) and Diagnostics' last line.
#include <string.h>

#include "logcheck.h"

// The text after a line's time and level ("   12.34 I "), or NULL.
static const char *body(const char *line)
{
	while (*line == ' ')
		line++;
	while ((*line >= '0' && *line <= '9') || *line == '.')
		line++;
	if (line[0] != ' ' || !line[1] || line[2] != ' ')
		return NULL;
	return line + 3;
}

static int starts(const char *text, const char *prefix)
{
	return !strncmp(text, prefix, strlen(prefix));
}

static int ends(const char *text, const char *suffix)
{
	size_t n = strlen(text), k = strlen(suffix);
	return n >= k && !strcmp(text + n - k, suffix);
}

// The last place needle is in text, or NULL: after the file name, which a
// launch line quotes first.
static const char *last(const char *text, const char *needle)
{
	const char *found = NULL;
	for (const char *p = strstr(text, needle); p; p = strstr(p + 1, needle))
		found = p;
	return found;
}

// What a "launch: loading NAME..." line says the launch uses.
static u32 launch_flags(const char *line)
{
	if (ends(line, " with Jagoomba Color"))
		return SEEN_GB;
	if (ends(line, " with PocketNES"))
		return SEEN_NES;
	if (ends(line, ", CLEAN BOOT"))
		return SEEN_GBA;
	// "from NOR at ...: ..., patched P, savestates S".
	const char *nor = last(line, " from NOR at ");
	if (nor)
		return SEEN_GBA | SEEN_NOR | (ends(nor, ", savestates 1") ? SEEN_SAVESTATE | SEEN_MENU : 0);
	// "BOOT WITH ADDON: reset R, savestate S, sleep L, cheats C (N)".
	const char *addon = last(line, ", BOOT WITH ADDON: ");
	if (!addon)
		return 0;
	u32 seen = SEEN_GBA;
	if (strstr(addon, " savestate 1,"))
		seen |= SEEN_SAVESTATE | SEEN_MENU;
	if (strstr(addon, " reset 1,"))
		seen |= SEEN_MENU;
	if (strstr(addon, " sleep 1,"))
		seen |= SEEN_SLEEP;
	return seen;
}

void logcheck_line(struct logcheck *c, const char *line)
{
	const char *text = body(line);
	if (!text)
		return;
	if (!strcmp(text, "diag: done")) {
		c->seen = c->pending = 0;
	} else if (starts(text, "debug info from the in-game menu: ")) {
		c->seen |= SEEN_DEBUG;
	} else if (starts(text, "eeprom probe: ")) {
		c->seen |= SEEN_EEPROM_PROBE;
	} else if (starts(text, "launch: loading ")) {
		c->pending = launch_flags(text);  // a new launch: the last one failed
	} else if (!strcmp(text, "launch: starting the game")) {
		c->seen |= c->pending;
		c->pending = 0;
	}
}
