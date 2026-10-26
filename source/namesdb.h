// The CRC name database (v0.4.1, docs/design/v0.4/decisions.md): the
// No-Intro names of /torii/names.db, which `just names-update` builds on the
// user's PC from libretro-database's dats (tools/names_db.py), and the CRC
// cache, /torii/crc.txt, which says which file has which CRC. A game's CRC
// name is the database's entry for its CRC, never its GBA game code, which
// a romhack shares with its base game (C4), so neither file keeps codes
// since v0.4.2: a game the dats don't list keeps its file name. gamename_display cleans the name like a file name.
//
// The first part of this file is hardware-free, and tests/host builds it:
// the database's format and lookups, and the cache's table and lines.
// names_store.c ("The kernel's names" below) loads both into
// PSRAM, computes CRCs and writes the cache.
//
// names.db, little-endian, read in place (in PSRAM, which reads like ROM):
//
//	header  64 B (struct namesdb_header): magic TNDB, version, the file's
//	        size, the dats' version ("2026.10.07"), the counts, CRC32s of
//	        the body and of the header
//	crcs    for each console in library order (GBA, GBC, GB, NES),
//	        crc_count[c] entries (struct namesdb_entry: the whole file's
//	        CRC32, NES with its iNES header as the dats count it, and the
//	        name's pool offset), sorted by CRC
//	codes   version 1 only: old_codes entries of 8 bytes, GBA game codes
//	        nothing reads (v0.4.1 wrote none); skipped
//	pool    the full No-Intro names, UTF-8, NUL-ended, padded with NULs to 4
//
// /torii/crc.txt, a safe file (safefile.h) of one line per file:
//
//	CRC32<TAB>SIZE<TAB>DATE<TAB>PATH
//
// CRC32 as 8 hex digits, or "-" while unknown; SIZE in bytes; DATE FatFs's fdate << 16 |
// ftime as 8 hex digits (`just names-update` writes the file's as the PC
// sees it, which can be whole hours off the cart's: names_cache_fits), or
// "-" where it isn't known (names-update between v0.4.1 and v0.4.2: the
// next walk takes the file's); PATH from the card's root. '#' lines are
// comments. An entry counts only while its file keeps that size (and date,
// once known): the walk drops one that doesn't. v0.4.1 wrote a GBA game code
// after the CRC (CRC32, CODE, SIZE, DATE, PATH): such lines still read, and
// their code is skipped (the fourth field starts with '/' only in the new
// form).
#ifndef TORII_NAMESDB_H
#define TORII_NAMESDB_H

#include <gba_base.h>

#define NAMESDB_FILE "/torii/names.db"
#define NAMESDB_MAGIC 0x42444E54u  // "TNDB"
#define NAMESDB_VERSION 2  // 1 (v0.4.1, with a code table) still reads
#define NAMESDB_CONSOLES 4  // GBA, GBC, GB, NES: library.h's LIB_* order
#define NAMESDB_VERSION_LEN 16

struct namesdb_header {
	u32 magic;  // NAMESDB_MAGIC
	u16 version;  // NAMESDB_VERSION
	u16 header_bytes;  // 64
	u32 file_bytes;
	u32 body_crc;  // CRC32 of the file after the header
	char dat_version[NAMESDB_VERSION_LEN];  // NUL-padded, at most 15 characters
	u32 crc_count[NAMESDB_CONSOLES];
	u32 old_codes;  // version 1's code table's entries, skipped; 0 from 2
	u32 pool_bytes;  // a multiple of 4, ending in NUL
	u32 reserved;  // 0
	u32 header_crc;  // CRC32 of the 60 bytes before it
};

struct namesdb_entry {
	u32 key;  // the CRC32
	u32 name;  // pool offset
};

_Static_assert(sizeof(struct namesdb_header) == 64, "the names.db header is 64 bytes");

u32 namesdb_header_crc(const struct namesdb_header *h);
// The file's size for h's counts; 0 if they overflow.
u32 namesdb_file_bytes(const struct namesdb_header *h);
// 1 if h is a header this kernel reads, of a file at most max bytes: magic,
// version, sizes and its CRC.
int namesdb_header_ok(const struct namesdb_header *h, u32 max);
// 1 if a loaded database's pool ends in NUL, so every name read from it
// ends within it. Its tables' order isn't checked: a bad one only misses.
int namesdb_body_ok(const u8 *db);
// The full name of the game whose CRC32 is crc in console's table, or NULL.
const char *namesdb_by_crc(const u8 *db, u32 console, u32 crc);

