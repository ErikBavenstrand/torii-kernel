// The cheat decoders and lowering (source/cheatcode.c, cheatlower.c). Codes
// are heap copies without a NUL, so AddressSanitizer catches a read past len.
// mGBA's own test vectors (src/gba/test/cheats.c) run through a reference
// interpreter below that follows mGBA's mCheatRefresh, so they check what the
// ops mean, not only their fields. Encrypted vectors are real codes from
// libretro's database whose decoding matched mGBA's (just cheat-corpus).
#include <stdlib.h>
#include <string.h>

#include "../../source/cheatcode.h"
#include "../kernel/torii_test.h"
#include "tests.h"

#define MAX_OPS 64

static struct cheat_op ops[MAX_OPS];
static u32 n_ops;

static u8 decode_n(struct cheat_decoder *d, const char *text, u32 ops_max)
{
	u32 len = strlen(text);
	char *code = malloc(len ? len : 1);
	memcpy(code, text, len);
	memset(ops, 0xA5, sizeof(ops));
	u8 status = cheat_decode(d, code, len, ops, ops_max, &n_ops);
	free(code);
	return status;
}

static u8 decode(struct cheat_decoder *d, const char *text)
{
	return decode_n(d, text, MAX_OPS);
}

// A fresh decoder of the given format
static u8 decode_as(u8 format, const char *text)
{
	struct cheat_decoder d;
	cheat_decoder_force(&d, format);
	return decode(&d, text);
}

#define CHECK_OP(i, t, w, addr, val, cnt) \
	do { \
		CHECK_EQ(ops[i].type, t); \
		CHECK_EQ(ops[i].width, w); \
		CHECK_EQ(ops[i].address, addr); \
		CHECK_EQ(ops[i].value, val); \
		CHECK_EQ(ops[i].count, cnt); \
	} while (0)

// ------------------------------------------------------------------------
// A reference interpreter, mCheatRefresh's loop over Torii's ops, on 64 KB of
// IWRAM at 0x03000000 and KEYINPUT

static u8 iwram[0x10000];
static u16 keyinput;
static int button;

static u32 rd(u32 address, u32 width)
{
	u32 value = 0, b;
	if (address == 0x04000130)
		return keyinput;
	for (b = 0; b < width; ++b)
		value |= (u32)iwram[(address + b - 0x03000000) & 0xFFFF] << (8 * b);
	return value;
}

static void wr(u32 address, u32 width, u32 value)
{
	u32 b;
	for (b = 0; b < width; ++b)
		iwram[(address + b - 0x03000000) & 0xFFFF] = value >> (8 * b);
}

static void run(void)
{
	u32 i, k, else_loc = 0, end_loc = 0;
	for (i = 0; i < n_ops; ++i) {
		const struct cheat_op *op = &ops[i];
		u32 address = op->address, value = op->value, r;
		int cond = 1, conditional = 1;
		switch (op->type) {
		case CHEAT_OP_WRITE:
			for (k = 0; k < op->count; ++k) {
				wr(address, op->width, value);
				address += op->addr_step;
				value += op->value_step;
			}
			conditional = 0;
			break;
		case CHEAT_OP_WRITE_PTR:
			wr(rd(address, 4) + op->addr_step, op->width, value);
			conditional = 0;
			break;
		case CHEAT_OP_ADD:
			wr(address, op->width, rd(address, op->width) + value);
			conditional = 0;
			break;
		case CHEAT_OP_OR:
			wr(address, op->width, rd(address, op->width) | value);
			conditional = 0;
			break;
		case CHEAT_OP_AND:
			wr(address, op->width, rd(address, op->width) & value);
			conditional = 0;
			break;
		case CHEAT_OP_ROM_PATCH:
		case CHEAT_OP_SLOWDOWN:
			conditional = 0;
			break;
		default:
			r = op->width ? rd(address, op->width) : 0;
			switch (op->type) {
			case CHEAT_OP_IF_EQ:
				cond = r == value;
				break;
			case CHEAT_OP_IF_NE:
				cond = r != value;
				break;
			case CHEAT_OP_IF_LT:
				cond = (s32)r < (s32)value;
				break;
			case CHEAT_OP_IF_GT:
				cond = (s32)r > (s32)value;
				break;
			case CHEAT_OP_IF_ULT:
				cond = r < value;
				break;
			case CHEAT_OP_IF_UGT:
				cond = r > value;
				break;
			case CHEAT_OP_IF_AND:
				cond = (r & value) != 0;
				break;
			case CHEAT_OP_IF_NAND:
				cond = (r & value) == 0;
				break;
			case CHEAT_OP_IF_BUTTON:
				cond = button;
				break;
			default:
				cond = 0;
				break;
			}
		}
		if (else_loc && i == else_loc) {
			i = end_loc;
			end_loc = 0;
		}
		if (conditional && op->count > 0 && !cond)
			i += op->count;
		else if (conditional && op->else_count > 0) {
			else_loc = i + op->count;
			end_loc = else_loc + op->else_count;
		}
	}
}

// pre and want are "OFFSET:VALUE ..." bytes at 0x03000000 + OFFSET (hex)
static void scenario(const char *pre, const char *want, const char *what)
{
	char *end;
	memset(iwram, 0, sizeof(iwram));
	while (*pre) {
		u32 offset = strtoul(pre, &end, 16);
		iwram[offset] = strtoul(end + 1, &end, 16);
		pre = end + strspn(end, " ");
	}
	run();
	while (*want) {
		u32 offset = strtoul(want, &end, 16);
		u32 value = strtoul(end + 1, &end, 16);
		if (iwram[offset] != value) {
			tlog_error("FAIL %s: [0x%X] is 0x%X, expected 0x%X", what, (unsigned)offset, iwram[offset],
				   (unsigned)value);
			++torii_test_failures;
		}
		want = end + strspn(end, " ");
	}
}

