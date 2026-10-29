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
static const char *span(u32 at, u32 len)
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
	CHECK_EQ(cheats[0].bytes, 12);  // a header and a halfword write
	CHECK_EQ(cheats[1].bytes, 24);  // a header and a run of two
	CHECK_EQ(cheats[2].bytes, 12);
	CHECK_EQ(cheats[1].cost, CHEAT_LOAD(CHEAT_COST_CHEAT + CHEAT_COST_RUN + 2 * CHEAT_COST_RUN_WRITE));
	CHECK_EQ(file.bytes, 24);  // the picked one's
	CHECK_EQ(file.cost, cheats[1].cost);
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

	// RetroArch's bools: 1 is true too, 0 and anything else false
	load_str("cheat0_code = \"32001000 0001\"\ncheat0_enable = 1\n"
		 "cheat1_code = \"32001000 0001\"\ncheat1_enable = 0\n"
		 "cheat2_code = \"32001000 0001\"\ncheat2_enable = 11\n");
	CHECK_EQ(cheats[0].picked, 1);
	CHECK_EQ(cheats[1].picked, 0);
	CHECK_EQ(cheats[2].picked, 0);
	CHECK(!file.cut);

	// A text past CHEATFILE_TEXT_MAX is read cut, and says so
	char *huge = malloc(CHEATFILE_TEXT_MAX + 1);
	memset(huge, '\n', CHEATFILE_TEXT_MAX + 1);
	load(huge, CHEATFILE_TEXT_MAX + 1);
	CHECK(file.cut);
	CHECK_EQ(file.len, CHEATFILE_TEXT_MAX);
	load(huge, CHEATFILE_TEXT_MAX);
	CHECK(!file.cut);
	free(huge);
}

void test_cheatfile_status(void)
{
	// 260 writes are more ops than a cheat takes
	static char many[260 * 18 + 64];
	char *p = many;
	p += sprintf(p, "cheat6_code = \"");
	for (u32 i = 0; i < 260; i++)
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
	CHECK_EQ(cheats[2].status, CHEATFILE_OK);  // a conditional runs (v0.7)
	CHECK_EQ(cheats[2].bytes, 24);
	CHECK_EQ(cheatfile_why(&file, 2), CHEATFILE_PICKABLE);
	CHECK_EQ(cheats[3].status, CHEATFILE_UNREADABLE);
	CHECK_EQ(cheatfile_why(&file, 3), CHEATFILE_WHY_UNREADABLE);
	CHECK_EQ(cheats[4].status, CHEATFILE_OK);  // a hook beside writes keeps its writes
	CHECK_EQ(cheats[4].bytes, 12);
	CHECK_EQ(cheats[5].status, CHEATFILE_OK);
	CHECK_EQ(cheats[5].bytes, 0);
	CHECK_EQ(cheats[5].cost, 0);
	CHECK_EQ(cheats[5].patches, 1);
	CHECK_EQ(cheats[6].status, CHEATFILE_BIG);
	CHECK_EQ(cheatfile_why(&file, 6), CHEATFILE_WHY_FULL);
	for (u32 i = 0; i < 7; i++) {
		int ok = i == 2 || i == 4 || i == 5;
		CHECK_EQ(cheatfile_toggle(&file, i), ok);
		CHECK_EQ(cheats[i].picked, ok);
	}
	CHECK_EQ(file.bytes, 36);
	// A ROM write under a condition can't run here
	load_str("cheat0_code = \"72001000 0001+88001000 4770\"\n");
	CHECK_EQ(cheats[0].status, CHEATFILE_CANT_RUN);
	CHECK_EQ(cheatfile_why(&file, 0), CHEATFILE_WHY_CANT_RUN);
	CHECK(!cheatfile_toggle(&file, 0));
	// A greyed cheat the file picks can be dropped all the same
	load_str("cheat0_code = \"3300075C+00??\"\ncheat0_enable = true\n");
	CHECK_EQ(cheats[0].picked, 1);
	CHECK_EQ(file.bytes, 0);
	CHECK_EQ(cheatfile_toggle(&file, 0), 1);
	CHECK_EQ(cheats[0].picked, 0);
	CHECK_EQ(cheatfile_toggle(&file, 0), 0);
}

// A cheat that changes nothing, or does what an earlier one does, reads but
// shows greyed with its reason and can't be picked; a picked one can be
// dropped
void test_cheatfile_junk(void)
{
	load_str("cheat0_code = \"32001010 0003\"\n"
		 "cheat1_code = \"72001000 0001\"\n"  // a condition over nothing
		 "cheat2_code = \"32001010 0003\"\n"  // cheat 0 again
		 "cheat3_code = \"72001000 0001+32001010 0003\"\n"  // cheat 0's write, under a condition
		 "cheat4_code = \"ZZZZ\"\n"
		 "cheat5_code = \"32001010 0003\"\n"
		 "cheat5_enable = true\n"
		 "cheat6_code = \"88001000 4770\"\n");  // a ROM patch
	CHECK_EQ(file.count, 7);
	CHECK_EQ(cheats[0].status, CHEATFILE_OK);
	CHECK_EQ(cheats[1].status, CHEATFILE_NOTHING);
	CHECK(cheatfile_shown(&cheats[1]));
	CHECK_EQ(cheatfile_why(&file, 1), CHEATFILE_WHY_NOTHING);
	CHECK(!cheatfile_toggle(&file, 1));
	CHECK_EQ(cheats[2].status, CHEATFILE_SAME);
	CHECK(cheatfile_shown(&cheats[2]));
	CHECK_EQ(cheatfile_why(&file, 2), CHEATFILE_WHY_SAME);
	CHECK_EQ(cheats[2].bytes, 0);
	CHECK_EQ(cheats[3].status, CHEATFILE_OK);
	CHECK_EQ(cheats[4].status, CHEATFILE_UNREADABLE);
	CHECK(cheatfile_shown(&cheats[4]));
	CHECK_EQ(cheats[5].status, CHEATFILE_SAME);
	CHECK_EQ(file.bytes, 0);  // a picked duplicate runs nothing
	CHECK(cheatfile_toggle(&file, 5));  // but can be dropped
	CHECK_EQ(cheats[5].picked, 0);
	CHECK_EQ(cheats[6].status, CHEATFILE_OK);
	// A second pass finds the same
	cheatfile_begin(&work);
	while (cheatfile_next(&file, &work) >= 0)
		;
	CHECK_EQ(cheats[0].status, CHEATFILE_OK);
	CHECK_EQ(cheats[2].status, CHEATFILE_SAME);
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
	CHECK_EQ(cheats[1].status, CHEATFILE_OK);  // a conditional, read with the key
	cheatfile_begin(&work);
	CHECK_EQ(cheatfile_next(&file, &work), 0);
	CHECK_EQ(cheatfile_next(&file, &work), 1);
	CHECK_EQ(work.ops[1].type, CHEAT_OP_WRITE);
	CHECK_EQ(work.ops[1].address, 0x0203A9D4);
	CHECK_EQ(work.ops[1].value, 0x270F);

	// The cheat's table part stays in the work area after each cheat
	load_str("cheat0_code = \"82001000+BEEF\"\ncheat1_code = \"08001000:4770\"\n");
	cheatfile_begin(&work);
	CHECK_EQ(cheatfile_next(&file, &work), 0);
	CHECK_EQ(work.n_prog, 3);
	CHECK_EQ(work.prog[1], 0x12001000);
	CHECK_EQ(work.prog[2], 0xBEEF);
	CHECK_EQ(work.n_patches, 0);
	CHECK_EQ(cheatfile_next(&file, &work), 1);
	CHECK_EQ(work.n_prog, 0);
	CHECK_EQ(work.n_patches, 1);
	CHECK_EQ(work.patches[0].offset, 0x1000);
	CHECK_EQ(work.patches[0].value, 0x4770);
	CHECK_EQ(work.patches[0].mask, 0xFFFF);
	CHECK_EQ(cheatfile_next(&file, &work), -1);
}

