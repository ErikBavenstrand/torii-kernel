// The kernel's printf (source/fmt.c): what a typical log line costs
#include <stdio.h>

#include <gba_base.h>

#include "../../source/engine/cycles.h"
#include "../../source/tlog.h"
#include "tests.h"
#include "torii_test.h"

void test_fmt_cycles(void)
{
	static char out[200] EWRAM_BSS;
	u32 start = cycles_now();
	for (u32 i = 0; i < 100; i++)
		snprintf(out, sizeof(out), "save: loaded %s into cart SRAM, %lu bytes, type %02X at %08lX",
			 "/torii/saves/GBA/Some Game (Europe).gba/Main.sav", (unsigned long)131072 + i, 0x31u,
			 (unsigned long)0x08001234 + i);
	u32 c = cycles_since(start) / 100;
	tlog("fmt: %lu cycles a log line", (unsigned long)c);
	CHECK(c < 40000);
	start = cycles_now();
	for (u32 i = 0; i < 100; i++)
		snprintf(out, 12, "%lu MB", 4ul << (i % 4));
	tlog("fmt: %lu cycles a short one", (unsigned long)(cycles_since(start) / 100));
	start = cycles_now();
	for (u32 i = 0; i < 100; i++)
		snprintf(out, 64, "%s %s %lu%s.gba", "Golden", "Sun", (unsigned long)i, i & 1 ? " (USA)" : "");
	tlog("fmt: %lu cycles a row name", (unsigned long)(cycles_since(start) / 100));  // newlib took 54,000, this about 25,000
}
