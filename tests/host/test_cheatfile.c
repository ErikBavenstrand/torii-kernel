// The cheat file model (source/cheatfile.c): libretro's format read, every
// cheat decoded in file order, picks within the write budget, and the file
// written back with only its enable values changed. Texts are heap copies
// without a NUL, so AddressSanitizer catches a read past the end.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../source/cheatfile.h"
#include "../kernel/torii_test.h"
#include "tests.h"

static struct cheatfile file;
static struct cheatfile_cheat cheats[CHEATFILE_MAX];
static struct cheatfile_work work;
static char *copy;

static void load(const char *text, u32 len)
{
	free(copy);
	copy = malloc(len ? len : 1);
	memcpy(copy, text, len);
	cheatfile_parse(&file, copy, len, cheats, CHEATFILE_MAX);
	cheatfile_begin(&work);
	while (cheatfile_next(&file, &work) >= 0)
		;
}

static void load_str(const char *text)
{
	load(text, (u32)strlen(text));
}

// cheat i's span as a string
static const char *span(u16 at, u16 len)
{
	static char out[4][256];
	static u32 turn;
	char *o = out[turn++ & 3];
	if (at == CHEATFILE_NONE)
		return "(none)";
	snprintf(o, 256, "%.*s", (int)len, copy + at);
	return o;
}

static const char *desc(u32 i)
{
	return span(cheats[i].desc, cheats[i].desc_len);
}

static const char *code(u32 i)
{
	return span(cheats[i].code, cheats[i].code_len);
}

// The written-back text, in chunks of step bytes, into a heap buffer (NUL
// added); its length must be cheatfile_text_len's
static char *written(u32 step)
{
	u32 want = cheatfile_text_len(&file);
	char *out = malloc(want + 64);
	u32 pos = 0, n;
	while ((n = cheatfile_text(&file, pos, out + pos, step)) != 0) {
		pos += n;
		CHECK(pos <= want);
		if (pos > want)
			break;
	}
	CHECK_EQ(pos, want);
	out[pos] = 0;
	return out;
}

void test_cheatfile_parse(void)
{
	load_str("cheats = 3\n"
		 "\n"
		 "cheat0_desc = \"Infinite Lives\"\n"
		 "cheat0_code = \"82001000+0063\"\n"
		 "cheat0_enable = false\n"
		 "\n"
		 "cheat1_desc = \"Max Money\"\n"
		 "cheat1_code = \"82001002 270F+82001004 270F\"\n"
		 "cheat1_enable = true\n"
		 "\n"
		 "cheat2_desc = \"Moon Jump\"\n"
		 "cheat2_code = \"02001006:01\"\n"
		 "cheat2_enable = false\n");
	CHECK_EQ(file.count, 3);
	CHECK_EQ(file.crlf, 0);
	CHECK(!strcmp(desc(0), "Infinite Lives"));
	CHECK(!strcmp(code(1), "82001002 270F+82001004 270F"));
	CHECK_EQ(cheats[0].picked, 0);
	CHECK_EQ(cheats[1].picked, 1);
	CHECK_EQ(cheats[1].was, 1);
	CHECK_EQ(cheats[0].status, CHEATFILE_OK);
	CHECK_EQ(cheats[0].writes, 2);
	CHECK_EQ(cheats[1].writes, 4);
	CHECK_EQ(cheats[2].writes, 1);
	CHECK_EQ(file.writes, 4);  // the picked one's
	CHECK_EQ(cheatfile_picks(&file), 1);
	CHECK(!cheatfile_changed(&file));

	// Keys in any order, numbers out of order, unknown keys, comments, a
	// count that disagrees, spaces and tabs, a missing closing quote, a
	// repeated key (its last line counts), and a cheat without a code
	load_str("# a comment\n"
		 "cheats = 7\n"
		 "cheat2_enable=TRUE\n"
		 "\tcheat2_code\t=\t\"32001000 0001\"  \n"
		 "cheat0_code = \"32001001 0002\n"
		 "cheat0_desc = \"Enemies Have Less HP\n"
		 "   # cheat3_code = \"32001003 0003\"\n"
		 "cheat5_desc = \"No code\"\n"
		 "cheat1_desc = first\n"
		 "cheat1_desc = \"second\"\n"
		 "cheat1_code = \"\"32001002 0004\"\"\n"
		 "something_else = 12\n"
		 "cheat1_colour = red\n"
		 "cheat01_enable = true\n"
		 "not a line\n"
		 "= 5\n"
		 "cheat4_code = 32001004 0005");  // no line end
	CHECK_EQ(file.count, 4);
	CHECK_EQ(cheats[0].number, 0);
	CHECK_EQ(cheats[1].number, 1);
	CHECK_EQ(cheats[2].number, 2);
	CHECK_EQ(cheats[3].number, 4);
	CHECK(!strcmp(code(0), "32001001 0002"));
	CHECK(!strcmp(desc(0), "Enemies Have Less HP"));
	CHECK(!strcmp(desc(1), "second"));
	CHECK(!strcmp(code(1), "\"32001002 0004\""));  // one quote off each end
	CHECK_EQ(cheats[1].picked, 1);                 // cheat01 is cheat1
	CHECK_EQ(cheats[2].picked, 1);                 // TRUE
	CHECK(!strcmp(desc(2), "(none)"));
	CHECK(!strcmp(code(3), "32001004 0005"));
	CHECK_EQ(cheats[3].enable, CHEATFILE_NONE);
	CHECK_EQ(cheats[0].status, CHEATFILE_OK);
	CHECK_EQ(cheats[2].status, CHEATFILE_OK);

	// CRLF and a BOM
	load_str("\xEF\xBB\xBF"
		 "cheats = 1\r\n"
		 "cheat0_desc = \"Lives\"\r\n"
		 "cheat0_code = \"32001000+0009\"\r\n"
		 "cheat0_enable = true\r\n");
	CHECK_EQ(file.count, 1);
	CHECK_EQ(file.crlf, 1);
	CHECK(!strcmp(desc(0), "Lives"));
	CHECK(!strcmp(code(0), "32001000+0009"));
	CHECK_EQ(cheats[0].picked, 1);
	CHECK_EQ(cheats[0].enable_len, 4);
}