// GameShark's cipher forward (cheatcode.c has the way back)
static void gs_encrypt(u32 *op1, u32 *op2, const u32 *seeds)
{
	u32 sum = 0;
	for (int i = 0; i < 32; ++i) {
		sum += 0x9E3779B9;
		*op1 += ((*op2 << 4) + seeds[0]) ^ (*op2 + sum) ^ ((*op2 >> 5) + seeds[1]);
		*op2 += ((*op1 << 4) + seeds[2]) ^ (*op1 + sum) ^ ((*op1 >> 5) + seeds[3]);
	}
}

void test_cheatfile_seeds_carry(void)
{
	// A GameShark file reseeded by DEADFACE, then a cheat typed in its format:
	// the cheat is read with the file's seeds, as the device reads it
	static const char master[] = "502F4DE9+75D4B6CA+8791F574+20D9F121";
	struct cheat_decoder d;
	struct cheat_op ops[4];
	u32 n, a = 0xDEADFACE, v = 0x102, b = 0x12000012, w = 0x1234;
	cheat_decoder_init(&d);
	CHECK_EQ(cheat_decode(&d, master, sizeof(master) - 1, ops, 4, &n), CHEAT_MASTER_ONLY);
	CHECK_EQ(d.gs_version, CHEAT_FMT_GSAV1);
	gs_encrypt(&a, &v, d.gs_seeds);
	char reseed[18];
	snprintf(reseed, sizeof(reseed), "%08X+%08X", (unsigned)a, (unsigned)v);
	CHECK_EQ(cheat_decode(&d, reseed, 17, ops, 4, &n), CHEAT_MASTER_ONLY);
	gs_encrypt(&b, &w, d.gs_seeds);

	char text[512];
	snprintf(text, sizeof(text),
		 "cheat0_code = \"%s\"\n"
		 "cheat1_code = \"%s\"\n"
		 "cheat2_code = \"%08X %08X\"\n"
		 "cheat2_format = \"GSAv1\"\n"
		 "cheat2_user = true\n",
		 master, reseed, (unsigned)b, (unsigned)w);
	load_str(text);
	CHECK_EQ(cheats[1].status, CHEATFILE_MASTER);
	CHECK_EQ(cheats[2].status, CHEATFILE_OK);
	cheatfile_begin(&work);
	for (u32 i = 0; i < 3; i++)
		CHECK_EQ(cheatfile_next(&file, &work), (s32)i);
	CHECK_EQ(work.ops[0].type, CHEAT_OP_WRITE);
	CHECK_EQ(work.ops[0].address, 0x02000012);
	CHECK_EQ(work.ops[0].value, 0x1234);

	// The other way: the DEADFACE sits in a cheat with its format, and the
	// file's later cheats are read with its seeds
	snprintf(text, sizeof(text),
		 "cheat0_code = \"%s\"\n"
		 "cheat1_code = \"%s\"\n"
		 "cheat1_format = \"GSAv1\"\n"
		 "cheat2_code = \"%08X %08X\"\n",
		 master, reseed, (unsigned)b, (unsigned)w);
	load_str(text);
	cheatfile_begin(&work);
	for (u32 i = 0; i < 3; i++)
		CHECK_EQ(cheatfile_next(&file, &work), (s32)i);
	CHECK_EQ(cheats[2].status, CHEATFILE_OK);
	CHECK_EQ(work.ops[0].type, CHEAT_OP_WRITE);
	CHECK_EQ(work.ops[0].address, 0x02000012);
	CHECK_EQ(work.ops[0].value, 0x1234);
}

void test_cheatfile_budget(void)
{
	// The table's bytes: three cheats of 250 32-bit writes (2,008 bytes
	// each), two fit; and one of 12 bytes beside them
	static char text[32768];
	char *p = text;
	for (u32 c = 0; c < 3; c++) {
		p += sprintf(p, "cheat%u_code = \"", (unsigned)c);
		for (u32 i = 0; i < 250; i++)
			p += sprintf(p, "%s0200%u%03X:01020304", i ? "+" : "", (unsigned)c, (unsigned)(i * 4));
		p += sprintf(p, "\"\n");
	}
	sprintf(p, "cheat3_code = \"03001000:01020304\"\n");
	load_str(text);
	CHECK_EQ(cheats[0].bytes, 2008);
	CHECK_EQ(cheats[3].bytes, 12);
	CHECK(cheatfile_toggle(&file, 0));
	CHECK(cheatfile_toggle(&file, 1));
	CHECK_EQ(file.bytes, 4016);
	CHECK_EQ(cheatfile_why(&file, 2), CHEATFILE_WHY_FULL);
	CHECK(!cheatfile_toggle(&file, 2));
	CHECK_EQ(cheatfile_why(&file, 3), CHEATFILE_PICKABLE);
	CHECK(cheatfile_toggle(&file, 0));  // dropped: room again
	CHECK_EQ(file.bytes, 2008);
	CHECK_EQ(cheatfile_why(&file, 2), CHEATFILE_PICKABLE);
	CHECK(cheatfile_toggle(&file, 2));
	CHECK_EQ(file.bytes, 4016);
	CHECK_EQ(cheatfile_why(&file, 0), CHEATFILE_WHY_FULL);
	CHECK(cheatfile_changed(&file));
	CHECK_EQ(cheatfile_picks(&file), 2);

	// The cycles: two cheats of 40 conditional writes each fit the bytes but
	// not one frame's share together
	p = text;
	for (u32 c = 0; c < 2; c++) {
		p += sprintf(p, "cheat%u_code = \"", (unsigned)c);
		for (u32 i = 0; i < 40; i++)
			p += sprintf(p, "%s72001000 0001+32001%03X 0003", i ? "+" : "", (unsigned)(c * 0x100 + i * 2));
		p += sprintf(p, "\"\n");
	}
	load_str(text);
	CHECK_EQ(cheats[0].status, CHEATFILE_OK);
	// The conditions always run, the writes they guard only while they hold:
	// the load is the conditions', scaled
	CHECK_EQ(cheats[0].cost, CHEAT_LOAD(CHEAT_COST_CHEAT + 40 * (CHEAT_COST_COND + CHEAT_COST_WALK)));
	CHECK(cheatfile_toggle(&file, 0));
	CHECK(file.bytes + cheats[1].bytes <= CHEATFILE_BYTES_MAX);
	CHECK_EQ(cheatfile_why(&file, 1), CHEATFILE_WHY_FULL);

	// Alone past the frame's share: too big
	p = text;
	p += sprintf(p, "cheat0_code = \"");
	for (u32 i = 0; i < 63; i++)
		p += sprintf(p, "%s72001000 0001+32001%03X 0003", i ? "+" : "", (unsigned)(i * 2));
	sprintf(p, "\"\n");
	load_str(text);
	CHECK_EQ(cheats[0].status, CHEATFILE_BIG);
	CHECK_EQ(cheatfile_why(&file, 0), CHEATFILE_WHY_FULL);

	// Heavy work a button guards may take the gated cap (Harvest Moon's
	// "Till Whole Field": 2152 halfwords while Select+L are held); the
	// same fill unguarded is too big
	load_str("cheat0_code = \"74000130 01FB+420057F8 0002+00000868 0002\"\n"
		 "cheat1_code = \"420057F8 0002+00000868 0002\"\n");
	CHECK_EQ(cheats[0].status, CHEATFILE_OK);
	CHECK(cheats[0].cost > CHEAT_CYCLES_MAX);
	CHECK(cheats[0].cost <= CHEATFILE_CYCLES_MAX);
	CHECK_EQ(cheatfile_why(&file, 0), CHEATFILE_PICKABLE);
	CHECK_EQ(cheats[1].status, CHEATFILE_BIG);
}

