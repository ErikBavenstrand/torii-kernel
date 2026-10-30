// The letter jumps of A-Z lists (source/ui/letter.c) and the brightness
// blend's colour maths the popups copy (ui_colour_bldy, source/ui/ui_theme.c).
#include <string.h>

#include "../../source/library.h"
#include "../../source/ui/letter.h"
#include "../../source/ui/ui.h"
#include "../kernel/torii_test.h"
#include "tests.h"

void test_letter_of(void)
{
	CHECK_EQ(letter_of("Advance Wars"), LETTER_A);
	CHECK_EQ(letter_of("advance wars"), LETTER_A);
	CHECK_EQ(letter_of("Zelda"), LETTER_Z);
	CHECK_EQ(letter_of("\xC3\x89lite"), LETTER_A + ('E' - 'A'));  // É folds to E
	CHECK_EQ(letter_of("\xC3\x9F"), LETTER_A + ('S' - 'A'));       // ß folds to ss
	CHECK_EQ(letter_of("\xC3\x85ngest"), LETTER_A);            // Å goes with A
	CHECK_EQ(letter_of("\xC3\x96stersund"), LETTER_A + ('O' - 'A'));  // Ö with O
	CHECK_EQ(letter_of("007"), LETTER_HASH_LOW);
	CHECK_EQ(letter_of("!Bang"), LETTER_HASH_LOW);
	CHECK_EQ(letter_of(""), LETTER_HASH_LOW);
	CHECK_EQ(letter_of("\xE3\x83\x9D"), LETTER_HASH_HIGH);  // katakana: after Z
	CHECK(!strcmp(letter_text(LETTER_A), "A"));
	CHECK(!strcmp(letter_text(LETTER_Z), "Z"));
	CHECK(!strcmp(letter_text(LETTER_HASH_LOW), "#"));
	CHECK(!strcmp(letter_text(LETTER_HASH_HIGH), "#"));
	CHECK(!strcmp(letter_text(LETTER_NONE), ""));
}

// A list in the order the lists sort, with letters missing
static const char *const names[] = {
	"2 Games in 1", "Advance Wars", "Astro Boy", "Boktai",     "Castlevania", "Mario Kart",
	"Metroid",      "Mother 3",     "Riviera",   "Wario Ware", "Yoshi",       "\xE3\x83\x9D",
};
#define NAMES (sizeof(names) / sizeof(names[0]))

static u32 name_letter(void *ctx, u32 i)
{
	(void)ctx;
	return letter_of(names[i]);
}

void test_letter_sorted(void)
{
	// The lists' order keeps the letters from going down
	for (u32 i = 1; i < NAMES; i++) {
		char a[64], b[64];
		u32 la = library_sort_key(a, sizeof(a), names[i - 1]), lb = library_sort_key(b, sizeof(b), names[i]);
		int cmp = memcmp(a, b, la < lb ? la : lb);
		CHECK(cmp < 0 || (cmp == 0 && la < lb));
		CHECK(name_letter(0, i - 1) <= name_letter(0, i));
	}
}

