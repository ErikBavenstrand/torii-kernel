// GB, GBC and NES cheats (source/cheatemu.c): the Game Genie decoders against
// second implementations, the patches they make in a game's image, and a cheat
// file of them through the cheat file model (source/cheatfile.c).
//
// The oracles are not cheatemu's own code:
//  - NES: the bit table of nesgg.txt ("NES Game Genie Code Format DOC v0.71" by
//    Benzene, https://www.nesdev.org/nesgg.txt): which bit of which letter is
//    which bit of the value, the address and the compare, and its worked
//    example SXIOPO (value AD, address 11D9, so 91D9 in the CPU's map).
//  - GB: mGBA's GBCheatAddGameGenieLine (src/gb/cheats.c, which Torii's
//    emu/mgba checkout holds), a different arrangement of the same code than
//    cheatemu's (gambatte's: cartridge.cpp's applyGameGenie).
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../source/cheatemu.h"
#include "../../source/cheatfile.h"
#include "../kernel/torii_test.h"
#include "tests.h"

// --- NES: nesgg.txt's tables ---------------------------------------------------

// "Char # 1-8, bit 3 2 1 0 maps to", from the document: '1'-'8' value bits
// (1 the top), 'A'-'O' address bits (A the top, bit 14), '!'-'*' compare
// bits ('!' the top, in the order ! @ # $ % ^ & *), '-' the length flag
static const char *const map6[6] = {"1678", "H234", "-IJK", "LABC", "DMNO", "5EFG"};
static const char *const map8[8] = {"1678", "H234", "-IJK", "LABC", "DMNO", "%EFG", "!^&*", "5@#$"};

static int letter_nibble(char c)
{
	static const char *const group1 = "APZLGITY", *const group2 = "EOXUKSVN";
	const char *p = strchr(group1, c);
	if (p)
		return (int)(p - group1);  // A P Z L G I T Y are 0000 to 0111
	p = strchr(group2, c);
	return p ? 8 + (int)(p - group2) : -1;  // E O X U K S V N are 1000 to 1111
}

struct nes_ref {
	u32 value, address, compare;
};

static struct nes_ref nes_oracle(const char *code)
{
	u32 n = (u32)strlen(code);
	struct nes_ref r = {0, 0, 0};
	const char *const *map = n == 6 ? map6 : map8;
	for (u32 i = 0; i < n; i++)
		for (u32 b = 0; b < 4; b++) {
			if ((letter_nibble(code[i]) >> (3 - b)) & 1) {
				char m = map[i][b];
				if (m >= '1' && m <= '8')
					r.value |= 1u << ('8' - m);
				else if (m >= 'A' && m <= 'O')
					r.address |= 1u << ('O' - m);
				else if (m != '-') {
					static const char order[] = "!@#$%^&*";
					r.compare |= 1u << (7 - (u32)(strchr(order, m) - order));
				}
			}
		}
	return r;
}

static u32 rnd_state = 12345;
static u32 rnd(void)
{
	rnd_state = rnd_state * 1664525u + 1013904223u;
	return rnd_state >> 8;
}