// The ROM patches a launch keeps (CHEATFILE_PICKED_PATCHES_MAX): a pick that
// would pass them is refused, so a launch never has to leave part of a cheat
// out
void test_cheatfile_patches_budget(void)
{
	static char text[16384];
	static const u32 n[4] = {100, 100, 60, 56};
	char *p = text;
	for (u32 c = 0; c < 4; c++) {
		p += sprintf(p, "cheat%u_code = \"", (unsigned)c);
		for (u32 i = 0; i < n[c]; i++)
			p += sprintf(p, "%s08%u%05X:4770", i ? "+" : "", (unsigned)c, (unsigned)(i * 2));
		p += sprintf(p, "\"\n");
	}
	load_str(text);
	for (u32 c = 0; c < 4; c++) {
		CHECK_EQ(cheats[c].status, CHEATFILE_OK);
		CHECK_EQ(cheats[c].patches, n[c]);
	}
	CHECK(cheatfile_toggle(&file, 0));
	CHECK(cheatfile_toggle(&file, 1));
	CHECK_EQ(file.patches, 200);
	CHECK_EQ(cheatfile_why(&file, 2), CHEATFILE_WHY_PATCHES);  // 260 of 256
	CHECK(!cheatfile_toggle(&file, 2));
	CHECK_EQ(cheatfile_why(&file, 3), CHEATFILE_PICKABLE);  // 256 exactly
	CHECK(cheatfile_toggle(&file, 3));
	CHECK_EQ(file.patches, CHEATFILE_PICKED_PATCHES_MAX);
	CHECK(cheatfile_toggle(&file, 1));  // dropped: room again
	CHECK_EQ(file.patches, 156);
	CHECK_EQ(cheatfile_why(&file, 2), CHEATFILE_PICKABLE);
	// Decoded again, the running total holds
	cheatfile_begin(&work);
	while (cheatfile_next(&file, &work) >= 0)
		;
	CHECK_EQ(file.patches, 156);
	// One cheat of the most ROM patches a cheat may have fits alone
	CHECK_EQ(CHEATFILE_PICKED_PATCHES_MAX, CHEATFILE_PATCHES_MAX);
}