// mGBA's src/gba/test/cheats.c, PARv3 raw: each code's lines joined by '+',
// then initial bytes and the bytes expected after one refresh
static const struct {
	const char *name, *code, *cases[8][2];
} par3_vectors[] = {
	{"Assign",
	 "00300000 00000078+02300002 00005678+04300004 12345678",
	 {{"", "0:78 2:78 3:56 4:78 5:56 6:34 7:12"}}},
	{"Slide1", "00000000 80300000+00000001 01020002", {{"", "0:1 1:0 2:2 3:0 4:0 5:0"}}},
	{"Slide2", "00000000 82300000+00000001 01020002", {{"", "0:1 1:0 2:0 3:0 4:2 5:0 6:0 7:0 8:0 9:0 a:0 b:0"}}},
	{"Slide4",
	 "00000000 84300000+00000001 01020002",
	 {{"", "0:1 1:0 4:0 5:0 8:2 9:0 c:0 d:0 10:0 11:0 14:0 15:0"}}},
	{"If1",
	 "00300001 00000011+08300000 00000000+00300001 00000012+00300000 00000001",
	 {{"", "0:1 1:12"}, {"0:1", "0:1 1:11"}}},
	{"If1x1",
	 "00300002 00000021+00300003 00000031+08300000 00000000+00300002 00000022+08300001 00000000+"
	 "00300003 00000032+00300000 00000001",
	 {{"", "0:1 1:0 2:22 3:32"},
	  {"0:1", "0:1 1:0 2:21 3:32"},
	  {"1:1", "0:1 1:1 2:22 3:31"},
	  {"0:1 1:1", "0:1 1:1 2:21 3:31"}}},
	{"If2",
	 "00300001 00000011+00300002 00000021+48300000 00000000+00300001 00000012+00300002 00000022+"
	 "00300000 00000001",
	 {{"", "0:1 1:12 2:22"}, {"0:1", "0:1 1:11 2:21"}}},
	{"If2x2",
	 "00300002 00000021+00300003 00000031+00300004 00000041+00300005 00000051+48300000 00000000+"
	 "00300002 00000022+00300003 00000032+48300001 00000000+00300004 00000042+00300005 00000052+"
	 "00300000 00000001",
	 {{"", "0:1 1:0 2:22 3:32 4:42 5:52"},
	  {"0:1", "0:1 1:0 2:21 3:31 4:42 5:52"},
	  {"1:1", "0:1 1:1 2:22 3:32 4:41 5:51"},
	  {"0:1 1:1", "0:1 1:1 2:21 3:31 4:41 5:51"}}},
	{"If2Contain1",
	 "00300002 00000021+48300000 00000000+08300001 00000000+00300002 00000022+00300000 00000001",
	 {{"", "0:1 1:0 2:22"}, {"0:1", "0:1 1:0 2:21"}, {"1:1", "0:1 1:1 2:21"}, {"0:1 1:1", "0:1 1:1 2:21"}}},
	{"IfX",
	 "00300001 00000011+88300000 00000000+00300001 00000012+00000000 40000000+00300000 00000001",
	 {{"", "0:1 1:12"}, {"0:1", "0:1 1:11"}}},
	{"IfXxX",
	 "00300002 00000021+00300003 00000031+00300004 00000041+88300000 00000000+00300002 00000022+"
	 "00000000 40000000+00300003 00000032+88300001 00000000+00300004 00000042+00000000 40000000+"
	 "00300000 00000001",
	 {{"", "0:1 1:0 2:22 3:32 4:42"},
	  {"0:1", "0:1 1:0 2:21 3:32 4:42"},
	  {"1:1", "0:1 1:1 2:22 3:32 4:41"},
	  {"0:1 1:1", "0:1 1:1 2:21 3:32 4:41"}}},
	{"IfXElse",
	 "00300001 00000011+00300002 00000021+88300000 00000000+00300001 00000012+00000000 60000000+"
	 "00300002 00000022+00000000 40000000+00300000 00000001",
	 {{"", "0:1 1:12 2:21"}, {"0:1", "0:1 1:11 2:22"}}},
	{"IfXElsexX",
	 "00300002 00000021+00300003 00000031+00300004 00000041+00300005 00000051+88300000 00000000+"
	 "00300002 00000022+00000000 60000000+00300003 00000032+00000000 40000000+00300004 00000042+"
	 "88300001 00000000+00300005 00000052+00000000 40000000+00300000 00000001",
	 {{"", "0:1 1:0 2:22 3:31 4:42 5:52"},
	  {"0:1", "0:1 1:0 2:21 3:32 4:42 5:52"},
	  {"1:1", "0:1 1:1 2:22 3:31 4:42 5:51"},
	  {"0:1 1:1", "0:1 1:1 2:21 3:32 4:42 5:51"}}},
	{"IfXElsexXElse",
	 "00300002 00000021+00300003 00000031+00300004 00000041+00300005 00000051+00300006 00000061+"
	 "88300000 00000000+00300002 00000022+00000000 60000000+00300003 00000032+00000000 40000000+"
	 "00300004 00000042+88300001 00000000+00300005 00000052+00000000 60000000+00300006 00000062+"
	 "00000000 40000000+00300000 00000001",
	 {{"", "0:1 1:0 2:22 3:31 4:42 5:52"},
	  {"0:1", "0:1 1:0 2:21 3:32 4:42 5:52 6:61"},
	  {"1:1", "0:1 1:1 2:22 3:31 4:42 5:51 6:62"},
	  {"0:1 1:1", "0:1 1:1 2:21 3:32 4:42 5:51 6:62"}}},
	{"IfXContain1",
	 "00300002 00000021+00300003 00000031+00300004 00000041+88300000 00000000+00300002 00000022+"
	 "08300001 00000000+00300003 00000032+00300004 00000042+00000000 40000000+00300000 00000001",
	 {{"", "0:1 1:0 2:22 3:32 4:42"},
	  {"0:1", "0:1 1:0 2:21 3:31 4:41"},
	  {"1:1", "0:1 1:1 2:22 3:31 4:42"},
	  {"0:1 1:1", "0:1 1:1 2:21 3:31 4:41"}}},
	{"IfXContain1Else",
	 "00300002 00000021+00300003 00000031+00300004 00000041+00300005 00000051+88300000 00000000+"
	 "00300002 00000022+08300001 00000000+00300003 00000032+00300004 00000042+00000000 60000000+"
	 "00300005 00000052+00000000 40000000+00300000 00000001",
	 {{"", "0:1 1:0 2:22 3:32 4:42 5:51"},
	  {"0:1", "0:1 1:0 2:21 3:31 4:41 5:52"},
	  {"1:1", "0:1 1:1 2:22 3:31 4:42 5:51"},
	  {"0:1 1:1", "0:1 1:1 2:21 3:31 4:41 5:52"}}},
	{"IfXElseContain1",
	 "00300002 00000021+00300003 00000031+00300004 00000041+00300005 00000051+88300000 00000000+"
	 "00300002 00000022+00000000 60000000+00300003 00000032+08300001 00000000+00300004 00000042+"
	 "00300005 00000052+00000000 40000000+00300000 00000001",
	 {{"", "0:1 1:0 2:22 3:31 4:41 5:51"},
	  {"0:1", "0:1 1:0 2:21 3:32 4:42 5:52"},
	  {"1:1", "0:1 1:1 2:22 3:31 4:41 5:51"},
	  {"0:1 1:1", "0:1 1:1 2:21 3:32 4:41 5:52"}}},
	{"IfXContain1ElseContain1",
	 "00300003 00000031+00300004 00000041+00300005 00000051+00300006 00000061+00300007 00000071+"
	 "00300008 00000081+88300000 00000000+00300003 00000032+08300001 00000000+00300004 00000042+"
	 "00300005 00000052+00000000 60000000+00300006 00000062+08300002 00000000+00300007 00000072+"
	 "00300008 00000082+00000000 40000000+00300000 00000001",
	 {{"", "0:1 1:0 2:0 3:32 4:42 5:52 6:61 7:71 8:81"},
	  {"0:1", "0:1 1:0 2:0 3:31 4:41 5:51 6:62 7:72 8:82"},
	  {"1:1", "0:1 1:1 2:0 3:32 4:41 5:52 6:61 7:71 8:81"},
	  {"2:1", "0:1 1:0 2:1 3:32 4:42 5:52 6:61 7:71 8:81"},
	  {"0:1 1:1", "0:1 1:1 2:0 3:31 4:41 5:51 6:62 7:72 8:82"},
	  {"0:1 2:1", "0:1 1:0 2:1 3:31 4:41 5:51 6:62 7:71 8:82"},
	  {"1:1 2:1", "0:1 1:1 2:1 3:32 4:41 5:52 6:61 7:71 8:81"},
	  {"0:1 1:1 2:1", "0:1 1:1 2:1 3:31 4:41 5:51 6:62 7:71 8:82"}}},
	{"Never1",
	 "00300001 00000011+0E300000 00000000+00300001 00000012+00300000 00000001",
	 {{"", "0:1 1:11"}, {"0:1", "0:1 1:11"}}},
	{"Never2",
	 "00300001 00000011+00300002 00000021+4E300000 00000000+00300001 00000012+00300002 00000022+"
	 "00300000 00000001",
	 {{"", "0:1 1:11 2:21"}, {"0:1", "0:1 1:11 2:21"}}},
	{"NeverX",
	 "00300001 00000011+8E300000 00000000+00300001 00000012+00000000 40000000+00300000 00000001",
	 {{"", "0:1 1:11"}, {"0:1", "0:1 1:11"}}},
	{"NeverXElse",
	 "00300001 00000011+00300002 00000021+8E300000 00000000+00300001 00000012+00000000 60000000+"
	 "00300002 00000022+00000000 40000000+00300000 00000001",
	 {{"", "0:1 1:11 2:22"}, {"0:1", "0:1 1:11 2:22"}}},
};

