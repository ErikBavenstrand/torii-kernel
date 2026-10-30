// The slot flows (slot_flow.h).
#include <stdio.h>
#include <string.h>

#include "slot_flow.h"
#include "rename.h"
#include "../ez_define.h"
#include "../lang.h"
#include "../tlog.h"
#include "../ui/art_ids.h"
#include "../ui/dialog.h"

// In pReadCache (READCACHE_FLOW) within a call: the keyboard's OK reads it
// while the prompt runs, nothing after
struct flow_state {
	struct slotstore_slot slots[SLOTSTORE_MAX_SLOTS];
	const char *rom;
	const char *own;
	char out[SAVESLOT_NAME_BYTES];
	char clean[SAVESLOT_NAME_BYTES];
};
_Static_assert(sizeof(struct flow_state) <= READCACHE_FLOW_SIZE, "the slot flows fit READCACHE_FLOW");
extern u8 pReadCache[];
#define fl (*(struct flow_state *)(pReadCache + READCACHE_FLOW))

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
		return S(STR_SLOTS_TAKEN);
	case SLOTSTORE_NAME:
		return S(STR_SLOTS_BAD);
	case SLOTSTORE_PATH:
		return S(STR_SLOTS_PATH);
	case SLOTSTORE_NO_SLOT:
		return S(STR_SLOTS_GONE);
	case SLOTSTORE_FULL:
		return S(STR_SLOTS_FULL);
	default:
		return S(STR_SLOTS_FAILED);
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
		return S(STR_SLOTS_BAD);
	int n = slot_flow_slots(fl.rom, 0);
	if (n < 0)
		return S(STR_SLOTS_FAILED);
	for (int i = 0; i < n; i++) {
		if (saveslot_names_equal(fl.slots[i].name, fl.clean) && !(fl.own && !strcmp(fl.slots[i].name, fl.own)))
			return S(STR_SLOTS_TAKEN);
	}
	memcpy(fl.out, fl.clean, sizeof(fl.out));
	return NULL;
}

int slot_flow_name_prompt(const char *rom, const char *title, const char *initial, const char *own, char *out)
{
	fl.rom = rom;
	fl.own = own;
	fl.out[0] = 0;
	// A new slot's name from a file is a suggestion: OK keeps it as it is
	const struct rename_ask ask = {ART_ICON16_TORII, initial, NULL,  SAVESLOT_NAME_CHARS,      1,
				       name_submit,      0,       title, (u8)(!own && initial[0]), 0};
	if (!rename_run(&ask) || !fl.out[0])
		return SLOT_FLOW_CANCEL;
	memcpy(out, fl.out, SAVESLOT_NAME_BYTES);
	return SLOT_FLOW_DONE;
}

int slot_flow_rename(const char *rom, const char *slot, char *renamed)
{
	char own[SAVESLOT_NAME_BYTES], name[SAVESLOT_NAME_BYTES];
	snprintf(own, sizeof(own), "%s", slot);
	if (slot_flow_name_prompt(rom, S(STR_SLOTS_RENAME_TITLE), own, own, name) != SLOT_FLOW_DONE)
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
