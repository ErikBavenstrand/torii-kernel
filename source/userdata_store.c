// The user's lists, the kernel's store: Recent and the favourites in PSRAM,
// read from and written to the SD card (userdata.h).
//
// PSRAM, from PSRAM_USERDATA_BASE: Recent's games as struct userdata_recent
// (newest first), then the favourites as a table of handles in path order,
// then their paths, each NUL-padded to a word (a handle is its offset). A
// change to Recent is made on a copy in RAM, then written to PSRAM and the
// card; a change to the favourites is written to the card, which is then
// read back into PSRAM, so the store always holds what the card does.
#include "userdata.h"

#include <stdio.h>
#include <string.h>

#include "RTC.h"
#include "engine/psram.h"
#include "ez_define.h"
#include "ezkernel.h"
#include "ff.h"
#include "lang.h"
#include "safefile.h"
#include "tlog.h"

_Static_assert(USERDATA_DIR_MAX == MAX_path_len && USERDATA_NAME_MAX == GAME_NAME_MAX &&
		       USERDATA_NOR_NAME_MAX == sizeof(((FM_NOR_FS *)0)->filename) - 1,
	       "the lists' path limits are the launch path's");
_Static_assert(USERDATA_LINE_MAX <= SAFEFILE_LINE_MAX, "a line of recent.txt fits safefile_lines");

#define WORD_UP(n) (((n) + 3u) & ~3u)
#define RECENT_BYTES WORD_UP(USERDATA_RECENT_MAX * sizeof(struct userdata_recent))
#define FAV_TABLE_BYTES (USERDATA_FAV_MAX * sizeof(u32))
#define RECENT_PS ((const struct userdata_recent *)PSRAM_USERDATA_BASE)
#define FAV_TABLE_PS ((const u32 *)(PSRAM_USERDATA_BASE + RECENT_BYTES))
#define FAV_POOL_PS (PSRAM_USERDATA_BASE + RECENT_BYTES + FAV_TABLE_BYTES)
#define FAV_POOL_BYTES (PSRAM_USERDATA_BYTES - RECENT_BYTES - FAV_TABLE_BYTES)
_Static_assert(FAV_POOL_BYTES >= 0x40000, "room for the favourites' paths");

// What an edit of the favourites does to the list as it is written
struct fav_edit {
	const char *add;  // a path to add, or NULL
	u32 add_at;       // where it goes
	int (*drop)(const char *path, const char *arg);  // paths to leave out, or NULL
	const char *arg;
	u32 k;     // the table's next entry to look at
	u8 added;  // add is written
};

// The store's RAM, in pReadCache within one call
struct work {
	union {
		struct userdata_recent recent[USERDATA_RECENT_MAX];
		u32 fav[USERDATA_FAV_MAX];
	} u;
	struct safefile_lines lines;
	struct fav_edit edit;
	char line[USERDATA_LINE_MAX];
	char path[USERDATA_PATH_MAX];  // a path being added, or a joined path
	char pad[USERDATA_PATH_MAX];   // a favourite's path on its way to PSRAM
};
_Static_assert(sizeof(struct work) <= READCACHE_USERDATA_SIZE, "the store's RAM fits its part of pReadCache");
#define W ((struct work *)(pReadCache + READCACHE_USERDATA))

static struct {
	u8 recent_ok, fav_ok;  // read since the last userdata_invalidate
	u8 boot_set;           // the boot position changed since recent.txt was written
	u32 recent_count;
	u32 fav_count;
	u32 fav_used;  // pool bytes
	struct userdata_boot boot;
	const char *recent_path, *fav_path, *old_path;  // tests' files; NULL for the user's
	struct userdata_boot kept_boot;                 // the user's, while the tests run
	u8 kept_boot_set;
} st EWRAM_BSS;

static const char *recent_file(void)
{
	return st.recent_path ? st.recent_path : USERDATA_RECENT_PATH;
}

static const char *fav_file(void)
{
	return st.fav_path ? st.fav_path : USERDATA_FAV_PATH;
}

static const char *old_file(void)
{
	return st.old_path ? st.old_path : USERDATA_OLD_RECENT_PATH;
}

static const char *fav_str(u32 handle)
{
	return (const char *)(FAV_POOL_PS + handle);
}

void userdata_invalidate(void)
{
	st.recent_ok = 0;
	st.fav_ok = 0;
}

// --- Recent ------------------------------------------------------------------