void test_cheatemu_nes_genie(void)
{
	struct emucheat e;
	u32 n;
	// The document's worked example, infinite lives in Super Mario Bros.
	CHECK_EQ(cheatemu_decode(CHEATEMU_NES, "SXIOPO", 6, &e, 1, &n), CHEATEMU_OK);
	CHECK_EQ(n, 1);
	CHECK_EQ(e.kind, CHEATEMU_GENIE);
	CHECK_EQ(e.value, 0xAD);
	CHECK_EQ(e.addr, 0x91D9);
	CHECK_EQ(e.has_compare, 0);
	struct nes_ref ref = nes_oracle("SXIOPO");
	CHECK_EQ(ref.value, 0xAD);
	CHECK_EQ(ref.address, 0x11D9);
	// Letters are 0 to F in order, either case
	CHECK_EQ(cheatemu_decode(CHEATEMU_NES, "aapzlg", 6, &e, 1, &n), CHEATEMU_OK);
	// Every letter alone, in every place, against the table: the 16 letters in each of 6 and 8 places
	static const char alphabet[] = "APZLGITYEOXUKSVN";
	for (u32 len = 6; len <= 8; len += 2)
		for (u32 pos = 0; pos < len; pos++)
			for (u32 k = 0; k < 16; k++) {
				char code[9] = "AAAAAAAA";
				code[len] = 0;
				code[pos] = alphabet[k];
				ref = nes_oracle(code);
				CHECK_EQ(cheatemu_decode(CHEATEMU_NES, code, len, &e, 1, &n), CHEATEMU_OK);
				CHECK_EQ(e.value, ref.value);
				CHECK_EQ(e.addr, 0x8000 | ref.address);
				CHECK_EQ(e.has_compare, len == 8);
				CHECK_EQ(e.compare, len == 8 ? ref.compare : 0);
			}
	// A million random codes of each length (the bits don't interact: a linear map)
	for (u32 t = 0; t < 1000000; t++) {
		char code[9];
		u32 len = t & 1 ? 8 : 6;
		for (u32 i = 0; i < len; i++)
			code[i] = alphabet[rnd() & 15];
		code[len] = 0;
		ref = nes_oracle(code);
		cheatemu_decode(CHEATEMU_NES, code, len, &e, 1, &n);
		if (e.value != ref.value || e.addr != (0x8000 | ref.address) ||
		    e.compare != (len == 8 ? ref.compare : 0)) {
			CHECK(0);
			printf("  %s: got %02X at %04X compare %02X\n", code, e.value, e.addr, e.compare);
			break;
		}
	}
	// Joined lines, spaces, and what isn't a code
	struct emucheat two[4];
	CHECK_EQ(cheatemu_decode(CHEATEMU_NES, "OZOYUEPX+OZVULSPX", 17, two, 4, &n), CHEATEMU_OK);
	CHECK_EQ(n, 2);
	CHECK(two[0].has_compare && two[1].has_compare);
	CHECK_EQ(cheatemu_decode(CHEATEMU_NES, "AOUGTAE", 7, &e, 1, &n), CHEATEMU_UNREADABLE);  // 7 letters
	CHECK_EQ(cheatemu_decode(CHEATEMU_NES, "SXIOPB", 6, &e, 1, &n), CHEATEMU_UNREADABLE);   // B isn't a letter
	CHECK_EQ(cheatemu_decode(CHEATEMU_NES, "", 0, &e, 1, &n), CHEATEMU_UNREADABLE);
	CHECK_EQ(cheatemu_decode(CHEATEMU_NES, "SXIOPO+SXIOPO", 13, &e, 1, &n), CHEATEMU_TOO_BIG);
	// Raw RAM writes: pokes at the folded address (0000-1FFF mirrors the 2 KB, 6000-7FFF the PRG-RAM)
	CHECK_EQ(cheatemu_decode(CHEATEMU_NES, "0754:00+0756:01", 15, two, 4, &n), CHEATEMU_OK);
	CHECK_EQ(n, 2);
	CHECK_EQ(two[0].kind, CHEATEMU_POKE);
	CHECK_EQ(two[0].addr, 0x0754);
	CHECK_EQ(two[0].value, 0x00);
	CHECK_EQ(two[1].addr, 0x0756);
	CHECK_EQ(two[1].value, 0x01);
	CHECK_EQ(cheatemu_decode(CHEATEMU_NES, "0B00:55", 7, two, 4, &n), CHEATEMU_OK);
	CHECK_EQ(two[0].addr, 0x0300);
	CHECK_EQ(cheatemu_decode(CHEATEMU_NES, "1FFF:ab", 7, two, 4, &n), CHEATEMU_OK);
	CHECK_EQ(two[0].addr, 0x07FF);
	CHECK_EQ(two[0].value, 0xAB);
	CHECK_EQ(cheatemu_decode(CHEATEMU_NES, "6000:5A", 7, two, 4, &n), CHEATEMU_OK);
	CHECK_EQ(two[0].addr, 0x6000);
	CHECK_EQ(cheatemu_decode(CHEATEMU_NES, "7FFF:01", 7, two, 4, &n), CHEATEMU_OK);
	CHECK_EQ(two[0].addr, 0x7FFF);
	// A poke and a Game Genie in one cheat
	CHECK_EQ(cheatemu_decode(CHEATEMU_NES, "0754:00+SXIOPO", 14, two, 4, &n), CHEATEMU_OK);
	CHECK_EQ(n, 2);
	CHECK_EQ(two[1].kind, CHEATEMU_GENIE);
	// Not RAM: the PPU, the registers, the ROM; and the compare form, which libretro has none of
	CHECK_EQ(cheatemu_decode(CHEATEMU_NES, "2000:00", 7, two, 4, &n), CHEATEMU_UNREADABLE);
	CHECK_EQ(cheatemu_decode(CHEATEMU_NES, "4015:0F", 7, two, 4, &n), CHEATEMU_UNREADABLE);
	CHECK_EQ(cheatemu_decode(CHEATEMU_NES, "5FFF:00", 7, two, 4, &n), CHEATEMU_UNREADABLE);
	CHECK_EQ(cheatemu_decode(CHEATEMU_NES, "8000:00", 7, two, 4, &n), CHEATEMU_UNREADABLE);
	CHECK_EQ(cheatemu_decode(CHEATEMU_NES, "0754?12:00", 10, two, 4, &n), CHEATEMU_UNREADABLE);
	CHECK_EQ(cheatemu_decode(CHEATEMU_NES, "0754:0", 6, two, 4, &n), CHEATEMU_UNREADABLE);
	CHECK_EQ(cheatemu_decode(CHEATEMU_NES, "$0D24206E", 9, two, 4, &n), CHEATEMU_UNREADABLE);
}

