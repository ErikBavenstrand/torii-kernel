// A larger cheat file's index (source/cheatindex.c): the cut into windows,
// and the index's lines written and read back.
#include <stdio.h>
#include <string.h>

#include "../../source/cheatindex.h"
#include "../kernel/torii_test.h"
#include "tests.h"

static int cut(struct cheatindex_cut *c, const char *line, u32 at)
{
	return cheatindex_cut_line(c, line, (u32)strlen(line), at);
}

void test_cheatindex_cut(void)
{
	struct cheatindex_cut c;
	char line[64];
	cheatindex_cut_begin(&c);
	CHECK_EQ(cut(&c, "# A comment", 0), CHEATINDEX_LINE);
	CHECK_EQ(cut(&c, "cheats = 600", 12), CHEATINDEX_LINE);
	// CHEATINDEX_CHEATS cheats a window: the next cheat's first line starts one
	u32 at = 100, cuts = 0;
	for (u32 n = 0; n < 600; n++) {
		snprintf(line, sizeof(line), "cheat%lu_desc = \"x\"", (unsigned long)n);
		int r = cut(&c, line, at);
		CHECK(r != CHEATINDEX_BAD);
		if (r == CHEATINDEX_NEW) {
			cuts++;
			CHECK_EQ(n % CHEATINDEX_CHEATS, 0);
			CHECK_EQ(c.at, at);
		}
		at += 20;
		snprintf(line, sizeof(line), "cheat%lu_code = \"32001000 0001\"", (unsigned long)n);
		CHECK_EQ(cut(&c, line, at), CHEATINDEX_LINE);
		at += 30;
	}
	CHECK_EQ(cuts, 2);
	CHECK_EQ(c.window, 2);
	CHECK_EQ(c.count, 100);
	CHECK_EQ(c.first, 500);
	// A pick's enable line written at its window's end: the window's
	CHECK_EQ(cut(&c, "cheat510_enable = true", at), CHEATINDEX_LINE);
	CHECK_EQ(c.picks, 1);
	// An earlier window's cheat: its lines would be apart
	CHECK_EQ(cut(&c, "cheat3_enable = true", at), CHEATINDEX_BAD);
	// Bytes cut too, and numbers far apart
	cheatindex_cut_begin(&c);
	CHECK_EQ(cut(&c, "cheat0_code = \"x\"", 0), CHEATINDEX_LINE);
	CHECK_EQ(cut(&c, "cheat1_code = \"x\"", CHEATINDEX_BYTES), CHEATINDEX_NEW);
	CHECK_EQ(cut(&c, "cheat2000_code = \"x\"", CHEATINDEX_BYTES + 20), CHEATINDEX_NEW);
	CHECK_EQ(c.first, 2000);
	CHECK_EQ(cut(&c, "cheat70000_code = \"x\"", CHEATINDEX_BYTES + 40), CHEATINDEX_BAD);
}

void test_cheatindex_lines(void)
{
	char out[CHEATINDEX_LINE_MAX];
	struct cheatindex_line got;
	u32 n = cheatindex_head(out, 0x150A2B);
	CHECK(!strcmp(out, "torii-cheat-index 1\nfile 150A2B\n"));
	CHECK_EQ(cheatindex_read_line(out, 19, &got), CHEATINDEX_MAGIC_LINE);
	CHECK_EQ(cheatindex_read_line(out + 20, n - 21, &got), CHEATINDEX_FILE);
	CHECK_EQ(got.size, 0x150A2B);

	struct cheatindex_window w = {0x1234, 0x9F00, 0xDEADBEEF, 750, 250, {0}};
	cheat_decoder_init(&w.dec);
	w.dec.cb_master = 0x9A2B3C4D;
	w.dec.cb_key2 = 0x1F2E;
	w.dec.gs_seeds[3] = 0x01020304;
	w.dec.cb_table[0x2F] = 0x77;
	w.dec.gs_version = CHEAT_FMT_PARV3;
	n = cheatindex_window_line(out, &w);
	CHECK(n < CHEATINDEX_LINE_MAX);
	CHECK_EQ(out[n - 1], '\n');
	CHECK_EQ(cheatindex_read_line(out, n - 1, &got), CHEATINDEX_WINDOW);
	CHECK_EQ(got.w.at, 0x1234);
	CHECK_EQ(got.w.len, 0x9F00);
	CHECK_EQ(got.w.crc, 0xDEADBEEF);
	CHECK_EQ(got.w.first, 750);
	CHECK_EQ(got.w.count, 250);
	CHECK_EQ(got.w.dec.cb_master, 0x9A2B3C4D);
	CHECK_EQ(got.w.dec.cb_key2, 0x1F2E);
	CHECK_EQ(got.w.dec.gs_seeds[3], 0x01020304);
	CHECK_EQ(got.w.dec.cb_table[0x2F], 0x77);
	CHECK_EQ(got.w.dec.gs_version, CHEAT_FMT_PARV3);
	CHECK(!memcmp(got.w.dec.cb_seeds, w.dec.cb_seeds, sizeof(w.dec.cb_seeds)));
	out[n - 3] = 'g';  // a bad digit
	CHECK_EQ(cheatindex_read_line(out, n - 1, &got), CHEATINDEX_OTHER);

	n = cheatindex_pick_line(out, 0x3A7, 0xCAFEF00D, 12, 0x5DC0, 1);
	CHECK(!strcmp(out, "p 3A7 CAFEF00D C 5DC0 1\n"));
	CHECK_EQ(cheatindex_read_line(out, n - 1, &got), CHEATINDEX_PICK);
	CHECK_EQ(got.number, 0x3A7);
	CHECK_EQ(got.hash, 0xCAFEF00D);
	CHECK_EQ(got.bytes, 12);
	CHECK_EQ(got.cost, 0x5DC0);
	CHECK_EQ(got.patches, 1);
	CHECK_EQ(cheatindex_read_line("q 1 2", 5, &got), CHEATINDEX_OTHER);
}
