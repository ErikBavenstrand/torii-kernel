// A game's screenshots on the card (shotlist.h).
#include "shotlist.h"

#include <stdio.h>
#include <string.h>

#include "Ezcard_OP.h"
#include "crc32.h"
#include "ez_define.h"
#include "ff.h"
#include "tlog.h"
#include "userdata.h"
#include "engine/cycles.h"
#include "engine/psram.h"

extern u8 pReadCache[];

#define TABLE_MAGIC 0x31544853u  // "SHT1"

struct table_entry {
	u32 crc;  // of a stem
	u32 count;
};

// In PSRAM page 2: the head, then its entries sorted by crc
struct table_head {
	u32 magic;
	u32 count;
	u32 crc;  // of the entries
	u32 files;
};

// pReadCache from READCACHE_SHOTS_LIST, within a call
struct list_work {
	DIR dir;
	FILINFO info;
	struct table_head head;
	u32 crc[SHOTLIST_FILES_MAX];
	struct table_entry out[SHOTLIST_GAMES_MAX];
};
_Static_assert(sizeof(struct list_work) <= READCACHE_SHOTS_LIST_SIZE, "the count's share of pReadCache");
_Static_assert(READCACHE_SHOTS_LIST + READCACHE_SHOTS_LIST_SIZE <= MAX_pReadCache_size, "READCACHE_SHOTS_LIST");
_Static_assert(SHOTLIST_TABLE_OFFSET + sizeof(struct table_head) + SHOTLIST_GAMES_MAX * sizeof(struct table_entry) <=
		       PSRAM_WINDOW_BYTES,
	       "the count table fits page 2");

static struct list_work *work(void)
{
	return (struct list_work *)(void *)(pReadCache + READCACHE_SHOTS_LIST);
}

static const struct table_head *table(void)
{
	return (const struct table_head *)(PSRAM_WINDOW + SHOTLIST_TABLE_OFFSET);
}

void shotlist_stem(char *out, u32 size, const char *game)
{
	const char *nor = userdata_nor_name(game);
	shotname_stem(out, size, nor ? nor : game);
}

// 1 if page 2 (selected) holds a whole table
static int table_ok(void)
{
	const struct table_head *h = table();
	return h->magic == TABLE_MAGIC && h->count <= SHOTLIST_GAMES_MAX &&
	       crc32(h + 1, h->count * sizeof(struct table_entry)) == h->crc;
}

static void sort(u32 *a, u32 n)
{
	for (u32 gap = n / 2; gap; gap /= 2)  // Shell's: no recursion, no heap
		for (u32 i = gap; i < n; i++) {
			u32 v = a[i], j = i;
			for (; j >= gap && a[j - gap] > v; j -= gap)
				a[j] = a[j - gap];
			a[j] = v;
		}
}

// The folder read into the table; 0 if PSRAM wouldn't take it
static int table_build(void)
{
	struct list_work *w = work();
	u32 start = cycles_now(), files = 0, n = 0, games = 0;
	if (f_opendir(&w->dir, SHOTLIST_DIR) == FR_OK) {
		for (;;) {
			if (f_readdir(&w->dir, &w->info) != FR_OK || !w->info.fname[0])
				break;
			struct shotname_when when;
			u32 len = (w->info.fattrib & AM_DIR) ? 0 : shotname_split(w->info.fname, &when);
			if (!len)
				continue;
			files++;
			if (n < SHOTLIST_FILES_MAX)
				w->crc[n++] = crc32(w->info.fname, len);
		}
		f_closedir(&w->dir);
	}
	sort(w->crc, n);
	for (u32 i = 0; i < n && games < SHOTLIST_GAMES_MAX; i++) {
		if (games && w->out[games - 1].crc == w->crc[i])
			w->out[games - 1].count++;
		else
			w->out[games++] = (struct table_entry){w->crc[i], 1};
	}
	w->head = (struct table_head){TABLE_MAGIC, games, crc32(w->out, games * sizeof(struct table_entry)), files};
	SetPSRampage(SHOTLIST_PAGE);
	void *at = (void *)table();
	int ok = (!games || psram_write((u8 *)at + sizeof(w->head), w->out, games * sizeof(struct table_entry))) &&
		 psram_write(at, &w->head, sizeof(w->head)) && table_ok();
	SetPSRampage(PSRAM_ASSET_PAGE);
	tlog("shots: %lu screenshots of %lu games counted in %lu cycles%s", (unsigned long)files, (unsigned long)games,
	     (unsigned long)cycles_since(start), ok ? "" : " (PSRAM didn't keep them)");
	return ok;
}

