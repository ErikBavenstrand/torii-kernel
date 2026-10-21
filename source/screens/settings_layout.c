// The Settings page's layout and hotkey capture (settings_layout.h).
#include <string.h>

#include "settings_layout.h"

#define KEYS_ALL 0x3FF  // the ten keys, KEYINPUT's bits
#define KEY_B 0x002

void settings_layout_build(struct settings_layout *l, const struct settings_values *v)
{
	memset(l, 0, sizeof(*l));
	for (u32 sec = 0; sec < SET_SECTIONS; sec++) {
		u8 ids[SET_CARD_ROWS];
		u32 n = settings_section_rows(v, sec, ids);
		l->first[sec] = l->last[sec] = SET_LAYOUT_NONE;
		if (!n)
			continue;
		l->slot[l->count++] = (struct settings_slot){SET_SLOT_LABEL, (u8)sec, 0};
		l->first[sec] = l->count;
		for (u32 i = 0; i < n; i++)
			l->slot[l->count++] = (struct settings_slot){SET_SLOT_ROW, (u8)sec, ids[i]};
		l->last[sec] = (u8)(l->count - 1);
		l->rows = (u8)(l->rows + n);
		l->slot[l->count++] = (struct settings_slot){SET_SLOT_GAP, (u8)sec, 0};
	}
}

u32 settings_layout_slot_of(const struct settings_layout *l, u32 id)
{
	for (u32 s = 0; s < l->count; s++)
		if (l->slot[s].kind == SET_SLOT_ROW && l->slot[s].id == id)
			return s;
	return SET_LAYOUT_NONE;
}

u32 settings_layout_row_at(const struct settings_layout *l, u32 s)
{
	u32 n = l->count < SET_LAYOUT_SLOTS ? l->count : SET_LAYOUT_SLOTS;
	for (u32 i = s; i < n; i++)
		if (l->slot[i].kind == SET_SLOT_ROW)
			return i;
	for (u32 i = s < n ? s : n; i-- > 0;)
		if (l->slot[i].kind == SET_SLOT_ROW)
			return i;
	return 0;
}

u32 settings_layout_position(const struct settings_layout *l, u32 s)
{
	u32 n = 0;
	for (u32 i = 0; i <= s && i < l->count; i++)
		n += l->slot[i].kind == SET_SLOT_ROW;
	return n;
}

u32 settings_layout_step(const struct settings_layout *l, u32 s, int dir, int wrap)
{
	if (!l->rows)
		return s;
	s32 n = l->count < SET_LAYOUT_SLOTS ? l->count : SET_LAYOUT_SLOTS, step = dir > 0 ? 1 : -1;
	for (s32 i = (s32)s + step; i >= 0 && i < n; i += step)
		if (l->slot[i].kind == SET_SLOT_ROW)
			return (u32)i;
	if (!wrap)
		return s;
	return dir > 0 ? settings_layout_row_at(l, 0) : settings_layout_row_at(l, l->count - 1);
}

u32 settings_layout_section(const struct settings_layout *l, u32 s, int dir)
{
	if (s >= l->count || !l->rows)
		return s;
	u32 sec = l->slot[s].section;
	if (dir < 0 && l->first[sec] != SET_LAYOUT_NONE && s > l->first[sec])
		return l->first[sec];
	for (u32 i = 1; i <= SET_SECTIONS; i++) {
		u32 to = dir > 0 ? (sec + i) % SET_SECTIONS : (sec + SET_SECTIONS - i) % SET_SECTIONS;
		if (l->first[to] != SET_LAYOUT_NONE)
			return l->first[to];
	}
	return s;
}

// --- Hotkey capture ---------------------------------------------------------

static u32 bits(u32 mask)
{
	u32 n = 0;
	for (; mask; mask &= mask - 1)
		n++;
	return n;
}

void settings_capture_start(struct settings_capture *c, u16 held, u32 hold, u32 timeout)
{
	memset(c, 0, sizeof(*c));
	c->ignore = held & KEYS_ALL;
	c->hold = (u16)hold;
	c->timeout = (u16)timeout;
}

u32 settings_capture_step(struct settings_capture *c, u16 held, u32 dt)
{
	c->ignore &= held;  // a key held at the start counts once let go
	u16 k = held & ~c->ignore & KEYS_ALL;
	c->seen |= k;
	if (k != c->run) {
		c->run = k;
		c->run_frames = (u16)dt;  // held this frame
		c->idle = 0;
	} else {
		c->run_frames = (u16)(c->run_frames + dt > 0xFFFF ? 0xFFFF : c->run_frames + dt);
		if (!k)
			c->idle = (u16)(c->idle + dt > 0xFFFF ? 0xFFFF : c->idle + dt);
	}
	if (k && c->run_frames >= c->hold && bits(k) >= bits(c->best))
		c->best = k;
	c->held = k;
	if (k)
		return SET_CAPTURE_WAIT;
	if (c->seen) {
		// Every key up: the set held longest, unless that was B alone
		if (c->best)
			return c->best == KEY_B ? SET_CAPTURE_CANCEL : SET_CAPTURE_DONE;
		if (c->seen == KEY_B)
			return SET_CAPTURE_CANCEL;
		c->seen = 0;  // a tap too short to count: it waits on
	}
	return c->idle >= c->timeout ? SET_CAPTURE_CANCEL : SET_CAPTURE_WAIT;
}

u32 settings_capture_left(const struct settings_capture *c)
{
	if (c->held)
		return c->timeout;
	return c->idle < c->timeout ? (u32)(c->timeout - c->idle) : 0;
}
