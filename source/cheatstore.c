// A game's cheat file on the card, and its cheats at a launch (cheatstore.h).
#include "cheatstore.h"

#include <stdio.h>
#include <string.h>

#include "cheat.h"
#include "crc32.h"
#include "cutpoint.h"
#include "ez_define.h"
#include "Ezcard_OP.h"
#include "GBApatch.h"
#include "ff.h"
#include "tlog.h"
#include "engine/cycles.h"

extern u8 pReadCache[];

_Static_assert(sizeof(struct cheatstore) <= READCACHE_CHEATS_SIZE, "the cheat file's share of pReadCache");
_Static_assert(READCACHE_CHEATS + READCACHE_CHEATS_SIZE <= MAX_pReadCache_size, "READCACHE_CHEATS");
_Static_assert(CHEATFILE_BYTES_MAX + 4 == CHEAT_TABLE_MAX, "a cheat file's budget is the table's");

u32 gl_cheat_table[CHEAT_TABLE_MAX / 4] EWRAM_BSS;
u32 gl_cheat_bytes;
u32 gl_cheat_count;
u32 gl_cheat_patches;

struct cheatstore *cheatstore(void)
{
	return (struct cheatstore *)(void *)(pReadCache + READCACHE_CHEATS);
}

int cheatstore_path(char *out, u32 size, const char *rom_path)
{
	const char *slash = strrchr(rom_path, '/');
	const char *name = slash ? slash + 1 : rom_path;
	const char *dot = strrchr(name, '.');
	u32 stem = dot && dot != name ? (u32)(dot - rom_path) : (u32)strlen(rom_path);
	const char *sep = rom_path[0] == '/' ? "" : "/";
	u32 n = (u32)snprintf(out, size, "%s%s%.*s.cht", CHEATS_DIR, sep, (int)stem, rom_path);
	if (n >= size || n + 4 >= SAFEFILE_PATH_MAX)
		return -1;
	return (int)n;
}

// 0 if f_stat says path is surely not on the card; a card error counts as
// there, so a file it hid is never written over
static int maybe_there(const char *path)
{
	FILINFO info;
	FRESULT r = f_stat(path, &info);
	return r != FR_NO_FILE && r != FR_NO_PATH;
}

// 1 if either copy of the cheat file at path (cheatstore()->path) may be on
// the card, though safefile_open took neither: too big, say
static int any_copy(char *path)
{
	u32 len = (u32)strlen(path);
	int there = maybe_there(path);
	memcpy(path + len, ".bak", 5);  // cheatstore_path leaves room for it
	there |= maybe_there(path);
	path[len] = 0;
	return there;
}

int cheatstore_open(const char *rom_path)
{
	struct cheatstore *cs = cheatstore();
	cs->file.count = 0;
	cs->work.next = 0;
	if (cheatstore_path(cs->path, sizeof(cs->path), rom_path) < 0) {
		tlog("cheats: %s: its cheat file's path is too long", rom_path);
		return CHEATSTORE_NONE;
	}
	if (safefile_open(cs->path, CHEATSTORE_FILE_MAX) == SAFEFILE_NONE) {
		if (safefile_failed()) {
			// A card error, not an absent file: never made anew over it
			tlog_error("cheats: %s can't be read", cs->path);
			return CHEATSTORE_NONE;
		}
		if (any_copy(cs->path)) {
			tlog("cheats: %s is there but can't be read (over %u bytes?), left alone", cs->path,
			     CHEATFILE_TEXT_MAX);
			return CHEATSTORE_NONE;
		}
		// None yet: an empty file, made on the first write
		cheatfile_parse(&cs->file, (const char *)cs->text, 0, cs->cheats, CHEATFILE_MAX);
		cheatfile_begin(&cs->work);
		return CHEATSTORE_NEW;
	}
	int len = safefile_read(cs->text, sizeof(cs->text));
	int failed = safefile_failed();
	safefile_close();
	if (len < 0 || failed) {
		tlog_error("cheats: %s can't be read", cs->path);
		return CHEATSTORE_NONE;
	}
	if ((u32)len > CHEATFILE_TEXT_MAX) {
		// Its cheats would be read from a cut text, and the picks written
		// back over the rest: none of it, then
		tlog("cheats: %s is %lu bytes, over %u: too big, left alone", cs->path, (unsigned long)len,
		     CHEATFILE_TEXT_MAX);
		return CHEATSTORE_NONE;
	}
	cheatfile_parse(&cs->file, (const char *)cs->text, (u32)len, cs->cheats, CHEATFILE_MAX);
	cheatfile_begin(&cs->work);
	if (cs->file.dropped)
		tlog("cheats: %s: %lu cheats numbered past %u are left out", cs->path, (unsigned long)cs->file.dropped,
		     CHEATFILE_MAX);
	tlog_debug("cheats: %s: %lu bytes, %lu cheats", cs->path, (unsigned long)len, (unsigned long)cs->file.count);
	return CHEATSTORE_READ;
}

