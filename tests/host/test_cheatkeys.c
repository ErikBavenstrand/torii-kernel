// How a cheat is activated (source/cheatkeys.h): cheatN_keys' values read
// and written, what a decoded code implies, and the text screens show.
#include <string.h>

#include "../../source/cheatkeys.h"
#include "../../source/lang.h"
#include "../kernel/torii_test.h"
#include "tests.h"

#define CHECK_STR(got, want) CHECK(strcmp((got), (want)) == 0)

#define KEY_A 0x001
#define KEY_B 0x002
#define KEY_SELECT 0x004
#define KEY_START 0x008
#define KEY_RIGHT 0x010
#define KEY_LEFT 0x020
#define KEY_UP 0x040
#define KEY_DOWN 0x080
#define KEY_R 0x100
#define KEY_L 0x200

static u16 parse(const char *s)
{
	return cheatkeys_parse(s, (u32)strlen(s));
}

// v as the file writes it, read back as v
static const char *format(u16 v)
{
	static char out[CHEATKEYS_TEXT_MAX];
	u32 n = cheatkeys_format(v, out, sizeof(out));
	CHECK_EQ(n, (u32)strlen(out));
	if (n)
		CHECK_EQ(parse(out), v);
	return out;
}

void test_cheatkeys_parse(void)
{
	CHECK_EQ(parse("always"), CHEATKEYS_ALWAYS_ON);
	CHECK_EQ(parse("  Always "), CHEATKEYS_ALWAYS_ON);
	CHECK_EQ(parse("LAUNCH"), CHEATKEYS_AT_LAUNCH);
	CHECK_EQ(parse("hold L"), CHEATKEYS(CHEATKEYS_HOLD, KEY_L));
	CHECK_EQ(parse("hold l+select"), CHEATKEYS(CHEATKEYS_HOLD, KEY_L | KEY_SELECT));
	CHECK_EQ(parse("Press SELECT + Up"), CHEATKEYS(CHEATKEYS_PRESS, KEY_SELECT | KEY_UP));
	CHECK_EQ(parse("hold A+B+Select+Start+Up+Down+Left+Right+L+R"), CHEATKEYS(CHEATKEYS_HOLD, 0x3FF));
	CHECK_EQ(parse("hold L+L"), CHEATKEYS(CHEATKEYS_HOLD, KEY_L));
	// Anything else is not set
	CHECK_EQ(parse(""), 0);
	CHECK_EQ(parse("hold"), 0);
	CHECK_EQ(parse("hold "), 0);
	CHECK_EQ(parse("hold+L"), 0);
	CHECK_EQ(parse("hold L+"), 0);
	CHECK_EQ(parse("hold L++R"), 0);
	CHECK_EQ(parse("hold X"), 0);
	CHECK_EQ(parse("hold Sel"), 0);
	CHECK_EQ(parse("always L"), 0);
	CHECK_EQ(parse("launch+R"), 0);
	CHECK_EQ(parse("tap A"), 0);
	CHECK_EQ(parse("hold L R"), 0);
	CHECK_EQ(parse("alwaysx"), 0);
	CHECK_EQ(cheatkeys_parse("hold Lx", 6), CHEATKEYS(CHEATKEYS_HOLD, KEY_L));  // len bytes only

	// Written back canonically, buttons in screen order
	CHECK_STR(format(CHEATKEYS_ALWAYS_ON), "always");
	CHECK_STR(format(CHEATKEYS_AT_LAUNCH), "launch");
	CHECK_STR(format(parse("hold select+l")), "hold L+Select");
	CHECK_STR(format(parse("press up+select")), "press Select+Up");
	CHECK_STR(format(CHEATKEYS(CHEATKEYS_HOLD, 0x3FF)), "hold L+R+Select+Start+A+B+Up+Down+Left+Right");
	CHECK_STR(format(0), "");
	CHECK_STR(format(CHEATKEYS(CHEATKEYS_HOLD, 0)), "");     // invalid: no buttons
	CHECK_STR(format(CHEATKEYS(CHEATKEYS_ALWAYS, 1)), "");   // invalid: buttons
	CHECK_STR(format((u16)(CHEATKEYS_ALWAYS_ON | 0x400)), "");  // unused bits
	CHECK_STR(format(CHEATKEYS(6, 0)), "");                  // no such mode
	char small[8];
	CHECK_EQ(cheatkeys_format(parse("hold L+Select"), small, sizeof(small)), 7);
	CHECK_STR(small, "hold L+");

	CHECK(cheatkeys_valid(0));
	CHECK(cheatkeys_valid(CHEATKEYS(CHEATKEYS_PRESS, KEY_A)));
	CHECK(!cheatkeys_valid(CHEATKEYS(CHEATKEYS_PRESS, 0)));
	CHECK(!cheatkeys_valid(CHEATKEYS(CHEATKEYS_LAUNCH, KEY_A)));
}