// --- GB: mGBA's reading ----------------------------------------------------------

// mGBA's hex12: three hex digits, 0xFFFF... for a bad one
static int mgba_hex12(const char *s, u16 *out)
{
	u16 v = 0;
	for (int i = 0; i < 3; i++) {
		char c = s[i];
		int d = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
		if (d < 0)
			return 0;
		v = (u16)(v << 4 | d);
	}
	*out = v;
	return 1;
}

#define ROR(v, n) (((v) >> (n)) | ((v) << (32 - (n))))

// src/gb/cheats.c's GBCheatAddGameGenieLine, as it is
static int gb_oracle(const char *line, u32 *address, u32 *value, int *has, u32 *compare)
{
	u16 op1, op2, op3 = 0x1000;
	u32 n = (u32)strlen(line);
	if ((n != 7 && n != 11) || line[3] != '-' || !mgba_hex12(line, &op1) || !mgba_hex12(line + 4, &op2))
		return 0;
	if (n == 11 && (line[7] != '-' || !mgba_hex12(line + 8, &op3)))
		return 0;
	*address = (u32)(op1 & 0xF) << 8 | ((op2 >> 4) & 0xFF) | (u32)((op2 & 0xF) ^ 0xF) << 12;
	*value = op1 >> 4;
	*has = op3 < 0x1000;
	if (*has) {
		u32 v = ((u32)(op3 & 0xF00) << 20) | (op3 & 0xF);
		v = ROR(v, 2);
		v |= v >> 24;
		v ^= 0xBA;
		*compare = v & 0xFF;
	}
	return 1;
}

static void check_gb(const char *code)
{
	struct emucheat e;
	u32 n, address, value, compare = 0;
	int has;
	CHECK(gb_oracle(code, &address, &value, &has, &compare));
	u8 r = cheatemu_decode(CHEATEMU_GB, code, (u32)strlen(code), &e, 1, &n);
	if (address >= 0x8000) {
		CHECK_EQ(r, CHEATEMU_UNREADABLE);  // no ROM there: refused (mGBA takes it)
		return;
	}
	if (r != CHEATEMU_OK || e.addr != address || e.value != value || e.has_compare != has ||
	    (has && e.compare != compare)) {
		CHECK(0);
		printf("  %s: got %02X at %04X (%d, %02X), mGBA %02X at %04X (%d, %02X)\n", code, e.value, e.addr,
		       e.has_compare, e.compare, value, address, has, compare);
	}
}

