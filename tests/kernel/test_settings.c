// Settings that decide how a game launches (source/Ezcard_OP.c,
// source/ezkernelnew.c).
#include <gba_base.h>

#include "../../source/Ezcard_OP.h"
#include "../../source/ez_define.h"
#include "../../source/lang.h"
#include "../../source/settings.h"
#include "torii_test.h"
#include "tests.h"

u32 Game_hardware(u32 is_EMU, const u8 *gamecode, u8 gb_cart_type);
extern u16 gl_game_clock;
extern u16 gl_game_rumble;

void test_game_hardware(void)
{
	CHECK_EQ(Game_hardware(0, (const u8 *)"V49E", 0), GAME_USES_RUMBLE);  // Drill Dozer
	CHECK_EQ(Game_hardware(0, (const u8 *)"RZWJ", 0), GAME_USES_RUMBLE);  // WarioWare: Twisted!
	CHECK_EQ(Game_hardware(0, (const u8 *)"BPEE", 0), GAME_USES_CLOCK);   // Pokémon Emerald
	CHECK_EQ(Game_hardware(1, (const u8 *)"\0\0\0\0", 0x10), GAME_USES_CLOCK);  // MBC3+TIMER: Pokémon Crystal
	CHECK_EQ(Game_hardware(1, (const u8 *)"\0\0\0\0", 0x1E), GAME_USES_RUMBLE);  // MBC5+RUMBLE: Pokémon Pinball
	CHECK_EQ(Game_hardware(2, (const u8 *)"\0\0\0\0", 0x00), 0);  // ROM only: Tetris
	CHECK_EQ(Game_hardware(3, (const u8 *)"\0\0\0\0", 0), GAME_USES_CLOCK);  // NES
}

static u16 gpio(u32 uses, u16 clock, u16 rumble)
{
	gl_game_uses = uses;
	gl_game_clock = clock;
	gl_game_rumble = rumble;
	return Game_GPIO_status();
}

void test_clock_and_rumble_are_separate(void)
{
	u16 clock = gl_game_clock, rumble = gl_game_rumble;
	u32 uses = gl_game_uses;
	// Turning CLOCK off leaves rumble on (ez-flash/omega-de-kernel#5).
	CHECK_EQ(gpio(GAME_USES_RUMBLE, 0, 1), 1);
	CHECK_EQ(gpio(GAME_USES_RUMBLE, 1, 0), 0);
	CHECK_EQ(gpio(GAME_USES_CLOCK, 1, 0), 1);
	CHECK_EQ(gpio(GAME_USES_CLOCK, 0, 1), 0);
	CHECK_EQ(gpio(0, 1, 1), 0);  // a game that uses neither
	gl_game_clock = clock;
	gl_game_rumble = rumble;
	gl_game_uses = uses;
}

extern u16 SET_info_buffer[0x200];

// SETTINGS stages its saves and writes the kernel NOR once, as it closes,
// only if a slot changed (Stage_SET_info, Commit_SET_info). Nothing here
// writes the NOR: Diagnostics runs these tests on the cart. A changed slot is
// staged only between two lines that can't fail, and the staging is
// discarded before any check, so no path leaves it on for a later commit.
void test_settings_staged_until_changed(void)
{
	u16 slots[assress_max];
	for (u32 a = 0; a < assress_max; a++)
		slots[a] = Read_SET_info(a);
	u32 unstaged = Commit_SET_info(SET_info_buffer);  // nothing staged
	Stage_SET_info(slots);
	u32 same = Commit_SET_info(SET_info_buffer);  // staged, the same as the NOR's

	// A changed slot reads as staged until the staging ends
	u16 dim = slots[assress_dim];
	slots[assress_dim] = (u16)(dim ^ 1);
	Stage_SET_info(slots);
	u16 staged_dim = Read_SET_info(assress_dim);
	u16 staged_language = Read_SET_info(assress_language);
	Discard_SET_info();
	u16 after_dim = Read_SET_info(assress_dim);
	// ... and changed back, there is nothing to write
	slots[assress_dim] = dim;
	Stage_SET_info(slots);
	u32 back = Commit_SET_info(SET_info_buffer);
	Discard_SET_info();  // whatever Commit did, staging is off

	CHECK_EQ(unstaged, 0);
	CHECK_EQ(same, 0);
	CHECK_EQ(staged_dim, (u16)(dim ^ 1));
	CHECK_EQ(staged_language, slots[assress_language]);
	CHECK_EQ(after_dim, dim);
	CHECK_EQ(back, 0);
	CHECK_EQ(Read_SET_info(assress_dim), dim);
}