// A pick's enable line counts against the file's cap: a pick the card
// couldn't be written with is refused up front, never accepted and lost
void test_cheatfile_room_pick(void)
{
	char *t = malloc(CHEATFILE_TEXT_MAX);
	static const char tail[] = "\ncheat0_code = \"32001000 0001\"\ncheat1_code = \"32001001 0001\"\n";
	u32 tl = (u32)strlen(tail), line = (u32)strlen("cheat0_enable = true\n");
	// Room for one added line and 9 bytes more
	u32 len = CHEATFILE_TEXT_MAX - line - 9;
	memset(t, '#', len - tl);
	memcpy(t + len - tl, tail, tl);
	load(t, len);
	CHECK_EQ(file.count, 2);
	CHECK_EQ(cheatfile_why(&file, 0), CHEATFILE_PICKABLE);
	CHECK(cheatfile_toggle(&file, 0));
	CHECK_EQ(cheatfile_text_len(&file), CHEATFILE_TEXT_MAX - 9);
	CHECK_EQ(cheatfile_why(&file, 1), CHEATFILE_WHY_ROOM);
	CHECK(!cheatfile_toggle(&file, 1));
	char *out = written(1000);
	free(out);
	CHECK(cheatfile_toggle(&file, 0));  // dropped: its line goes, and the room with it
	CHECK_EQ(cheatfile_text_len(&file), len);
	CHECK_EQ(cheatfile_why(&file, 1), CHEATFILE_PICKABLE);

	// CRLF and no line end at the end: the first added line brings one,
	// and a pick that fills the cap to the byte is taken
	static const char crlf[] = "\r\ncheat0_code = \"32001000 0001\"\r\ncheat1_code = \"32001001 0001\"";
	tl = (u32)strlen(crlf);
	len = CHEATFILE_TEXT_MAX - 2 - (u32)strlen("cheat1_enable = true\r\n");
	memset(t, '#', len - tl);
	memcpy(t + len - tl, crlf, tl);
	load(t, len);
	CHECK(file.crlf);
	CHECK_EQ(cheatfile_why(&file, 1), CHEATFILE_PICKABLE);
	CHECK(cheatfile_toggle(&file, 1));
	CHECK_EQ(cheatfile_text_len(&file), CHEATFILE_TEXT_MAX);
	out = written(333);
	CHECK(!memcmp(out + len, "\r\ncheat1_enable = true\r\n", 24));
	free(out);
	CHECK_EQ(cheatfile_why(&file, 0), CHEATFILE_WHY_ROOM);

	// An enable line already there: "false" to "true" takes a byte less, so
	// a file at the cap still picks
	static const char has[] = "\ncheat0_code = \"32001000 0001\"\ncheat0_enable = false\n";
	tl = (u32)strlen(has);
	memset(t, '#', CHEATFILE_TEXT_MAX - tl);
	memcpy(t + CHEATFILE_TEXT_MAX - tl, has, tl);
	load(t, CHEATFILE_TEXT_MAX);
	CHECK_EQ(cheatfile_why(&file, 0), CHEATFILE_PICKABLE);
	CHECK(cheatfile_toggle(&file, 0));
	CHECK_EQ(cheatfile_text_len(&file), CHEATFILE_TEXT_MAX - 1);
	out = written(4096);
	free(out);
	free(t);
	free(copy);
	copy = NULL;
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

// --- Edits (the code keypad's) -------------------------------------------------------

static char *edit_buf;
static u32 edit_cap;

// The room on the host: plain memory. put's source never overlaps its
// destination (cheatfile.h), so memcpy catches one that does under ASan.
static void host_put(char *dst, const char *src, u32 n)
{
	CHECK(dst >= edit_buf && dst + n <= edit_buf + edit_cap);
	memcpy(dst, src, n);
}

static void host_move(char *dst, const char *src, u32 n)
{
	CHECK(dst >= edit_buf && dst + n <= edit_buf + edit_cap);
	CHECK(src >= edit_buf && src + n <= edit_buf + edit_cap);
	memmove(dst, src, n);
}

static struct cheatfile_room room;

// text into a buffer of cap bytes (ASan catches a write past it), read and
// decoded
static void load_cap(const char *text, u32 cap)
{
	u32 len = (u32)strlen(text);
	free(copy);
	copy = NULL;
	free(edit_buf);
	edit_cap = cap;
	edit_buf = malloc(cap ? cap : 1);
	room = (struct cheatfile_room){edit_buf, cap, host_put, host_move};
	memcpy(edit_buf, text, len);
	cheatfile_parse(&file, edit_buf, len, cheats, CHEATFILE_MAX);
	cheatfile_begin(&work);
	while (cheatfile_next(&file, &work) >= 0)
		;
}

static void redecode(void)
{
	cheatfile_begin(&work);
	while (cheatfile_next(&file, &work) >= 0)
		;
}

// The edited text as it is now (picks not written in)
static int text_is(const char *want)
{
	int same = file.len == strlen(want) && !memcmp(file.text, want, file.len);
	if (!same)
		printf("text is:\n%.*s\n--- not:\n%s\n", (int)file.len, file.text, want);
	return same;
}

// 1 if the edited text holds s
static int text_has(const char *s)
{
	u32 n = (u32)strlen(s);
	for (u32 i = 0; i + n <= file.len; i++)
		if (!memcmp(file.text + i, s, n))
			return 1;
	return 0;
}

static const char *edit_span(u32 at, u32 len)
{
	static char out[4][256];
	static u32 turn;
	char *o = out[turn++ & 3];
	if (at == CHEATFILE_NONE)
		return "(none)";
	snprintf(o, 256, "%.*s", (int)len, file.text + at);
	return o;
}

void test_cheatfile_add(void)
{
	u32 at = 99;
	// After the others, with a blank line; the count follows
	load_cap("cheats = 2\n"
		 "\n"
		 "cheat0_desc = \"Lives\"\n"
		 "cheat0_code = \"32001000 0009\"\n"
		 "cheat0_enable = false\n"
		 "\n"
		 "cheat1_desc = \"Money\"\n"
		 "cheat1_code = \"32001001 0009\"\n"
		 "cheat1_enable = false\n",
		 4096);
	CHECK(cheatfile_toggle(&file, 1));  // a pick not saved yet stays
	CHECK_EQ(cheatfile_add(&file, &room, "Jump \"high\"\x01", "82001234+0063", CHEAT_FMT_AUTO, &at),
		 CHEATFILE_EDIT_OK);
	CHECK(text_is("cheats = 3\n"
		      "\n"
		      "cheat0_desc = \"Lives\"\n"
		      "cheat0_code = \"32001000 0009\"\n"
		      "cheat0_enable = false\n"
		      "\n"
		      "cheat1_desc = \"Money\"\n"
		      "cheat1_code = \"32001001 0009\"\n"
		      "cheat1_enable = false\n"
		      "\n"
		      "cheat2_desc = \"Jump 'high'\"\n"
		      "cheat2_code = \"82001234+0063\"\n"
		      "cheat2_enable = false\n"
		      "cheat2_user = true\n"));
	CHECK_EQ(at, 2);
	CHECK_EQ(file.count, 3);
	CHECK(file.edited);
	CHECK_EQ(cheats[2].own, 1);
	CHECK_EQ(cheats[1].own, 0);
	CHECK_EQ(cheats[2].format, CHEAT_FMT_AUTO);
	CHECK_EQ(cheats[1].picked, 1);
	CHECK_EQ(cheats[1].was, 0);
	CHECK_EQ(cheats[2].status, CHEATFILE_NEW);
	redecode();
	CHECK_EQ(cheats[2].status, CHEATFILE_OK);
	CHECK(cheatfile_toggle(&file, 2));
	char *out = written(9);
	CHECK(strstr(out, "cheat1_enable = true\n") != NULL);
	CHECK(strstr(out, "cheat2_enable = true\ncheat2_user = true\n") != NULL);
	free(out);

	// A CRLF file with no count line and no last line end, a format: the
	// count goes at the top
	load_cap("cheat0_code = \"32001000 0009\"\r\ncheat0_enable = true", 4096);
	CHECK_EQ(cheatfile_add(&file, &room, "Raw", "02001000 00000063", CHEAT_FMT_GSAV1_RAW, &at), CHEATFILE_EDIT_OK);
	CHECK(text_is("cheats = 2\r\n"
		      "\r\n"
		      "cheat0_code = \"32001000 0009\"\r\n"
		      "cheat0_enable = true\r\n"
		      "\r\n"
		      "cheat1_desc = \"Raw\"\r\n"
		      "cheat1_code = \"02001000 00000063\"\r\n"
		      "cheat1_enable = false\r\n"
		      "cheat1_format = \"GSAv1 raw\"\r\n"
		      "cheat1_user = true\r\n"));
	CHECK_EQ(at, 1);
	CHECK_EQ(cheats[0].picked, 1);
	CHECK_EQ(cheats[1].format, CHEAT_FMT_GSAV1_RAW);
	cheatfile_begin(&work);
	CHECK_EQ(cheatfile_next(&file, &work), 0);
	CHECK_EQ(cheatfile_next(&file, &work), 1);
	CHECK_EQ(cheats[1].status, CHEATFILE_OK);  // read raw, as the key says
	CHECK_EQ(cheats[1].detected, CHEAT_FMT_GSAV1_RAW);  // its own format, for the detail view
	CHECK_EQ(cheats[0].detected, CHEAT_FMT_CODEBREAKER);
	CHECK_EQ(work.ops[0].type, CHEAT_OP_WRITE);
	CHECK_EQ(work.ops[0].address, 0x02001000);
	CHECK_EQ(work.ops[0].value, 0x63);
	CHECK_EQ(work.ops[0].width, 1);

	// A new file: nothing yet
	load_cap("", 512);
	CHECK_EQ(cheatfile_add(&file, &room, "First", "02001000:63", CHEAT_FMT_VBA, &at), CHEATFILE_EDIT_OK);
	CHECK(text_is("cheats = 1\n"
		      "\n"
		      "cheat0_desc = \"First\"\n"
		      "cheat0_code = \"02001000:63\"\n"
		      "cheat0_enable = false\n"
		      "cheat0_format = \"VBA\"\n"
		      "cheat0_user = true\n"));
	CHECK_EQ(at, 0);
	CHECK_EQ(file.count, 1);

	// The number after the highest cheatN_ key, a code-less one's too; a
	// count that disagreed is set to it
	load_cap("cheats = 9\ncheat0_code = \"32001000 0009\"\ncheat4_note = \"x\"\n", 512);
	CHECK_EQ(cheatfile_add(&file, &room, "N", "32001000 0001", CHEAT_FMT_AUTO, &at), CHEATFILE_EDIT_OK);
	CHECK_EQ(cheats[at].number, 5);
	CHECK(!memcmp(file.text, "cheats = 6\n", 11));

	// Refused: a quote in a code, an empty one, a format past the last
	load_cap("", 512);
	CHECK_EQ(cheatfile_add(&file, &room, "Q", "3200\"1000", CHEAT_FMT_AUTO, &at), CHEATFILE_EDIT_BAD);
	CHECK_EQ(cheatfile_add(&file, &room, "Q", "", CHEAT_FMT_AUTO, &at), CHEATFILE_EDIT_BAD);
	CHECK_EQ(cheatfile_add(&file, &room, "Q", "32001000 0001", 9, &at), CHEATFILE_EDIT_BAD);
	CHECK_EQ(file.len, 0);
	CHECK(!file.edited);
}

void test_cheatfile_set_code(void)
{
	// A placeholder filled in, in place: only its code changes; a format
	// line goes after the code line, and a later AUTO takes it out
	static const char text[] = "cheats = 2\r\n"
				   "\r\n"
				   "cheat0_desc = \"Pick a level\"\r\n"
				   "cheat0_code = \"3300075C+00??\"\r\n"
				   "cheat0_enable = false\r\n"
				   "\r\n"
				   "cheat1_code = \"32001001 0009\"\r\n"
				   "cheat1_enable = true\r\n";
	load_cap(text, 4096);
	CHECK_EQ(cheats[0].status, CHEATFILE_PLACEHOLDER);
	CHECK(cheatfile_toggle(&file, 1));  // dropped, not saved yet
	CHECK_EQ(cheatfile_set_code(&file, &room, 0, NULL, "3300075C+0003", CHEAT_FMT_AUTO), CHEATFILE_EDIT_OK);
	CHECK(text_is("cheats = 2\r\n"
		      "\r\n"
		      "cheat0_desc = \"Pick a level\"\r\n"
		      "cheat0_code = \"3300075C+0003\"\r\n"
		      "cheat0_enable = false\r\n"
		      "\r\n"
		      "cheat1_code = \"32001001 0009\"\r\n"
		      "cheat1_enable = true\r\n"));
	CHECK_EQ(cheats[1].picked, 0);
	CHECK_EQ(cheats[1].was, 1);
	redecode();
	CHECK_EQ(cheats[0].status, CHEATFILE_OK);
	CHECK_EQ(cheats[0].own, 0);  // still the database's

	CHECK_EQ(cheatfile_set_code(&file, &room, 0, "Level 3", "02001000 00000063", CHEAT_FMT_GSAV1_RAW),
		 CHEATFILE_EDIT_OK);
	CHECK(text_is("cheats = 2\r\n"
		      "\r\n"
		      "cheat0_desc = \"Level 3\"\r\n"
		      "cheat0_code = \"02001000 00000063\"\r\n"
		      "cheat0_format = \"GSAv1 raw\"\r\n"
		      "cheat0_enable = false\r\n"
		      "\r\n"
		      "cheat1_code = \"32001001 0009\"\r\n"
		      "cheat1_enable = true\r\n"));
	CHECK_EQ(cheats[0].format, CHEAT_FMT_GSAV1_RAW);
	CHECK_EQ(cheatfile_set_code(&file, &room, 0, NULL, "3300075C+0004", CHEAT_FMT_CODEBREAKER), CHEATFILE_EDIT_OK);
	CHECK(!strcmp(edit_span(cheats[0].code, cheats[0].code_len), "3300075C+0004"));
	CHECK_EQ(cheats[0].format, CHEAT_FMT_CODEBREAKER);
	CHECK(text_has("cheat0_format = \"CodeBreaker\"\r\n"));
	CHECK_EQ(cheatfile_set_code(&file, &room, 0, NULL, "3300075C+0005", CHEAT_FMT_AUTO), CHEATFILE_EDIT_OK);
	CHECK(text_is("cheats = 2\r\n"
		      "\r\n"
		      "cheat0_desc = \"Level 3\"\r\n"
		      "cheat0_code = \"3300075C+0005\"\r\n"
		      "cheat0_enable = false\r\n"
		      "\r\n"
		      "cheat1_code = \"32001001 0009\"\r\n"
		      "cheat1_enable = true\r\n"));

	// A cheat with no desc gets one before its code; the last line, with no
	// end, gets one before the format line after it
	load_cap("cheat0_code = \"32001000 0009\"", 512);
	CHECK_EQ(cheatfile_set_code(&file, &room, 0, "Named", "32001000 0001", CHEAT_FMT_CODEBREAKER),
		 CHEATFILE_EDIT_OK);
	CHECK(text_is("cheat0_desc = \"Named\"\n"
		      "cheat0_code = \"32001000 0001\"\n"
		      "cheat0_format = \"CodeBreaker\"\n"));
	CHECK_EQ(cheatfile_set_code(&file, &room, 1, NULL, "32001000 0001", 0), CHEATFILE_EDIT_BAD);
}

void test_cheatfile_remove(void)
{
	// Cheat 1 of 12 goes, every line of its own (an unknown key too) and the
	// blank line before them; the ones after are numbered one lower (10 is
	// 9 now), their picks kept; the count follows
	static char text[4096];
	char *p = text;
	p += sprintf(p, "# mine\ncheats = 12\n\n");
	for (u32 i = 0; i < 12; i++)
		p += sprintf(p, "cheat%u_desc = \"C%u\"\ncheat%u_code = \"3200100%X 0001\"\ncheat%u_enable = %s\n%s\n",
			     (unsigned)i, (unsigned)i, (unsigned)i, (unsigned)(i & 15), (unsigned)i,
			     i == 11 ? "true" : "false", i == 1 ? "cheat1_note = \"gone too\"\n" : "");
	load_cap(text, sizeof(text));
	CHECK_EQ(file.count, 12);
	CHECK(cheatfile_toggle(&file, 10));
	CHECK_EQ(cheatfile_remove(&file, &room, 1), CHEATFILE_EDIT_OK);
	CHECK_EQ(file.count, 11);
	CHECK(!strcmp(edit_span(cheats[1].desc, cheats[1].desc_len), "C2"));
	CHECK_EQ(cheats[1].number, 1);
	CHECK_EQ(cheats[9].number, 9);
	CHECK(!strcmp(edit_span(cheats[9].desc, cheats[9].desc_len), "C10"));
	CHECK_EQ(cheats[9].picked, 1);  // C10's pick, not saved yet
	CHECK_EQ(cheats[9].was, 0);
	CHECK_EQ(cheats[10].picked, 1);  // C11's, saved
	CHECK_EQ(cheats[10].was, 1);
	CHECK(!text_has("gone too"));
	CHECK(!memcmp(file.text, "# mine\ncheats = 11\n\ncheat0_desc = \"C0\"\n", 39));
	CHECK(text_has("cheat0_enable = false\n\ncheat1_desc = \"C2\"\n"));
	CHECK(text_has("cheat9_desc = \"C10\"\ncheat9_code"));
	CHECK(text_has("cheat10_desc = \"C11\"\n"));
	CHECK(!text_has("cheat11_"));
	redecode();
	CHECK_EQ(cheats[9].status, CHEATFILE_OK);

	// The last cheat, CRLF, no blank line before it and no line end after
	load_cap("cheats = 2\r\ncheat0_code = \"32001000 0009\"\r\ncheat1_code = \"32001001 0009\"\r\ncheat1_user = "
		 "true",
		 512);
	CHECK_EQ(cheats[1].own, 1);
	CHECK_EQ(cheatfile_remove(&file, &room, 1), CHEATFILE_EDIT_OK);
	CHECK(text_is("cheats = 1\r\ncheat0_code = \"32001000 0009\"\r\n"));
	// And the only one: the count says so
	CHECK_EQ(cheatfile_remove(&file, &room, 0), CHEATFILE_EDIT_OK);
	CHECK(text_is("cheats = 0\r\n"));
	CHECK_EQ(file.count, 0);
	CHECK(cheatfile_empty(&file));  // nothing left worth a file
	load_cap("# for crc32 0123ABCD\n\ncheats = 0\n\n", 512);
	CHECK(cheatfile_empty(&file));
	load_cap("# my notes\ncheats = 0\n", 512);
	CHECK(!cheatfile_empty(&file));  // the user's comment keeps it
	load_cap("cheat3_note = \"x\"\n", 512);
	CHECK(!cheatfile_empty(&file));
	load_cap("cheat0_code = \"32001000 0009\"\n", 512);
	CHECK(!cheatfile_empty(&file));
	load_cap("", 512);
	CHECK_EQ(cheatfile_remove(&file, &room, 0), CHEATFILE_EDIT_BAD);
}

void test_cheatfile_edit_limits(void)
{
	u32 at;
	// Cheat 1023 is the table's last: no number is left for a new one
	load_cap("cheat1023_code = \"32001000 0001\"\n", 512);
	CHECK_EQ(file.next, 1024);
	CHECK_EQ(cheatfile_add(&file, &room, "N", "32001000 0001", CHEAT_FMT_AUTO, &at), CHEATFILE_EDIT_FULL);
	CHECK_EQ(cheatfile_can_add(&file, 512, "Code 1"), CHEATFILE_EDIT_FULL);  // told before any typing
	load_cap("cheat1022_code = \"32001000 0001\"\n", 512);
	CHECK_EQ(cheatfile_can_add(&file, 512, "Code 1"), CHEATFILE_EDIT_OK);
	CHECK_EQ(cheatfile_add(&file, &room, "N", "32001000 0001", CHEAT_FMT_AUTO, &at), CHEATFILE_EDIT_OK);
	CHECK_EQ(cheats[at].number, 1023);

	// The buffer's room
	load_cap("cheat0_code = \"32001000 0001\"\n", 40);
	CHECK_EQ(cheatfile_can_add(&file, 40, "N"), CHEATFILE_EDIT_LONG);
	CHECK_EQ(cheatfile_can_add(&file, 512, "N"), CHEATFILE_EDIT_OK);
	CHECK_EQ(cheatfile_add(&file, &room, "N", "32001000 0001", CHEAT_FMT_AUTO, &at), CHEATFILE_EDIT_LONG);
	CHECK_EQ(file.len, 30);
	CHECK_EQ(cheatfile_set_code(&file, &room, 0, NULL, "32001000 0001+32001002 0001", 0), CHEATFILE_EDIT_LONG);
	CHECK_EQ(cheatfile_set_code(&file, &room, 0, NULL, "32001000 0002", 0), CHEATFILE_EDIT_OK);

	// A code that grows past a desc that shrinks more: the code goes in
	// first, so the buffer needs the growth before the desc gives room back
	const char *grow = "cheat0_desc = \"AAAAAAAAAAAAAAAAAAAA\"\ncheat0_code = \"32001000 0001\"\n";
	u32 grow_len = (u32)strlen(grow);
	load_cap(grow, grow_len);
	CHECK_EQ(cheatfile_set_code(&file, &room, 0, "B", "32001000 0001+32001002 0001", 0), CHEATFILE_EDIT_LONG);
	CHECK(!file.edited);
	load_cap(grow, grow_len + 14);
	CHECK_EQ(cheatfile_set_code(&file, &room, 0, "B", "32001000 0001+32001002 0001", 0), CHEATFILE_EDIT_OK);
	CHECK_EQ(file.len, grow_len - 5);

	// The file's cap, with the picks written in
	u32 big = CHEATFILE_TEXT_MAX + 512;
	char *t = malloc(big);
	memset(t, '#', CHEATFILE_TEXT_MAX - 80);
	t[0] = '\n';
	snprintf(t + CHEATFILE_TEXT_MAX - 80, 81, "\ncheat0_code = \"32001000 0001\"\n");
	load_cap(t, big);
	CHECK_EQ(file.count, 1);
	CHECK(cheatfile_toggle(&file, 0));  // its enable line takes 22 of the bytes left
	CHECK(CHEATFILE_TEXT_MAX - cheatfile_text_len(&file) < 100);
	CHECK_EQ(cheatfile_can_add(&file, big, "Code 1"), CHEATFILE_EDIT_LONG);
	CHECK_EQ(cheatfile_add(&file, &room, "N", "32001000 0001", CHEAT_FMT_AUTO, &at), CHEATFILE_EDIT_LONG);
	CHECK(!file.edited);
	free(t);
	free(edit_buf);
	edit_buf = NULL;
}

// Texts past 64 KB (offsets past 16 bits): 1,000 cheats in about 110 KB,
// read, picked, written back and edited far from the start
void test_cheatfile_big(void)
{
	u32 cap = CHEATFILE_TEXT_MAX, at = 0;
	char *text = malloc(cap), *p = text;
	p += sprintf(p, "cheats = 1000\n\n");
	for (u32 i = 0; i < 1000; i++)
		p += sprintf(p,
			     "cheat%u_desc = \"Cheat %u, a name long enough to pad the file\"\ncheat%u_code = "
			     "\"3200%04X 0001\"\ncheat%u_enable = false\n\n",
			     (unsigned)i, (unsigned)i, (unsigned)i, (unsigned)(i & 0xFFF), (unsigned)i);
	*p = 0;
	CHECK(p - text > 0x18000);
	load_cap(text, cap);
	CHECK_EQ(file.count, 1000);
	CHECK(cheats[999].code > 0xFFFF);
	CHECK(!strcmp(edit_span(cheats[999].code, cheats[999].code_len), "320003E7 0001"));
	CHECK(!strcmp(edit_span(cheats[999].desc, cheats[999].desc_len),
		      "Cheat 999, a name long enough to pad the file"));
	CHECK_EQ(cheats[999].status, CHEATFILE_OK);
	CHECK(cheatfile_toggle(&file, 999));
	char *out = written(511);
	CHECK(strstr(out, "cheat999_enable = true\n") != NULL);
	CHECK(strstr(out, "cheat998_enable = false\n") != NULL);
	free(out);

	// Edits far into the text: a code changed, a cheat removed, one added
	CHECK_EQ(cheatfile_set_code(&file, &room, 900, "Nine hundred", "32001900 0002", CHEAT_FMT_AUTO),
		 CHEATFILE_EDIT_OK);
	CHECK(!strcmp(edit_span(cheats[900].code, cheats[900].code_len), "32001900 0002"));
	CHECK(!strcmp(edit_span(cheats[900].desc, cheats[900].desc_len), "Nine hundred"));
	CHECK(!strcmp(edit_span(cheats[901].desc, cheats[901].desc_len),
		      "Cheat 901, a name long enough to pad the file"));
	CHECK_EQ(cheatfile_remove(&file, &room, 10), CHEATFILE_EDIT_OK);
	CHECK_EQ(file.count, 999);
	CHECK_EQ(cheats[998].number, 998);
	CHECK(!strcmp(edit_span(cheats[998].desc, cheats[998].desc_len),
		      "Cheat 999, a name long enough to pad the file"));
	CHECK_EQ(cheats[998].picked, 1);  // cheat 999's pick, now 998's
	CHECK(text_has("cheat899_desc = \"Nine hundred\"\ncheat899_code = \"32001900 0002\"\n"));
	CHECK(!memcmp(file.text, "cheats = 999\n\n", 14));
	CHECK_EQ(cheatfile_add(&file, &room, "Last", "32001000 0003", CHEAT_FMT_AUTO, &at), CHEATFILE_EDIT_OK);
	CHECK_EQ(at, 999);
	CHECK_EQ(cheats[at].number, 999);
	CHECK(!memcmp(file.text, "cheats = 1000\n\n", 15));
	CHECK(text_has("cheat999_desc = \"Last\"\ncheat999_code = \"32001000 0003\"\n"));
	redecode();
	CHECK_EQ(cheats[999].status, CHEATFILE_OK);
	free(text);

	// A value over 65,535 bytes isn't read: its line counts for nothing
	u32 long_len = 70000;
	char *lng = malloc(long_len + 64);
	p = lng + sprintf(lng, "cheat0_code = \"");
	memset(p, 'q', long_len);
	p += long_len;
	sprintf(p, "\"\ncheat1_code = \"32001000 0001\"\n");
	load(lng, (u32)strlen(lng));
	CHECK_EQ(file.count, 1);
	CHECK_EQ(cheats[0].number, 1);
	free(lng);
	free(edit_buf);
	edit_buf = NULL;
}

static int rom_crc(const char *text, u32 *crc)
{
	*crc = 0;
	return cheatfile_rom_crc(text, (u32)strlen(text), crc);
}

void test_cheatfile_rom_crc(void)
{
	u32 crc;
	CHECK(rom_crc("# for crc32 DD88761C\ncheats = 0\n", &crc));
	CHECK_EQ(crc, 0xDD88761C);
	// Lower case, CRLF, a BOM, spaces around, and not on the first line
	CHECK(rom_crc("\xEF\xBB\xBF# GameHacking.org\r\n  # for crc32 84ee4776 \r\ncheats = 0\r\n", &crc));
	CHECK_EQ(crc, 0x84EE4776);
	// The first such line counts
	CHECK(rom_crc("# for crc32 00000001\n# for crc32 00000002\n", &crc));
	CHECK_EQ(crc, 1);
	// Without the line, or a broken one: for any dump
	CHECK(!rom_crc("cheats = 0\n", &crc));
	CHECK(!rom_crc("# for crc32 DD88761\n", &crc));
	CHECK(!rom_crc("# for crc32 DD88761CX\n", &crc));
	CHECK(!rom_crc("# for crc32 DD8876G1\n", &crc));
	CHECK(!rom_crc("#for crc32 DD88761C\n", &crc));
	CHECK(!rom_crc("cheat0_desc = \"# for crc32 DD88761C\"\n", &crc));
	CHECK(!rom_crc("", &crc));
	// The end of the text ends the line
	CHECK(cheatfile_rom_crc("# for crc32 DD88761Cjunk", 20, &crc));
	CHECK_EQ(crc, 0xDD88761C);
}

// A window of a larger file (cheatstore.h): its cheats keep their numbers,
// table entries counting from its base; a pick, an add, a remove and an
// edit work there as in a whole file, but its count line stays as it is
void test_cheatfile_part(void)
{
	u32 at = 99;
	load_cap("cheat5000_desc = \"A\"\n"
		 "cheat5000_code = \"32001000 0009\"\n"
		 "cheat5000_enable = true\n"
		 "\n"
		 "cheat5001_desc = \"B\"\n"
		 "cheat5001_code = \"32001001 0009\"\n"
		 "cheat5001_enable = false\n"
		 "\n"
		 "cheat5002_desc = \"C\"\n"
		 "cheat5002_code = \"32001002 0009\"\n"
		 "cheat5002_enable = false\n"
		 "cheat7_code = \"32001003 0009\"\n",  // not this window's: dropped
		 4096);
	cheatfile_parse_part(&file, edit_buf, file.len, cheats, CHEATFILE_MAX, 5000, 1);
	redecode();
	CHECK_EQ(file.count, 3);
	CHECK_EQ(file.dropped, 1);
	CHECK_EQ(cheats[0].number, 5000);
	CHECK_EQ(cheats[2].number, 5002);
	CHECK_EQ(file.next, 5003);
	CHECK_EQ(cheats[0].picked, 1);
	CHECK_EQ(cheats[1].status, CHEATFILE_OK);
	CHECK(cheatfile_toggle(&file, 2));
	CHECK_EQ(cheatfile_add(&file, &room, "D", "32001004 0009", CHEAT_FMT_AUTO, &at), CHEATFILE_EDIT_OK);
	CHECK_EQ(at, 3);
	CHECK_EQ(cheats[3].number, 5003);
	CHECK(!text_has("cheats ="));  // no count line added to a window
	CHECK_EQ(cheats[2].picked, 1);  // the pick kept over the read again
	CHECK_EQ(cheatfile_remove(&file, &room, 1), CHEATFILE_EDIT_OK);
	CHECK_EQ(file.count, 3);
	CHECK_EQ(cheats[1].number, 5001);  // C, numbered one lower
	CHECK(!strcmp(edit_span(cheats[1].desc, cheats[1].desc_len), "C"));
	CHECK_EQ(cheats[0].picked, 1);
	CHECK_EQ(cheats[1].picked, 1);
	CHECK(text_has("cheat5002_code = \"32001004 0009\""));
	// The first window's count line, which counts the whole file, stays
	load_cap("cheats = 9000\n\ncheat0_code = \"32001000 0009\"\n", 4096);
	cheatfile_parse_part(&file, edit_buf, file.len, cheats, CHEATFILE_MAX, 0, 1);
	CHECK_EQ(cheatfile_add(&file, &room, "E", "32001005 0009", CHEAT_FMT_AUTO, &at), CHEATFILE_EDIT_OK);
	CHECK(text_has("cheats = 9000\n"));

	// A line at a time, as the windowed reader sees them
	u32 n = 0;
	int on = 0;
	CHECK(cheatfile_line_key("cheat12345_enable = true", 24, &n, &on));
	CHECK_EQ(n, 12345);
	CHECK(on);
	CHECK(cheatfile_line_key("  cheat3_code = \"x\"", 19, &n, &on));
	CHECK_EQ(n, 3);
	CHECK(!on);
	CHECK(cheatfile_line_key("cheat4_note = x", 15, &n, &on));
	CHECK(!cheatfile_line_key("cheats = 4", 10, &n, &on));
	CHECK(!cheatfile_line_key("# cheat5_code = x", 17, &n, &on));
	CHECK(!cheatfile_line_key("", 0, &n, &on));
}

// Several cheats removed in one pass: their lines go, with a blank line
// before each, the rest numbered to close the gaps, every pick kept
void test_cheatfile_remove_many(void)
{
	load_cap("cheats = 5\n"
		 "\n"
		 "cheat0_desc = \"A\"\n"
		 "cheat0_code = \"32001000 0009\"\n"
		 "cheat0_enable = true\n"
		 "\n"
		 "cheat1_desc = \"B\"\n"
		 "cheat1_code = \"ZZZZ\"\n"
		 "\n"
		 "cheat2_desc = \"C\"\n"
		 "cheat2_code = \"32001002 0009\"\n"
		 "\n"
		 "cheat3_desc = \"D\"\n"
		 "cheat3_code = \"72001000 0001\"\n"
		 "\n"
		 "cheat4_desc = \"E\"\n"
		 "cheat4_code = \"32001004 0009\"\n"
		 "cheat4_enable = true\n",
		 4096);
	CHECK(cheatfile_toggle(&file, 2));  // a pick not saved yet stays too
	u8 drop[CHEATFILE_MAX / 8] = {0};
	drop[0] = 1 << 1 | 1 << 3;
	CHECK_EQ(cheatfile_remove_many(&file, &room, drop), 2);
	CHECK(text_is("cheats = 3\n"
		      "\n"
		      "cheat0_desc = \"A\"\n"
		      "cheat0_code = \"32001000 0009\"\n"
		      "cheat0_enable = true\n"
		      "\n"
		      "cheat1_desc = \"C\"\n"
		      "cheat1_code = \"32001002 0009\"\n"
		      "\n"
		      "cheat2_desc = \"E\"\n"
		      "cheat2_code = \"32001004 0009\"\n"
		      "cheat2_enable = true\n"));
	CHECK_EQ(file.count, 3);
	CHECK_EQ(cheats[0].picked, 1);
	CHECK_EQ(cheats[1].picked, 1);
	CHECK_EQ(cheats[2].picked, 1);
	CHECK(cheatfile_changed(&file));
	u8 none[CHEATFILE_MAX / 8] = {0};
	CHECK_EQ(cheatfile_remove_many(&file, &room, none), 0);
}

// Rename: a database cheat's desc replaced and a renamed line added once; a
// cheat with no desc gets one; the user's own code gets no renamed line
void test_cheatfile_rename(void)
{
	load_cap("cheats = 3\n"
		 "\n"
		 "cheat0_desc = \"Max HP\"\n"
		 "cheat0_code = \"32001000 0009\"\n"
		 "cheat0_enable = true\n"
		 "\n"
		 "cheat1_code = \"32001002 0009\"\n"
		 "\n"
		 "cheat2_desc = \"Mine\"\n"
		 "cheat2_code = \"32001004 0009\"\n"
		 "cheat2_user = true\n",
		 4096);
	CHECK_EQ(cheatfile_rename(&file, &room, 0, "Lots of HP"), CHEATFILE_EDIT_OK);
	CHECK_EQ(cheats[0].renamed, 1);
	CHECK_EQ(cheats[0].picked, 1);
	CHECK_EQ(cheatfile_rename(&file, &room, 0, "HP"), CHEATFILE_EDIT_OK);
	CHECK_EQ(cheatfile_rename(&file, &room, 1, "Money"), CHEATFILE_EDIT_OK);
	CHECK_EQ(cheatfile_rename(&file, &room, 2, "Still mine"), CHEATFILE_EDIT_OK);
	CHECK_EQ(cheatfile_rename(&file, &room, 0, ""), CHEATFILE_EDIT_BAD);
	CHECK_EQ(cheatfile_rename(&file, &room, 3, "No such"), CHEATFILE_EDIT_BAD);
	CHECK(text_is("cheats = 3\n"
		      "\n"
		      "cheat0_desc = \"HP\"\n"
		      "cheat0_code = \"32001000 0009\"\n"
		      "cheat0_renamed = true\n"
		      "cheat0_enable = true\n"
		      "\n"
		      "cheat1_desc = \"Money\"\n"
		      "cheat1_code = \"32001002 0009\"\n"
		      "cheat1_renamed = true\n"
		      "\n"
		      "cheat2_desc = \"Still mine\"\n"
		      "cheat2_code = \"32001004 0009\"\n"
		      "cheat2_user = true\n"));
	CHECK_EQ(cheats[1].renamed, 1);
	CHECK_EQ(cheats[2].renamed, 0);
}