void test_cheatemu_gb_genie(void)
{
	static const char hex[] = "0123456789ABCDEF";
	char code[16];
	// Every 6-digit code (16 M): the whole of the map
	for (u32 v = 0; v < 0x1000000; v += 1 + (v & 7)) {
		for (u32 i = 0; i < 6; i++)
			code[i + (i >= 3)] = hex[(v >> (20 - 4 * i)) & 15];
		code[3] = '-';
		code[7] = 0;
		check_gb(code);
		if (torii_test_failures)
			return;
	}
	// Every compare group with every other group: the compare is a map of its two digits
	for (u32 z = 0; z < 4096; z++) {
		snprintf(code, sizeof(code), "A5C-1F3-%03X", z);
		check_gb(code);
		snprintf(code, sizeof(code), "09C-56B-%03X", z);
		check_gb(code);
	}
	for (u32 t = 0; t < 1000000; t++) {
		for (u32 i = 0; i < 9; i++)
			code[i + (i >= 3) + (i >= 6)] = hex[rnd() & 15];
		code[3] = code[7] = '-';
		code[11] = 0;
		check_gb(code);
	}
	struct emucheat e[3];
	u32 n;
	// libretro's: a 9-digit code, a pair joined by '+', a lower-case one
	CHECK_EQ(cheatemu_decode(CHEATEMU_GB, "FA0-999-4C1", 11, e, 3, &n), CHEATEMU_OK);
	CHECK_EQ(cheatemu_decode(CHEATEMU_GB, "09C-56B-E6E+09C-74A-E6E", 23, e, 3, &n), CHEATEMU_OK);
	CHECK_EQ(n, 2);
	CHECK_EQ(cheatemu_decode(CHEATEMU_GB, "fa0-999", 7, e, 3, &n), CHEATEMU_OK);
	CHECK(!e[0].has_compare);
	// What isn't one: wildcards, the Xploder's $, a number, 7 digits
	CHECK_EQ(cheatemu_decode(CHEATEMU_GB, "01??DCC4", 8, e, 3, &n), CHEATEMU_UNREADABLE);
	CHECK_EQ(cheatemu_decode(CHEATEMU_GB, "$0D24206E", 9, e, 3, &n), CHEATEMU_UNREADABLE);
	CHECK_EQ(cheatemu_decode(CHEATEMU_GB, "FA0-9999", 8, e, 3, &n), CHEATEMU_UNREADABLE);
	CHECK_EQ(cheatemu_decode(CHEATEMU_GB, "FA0999", 6, e, 3, &n), CHEATEMU_UNREADABLE);
	CHECK_EQ(cheatemu_decode(CHEATEMU_GBA, "FA0-999", 7, e, 3, &n), CHEATEMU_UNREADABLE);
	// GameShark TT VV LL HH (EnHacklopedia's Game Boy page): 01 writes VV at HHLL, 9B the
	// same with WRAM bank B for D000-DFFF (GBC); the address is the low byte first
	CHECK_EQ(cheatemu_decode(CHEATEMU_GB, "01995EDB", 8, e, 3, &n), CHEATEMU_OK);
	CHECK_EQ(n, 1);
	CHECK_EQ(e[0].kind, CHEATEMU_POKE);
	CHECK_EQ(e[0].value, 0x99);
	CHECK_EQ(e[0].addr, 0xDB5E);
	CHECK_EQ(e[0].compare, 0);  // the bank mapped now
	CHECK_EQ(cheatemu_decode(CHEATEMU_GB, "01FF00C1", 8, e, 3, &n), CHEATEMU_OK);
	CHECK_EQ(e[0].addr, 0xC100);
	CHECK_EQ(cheatemu_decode(CHEATEMU_GB, "01FF00E1", 8, e, 3, &n), CHEATEMU_OK);  // the echo
	CHECK_EQ(e[0].addr, 0xE100);
	CHECK_EQ(cheatemu_decode(CHEATEMU_GB, "01FF80FF", 8, e, 3, &n), CHEATEMU_OK);  // HRAM
	CHECK_EQ(e[0].addr, 0xFF80);
	CHECK_EQ(cheatemu_decode(CHEATEMU_GB, "01FFFEFF", 8, e, 3, &n), CHEATEMU_OK);
	CHECK_EQ(cheatemu_decode(CHEATEMU_GB, "01ff00dd", 8, e, 3, &n), CHEATEMU_OK);
	CHECK_EQ(e[0].addr, 0xDD00);
	CHECK_EQ(cheatemu_decode(CHEATEMU_GB, "93AB00D1", 8, e, 3, &n), CHEATEMU_OK);
	CHECK_EQ(e[0].addr, 0xD100);
	CHECK_EQ(e[0].value, 0xAB);
	CHECK_EQ(e[0].compare, 3);
	CHECK_EQ(cheatemu_decode(CHEATEMU_GB, "97AB00D1", 8, e, 3, &n), CHEATEMU_OK);
	CHECK_EQ(e[0].compare, 7);
	CHECK_EQ(cheatemu_decode(CHEATEMU_GB, "90AB00D1", 8, e, 3, &n), CHEATEMU_OK);
	CHECK_EQ(e[0].compare, 1);  // bank 0 is 1, as SVBK has it
	// Reads, doesn't run: no WRAM or HRAM there (the ROM, VRAM, cart RAM, OAM, I/O, IE), a cart RAM bank (8B), another type
	static const char *const cant[] = {"01FF0080", "01FF00A0", "01FF00BF", "01FF00FE", "01FF00FF",
					   "01FFFFFF", "01FF007F", "01FF0000", "80FF00A0", "8AFF00C0",
					   "98FF00D0", "00FF00C1", "11FF00C1", "FFFF00C1"};
	for (u32 i = 0; i < sizeof(cant) / sizeof(*cant); i++)
		CHECK_EQ(cheatemu_decode(CHEATEMU_GB, cant[i], 8, e, 3, &n), CHEATEMU_CANT_RUN);
	// One that doesn't run makes the cheat not run, a Game Genie beside a write both go
	CHECK_EQ(cheatemu_decode(CHEATEMU_GB, "01995EDB+01FF0080", 17, e, 3, &n), CHEATEMU_CANT_RUN);
	CHECK_EQ(cheatemu_decode(CHEATEMU_GB, "01995EDB+FA0-999-4C1", 20, e, 3, &n), CHEATEMU_OK);
	CHECK_EQ(n, 2);
	CHECK_EQ(e[1].kind, CHEATEMU_GENIE);
	// A sample of the cheat's lines the patch room keeps
	struct cheat_patch p;
	struct emucheat back;
	cheatemu_decode(CHEATEMU_GB, "09C-56B-E6E", 11, e, 3, &n);
	cheatemu_to_patch(&e[0], &p);
	cheatemu_from_patch(&p, &back);
	CHECK(!memcmp(&back, &e[0], sizeof(back)));
	CHECK_EQ(cheatfile_run_masked(&p), 1);
	cheatemu_decode(CHEATEMU_GB, "09C-56B", 7, e, 3, &n);
	cheatemu_to_patch(&e[0], &p);
	cheatemu_from_patch(&p, &back);
	CHECK(!memcmp(&back, &e[0], sizeof(back)));
}