// --- The CRC cache ------------------------------------------------------------
//
// In memory (PSRAM in the kernel), a hash table of u16 record numbers by
// path over an array of records and a pool of paths, at fixed offsets from
// its base (NAMES_CACHE_OFF_*). Reads go straight to memory; writes through
// put (psram_write in the kernel, memcpy on the host), in halfwords.

#define NAMES_CACHE_MAX 4608  // records: the library's 4,096 games and room for more
#define NAMES_CACHE_SLOTS 8192  // hash slots, a power of 2
#define NAMES_CACHE_NONE 0xFFFFu
#define NAMES_PATH_MAX 511  // bytes, without the NUL
#define NAMES_CACHE_OFF_SLOTS 0x0u
#define NAMES_CACHE_OFF_RECS (NAMES_CACHE_OFF_SLOTS + NAMES_CACHE_SLOTS * 2u)
#define NAMES_CACHE_OFF_POOL (NAMES_CACHE_OFF_RECS + NAMES_CACHE_MAX * 20u)
#define NAMES_CACHE_POOL_BYTES 0xA0000u  // 640 KB of paths: 4,608 of 142 bytes
#define NAMES_CACHE_BYTES (NAMES_CACHE_OFF_POOL + NAMES_CACHE_POOL_BYTES)

// names_rec.flags
#define NAMES_CRC 0x1u  // the CRC is known
#define NAMES_DATE 0x2u  // the date is known
#define NAMES_DEAD 0x4u  // dropped: its file changed (a later put revives it)

struct names_rec {
	u32 hash;  // names_path_hash of its path
	u32 size;
	u32 date;  // fdate << 16 | ftime, with NAMES_DATE
	u32 crc;  // with NAMES_CRC
	u32 path;  // bits 0-23: pool offset; 24-31: flags
};
_Static_assert(sizeof(struct names_rec) == 20, "a cache record is 20 bytes");

typedef void (*names_put_fn)(void *dst, const void *src, u32 bytes);

struct names_cache {
	u8 *base;  // NAMES_CACHE_BYTES of memory
	names_put_fn put;
	u32 count;  // records used
	u32 pool;  // pool bytes used
	u8 full;  // a put found no room since the last clear
};

// Empties the table: writes the slots, NAMES_CACHE_SLOTS * 2 bytes (the
// kernel does it in pieces: names_cache_clear_part).
void names_cache_init(struct names_cache *c, u8 *base, names_put_fn put);
// Empties bytes of the slots from at: init without writing them, then this
// until it returns 1 (when all are written).
int names_cache_clear_part(struct names_cache *c, u32 at, u32 bytes);
// The FNV-1a hash of a path, ASCII case folded as FAT matches names.
u32 names_path_hash(const char *path);
// The same, and the path's length (in IWRAM: crc.txt's load hashes every line).
u32 IWRAM_CODE names_path_hash_len(const char *path, u32 *len);
// A record by number, and its path and flags.
static inline const struct names_rec *names_cache_rec(const struct names_cache *c, u32 n)
{
	return (const struct names_rec *)(c->base + NAMES_CACHE_OFF_RECS) + n;
}
static inline const char *names_cache_path(const struct names_cache *c, const struct names_rec *r)
{
	return (const char *)(c->base + NAMES_CACHE_OFF_POOL + (r->path & 0xFFFFFFu));
}
static inline u32 names_rec_flags(const struct names_rec *r)
{
	return r->path >> 24;
}
// The record of path (paths compare without ASCII case), dead or alive, or
// NAMES_CACHE_NONE.
u32 names_cache_find(const struct names_cache *c, const char *path);
// Sets path's record (adds it, or changes and revives the one there):
// flags NAMES_CRC and NAMES_DATE say which of crc and date hold. Returns
// its number, or NAMES_CACHE_NONE when the table or pool is full or path
// is empty, longer than NAMES_PATH_MAX or not from the root.
u32 names_cache_put(struct names_cache *c, const char *path, u32 size, u32 date, u32 crc, u32 flags);
// Marks a record dead.
void names_cache_drop(struct names_cache *c, u32 n);
// Gives a record a date (the walk's, for one written on a PC).
void names_cache_set_date(struct names_cache *c, u32 n, u32 date);
// What a record says about its file, now that it has this size and date:
// NAMES_FIT (it holds), NAMES_FIT_DATED (it holds but has no date of the
// cart's yet: set_date), or NAMES_STALE (the file changed: drop it). A
// date a whole number of hours off the file's, at most
// NAMES_DATE_SKEW_HOURS, with minutes and seconds equal, fits dated: it is
// names-update's, from a PC whose local time isn't the one the card's
// driver wrote (a file dated in winter reads an hour off in summer: DST;
// a PC in another zone, further). A kernel before v0.4.2 forgets it.
#define NAMES_DATE_SKEW_HOURS 14
enum {
	NAMES_FIT,
	NAMES_FIT_DATED,
	NAMES_STALE,
};
int names_cache_fits(const struct names_rec *r, u32 size, u32 date);
// Parses one line of crc.txt in place (it cuts the fields with NULs): 1
// with its fields, 0 for a comment, a blank line or one that doesn't read.
struct names_line {
	const char *path;
	u32 size, date, crc, flags;
};
int IWRAM_CODE names_parse_line(char *line, struct names_line *out);
// Writes record n's line, with its newline, into buf (size bytes); returns
// its length, 0 for a dead record or if it doesn't fit.
u32 names_format_line(const struct names_cache *c, u32 n, char *buf, u32 size);

