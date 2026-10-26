// The code keypad's model (source/keypad_edit.c): lines typed and joined,
// a placeholder's stand-ins typed over, and what a code reads as.
#include <stdio.h>
#include <string.h>

#include "../../source/keypad_edit.h"
#include "../kernel/torii_test.h"
#include "tests.h"

static struct keypad_edit e;
static struct cheatfile_work work;
static char scratch[KEYPAD_CODE_MAX + 1];

static const char *code_of(void)
{
	static char out[KEYPAD_CODE_MAX + 1];
	keypad_edit_code(&e, out, sizeof(out));
	return out;
}

static void type_s(const char *s)
{
	for (; *s; s++) {
		if (*s == '\n')
			CHECK(keypad_edit_break(&e));
		else
			CHECK(keypad_edit_type(&e, *s));
	}
}

void test_keypad_edit(void)
{
	// A file's code: CodeBreaker lines in one or two parts, GameShark's in
	// two, VBA's; the caret at the end
	static const char file_code[] = "82001234+0063 32001000 0001+A9873144 203A53CE+02001000:63";
	CHECK(keypad_edit_init(&e, file_code, sizeof(file_code) - 1));
	CHECK_EQ(e.lines, 4);
	CHECK(!strcmp(e.text[0], "820012340063"));
	CHECK(!strcmp(e.text[1], "320010000001"));
	CHECK(!strcmp(e.text[2], "A9873144203A53CE"));
	CHECK(!strcmp(e.text[3], "02001000:63"));
	CHECK_EQ(e.line, 3);
	CHECK_EQ(e.caret, 11);
	CHECK(!strcmp(code_of(), "82001234 0063+32001000 0001+A9873144 203A53CE+02001000:63"));
	char shown[KEYPAD_LINE_MAX + 2];
	u32 caret = 99;
	CHECK_EQ(keypad_edit_show(&e, 0, shown, &caret), 13);
	CHECK(!strcmp(shown, "82001234 0063"));
	CHECK_EQ(caret, 99);  // not the caret's line
	keypad_edit_show(&e, 3, shown, &caret);
	CHECK(!strcmp(shown, "02001000:63"));
	CHECK_EQ(caret, 11);

	// Typing: lower case goes up, ':' too, other keys don't
	CHECK(keypad_edit_init(&e, "", 0));
	CHECK_EQ(e.lines, 1);
	CHECK(!strcmp(code_of(), ""));
	CHECK(!keypad_edit_break(&e));  // not on an empty line
	type_s("8200123");
	CHECK(!keypad_edit_type(&e, 'g'));
	CHECK(!keypad_edit_type(&e, ' '));
	type_s("4ab");
	keypad_edit_show(&e, 0, shown, &caret);
	CHECK(!strcmp(shown, "82001234 AB"));
	CHECK_EQ(caret, 11);
	type_s("\n02001000:FF");
	CHECK(!strcmp(code_of(), "82001234AB+02001000:FF"));  // a line still typed stays as it is
	// Delete, and join at a line's start; L/R between lines
	CHECK(keypad_edit_line(&e, -1));
	CHECK_EQ(e.line, 0);
	CHECK_EQ(e.caret, 10);
	CHECK(!keypad_edit_line(&e, -1));
	CHECK(keypad_edit_delete(&e));
	CHECK(keypad_edit_delete(&e));
	type_s("0063");
	CHECK(!strcmp(code_of(), "82001234 0063+02001000:FF"));
	CHECK(keypad_edit_line(&e, 1));
	CHECK_EQ(e.caret, 11);
	for (u32 i = 0; i < 11; i++)
		CHECK(keypad_edit_delete(&e));
	CHECK(keypad_edit_delete(&e));  // the empty line joins the one before
	CHECK_EQ(e.lines, 1);
	CHECK_EQ(e.caret, 12);
	// A line's cap, and the lines' cap
	type_s("12345");
	CHECK(!keypad_edit_type(&e, '6'));
	CHECK_EQ(e.len[0], KEYPAD_LINE_MAX);
	// A join that wouldn't fit
	CHECK(keypad_edit_init(&e, "02001000:12345678+1234?", 23));
	CHECK_EQ(e.line, 1);
	CHECK_EQ(e.caret, 4);
	for (u32 i = 0; i < 4; i++)
		CHECK(keypad_edit_delete(&e));
	CHECK(!keypad_edit_delete(&e));
	CHECK_EQ(e.lines, 2);
	CHECK(keypad_edit_init(&e, "", 0));
	for (u32 i = 0; i < KEYPAD_LINES - 1; i++)
		type_s("1\n");
	type_s("1");
	CHECK_EQ(e.lines, KEYPAD_LINES);
	CHECK(!keypad_edit_break(&e));
	// A break in the middle of a line
	CHECK(keypad_edit_init(&e, "820012340063", 12));
	for (u32 i = 0; i < 4; i++)
		CHECK(keypad_edit_delete(&e));
	type_s("\n0063");
	CHECK(!strcmp(code_of(), "82001234+0063"));

	// Too much to edit here
	static char many[KEYPAD_LINES * 14 + 16];
	char *p = many;
	for (u32 i = 0; i <= KEYPAD_LINES; i++)
		p += sprintf(p, "%s32001000 0001", i ? "+" : "");
	CHECK(!keypad_edit_init(&e, many, (u32)strlen(many)));
	CHECK_EQ(e.lines, 1);
	CHECK(!keypad_edit_init(&e, "0200100000:12345678", 19));
	// A byte the keypad can't type (the font has no glyph below ' ')
	CHECK(!keypad_edit_init(&e,
				"32001000 00\x01"
				"1",
				13));
	CHECK(!keypad_edit_init(&e, "32001000 G001", 13));
	CHECK_EQ(e.lines, 1);
	CHECK_EQ(e.len[0], 0);
}

