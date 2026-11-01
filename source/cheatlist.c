// The cheat screen's list of a cheat file (cheatlist.h).
#include "cheatlist.h"

#include <string.h>

#include "cheatstore.h"
#include "crc32.h"
#include "cutpoint.h"
#include "Ezcard_OP.h"
#include "ff.h"
#include "tlog.h"
#include "engine/psram.h"

// Page 3's map: the entries, the names, the screen's rows, the checkpoints
#define LIST_BASE ((u8 *)PSRAM_WINDOW)
#define ENTRIES ((struct cheatlist_entry *)LIST_BASE)
#define NAMES (LIST_BASE + CHEATLIST_MAX * sizeof(struct cheatlist_entry))
#define ROWS ((u32 *)(NAMES + CHEATLIST_NAMES_BYTES))
#define ROWS_MAX (CHEATLIST_MAX + CHEATLIST_HEADINGS + 1)
#define AUX (ROWS + ROWS_MAX)
#define CKPTS ((struct cheatlist_ckpt *)(AUX + ROWS_MAX))
#define CKPTS_MAX CHEATLIST_CKPTS_OF(CHEATLIST_MAX)
_Static_assert((u32)(CHEATLIST_MAX * sizeof(struct cheatlist_entry)) + CHEATLIST_NAMES_BYTES + 2 * ROWS_MAX * 4u +
			       CKPTS_MAX * sizeof(struct cheatlist_ckpt) <=
		       PSRAM_WINDOW_BYTES - 0x10000u,
	       "the list fits page 3 below Diagnostics' PSRAM test");
_Static_assert(CHEATSTORE_WINDOWS *CHEATINDEX_CHEATS <= CHEATLIST_MAX, "every window's cheats have an entry");
_Static_assert(CHEATFILE_MAX <= CHEATLIST_MAX, "a small file's cheats have an entry");
_Static_assert(CHEATLIST_HEADING_BYTES % 4 == 0, "a heading's bytes keep the cache word-aligned");

static struct cheatlist *list(void)
{
	return &cheatstore()->list;
}

// --- Page 3 ---------------------------------------------------------------------------
//
// Selected only within these, between FatFs calls, and page 0 again before
// they return: nothing reads PSRAM from an interrupt.

static void page_get(const void *src, void *dst, u32 n)
{
	SetPSRampage(CHEATLIST_PAGE);
	memcpy(dst, src, n);
	SetPSRampage(PSRAM_ASSET_PAGE);
}

// n bytes (rounded up to halfwords) from RAM to page 3 at dst (halfword-aligned)
static u8 put_failed;  // a page_put failed since cache_read started

static void page_put(void *dst, const void *src, u32 n)
{
	SetPSRampage(CHEATLIST_PAGE);
	int ok = psram_write(dst, src, (n + 1) & ~1u);
	SetPSRampage(PSRAM_ASSET_PAGE);
	if (!ok)
		put_failed = 1;
	if (!ok && (list()->valid || list()->building)) {
		tlog_error("cheats: the list's PSRAM write at %08lX failed: built again next time",
			   (unsigned long)(u32)dst);
		cheatlist_drop();
	}
}

void cheatlist_get(u32 g, struct cheatlist_entry *e)
{
	page_get(&ENTRIES[g], e, sizeof(*e));
}

void cheatlist_read(u32 g, u32 n, struct cheatlist_entry *out)
{
	page_get(&ENTRIES[g], out, n * sizeof(*out));
}

void cheatlist_put(u32 g, const struct cheatlist_entry *e)
{
	page_put(&ENTRIES[g], e, sizeof(*e));
}

// --- Checkpoints -------------------------------------------------------------------------

// a and b hold the same decoder (member by member: the struct has padding)
static int dec_same(const struct cheat_decoder *a, const struct cheat_decoder *b)
{
	return a->forced == b->forced && a->gs_version == b->gs_version && a->last == b->last &&
	       a->last_alone == b->last_alone && !memcmp(a->cb_table, b->cb_table, sizeof(a->cb_table)) &&
	       a->cb_key2 == b->cb_key2 && !memcmp(a->gs_seeds, b->gs_seeds, sizeof(a->gs_seeds)) &&
	       a->cb_master == b->cb_master && !memcmp(a->cb_seeds, b->cb_seeds, sizeof(a->cb_seeds)) &&
	       a->cb_rng == b->cb_rng;
}