void test_cheatcode_mgba_vectors(void)
{
	u32 i, k;
	for (i = 0; i < sizeof(par3_vectors) / sizeof(par3_vectors[0]); ++i) {
		u8 status = decode_as(CHEAT_FMT_PARV3_RAW, par3_vectors[i].code);
		if (status != CHEAT_OK) {
			tlog_error("FAIL doPARv3%s: status %u", par3_vectors[i].name, status);
			++torii_test_failures;
			continue;
		}
		for (k = 0; k < 8 && par3_vectors[i].cases[k][1]; ++k)
			scenario(par3_vectors[i].cases[k][0], par3_vectors[i].cases[k][1], par3_vectors[i].name);
	}

	// addRawPARv3: an add takes a line; bit 24 without the IO base doesn't
	CHECK_EQ(decode_as(CHEAT_FMT_PARV3_RAW, "80000000 00000001"), CHEAT_OK);
	CHECK_OP(0, CHEAT_OP_ADD, 1, 0, 1, 1);
	CHECK_EQ(decode_as(CHEAT_FMT_PARV3_RAW, "80000000 00000000"), CHEAT_MASTER_ONLY);  // padding
	CHECK_EQ(decode_as(CHEAT_FMT_PARV3_RAW, "43000000 00000000"), CHEAT_UNREADABLE);

	// doPARv3IfButton: the write runs only while the button is held
	CHECK_EQ(decode_as(CHEAT_FMT_PARV3_RAW, "00000000 10300000+00000001 00000000"), CHEAT_OK);
	CHECK_EQ(n_ops, 2);
	CHECK_OP(0, CHEAT_OP_IF_BUTTON, 0, 0, 0, 1);
	CHECK_OP(1, CHEAT_OP_WRITE, 1, 0x03000000, 1, 1);
	button = 0;
	scenario("", "0:0", "IfButton up");
	button = 1;
	scenario("", "0:1", "IfButton down");
	button = 0;
}

// ------------------------------------------------------------------------