// --- Patches ---------------------------------------------------------------------

struct image {
	u8 *bytes;
	u32 size;
};

static u8 get(void *ctx, u32 at)
{
	return ((struct image *)ctx)->bytes[at];
}

static void set(void *ctx, u32 at, u8 v)
{
	((struct image *)ctx)->bytes[at] = v;
}

static struct emucheat genie(u16 addr, u8 value, int has, u8 compare)
{
	struct emucheat e = {CHEATEMU_GENIE, value, compare, (u8)has, addr};
	return e;
}

void test_cheatemu_gb_patch(void)
{
	// 8 banks of 16 KB, each filled with its number + 1; bank 3 has a different byte at 5123
	struct image im = {malloc(8 * 0x4000), 8 * 0x4000};
	for (u32 i = 0; i < im.size; i++)
		im.bytes[i] = (u8)(i / 0x4000 + 1);
	struct cheatemu_rom rom;
	CHECK(cheatemu_rom_gb(&rom, im.size));
	struct cheatemu_done d = {0};
	// 6 digits at 5123: every bank from 1 (bank 0 isn't mapped there)
	struct emucheat e = genie(0x5123, 0xEE, 0, 0);
	cheatemu_patch(&rom, &e, get, set, &im, 100, &d);
	CHECK_EQ(d.patched, 7);
	for (u32 b = 0; b < 8; b++)
		CHECK_EQ(im.bytes[b * 0x4000 + 0x1123], b ? 0xEE : 1);
	CHECK_EQ(im.bytes[0x5123 - 0x4000 + 0x4000 * 0 + 1], 1);  // the next byte alone
	// With a compare: bank 3 only (its byte there is 4)
	for (u32 i = 0; i < im.size; i++)
		im.bytes[i] = (u8)(i / 0x4000 + 1);
	memset(&d, 0, sizeof(d));
	e = genie(0x5123, 0xEE, 1, 4);
	cheatemu_patch(&rom, &e, get, set, &im, 100, &d);
	CHECK_EQ(d.patched, 1);
	CHECK_EQ(d.other, 6);
	CHECK_EQ(im.bytes[3 * 0x4000 + 0x1123], 0xEE);
	CHECK_EQ(im.bytes[2 * 0x4000 + 0x1123], 3);
	// Below 4000 it is bank 0's address, with the compare checked there too
	memset(&d, 0, sizeof(d));
	e = genie(0x0150, 0x77, 1, 1);
	cheatemu_patch(&rom, &e, get, set, &im, 100, &d);
	CHECK_EQ(d.patched, 1);
	CHECK_EQ(im.bytes[0x150], 0x77);
	e = genie(0x0151, 0x77, 1, 9);
	cheatemu_patch(&rom, &e, get, set, &im, 100, &d);
	CHECK_EQ(d.patched, 1);
	CHECK_EQ(d.other, 1);
	// The checksum's samples (0, 1, 2, 3 of every 128 bytes of bank 0) are never patched
	memset(&d, 0, sizeof(d));
	e = genie(0x0100, 0x00, 0, 0);
	cheatemu_patch(&rom, &e, get, set, &im, 100, &d);
	CHECK_EQ(d.patched, 0);
	CHECK_EQ(d.sampled, 1);
	e = genie(0x0104, 0x00, 0, 0);  // the next byte past the four
	cheatemu_patch(&rom, &e, get, set, &im, 100, &d);
	CHECK_EQ(d.patched, 1);
	e = genie(0x4100, 0x00, 0, 0);  // in a switched bank: not sampled
	cheatemu_patch(&rom, &e, get, set, &im, 100, &d);
	CHECK_EQ(d.patched, 8);
	// The limit
	memset(&d, 0, sizeof(d));
	e = genie(0x6000, 0x11, 0, 0);
	cheatemu_patch(&rom, &e, get, set, &im, 3, &d);
	CHECK_EQ(d.patched, 3);
	CHECK_EQ(d.limit, 4);
	// A 32 KB game (no banks to choose): 4000 is the one place
	struct image small = {im.bytes, 0x8000};
	cheatemu_rom_gb(&rom, small.size);
	memset(&d, 0, sizeof(d));
	e = genie(0x4000, 0x11, 0, 0);
	cheatemu_patch(&rom, &e, get, set, &small, 100, &d);
	CHECK_EQ(d.patched, 1);
	free(im.bytes);
}