void cheatlist_ckpt_note(u32 g, const struct cheat_decoder *d)
{
	struct cheatlist *l = list();
	if (g % CHEATLIST_CKPT_EVERY || g / CHEATLIST_CKPT_EVERY >= CKPTS_MAX ||
	    !(l->building == 1 || (l->valid && l->on)))
		return;
	struct cheatlist_ckpt c, was;
	memset(&c, 0, sizeof(c));  // its padding written as zeros, as the cache's CRC sees it
	c.at = g + 1;
	memcpy(&c.dec, d, sizeof(c.dec));
	page_get(&CKPTS[g / CHEATLIST_CKPT_EVERY], &was, sizeof(was));
	if (was.at != c.at || !dec_same(&was.dec, &c.dec)) {
		page_put(&CKPTS[g / CHEATLIST_CKPT_EVERY], &c, sizeof(c));
		l->dirty |= l->valid;
	}
}

// The checkpoints of entries g to end are not known
static void ckpts_forget(u32 g, u32 end)
{
	u32 zero = 0;
	for (u32 k = CHEATLIST_CKPTS_OF(g); k < CKPTS_MAX && k * CHEATLIST_CKPT_EVERY <= end; k++) {
		u32 at;
		page_get(&CKPTS[k].at, &at, 4);
		if (at)
			page_put(&CKPTS[k].at, &zero, 4);
	}
}

void cheatlist_row_put(u32 r, u32 value)
{
	if (r < ROWS_MAX)
		page_put(&ROWS[r], &value, 4);
}

void cheatlist_row_aux_put(u32 r, u32 value)
{
	if (r < ROWS_MAX)
		page_put(&AUX[r], &value, 4);
}

u32 cheatlist_row_aux(u32 r)
{
	u32 v = 0;
	if (r < ROWS_MAX)
		page_get(&AUX[r], &v, 4);
	return v;
}

u32 cheatlist_row(u32 r)
{
	u32 v = 0;
	if (r < ROWS_MAX)
		page_get(&ROWS[r], &v, 4);
	return v;
}

u32 cheatlist_name(const struct cheatlist_entry *e, char *out, u32 size)
{
	u32 n = e->name_len < size ? e->name_len : size - 1;
	if (e->name_at > CHEATLIST_NAMES_BYTES - CHEATLIST_NAME_MAX - 1)
		n = 0;  // past the names' room (a cache that passed its CRC all the same): no name
	if (n)
		page_get(NAMES + e->name_at, out, n);
	out[n] = 0;
	return n;
}

const char *cheatlist_heading(u32 h, u32 *len)
{
	struct cheatlist *l = list();
	if (!h || h > l->n_headings) {
		*len = 0;
		return NULL;
	}
	*len = l->headings[h - 1].len;
	return l->headings[h - 1].name;
}

int cheatlist_variant(const struct cheatlist_entry *before, const struct cheatlist_entry *e)
{
	return e->shape && e->shape == before->shape && e->heading == before->heading;
}

u32 cheatlist_run(u32 g, u32 *end)
{
	struct cheatlist *l = list();
	struct cheatlist_entry buf[16];
	const u32 per = sizeof(buf) / sizeof(*buf);
	u32 start = g, k = g + 1;
	// Back to its first, a few entries a read
	for (int found = 0; !found;) {
		u32 from = start + 1 > per ? start + 1 - per : 0, n = start + 1 - from;
		cheatlist_read(from, n, buf);
		for (u32 i = n; i-- > 0;) {
			if (!(buf[i].flags & CHEATLIST_VARIANT) || !(from + i)) {
				start = from + i;
				found = 1;
				break;
			}
		}
		if (!found)
			start = from - 1;
	}
	// On past its last
	while (k < l->count) {
		u32 n = l->count - k < per ? l->count - k : per, i = 0;
		cheatlist_read(k, n, buf);
		while (i < n && (buf[i].flags & CHEATLIST_VARIANT))
			i++;
		k += i;
		if (i < n)
			break;
	}
	*end = k;
	return start;
}

u32 cheatlist_run_pick(u32 start, u32 end)
{
	struct cheatlist_entry buf[16];
	const u32 per = sizeof(buf) / sizeof(*buf);
	for (u32 g = start; g < end; g += per) {
		u32 n = end - g < per ? end - g : per;
		cheatlist_read(g, n, buf);
		for (u32 i = 0; i < n; i++)
			if (buf[i].flags & CHEATLIST_PICKED)
				return g + i;
	}
	return CHEATFILE_NONE;
}

// --- Building it from the model ------------------------------------------------------

// The id + 1 of the heading name (len bytes), added if new; 0 for none or
// past CHEATLIST_HEADINGS
static u8 heading_id(const char *name, u32 len)
{
	struct cheatlist *l = list();
	if (!name || !len || len > CHEATFILE_FOLDER_MAX)
		return 0;
	for (u32 h = 0; h < l->n_headings; h++)
		if (l->headings[h].len == len && !memcmp(l->headings[h].name, name, len))
			return (u8)(h + 1);
	if (l->n_headings == CHEATLIST_HEADINGS)
		return 0;
	struct cheatlist_heading *at = &l->headings[l->n_headings++];
	at->len = (u8)len;
	memcpy(at->name, name, len);
	return l->n_headings;
}