void test_cheatcode_par3_types(void)
{
	struct cheat_decoder d;

	// Fills from an 8- or 16-bit write's high bits; a 32-bit write is one
	CHECK_EQ(decode_as(CHEAT_FMT_PARV3_RAW, "00300010 000003AB+02300020 0004BEEF+04300030 CAFEBABE"), CHEAT_OK);
	CHECK_EQ(n_ops, 3);
	CHECK_OP(0, CHEAT_OP_WRITE, 1, 0x03000010, 0xAB, 4);
	CHECK_EQ(ops[0].addr_step, 1);
	CHECK_OP(1, CHEAT_OP_WRITE, 2, 0x03000020, 0xBEEF, 5);
	CHECK_EQ(ops[1].addr_step, 2);
	CHECK_OP(2, CHEAT_OP_WRITE, 4, 0x03000030, 0xCAFEBABE, 1);

	// The address's region nibble moves (0x00F00000 -> 0x0F000000)
	CHECK_EQ(decode_as(CHEAT_FMT_PARV3_RAW, "00212345 00000001"), CHEAT_OK);
	CHECK_EQ(ops[0].address, 0x02012345);

	// Pointer writes, adds, IO writes
	CHECK_EQ(decode_as(CHEAT_FMT_PARV3_RAW, "40300100 00000342+42300104 00021234+84300108 00000010"), CHEAT_OK);
	CHECK_OP(0, CHEAT_OP_WRITE_PTR, 1, 0x03000100, 0x42, 1);
	CHECK_EQ(ops[0].addr_step, 3);
	CHECK_OP(1, CHEAT_OP_WRITE_PTR, 2, 0x03000104, 0x1234, 1);
	CHECK_EQ(ops[1].addr_step, 4);
	CHECK_OP(2, CHEAT_OP_ADD, 4, 0x03000108, 0x10, 1);
	CHECK_EQ(decode_as(CHEAT_FMT_PARV3_RAW, "C6000200 00000080+C7000204 00001234"), CHEAT_OK);
	CHECK_OP(0, CHEAT_OP_WRITE, 1, 0x04000200, 0x80, 1);
	CHECK_OP(1, CHEAT_OP_WRITE, 2, 0x04000204, 0x1234, 1);

	// Four ROM patch slots take their value from the next line
	CHECK_EQ(decode_as(CHEAT_FMT_PARV3_RAW,
			   "00000000 18012345+0000A0B0 00000000+00000000 1E000010+00004770 00000000"),
		 CHEAT_OK);
	CHECK_EQ(n_ops, 2);
	CHECK_OP(0, CHEAT_OP_ROM_PATCH, 2, 0x0802468A, 0xA0B0, 1);
	CHECK_OP(1, CHEAT_OP_ROM_PATCH, 2, 0x08000020, 0x4770, 1);

	// Slowdown is kept; the hook, the marker and the end line are dropped
	CHECK_EQ(decode_as(CHEAT_FMT_PARV3_RAW, "00000000 08000123"), CHEAT_OK);
	CHECK_OP(0, CHEAT_OP_SLOWDOWN, 0, 0, 0x123, 1);
	CHECK_EQ(decode_as(CHEAT_FMT_PARV3_RAW, "C4000100 00000000+12345678 001DC0DE+00000000 00000000"),
		 CHEAT_MASTER_ONLY);
	CHECK_EQ(decode_as(CHEAT_FMT_PARV3_RAW, "C4000100 00000000+00300000 00000005"), CHEAT_OK);
	CHECK_EQ(n_ops, 1);

	// Unreadable: ELSE or ENDIF without a block, disable-type conditionals,
	// unknown specials, an 8-byte write, a slide missing its second line
	CHECK_EQ(decode_as(CHEAT_FMT_PARV3_RAW, "00000000 40000000"), CHEAT_UNREADABLE);
	CHECK_EQ(decode_as(CHEAT_FMT_PARV3_RAW, "00000000 60000000"), CHEAT_UNREADABLE);
	CHECK_EQ(decode_as(CHEAT_FMT_PARV3_RAW, "C8300000 00000000+00300000 00000001"), CHEAT_UNREADABLE);
	CHECK_EQ(decode_as(CHEAT_FMT_PARV3_RAW, "00000000 02000000"), CHEAT_UNREADABLE);
	CHECK_EQ(decode_as(CHEAT_FMT_PARV3_RAW, "06300000 00000000"), CHEAT_UNREADABLE);
	CHECK_EQ(decode_as(CHEAT_FMT_PARV3_RAW, "00000000 80300000"), CHEAT_UNREADABLE);

	// Conditional types, signed and not; a block open at the end closes there
	CHECK_EQ(decode_as(CHEAT_FMT_PARV3_RAW,
			   "10300000 00000001+18300000 00000002+20300000 00000003+28300000 00000004+"
			   "30300000 00000005+38300000 00000006+8A300000 00000007+00300001 00000001+"
			   "00300002 00000001"),
		 CHEAT_OK);
	CHECK_EQ(n_ops, 9);
	CHECK_EQ(ops[0].type, CHEAT_OP_IF_NE);
	CHECK_EQ(ops[1].type, CHEAT_OP_IF_LT);
	CHECK_EQ(ops[2].type, CHEAT_OP_IF_GT);
	CHECK_EQ(ops[3].type, CHEAT_OP_IF_ULT);
	CHECK_EQ(ops[4].type, CHEAT_OP_IF_UGT);
	CHECK_EQ(ops[5].type, CHEAT_OP_IF_AND);
	CHECK_OP(6, CHEAT_OP_IF_EQ, 2, 0x03000000, 7, 2);

	// A next-one conditional covers a whole button code (two ops)
	CHECK_EQ(decode_as(CHEAT_FMT_PARV3_RAW, "08300000 00000000+00000000 12300004+00001234 00000000"), CHEAT_OK);
	CHECK_EQ(n_ops, 3);
	CHECK_EQ(ops[0].count, 2);
	CHECK_OP(2, CHEAT_OP_WRITE, 2, 0x03000004, 0x1234, 1);

	// DEADFACE reseeds, for this cheat and the file's later ones
	cheat_decoder_force(&d, CHEAT_FMT_PARV3);
	u32 seed0 = d.gs_seeds[0];
	decode(&d, "00000000 00000000");  // decrypts to garbage
	CHECK_EQ(d.gs_seeds[0], seed0);
	cheat_decoder_force(&d, CHEAT_FMT_PARV3_RAW);
	CHECK_EQ(decode(&d, "DEADFACE 00001234"), CHEAT_MASTER_ONLY);
	CHECK(d.gs_seeds[0] != seed0);
}

void test_cheatcode_par3_encrypted(void)
{
	struct cheat_decoder d;
	u32 seed0;
	// Pokemon - Rubin-Edition (G): the master code, then money
	cheat_decoder_init(&d);
	CHECK_EQ(decode(&d, "f57c7bcb+adc632b9+851b05e0594b5d88"), CHEAT_MASTER_ONLY);
	CHECK_EQ(d.gs_version, CHEAT_FMT_PARV3);
	CHECK_EQ(d.last, CHEAT_FMT_PARV3);
	seed0 = d.gs_seeds[0];
	CHECK_EQ(decode(&d, "e51e97c3+7858e4eb"), CHEAT_OK);
	CHECK_EQ(n_ops, 1);
	CHECK_OP(0, CHEAT_OP_WRITE, 4, 0x02025BC4, 0xF423F, 1);
	CHECK_EQ(decode(&d, "280ea266+88a62e5c"), CHEAT_OK);
	CHECK_OP(0, CHEAT_OP_WRITE, 4, 0x02025BCC, 0x630044, 1);
	CHECK_EQ(d.gs_seeds[0], seed0);
}

// ------------------------------------------------------------------------