// A NES image of banks 16 KB banks, bank b's bytes b + 1, with an iNES header
static struct image nes_image(u32 banks, u8 mapper, u32 trainer, struct cheatemu_rom *rom)
{
	struct image im = {calloc(1, 16 + (trainer ? 512 : 0) + banks * 0x4000), 0};
	u32 at = 16 + (trainer ? 512 : 0);
	memcpy(im.bytes, "NES\x1A", 4);
	im.bytes[4] = (u8)banks;
	im.bytes[5] = 1;
	im.bytes[6] = (u8)((mapper & 15) << 4 | (trainer ? 4 : 0));
	im.bytes[7] = (u8)(mapper & 0xF0);
	for (u32 i = 0; i < banks * 0x4000; i++)
		im.bytes[at + i] = (u8)(i / 0x4000 + 1);
	im.size = at + banks * 0x4000;
	CHECK(cheatemu_rom_nes(rom, im.bytes, im.size));
	return im;
}

void test_cheatemu_nes_patch(void)
{
	struct cheatemu_rom rom;
	struct cheatemu_done d;
	struct emucheat e;
	// NROM-128: C123 and 8123 are one byte
	struct image im = nes_image(1, 0, 0, &rom);
	CHECK_EQ(rom.mapper, 0);
	CHECK_EQ(rom.prg_at, 16);
	memset(&d, 0, sizeof(d));
	e = genie(0xC123, 0x55, 0, 0);
	cheatemu_patch(&rom, &e, get, set, &im, 10, &d);
	CHECK_EQ(d.patched, 1);
	CHECK_EQ(im.bytes[16 + 0x123], 0x55);
	free(im.bytes);
	// NROM-256, with a trainer: PRG starts at 528
	im = nes_image(2, 0, 1, &rom);
	CHECK_EQ(rom.prg_at, 528);
	memset(&d, 0, sizeof(d));
	e = genie(0xC123, 0x55, 0, 0);
	cheatemu_patch(&rom, &e, get, set, &im, 10, &d);
	CHECK_EQ(im.bytes[528 + 0x4123], 0x55);
	CHECK_EQ(d.patched, 1);
	free(im.bytes);
	// UxROM (2), 8 banks: 8000-BFFF in every bank (compare picks), C000 the last
	im = nes_image(8, 2, 0, &rom);
	memset(&d, 0, sizeof(d));
	e = genie(0x9000, 0x55, 1, 6);
	cheatemu_patch(&rom, &e, get, set, &im, 10, &d);
	CHECK_EQ(d.patched, 1);
	CHECK_EQ(im.bytes[16 + 5 * 0x4000 + 0x1000], 0x55);
	CHECK_EQ(d.other, 7);
	memset(&d, 0, sizeof(d));
	e = genie(0xD000, 0x66, 0, 0);
	cheatemu_patch(&rom, &e, get, set, &im, 10, &d);
	CHECK_EQ(d.patched, 1);
	CHECK_EQ(im.bytes[16 + 7 * 0x4000 + 0x1000], 0x66);
	free(im.bytes);
	// MMC3 (4), 16 banks of 16 KB = 32 of 8 KB: E000 the last 8 KB, others any
	im = nes_image(16, 4, 0, &rom);
	memset(&d, 0, sizeof(d));
	e = genie(0xE005, 0x77, 0, 0);
	cheatemu_patch(&rom, &e, get, set, &im, 100, &d);
	CHECK_EQ(d.patched, 1);
	CHECK_EQ(im.bytes[16 + 16 * 0x4000 - 0x2000 + 5], 0x77);
	memset(&d, 0, sizeof(d));
	e = genie(0xA005, 0x78, 0, 0);
	cheatemu_patch(&rom, &e, get, set, &im, 100, &d);
	CHECK_EQ(d.patched, 32);
	free(im.bytes);
	// AxROM (7): 32 KB banks
	im = nes_image(8, 7, 0, &rom);
	memset(&d, 0, sizeof(d));
	e = genie(0x8010, 0x11, 1, 3);  // bank 2 of 16 KB banks is byte 3 at 32 KB bank 1's start
	cheatemu_patch(&rom, &e, get, set, &im, 100, &d);
	CHECK_EQ(d.patched, 1);
	CHECK_EQ(im.bytes[16 + 0x8000 + 0x10], 0x11);
	free(im.bytes);
	// An unknown mapper (5): a compare patches by the byte, at 8 KB banks (the
	// compare 2 is 16 KB bank 1: two 8 KB ones); none, no patch
	im = nes_image(2, 5, 0, &rom);
	memset(&d, 0, sizeof(d));
	e = genie(0x8010, 0x11, 0, 0);
	cheatemu_patch(&rom, &e, get, set, &im, 100, &d);
	CHECK_EQ(d.patched, 0);
	CHECK_EQ(d.unknown, 1);
	e = genie(0x8010, 0x11, 1, 2);
	cheatemu_patch(&rom, &e, get, set, &im, 100, &d);
	CHECK_EQ(d.patched, 2);
	free(im.bytes);
	// The sampled bytes of PRG's first 16 KB (every 128th, four), never: 8000 is PRG's 0
	im = nes_image(2, 0, 0, &rom);
	memset(&d, 0, sizeof(d));
	e = genie(0x8080, 0x11, 0, 0);
	cheatemu_patch(&rom, &e, get, set, &im, 100, &d);
	CHECK_EQ(d.sampled, 1);
	CHECK_EQ(d.patched, 0);
	free(im.bytes);
	// A header that isn't iNES, or a file short of its PRG
	u8 bad[16] = "NOT AN INES HDR";
	CHECK(!cheatemu_rom_nes(&rom, bad, 100000));
	u8 hdr[16] = {'N', 'E', 'S', 0x1A, 2};
	CHECK(!cheatemu_rom_nes(&rom, hdr, 16 + 0x4000));
	CHECK(cheatemu_rom_nes(&rom, hdr, 16 + 0x8000));
}

