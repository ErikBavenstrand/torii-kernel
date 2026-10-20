// The settings model's values on the cart (settings_store.c): read from the
// kernel NOR's settings slots and the RTC, staged, and written once.
//
// A page loads the values as it opens, changes them in its copy, and as it
// closes stages them and commits once: the kernel NOR is written (through
// Save_info) only if a slot changed. The clock is the RTC's, set at once.
#ifndef TORII_SETTINGS_H
#define TORII_SETTINGS_H

#include <gba_base.h>

#include "settings_model.h"

// The kernel NOR's settings sector as it is written (Save_SET_info): the
// slots from assress_max are written as they are
extern u16 SET_info_buffer[0x200];

// Reads every slot (as staged, while staged) and the clock into v
void settings_load(struct settings_values *v);
// Stages v's slots: Read_SET_info reads them, nothing is written yet
void settings_stage(const struct settings_values *v);
// Writes what is staged to the kernel NOR, once and only if a slot changed,
// ends the staging and, if it wrote, applies the settings (the launches'
// switches, the LED, the strings, DIM, LOGGING). Returns 1 if it wrote.
u32 settings_commit(void);
void settings_read_clock(struct settings_clock *c);
void settings_write_clock(const struct settings_clock *c);
// Sets the RTC to what the field editor e edited, over the clock as it is
// now (settings_editor_apply)
void settings_write_edit(const struct settings_editor *e);
// Logs the v0.4 settings nothing shows yet, at boot
void settings_log(void);

#endif