// What code (format fmt) implies, decoded and lowered as cheatfile.c does
static u16 derive(u8 fmt, const char *code)
{
	static struct cheat_op ops[64];
	static u32 prog[256];
	static struct cheat_patch patches[64];
	struct cheat_decoder d;
	u32 n = 0, words = 0, cost = 0, n_patches = 0;
	cheat_decoder_force(&d, fmt);
	CHECK_EQ(cheat_decode(&d, code, (u32)strlen(code), ops, 64, &n), CHEAT_OK);
	CHECK_EQ(cheat_lower(ops, n, prog, 256, &words, &cost, patches, 64, &n_patches), CHEAT_LOWER_OK);
	return cheatkeys_derive(ops, n, n_patches != 0, words != 0);
}

void test_cheatkeys_derive(void)
{
	// CodeBreaker's key test: KEYINPUT equal to 0x3FB is SELECT alone held
	CHECK_EQ(derive(CHEAT_FMT_AUTO, "74000130 03FB+32001000 0063"), CHEATKEYS(CHEATKEYS_HOLD, KEY_SELECT));
	CHECK_EQ(derive(CHEAT_FMT_AUTO, "74000130 01FB+32001000 0063"), CHEATKEYS(CHEATKEYS_HOLD, KEY_L | KEY_SELECT));
	// CodeBreaker's IF_NAND: SELECT held, with any other key
	CHECK_EQ(derive(CHEAT_FMT_AUTO, "D0000020 0004+32001000 0063"), CHEATKEYS(CHEATKEYS_HOLD, KEY_SELECT));
	// The same combination twice is one; two differ: unknown
	CHECK_EQ(derive(CHEAT_FMT_AUTO, "D0000020 0004+32001000 0063+74000130 03FB+32001002 0001"),
		 CHEATKEYS(CHEATKEYS_HOLD, KEY_SELECT));
	CHECK_EQ(derive(CHEAT_FMT_AUTO, "74000130 03FB+32001000 0063+74000130 03FE+32001002 0001"), 0);
	// The device's button: R+SELECT
	CHECK_EQ(derive(CHEAT_FMT_PARV3_RAW, "00000000 10300000+00000001 00000000"),
		 CHEATKEYS(CHEATKEYS_HOLD, KEY_R | KEY_SELECT));
	// No condition on the keys: always on, game-state conditions too
	CHECK_EQ(derive(CHEAT_FMT_AUTO, "32001000 0063"), CHEATKEYS_ALWAYS_ON);
	CHECK_EQ(derive(CHEAT_FMT_AUTO, "72001000 0001+32001002 0001"), CHEATKEYS_ALWAYS_ON);
	CHECK_EQ(derive(CHEAT_FMT_VBA, "02001000:AB"), CHEATKEYS_ALWAYS_ON);
	// ROM patches only: at launch; with a write that runs in game, always
	CHECK_EQ(derive(CHEAT_FMT_VBA, "08001000:4770"), CHEATKEYS_AT_LAUNCH);
	CHECK_EQ(derive(CHEAT_FMT_VBA, "08001000:4770+02001000:AB"), CHEATKEYS_ALWAYS_ON);

	// Other conditions on KEYINPUT, and ones that need nothing held, aren't read
	struct cheat_op ops[2];
	memset(ops, 0, sizeof(ops));
	ops[0] = (struct cheat_op){
		.address = 0x04000130, .value = 0x3FB, .count = 1, .type = CHEAT_OP_IF_NE, .width = 2};
	ops[1] = (struct cheat_op){.address = 0x02001000, .value = 1, .count = 1, .type = CHEAT_OP_WRITE, .width = 1};
	CHECK_EQ(cheatkeys_derive(ops, 2, 0, 1), 0);
	ops[0].type = CHEAT_OP_IF_EQ;
	ops[0].value = 0x3FF;
	CHECK_EQ(cheatkeys_derive(ops, 2, 0, 1), 0);
	ops[0].value = 0x3FE;
	ops[0].else_count = 1;
	CHECK_EQ(cheatkeys_derive(ops, 2, 0, 1), 0);
	ops[0] = (struct cheat_op){
		.address = 0x04000131, .value = 0x02, .count = 1, .type = CHEAT_OP_IF_NAND, .width = 1};
	CHECK_EQ(cheatkeys_derive(ops, 2, 0, 1), CHEATKEYS(CHEATKEYS_HOLD, KEY_L));
	ops[0].type = CHEAT_OP_IF_EQ;
	CHECK_EQ(cheatkeys_derive(ops, 2, 0, 1), 0);
	CHECK_EQ(cheatkeys_derive(ops, 0, 0, 0), 0);
}