void test_cheatfile_odd(void)
{
	load("", 0);
	CHECK_EQ(file.count, 0);
	CHECK_EQ(cheatfile_text_len(&file), 0);
	free(written(16));

	static const char junk[] =
		"\0\xff\xfe=\n\n===\ncheat\ncheat_code = 1\ncheatX_code = 2\ncheat0code = 3\n\x80\x81";
	load(junk, sizeof(junk) - 1);
	CHECK_EQ(file.count, 0);

	// A number past the table, and one too long for any number
	load_str("cheat1023_code = \"32001000 0001\"\n"
		 "cheat1024_code = \"32001000 0001\"\n"
		 "cheat99999999999999999999_code = \"32001000 0001\"\n");
	CHECK_EQ(file.count, 1);
	CHECK_EQ(cheats[0].number, 1023);
	CHECK_EQ(file.dropped, 2);

	// A line of only '=' and a long line: nothing read past the end
	char *big = malloc(5000);
	memset(big, 'q', 5000);
	memcpy(big, "cheat0_code = ", 14);
	load(big, 5000);
	CHECK_EQ(file.count, 1);
	CHECK_EQ(cheats[0].status, CHEATFILE_UNREADABLE);
	free(big);

	// A cheat whose code reads as nothing
	load_str("cheat0_code = \"\"\n");
	CHECK_EQ(file.count, 1);
	CHECK(cheats[0].status != CHEATFILE_OK);
}

