// The user's own lists on the SD card: the games played lately (Recent), the
// favourites and where the menu was left (v0.4 decisions, "Favourites and
// recents"; ticket B5).
//
// /torii/recent.txt, newest first, at most USERDATA_RECENT_MAX games:
//     @boot <item>              the carousel item last faced (only once set);
//                               older kernels' "<item> <row>" reads as item
//     <unix time><TAB><path>    one line per game; the time is 0 when the
//                               clock wasn't set (T4), and may be left out
// /torii/favorites.txt, one path per line, in byte order of the paths (the
// pages sort them by the names they show).
//
// A path is an SD file's full path ("/GBA/Advance Wars.gba") or NOR: and a
// game NOR entry's name ("NOR:Advance Wars.gba"). Lines starting with '#'
// are comments, blank lines and lines that don't parse are skipped. Both
// files are written safely (safefile.h): two copies, each ending in a CRC
// line. A file edited by hand, without that line, is read as it is.
//
// The v0.3 list, /SAVER/Recently play.txt (one path or NOR: entry per
// line), is read while recent.txt doesn't exist yet; the first write of
// recent.txt takes its games over and deletes it.
//
// This header has two halves: hardware-free parsing, list edits and the
// "played ago" text in userdata.c (host tests: tests/host/test_userdata.c),
// and the kernel's store in userdata_store.c, which keeps both lists in
// PSRAM page 0 (engine/psram.h) and reads them from the card when first
// asked, and again after a launch overwrote PSRAM.
#ifndef TORII_USERDATA_H
#define TORII_USERDATA_H

#include <gba_base.h>

#define USERDATA_RECENT_PATH "/torii/recent.txt"
#define USERDATA_FAV_PATH "/torii/favorites.txt"
#define USERDATA_OLD_RECENT_PATH "/SAVER/Recently play.txt"

// Recent keeps the newest USERDATA_RECENT_MAX games (T3)
#define USERDATA_RECENT_MAX 32
// Favourites have no cap of their own; the store holds this many
// (the library's 4,096 games) in USERDATA_FAV_POOL bytes of paths
#define USERDATA_FAV_MAX 4096

// Paths the launch path can open: a folder shorter than MAX_path_len (256)
// and a name of at most GAME_NAME_MAX (255) bytes; a NOR entry's name fits
// FM_NOR_FS's filename (100 bytes with its NUL)
#define USERDATA_DIR_MAX 256
#define USERDATA_NAME_MAX 255
#define USERDATA_NOR "NOR:"
#define USERDATA_NOR_NAME_MAX 99
#define USERDATA_PATH_MAX (USERDATA_DIR_MAX + USERDATA_NAME_MAX + 1)  // with its NUL
// A line of recent.txt: the time, a TAB, a path and its newline
#define USERDATA_LINE_MAX (USERDATA_PATH_MAX + 16)
// The largest files the store reads (a larger copy counts as missing): room
// for every game a list keeps at the longest lines, and comments
#define USERDATA_RECENT_FILE_MAX 0x10000u
#define USERDATA_FAV_FILE_MAX 0x200000u
// The boot position's item: a word of letters, digits, '-' and '_'
#define USERDATA_ITEM_MAX 16  // with its NUL

// A clock that reads earlier than this isn't set (T4): 2026-10-01 00:00:00,
// a fixed date rather than the build's, so the build stays reproducible
#define USERDATA_CLOCK_FROM 1790812800u

// --- Hardware-free (userdata.c) ---------------------------------------------

struct userdata_recent {
	u32 time;  // unix time it was started, 0 if the clock wasn't set
	char path[USERDATA_PATH_MAX];
};

struct userdata_boot {
	char item[USERDATA_ITEM_MAX];  // "" when never set
};

// 1 if path is one the lists keep: an SD path (from '/', folder and name
// within the limits above, no control characters) or a NOR entry.
int userdata_path_ok(const char *path);
// The NOR game's name in a NOR: entry, or NULL for an SD path.
const char *userdata_nor_name(const char *path);
// An SD path, in place, without empty, "." and ".." parts ("/deep/../x.gba"
// reads "/x.gba"; ".." at the root stays there). Anything not starting with
// '/', such as a NOR entry, is left as it is.
void userdata_canonical(char *path);
// dir and name joined into out (USERDATA_PATH_MAX bytes); 0 if too long.
int userdata_join(char *out, const char *dir, const char *name);
// 1 if item is a boot position's item word.
int userdata_item_ok(const char *item);

// What a line of recent.txt or the v0.3 list holds. The line is changed in
// place (blanks trimmed, the path split off).
enum {
	USERDATA_SKIP,
	USERDATA_GAME,
	USERDATA_BOOT,
	USERDATA_BAD
};
int userdata_parse_recent(char *line, u32 *time, char **path, struct userdata_boot *boot);
// A line of favorites.txt: USERDATA_GAME (with *path), _SKIP or _BAD.
int userdata_parse_fav(char *line, char **path);
// Line n of recent.txt's text for list (count games) and boot, with its
// newline, into buf; returns its length, 0 past the last line.
u32 userdata_recent_line(const struct userdata_recent *list, u32 count, const struct userdata_boot *boot, u32 n,
			 char *buf, u32 size);