void test_cheatkeys_text(void)
{
	u16 v = parse("hold select+l");
	CHECK_EQ(cheatkeys_count(v), 2);
	CHECK_EQ(cheatkeys_first(v), CHEATKEYS_L);
	CHECK_EQ(cheatkeys_next(v, CHEATKEYS_L), CHEATKEYS_SELECT);
	CHECK_EQ(cheatkeys_next(v, CHEATKEYS_SELECT), -1);
	CHECK_EQ(cheatkeys_count(CHEATKEYS_ALWAYS_ON), 0);
	CHECK_EQ(cheatkeys_first(CHEATKEYS_ALWAYS_ON), -1);
	CHECK_EQ(cheatkeys_count(CHEATKEYS(CHEATKEYS_HOLD, 0x3FF)), 10);
	for (u32 b = 0; b < CHEATKEYS_BUTTON_COUNT; b++)
		CHECK_EQ(cheatkeys_next(CHEATKEYS(CHEATKEYS_HOLD, cheatkeys_bit(b)), -1), (s32)b);
	CHECK_EQ(cheatkeys_bit(CHEATKEYS_L), KEY_L);
	CHECK_EQ(cheatkeys_bit(CHEATKEYS_RIGHT), KEY_RIGHT);

	char out[64];
	cheatkeys_sentence(v, out, sizeof(out));
	CHECK_STR(out, "Hold L+SELECT");
	cheatkeys_sentence(parse("press up+select"), out, sizeof(out));
	CHECK_STR(out, "Press SELECT+Up");
	cheatkeys_sentence(CHEATKEYS_ALWAYS_ON, out, sizeof(out));
	CHECK_STR(out, "Always on");
	cheatkeys_sentence(CHEATKEYS_AT_LAUNCH, out, sizeof(out));
	CHECK_STR(out, "Set when the game starts");
	CHECK_EQ(cheatkeys_sentence(0, out, sizeof(out)), 0);
	CHECK_STR(out, "");
	CHECK_EQ(cheatkeys_names(v, out, sizeof(out)), 8);
	CHECK_STR(out, "L+SELECT");
	cheatkeys_names(CHEATKEYS(CHEATKEYS_HOLD, 0x3FF), out, 12);
	CHECK_STR(out, "L+R+SELECT+");

	cheatkeys_tag(parse("hold select"), out, sizeof(out));
	CHECK_STR(out, "SEL");
	cheatkeys_tag(v, out, sizeof(out));
	CHECK_STR(out, "L+1");
	cheatkeys_tag(parse("press a+b+r"), out, sizeof(out));
	CHECK_STR(out, "R+2");
	CHECK_EQ(cheatkeys_tag(CHEATKEYS_ALWAYS_ON, out, sizeof(out)), 0);
	CHECK_EQ(cheatkeys_tag(CHEATKEYS_AT_LAUNCH, out, sizeof(out)), 0);
}
