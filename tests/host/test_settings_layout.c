// The Settings page's layout and hotkey capture
// (source/screens/settings_layout.c).
#include <string.h>

#include "../../source/lang.h"
#include "../../source/screens/settings_layout.h"
#include "../kernel/torii_test.h"
#include "tests.h"

#define A 0x001
#define B 0x002
#define SELECT 0x004
#define START 0x008
#define R 0x100
#define L 0x200

static struct settings_values v;
static struct settings_layout lay;

static void erased(void)
{
	u16 slots[assress_max];
	for (u32 a = 0; a < assress_max; a++)
		slots[a] = 0xFFFF;
	settings_values_init(&v, slots);
	settings_layout_build(&lay, &v);
}

void test_settings_layout_slots(void)
{
	LoadEnglish();
	erased();
	// Eight sections: a label, the rows, a gap each; 29 rows with the LED on
	CHECK_EQ(lay.rows, 29);
	CHECK_EQ(lay.count, 29 + 2 * SET_SECTIONS);
	CHECK_EQ(lay.slot[0].kind, SET_SLOT_LABEL);
	CHECK_EQ(lay.slot[1].kind, SET_SLOT_ROW);
	CHECK_EQ(lay.slot[1].id, SET_THEME);
	CHECK_EQ(lay.slot[6].kind, SET_SLOT_GAP);
	CHECK_EQ(lay.slot[lay.count - 1].kind, SET_SLOT_GAP);
	CHECK_EQ(lay.first[SET_SEC_LOOK], 1);
	CHECK_EQ(lay.last[SET_SEC_LOOK], 5);
	CHECK_EQ(lay.first[SET_SEC_CLOCK], 8);
	// Each row once, in the model's order, with its section's label before
	u32 id = 0;
	for (u32 s = 0; s < lay.count; s++) {
		const struct settings_slot *sl = &lay.slot[s];
		if (sl->kind != SET_SLOT_ROW)
			continue;
		CHECK_EQ(sl->id, id);
		CHECK_EQ(sl->section, settings_rows[id].section);
		CHECK(s >= lay.first[sl->section] && s <= lay.last[sl->section]);
		CHECK_EQ(lay.slot[lay.first[sl->section] - 1].kind, SET_SLOT_LABEL);
		CHECK_EQ(settings_layout_slot_of(&lay, id), s);
		CHECK_EQ(settings_layout_position(&lay, s), id + 1);
		id++;
	}
	CHECK_EQ(id, SET_LOGGING + 1);  // Language hidden
	CHECK_EQ(settings_layout_slot_of(&lay, SET_LANGUAGE), SET_LAYOUT_NONE);
	// The LED off hides its colours: two rows fewer, the slots after move up
	settings_set(&v, SET_LED, 0);
	settings_layout_build(&lay, &v);
	CHECK_EQ(lay.rows, 27);
	CHECK_EQ(settings_layout_slot_of(&lay, SET_LED_IDLE), SET_LAYOUT_NONE);
	CHECK_EQ(settings_layout_slot_of(&lay, SET_RESET_ON), settings_layout_slot_of(&lay, SET_LED) + 3);
}

void test_settings_layout_moves(void)
{
	LoadEnglish();
	erased();
	u32 theme = settings_layout_slot_of(&lay, SET_THEME), dim = settings_layout_slot_of(&lay, SET_DIM);
	u32 time = settings_layout_slot_of(&lay, SET_TIME_ROW), logging = settings_layout_slot_of(&lay, SET_LOGGING);
	// UP and DOWN skip the labels and gaps; an end wraps only on a fresh press
	CHECK_EQ(settings_layout_step(&lay, theme, 1, 0), theme + 1);
	CHECK_EQ(settings_layout_step(&lay, dim, 1, 0), time);
	CHECK_EQ(settings_layout_step(&lay, time, -1, 0), dim);
	CHECK_EQ(settings_layout_step(&lay, theme, -1, 0), theme);
	CHECK_EQ(settings_layout_step(&lay, theme, -1, 1), logging);
	CHECK_EQ(settings_layout_step(&lay, logging, 1, 0), logging);
	CHECK_EQ(settings_layout_step(&lay, logging, 1, 1), theme);
	// L and R: a section's first row, back to its own first, wrapping
	CHECK_EQ(settings_layout_section(&lay, theme, 1), time);
	CHECK_EQ(settings_layout_section(&lay, dim, -1), theme);
	CHECK_EQ(settings_layout_section(&lay, theme, -1), logging);
	CHECK_EQ(settings_layout_section(&lay, logging, 1), theme);
	CHECK_EQ(settings_layout_section(&lay, logging, -1), settings_layout_slot_of(&lay, SET_RESCAN));
	// A slot that isn't a row: the next row
	CHECK_EQ(settings_layout_row_at(&lay, 0), theme);
	CHECK_EQ(settings_layout_row_at(&lay, dim + 1), time);
	CHECK_EQ(settings_layout_row_at(&lay, lay.count - 1), logging);
}

