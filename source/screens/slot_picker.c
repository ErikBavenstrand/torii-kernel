// The launch's save slot picker (slot_picker.h).
#include <stdio.h>
#include <string.h>

#include "slot_picker.h"
#include "slot_flow.h"
#include "slot_list.h"
#include "../ezkernel.h"
#include "../lang.h"
#include "../tlog.h"
#include "../ui/art_ids.h"
#include "../ui/dialog.h"

static struct {
	char current[SAVESLOT_NAME_BYTES];
	char detail[SLOTSTORE_MAX_SLOTS][12];
	struct slot_list_row rows[SLOTSTORE_MAX_SLOTS + 1];
	struct ui_hint hints[3];
	u32 count;
} sp EWRAM_BSS;

int slot_picker_wanted(const char *rom_path, int force)
{
	if (force)
		return 1;
	return slot_flow_slots(rom_path, 0) >= 2;
}

static void size_text(char *out, u32 bytes)
{
	if (bytes < 1024)
		snprintf(out, 12, "%lu B", (unsigned long)bytes);
	else
		snprintf(out, 12, "%lu KB", (unsigned long)bytes / 1024);
}

// The rows: the slots, then New slot. Returns the row of name (or of the
// current slot), 0 if neither is there. A game with no file yet shows its
// current slot (Main) as a row of its own.
static u32 rows_build(const char *rom_path, const char *want)
{
	const struct slotstore_slot *slots;
	int n = slot_flow_slots(rom_path, &slots);
	if (n < 0)
		n = 0;
	slotstore_current(rom_path, sp.current);
	u32 count = 0, at = 0;
	if (n == 0) {
		sp.rows[0] = (struct slot_list_row){sp.current, gl_slots->fresh, ART_ICON8_SAVE + 1};
		count = 1;
	}
	for (int i = 0; i < n; i++, count++) {
		size_text(sp.detail[i], slots[i].size);
		int last = !strcmp(slots[i].name, sp.current);
		sp.rows[i] = (struct slot_list_row){slots[i].name, sp.detail[i], (u8)(last ? ART_ICON8_SAVE + 1 : 0)};
		if (want ? !strcmp(slots[i].name, want) : last)
			at = (u32)i;
	}
	sp.rows[count++] = (struct slot_list_row){gl_slots->new_row, 0, 0};
	sp.count = count;
	return at;
}

int slot_picker_run(const char *rom_path, const char *title, u32 blank_size)
{
	int own = !ui_active();
	if (own)
		dialog_open();
	if (!pending_save_flush()) {  // before New slot or Rename can move a slot
		if (own)
			dialog_close();
		return SLOT_PICKER_ERROR;
	}
	sp.hints[0] = (struct ui_hint){"A", gl_slots->hint_play};
	sp.hints[1] = (struct ui_hint){"START", gl_slots->hint_rename};
	sp.hints[2] = (struct ui_hint){"B", gl_slots->hint_cancel};
	char want[SAVESLOT_NAME_BYTES];
	want[0] = 0;
	int result = SLOT_PICKER_CANCEL;
	for (;;) {
		u32 cursor = rows_build(rom_path, want[0] ? want : NULL);
		const struct slot_list list = {
			"slot-picker", gl_slots->picker_title, title, sp.rows, sp.count, cursor, sp.hints, 3, 1};
		u32 row;
		int key = slot_list_run(&list, &row);
		int slots = (int)sp.count - 1, empty = 0;
		{
			const struct slotstore_slot *s;
			empty = slot_flow_slots(rom_path, &s) == 0;
		}
		if (key == SLOT_LIST_CANCEL)
			break;
		if (row == (u32)slots) {  // New slot...
			if (key == SLOT_LIST_PICK) {
				slot_flow_new(rom_path, blank_size, want);
			}
			if (key != SLOT_LIST_PICK || want[0] == 0)
				want[0] = 0;
			continue;
		}
		if (key == SLOT_LIST_EXTRA) {
			if (empty)
				continue;  // no file to rename yet
			char name[SAVESLOT_NAME_BYTES];
			snprintf(name, sizeof(name), "%s", sp.rows[row].text);
			want[0] = 0;
			if (slot_flow_rename(rom_path, name, want) != SLOT_FLOW_DONE)
				snprintf(want, sizeof(want), "%s", name);
			continue;
		}
		if (!empty) {
			char name[SAVESLOT_NAME_BYTES];
			snprintf(name, sizeof(name), "%s", sp.rows[row].text);
			int r = slotstore_set_current(rom_path, name);
			if (r != SLOTSTORE_OK) {
				slot_flow_error(r);
				snprintf(want, sizeof(want), "%s", name);
				continue;
			}
			tlog("slots: playing %s", name);
		}
		result = SLOT_PICKER_LAUNCH;
		break;
	}
	if (own)
		dialog_close();
	return result;
}