void test_cheatfile_status(void)
{
	// 151 32-bit writes are 604 bytes: more than a game takes
	static char many[151 * 18 + 64];
	char *p = many;
	p += sprintf(p, "cheat6_code = \"");
	for (u32 i = 0; i < 151; i++)
		p += sprintf(p, "%s02001%03X:DEADBEEF", i ? "+" : "", (unsigned)(i * 4));
	sprintf(p, "\"\n");
	static char text[8192];
	snprintf(text, sizeof(text),
		 "cheat0_desc = \"Master Code\"\n"
		 "cheat0_code = \"0000ABCD 0007+100F68A4 0007\"\n"
		 "cheat1_code = \"3300075C+00??\"\n"
		 "cheat2_code = \"72001000 0001+32001010 0003\"\n"
		 "cheat3_code = \"ZZZZ\"\n"
		 "cheat4_code = \"0000ABCD 0007+100F68A4 0007+32001002 0001\"\n"
		 "cheat5_code = \"08001000:4770\"\n"
		 "%s",
		 many);
	load_str(text);
	CHECK_EQ(file.count, 7);
	CHECK_EQ(cheats[0].status, CHEATFILE_MASTER);
	CHECK(!cheatfile_shown(&cheats[0]));
	CHECK_EQ(cheats[1].status, CHEATFILE_PLACEHOLDER);
	CHECK_EQ(cheatfile_why(&file, 1), CHEATFILE_WHY_EDIT);
	CHECK_EQ(cheats[2].status, CHEATFILE_V08);
	CHECK_EQ(cheatfile_why(&file, 2), CHEATFILE_WHY_V08);
	CHECK_EQ(cheats[3].status, CHEATFILE_UNREADABLE);
	CHECK_EQ(cheatfile_why(&file, 3), CHEATFILE_WHY_UNREADABLE);
	CHECK_EQ(cheats[4].status, CHEATFILE_OK);  // a hook beside writes keeps its writes
	CHECK_EQ(cheats[4].writes, 1);
	CHECK_EQ(cheats[5].status, CHEATFILE_OK);
	CHECK_EQ(cheats[5].writes, 0);
	CHECK_EQ(cheats[5].patches, 1);
	CHECK_EQ(cheats[6].status, CHEATFILE_BIG);
	CHECK_EQ(cheatfile_why(&file, 6), CHEATFILE_WHY_FULL);
	for (u32 i = 0; i < 7; i++) {
		int ok = i == 4 || i == 5;
		CHECK_EQ(cheatfile_toggle(&file, i), ok);
		CHECK_EQ(cheats[i].picked, ok);
	}
	CHECK_EQ(file.writes, 1);
	// A greyed cheat the file picks can be dropped all the same
	load_str("cheat0_code = \"72001000 0001+32001010 0003\"\ncheat0_enable = true\n");
	CHECK_EQ(cheats[0].picked, 1);
	CHECK_EQ(file.writes, 0);
	CHECK_EQ(cheatfile_toggle(&file, 0), 1);
	CHECK_EQ(cheats[0].picked, 0);
	CHECK_EQ(cheatfile_toggle(&file, 0), 0);
}

// The decoder runs over every cheat in file order: a CodeBreaker key in a
// cheat no one picks still decrypts the cheats after it
void test_cheatfile_keys_carry(void)
{
	load_str("cheat0_desc = \"(M) Must Be On\"\n"
		 "cheat0_code = \"930CFE0E61A7+5E4E1F94523A+DB3A64369A43\"\n"
		 "cheat1_desc = \"Infinite HP\"\n"
		 "cheat1_code = \"D86098902576+777F4707DA29\"\n");
	CHECK_EQ(cheats[0].status, CHEATFILE_MASTER);
	CHECK_EQ(cheats[1].status, CHEATFILE_V08);  // a conditional, read with the key
	cheatfile_begin(&work);
	CHECK_EQ(cheatfile_next(&file, &work), 0);
	CHECK_EQ(cheatfile_next(&file, &work), 1);
	CHECK_EQ(work.ops[1].type, CHEAT_OP_WRITE);
	CHECK_EQ(work.ops[1].address, 0x0203A9D4);
	CHECK_EQ(work.ops[1].value, 0x270F);

	// The lowered writes stay in the work area after each cheat
	load_str("cheat0_code = \"82001000+BEEF\"\ncheat1_code = \"08001000:4770\"\n");
	cheatfile_begin(&work);
	CHECK_EQ(cheatfile_next(&file, &work), 0);
	CHECK_EQ(work.n_writes, 2);
	CHECK_EQ(work.writes[0].address, 0x02001000);
	CHECK_EQ(work.writes[0].value, 0xEF);
	CHECK_EQ(work.writes[1].address, 0x02001001);
	CHECK_EQ(work.writes[1].value, 0xBE);
	CHECK_EQ(work.n_patches, 0);
	CHECK_EQ(cheatfile_next(&file, &work), 1);
	CHECK_EQ(work.n_writes, 0);
	CHECK_EQ(work.n_patches, 1);
	CHECK_EQ(work.patches[0].offset, 0x1000);
	CHECK_EQ(work.patches[0].value, 0x4770);
	CHECK_EQ(work.patches[0].mask, 0xFFFF);
	CHECK_EQ(cheatfile_next(&file, &work), -1);
}

