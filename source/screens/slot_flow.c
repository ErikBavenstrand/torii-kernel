// The slot flows (slot_flow.h).
#include <stdio.h>
#include <string.h>

#include "slot_flow.h"
#include "rename.h"
#include "slot_list.h"
#include "../lang.h"
#include "../tlog.h"
#include "../ui/art_ids.h"
#include "../ui/dialog.h"

static struct {
	struct slotstore_slot slots[SLOTSTORE_MAX_SLOTS];
	const char *rom;
	const char *own;
	char out[SAVESLOT_NAME_BYTES];
	char clean[SAVESLOT_NAME_BYTES];
	char current[SAVESLOT_NAME_BYTES];
	char copy[SAVESLOT_NAME_BYTES + 16];
	struct slot_list_row rows[2];
	struct ui_hint hints[2];
} fl EWRAM_BSS;

int slot_flow_slots(const char *rom, const struct slotstore_slot **out)
{
	int n = slotstore_list(rom, fl.slots, SLOTSTORE_MAX_SLOTS);
	if (n > SLOTSTORE_MAX_SLOTS)
		n = SLOTSTORE_MAX_SLOTS;
	if (out)
		*out = fl.slots;
	return n;
}

const char *slot_flow_error_text(int code)
{
	switch (code) {
	case SLOTSTORE_TAKEN:
		return gl_slots->taken;
	case SLOTSTORE_NAME:
		return gl_slots->bad;
	case SLOTSTORE_PATH:
		return gl_slots->path;
	case SLOTSTORE_NO_SLOT:
		return gl_slots->gone;
	case SLOTSTORE_FULL:
		return gl_slots->full;
	default:
		return gl_slots->failed;
	}
}

void slot_flow_error(int code)
{
	tlog_error("slots: %d", code);
	dialog_message(NULL, slot_flow_error_text(code), DIALOG_ERROR);
}

// OK on the keyboard: the cleaned name must be usable and the game's own
static const char *name_submit(void *ctx, const char *name)
{
	(void)ctx;
	if (!saveslot_clean_name(fl.clean, name))
		return gl_slots->bad;
	int n = slot_flow_slots(fl.rom, 0);
	if (n < 0)
		return gl_slots->failed;
	for (int i = 0; i < n; i++) {
		if (saveslot_names_equal(fl.slots[i].name, fl.clean) &&
		    !(fl.own && !strcmp(fl.slots[i].name, fl.own)))
			return gl_slots->taken;
	}
	memcpy(fl.out, fl.clean, sizeof(fl.out));
	return NULL;
}

int slot_flow_name_prompt(const char *rom, const char *title, const char *initial, const char *own, char *out)
{
	fl.rom = rom;
	fl.own = own;
	fl.out[0] = 0;
	const struct rename_ask ask = {ART_ICON16_TORII, initial, NULL, SAVESLOT_NAME_CHARS, 1, name_submit, 0, title};
	if (!rename_run(&ask) || !fl.out[0])
		return SLOT_FLOW_CANCEL;
	memcpy(out, fl.out, SAVESLOT_NAME_BYTES);
	return SLOT_FLOW_DONE;
}

int slot_flow_new(const char *rom, u32 blank_size, char *created)
{
	const struct slotstore_slot *slots;
	int n = slot_flow_slots(rom, &slots);
	if (n < 0) {
		slot_flow_error(n);
		return SLOT_FLOW_FAILED;
	}
	if (n >= SLOTSTORE_MAX_SLOTS) {
		slot_flow_error(SLOTSTORE_FULL);
		return SLOT_FLOW_FAILED;
	}
	char name[SAVESLOT_NAME_BYTES];
	if (slot_flow_name_prompt(rom, gl_slots->new_title, "", NULL, name) != SLOT_FLOW_DONE)
		return SLOT_FLOW_CANCEL;
	// A copy needs a slot with a file: the current one, else the first
	const char *copy_from = NULL;
	if (n > 0) {
		slotstore_current(rom, fl.current);
		copy_from = slots[0].name;
		for (int i = 0; i < n; i++)
			if (!strcmp(slots[i].name, fl.current))
				copy_from = slots[i].name;
		snprintf(fl.copy, sizeof(fl.copy), EN_SLOT_COPY_OF, copy_from);
		fl.rows[0] = (struct slot_list_row){gl_slots->blank, 0, 0};
		fl.rows[1] = (struct slot_list_row){fl.copy, 0, 0};
		fl.hints[0] = (struct ui_hint){"A", gl_hint_ok};
		fl.hints[1] = (struct ui_hint){"B", gl_slots->hint_cancel};
		const struct slot_list list = {"slot-new-choice", gl_slots->choice_title, name, fl.rows, 2, 0, fl.hints, 2, 0};
		u32 row;
		if (slot_list_run(&list, &row) != SLOT_LIST_PICK)
			return SLOT_FLOW_CANCEL;
		if (row == 0)
			copy_from = NULL;
		else
			snprintf(fl.current, sizeof(fl.current), "%s", copy_from);
		if (row != 0)
			copy_from = fl.current;
	}
	char clean[SAVESLOT_NAME_BYTES];
	int r = slotstore_create(rom, name, copy_from, blank_size, clean);
	tlog("slots: new %s%s%s: %d", clean, copy_from ? " copy of " : "", copy_from ? copy_from : "", r);
	if (r != SLOTSTORE_OK) {
		slot_flow_error(r);
		return SLOT_FLOW_FAILED;
	}
	if (created)
		memcpy(created, clean, SAVESLOT_NAME_BYTES);
	return SLOT_FLOW_DONE;
}

int slot_flow_rename(const char *rom, const char *slot, char *renamed)
{
	char own[SAVESLOT_NAME_BYTES], name[SAVESLOT_NAME_BYTES];
	snprintf(own, sizeof(own), "%s", slot);
	if (slot_flow_name_prompt(rom, gl_slots->rename_title, own, own, name) != SLOT_FLOW_DONE)
		return SLOT_FLOW_CANCEL;
	char clean[SAVESLOT_NAME_BYTES];
	int r = slotstore_rename(rom, own, name, clean);
	tlog("slots: rename %s to %s: %d", own, clean, r);
	if (r != SLOTSTORE_OK) {
		slot_flow_error(r);
		return SLOT_FLOW_FAILED;
	}
	if (renamed)
		memcpy(renamed, clean, SAVESLOT_NAME_BYTES);
	return SLOT_FLOW_DONE;
}