// The settings model's values (source/settings_model.h) round-trip through
// their slots: every row's i-th value, set and staged together (the rows
// have slots of their own), reads back the same through Read_SET_info, and
// no slot outside the rows changes. Nothing is written: the staging is
// discarded after each pass.
static struct settings_values model_set EWRAM_BSS, model_read EWRAM_BSS;

static void read_slots(struct settings_values *v)
{
	u16 slots[assress_max];
	for (u32 a = 0; a < assress_max; a++)
		slots[a] = Read_SET_info(a);
	settings_values_init(v, slots);
}

static const u16 test_dims[] = {0, 1, 7, 60};
// L+R+SELECT, L+R+START, START+SELECT, R+B, R+UP+DOWN: no two in a row
// hold each other
static const u16 test_combos[] = {0x0304, 0x0308, 0x000C, 0x0102, 0x01C0};
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

// How many values the round trip tries on row id, and the i-th
static u32 test_values(u32 id)
{
	const struct settings_row *r = &settings_rows[id];
	if (r->slot == SET_NO_SLOT)
		return 0;
	if (r->kind == SET_NUMBER)
		return COUNT(test_dims);
	if (r->kind == SET_KEYS)
		return COUNT(test_combos);
	return id == SET_LANGUAGE ? lang_pack_count : r->count;
}

static u16 test_value(u32 id, u32 i)
{
	const struct settings_row *r = &settings_rows[id];
	if (r->kind == SET_NUMBER)
		return test_dims[i];
	if (r->kind == SET_KEYS)  // the two hotkeys never share a combination
		return test_combos[(i + (id == SET_SLEEP_KEYS)) % COUNT(test_combos)];
	return (u16)i;
}

void test_settings_model_round_trip(void)
{
	u32 trips = 0;
	for (u32 i = 0; i < SET_STARTS; i++) {
		u8 row_slot[assress_max] = {0};
		read_slots(&model_set);
		// The hotkeys parked on combinations the test doesn't use, so no
		// value is refused for the other's (L+LEFT+RIGHT, L+LEFT+A)
		static const u8 park_reset[3] = {9, 5, 4}, park_sleep[3] = {9, 5, 0};
		for (u32 k = 0; k < 3; k++) {
			model_set.slot[assress_edit_rtshotkey_0 + k] = park_reset[k];
			model_set.slot[assress_edit_sleephotkey_0 + k] = park_sleep[k];
			row_slot[assress_edit_rtshotkey_0 + k] = row_slot[assress_edit_sleephotkey_0 + k] = 1;
		}
		for (u32 id = 0; id < SET_ROWS; id++) {
			if (i >= test_values(id))
				continue;
			const struct settings_row *r = &settings_rows[id];
			if (r->kind == SET_KEYS)
				CHECK_EQ(settings_set_keys(&model_set, id, test_value(id, i)), SET_KEYS_OK);
			else
				settings_set(&model_set, id, test_value(id, i));
			u32 n = r->kind == SET_KEYS || r->kind == SET_COLOUR ? 3 : 1;
			for (u32 a = r->slot; a < r->slot + n; a++)
				row_slot[a] = 1;
		}
		settings_stage(&model_set);
		read_slots(&model_read);
		Discard_SET_info();
		for (u32 id = 0; id < SET_ROWS; id++) {
			if (i >= test_values(id))
				continue;
			CHECK_EQ(settings_get(&model_read, id), test_value(id, i));
			trips++;
		}
		for (u32 a = 0; a < assress_max; a++)
			if (!row_slot[a])
				CHECK_EQ(model_read.slot[a], model_set.loaded[a]);
	}
	CHECK(trips > 60);
	CHECK_EQ(Read_SET_info(assress_dim), model_set.loaded[assress_dim]);  // the staging is gone

	// settings_load reads the staged slots, and the clock
	read_slots(&model_set);
	settings_step(&model_set, SET_START_ON, 1);
	settings_stage(&model_set);
	settings_load(&model_read);
	Discard_SET_info();
	CHECK_EQ(settings_get(&model_read, SET_START_ON), settings_get(&model_set, SET_START_ON));
	CHECK(model_read.clock.year >= 2000 && model_read.clock.year <= 2099);
	CHECK(model_read.clock.month >= 1 && model_read.clock.month <= 12);
}