// A name (len bytes in RAM or page 0) into the names: its offset, *kept
// the bytes kept (cut on a character)
static u32 name_add(const char *name, u32 len, u8 *kept)
{
	struct cheatlist *l = list();
	char buf[CHEATLIST_NAME_MAX + 1];
	if (len > CHEATLIST_NAME_MAX) {
		len = CHEATLIST_NAME_MAX;
		while (len && ((u8)name[len] & 0xC0) == 0x80)
			len--;  // not inside a character
	}
	if (l->names_bytes + len + 1 > CHEATLIST_NAMES_BYTES) {
		// Full (each edit adds its window's names again, until the cache is
		// read): this visit's list stands, never kept, built again next time
		len = 0;
		if (!l->names_full)
			tlog("cheats: the list's names are full: built again next time");
		l->names_full = 1;
	}
	*kept = (u8)len;
	if (!len)
		return 0;
	memcpy(buf, name, len);
	u32 at = l->names_bytes;
	page_put(NAMES + at, buf, len);
	l->names_bytes = (at + len + 1) & ~1u;  // halfword-aligned, as PSRAM writes are
	return at;
}

// Entries from to count move to to (the list's tail, as a window's count changed)
static void entries_move(u32 from, u32 to)
{
	struct cheatlist *l = list();
	struct cheatstore *cs = cheatstore();
	struct cheatlist_entry *buf = (struct cheatlist_entry *)cs->part;
	const u32 per = sizeof(cs->part) / sizeof(*buf);
	u32 n = l->count - from;
	if (to < from) {
		for (u32 done = 0; done < n; done += per) {
			u32 c = n - done < per ? n - done : per;
			cheatlist_read(from + done, c, buf);
			page_put(&ENTRIES[to + done], buf, c * sizeof(*buf));
		}
	} else if (to > from) {
		while (n) {
			u32 c = n < per ? n : per;
			n -= c;
			cheatlist_read(from + n, c, buf);
			page_put(&ENTRIES[to + n], buf, c * sizeof(*buf));
		}
	}
	l->count = l->count - from + to;
}

// Entry g's variant flag from the one before it (none for the first)
static void variant_fix(u32 g)
{
	struct cheatlist *l = list();
	struct cheatlist_entry two[2];
	if (g >= l->count)
		return;
	if (!g) {
		cheatlist_get(0, &two[1]);
	} else {
		cheatlist_read(g - 1, 2, two);
	}
	u8 was = two[1].flags;
	two[1].flags = (u8)(two[1].flags & ~CHEATLIST_VARIANT);
	if (g && cheatlist_variant(&two[0], &two[1]))
		two[1].flags |= CHEATLIST_VARIANT;
	if (two[1].flags != was) {
		two[1].flags &= (u8)~CHEATLIST_NAMED;
		cheatlist_put(g, &two[1]);
	}
}

void cheatlist_from_model(u32 start, u32 old)
{
	struct cheatstore *cs = cheatstore();
	struct cheatlist *l = list();
	const struct cheatfile *f = &cs->file;
	if (start + old > l->count)
		old = l->count > start ? l->count - start : 0;
	if (l->count - old + f->count > CHEATLIST_MAX) {
		tlog_error("cheats: %s: over %u cheats: no list", cs->path, CHEATLIST_MAX);
		cheatlist_drop();
		return;
	}
	entries_move(start + old, start + f->count);
	if (l->building != 1)
		ckpts_forget(start + f->count, l->count);  // the decoder past an edit may be other now
	for (u32 i = 0; i < f->count; i++) {
		const struct cheatfile_cheat *c = &cs->cheats[i];
		struct cheatlist_entry e;
		memset(&e, 0, sizeof(e));
		u32 flen;
		const char *folder = cheatfile_folder_name(f, c, &flen);
		e.heading = heading_id(folder, flen);
		if (c->desc != CHEATFILE_NONE && c->desc_len)
			e.name_at = name_add(f->text + c->desc, c->desc_len, &e.name_len);
		e.hash = c->hash;
		// The shapes a window's edges need (a run of variants across two)
		e.shape = !i ? cs->work.first_shape : i + 1 == f->count ? cs->work.last_shape : 0;
		e.number = c->number;
		e.cost = c->cost;
		e.bytes = (u16)c->bytes;
		e.patches = (u16)c->patches;
		e.keys = c->keys;
		e.act = c->act;
		e.status = c->status;
		e.flags = (u8)((c->picked ? CHEATLIST_PICKED : 0) | (c->own ? CHEATLIST_OWN : 0));
		e.detected = c->detected;
		e.format = c->format;
		cheatlist_put(start + i, &e);
	}
	// Within the window its own decode says; its edges, the windows beside
	for (u32 i = 1; i < f->count; i++)
		if (cs->cheats[i].variant) {
			struct cheatlist_entry e;
			cheatlist_get(start + i, &e);
			e.flags |= CHEATLIST_VARIANT;
			cheatlist_put(start + i, &e);
		}
	variant_fix(start);
	variant_fix(start + f->count);
	// A run reaching in from the window before names itself anew
	if (start && start < l->count) {
		u32 end, first = cheatlist_run(start, &end);
		struct cheatlist_entry e;
		cheatlist_get(first, &e);
		if (e.flags & CHEATLIST_NAMED) {
			e.flags &= (u8)~CHEATLIST_NAMED;
			cheatlist_put(first, &e);
		}
	}
	l->dirty = 1;
}