// --- Through the cheat file model --------------------------------------------------

static struct cheatfile file;
static struct cheatfile_cheat cheats[CHEATFILE_MAX];
static struct cheatfile_work work;

static void load(u32 console, const char *text)
{
	static char *copy;
	free(copy);
	copy = strdup(text);
	cheatfile_parse(&file, copy, (u32)strlen(text), cheats, CHEATFILE_MAX);
	work.console = (u8)console;
	cheatfile_begin(&work);
	while (cheatfile_next(&file, &work) >= 0)
		;
}

void test_cheatemu_file(void)
{
	load(CHEATEMU_GB, "cheat0_desc = \"Lives\"\ncheat0_code = \"09C-56B-E6E+09C-74A-E6E\"\n"
			  "cheat1_desc = \"RAM\"\ncheat1_code = \"01995EDB\"\n"
			  "cheat2_desc = \"Bad\"\ncheat2_code = \"01??DCC4\"\n"
			  "cheat3_desc = \"Again\"\ncheat3_code = \"09C-56B-E6E+09C-74A-E6E\"\n");
	CHECK_EQ(file.count, 4);
	CHECK_EQ(cheats[0].status, CHEATFILE_OK);
	CHECK(cheats[0].patches > 0);
	CHECK_EQ(cheats[0].bytes, 0);
	CHECK_EQ(cheats[1].status, CHEATFILE_OK);  // 01995EDB: a poke
	CHECK_EQ(cheats[2].status, CHEATFILE_UNREADABLE);
	CHECK_EQ(cheats[3].status, CHEATFILE_SAME);
	CHECK_EQ(cheatfile_why(&file, 0), CHEATFILE_PICKABLE);
	CHECK_EQ(cheatfile_why(&file, 1), CHEATFILE_PICKABLE);
	CHECK_EQ(cheatfile_why(&file, 2), CHEATFILE_WHY_UNREADABLE);
	CHECK(cheatfile_toggle(&file, 0));
	CHECK(cheatfile_toggle(&file, 1));
	CHECK_EQ(cheatfile_picks(&file), 2);
	// It sets at launch, whatever the keys say
	CHECK_EQ(cheatfile_activation(&cheats[0]), CHEATKEYS_AT_LAUNCH);
	CHECK_EQ(cheatfile_activation(&cheats[1]), CHEATKEYS_ALWAYS_ON);
	load(CHEATEMU_GB, "cheat0_code = \"01FF0080\"\n");
	CHECK_EQ(cheats[0].status, CHEATFILE_CANT_RUN);
	CHECK_EQ(cheatfile_why(&file, 0), CHEATFILE_WHY_CANT_RUN);
	// The same file as a GBA's is unreadable: the console decides
	load(CHEATEMU_GBA, "cheat0_code = \"09C-56B-E6E\"\n");
	CHECK(cheats[0].status != CHEATFILE_OK || work.n_patches == 0);
	// NES
	load(CHEATEMU_NES, "cheat0_code = \"SXIOPO\"\ncheat1_code = \"0754:00\"\ncheat2_code = "
			   "\"SZESPUVK\"\ncheat3_code = \"AEVYZLAE+ZEEXIPIE\"\n");
	CHECK_EQ(cheats[0].status, CHEATFILE_OK);
	CHECK_EQ(cheats[1].status, CHEATFILE_OK);  // a poke: in the table budget, not the patches'
	CHECK_EQ(cheats[1].bytes, CHEATFILE_POKE_WORDS * 4);
	CHECK(cheats[1].patches > 0);
	CHECK_EQ(cheatfile_activation(&cheats[1]), CHEATKEYS_ALWAYS_ON);
	CHECK(cheatfile_ungated(&cheats[1]));
	CHECK_EQ(cheats[0].bytes, 0);
	CHECK_EQ(cheats[2].status, CHEATFILE_OK);
	CHECK_EQ(cheats[3].status, CHEATFILE_OK);
	CHECK(cheats[3].patches >= cheats[0].patches);
	// A cheat of more lines than the room is too big
	char text[2000] = "cheat0_code = \"";
	for (u32 i = 0; i < CHEATFILE_EMU_LINES + 1; i++)
		strcat(text, i ? "+SXIOPO" : "SXIOPO");
	strcat(text, "\"\n");
	load(CHEATEMU_NES, text);
	CHECK_EQ(cheats[0].status, CHEATFILE_BIG);
	// The picked pokes fit the emulator's table: 63 of one-line cheats, not the 64th
	static char many[64 * 40];
	char *p = many;
	for (u32 i = 0; i < 64; i++)
		p += sprintf(p, "cheat%u_code = \"%04X:01\"\n", i, (unsigned)(0x300 + i));
	load(CHEATEMU_NES, many);
	CHECK_EQ(file.count, 64);
	u32 picked = 0;
	for (u32 i = 0; i < 64; i++)
		picked += (u32)cheatfile_toggle(&file, i);
	CHECK_EQ(picked, TORII_CHEATS_MAX - 1);
	CHECK_EQ(cheatfile_why(&file, 63), CHEATFILE_WHY_FULL);
	// One cheat of 64 poke lines is too big, of 63 fits
	p = many + sprintf(many, "cheat0_code = \"");
	for (u32 i = 0; i < 64; i++)
		p += sprintf(p, i ? "+%04X:01" : "%04X:01", (unsigned)(0x300 + i));
	strcat(many, "\"\n");
	load(CHEATEMU_NES, many);
	CHECK_EQ(cheats[0].status, CHEATFILE_BIG);
	work.console = CHEATEMU_GBA;
}