static void load_recent(void)
{
	struct userdata_boot boot = {{0}, 0};
	const char *from_file = recent_file();
	u32 count = 0, dropped = 0;
	int from = safefile_open(from_file, USERDATA_RECENT_FILE_MAX);
	if (from == SAFEFILE_NONE) {
		// No recent.txt yet: the v0.3 list, if there is one, until the
		// first write takes it over
		from_file = old_file();
		from = safefile_open(from_file, USERDATA_RECENT_FILE_MAX);
	}
	int len;
	while (from != SAFEFILE_NONE && (len = safefile_line(W->line, sizeof(W->line))) != SAFEFILE_END) {
		u32 time;
		char *path;
		int kind = len == SAFEFILE_LONG ? USERDATA_BAD : userdata_parse_recent(W->line, &time, &path, &boot);
		if (kind == USERDATA_BAD)
			dropped++;
		if (kind != USERDATA_GAME)
			continue;
		u32 i = 0;
		while (i < count && strcmp(W->u.recent[i].path, path) != 0)
			i++;
		if (i < count || count == USERDATA_RECENT_MAX) {
			dropped += i == count;  // twice is no loss; past the cap is
			continue;
		}
		W->u.recent[count].time = time;
		snprintf(W->u.recent[count].path, sizeof(W->u.recent[count].path), "%s", path);
		count++;
	}
	safefile_close();
	if (count) {
		psram_begin();
		psram_write((void *)RECENT_PS, W->u.recent, count * sizeof(W->u.recent[0]));
	}
	st.recent_count = count;
	st.recent_ok = 1;
	if (!st.boot_set)
		st.boot = boot;  // a place set since the last write wins
	if (from != SAFEFILE_NONE)
		tlog("userdata: Recent: %lu games from %s, %lu lines dropped", (unsigned long)count, from_file,
		     (unsigned long)dropped);
}

static void ensure_recent(void)
{
	if (!st.recent_ok)
		load_recent();
}

// Recent's games into W->u.recent; returns how many
static u32 recent_get(void)
{
	ensure_recent();
	memcpy(W->u.recent, RECENT_PS, st.recent_count * sizeof(W->u.recent[0]));
	return st.recent_count;
}

static void ensure_fav(void);

void userdata_load(void)
{
	ensure_recent();
	ensure_fav();
}

static u32 recent_line(void *ctx, u32 n, char *buf, u32 size)
{
	(void)ctx;
	return userdata_recent_line(W->u.recent, st.recent_count, &st.boot, n, buf, size);
}

// W->u.recent's count games become Recent, in PSRAM and on the card
static int recent_put(u32 count)
{
	if (count) {
		psram_begin();
		psram_write((void *)RECENT_PS, W->u.recent, count * sizeof(W->u.recent[0]));
	}
	st.recent_count = count;
	W->lines.line = recent_line;
	W->lines.ctx = NULL;
	if (!safefile_write(recent_file(), USERDATA_RECENT_FILE_MAX, safefile_lines_fill, &W->lines))
		return 0;
	st.boot_set = 0;
	// recent.txt holds its games now: the v0.3 list goes (also when a power
	// cut before this left it beside a recent.txt)
	if (f_unlink(old_file()) == FR_OK)
		tlog("userdata: took over %s", old_file());
	return 1;
}

u32 userdata_recent_count(void)
{
	ensure_recent();
	return st.recent_count;
}

const char *userdata_recent_path(u32 i)
{
	ensure_recent();
	return i < st.recent_count ? RECENT_PS[i].path : "";
}

u32 userdata_recent_time(u32 i)
{
	ensure_recent();
	return i < st.recent_count ? RECENT_PS[i].time : 0;
}

int userdata_recent_add(const char *path)
{
	if (!userdata_path_ok(path)) {
		tlog("userdata: can't keep %s on Recent", path);
		return 0;
	}
	snprintf(W->path, sizeof(W->path), "%s", path);  // path may be in PSRAM
	u32 now = userdata_now();
	u32 count = recent_get();
	return recent_put(userdata_recent_put(W->u.recent, count, W->path, now));
}

int userdata_recent_add_file(const char *dir, const char *name)
{
	char *path = W->pad;  // userdata_recent_add copies it to W->path
	return userdata_join(path, dir, name) && userdata_recent_add(path);
}

const struct userdata_boot *userdata_boot_get(void)
{
	ensure_recent();
	return &st.boot;
}

void userdata_boot_set(const char *item, u32 row)
{
	if (!userdata_item_ok(item)) {
		tlog("userdata: no boot place %s", item);
		return;
	}
	ensure_recent();
	if (st.boot.row == row && strcmp(st.boot.item, item) == 0)
		return;
	snprintf(st.boot.item, sizeof(st.boot.item), "%s", item);
	st.boot.row = row;
	st.boot_set = 1;
}

int userdata_flush(void)
{
	if (!st.boot_set)
		return 1;
	return recent_put(recent_get());
}