int cheatlist_apply(u32 start)
{
	struct cheatstore *cs = cheatstore();
	struct cheatlist *l = list();
	struct cheatfile *f = &cs->file;
	if (!l->valid || !l->on)
		return 0;
	if (start + f->count > l->count) {
		tlog_error("cheats: %s: the list has %lu cheats, the file more: built again", cs->path,
			   (unsigned long)l->count);
		cheatlist_drop();
		return 0;
	}
	struct cheatlist_entry *buf = (struct cheatlist_entry *)cs->part;
	const u32 per = sizeof(cs->part) / sizeof(*buf);
	f->bytes = f->cost = f->patches = 0;
	for (u32 i = 0; i < f->count; i++) {
		if (!(i % per))
			cheatlist_read(start + i, f->count - i < per ? f->count - i : per, buf);
		const struct cheatlist_entry *e = &buf[i % per];
		struct cheatfile_cheat *c = &cs->cheats[i];
		if (e->number != c->number || e->format != c->format || e->keys != c->keys) {
			tlog_error("cheats: %s: cheat %u isn't as its list says: built again", cs->path, c->number);
			cheatlist_drop();
			cheatstore_begin();
			return 0;
		}
		c->status = e->status;
		c->bytes = e->bytes;
		c->cost = e->cost;
		c->patches = e->patches;
		c->hash = e->hash;
		c->act = e->act;
		c->detected = e->detected;
		c->variant = i && (e->flags & CHEATLIST_VARIANT);
		if (!i)
			cs->work.first_shape = e->shape;
		if (i + 1 == f->count)
			cs->work.last_shape = e->shape;
		if (c->status == CHEATFILE_OK && c->picked) {
			f->bytes += c->bytes;
			f->cost += c->cost;
			f->patches += c->patches;
		}
	}
	cs->work.next = f->count;
	cs->listed_decode = 1;  // the decoder wasn't run: cheatstore_decoder_before runs it
	return 1;
}

void cheatlist_sync_picks(void)
{
	struct cheatlist *l = list();
	struct cheatstore *cs = cheatstore();
	struct cheatlist_entry *buf = (struct cheatlist_entry *)cs->part;
	const u32 per = sizeof(cs->part) / sizeof(*buf);
	u32 start = cheatstore_window_start(cheatstore_window()), end = start + cs->file.count;
	if (!l->valid)
		return;
	for (u32 g = 0; g < l->count; g += per) {
		u32 n = l->count - g < per ? l->count - g : per, changed = 0;
		cheatlist_read(g, n, buf);
		for (u32 i = 0; i < n; i++) {
			u32 at = g + i;
			// The window read now's from the model, the others' from the pick list
			int on = at >= start && at < end ? cs->cheats[at - start].picked
				 : cs->windows           ? cheatstore_number_picked(buf[i].number)
							 : 0;
			u8 want = (u8)((buf[i].flags & ~CHEATLIST_PICKED) | (on ? CHEATLIST_PICKED : 0));
			changed |= want != buf[i].flags;
			buf[i].flags = want;
		}
		if (changed)
			page_put(&ENTRIES[g], buf, n * sizeof(*buf));
	}
}

void cheatlist_drop(void)
{
	struct cheatlist *l = list();
	l->valid = l->dirty = 0;
	if (l->building)
		l->building = 2;  // failed
	cheatstore()->listed_decode = 0;
}

