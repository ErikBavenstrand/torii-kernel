// Diagnostics' checks of what the log and the EEPROM probe show
// (source/logcheck.c, source/eepromprobe.c).
#include <string.h>

#include "../../source/eepromprobe.h"
#include "../../source/logcheck.h"
#include "../kernel/torii_test.h"
#include "tests.h"

static u32 seen_after(const char *const *lines)
{
	struct logcheck c = {0};
	for (; *lines; lines++)
		logcheck_line(&c, *lines);
	return c.seen;
}

#define SEEN(...) seen_after((const char *const[]){__VA_ARGS__, NULL})
#define STARTED "   9.00 I launch: starting the game"

void test_logcheck_launches(void)
{
	CHECK_EQ(SEEN("   1.00 I launch: loading Hello.gba, CLEAN BOOT", STARTED), SEEN_GBA);
	CHECK_EQ(SEEN("   2.00 I launch: loading Zelda.gbc with Jagoomba Color", STARTED,
		      "   3.00 I launch: loading Mario.nes with PocketNES", STARTED),
		 SEEN_GB | SEEN_NES);
	CHECK_EQ(SEEN("   4.00 I launch: loading Wars.gba from NOR at 00000000: code AWRE, 1 bytes, save type 11, "
		      "patched 1, savestates 1",
		      STARTED),
		 SEEN_GBA | SEEN_NOR | SEEN_SAVESTATE | SEEN_MENU);
	CHECK_EQ(SEEN("   4.00 I launch: loading Wars.gba from NOR at 00000000: code AWRE, 1 bytes, save type 11, "
		      "patched 0, savestates 0",
		      STARTED),
		 SEEN_GBA | SEEN_NOR);
	// The settings come after the name, which can't fool them.
	CHECK_EQ(SEEN("   5.00 I launch: loading reset 1, sleep 1, with PocketNES.gba, BOOT WITH ADDON: reset 0, "
		      "savestate 1, sleep 0, cheats 0 (0)",
		      STARTED),
		 SEEN_GBA | SEEN_MENU | SEEN_SAVESTATE);
	CHECK_EQ(SEEN("   5.00 I launch: loading A.gba, BOOT WITH ADDON: reset 1, savestate 0, sleep 1, cheats 0 (0)",
		      STARTED),
		 SEEN_GBA | SEEN_MENU | SEEN_SLEEP);
	CHECK_EQ(SEEN("   5.00 I launch: loading A.gba, BOOT WITH ADDON: reset 0, savestate 0, sleep 0, cheats 1 (3)",
		      STARTED),
		 SEEN_GBA);
	CHECK_EQ(SEEN("   5.00 I launch: loading x with Jagoomba Color.gba, CLEAN BOOT", STARTED), SEEN_GBA);
	// A launch that never reached the game proves nothing, nor do other lines.
	CHECK_EQ(SEEN("   6.00 I launch: loading A.gba, BOOT WITH ADDON: reset 0, savestate 1, sleep 1, cheats 0 (0)",
		      "   6.10 E launch: error 6", "   7.00 I launch: loading B.gba, CLEAN BOOT"),
		 0);
	CHECK_EQ(SEEN("   6.00 I launch: loading Mario.nes with PocketNES",
		      "   7.00 I launch: loading B.gba, CLEAN BOOT", STARTED),
		 SEEN_GBA);
	CHECK_EQ(SEEN("   6.00 I launch Hello.gba: code OMRS, 1234 bytes, save mode 11", STARTED), 0);
	CHECK_EQ(SEEN("   6.00 E previous session: a launch never reached the game; its last line was: "
		      "   5.00 I launch: loading Mario.nes with PocketNES",
		      STARTED),
		 0);
}

void test_logcheck_reports(void)
{
	u32 s = SEEN("  1.00 I debug info from the in-game menu: HELLO, code OMRS, version 0",
		     "  1.00 I eeprom probe: a 6-bit run, 9 steps");
	CHECK_EQ(s, SEEN_DEBUG | SEEN_EEPROM_PROBE);
	// The end of a Diagnostics run starts over; its start doesn't, nor a
	// file name that quotes it.
	CHECK_EQ(SEEN("  1.00 I eeprom probe: a 6-bit run", "  2.00 I diag: Diagnostics started, logging at debug"),
		 SEEN_EEPROM_PROBE);
	CHECK_EQ(SEEN("  1.00 I eeprom probe: a 6-bit run", "  2.00 I diag: done"), 0);
	CHECK_EQ(SEEN("  1.00 I eeprom probe: a 6-bit run", "  2.00 I launch: loading diag: done.gba, CLEAN BOOT"),
		 SEEN_EEPROM_PROBE);
	CHECK_EQ(SEEN("  1.00 I launch eeprom probe: .gba: code -"), 0);
	CHECK_EQ(SEEN("torii log 00000001     "), 0);
}