void test_cheatfile_budget(void)
{
	// Two cheats of 300 writes each (75 32-bit writes), and one of 4
	static char text[16384];
	char *p = text;
	for (u32 c = 0; c < 2; c++) {
		p += sprintf(p, "cheat%u_code = \"", (unsigned)c);
		for (u32 i = 0; i < 75; i++)
			p += sprintf(p, "%s0200%u%03X:01020304", i ? "+" : "", (unsigned)c, (unsigned)(i * 4));
		p += sprintf(p, "\"\n");
	}
	sprintf(p, "cheat2_code = \"03001000:01020304\"\n");
	load_str(text);
	CHECK_EQ(cheats[0].writes, 300);
	CHECK_EQ(cheats[1].writes, 300);
	CHECK_EQ(cheats[2].writes, 4);
	CHECK(cheatfile_toggle(&file, 0));
	CHECK(cheatfile_toggle(&file, 1));
	CHECK_EQ(file.writes, 600);
	CHECK_EQ(cheatfile_why(&file, 2), CHEATFILE_WHY_FULL);
	CHECK(!cheatfile_toggle(&file, 2));
	CHECK(cheatfile_toggle(&file, 0));  // dropped: room again
	CHECK_EQ(file.writes, 300);
	CHECK_EQ(cheatfile_why(&file, 2), CHEATFILE_PICKABLE);
	CHECK(cheatfile_toggle(&file, 2));
	CHECK_EQ(file.writes, 304);
	CHECK_EQ(cheatfile_why(&file, 0), CHEATFILE_WHY_FULL);
	CHECK(cheatfile_changed(&file));
	CHECK_EQ(cheatfile_picks(&file), 2);
}

// Only enable values change; every other byte stays, CRLF and comments too
void test_cheatfile_write_back(void)
{
	static const char text[] = "# by hand\r\n"
				   "cheats = 3\r\n"
				   "\r\n"
				   "cheat0_desc = \"A\"\r\n"
				   "cheat0_code = \"32001000 0001\"\r\n"
				   "cheat0_enable =   false   \r\n"
				   "cheat1_enable = \"true\"\r\n"
				   "cheat1_code = \"32001001 0001\"\r\n"
				   "cheat2_code = \"32001002 0001\"\r\n"
				   "cheat2_unknown = whatever\r\n";
	load_str(text);
	CHECK_EQ(file.count, 3);
	char *same = written(7);
	CHECK(!strcmp(same, text));  // nothing changed: the text as it was
	free(same);
	CHECK(cheatfile_toggle(&file, 0));
	CHECK(cheatfile_toggle(&file, 1));
	CHECK(cheatfile_toggle(&file, 2));  // no enable line: one is added
	static const char want[] = "# by hand\r\n"
				   "cheats = 3\r\n"
				   "\r\n"
				   "cheat0_desc = \"A\"\r\n"
				   "cheat0_code = \"32001000 0001\"\r\n"
				   "cheat0_enable =   true   \r\n"
				   "cheat1_enable = \"false\"\r\n"
				   "cheat1_code = \"32001001 0001\"\r\n"
				   "cheat2_code = \"32001002 0001\"\r\n"
				   "cheat2_unknown = whatever\r\n"
				   "cheat2_enable = true\r\n";
	for (u32 step = 1; step <= 600; step = step * 3 + 1) {
		char *out = written(step);
		CHECK(!strcmp(out, want));
		free(out);
	}
	// Read again, it has the new picks
	load_str(want);
	CHECK_EQ(cheats[0].picked, 1);
	CHECK_EQ(cheats[1].picked, 0);
	CHECK_EQ(cheats[2].picked, 1);

	// A file whose last line has no end gets one before an added line
	load_str("cheat0_code = \"32001000 0001\"\ncheat1_code = \"32001001 0001\"");
	CHECK(cheatfile_toggle(&file, 1));
	char *out = written(5);
	CHECK(!strcmp(out, "cheat0_code = \"32001000 0001\"\ncheat1_code = \"32001001 0001\"\ncheat1_enable = true\n"));
	free(out);
	// A call from anywhere but the last call's end gives nothing
	char buf[8];
	CHECK_EQ(cheatfile_text(&file, 0, buf, 4), 4);
	CHECK_EQ(cheatfile_text(&file, 3, buf, 4), 0);
	free(copy);
	copy = NULL;
}