int cheatstore_read(const char *rom_path)
{
	return cheatstore_open(rom_path) == CHEATSTORE_READ && cheatstore()->file.count != 0;
}

void cheatstore_decoder_before(u32 i, struct cheat_decoder *out)
{
	struct cheatstore *cs = cheatstore();
	if (cs->work.next != i || i == 0) {
		cheatfile_begin(&cs->work);
		while (cs->work.next < i && cheatfile_next(&cs->file, &cs->work) >= 0)
			;
	}
	*out = cs->work.dec;
}

int cheatstore_decode(u32 budget)
{
	struct cheatstore *cs = cheatstore();
	u32 start = cycles_now();
	while (cheatfile_next(&cs->file, &cs->work) >= 0)
		if (budget && cycles_since(start) > budget)
			return cs->work.next >= cs->file.count;
	return 1;
}

// The last file cheatstore_listed counted whole: its path's CRC and cheat
// count (picks don't change what is listed), and its listed count
static struct {
	u32 path_crc, count, listed;
	u8 valid;
} listed_cache EWRAM_BSS;

s32 cheatstore_listed(u32 budget)
{
	struct cheatstore *cs = cheatstore();
	u32 path_crc = crc32(cs->path, (u32)strlen(cs->path));
	if (listed_cache.valid && listed_cache.path_crc == path_crc && listed_cache.count == cs->file.count)
		return (s32)listed_cache.listed;
	if (!cheatstore_decode(budget))
		return -1;
	u32 listed = 0;
	for (u32 i = 0; i < cs->file.count; i++)
		listed += (u32)cheatfile_shown(&cs->cheats[i]);
	listed_cache.path_crc = path_crc;
	listed_cache.count = cs->file.count;
	listed_cache.listed = listed;
	listed_cache.valid = 1;
	return (s32)listed;
}

int cheatstore_write(void)
{
	struct cheatstore *cs = cheatstore();
	if (!cheatfile_changed(&cs->file))
		return 1;
	if (cs->file.cut)
		return 0;  // never a text cut short (cheatstore_read refuses such a file)
	u32 len = cheatfile_text_len(&cs->file);
	if (len > CHEATFILE_TEXT_MAX) {
		tlog_error("cheats: %s would be %lu bytes, over %u: not written", cs->path, (unsigned long)len,
			   CHEATFILE_TEXT_MAX);
		return 0;
	}
	int ok = safefile_write_cut(cs->path, CHEATSTORE_FILE_MAX, cheatfile_text, &cs->file);
	tlog("cheats: %s: %lu picked, %s", cs->path, (unsigned long)cheatfile_picks(&cs->file),
	     ok ? "written" : "not written");
	if (ok)
		cheatfile_written(&cs->file);
	return ok;
}

int cheatstore_delete(const char *rom_path)
{
	char *path = cheatstore()->path;
	int len = cheatstore_path(path, SAFEFILE_PATH_MAX, rom_path);
	if (len < 0)
		return 1;
	FRESULT a = f_unlink(path);
	if (a == FR_OK)
		cutpoint("cheats: the cheat file deleted");
	memcpy(path + len, ".bak", 5);  // cheatstore_path leaves room for it
	FRESULT b = f_unlink(path);
	if (b == FR_OK)
		cutpoint("cheats: its .bak deleted");
	int gone = (a == FR_OK || a == FR_NO_FILE || a == FR_NO_PATH) &&
		   (b == FR_OK || b == FR_NO_FILE || b == FR_NO_PATH);
	if (a == FR_OK || b == FR_OK || !gone)
		tlog("cheats: %s: deleted (FatFs %u, %u)", path, (unsigned)a, (unsigned)b);
	return gone;
}