// --- Favourites ----------------------------------------------------------------

static void load_fav(void)
{
	u32 count = 0, used = 0, dropped = 0;
	int from = safefile_open(fav_file(), USERDATA_FAV_FILE_MAX);
	int len;
	psram_begin();
	while (from != SAFEFILE_NONE && (len = safefile_line(W->line, sizeof(W->line))) != SAFEFILE_END) {
		char *path;
		int kind = len == SAFEFILE_LONG ? USERDATA_BAD : userdata_parse_fav(W->line, &path);
		if (kind == USERDATA_BAD)
			dropped++;
		if (kind != USERDATA_GAME)
			continue;
		u32 n = (u32)strlen(path);
		u32 bytes = WORD_UP(n + 1);
		if (count == USERDATA_FAV_MAX || used + bytes > FAV_POOL_BYTES) {
			dropped++;
			continue;
		}
		memcpy(W->pad, path, n + 1);
		memset(W->pad + n + 1, 0, bytes - n - 1);
		psram_write((void *)(FAV_POOL_PS + used), W->pad, bytes);
		W->u.fav[count++] = used;
		used += bytes;
	}
	safefile_close();
	if (!userdata_in_order(W->u.fav, count, fav_str)) {
		count = userdata_sort(W->u.fav, count, fav_str);  // edited by hand
		tlog("userdata: favourites put in order");
	}
	if (count) {
		psram_begin();
		psram_write((void *)FAV_TABLE_PS, W->u.fav, count * sizeof(W->u.fav[0]));
	}
	st.fav_count = count;
	st.fav_used = used;
	st.fav_ok = 1;
	if (from != SAFEFILE_NONE)
		tlog("userdata: %lu favourites, %lu lines dropped", (unsigned long)count, (unsigned long)dropped);
}

static void ensure_fav(void)
{
	if (!st.fav_ok)
		load_fav();
}

// Line n of favorites.txt as W->edit changes it. safefile_lines asks for the
// lines in order, from 0 again for the second copy.
static u32 fav_line(void *ctx, u32 n, char *buf, u32 size)
{
	struct fav_edit *e = ctx;
	if (n == 0) {
		e->k = 0;
		e->added = 0;
	}
	for (;;) {
		const char *path;
		if (e->add && !e->added && e->k == e->add_at) {
			path = e->add;
			e->added = 1;
		} else if (e->k < st.fav_count) {
			path = fav_str(FAV_TABLE_PS[e->k++]);
			if (e->drop && e->drop(path, e->arg))
				continue;
		} else {
			return 0;
		}
		int len = snprintf(buf, size, "%s\n", path);
		return len < 0 ? 0 : (u32)len < size ? (u32)len : size - 1;
	}
}

// Writes the favourites with W->edit's change, then reads them back (over
// W->u, W->line and W->pad: the edit's paths are needed only until then)
static int fav_write(void)
{
	W->lines.line = fav_line;
	W->lines.ctx = &W->edit;
	int ok = safefile_write(fav_file(), USERDATA_FAV_FILE_MAX, safefile_lines_fill, &W->lines);
	st.fav_ok = 0;
	ensure_fav();
	return ok;
}

static int same_path(const char *path, const char *arg)
{
	return strcmp(path, arg) == 0;
}

static int nor_game(const char *path, const char *name)
{
	const char *nor = userdata_nor_name(path);
	return nor && (!name || strcmp(nor, name) == 0);
}

// How many favourites drop says yes to
static u32 fav_matches(int (*drop)(const char *path, const char *arg), const char *arg)
{
	u32 n = 0;
	for (u32 i = 0; i < st.fav_count; i++)
		n += drop(fav_str(FAV_TABLE_PS[i]), arg) != 0;
	return n;
}

// Leaves out the favourites drop says yes to, and adds add (NULL: none)
static int fav_change(const char *add, int (*drop)(const char *path, const char *arg), const char *arg)
{
	struct fav_edit *e = &W->edit;
	int found = 0;
	memset(e, 0, sizeof(*e));
	if (add) {
		e->add_at = userdata_find(FAV_TABLE_PS, st.fav_count, add, fav_str, &found);
		// A rename drops a path as it adds one: never full then
		int full = st.fav_count == USERDATA_FAV_MAX || st.fav_used + WORD_UP(strlen(add) + 1) > FAV_POOL_BYTES;
		if (!found && full && !(drop && fav_matches(drop, arg))) {
			tlog_error("userdata: the favourites are full");
			return 0;
		}
	}
	e->add = found ? NULL : add;
	e->drop = drop;
	e->arg = arg;
	if (!e->add && (!drop || !fav_matches(drop, arg)))
		return 1;  // nothing changes
	return fav_write();
}