// --- The name ----------------------------------------------------------------------

// The database's name for a cached file of console: by its CRC (a .gb in
// the GBC table and the other way round too, since the extension doesn't
// always say). NULL without one: a CRC the dats don't list is a romhack, a
// translation or a bad dump, which keeps its own name.
const char *names_db_name(const u8 *db, const struct names_rec *r, u32 console);

// --- The kernel's names (names_store.c) ----------------------------------------
//
// names.db and crc.txt load into PSRAM (psram.h's map) at the start of the
// library's first walk after the boot or a game, a piece per step
// (names_load_step). The walk then checks each game's cache record against
// its file (names_walk_seen): a changed file's record is dropped, a record
// written on a PC takes the file's date (names_cache_fits), and the
// database's name for it goes into the index's sort key. The walk's
// fingerprint includes names_identity, so new names rebuild the index.
//
// CRCs are computed by a job between frames, paused while the user moves,
// covers load or the library walks: for every library game without one
// (names_identify, Settings' "Identify library"), and after any walk that
// found games without one (new on the card, or changed): Recent's first,
// then the library's, so a game added gets its name and art without a PC
// or a launch. A CRC the database doesn't know is kept all the same, so a
// romhack is read once. Records go to crc.txt every NAMES_WRITE_EVERY CRCs,
// at the end, and before a launch; a launch stops the job, and the next
// boot's walk starts it again.

// The boot, before library_boot: forgets everything.
void names_boot(void);
// Loads a piece; 1 once names.db (if any) and crc.txt are loaded.
int names_load_step(void);
// The walk starts, and ends (whole: it saw every folder); records the walk
// didn't see aren't written back after a whole one.
void names_walk_begin(void);
void names_walk_end(int whole);
// The walk found a game of console (LIB_*): checks its record as above;
// returns the database's full name for it, or NULL.
const char *names_walk_seen(const char *path, u32 size, u32 date, u32 console);
// A value that changes with the names the walk just found (which game has
// which name; a CRC that finds none doesn't count): the walk hashes it at
// its end. 0 without a database, so an index built without one stays valid.
u32 names_identity(void);
// For a screen: the database's name of a game (its path, size and console),
// or NULL. Points into PSRAM.
const char *names_lookup(const char *path, u32 size, u32 console);
// For box art: 1 with a game's CRC32 in *crc from the CRC cache, whether or
// not names.db is there; 0 if the cache has no live record of it at that
// size with a CRC (or isn't loaded yet).
int names_crc(const char *path, u32 size, u32 *crc);
// The same for the file at path as it is on the card now, before the cache
// is loaded too (a launch at the boot): then crc.txt's line for it is read
// straight from the card through safefile, line (line_size bytes, at least
// SAFEFILE_LINE_MAX) its scratch. 0 if no line holds for the file's size
// and date.
int names_crc_now(const char *path, u32 *crc, char *line, u32 line_size);
// The dats' version names.db was made from ("2026.10.07"), or NULL.
const char *names_dat_version(void);
// Settings' "Identify library": CRCs for every library game without one.
void names_identify(void);
// Runs Identify library (or the run under way) to its end in a loop,
// without the job: for tests, and a caller that shows its own progress.
void names_identify_now(void);
// One step of the run under way, with no job and no waiting: for tests that
// stop it at a chosen point. JOB_DONE when nothing runs (or it just ended).
int names_identify_step(void);
// 1 while a game's file is being read, with the bytes read and its size.
int names_reading(u32 *read, u32 *size);
// 1 while Identify library runs, with the games looked at and their total.
int names_identifying(u32 *done, u32 *total);
// CRCs computed this session: a screen that shows box art looks again when
// it changes.
u32 names_changes(void);
// Each pass of the menu's loop (library_poll calls it): starts the job.
void names_poll(void);
// Before a launch fills PSRAM: stops the job and writes crc.txt if it changed.
void names_before_launch(void);

#endif
