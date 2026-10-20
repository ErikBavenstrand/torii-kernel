// The CRC name database (v0.4.1, docs/plans/v0.4-decisions.md B21): the
// No-Intro names of /torii/names.db, which `just names-update` builds on the
// user's PC from libretro-database's dats (tools/names_db.py), and the CRC
// cache, /torii/crc.txt, which says which file has which CRC. A game's CRC
// name is the database's entry for its CRC, else for its GBA game code;
// gamename_display cleans it like a file name.
//
// The first part of this file is hardware-free, and tests/host builds it:
// the database's format and lookups, and the cache's table, lines and
// identity. names_store.c ("The kernel's names" below) loads both into
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
//	codes   code_count entries: a GBA game code (header 0xAC, as a
//	        little-endian u32 of its four characters) and the name of the
//	        first game the dat lists with it, sorted by code
//	pool    the full No-Intro names, UTF-8, NUL-ended, padded with NULs to 4
//
// /torii/crc.txt, a safe file (safefile.h) of one line per file:
//
//	CRC32<TAB>CODE<TAB>SIZE<TAB>DATE<TAB>PATH
//
// CRC32 as 8 hex digits, or "-" while unknown; CODE the GBA game code (4
// characters 0-9 A-Z) or "-"; SIZE in bytes; DATE FatFs's fdate << 16 |
// ftime as 8 hex digits, or "-" where it isn't known (written on a PC: the
// next walk takes the file's); PATH from the card's root. '#' lines are
// comments. An entry counts only while its file keeps that size (and date,
// once known): the walk drops one that doesn't.
#ifndef TORII_NAMESDB_H
#define TORII_NAMESDB_H

#include <gba_base.h>

#define NAMESDB_FILE "/torii/names.db"
#define NAMESDB_MAGIC 0x42444E54u  // "TNDB"
#define NAMESDB_VERSION 1
#define NAMESDB_CONSOLES 4  // GBA, GBC, GB, NES: library.h's LIB_* order
#define NAMESDB_VERSION_LEN 16

struct namesdb_header {
	u32 magic;  // NAMESDB_MAGIC
	u16 version;  // NAMESDB_VERSION
	u16 header_bytes;  // 64
	u32 file_bytes;
	u32 body_crc;  // CRC32 of the file after the header
	char dat_version[NAMESDB_VERSION_LEN];  // NUL-padded
	u32 crc_count[NAMESDB_CONSOLES];
	u32 code_count;
	u32 pool_bytes;  // a multiple of 4, ending in NUL
	u32 reserved;  // 0
	u32 header_crc;  // CRC32 of the 60 bytes before it
};

struct namesdb_entry {
	u32 key;  // the CRC32, or the game code
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
// The name the dat gives a GBA game code, or NULL.
const char *namesdb_by_code(const u8 *db, u32 code);
// A GBA game code from the four header bytes at 0xAC, as the database keys
// it; 0 unless they are 0-9 or A-Z.
u32 namesdb_code(const u8 *bytes);

// --- The CRC cache ------------------------------------------------------------
//
// In memory (PSRAM in the kernel), a hash table of u16 record numbers by
// path over an array of records and a pool of paths, at fixed offsets from
// its base (NAMES_CACHE_OFF_*). Reads go straight to memory; writes through
// put (psram_write in the kernel, memcpy on the host), in halfwords.

#define NAMES_CACHE_MAX 6144  // records
#define NAMES_CACHE_SLOTS 8192  // hash slots, a power of 2
#define NAMES_CACHE_NONE 0xFFFFu
#define NAMES_PATH_MAX 511  // bytes, without the NUL
#define NAMES_CACHE_OFF_SLOTS 0x0u
#define NAMES_CACHE_OFF_RECS (NAMES_CACHE_OFF_SLOTS + NAMES_CACHE_SLOTS * 2u)
#define NAMES_CACHE_OFF_POOL (NAMES_CACHE_OFF_RECS + NAMES_CACHE_MAX * 24u)
#define NAMES_CACHE_POOL_BYTES 0x50000u  // 320 KB of paths
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
	u32 code;  // namesdb_code's, 0 for none
	u32 path;  // bits 0-23: pool offset; 24-31: flags
};
_Static_assert(sizeof(struct names_rec) == 24, "a cache record is 24 bytes");