void test_cheatcode_gsa(void)
{
	struct cheat_decoder d;

	CHECK_EQ(decode_as(CHEAT_FMT_GSAV1_RAW, "03001234 000000AB+13001236 0000BEEF+2300123C DEADBEEF"), CHEAT_OK);
	CHECK_EQ(n_ops, 3);
	CHECK_OP(0, CHEAT_OP_WRITE, 1, 0x03001234, 0xAB, 1);
	CHECK_OP(1, CHEAT_OP_WRITE, 2, 0x03001236, 0xBEEF, 1);
	CHECK_OP(2, CHEAT_OP_WRITE, 4, 0x0300123C, 0xDEADBEEF, 1);

	// A list: one value to three addresses, two to a line
	CHECK_EQ(decode_as(CHEAT_FMT_GSAV1_RAW, "30000003 12345678+03000010 03000020+03000030 00000000"), CHEAT_OK);
	CHECK_EQ(n_ops, 3);
	CHECK_OP(0, CHEAT_OP_WRITE, 4, 0x03000010, 0x12345678, 1);
	CHECK_OP(1, CHEAT_OP_WRITE, 4, 0x03000020, 0x12345678, 1);
	CHECK_OP(2, CHEAT_OP_WRITE, 4, 0x03000030, 0x12345678, 1);
	CHECK_EQ(decode_as(CHEAT_FMT_GSAV1_RAW, "30000003 12345678+03000010 03000020"), CHEAT_UNREADABLE);

	// A ROM patch, button writes, conditionals
	CHECK_EQ(decode_as(CHEAT_FMT_GSAV1_RAW, "60012345 00004770"), CHEAT_OK);
	CHECK_OP(0, CHEAT_OP_ROM_PATCH, 2, 0x0802468A, 0x4770, 1);
	CHECK_EQ(decode_as(CHEAT_FMT_GSAV1_RAW, "83100010 00000063+83200012 00001234"), CHEAT_OK);
	CHECK_EQ(n_ops, 4);
	CHECK_OP(0, CHEAT_OP_IF_BUTTON, 0, 0, 0, 1);
	CHECK_OP(1, CHEAT_OP_WRITE, 1, 0x03000010, 0x63, 1);
	CHECK_OP(3, CHEAT_OP_WRITE, 2, 0x03000012, 0x1234, 1);
	CHECK_EQ(decode_as(CHEAT_FMT_GSAV1_RAW, "83300010 00000063"), CHEAT_UNREADABLE);
	CHECK_EQ(decode_as(CHEAT_FMT_GSAV1_RAW, "D3000010 00000005+13000012 00000001"), CHEAT_OK);
	CHECK_OP(0, CHEAT_OP_IF_EQ, 2, 0x03000010, 5, 1);
	CHECK_EQ(decode_as(CHEAT_FMT_GSAV1_RAW,
			   "E0020005 03000010+13000012 00000001+13000014 00000001+13000016 00000001"),
		 CHEAT_OK);
	CHECK_OP(0, CHEAT_OP_IF_EQ, 2, 0x03000010, 5, 2);

	// The hook and the marker are master lines; unknown types don't read
	CHECK_EQ(decode_as(CHEAT_FMT_GSAV1_RAW, "F0000100 00000001+12345678 001DC0DE"), CHEAT_MASTER_ONLY);
	CHECK_EQ(decode_as(CHEAT_FMT_GSAV1_RAW, "43000000 00000000"), CHEAT_UNREADABLE);

	// Mario & Luigi: Superstar Saga's GameShark master code, then a write,
	// the same one its CodeBreaker file gives as 820048E0 270F
	cheat_decoder_init(&d);
	CHECK_EQ(decode(&d, "502F4DE9+75D4B6CA+8791F574+20D9F121"), CHEAT_MASTER_ONLY);
	CHECK_EQ(d.gs_version, CHEAT_FMT_GSAV1);
	CHECK_EQ(decode(&d, "A9873144+203A53CE"), CHEAT_OK);
	CHECK_EQ(n_ops, 1);
	CHECK_OP(0, CHEAT_OP_WRITE, 2, 0x020048E0, 0x270F, 1);
	CHECK_EQ(decode(&d, "FFFE50E5 A8AD1330"), CHEAT_OK);
	CHECK_OP(0, CHEAT_OP_WRITE, 1, 0x03003F62, 0xFF, 1);
	CHECK_EQ(decode(&d, "FFFE50E5A8AD1330"), CHEAT_OK);  // one 16-digit token
	CHECK_OP(0, CHEAT_OP_WRITE, 1, 0x03003F62, 0xFF, 1);

	// Sonic Advance's raw ROM patch, alone in its file: the detection picks a
	// decrypted reading that doesn't read, so the raw one is taken; a second
	// one in two lines too, and the file keeps the raw format
	cheat_decoder_init(&d);
	CHECK_EQ(decode(&d, "60018CFC 000046C0"), CHEAT_OK);
	CHECK_EQ(d.gs_version, CHEAT_FMT_GSAV1_RAW);
	CHECK_OP(0, CHEAT_OP_ROM_PATCH, 2, 0x080319F8, 0x46C0, 1);
	cheat_decoder_init(&d);
	CHECK_EQ(decode(&d, "60079D70 000046C0+60079D80 000046C0"), CHEAT_OK);
	CHECK_OP(1, CHEAT_OP_ROM_PATCH, 2, 0x080F3B00, 0x46C0, 1);

	// DEADFACE reseeds GameShark v1 with its own tables
	cheat_decoder_force(&d, CHEAT_FMT_GSAV1_RAW);
	u32 seed0 = d.gs_seeds[0];
	CHECK_EQ(decode(&d, "DEADFACE 00000102"), CHEAT_MASTER_ONLY);
	CHECK(d.gs_seeds[0] != seed0);
}

// ------------------------------------------------------------------------

void test_cheatcode_codebreaker(void)
{
	struct cheat_decoder d;

	CHECK_EQ(decode_as(CHEAT_FMT_AUTO, "22001000 0001+32001002 00AB+62001004 FFFE+82001006 1234+E2001008 0010"),
		 CHEAT_OK);
	CHECK_EQ(n_ops, 5);
	CHECK_OP(0, CHEAT_OP_OR, 2, 0x02001000, 1, 1);
	CHECK_OP(1, CHEAT_OP_WRITE, 1, 0x02001002, 0xAB, 1);
	CHECK_OP(2, CHEAT_OP_AND, 2, 0x02001004, 0xFFFE, 1);
	CHECK_OP(3, CHEAT_OP_WRITE, 2, 0x02001006, 0x1234, 1);
	CHECK_OP(4, CHEAT_OP_ADD, 2, 0x02001008, 0x10, 1);

	// A fill: count, value step and address step on the next line
	CHECK_EQ(decode_as(CHEAT_FMT_AUTO, "42001000 0100+00020005 0004"), CHEAT_OK);
	CHECK_EQ(n_ops, 1);
	CHECK_OP(0, CHEAT_OP_WRITE, 2, 0x02001000, 0x100, 5);
	CHECK_EQ(ops[0].addr_step, 4);
	CHECK_EQ(ops[0].value_step, 2);
	CHECK_EQ(decode_as(CHEAT_FMT_AUTO, "42001000 0100"), CHEAT_UNREADABLE);

	// Conditionals cover the next code
	CHECK_EQ(decode_as(CHEAT_FMT_AUTO, "72001000 0001+A2001000 0002+B2001000 0003+C2001000 0004+F2001000 0005+"
					   "D0000020 0008+32001002 0001"),
		 CHEAT_OK);
	CHECK_EQ(n_ops, 7);
	CHECK_OP(0, CHEAT_OP_IF_EQ, 2, 0x02001000, 1, 1);
	CHECK_EQ(ops[1].type, CHEAT_OP_IF_NE);
	CHECK_EQ(ops[2].type, CHEAT_OP_IF_GT);
	CHECK_EQ(ops[3].type, CHEAT_OP_IF_LT);
	CHECK_EQ(ops[4].type, CHEAT_OP_IF_AND);
	CHECK_OP(5, CHEAT_OP_IF_NAND, 2, 0x04000130, 8, 1);
	CHECK_EQ(decode_as(CHEAT_FMT_AUTO, "D0000021 0008+32001002 0001"), CHEAT_UNREADABLE);

	// The key test: the write runs only while every key in the mask is down
	CHECK_EQ(decode_as(CHEAT_FMT_AUTO, "D0000020 0004+33000010 0055"), CHEAT_OK);
	keyinput = 0x3FF;
	scenario("", "10:0", "CB key up");
	keyinput = 0x3FB;
	scenario("", "10:55", "CB key down");

	// A super code: 2 * 3 bytes in written order from the next lines, all
	// covered by the conditional before it
	CHECK_EQ(decode_as(CHEAT_FMT_AUTO, "72001000 0001+52001010 0003+0102A3B4 C5D6"), CHEAT_OK);
	CHECK_EQ(n_ops, 7);
	CHECK_EQ(ops[0].count, 6);
	CHECK_OP(1, CHEAT_OP_WRITE, 1, 0x02001010, 0x01, 1);
	CHECK_OP(4, CHEAT_OP_WRITE, 1, 0x02001013, 0xB4, 1);
	CHECK_OP(6, CHEAT_OP_WRITE, 1, 0x02001015, 0xD6, 1);
	CHECK_EQ(decode_as(CHEAT_FMT_AUTO, "52001010 0004+0102A3B4 C5D6"), CHEAT_UNREADABLE);
	CHECK_EQ(decode_as(CHEAT_FMT_AUTO, "52001010 0000"), CHEAT_UNREADABLE);

	// Game ID and hook lines are master lines, also inside a real cheat
	CHECK_EQ(decode_as(CHEAT_FMT_AUTO, "0000ABCD 0007+100F68A4 0007"), CHEAT_MASTER_ONLY);
	CHECK_EQ(decode_as(CHEAT_FMT_AUTO, "0000ABCD 0007+100F68A4 0007+32001002 0001"), CHEAT_OK);
	CHECK_EQ(n_ops, 1);
	CHECK_OP(0, CHEAT_OP_WRITE, 1, 0x02001002, 1, 1);

	// Mega Man Battle Network 6 - Cybeast Gregar: the key in the master
	// code carries to a later cheat ("Infinite HP"); a later cheat repeating
	// the key line keeps it
	cheat_decoder_init(&d);
	CHECK_EQ(decode(&d, "930CFE0E61A7+5E4E1F94523A+DB3A64369A43"), CHEAT_MASTER_ONLY);
	CHECK_EQ(d.cb_master, 0x930CFE0E);
	CHECK_EQ(d.cb_key2, 0x61A7);
	CHECK_EQ(decode(&d, "D86098902576+777F4707DA29"), CHEAT_OK);
	CHECK_EQ(n_ops, 2);
	CHECK_OP(0, CHEAT_OP_IF_EQ, 2, 0x0203AA04, 0x84F0, 1);
	CHECK_OP(1, CHEAT_OP_WRITE, 2, 0x0203A9D4, 0x270F, 1);
	CHECK_EQ(d.last_alone, 0);
	CHECK_EQ(decode(&d, "930CFE0E61A7+D86098902576+777F4707DA29"), CHEAT_OK);
	CHECK_EQ(n_ops, 2);
	CHECK_OP(1, CHEAT_OP_WRITE, 2, 0x0203A9D4, 0x270F, 1);

	// A plain cheat after the key, which decrypts to nonsense, reads plain
	CHECK_EQ(decode(&d, "8203A04E+0258"), CHEAT_OK);
	CHECK_EQ(n_ops, 1);
	CHECK_OP(0, CHEAT_OP_WRITE, 2, 0x0203A04E, 0x258, 1);
	CHECK_EQ(d.last_alone, 1);
	CHECK_EQ(d.cb_master, 0x930CFE0E);
	CHECK_EQ(decode(&d, "D86098902576+777F4707DA29"), CHEAT_OK);
	CHECK_OP(1, CHEAT_OP_WRITE, 2, 0x0203A9D4, 0x270F, 1);
}