// steps c through frames frames of held, returning the last answer
static u32 hold(struct settings_capture *c, u16 held, u32 frames)
{
	u32 r = SET_CAPTURE_WAIT;
	for (u32 i = 0; i < frames && r == SET_CAPTURE_WAIT; i++)
		r = settings_capture_step(c, held, 1);
	return r;
}

void test_settings_capture(void)
{
	struct settings_capture c;
	// A, which started it, counts only once let go; the largest set held 18
	// frames is the answer, once every key is up
	settings_capture_start(&c, A, 18, 300);
	CHECK_EQ(hold(&c, A, 30), SET_CAPTURE_WAIT);
	CHECK_EQ(c.held, 0);
	CHECK_EQ(hold(&c, A | L, 20), SET_CAPTURE_WAIT);
	CHECK_EQ(c.held, L);
	CHECK_EQ(hold(&c, L | R, 5), SET_CAPTURE_WAIT);
	CHECK_EQ(hold(&c, L | R | START, 18), SET_CAPTURE_WAIT);
	CHECK_EQ(hold(&c, L | R, 30), SET_CAPTURE_WAIT);
	CHECK_EQ(hold(&c, 0, 1), SET_CAPTURE_DONE);
	CHECK_EQ(c.best, L | R | START);
	// A set held too briefly doesn't count: the capture waits on
	settings_capture_start(&c, 0, 18, 300);
	CHECK_EQ(hold(&c, L | R, 10), SET_CAPTURE_WAIT);
	CHECK_EQ(hold(&c, 0, 5), SET_CAPTURE_WAIT);
	CHECK_EQ(hold(&c, R | SELECT, 18), SET_CAPTURE_WAIT);
	CHECK_EQ(hold(&c, 0, 1), SET_CAPTURE_DONE);
	CHECK_EQ(c.best, R | SELECT);
	// B alone cancels, tapped or held; B with others is a key
	settings_capture_start(&c, 0, 18, 300);
	CHECK_EQ(hold(&c, B, 2), SET_CAPTURE_WAIT);
	CHECK_EQ(hold(&c, 0, 1), SET_CAPTURE_CANCEL);
	settings_capture_start(&c, 0, 18, 300);
	CHECK_EQ(hold(&c, B, 40), SET_CAPTURE_WAIT);
	CHECK_EQ(hold(&c, 0, 1), SET_CAPTURE_CANCEL);
	settings_capture_start(&c, 0, 18, 300);
	CHECK_EQ(hold(&c, B | L, 20), SET_CAPTURE_WAIT);
	CHECK_EQ(hold(&c, 0, 1), SET_CAPTURE_DONE);
	CHECK_EQ(c.best, B | L);
	// The answer let go and B pressed on the same frame: the answer stands,
	// and B is a press of its own (the page's, after)
	settings_capture_start(&c, 0, 18, 300);
	CHECK_EQ(hold(&c, L | R | SELECT, 20), SET_CAPTURE_WAIT);
	CHECK_EQ(hold(&c, B, 1), SET_CAPTURE_DONE);
	CHECK_EQ(c.best, L | R | SELECT);
	CHECK_EQ(c.after, B);
	// ... but a key let go while others of the answer stay held is still the hold
	settings_capture_start(&c, 0, 18, 300);
	CHECK_EQ(hold(&c, L | R | SELECT, 20), SET_CAPTURE_WAIT);
	CHECK_EQ(hold(&c, L | R | B, 20), SET_CAPTURE_WAIT);
	CHECK_EQ(hold(&c, 0, 1), SET_CAPTURE_DONE);
	CHECK_EQ(c.best, L | R | B);
	CHECK_EQ(c.after, 0);
	// ... and keys rolled over (START held as L+R go) are the hold too
	settings_capture_start(&c, 0, 18, 300);
	CHECK_EQ(hold(&c, L | R, 20), SET_CAPTURE_WAIT);
	CHECK_EQ(hold(&c, L | R | START, 2), SET_CAPTURE_WAIT);
	CHECK_EQ(hold(&c, START | SELECT, 20), SET_CAPTURE_WAIT);
	CHECK_EQ(hold(&c, 0, 1), SET_CAPTURE_DONE);
	CHECK_EQ(c.best, START | SELECT);
	// Nothing pressed for 5 s gives up; a key held restarts the time
	settings_capture_start(&c, 0, 18, 300);
	CHECK_EQ(settings_capture_left(&c), 300);
	CHECK_EQ(hold(&c, 0, 200), SET_CAPTURE_WAIT);
	CHECK_EQ(settings_capture_left(&c), 100);
	CHECK_EQ(hold(&c, L, 5), SET_CAPTURE_WAIT);
	CHECK_EQ(settings_capture_left(&c), 300);
	CHECK_EQ(hold(&c, 0, 1), SET_CAPTURE_WAIT);  // too brief: waits on
	CHECK_EQ(hold(&c, 0, 298), SET_CAPTURE_WAIT);
	CHECK_EQ(hold(&c, 0, 2), SET_CAPTURE_CANCEL);
}