void cheatlist_build_begin(void)
{
	struct cheatlist *l = list();
	ckpts_forget(0, CHEATLIST_MAX);  // each noted as the build decodes
	l->count = l->names_bytes = l->n_headings = l->names_full = 0;
	l->valid = l->dirty = 0;
	l->building = 1;
}

int cheatlist_build_end(void)
{
	struct cheatlist *l = list();
	l->valid = l->building == 1;
	l->dirty = l->valid;
	l->building = 0;
	return l->valid;
}

u32 cheatlist_why(const struct cheatlist_entry *e)
{
	const struct cheatfile *f = &cheatstore()->file;
	if (e->flags & CHEATLIST_PICKED)
		return CHEATFILE_PICKABLE;
	switch (e->status) {
	case CHEATFILE_OK:
		if (f->bytes + f->more_bytes + e->bytes > CHEATFILE_BYTES_MAX ||
		    f->cost + f->more_cost + e->cost > CHEATFILE_CYCLES_MAX)
			return CHEATFILE_WHY_FULL;
		if (f->patches + f->more_patches + e->patches > CHEATFILE_PICKED_PATCHES_MAX)
			return CHEATFILE_WHY_PATCHES;
		return CHEATFILE_PICKABLE;
	case CHEATFILE_NEW:
		return CHEATFILE_PICKABLE;
	case CHEATFILE_BIG:
		return CHEATFILE_WHY_FULL;
	case CHEATFILE_PLACEHOLDER:
		return CHEATFILE_WHY_EDIT;
	case CHEATFILE_CANT_RUN:
		return CHEATFILE_WHY_CANT_RUN;
	case CHEATFILE_NOTHING:
		return CHEATFILE_WHY_NOTHING;
	case CHEATFILE_SAME:
		return CHEATFILE_WHY_SAME;
	case CHEATFILE_MASTER:
		return CHEATFILE_WHY_MASTER;
	default:
		return CHEATFILE_WHY_UNREADABLE;
	}
}

u32 cheatlist_key_line(u32 crc, const char *line, u32 len, u32 *bytes)
{
	u32 i = 0;
	while (i < len && (line[i] == ' ' || line[i] == '\t'))
		i++;
	if (i + 5 < len && !memcmp(line + i, "cheat", 5)) {
		u32 k = i + 5;
		while (k < len && line[k] >= '0' && line[k] <= '9')
			k++;
		if (k > i + 5 && k + 7 <= len && !memcmp(line + k, "_enable", 7))
			return crc;  // a pick: no part of the key
	}
	*bytes += len + 1;
	return crc32_update(crc, line, len);
}

// --- The cache on the card -----------------------------------------------------------

// The cache's path: the cheat file's, ".cdc" for ".cht"
static void cache_path(char *out)
{
	const char *path = cheatstore()->path;
	u32 n = (u32)strlen(path);
	memcpy(out, path, n + 1);
	if (n > 4)
		memcpy(out + n - 4, CHEATLIST_EXT, sizeof(CHEATLIST_EXT));
}

// The cache's bytes after its header, made again from the start: the
// checkpoints, the headings, the entries (their names' offsets as written: one after
// another, in entry order) and the names. A stream given in parts to *out.
struct cache_out {
	u32 part;  // 0 checkpoints, 1 headings, 2 entries, 3 names, 4 done
	u32 i;     // the item within the part
	u32 names_at;  // the next name's offset as written
	u32 total;     // bytes given so far
};