void test_letter_jump(void)
{
	// RIGHT: the next letter with games, skipping the empty ones
	CHECK_EQ(letter_jump(NAMES, 0, 1, name_letter, 0), 1);  // # to A
	CHECK_EQ(letter_jump(NAMES, 1, 1, name_letter, 0), 3);  // A to B, from A's first
	CHECK_EQ(letter_jump(NAMES, 2, 1, name_letter, 0), 3);  // and from inside A
	CHECK_EQ(letter_jump(NAMES, 4, 1, name_letter, 0), 5);  // C to M: D-L have none
	CHECK_EQ(letter_jump(NAMES, 6, 1, name_letter, 0), 8);  // M to R
	// The two "#" runs (before A, after Z) are one stop a lap, at the first's start
	CHECK_EQ(letter_jump(NAMES, 10, 1, name_letter, 0), 0);  // Y to "#"
	CHECK_EQ(letter_jump(NAMES, 11, 1, name_letter, 0), 1);  // from the second run: on to A
	// LEFT: the first game of the letter before
	CHECK_EQ(letter_jump(NAMES, 7, -1, name_letter, 0), 4);  // M to C
	CHECK_EQ(letter_jump(NAMES, 2, -1, name_letter, 0), 0);  // A to #
	CHECK_EQ(letter_jump(NAMES, 0, -1, name_letter, 0), 10);  // wraps past the second "#" run to Y
	CHECK_EQ(letter_jump(NAMES, 11, -1, name_letter, 0), 10);
	CHECK_EQ(letter_jump(NAMES, 8, -1, name_letter, 0), 5);   // R to M's first
	CHECK_EQ(letter_jump(0, 3, 1, name_letter, 0), 3);
	// The card's neighbours: where LEFT and RIGHT go
	u32 prev, next;
	letter_around(NAMES, 6, name_letter, 0, &prev, &next);
	CHECK_EQ(prev, LETTER_A + 2);  // C
	CHECK_EQ(next, LETTER_A + 17);  // R
	letter_around(NAMES, 0, name_letter, 0, &prev, &next);
	CHECK_EQ(prev, LETTER_A + 24);  // Y
	CHECK_EQ(next, LETTER_A);
	letter_around(NAMES, 10, name_letter, 0, &prev, &next);
	CHECK_EQ(prev, LETTER_A + 22);  // W
	CHECK_EQ(next, LETTER_HASH_LOW);
	letter_around(3, 1, name_letter, 0, &prev, &next);  // "2 Games", Advance Wars, Astro Boy
	CHECK_EQ(prev, LETTER_HASH_LOW);
	CHECK_EQ(next, LETTER_HASH_LOW);
	letter_around(2, 1, name_letter, 0, &prev, &next);  // only # and A
	CHECK_EQ(prev, LETTER_HASH_LOW);
	CHECK_EQ(next, LETTER_HASH_LOW);
}

static u32 same_letter(void *ctx, u32 i)
{
	return letter_of(((const char *const *)ctx)[i]);
}

void test_letter_one(void)
{
	// A list of one letter: no neighbours, and the jumps go to its start
	static const char *const same[] = {"Mario", "Metroid", "Mother"};
	void *ctx = (void *)same;
	u32 prev, next;
	CHECK_EQ(letter_jump(3, 2, 1, same_letter, ctx), 0);
	CHECK_EQ(letter_jump(3, 2, -1, same_letter, ctx), 0);
	letter_around(3, 1, same_letter, ctx, &prev, &next);
	CHECK_EQ(prev, LETTER_NONE);
	CHECK_EQ(next, LETTER_NONE);
	// Only the two "#" runs: one stop
	static const char *const hashes[] = {"1942", "\xE3\x83\x9D"};
	ctx = (void *)hashes;
	CHECK_EQ(letter_jump(2, 1, 1, same_letter, ctx), 0);
	CHECK_EQ(letter_jump(2, 0, -1, same_letter, ctx), 0);
	letter_around(2, 1, same_letter, ctx, &prev, &next);
	CHECK_EQ(prev, LETTER_NONE);
	CHECK_EQ(next, LETTER_NONE);
	// Only the run after Z: a stop of its own, as any letter
	static const char *const high[] = {"Advance", "\xE3\x83\x9D"};
	ctx = (void *)high;
	CHECK_EQ(letter_jump(2, 0, 1, same_letter, ctx), 1);
	CHECK_EQ(letter_jump(2, 1, 1, same_letter, ctx), 0);
}

void test_colour_bldy(void)
{
	// The hardware's maths: I + (31 - I) * EVY / 16 brightening, I - I * EVY / 16
	// darkening, per channel, rounded down
	for (u32 c = 0; c < 32; c++) {
		u16 colour = (u16)(c | (31 - c) << 5 | (c / 2) << 10);
		for (u32 evy = 0; evy <= 16; evy++) {
			u16 up = ui_colour_bldy(colour, evy, 1), down = ui_colour_bldy(colour, evy, 0);
			CHECK_EQ(up & 31, c + ((31 - c) * evy >> 4));
			CHECK_EQ(down & 31, c - (c * evy >> 4));
			CHECK_EQ(up >> 5 & 31, (31 - c) + (c * evy >> 4));
			CHECK_EQ(down >> 10 & 31, c / 2 - ((c / 2) * evy >> 4));
		}
	}
	CHECK_EQ(ui_colour_bldy(0x1234, 0, 1), 0x1234);
	CHECK_EQ(ui_colour_bldy(0x1234, 16, 1), 0x7FFF);
	CHECK_EQ(ui_colour_bldy(0x1234, 16, 0), 0);
	CHECK_EQ(ui_colour_bldy(0x1234, 40, 0), 0);  // past 16: 16
}