void test_keypad_placeholder(void)
{
	// The caret starts on the first stand-in; typing goes over each in turn,
	// on to the next line's
	CHECK(keypad_edit_init(&e, "3300075C+00??+XX001000 000?", 27));
	CHECK_EQ(e.lines, 2);
	CHECK(!strcmp(e.text[0], "3300075C00??"));
	CHECK_EQ(keypad_edit_marks(&e), 5);
	CHECK_EQ(e.line, 0);
	CHECK_EQ(e.caret, 10);
	type_s("1");
	CHECK_EQ(e.caret, 11);
	type_s("F");
	CHECK_EQ(e.line, 1);
	CHECK_EQ(e.caret, 0);
	type_s("3");
	CHECK_EQ(e.caret, 1);
	type_s("2");
	CHECK_EQ(e.caret, 11);
	type_s("4");
	CHECK_EQ(keypad_edit_marks(&e), 0);
	CHECK_EQ(e.caret, 12);  // past the last
	CHECK(!strcmp(code_of(), "3300075C 001F+32001000 0004"));
	// L/R land on a line's first stand-in
	CHECK(keypad_edit_init(&e, "32001000 00??+32001002 0001", 27));
	CHECK(keypad_edit_line(&e, 1));
	CHECK_EQ(e.caret, 12);
	CHECK(keypad_edit_line(&e, -1));
	CHECK_EQ(e.caret, 10);
}

static struct keypad_check check(const char *code, u8 format)
{
	struct keypad_check c;
	CHECK(keypad_edit_init(&e, code, (u32)strlen(code)));
	keypad_check(&e, format, NULL, &work, scratch, &c);
	return c;
}