// Up to max bytes of the stream into buf (word-aligned); how many, 0 at its end
static u32 cache_fill(struct cache_out *o, u8 *buf, u32 max)
{
	struct cheatlist *l = list();
	u32 n = 0;
	while (o->part < 4) {
		if (o->part == 0) {
			if (o->i == CHEATLIST_CKPTS_OF(l->count)) {
				o->part++;
				o->i = 0;
				if (!n)
					continue;
				o->total += n;
				return n;  // the checkpoints end a part of their own (their CRC's)
			}
			if (n + sizeof(struct cheatlist_ckpt) > max) {
				o->total += n;
				return n;
			}
			page_get(&CKPTS[o->i], buf + n, sizeof(struct cheatlist_ckpt));
			n += sizeof(struct cheatlist_ckpt);
			o->i++;
		} else if (o->part == 1) {
			if (o->i == l->n_headings) {
				o->part++;
				o->i = 0;
				continue;
			}
			if (n + CHEATLIST_HEADING_BYTES > max) {
				o->total += n;
				return n;
			}
			u8 *h = buf + n;
			memset(h, 0, CHEATLIST_HEADING_BYTES);
			h[0] = l->headings[o->i].len;
			memcpy(h + 1, l->headings[o->i].name, l->headings[o->i].len);
			n += CHEATLIST_HEADING_BYTES;
			o->i++;
		} else if (o->part == 2) {
			if (o->i == l->count) {
				o->part++;
				o->i = 0;
				continue;
			}
			if (n + sizeof(struct cheatlist_entry) > max) {
				o->total += n;
				return n;
			}
			struct cheatlist_entry *e = (struct cheatlist_entry *)(buf + n);
			cheatlist_get(o->i, e);
			e->flags &= (u8) ~(CHEATLIST_PICKED |
					   CHEATLIST_NAMED);  // the picks are the store's; names found again
			e->name_at = e->name_len ? o->names_at : 0;
			o->names_at += (e->name_len + 1u) & ~1u;
			n += sizeof(*e);
			o->i++;
		} else {
			if (o->i == l->count) {
				o->part++;
				continue;
			}
			struct cheatlist_entry e;
			cheatlist_get(o->i, &e);
			u32 len = (e.name_len + 1u) & ~1u;
			if (n + len + 4 > max) {  // room for the end's padding
				o->total += n;
				return n;
			}
			if (e.name_len && e.name_at <= CHEATLIST_NAMES_BYTES - CHEATLIST_NAME_MAX - 1) {
				page_get(NAMES + e.name_at, buf + n, len);
				n += len;
			}
			o->i++;
		}
	}
	u32 pad = (o->total + n) & 3;
	if (pad && o->part == 4) {
		memset(buf + n, 0, 4 - pad);  // the file ends word-aligned
		n += 4 - pad;
	}
	o->total += n;
	return n;
}