void test_cheatcode_vba(void)
{
	CHECK_EQ(decode_as(CHEAT_FMT_AUTO, "02001000:AB+02001002:BEEF+02001004:DEADBEEF"), CHEAT_OK);
	CHECK_EQ(n_ops, 3);
	CHECK_OP(0, CHEAT_OP_WRITE, 1, 0x02001000, 0xAB, 1);
	CHECK_OP(1, CHEAT_OP_WRITE, 2, 0x02001002, 0xBEEF, 1);
	CHECK_OP(2, CHEAT_OP_WRITE, 4, 0x02001004, 0xDEADBEEF, 1);
	CHECK_EQ(decode_as(CHEAT_FMT_VBA, "08001000:4770"), CHEAT_OK);
	CHECK_OP(0, CHEAT_OP_ROM_PATCH, 2, 0x08001000, 0x4770, 1);
	CHECK_EQ(decode_as(CHEAT_FMT_AUTO, "02001000:ABCDEF"), CHEAT_UNREADABLE);
	CHECK_EQ(decode_as(CHEAT_FMT_AUTO, "02001000:"), CHEAT_UNREADABLE);
	CHECK_EQ(decode_as(CHEAT_FMT_AUTO, "0200100:AB"), CHEAT_UNREADABLE);
	// No GBA address is that high: a CodeBreaker line written with a ':'
	CHECK_EQ(decode_as(CHEAT_FMT_AUTO, "820001A8:FFFF+33000024:0002"), CHEAT_OK);
	CHECK_OP(0, CHEAT_OP_WRITE, 2, 0x020001A8, 0xFFFF, 1);
	CHECK_OP(1, CHEAT_OP_WRITE, 1, 0x03000024, 0x02, 1);
}

// Line shapes, separators, stand-ins and malformed text
void test_cheatcode_shapes(void)
{
	static const char *const same[] = {
		"82001000 1234", "82001000+1234", "820010001234", "  82001000\t1234 \r\n", "+82001000++1234+",
	};
	struct cheat_decoder d;
	u32 i;
	for (i = 0; i < sizeof(same) / sizeof(same[0]); ++i) {
		CHECK_EQ(decode_as(CHEAT_FMT_AUTO, same[i]), CHEAT_OK);
		CHECK_EQ(n_ops, 1);
		CHECK_OP(0, CHEAT_OP_WRITE, 2, 0x02001000, 0x1234, 1);
	}
	CHECK_EQ(decode_as(CHEAT_FMT_AUTO, "82001000+1234 32001002+0001"), CHEAT_OK);
	CHECK_EQ(n_ops, 2);

	CHECK_EQ(decode_as(CHEAT_FMT_AUTO, ""), CHEAT_UNREADABLE);
	CHECK_EQ(decode_as(CHEAT_FMT_AUTO, " + \t"), CHEAT_UNREADABLE);
	CHECK_EQ(decode_as(CHEAT_FMT_AUTO, "82001000"), CHEAT_UNREADABLE);
	CHECK_EQ(decode_as(CHEAT_FMT_AUTO, "1234"), CHEAT_UNREADABLE);
	CHECK_EQ(decode_as(CHEAT_FMT_AUTO, "8200100+1234"), CHEAT_UNREADABLE);
	CHECK_EQ(decode_as(CHEAT_FMT_AUTO, "82001000+12345"), CHEAT_UNREADABLE);
	CHECK_EQ(decode_as(CHEAT_FMT_AUTO, "N/A"), CHEAT_UNREADABLE);
	CHECK_EQ(decode_as(CHEAT_FMT_AUTO, "82001000+1234+70A8A14DvE637AD4"), CHEAT_UNREADABLE);
	CHECK_EQ(decode_as(CHEAT_FMT_AUTO, "\"82001000+1234\""), CHEAT_UNREADABLE);  // quotes are the caller's

	// Digit stand-ins make a placeholder, unless the text is malformed too
	CHECK_EQ(decode_as(CHEAT_FMT_AUTO, "3300075C+00??"), CHEAT_PLACEHOLDER);
	CHECK_EQ(decode_as(CHEAT_FMT_AUTO, "320232DA+000X"), CHEAT_PLACEHOLDER);
	CHECK_EQ(decode_as(CHEAT_FMT_AUTO, "3200E2D0+0002+8200E2CE+XXYY"), CHEAT_PLACEHOLDER);
	CHECK_EQ(decode_as(CHEAT_FMT_AUTO, "03001D04+000000??"), CHEAT_PLACEHOLDER);
	CHECK_EQ(decode_as(CHEAT_FMT_AUTO, "02001000:0Z"), CHEAT_PLACEHOLDER);
	CHECK_EQ(decode_as(CHEAT_FMT_AUTO, "3300075C+00??+1234"), CHEAT_UNREADABLE);
	CHECK_EQ(n_ops, 0);

	// A placeholder or a malformed cheat leaves the file's state alone
	cheat_decoder_init(&d);
	CHECK_EQ(decode(&d, "930CFE0E 61A7+0000+00??"), CHEAT_UNREADABLE);
	CHECK_EQ(decode(&d, "930CFE0E 61A7+330000?? 0001"), CHEAT_PLACEHOLDER);
	CHECK_EQ(d.cb_master, 0);

	// Forcing a format refuses the others' lines
	CHECK_EQ(decode_as(CHEAT_FMT_CODEBREAKER, "03001234 000000AB"), CHEAT_UNREADABLE);
	CHECK_EQ(decode_as(CHEAT_FMT_CODEBREAKER, "02001000:AB"), CHEAT_UNREADABLE);
	CHECK_EQ(decode_as(CHEAT_FMT_GSAV1_RAW, "82001000 1234"), CHEAT_UNREADABLE);
	CHECK_EQ(decode_as(CHEAT_FMT_VBA, "82001000 1234"), CHEAT_UNREADABLE);
	CHECK_EQ(decode_as(CHEAT_FMT_GSAV1_RAW, "03001234 000000AB"), CHEAT_OK);
	CHECK_EQ(decode_as(CHEAT_FMT_PARV3_RAW, "04300234 000000AB"), CHEAT_OK);
	CHECK_OP(0, CHEAT_OP_WRITE, 4, 0x03000234, 0xAB, 1);
	// A CodeBreaker code can't run on into a GameShark one
	CHECK_EQ(decode_as(CHEAT_FMT_AUTO, "42001000 0100+03001234 000000AB"), CHEAT_UNREADABLE);
}

