// The launch's save slot choice (slot_picker.h): "Play with", on the save
// page (save_page.h): the slots on the left, the chosen one's facts on the
// right, as a game's Saves page shows them. A plays, B starts nothing;
// managing slots is the Saves page's (START in the game's popup).
//
// Its state lives in pReadCache (READCACHE_GSAVES, the Saves page's room:
// the two never run together) while it runs.
#include <stdio.h>
#include <string.h>

#include "slot_picker.h"
#include "save_page.h"
#include "slot_flow.h"
#include "../ez_define.h"
#include "../ezkernel.h"
#include "../lang.h"
#include "../tlog.h"
#include "../ui/art_ids.h"
#include "../ui/dialog.h"

extern u8 pReadCache[];

struct picker_state {
	char current[SAVESLOT_NAME_BYTES];
	struct slotstore_slot slots[SLOTSTORE_MAX_SLOTS];
	u32 count;
	struct save_page_row rows[SLOTSTORE_MAX_SLOTS];
	char values[32];
	struct ui_hint hints[2];
	struct save_page page;
};
_Static_assert(sizeof(struct picker_state) <= READCACHE_GSAVES_SIZE, "the slot choice fits READCACHE_GSAVES");
#define sp (*(struct picker_state *)(pReadCache + READCACHE_GSAVES))

int slot_picker_wanted(const char *rom_path, int force)
{
	if (force)
		return 1;
	return slot_flow_slots(rom_path, 0) >= 2;
}

static void pane(void *ctx, u32 row, struct save_pane *out)
{
	(void)ctx;
	if (row >= sp.count)
		return;
	const struct slotstore_slot *s = &sp.slots[row];
	save_page_ago(sp.values, sizeof(sp.values), s->saved);
	out->head = s->name;
	out->tag = strcmp(s->name, sp.current) ? NULL : S(STR_GSAVES_ACTIVE);
	out->keys = S(STR_GSAVES_PLAY_KEYS);
	out->values = sp.values;
	out->note = S(STR_GSAVES_NOTE_PLAY);
}

static void hints(void *ctx, u32 row, const struct ui_hint **keys, u32 *count)
{
	(void)ctx;
	(void)row;
	sp.hints[0] = (struct ui_hint){"A", S(STR_GSAVES_HINT_PLAY)};
	sp.hints[1] = (struct ui_hint){"B", S(STR_GSAVES_HINT_CANCEL)};
	*keys = sp.hints;
	*count = 2;
}

int slot_picker_run(const char *rom_path, const char *title, u32 blank_size)
{
	(void)blank_size;
	int own = !ui_active();
	if (own)
		dialog_open();
	int result = SLOT_PICKER_CANCEL;
	if (!pending_save_flush()) {  // the slots as the card has them
		result = SLOT_PICKER_ERROR;
		goto out;
	}
	memset(&sp, 0, sizeof(sp));
	int n = slotstore_list(rom_path, sp.slots, SLOTSTORE_MAX_SLOTS);
	if (n <= 0) {
		// No file yet (a first launch): nothing to choose, it starts
		result = n < 0 ? SLOT_PICKER_ERROR : SLOT_PICKER_LAUNCH;
		if (n < 0)
			slot_flow_error(n);
		goto out;
	}
	sp.count = (u32)n < SLOTSTORE_MAX_SLOTS ? (u32)n : SLOTSTORE_MAX_SLOTS;
	slotstore_current(rom_path, sp.current);
	u32 at = 0;
	for (u32 i = 0; i < sp.count; i++) {
		int act = !strcmp(sp.slots[i].name, sp.current);
		sp.rows[i] = (struct save_page_row){sp.slots[i].name, (u8)(act ? ART_ICON8_SAVE + 1 : 0), 0};
		if (act)
			at = i;
	}
	sp.page = (struct save_page){"slot-picker",
				     ART_ICON16_GBA,
				     S(STR_GSAVES_PLAY_TITLE),
				     title,
				     S(STR_GSAVES_PLAY_KEYS),
				     sp.rows,
				     sp.count,
				     at,
				     pane,
				     hints,
				     0,
				     0};
	if (!save_page_open(&sp.page)) {
		result = SLOT_PICKER_ERROR;
		goto out;
	}
	u32 row;
	u16 key = save_page_wait(&row);
	if (key == INPUT_A && row < sp.count) {
		int r = slotstore_set_current(rom_path, sp.slots[row].name);
		if (r != SLOTSTORE_OK) {
			slot_flow_error(r);
			result = SLOT_PICKER_ERROR;
		} else {
			tlog("slots: playing %s", sp.slots[row].name);
			result = SLOT_PICKER_LAUNCH;
		}
	}
	save_page_close();
out:
	if (own)
		dialog_close();
	return result;
}