static void add(struct eeprom_probe *p, int op, unsigned block, const u8 *data)
{
	struct eeprom_probe_step *st = &p->step[p->steps++];
	st->op = (u8)op;
	st->width = (u8)p->width;
	st->block = (u16)block;
	if (data)
		memcpy(st->data, data, 8);
	else
		eeprom_probe_pattern(st->data, p->width, block, p->nonce);
}

void test_eeprom_probe_patterns(void)
{
	u8 a[8], b[8], c[8];
	eeprom_probe_pattern(a, 6, 1, 7);
	eeprom_probe_pattern(b, 14, 1, 7);
	eeprom_probe_pattern(c, 14, 65, 7);
	CHECK(memcmp(a, b, 8) != 0);
	CHECK(memcmp(b, c, 8) != 0);
	eeprom_probe_pattern(c, 14, 1, 8);  // another run
	CHECK(memcmp(b, c, 8) != 0);
	static const u8 ones[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, zeros[8];
	for (unsigned block = 0; block < 1024; block++) {
		eeprom_probe_pattern(a, 14, block, 0xFFFF);
		CHECK(memcmp(a, ones, 8) != 0 && memcmp(a, zeros, 8) != 0);
	}
}

void test_eeprom_probe_verdict(void)
{
	static const u8 ones[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
	u8 w65[8], w1023[8];
	struct eeprom_probe p;

	// An 8 KB chip: every block its own.
	memset(&p, 0, sizeof(p));
	p.width = 14;
	add(&p, EEPROM_PROBE_READ, 1, ones);
	add(&p, EEPROM_PROBE_WRITE, 1, NULL);
	add(&p, EEPROM_PROBE_READ, 1, NULL);
	add(&p, EEPROM_PROBE_WRITE, 65, NULL);
	add(&p, EEPROM_PROBE_READ, 1, p.step[1].data);
	add(&p, EEPROM_PROBE_WRITE, 1023, NULL);
	add(&p, EEPROM_PROBE_READ, 1023, NULL);
	CHECK_EQ(eeprom_probe_verdict(&p), EEPROM_PROBE_WORKS | EEPROM_PROBE_8K);
	CHECK_EQ(eeprom_probe_source(&p, 0), -1);
	CHECK_EQ(eeprom_probe_source(&p, 4), 1);
	CHECK_EQ(eeprom_probe_source(&p, 6), 5);
	CHECK_EQ(eeprom_probe_source(&p, 5), -1);  // a write

	// A 512-byte chip: block 65 lands on block 1, 1023 on 63.
	memset(&p, 0, sizeof(p));
	p.width = 14;
	eeprom_probe_pattern(w65, 14, 65, 0);
	eeprom_probe_pattern(w1023, 14, 1023, 0);
	add(&p, EEPROM_PROBE_WRITE, 1, NULL);
	add(&p, EEPROM_PROBE_READ, 1, NULL);
	add(&p, EEPROM_PROBE_WRITE, 65, NULL);
	add(&p, EEPROM_PROBE_READ, 1, w65);
	add(&p, EEPROM_PROBE_WRITE, 1023, NULL);
	add(&p, EEPROM_PROBE_READ, 1023, NULL);
	add(&p, EEPROM_PROBE_READ, 63, w1023);
	CHECK_EQ(eeprom_probe_verdict(&p), EEPROM_PROBE_WORKS | EEPROM_PROBE_WRAPS);

	// A chip that doesn't answer, and a write that never finishes.
	memset(&p, 0, sizeof(p));
	p.width = 6;
	add(&p, EEPROM_PROBE_WRITE, 1, NULL);
	p.step[0].polls = EEPROM_PROBE_TIMEOUT;
	add(&p, EEPROM_PROBE_READ, 1, ones);
	CHECK_EQ(eeprom_probe_verdict(&p), EEPROM_PROBE_TIMED_OUT);

	// A chip that ignores writes, holding an earlier run's data: that run's
	// nonce differs, so it doesn't pass for this one's.
	u8 earlier[8];
	memset(&p, 0, sizeof(p));
	p.width = 6;
	p.nonce = 5;
	eeprom_probe_pattern(earlier, 6, 1, 4);
	add(&p, EEPROM_PROBE_READ, 1, earlier);
	add(&p, EEPROM_PROBE_WRITE, 1, NULL);
	add(&p, EEPROM_PROBE_READ, 1, earlier);
	CHECK_EQ(eeprom_probe_verdict(&p), 0);

	// A step count past the table is cut to it.
	p.steps = 1000;
	CHECK_EQ(eeprom_probe_source(&p, EEPROM_PROBE_STEPS), -1);
	(void)eeprom_probe_verdict(&p);
}