void test_cheatcode_too_big(void)
{
	struct cheat_decoder d;
	cheat_decoder_init(&d);
	CHECK_EQ(decode_n(&d, "32001000 0001+32001001 0002+32001002 0003", 2), CHEAT_TOO_BIG);
	CHECK_EQ(n_ops, 3);
	CHECK_OP(1, CHEAT_OP_WRITE, 1, 0x02001001, 2, 1);
	CHECK_EQ(ops[2].type, 0xA5);  // untouched
	CHECK_EQ(decode_n(&d, "32001000 0001+32001001 0002", 2), CHEAT_OK);

	// No buffer at all: the count is still right
	u32 n;
	const char code[] = {'7', '2', '0', '0', '1', '0', '0', '0', '0', '0', '0', '1', '+',
			     '5', '2', '0', '0', '1', '0', '1', '0', '0', '0', '0', '3', '+',
			     '0', '1', '0', '2', 'A', '3', 'B', '4', 'C', '5', 'D', '6'};
	char *copy = malloc(sizeof(code));
	memcpy(copy, code, sizeof(code));
	CHECK_EQ(cheat_decode(&d, copy, sizeof(code), NULL, 0, &n), CHEAT_TOO_BIG);
	CHECK_EQ(n, 7);
	free(copy);
}

void test_cheatcode_detect(void)
{
	s32 score;
	CHECK_EQ(cheat_detect("82001000 1234", 13, &score), CHEAT_FMT_CODEBREAKER);
	CHECK_EQ(score, 0x100);
	CHECK_EQ(cheat_detect("820010001234", 12, NULL), CHEAT_FMT_CODEBREAKER);
	CHECK_EQ(cheat_detect("02001000:12", 11, &score), CHEAT_FMT_VBA);
	CHECK_EQ(cheat_detect("03001234 000000AB", 17, &score), CHEAT_FMT_GSAV1_RAW);
	CHECK(score > 0);
	CHECK_EQ(cheat_detect("00300000 00000078", 17, NULL), CHEAT_FMT_PARV3_RAW);
	CHECK_EQ(cheat_detect("A9873144 203A53CE", 17, NULL), CHEAT_FMT_GSAV1);
	CHECK_EQ(cheat_detect("e51e97c3 7858e4eb", 17, NULL), CHEAT_FMT_PARV3);
	CHECK_EQ(cheat_detect("3300075C 00??", 13, NULL), CHEAT_FMT_CODEBREAKER);
	CHECK_EQ(cheat_detect("82001000 1234+32001002 0001", 27, NULL), CHEAT_FMT_AUTO);
	CHECK_EQ(cheat_detect("hello", 5, &score), CHEAT_FMT_AUTO);
	CHECK_EQ(score, 0);
	CHECK_EQ(cheat_detect("", 0, NULL), CHEAT_FMT_AUTO);
}

// ------------------------------------------------------------------------

static struct cheat_write writes[16];
static struct cheat_patch patches[8];

static struct cheat_op op(u8 type, u8 width, u32 address, u32 value, u32 count)
{
	struct cheat_op o = {address, value, count, (s32)width, 0, 0, type, width};
	return o;
}