void test_keypad_check(void)
{
	struct keypad_check c = check("", CHEAT_FMT_AUTO);
	CHECK_EQ(c.status, KEYPAD_EMPTY);
	// CodeBreaker, 8 + 4
	c = check("82001234 0063", CHEAT_FMT_AUTO);
	CHECK_EQ(c.status, KEYPAD_OK);
	CHECK_EQ(c.format, CHEAT_FMT_CODEBREAKER);
	CHECK_EQ(c.ops, 1);
	CHECK_EQ(work.ops[0].type, CHEAT_OP_WRITE);
	CHECK_EQ(work.ops[0].address, 0x02001234);
	CHECK_EQ(work.ops[0].value, 0x63);
	CHECK_EQ(work.ops[0].width, 2);
	// VBA's addr:value
	c = check("03006000:5A", CHEAT_FMT_AUTO);
	CHECK_EQ(c.status, KEYPAD_OK);
	CHECK_EQ(c.format, CHEAT_FMT_VBA);
	CHECK_EQ(work.ops[0].width, 1);
	// GameShark v1, encrypted, by the plausibility score
	c = check("A9873144 203A53CE", CHEAT_FMT_AUTO);
	CHECK_EQ(c.status, KEYPAD_OK);
	CHECK_EQ(c.format, CHEAT_FMT_GSAV1);
	CHECK_EQ(work.ops[0].address, 0x020048E0);
	CHECK_EQ(work.ops[0].value, 0x270F);
	// tests/cheats.sh's, typed on the keypad there: 16-bit BEEF at 0203F040
	// (tools/cheat_encode.py's tea_encrypt of 1203F040 0000BEEF)
	c = check("D5A7153C 73B76013", CHEAT_FMT_AUTO);
	CHECK_EQ(c.status, KEYPAD_OK);
	CHECK_EQ(c.format, CHEAT_FMT_GSAV1);
	CHECK_EQ(work.ops[0].address, 0x0203F040);
	CHECK_EQ(work.ops[0].value, 0xBEEF);
	CHECK_EQ(work.ops[0].width, 2);
	// The same digits forced raw: off the GBA's map
	c = check("A9873144 203A53CE", CHEAT_FMT_GSAV1_RAW);
	CHECK_EQ(c.format, CHEAT_FMT_GSAV1_RAW);
	CHECK(c.status == KEYPAD_ADDRESS || c.status == KEYPAD_WRONG);
	// Raw GameShark, detected and forced
	c = check("03001234 000000AB", CHEAT_FMT_AUTO);
	CHECK_EQ(c.status, KEYPAD_OK);
	CHECK_EQ(c.format, CHEAT_FMT_GSAV1_RAW);
	CHECK(c.score > 0);
	// Action Replay v3, encrypted
	c = check("e51e97c3 7858e4eb", CHEAT_FMT_AUTO);
	CHECK_EQ(c.status, KEYPAD_OK);
	CHECK_EQ(c.format, CHEAT_FMT_PARV3);
	CHECK_EQ(work.ops[0].address, 0x02025BC4);
	// A CodeBreaker line forced as VBA doesn't read
	c = check("82001234 0063", CHEAT_FMT_VBA);
	CHECK_EQ(c.status, KEYPAD_WRONG);
	CHECK_EQ(c.format, CHEAT_FMT_VBA);
	// A line still typed: its shape, and which
	c = check("82001234 0063+3200", CHEAT_FMT_AUTO);
	CHECK_EQ(c.status, KEYPAD_SHAPE);
	CHECK_EQ(c.line, 1);
	// Stand-ins left
	c = check("3300075C 00??", CHEAT_FMT_AUTO);
	CHECK_EQ(c.status, KEYPAD_FILL);
	CHECK_EQ(c.format, CHEAT_FMT_CODEBREAKER);
	// Master codes alone
	c = check("0000ABCD 0007+100F68A4 0007", CHEAT_FMT_AUTO);
	CHECK_EQ(c.status, KEYPAD_MASTER);
	// A ROM write under a condition can't run here
	c = check("72001000 0001+88001000 4770", CHEAT_FMT_AUTO);
	CHECK_EQ(c.status, KEYPAD_CANT_RUN);
	// The file's decoder before the cheat: a GameShark version the file set
	struct cheat_decoder ctx;
	cheat_decoder_force(&ctx, CHEAT_FMT_GSAV1_RAW);
	ctx.forced = CHEAT_FMT_AUTO;
	CHECK(keypad_edit_init(&e, "03001234 000000AB", 17));
	keypad_check(&e, CHEAT_FMT_AUTO, &ctx, &work, scratch, &c);
	CHECK_EQ(c.status, KEYPAD_OK);
	CHECK_EQ(c.format, CHEAT_FMT_GSAV1_RAW);
}