// The halfword at offset in the game's PSRAM image becomes (old & ~mask) |
// (value & mask); PSRAM shows 8 MB a page
static void rom_patch(const struct cheat_patch *p)
{
	vu16 *at = (vu16 *)((u8 *)PSRAMBase_S98 + (p->offset & 0x7FFFFF));
	SetPSRampage((u16)((p->offset >> 23) * 0x1000));
	*at = (u16)((*at & ~p->mask) | (p->value & p->mask));
	SetPSRampage(0);
}

static const char *const status_names[] = {"new",        "ok",          "master",        "too big",
					   "unreadable", "placeholder", "can't run here"};

void cheatstore_launch(const char *rom_path, u32 psram_bytes)
{
	u32 cost = CHEAT_COST_CALL;
	gl_cheat_count = 0;
	gl_cheat_bytes = 0;
	gl_cheat_patches = 0;
	if (!cheatstore_read(rom_path))
		return;
	struct cheatstore *cs = cheatstore();
	struct cheatfile *f = &cs->file;
	u32 last = 0, picks = 0, applied = 0, skipped = 0, guarded = 0;
	for (u32 i = 0; i < f->count; i++)
		if (cs->cheats[i].picked) {
			last = i + 1;
			picks++;
		}
	if (!picks)
		return;
	// Every cheat up to the last pick, as keys carry from cheat to cheat
	s32 i;
	while ((i = cheatfile_next(f, &cs->work)) >= 0 && (u32)i < last) {
		const struct cheatfile_cheat *c = &cs->cheats[i];
		if (!c->picked)
			continue;
		if (c->status != CHEATFILE_OK) {
			tlog("cheats: cheat %u (%.*s): %s, left out", c->number, c->desc_len, f->text + c->desc,
			     status_names[c->status]);
			continue;
		}
		u32 bytes = cs->work.n_prog * 4;
		if (gl_cheat_bytes + bytes > CHEATFILE_BYTES_MAX || cost + cs->work.cost > CHEAT_CYCLES_MAX) {
			tlog("cheats: cheat %u (%.*s): its %lu bytes and %lu cycles don't fit beside %lu and %lu, left "
			     "out",
			     c->number, c->desc_len, f->text + c->desc, (unsigned long)bytes,
			     (unsigned long)cs->work.cost, (unsigned long)gl_cheat_bytes, (unsigned long)cost);
			continue;
		}
		if (bytes) {
			memcpy((u8 *)gl_cheat_table + gl_cheat_bytes, cs->work.prog, bytes);
			gl_cheat_bytes += bytes;
			cost += cs->work.cost;
			gl_cheat_count++;
		}
		gl_cheat_patches += cs->work.n_patches;
		for (u32 p = 0; psram_bytes && p < cs->work.n_patches; p++) {
			const struct cheat_patch *pt = &cs->work.patches[p];
			if (pt->offset + 2 > psram_bytes) {
				skipped++;
				continue;
			}
			// Not over the kernel's own patches: the patch area (the hook,
			// the interpreter, this table), the entry branch, the hook's words
			if (rom_guard_hits(pt->offset, 2)) {
				if (!guarded++ || tlog_wants(TLOG_DEBUG))
					tlog("cheats: cheat %u (%.*s): its ROM patch at %08lX is over the kernel's "
					     "own, "
					     "left out",
					     c->number, c->desc_len, f->text + c->desc,
					     (unsigned long)0x08000000 + pt->offset);
				continue;
			}
			rom_patch(pt);
			applied++;
		}
	}
	if (gl_cheat_bytes) {
		gl_cheat_table[gl_cheat_bytes / 4] = 0;  // the table's end
		gl_cheat_bytes += 4;
	}
	if (psram_bytes)
		tlog("cheats: %lu ROM patches written into the game%s%s", (unsigned long)applied,
		     skipped ? ", some past its end left out" : "",
		     guarded ? ", some over the kernel's own patches left out" : "");
	else
		tlog("cheats: %s: %lu picked, %lu run in game (%lu bytes, %lu cycles a frame at most), %lu ROM "
		     "patches",
		     cs->path, (unsigned long)picks, (unsigned long)gl_cheat_count, (unsigned long)gl_cheat_bytes,
		     (unsigned long)(gl_cheat_count ? cost : 0), (unsigned long)gl_cheat_patches);
}