void test_cheatemu_table(void)
{
	// A built-in image: aligned magic once, found; twice or none, not
	static u8 image[512] __attribute__((aligned(4)));
	CHECK_EQ(cheatemu_find_table(image, sizeof(image)), 0xFFFFFFFFu);
	memcpy(image + 64, TORII_CHEATS_MAGIC, 8);
	CHECK_EQ(cheatemu_find_table(image, sizeof(image)), 0xFFFFFFFFu);  // too little room after it for the table
	static u8 big[4096] __attribute__((aligned(4)));
	memcpy(big + 64, TORII_CHEATS_MAGIC, 8);
	CHECK_EQ(cheatemu_find_table(big, sizeof(big)), 64);
	memcpy(big + 130, TORII_CHEATS_MAGIC, 8);  // not 4-aligned: not a match
	CHECK_EQ(cheatemu_find_table(big, sizeof(big)), 64);
	memcpy(big + 2000, TORII_CHEATS_MAGIC, 8);
	CHECK_EQ(cheatemu_find_table(big, sizeof(big)), 0xFFFFFFFFu);  // two
	CHECK_EQ(sizeof(struct torii_cheats), 16 + 8 * TORII_CHEATS_MAX);
	CHECK_EQ(offsetof(struct torii_cheats, e), 16);
}