u32 userdata_fav_count(void)
{
	ensure_fav();
	return st.fav_count;
}

const char *userdata_fav_path(u32 i)
{
	ensure_fav();
	return i < st.fav_count ? fav_str(FAV_TABLE_PS[i]) : "";
}

int userdata_fav_has(const char *path)
{
	int found;
	ensure_fav();
	userdata_find(FAV_TABLE_PS, st.fav_count, path, fav_str, &found);
	return found;
}

int userdata_fav_set(const char *path, int on)
{
	if (!userdata_path_ok(path)) {
		tlog("userdata: can't keep %s as a favourite", path);
		return 0;
	}
	ensure_fav();
	snprintf(W->path, sizeof(W->path), "%s", path);  // path may be in PSRAM, which the read back changes
	tlog("userdata: favourite %s %s", W->path, on ? "on" : "off");
	return on ? fav_change(W->path, NULL, NULL) : fav_change(NULL, same_path, W->path);
}

// --- Both lists ------------------------------------------------------------------

int userdata_forget(const char *path)
{
	snprintf(W->path, sizeof(W->path), "%s", path);
	u32 count = recent_get();
	u32 left = userdata_recent_drop(W->u.recent, count, W->path);
	int ok = 1;
	if (left != count) {
		tlog("recent: forgot %s", W->path);
		ok = recent_put(left);
	}
	ensure_fav();
	return fav_change(NULL, same_path, W->path) && ok;
}

int userdata_forget_file(const char *dir, const char *name)
{
	return userdata_join(W->pad, dir, name) && userdata_forget(W->pad);
}

int userdata_forget_nor(const char *name)
{
	if (name)
		snprintf(W->path, sizeof(W->path), "%s", name);
	const char *arg = name ? W->path : NULL;
	u32 count = recent_get();
	for (u32 i = 0; i < count; i++)
		if (nor_game(W->u.recent[i].path, arg))
			tlog("recent: forgot %s", W->u.recent[i].path);
	u32 left = userdata_recent_drop_nor(W->u.recent, count, arg);
	int ok = left == count || recent_put(left);
	ensure_fav();
	return fav_change(NULL, nor_game, arg) && ok;
}

int userdata_rename(const char *from, const char *to)
{
	if (!userdata_path_ok(to))
		return 0;
	// Both lists read first: reading the favourites uses W->pad
	ensure_recent();
	ensure_fav();
	snprintf(W->path, sizeof(W->path), "%s", to);
	snprintf(W->pad, sizeof(W->pad), "%s", from);
	u32 count = recent_get();
	int ok = 1, in_recent = 0;
	for (u32 i = 0; i < count; i++)
		in_recent |= strcmp(W->u.recent[i].path, W->pad) == 0;
	if (in_recent)
		ok = recent_put(userdata_recent_rename(W->u.recent, count, W->pad, W->path));
	ensure_fav();
	if (fav_matches(same_path, W->pad))
		ok = fav_change(W->path, same_path, W->pad) && ok;
	return ok;
}

// --- The clock -------------------------------------------------------------------

static int bcd_ok(u8 v)
{
	return (v & 0xF) <= 9 && (v >> 4) <= 9;
}

u32 userdata_now(void)
{
	u8 t[7];
	rtc_enable();
	rtc_get(t);
	rtc_disenable();
	u8 f[6] = {t[0], (u8)(t[1] & 0x1F), (u8)(t[2] & 0x3F), (u8)(t[4] & 0x3F), (u8)(t[5] & 0x7F), (u8)(t[6] & 0x7F)};
	for (u32 i = 0; i < sizeof(f); i++)
		if (!bcd_ok(f[i]))
			return 0;
	return userdata_clock_time(userdata_unix_time(2000 + UNBCD(f[0]), UNBCD(f[1]), UNBCD(f[2]), UNBCD(f[3]),
						      UNBCD(f[4]), UNBCD(f[5])));
}

void userdata_ago_text(char *out, u32 size, u32 then)
{
	userdata_ago(out, size, then, then ? userdata_now() : 0, &gl_ago);
}

void userdata_test_paths(const char *recent, const char *fav, const char *old_recent)
{
	if (recent && !st.recent_path) {  // the user's place, until the tests end
		st.kept_boot = st.boot;
		st.kept_boot_set = st.boot_set;
		memset(&st.boot, 0, sizeof(st.boot));
		st.boot_set = 0;
	} else if (!recent && st.recent_path) {
		st.boot = st.kept_boot;
		st.boot_set = st.kept_boot_set;
	}
	st.recent_path = recent;
	st.fav_path = fav;
	st.old_path = old_recent;
	userdata_invalidate();
}
