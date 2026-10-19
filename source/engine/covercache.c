// See covercache.h.
#include "covercache.h"

void cc_init(struct cover_cache *c)
{
	cc_invalidate(c);
}

void cc_invalidate(struct cover_cache *c)
{
	for (u32 i = 0; i < COVER_ENTRIES; i++) {
		c->entry[i].id = 0;
		c->entry[i].dist = COVER_DIST_NONE;
		c->entry[i].state = COVER_FREE;
		c->entry[i].pins = 0;
	}
}

u8 cc_find(const struct cover_cache *c, u32 id)
{
	for (u32 i = 0; i < COVER_ENTRIES; i++)
		if (c->entry[i].state != COVER_FREE && c->entry[i].id == id)
			return (u8)i;
	return COVER_NONE;
}

void cc_unwant_all(struct cover_cache *c)
{
	for (u32 i = 0; i < COVER_ENTRIES; i++)
		c->entry[i].dist = COVER_DIST_NONE;
}

// What evicting an entry loses, for breaking ties: nothing loaded yet, a
// failure that would be retried, a decoded cover.
static u32 loss(u8 state)
{
	return state == COVER_WANTED ? 0 : state == COVER_FAILED ? 1 : 2;
}

u8 cc_want(struct cover_cache *c, u32 id, u16 dist)
{
	u8 i = cc_find(c, id);
	if (i != COVER_NONE) {
		if (dist < c->entry[i].dist)
			c->entry[i].dist = dist;
		return i;
	}
	u8 victim = COVER_NONE;
	for (u32 j = 0; j < COVER_ENTRIES; j++) {
		const struct cover_entry *e = &c->entry[j];
		if (e->state == COVER_FREE) {
			victim = (u8)j;
			break;
		}
		if (e->pins || e->state == COVER_LOADING || e->dist <= dist)
			continue;
		if (victim == COVER_NONE)
			victim = (u8)j;
		else {
			const struct cover_entry *v = &c->entry[victim];
			if (e->dist > v->dist || (e->dist == v->dist && loss(e->state) < loss(v->state)))
				victim = (u8)j;
		}
	}
	if (victim == COVER_NONE)
		return COVER_NONE;
	struct cover_entry *e = &c->entry[victim];
	e->id = id;
	e->dist = dist;
	e->state = COVER_WANTED;
	e->pins = 0;
	return victim;
}

u8 cc_next_load(const struct cover_cache *c)
{
	u8 best = COVER_NONE;
	for (u32 i = 0; i < COVER_ENTRIES; i++) {
		const struct cover_entry *e = &c->entry[i];
		if (e->state != COVER_WANTED || e->dist == COVER_DIST_NONE)
			continue;
		if (best == COVER_NONE || e->dist < c->entry[best].dist)
			best = (u8)i;
	}
	return best;
}

void cc_load_start(struct cover_cache *c, u8 index)
{
	if (index < COVER_ENTRIES && c->entry[index].state == COVER_WANTED)
		c->entry[index].state = COVER_LOADING;
}

void cc_load_abort(struct cover_cache *c, u8 index)
{
	if (index < COVER_ENTRIES && c->entry[index].state == COVER_LOADING)
		c->entry[index].state = COVER_WANTED;
}

void cc_load_finish(struct cover_cache *c, u8 index, int ok)
{
	if (index < COVER_ENTRIES && c->entry[index].state == COVER_LOADING)
		c->entry[index].state = ok ? COVER_READY : COVER_FAILED;
}

int cc_pin(struct cover_cache *c, u8 index)
{
	if (index >= COVER_ENTRIES || c->entry[index].state == COVER_FREE || c->entry[index].pins == 0xFF)
		return 0;
	c->entry[index].pins++;
	return 1;
}

void cc_unpin(struct cover_cache *c, u8 index)
{
	if (index < COVER_ENTRIES && c->entry[index].pins)
		c->entry[index].pins--;
}

u32 cc_count(const struct cover_cache *c, u8 state)
{
	u32 n = 0;
	for (u32 i = 0; i < COVER_ENTRIES; i++)
		n += c->entry[i].state == state;
	return n;
}