void test_cheatcode_lower(void)
{
	struct cheat_op in[4];
	u32 nw = 0, np = 0;

	// 16- and 32-bit writes split little-endian
	in[0] = op(CHEAT_OP_WRITE, 2, 0x02000010, 0xBEEF, 1);
	in[1] = op(CHEAT_OP_WRITE, 4, 0x03000020, 0x12345678, 1);
	CHECK_EQ(cheat_lower(in, 2, writes, 16, &nw, patches, 8, &np), CHEAT_LOWER_OK);
	CHECK_EQ(nw, 6);
	CHECK_EQ(np, 0);
	CHECK_EQ(writes[0].address, 0x02000010);
	CHECK_EQ(writes[0].value, 0xEF);
	CHECK_EQ(writes[1].value, 0xBE);
	CHECK_EQ(writes[2].address, 0x03000020);
	CHECK_EQ(writes[2].value, 0x78);
	CHECK_EQ(writes[5].address, 0x03000023);
	CHECK_EQ(writes[5].value, 0x12);

	// Fills expand, appended after what's there; IO keeps its address
	in[0] = op(CHEAT_OP_WRITE, 1, 0x02000100, 0x10, 3);
	in[0].addr_step = 2;
	in[0].value_step = 1;
	in[1] = op(CHEAT_OP_WRITE, 1, 0x04000200, 0x80, 1);
	CHECK_EQ(cheat_lower(in, 2, writes, 16, &nw, patches, 8, &np), CHEAT_LOWER_OK);
	CHECK_EQ(nw, 10);
	CHECK_EQ(writes[6].address, 0x02000100);
	CHECK_EQ(writes[8].address, 0x02000104);
	CHECK_EQ(writes[8].value, 0x12);
	CHECK_EQ(writes[9].address, 0x04000200);

	// Too many: nothing is appended
	in[0] = op(CHEAT_OP_WRITE, 4, 0x02000000, 0, 2);
	CHECK_EQ(cheat_lower(in, 1, writes, 16, &nw, patches, 8, &np), CHEAT_LOWER_TOO_MANY_WRITES);
	CHECK_EQ(nw, 10);
	in[0] = op(CHEAT_OP_WRITE, 1, 0x02000000, 0, 0xFFFFFFFF);  // a huge fill stops early
	in[0].addr_step = 0;
	CHECK_EQ(cheat_lower(in, 1, writes, 16, &nw, patches, 8, &np), CHEAT_LOWER_TOO_MANY_WRITES);
	CHECK_EQ(nw, 10);

	// ROM writes and patches become halfword patches with masks
	nw = 0;
	in[0] = op(CHEAT_OP_ROM_PATCH, 2, 0x0802468A, 0x4770, 1);
	in[1] = op(CHEAT_OP_WRITE, 1, 0x08000101, 0xAB, 1);
	in[2] = op(CHEAT_OP_WRITE, 4, 0x08000200, 0x11223344, 1);
	in[3] = op(CHEAT_OP_ROM_PATCH, 1, 0x08000300, 0xCD, 1);
	CHECK_EQ(cheat_lower(in, 4, writes, 16, &nw, patches, 8, &np), CHEAT_LOWER_OK);
	CHECK_EQ(nw, 0);
	CHECK_EQ(np, 5);
	CHECK_EQ(patches[0].offset, 0x2468A);
	CHECK_EQ(patches[0].value, 0x4770);
	CHECK_EQ(patches[0].mask, 0xFFFF);
	CHECK_EQ(patches[1].offset, 0x100);
	CHECK_EQ(patches[1].value, 0xAB00);
	CHECK_EQ(patches[1].mask, 0xFF00);
	CHECK_EQ(patches[2].offset, 0x200);
	CHECK_EQ(patches[2].value, 0x3344);
	CHECK_EQ(patches[3].offset, 0x202);
	CHECK_EQ(patches[3].value, 0x1122);
	CHECK_EQ(patches[4].mask, 0x00FF);
	CHECK_EQ(cheat_lower(in, 4, writes, 16, &nw, patches, 8, &np), CHEAT_LOWER_TOO_MANY_PATCHES);
	CHECK_EQ(np, 5);

	// What needs v0.8's interpreter
	static const u8 interpreted[] = {CHEAT_OP_WRITE_PTR, CHEAT_OP_ADD,     CHEAT_OP_OR,        CHEAT_OP_AND,
					 CHEAT_OP_IF_EQ,     CHEAT_OP_IF_NAND, CHEAT_OP_IF_BUTTON, CHEAT_OP_SLOWDOWN};
	u32 i;
	for (i = 0; i < sizeof(interpreted); ++i) {
		in[0] = op(CHEAT_OP_WRITE, 2, 0x02000000, 1, 1);
		in[1] = op(interpreted[i], 2, 0x02000000, 1, 1);
		CHECK_EQ(cheat_lower(in, 2, writes, 16, &nw, patches, 8, &np), CHEAT_LOWER_NEEDS_INTERPRETER);
	}

	// Bad addresses, checked first; RAM mirrors are good
	static const u32 bad[] = {0x00000000, 0x01000000, 0x040003FF, 0x05000400,
				  0x06018000, 0x0A000000, 0x0E010000, 0x10000000};
	for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
		in[0] = op(CHEAT_OP_IF_EQ, 2, 0x02000000, 1, 1);
		in[1] = op(CHEAT_OP_WRITE, 2, bad[i], 1, 1);
		CHECK_EQ(cheat_lower(in, 2, writes, 16, &nw, patches, 8, &np), CHEAT_LOWER_BAD_ADDRESS);
	}
	in[0] = op(CHEAT_OP_WRITE, 1, 0x0203FFFF, 1, 2);  // a fill running past EWRAM's mirror end is fine
	CHECK_EQ(cheat_lower(in, 1, writes, 16, &nw, patches, 8, &np), CHEAT_LOWER_OK);
	in[0] = op(CHEAT_OP_WRITE, 1, 0x02FFFFFF, 1, 2);  // past the region isn't
	CHECK_EQ(cheat_lower(in, 1, writes, 16, &nw, patches, 8, &np), CHEAT_LOWER_BAD_ADDRESS);
	in[0] = op(CHEAT_OP_ROM_PATCH, 2, 0x02000000, 1, 1);
	CHECK_EQ(cheat_lower(in, 1, writes, 16, &nw, patches, 8, &np), CHEAT_LOWER_BAD_ADDRESS);
	in[0] = op(CHEAT_OP_WRITE, 2, 0x02000000, 1, 0);  // a fill of none writes nothing
	nw = 0;
	CHECK_EQ(cheat_lower(in, 1, writes, 16, &nw, patches, 8, &np), CHEAT_LOWER_OK);
	CHECK_EQ(nw, 0);
}

// Every prefix of real codes, and random bytes, decode without reading past
// len (ASan) or overflowing (UBSan)
void test_cheatcode_fuzz(void)
{
	static const char *const codes[] = {
		"930CFE0E61A7+5E4E1F94523A+DB3A64369A43",
		"f57c7bcb+adc632b9+851b05e0594b5d88",
		"72001000 0001+52001010 0003+0102A3B4 C5D6",
		"00000000 84225990+00010153+01080001+92225990 00000000+00000000+40000000",
		"820001A8:FFFF+02001000:12345678",
	};
	struct cheat_decoder d;
	u32 i, k, n;
	cheat_decoder_init(&d);
	for (i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i) {
		u32 len = strlen(codes[i]);
		for (k = 0; k <= len; ++k) {
			char *copy = malloc(k ? k : 1);
			memcpy(copy, codes[i], k);
			cheat_decode(&d, copy, k, ops, MAX_OPS, &n);
			cheat_decode(&d, copy, k, ops, 1, &n);
			free(copy);
		}
	}
	static const char alphabet[] = "0123456789ABCDEFabcdef?X+ :\t";
	u32 seed = 12345;
	for (i = 0; i < 3000; ++i) {
		u32 len = i % 60;
		char *copy = malloc(len ? len : 1);
		for (k = 0; k < len; ++k) {
			seed = seed * 1103515245 + 12345;
			copy[k] = alphabet[(seed >> 16) % (sizeof(alphabet) - 1)];
		}
		if (i % 500 == 0)
			cheat_decoder_init(&d);
		if (cheat_decode(&d, copy, len, ops, MAX_OPS, &n) == CHEAT_OK) {
			u32 nw = 0, np = 0;
			cheat_lower(ops, n, writes, 16, &nw, patches, 8, &np);
		}
		free(copy);
	}
}