// Writes the cache: 1 if it is on the card
static int cache_write(void)
{
	struct cheatstore *cs = cheatstore();
	struct cheatlist *l = list();
	char path[SAFEFILE_PATH_MAX];
	u8 *buf = (u8 *)cs->part;
	struct cache_out o;
	u32 n, names = 0;
	cache_path(path);
	if (l->names_full || !l->count) {
		// Never a list missing names; nor one of no cheats (a game whose
		// screen opened with no cheat file keeps none beside it)
		l->dirty = 0;
		return 0;
	}
	// The CRC first: the stream made twice, as safe files are
	struct cheatlist_head h = {CHEATLIST_MAGIC, l->key_size, l->key_crc, l->count, l->n_headings, 0, 0, 0};
	memset(&o, 0, sizeof(o));
	for (u32 was = 0; (n = cache_fill(&o, buf, sizeof(cs->part))) > 0; was = o.part) {
		h.crc = crc32_update(h.crc, buf, n);
		if (!was)
			h.ckpt_crc = crc32_update(h.ckpt_crc, buf, n);  // a part from the checkpoints is only theirs
	}
	names = o.names_at;
	h.names_bytes = names;
	FIL *fil = &cs->fil;
	UINT put;
	int ok = f_open(fil, path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK;
	if (ok) {
		cutpoint("cheats: the list's cache opened");
		memcpy(buf, &h, sizeof(h));
		ok = f_write(fil, buf, sizeof(h), &put) == FR_OK && put == sizeof(h);
		cutpoint("cheats: the list's cache head written");
		memset(&o, 0, sizeof(o));
		while (ok && (n = cache_fill(&o, buf, sizeof(cs->part))) > 0) {
			ok = f_write(fil, buf, n, &put) == FR_OK && put == n;
			cutpoint("cheats: a part of the list's cache written");
		}
		ok &= f_close(fil) == FR_OK;
		cutpoint("cheats: the list's cache written");
	}
	tlog("cheats: %s: list %s (%lu cheats, %u headings, %lu bytes of names)", path, ok ? "kept" : "not kept",
	     (unsigned long)l->count, l->n_headings, (unsigned long)names);
	if (ok)
		l->dirty = 0;
	return ok;
}

// Reads the cache, if it is the file's: 1 with the list up
static int cache_read(void)
{
	struct cheatstore *cs = cheatstore();
	struct cheatlist *l = list();
	char path[SAFEFILE_PATH_MAX];
	u8 *buf = (u8 *)cs->part;
	struct cheatlist_head h;
	FIL *fil = &cs->fil;
	UINT got;
	cache_path(path);
	put_failed = 0;
	if (f_open(fil, path, FA_READ) != FR_OK)
		return 0;
	int ok = f_read(fil, buf, sizeof(h), &got) == FR_OK && got == sizeof(h);
	got = 0;
	memcpy(&h, buf, sizeof(h));
	u32 want = cheatstore_window_start(cs->windows ? cs->windows : 1u);
	ok = ok && h.magic == CHEATLIST_MAGIC && h.key_size == l->key_size && h.key_crc == l->key_crc &&
	     h.count == want && h.count <= CHEATLIST_MAX && h.n_headings <= CHEATLIST_HEADINGS &&
	     h.names_bytes <= CHEATLIST_NAMES_BYTES &&
	     f_size(fil) == sizeof(h) + CHEATLIST_CKPTS_OF(h.count) * sizeof(struct cheatlist_ckpt) +
				    h.n_headings * CHEATLIST_HEADING_BYTES + h.count * sizeof(struct cheatlist_entry) +
				    ((h.names_bytes + 3) & ~3u);
	// The rest a part at a time, each part's reads whole items: the headings
	// into RAM, the checkpoints, the entries and the names to page 3
	u32 crc = 0;
	const u32 parts[4] = {CHEATLIST_CKPTS_OF(h.count) * sizeof(struct cheatlist_ckpt),
			      h.n_headings * CHEATLIST_HEADING_BYTES, h.count * sizeof(struct cheatlist_entry),
			      (h.names_bytes + 3) & ~3u};
	const u32 units[4] = {sizeof(struct cheatlist_ckpt), CHEATLIST_HEADING_BYTES, sizeof(struct cheatlist_entry),
			      4};
	for (u32 p = 0; ok && p < 4; p++) {
		const u32 chunk = sizeof(cs->part) / units[p] * units[p];
		for (u32 at = 0; ok && at < parts[p]; at += got) {
			u32 n = parts[p] - at < chunk ? parts[p] - at : chunk;
			if (f_read(fil, buf, n, &got) != FR_OK || got != n) {
				ok = 0;
				break;
			}
			crc = crc32_update(crc, buf, got);
			if (p == 1) {
				for (u32 k = 0; k < got; k += CHEATLIST_HEADING_BYTES) {
					struct cheatlist_heading *hd = &l->headings[(at + k) / CHEATLIST_HEADING_BYTES];
					hd->len = buf[k] <= CHEATFILE_FOLDER_MAX ? buf[k] : 0;
					memcpy(hd->name, buf + k + 1, CHEATFILE_FOLDER_MAX);
				}
			} else {
				page_put((p == 0 ? (u8 *)CKPTS : p == 2 ? (u8 *)ENTRIES : NAMES) + at, buf, got);
			}
		}
	}
	f_close(fil);
	if (!ok || crc != h.crc || put_failed) {
		tlog("cheats: %s: no list that fits: built again", path);
		return 0;
	}
	l->count = h.count;
	l->n_headings = (u8)h.n_headings;
	l->names_bytes = h.names_bytes;
	l->names_full = 0;
	l->valid = 1;
	l->dirty = 0;
	return 1;
}

// --- A launch's quick decode ---------------------------------------------------------
//
// The cache read without the rest of the list: its header, its checkpoints
// (their CRC checked whole, then read a window's at a time) and an entry at
// a time. Page 3 is never touched: at a launch it may hold the game.

int cheatlist_launch_begin(void)
{
	struct cheatstore *cs = cheatstore();
	char path[SAFEFILE_PATH_MAX];
	u8 *buf = (u8 *)cs->part;
	struct cheatlist_head h;
	FIL *fil = &cs->quick.fil;
	UINT got;
	u32 size, crc, want = cheatstore_window_start(cs->windows ? cs->windows : 1u);
	cheatlist_launch_end();
	cache_path(path);
	if (f_open(fil, path, FA_READ) != FR_OK)
		return 0;
	cs->quick_open = 1;
	u32 t0 = torii_frames;
	cheatstore_key(&size, &crc);
	tlog_debug("cheats: the cache's key in %lu frames", (unsigned long)(torii_frames - t0));
	int ok = f_read(fil, &h, sizeof(h), &got) == FR_OK && got == sizeof(h) && h.magic == CHEATLIST_MAGIC &&
		 h.key_size == size && h.key_crc == crc && h.count == want && h.count <= CHEATLIST_MAX &&
		 h.n_headings <= CHEATLIST_HEADINGS;
	u32 ckpts = ok ? CHEATLIST_CKPTS_OF(h.count) * sizeof(struct cheatlist_ckpt) : 0, sum = 0;
	for (u32 at = 0; ok && at < ckpts; at += got) {
		u32 n = ckpts - at < sizeof(cs->part) ? ckpts - at : sizeof(cs->part);
		ok = f_read(fil, buf, n, &got) == FR_OK && got == n;
		sum = crc32_update(sum, buf, got);
	}
	if (!ok || sum != h.ckpt_crc) {
		tlog("cheats: %s: no cache that fits: the launch decodes every cheat", path);
		cheatlist_launch_end();
		return 0;
	}
	cs->quick.entries_at = sizeof(h) + ckpts + h.n_headings * CHEATLIST_HEADING_BYTES;
	return 1;
}

u32 cheatlist_launch_ckpts(u32 start, u32 n, struct cheatlist_ckpt *out, u32 max)
{
	struct cheatstore *cs = cheatstore();
	UINT got;
	u32 first = CHEATLIST_CKPTS_OF(start), last = (start + n) / CHEATLIST_CKPT_EVERY, k = 0;
	if (!cs->quick_open || first > last)
		return 0;
	if (last - first + 1 > max)
		last = first + max - 1;
	u32 bytes = (last - first + 1) * sizeof(struct cheatlist_ckpt);
	if (last >= CHEATLIST_CKPTS_OF(cheatstore_window_start(cs->windows ? cs->windows : 1u)))
		return 0;  // past the cache's (an edit's count): none
	if (f_lseek(&cs->quick.fil, sizeof(struct cheatlist_head) + first * sizeof(struct cheatlist_ckpt)) != FR_OK ||
	    f_read(&cs->quick.fil, out, bytes, &got) != FR_OK || got != bytes)
		return 0;
	// Only those it knows, each where it says (at past entry start + n: not this window's)
	for (u32 i = 0; i <= last - first; i++)
		if (out[i].at == (first + i) * CHEATLIST_CKPT_EVERY + 1 && out[i].at - 1 <= start + n)
			out[k++] = out[i];
	return k;
}

int cheatlist_launch_entry(u32 g, struct cheatlist_entry *e)
{
	struct cheatstore *cs = cheatstore();
	UINT got;
	return cs->quick_open && f_lseek(&cs->quick.fil, cs->quick.entries_at + g * sizeof(*e)) == FR_OK &&
	       f_read(&cs->quick.fil, e, sizeof(*e), &got) == FR_OK && got == sizeof(*e);
}

void cheatlist_launch_end(void)
{
	struct cheatstore *cs = cheatstore();
	if (cs->quick_open)
		f_close(&cs->quick.fil);
	cs->quick_open = 0;
}

// --- Up ---------------------------------------------------------------------------------

// Decodes the model read now whole, done counting from before of total
static void decode_model(u32 before, u32 total)
{
	struct cheatstore *cs = cheatstore();
	cheatstore_begin();
	while (!cheatstore_decode(16780000u / 20))
		cheatstore_show_progress(before + cs->work.next, total);
}

// The list built: every window decoded in turn (the decoder carrying from
// each to the next, as the index keeps it), then the one read before again
static int build(void)
{
	struct cheatstore *cs = cheatstore();
	struct cheatlist *l = list();
	u32 total = cheatstore_window_start(cs->windows ? cs->windows : 1u), was = cheatstore_window();
	cheatlist_build_begin();
	if (!cs->windows) {
		decode_model(0, total);
		cheatlist_from_model(0, 0);
		return cheatlist_build_end();
	}
	for (u32 k = 0; k < cs->windows && l->building == 1; k++) {
		if (k != cs->window && !cheatstore_window_go(k)) {
			cheatlist_drop();
			break;
		}
		decode_model(cheatstore_window_start(k), total);
		cheatlist_from_model(cheatstore_window_start(k), 0);
	}
	if (!cheatlist_build_end())
		return 0;
	if (cheatstore_window() != was)
		cheatstore_window_go(was);  // the list is up: taken from it, not decoded
	return 1;
}

int cheatlist_open(void)
{
	struct cheatstore *cs = cheatstore();
	struct cheatlist *l = list();
	u32 start = torii_frames;
	cheatstore_key(&l->key_size, &l->key_crc);
	l->on = 1;
	if (l->valid) {
		// Built as the file was read (a larger file indexed): kept now
		cheatlist_apply(cheatstore_window_start(cs->window));
		cheatlist_sync_picks();
		if (l->dirty)
			cache_write();
		return 1;
	}
	if (cache_read() && cheatlist_apply(cheatstore_window_start(cs->window))) {
		cheatlist_sync_picks();
		tlog("cheats: %s: %lu cheats listed from its cache in %lu frames", cs->path, (unsigned long)l->count,
		     (unsigned long)(torii_frames - start));
		return 1;
	}
	if (!build()) {
		l->on = 0;
		return 0;
	}
	cheatlist_sync_picks();
	cache_write();
	tlog("cheats: %s: %lu cheats listed, decoded in %lu frames", cs->path, (unsigned long)l->count,
	     (unsigned long)(torii_frames - start));
	return 1;
}

void cheatlist_save(void)
{
	struct cheatlist *l = list();
	if (!l->valid || !l->dirty)
		return;
	cheatstore_key(&l->key_size, &l->key_crc);
	cache_write();
}