u32 shotlist_count(const char *game)
{
	char stem[SHOTNAME_STEM_BYTES];
	shotlist_stem(stem, sizeof(stem), game);
	if (!stem[0])
		return 0;
	SetPSRampage(SHOTLIST_PAGE);
	int ok = table_ok();
	SetPSRampage(PSRAM_ASSET_PAGE);
	if (!ok && !table_build())
		return 0;
	u32 crc = crc32(stem, (u32)strlen(stem)), found = 0;
	SetPSRampage(SHOTLIST_PAGE);
	const struct table_head *h = table();
	const struct table_entry *e = (const struct table_entry *)(h + 1);
	u32 lo = 0, hi = h->count;
	while (lo < hi) {
		u32 mid = (lo + hi) / 2;
		if (e[mid].crc < crc) {
			lo = mid + 1;
		} else {
			if (e[mid].crc == crc)
				found = e[mid].count;
			hi = mid;
		}
	}
	SetPSRampage(PSRAM_ASSET_PAGE);
	return found;
}

void shotlist_forget(void)
{
	u32 none[2] = {0, 0};  // in RAM: a DMA from ROM misses PSRAM
	SetPSRampage(SHOTLIST_PAGE);
	psram_write((void *)table(), none, sizeof(none));
	SetPSRampage(PSRAM_ASSET_PAGE);
}

void shotlist_changed(const char *path)
{
	const char *slash = strrchr(path, '/');
	u32 n = slash ? (u32)(slash - path) : 0;
	if (n != sizeof(SHOTLIST_DIR) - 1)
		return;
	for (u32 i = 0; i < n; i++)  // FAT's names are alike in any case
		if ((path[i] | 0x20) != (SHOTLIST_DIR[i] | 0x20))
			return;
	shotlist_forget();
}

#define MOVE_BATCH 8  // names gathered before they move: none moves under f_readdir

// pReadCache from READCACHE_SHOTS_MOVE, within shotlist_rename
struct move_work {
	DIR dir;
	FILINFO info;
	char old_stem[SHOTNAME_STEM_BYTES], new_stem[SHOTNAME_STEM_BYTES];
	char name[MOVE_BATCH][SHOTNAME_STEM_BYTES + 32];  // a shot's: its stem, its suffix, .bmp
	char from[sizeof(SHOTLIST_DIR) + SHOTNAME_STEM_BYTES + 32];
	char to[sizeof(SHOTLIST_DIR) + SHOTNAME_STEM_BYTES + 32];
};
_Static_assert(sizeof(struct move_work) <= READCACHE_SHOTS_MOVE_SIZE, "the move's share of pReadCache");

u32 shotlist_rename(const char *old_name, const char *new_name)
{
	struct move_work *w = (struct move_work *)(void *)(pReadCache + READCACHE_SHOTS_MOVE);
	shotname_stem(w->old_stem, sizeof(w->old_stem), old_name);
	shotname_stem(w->new_stem, sizeof(w->new_stem), new_name);
	if (!w->old_stem[0] || !w->new_stem[0] || !strcmp(w->old_stem, w->new_stem))
		return 0;
	u32 moved = 0, stuck = 0, old_len = (u32)strlen(w->old_stem);
	for (;;) {
		// A batch of the old stem's shots, past the ones that wouldn't move
		u32 n = 0, seen = 0;
		if (f_opendir(&w->dir, SHOTLIST_DIR) != FR_OK)
			break;
		while (n < MOVE_BATCH && f_readdir(&w->dir, &w->info) == FR_OK && w->info.fname[0]) {
			struct shotname_when when;
			if ((w->info.fattrib & AM_DIR) || strlen(w->info.fname) >= sizeof(w->name[0]) ||
			    !shotname_match(w->info.fname, w->old_stem, &when) || seen++ < stuck)
				continue;
			memcpy(w->name[n++], w->info.fname, strlen(w->info.fname) + 1);  // it fits: checked above
		}
		f_closedir(&w->dir);
		if (!n)
			break;
		for (u32 i = 0; i < n; i++) {
			// On the stack: GCC can't tell the parts of pReadCache apart
			char name[sizeof(w->name[0])], stem[sizeof(w->new_stem)];
			memcpy(name, w->name[i], sizeof(name));
			memcpy(stem, w->new_stem, sizeof(stem));
			snprintf(w->from, sizeof(w->from), SHOTLIST_DIR "/%s", name);
			snprintf(w->to, sizeof(w->to), SHOTLIST_DIR "/%s%s", stem, name + old_len);
			FRESULT r = f_rename(w->from, w->to);
			if (r == FR_OK) {
				moved++;
			} else {
				stuck++;
				tlog("shots: %s stays (FatFs %u)", w->from, (unsigned)r);
			}
		}
	}
	if (moved) {
		shotlist_forget();
		tlog("shots: %lu moved from %s to %s", (unsigned long)moved, w->old_stem, w->new_stem);
	}
	return moved;
}
