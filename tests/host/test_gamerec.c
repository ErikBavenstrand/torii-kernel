// A game's record (source/gamerec.c): defaults, parsing what a PC editor may
// leave, and the round trip. Buffers are heap copies of exactly their size,
// so AddressSanitizer catches a write or read past either.
#include <stdlib.h>
#include <string.h>

#include "../../source/gamerec.h"
#include "../kernel/torii_test.h"
#include "tests.h"

static void parse(struct gamerec *r, const char *text)
{
	u32 len = (u32)strlen(text);
	char *copy = malloc(len ? len : 1);
	memcpy(copy, text, len);
	gamerec_init(r);
	gamerec_parse(r, copy, len);
	free(copy);
}

void test_gamerec(void)
{
	struct gamerec r;
	gamerec_init(&r);
	CHECK_EQ(r.save_type, 0);
	CHECK_EQ(r.boot_mode, 0);
	CHECK(!strcmp(r.slot, "Main"));

	// Defaults for nothing, and for text with no keys
	parse(&r, "");
	CHECK(!strcmp(r.slot, "Main") && r.save_type == 0 && r.boot_mode == 0);
	parse(&r, "\n\nhello\n=\n");
	CHECK(!strcmp(r.slot, "Main") && r.save_type == 0 && r.boot_mode == 0);

	// A normal record, and one edited on a PC: CRLF, spaces, a BOM, no last newline
	parse(&r, "save_type = 3\nboot_mode = 2\nslot = Boss fight\n");
	CHECK(r.save_type == 3 && r.boot_mode == 2 && !strcmp(r.slot, "Boss fight"));
	parse(&r, "\xEF\xBB\xBFsave_type=4\r\n  boot_mode\t=\t1  \r\nslot =  Run 2  \r\nextra = 9");
	CHECK(r.save_type == 4 && r.boot_mode == 1 && !strcmp(r.slot, "Run 2"));

	// Bad values keep the default; an unclean slot is cleaned, an empty one is Main
	parse(&r, "save_type = 300\nboot_mode = x\nslot = a/b\n");
	CHECK(r.save_type == 0 && r.boot_mode == 0 && !strcmp(r.slot, "ab"));
	parse(&r, "slot = ..\nsave_type = -1\n");
	CHECK(!strcmp(r.slot, "Main") && r.save_type == 0);
	parse(&r, "save_type = 1\nsave_type = 2\n");
	CHECK_EQ(r.save_type, 2);  // the last line wins
	parse(&r, "SAVE_TYPE = 3\n");
	CHECK_EQ(r.save_type, 0);  // keys are exact

	// A long line is no overflow
	char big[2000];
	memset(big, 'a', sizeof(big));
	memcpy(big, "slot = ", 7);
	big[sizeof(big) - 1] = 0;
	parse(&r, big);
	CHECK(strlen(r.slot) <= SAVESLOT_NAME_BYTES - 1 && r.slot[0] == 'a');

	// The round trip, in exactly the room it needs
	gamerec_init(&r);
	r.save_type = 5;
	r.boot_mode = 1;
	strcpy(r.slot, "Caf\xC3\xA9 run");
	char text[GAMEREC_BYTES];
	u32 n = gamerec_format(text, sizeof(text), &r);
	CHECK(n && n < GAMEREC_BYTES);
	char *exact = malloc(n + 1);
	CHECK_EQ(gamerec_format(exact, n + 1, &r), n);
	CHECK_EQ(gamerec_format(exact, n, &r), 0);  // no room: nothing
	struct gamerec back;
	gamerec_init(&back);
	gamerec_parse(&back, text, n);
	CHECK(back.save_type == 5 && back.boot_mode == 1 && !strcmp(back.slot, r.slot));
	free(exact);

	// The longest slot fits GAMEREC_BYTES
	gamerec_init(&r);
	memset(r.slot, 'x', SAVESLOT_NAME_BYTES - 1);
	r.slot[SAVESLOT_NAME_BYTES - 1] = 0;
	CHECK(gamerec_format(text, sizeof(text), &r) != 0);
}