typedef void (*names_put_fn)(void *dst, const void *src, u32 bytes);

struct names_cache {
	u8 *base;  // NAMES_CACHE_BYTES of memory
	names_put_fn put;
	u32 count;  // records used
	u32 pool;  // pool bytes used
	u32 ident;  // names_cache_ident
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
u32 names_cache_put(struct names_cache *c, const char *path, u32 size, u32 date, u32 crc, u32 code, u32 flags);
// Marks a record dead.
void names_cache_drop(struct names_cache *c, u32 n);
// Gives a record a date (the walk's, for one written on a PC).
void names_cache_set_date(struct names_cache *c, u32 n, u32 date);
// What a record says about its file, now that it has this size and date:
// NAMES_FIT (it holds), NAMES_FIT_DATED (it holds but has no date yet:
// set_date), or NAMES_STALE (the file changed: drop it).
enum {
	NAMES_FIT,
	NAMES_FIT_DATED,
	NAMES_STALE,
};
int names_cache_fits(const struct names_rec *r, u32 size, u32 date);
// A value that changes whenever a live record's path, CRC or code does (an
// XOR over them): the library walk hashes it, so new CRC names rebuild the
// index. Sizes and dates aren't part of it: the walk itself sets dates.
static inline u32 names_cache_ident(const struct names_cache *c)
{
	return c->ident;
}

// Parses one line of crc.txt in place (it cuts the fields with NULs): 1
// with its fields, 0 for a comment, a blank line or one that doesn't read.
struct names_line {
	const char *path;
	u32 size, date, crc, code, flags;
};
int names_parse_line(char *line, struct names_line *out);
// Writes record n's line, with its newline, into buf (size bytes); returns
// its length, 0 for a dead record or if it doesn't fit.
u32 names_format_line(const struct names_cache *c, u32 n, char *buf, u32 size);

// --- The name ----------------------------------------------------------------------

// The database's name for a cached file of console: by its CRC (a .gb in
// the GBC table and the other way round too, since the extension doesn't
// always say), else by its game code. NULL without either.
const char *names_db_name(const u8 *db, const struct names_rec *r, u32 console);

// --- The kernel's names (names_store.c) ----------------------------------------
//
// names.db and crc.txt load into PSRAM (psram.h's map) at the start of the
// library's first walk after the boot or a game, a piece per step
// (names_load_step). The walk then checks each game's cache record against
// its file (names_walk_seen): a changed file's record is dropped, a record
// written on a PC takes the file's date, and the database's name for it
// goes into the index's sort key. The walk's fingerprint includes
// names_identity, so new names rebuild the index.
//
// CRCs are computed by a job between frames, paused while the user moves,
// covers load or the library walks: for every library game without one
// (names_identify, Settings' "Identify library"), and after each boot for
// the games on Recent without one, so a game played gets its name without a
// pass of its own during the launch. Records go to crc.txt every
// NAMES_WRITE_EVERY CRCs, at the end, and before a launch.

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
// A value that changes with the names the walk would find; 0 without a
// database (so an index built without one stays valid).
u32 names_identity(void);
// For a screen: the database's name of a game (its path, size and console),
// or NULL. Points into PSRAM.
const char *names_lookup(const char *path, u32 size, u32 console);
// The dats' version names.db was made from ("2026.10.07"), or NULL.
const char *names_dat_version(void);
// Settings' "Identify library": CRCs for every library game without one.
void names_identify(void);
// 1 while Identify library runs, with the games looked at and their total.
int names_identifying(u32 *done, u32 *total);
// Each pass of the menu's loop (library_poll calls it): starts the job.
void names_poll(void);
// Before a launch fills PSRAM: stops the job and writes crc.txt if it changed.
void names_before_launch(void);

#endif