// Recent's edits on count games in list; each returns the new count.
// Puts path on top with time, moving it up if it is there, dropping the
// oldest game past USERDATA_RECENT_MAX.
u32 userdata_recent_put(struct userdata_recent *list, u32 count, const char *path, u32 time);
// Drops path.
u32 userdata_recent_drop(struct userdata_recent *list, u32 count, const char *path);
// Drops the NOR game named name, or every NOR game when name is NULL.
u32 userdata_recent_drop_nor(struct userdata_recent *list, u32 count, const char *name);
// Renames from to to, keeping its place and time (dropping an entry for to).
u32 userdata_recent_rename(struct userdata_recent *list, u32 count, const char *from, const char *to);

// Favourites as handles to their paths (str gives a handle's path), kept in
// byte order of the paths. find returns where path is or would go, *found 1
// if it is there.
typedef const char *(*userdata_str)(u32 handle);
u32 userdata_find(const u32 *list, u32 count, const char *path, userdata_str str, int *found);
// 1 if list is in order with no path twice.
int userdata_in_order(const u32 *list, u32 count, userdata_str str);
// Sorts list into order and drops paths that are there twice; returns the
// new count. A heapsort: no memory of its own, n log n compares.
u32 userdata_sort(u32 *list, u32 count, userdata_str str);

// Unix time of a clock reading (year 2000-2099), 0 if a field is out of
// range; userdata_clock_time gives 0 for a reading before
// USERDATA_CLOCK_FROM (a clock that isn't set).
u32 userdata_unix_time(u32 year, u32 month, u32 day, u32 hour, u32 minute, u32 second);
u32 userdata_clock_time(u32 t);

// "Played ago" words (lang.c's gl_ago): whole texts, or the words after a
// number (minutes "m ago" makes "5m ago")
struct userdata_ago_words {
	const char *now;        // whole
	const char *minutes;    // after a number
	const char *hours;      // after a number
	const char *yesterday;  // whole
	const char *days;       // after a number
	const char *week;       // whole: one week
	const char *weeks;      // after a number
	const char *month;      // whole: one month
	const char *months;     // after a number
	const char *year;       // whole: one year
	const char *years;      // after a number
};
// How long ago then was at now, into out: "just now", "5m ago", "2h ago",
// "yesterday", "3 days", "1 week", "3 weeks", "1 month", "2 years"; empty
// when either time is 0 (the clock wasn't set) or then is after now.
void userdata_ago(char *out, u32 size, u32 then, u32 now, const struct userdata_ago_words *w);

// --- The kernel's store (userdata_store.c) ----------------------------------
//
// The lists' paths are in PSRAM: read them, never write them, and copy one
// before the next call here, which may change them. Every call reads the
// card the first time and after userdata_invalidate. Writes go to the card
// at once, and never to the kernel's NOR. They borrow pReadCache
// (READCACHE_USERDATA, ez_define.h): don't call them while holding data
// there. Calls that change a list return 1 once the card has it.

// PSRAM is about to be overwritten (a launch): read the card again next time.
void userdata_invalidate(void);
// Reads both lists now if they aren't in PSRAM, so no later call reads the
// card while a screen draws: for the boot and pages to call before they draw
// (nothing calls it yet; the v0.3 Recent list reads it before it draws).
void userdata_load(void);

u32 userdata_recent_count(void);
// Game i's path, newest first ("" past the end) and when it was played.
const char *userdata_recent_path(u32 i);
u32 userdata_recent_time(u32 i);
// path (a NOR: entry), or the file name in folder dir, goes on top of
// Recent, played now: the launch path calls these.
int userdata_recent_add(const char *path);
int userdata_recent_add_file(const char *dir, const char *name);

// path, or name in folder dir, is gone (deleted, or missing at launch): off
// both lists.
int userdata_forget(const char *path);
int userdata_forget_file(const char *dir, const char *name);
// The NOR game named name is gone, or every NOR game (name NULL, a format).
int userdata_forget_nor(const char *name);
// A file renamed from from to to: both lists follow it.
int userdata_rename(const char *from, const char *to);

// The carousel item last faced; item "" when never set.
const struct userdata_boot *userdata_boot_get(void);
// A new item (a word, userdata_item_ok): kept, and written with the next
// write of recent.txt or by userdata_flush.
void userdata_boot_set(const char *item);
int userdata_flush(void);

u32 userdata_fav_count(void);
// Favourite i's path, in byte order ("" past the end).
const char *userdata_fav_path(u32 i);
int userdata_fav_has(const char *path);
// Where path is or would go among the favourites (byte order); *found 1 if it is.
u32 userdata_fav_find(const char *path, int *found);
// Favourite i's file name hashed (userdata_name_hash), kept beside it.
u32 userdata_fav_hash(u32 i);
// The hash of path's file name (the bytes after its last '/'): FNV-1a.
// In IWRAM, for the Favourites page's hot loop.
u32 userdata_name_hash(const char *path);
// Counts each read of the favourites (every change reads them back): equal
// numbers, the same list.
u32 userdata_fav_version(void);
// Adds (on) or removes path; 0 if the card or the store is full or failed.
int userdata_fav_set(const char *path, int on);

// The clock's time now, 0 if it isn't set (USERDATA_CLOCK_FROM).
u32 userdata_now(void);
// How long ago then was, from the clock, in the menu's words (userdata_ago).
void userdata_ago_text(char *out, u32 size, u32 then);

// Tests only: use these files instead of the user's (NULL: theirs again);
// the store reads them next time.
void userdata_test_paths(const char *recent, const char *fav, const char *old_recent);

#endif
