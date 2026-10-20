// See nameindex.h.
#include "nameindex.h"

#include <string.h>

#include "savename.h"
#include "tlog.h"

_Static_assert(NAMES_BASE + NAMES_BYTES <= PSRAM_USERDATA_BASE, "the names reach the user's lists");

static u32 names_top EWRAM_BSS;

void names_reset(void)
{
	names_top = 0;
	psram_begin();  // once per batch of writes
}

u32 names_add(const char *name)
{
	// A halfword-aligned copy in RAM, as psram_write takes
	static u16 copy[(GAME_NAME_MAX + 2) / 2] EWRAM_BSS;
	u32 len = strnlen(name, GAME_NAME_MAX + 1);
	if (len > GAME_NAME_MAX)
		return NAMES_NONE;
	u32 bytes = (len + 2) & ~1u;  // the NUL, rounded up to a halfword
	if (names_top + bytes > NAMES_BYTES)
		return NAMES_NONE;
	memcpy(copy, name, len + 1);
	if (!psram_write((void *)(NAMES_BASE + names_top), copy, bytes)) {
		tlog_error("names: %s", "the PSRAM write was refused");
		return NAMES_NONE;
	}
	u32 h = names_top;
	names_top += bytes;
	return h;
}

u32 names_used(void)
{
	return names_top;
}
