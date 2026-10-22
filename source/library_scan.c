// The kernel's library index: loading, walking the card, writing the file
// (library.h, "The kernel's index"). One state machine, stepped by a job
// between frames, or in a loop at the boot and before a launch. Each step is
// one bounded piece of work: one FatFs call, one chunk of the file, or a few
// dozen comparisons of the sort.
#include <gba_interrupt.h>
#include <stdio.h>
#include <string.h>

#include "library.h"
#include "crc32.h"
#include "ez_define.h"
#include "ff.h"
#include "gamename.h"
// After ff.h, whose types it uses
#include "diskio.h"
#include "lang.h"
#include "namesdb.h"
#include "safefile.h"
#include "saveslot.h"
#include "tlog.h"
#include "engine/cover.h"
#include "engine/cycles.h"
#include "engine/frame.h"
#include "engine/jobs.h"
#include "engine/psram.h"
#include "ui/dialog.h"
#include "ui/ui.h"

extern FATFS EZcardFs;  // ezkernelnew.c
extern u8 pReadCache[];

// The index lives in two files, written in turn: a write replaces the older
// one, so a power cut while writing leaves the newer one whole (no rename:
// R2). The boot takes the newest valid one by its header's generation.
static const char *const lib_files[2] = {"/torii/library.idx", "/torii/library.bak"};
#define LIB_DIR "/torii"
#define LIB_IO_BYTES 2048  // a chunk of the file per step
#define LIB_PARSE_WORK 16  // names.txt's lines found, compared or moved per step
#define LIB_STEP_CYCLES 30000  // one job call's work, about a ninth of a frame
#define LIB_SORT_BUDGET 48  // merged entries per step
#define LIB_DEPTH 8  // folders deeper than this are skipped
#define LIB_FIXUP_WORK 128  // records a step of the beside-ROM saves' fix-up marks
#define LIB_SAVES_SLOTS 8192  // the save names' hash table, 32 KB of PSRAM
#define LIB_SAVES_MAX (LIB_SAVES_SLOTS * 3 / 4)
#define LIB_NAMES_MAX \
	PSRAM_NAMES_TEXT_BYTES  // the most of /torii/names.txt the walk reads: what Rename reads too (116 KB)
#define LIB_PROGRESS_FRAMES 30  // a first scan shows its dialog after this
#define LIB_KEY_REST 30  // the job waits for the keys to rest this many frames
#define LIB_WRITE_IDLE 120  // a background write waits for the keys to rest this long
#define LIB_KEEP_FREE 0x100000u  // the index is written only if the card keeps 1 MB free after it
#define LIB_CYCLES_PER_MS 16780u

// The walk's scratch in PSRAM: the save table, the sort's second buffer, and
// a copy of names.txt (gamename_custom reads it there)
#define LIB_SAVES ((u32 *)PSRAM_LIBRARY_SCRATCH)
#define LIB_SORT_TMP ((u8 *)PSRAM_LIBRARY_SCRATCH + LIB_SAVES_SLOTS * 4u)
#define LIB_NAMES_TEXT ((u8 *)PSRAM_LIBRARY_SCRATCH + LIB_SAVES_SLOTS * 4u + LIBRARY_GAMES_MAX * 2u)

_Static_assert(LIB_SAVES_SLOTS * 4u + LIBRARY_GAMES_MAX * 2u + LIB_NAMES_MAX <= PSRAM_LIBRARY_SCRATCH_BYTES,
	       "the library's scratch");
_Static_assert(PSRAM_LIBRARY_SCRATCH + PSRAM_LIBRARY_SCRATCH_BYTES <= PSRAM_LIBRARY_BASE,
	       "the scratch reaches the slots");
_Static_assert(PSRAM_LIBRARY_BASE + 2 * LIB_SLOT_BYTES <= PSRAM_LIBRARY_END, "the library slots reach the user data");
_Static_assert(LIB_IO_BYTES >= LIBRARY_GAMES_MAX / 8, "lib.buf holds lib_body_ok's bitmap");
_Static_assert(sizeof(struct lib_build) <= 0x400, "READCACHE_LIBRARY holds a lib_build");

enum {
	S_IDLE,  // nothing under way: the scheduler picks the next thing (a write, a walk)
	S_LOAD,
	S_WALK,
	S_SORT,
	S_WRITE,
	S_LOST,  // PSRAM was overwritten: library_poll loads the file again
};

enum {
	L_PROBE,
	L_OPEN,
	L_HEADER,
	L_BODY,
	L_END,
};

enum {
	W_START,
	W_NAMESDB,
	W_CLEAR,
	W_NAMES,
	W_NAMES_PARSE,
	W_SAVER_OPEN,
	W_SAVER,
	W_ROOT,
	W_TREE,
	W_FIXUP,
	W_END,
};

enum {
	X_START,
	X_BODY,
	X_END,
};

static struct {
	u8 state, phase;
	u8 job;
	u8 valid;  // the active slot holds an index
	u8 active;  // which slot
	u8 want_walk;
	u8 want_write;  // the active index isn't on the card yet
	u16 names_applied;  // library_name_found's names since library_names_applied
	u8 cant_keep;  // the index file can't be made this boot: no write is tried
	u8 force;  // the next walk rebuilds whatever the hash says (Rescan)
	u8 build;  // this walk builds an index (else it only hashes)
	u8 file_open;  // io.fil
	u8 write_dirty;  // the index changed while being written: start over
	u8 newest;  // the file holding the newest valid index, or 0xFF for none
	u8 loading;  // the file being loaded
	u8 tried;  // files the load has tried, a bit each
	u8 probed;  // files whose header is valid, a bit each
	u16 progress;  // the first scan's bar, in thousandths
	u32 file_gen[2];  // the generation of each valid file
	u32 gen;
	u32 started;  // cycles_now when the load, walk or write began
	union {
		struct {
			FIL fil;  // first: word-aligned
			struct lib_header hdr;
			struct lib_section sec[LIB_SECTIONS];
			u32 real[LIB_SECTIONS];  // bytes of data in each section, before the padding
			u32 crc, sec_i, sec_done, bytes, written;
			u8 target;  // the file being written
		} io;
		struct {
			DIR dirs[LIB_DEPTH + 1];  // the saves walk is a folder deeper (a game's)
			FILINFO fi;
			struct lib_build b;
			struct lib_sort sort;
			struct gamename_custom custom;
			u8 custom_name;  // walk_display's name was a custom one
			u32 lines[GAMENAME_CUSTOM_MAX];  // names.txt's line offsets
			u16 folder[LIB_DEPTH];  // the depth's folder number, once it has a game
			u16 path_len[LIB_DEPTH + 1];
			s8 depth;
			char path[LIBRARY_NAME_MAX + 2];
			char full[2 * LIBRARY_NAME_MAX + 2];  // a game's path, for its custom name
			u32 hash, serial, files, games, entries, folders_seen, folders_done, saves, skipped, cleared;
			u32 beside, fix_i;  // saves beside their ROMs found; the records the fix-up has done
			u32 unreadable, names_len, too_long;
			u8 names_ok;  // names.txt read whole into LIB_NAMES_TEXT
			u32 parse_started;  // when its parse began
		} walk;
	} u;
	u8 buf[LIB_IO_BYTES] ALIGN(4);
} lib EWRAM_BSS;

static u8 *slot_at(u32 i)
{
	return (u8 *)(PSRAM_LIBRARY_BASE + i * LIB_SLOT_BYTES);
}

static const u8 *active_slot(void)
{
	return slot_at(lib.active);
}

static void put(void *dst, const void *src, u32 bytes)
{
	psram_write(dst, src, bytes);
}

static u32 ms_since(u32 start)
{
	return cycles_since(start) / LIB_CYCLES_PER_MS;
}

static void changed(void)
{
	lib.gen++;
	frame_wake();
}

// --- Loading ------------------------------------------------------------------

static void close_file(void)
{
	if (lib.file_open) {
		f_close(&lib.u.io.fil);
		lib.file_open = 0;
	}
}

// The other file's turn, if it is valid and untried; else no index
static void load_failed(const char *why)
{
	close_file();
	tlog("library: %s not loaded (%s)", lib_files[lib.loading], why);
	if (lib.newest == lib.loading)
		lib.newest = 0xFF;
	u32 other = !lib.loading;
	if ((lib.probed >> other & 1) && !(lib.tried >> other & 1)) {
		lib.loading = (u8)other;
		lib.phase = L_OPEN;
		return;
	}
	lib.state = S_IDLE;
}

// Reads file i's header into lib.u.io.hdr: 1 if it is one this kernel
// reads and the file has its size
static int probe(u32 i)
{
	UINT got = 0;
	if (f_open(&lib.u.io.fil, lib_files[i], FA_READ) != FR_OK)
		return 0;
	int ok = f_read(&lib.u.io.fil, lib.buf, sizeof(struct lib_header), &got) == FR_OK &&
		 got == sizeof(struct lib_header);
	if (ok) {
		memcpy(&lib.u.io.hdr, lib.buf, sizeof(struct lib_header));
		ok = lib_header_ok(&lib.u.io.hdr) && f_size(&lib.u.io.fil) == lib_file_bytes(&lib.u.io.hdr);
	}
	f_close(&lib.u.io.fil);
	return ok;
}

static void load_step(void)
{
	struct lib_header *h = &lib.u.io.hdr;
	UINT got = 0;
	switch (lib.phase) {
	case L_PROBE:
		// Both headers: the newer valid file goes first
		lib.started = cycles_now();
		lib.probed = lib.tried = 0;
		lib.newest = 0xFF;
		for (u32 i = 0; i < 2; i++) {
			if (!probe(i))
				continue;
			lib.probed |= (u8)(1 << i);
			lib.file_gen[i] = h->generation;
			if (lib.newest == 0xFF || h->generation > lib.file_gen[lib.newest])
				lib.newest = (u8)i;
		}
		if (lib.newest == 0xFF) {
			tlog("library: no index loaded (none on the card, or another version)");
			lib.state = S_IDLE;
			return;
		}
		lib.loading = lib.newest;
		lib.phase = L_OPEN;
		return;
	case L_OPEN:
		lib.tried |= (u8)(1 << lib.loading);
		if (f_open(&lib.u.io.fil, lib_files[lib.loading], FA_READ) != FR_OK) {
			load_failed("can't open it");
			return;
		}
		lib.file_open = 1;
		lib.phase = L_HEADER;
		return;
	case L_HEADER:
		if (f_read(&lib.u.io.fil, lib.buf, sizeof(*h), &got) != FR_OK || got != sizeof(*h)) {
			load_failed("a short header");
			return;
		}
		memcpy(h, lib.buf, sizeof(*h));
		if (!lib_header_ok(h) || f_size(&lib.u.io.fil) != lib_file_bytes(h)) {
			load_failed("another version, or damaged");
			return;
		}
		lib_sections(h, lib.u.io.sec);
		lib.u.io.crc = 0;
		lib.u.io.sec_i = lib.u.io.sec_done = 0;
		lib.phase = L_BODY;
		return;
	case L_BODY: {
		if (lib.u.io.sec_i == LIB_SECTIONS) {
			lib.phase = L_END;
			return;
		}
		const struct lib_section *s = &lib.u.io.sec[lib.u.io.sec_i];
		u32 n = s->bytes - lib.u.io.sec_done;
		if (n > LIB_IO_BYTES)
			n = LIB_IO_BYTES;
		if (n && (f_read(&lib.u.io.fil, lib.buf, n, &got) != FR_OK || got != n)) {
			load_failed("a short read");
			return;
		}
		lib.u.io.crc = crc32_update(lib.u.io.crc, lib.buf, n);
		if (n)
			put(slot_at(0) + s->off + lib.u.io.sec_done, lib.buf, n);
		lib.u.io.sec_done += n;
		if (lib.u.io.sec_done == s->bytes) {
			lib.u.io.sec_i++;
			lib.u.io.sec_done = 0;
		}
		return;
	}
	default:
		close_file();
		if (lib.u.io.crc != h->body_crc) {
			load_failed("a bad CRC");
			return;
		}
		put(slot_at(0) + LIB_OFF_HEADER, h, sizeof(*h));
		if (!lib_body_ok(slot_at(0), (u32 *)lib.buf)) {
			load_failed("inconsistent");
			return;
		}
		lib.newest = lib.loading;  // the file the next write leaves alone
		lib.active = 0;
		lib.valid = 1;
		lib.state = S_IDLE;
		changed();
		// Writes alternate between the files, so either may hold the newest
		tlog("library: loaded %u games (GBA %u, GBC %u, GB %u, NES %u) from %s (generation %lu) in %lu ms",
		     h->records, h->counts[LIB_GBA], h->counts[LIB_GBC], h->counts[LIB_GB], h->counts[LIB_NES],
		     lib_files[lib.loading], (unsigned long)h->generation, (unsigned long)ms_since(lib.started));
		return;
	}
}

static void load_begin(void)
{
	lib.state = S_LOAD;
	lib.phase = L_PROBE;
}

// --- Walking ------------------------------------------------------------------

// The engine adds no IRQ handler (engine/frame.h): one in ROM would run
// while an SD command hides the ROM. IntrMain runs the handlers of the
// entries before the first one with no mask (irqInit leaves libgba's dummy,
// in ROM, in every entry, with none).
static int irq_handlers_in_ram(void)
{
	for (u32 i = 0; i < MAX_INTS && IntrTable[i].mask; i++)
		if ((u32)IntrTable[i].handler >= 0x08000000u)
			return 0;
	return 1;
}

// The FAT or exFAT volume serial, from the boot sector; 0 if unreadable
static u32 volume_serial(void)
{
	FATFS *fs = &EZcardFs;
	if (!fs->fs_type || disk_read(fs->pdrv, lib.buf, fs->volbase, 1) != RES_OK)
		return 0;
	u32 at = fs->fs_type == FS_EXFAT ? 100 : fs->fs_type == FS_FAT32 ? 67 : 39;
	return lib.buf[at] | lib.buf[at + 1] << 8 | lib.buf[at + 2] << 16 | (u32)lib.buf[at + 3] << 24;
}

// A save name's hash, case folded as FAT matches names; never 0 (empty)
static u32 save_hash(const char *name)
{
	u32 h = 2166136261u;
	for (; *name; name++) {
		u8 c = (u8)*name;
		h = (h ^ (c >= 'A' && c <= 'Z' ? c + 32 : c)) * 16777619u;
	}
	return h ? h : 1;
}

static void save_insert(const char *name)
{
	if (lib.u.walk.saves >= LIB_SAVES_MAX)
		return;
	u32 h = save_hash(name);
	for (u32 i = h % LIB_SAVES_SLOTS;; i = (i + 1) % LIB_SAVES_SLOTS) {
		u32 at = LIB_SAVES[i];
		if (at == h)
			return;
		if (!at) {
			put(&LIB_SAVES[i], &h, 4);
			lib.u.walk.saves++;
			return;
		}
	}
}

static int save_known(const char *name)
{
	u32 h = save_hash(name);
	for (u32 i = h % LIB_SAVES_SLOTS;; i = (i + 1) % LIB_SAVES_SLOTS) {
		u32 at = LIB_SAVES[i];
		if (at == h)
			return 1;
		if (!at)
			return 0;
	}
}

static void hash_bytes(const void *p, u32 n)
{
	lib.u.walk.hash = crc32_update(lib.u.walk.hash, p, n);
}

// A walk entry's part of the fingerprint: kind, name, and for a game its
// size and date (a folder's date changes with nothing the index holds)
static void hash_entry(u8 kind, const FILINFO *fi, int game)
{
	u32 meta[2] = {game ? (u32)fi->fsize : 0, game ? (u32)fi->fdate << 16 | fi->ftime : 0};
	hash_bytes(&kind, 1);
	hash_bytes(fi->fname, strlen(fi->fname) + 1);
	hash_bytes(meta, sizeof(meta));
}

static void walk_begin(int build)
{
	memset(&lib.u.walk, 0, sizeof(lib.u.walk));
	lib.build = (u8)build;
	lib.state = S_WALK;
	lib.phase = W_START;
}

static void walk_failed(FRESULT fr)
{
	tlog_error("library: the walk stopped at %s (FatFs error %d); the index stays as it was",
		   lib.u.walk.path[0] ? lib.u.walk.path : "/", fr);
	names_walk_end(0);
	lib.state = S_IDLE;
}

// The folder number of the folder at depth d, added at its first game
static u16 walk_folder(void)
{
	s8 d = lib.u.walk.depth;
	if (lib.u.walk.folder[d] == LIBRARY_NONE)
		lib.u.walk.folder[d] = lib_build_folder(&lib.u.walk.b, d ? lib.u.walk.path : "/");
	return lib.u.walk.folder[d];
}

// The game just read's path into lib.u.walk.full
static void walk_full_path(void)
{
	u32 len = lib.u.walk.path_len[lib.u.walk.depth];
	char *full = lib.u.walk.full;
	memcpy(full, lib.u.walk.path, len);
	full[len] = '/';
	memcpy(full + len + 1, lib.u.walk.fi.fname, strnlen(lib.u.walk.fi.fname, LIBRARY_NAME_MAX) + 1);
}

// The name the lists show for the game just read (its path in
// lib.u.walk.full): its custom name if names.txt has one, else crc_name
// (the names database's), else its cleaned file name (gamename_display);
// custom_name says it isn't the cleaned one (never told apart by tags)
static const char *walk_display(const char *crc_name)
{
	if (!lib.u.walk.names_ok && !crc_name)
		return NULL;  // lib_build_game cleans the file name
	char *out = lib_build_display(&lib.u.walk.b);
	lib.u.walk.custom_name = gamename_display(out, lib.u.walk.full, lib.u.walk.names_ok ? &lib.u.walk.custom : NULL,
						  crc_name) != GAMENAME_CLEAN;
	return out;
}

// 1 if name is a save file: ends in .sav, any case
static int is_save_file(const char *name)
{
	u32 len = (u32)strlen(name);
	return len > 4 && name[len - 4] == '.' && (name[len - 3] | 32) == 's' && (name[len - 2] | 32) == 'a' &&
	       (name[len - 1] | 32) == 'v';
}

// A .sav file the tree walk found (the file just read, in the folder at the
// walk's depth) may be a game's save beside its ROM, <stem>.<slot>.sav: the
// table gets "<folder>/<stem>" for every dot of the name that leaves a
// ".<slot>.sav" after it (a slot's name may have dots), and the fingerprint
// the file
static void beside_note(void)
{
	const char *name = lib.u.walk.fi.fname;
	u32 len = lib.u.walk.path_len[lib.u.walk.depth], n = (u32)strlen(name);
	hash_bytes("S", 1);
	hash_bytes(lib.u.walk.path, len);
	hash_bytes(name, n + 1);
	if (!lib.build)
		return;
	char *key = lib.u.walk.full;
	memcpy(key, lib.u.walk.path, len);
	key[len] = '/';
	for (u32 j = 1; j + 6 <= n; j++) {
		if (name[j] != '.')
			continue;
		memcpy(key + len + 1, name, j);
		key[len + 1 + j] = 0;
		save_insert(key);
		lib.u.walk.beside++;
	}
}

// One entry of the folder at the walk's depth; sub, when the entry is a
// folder, is already open on it (f_readdir_sub)
static void walk_entry(DIR *sub)
{
	FILINFO *fi = &lib.u.walk.fi;
	s8 d = lib.u.walk.depth;
	lib.u.walk.entries++;
	if (fi->fattrib & 0x08)
		return;  // a volume label
	// (not a game, so the rule below hides it: the page doesn't list it)
	if (!(fi->fattrib & (AM_DIR | AM_HID | AM_SYS)) && fi->fname[0] != '.' && is_save_file(fi->fname))
		beside_note();
	u32 flags = ((fi->fattrib & AM_DIR) ? GAMENAME_DIR : 0) |
		    ((fi->fattrib & (AM_HID | AM_SYS)) ? GAMENAME_ATTR_HIDDEN : 0) |
		    (library_console_of(fi->fname) != LIB_CONSOLE_NONE ? GAMENAME_OPENABLE : 0);
	if (gamename_hidden(d ? lib.u.walk.path : "/", fi->fname, flags, 0))
		return;  // the SD card page's rule: files it hides (not games) don't count either
	if (fi->fattrib & AM_DIR) {
		hash_entry('D', fi, 0);
		u32 len = lib.u.walk.path_len[d], name = (u32)strlen(fi->fname);
		if (!sub || !sub->obj.fs) {
			lib.u.walk.skipped++;  // too deep, or it didn't open
			return;
		}
		if (len + 1 + name > LIBRARY_NAME_MAX) {
			lib.u.walk.too_long++;
			return;
		}
		char *path = lib.u.walk.path;
		path[len] = '/';
		memcpy(path + len + 1, fi->fname, name + 1);
		lib.u.walk.depth = (s8)(d + 1);
		lib.u.walk.path_len[d + 1] = (u16)(len + 1 + name);
		lib.u.walk.folder[d + 1] = LIBRARY_NONE;
		lib.u.walk.folders_seen++;
		return;
	}
	lib.u.walk.files++;
	u32 console = library_console_of(fi->fname);
	if (console == LIB_CONSOLE_NONE)
		return;
	lib.u.walk.games++;
	hash_entry('F', fi, 1);
	// Its CRC cache record checked against the file, every walk
	walk_full_path();
	const char *crc_name =
		names_walk_seen(lib.u.walk.full, (u32)fi->fsize, (u32)fi->fdate << 16 | fi->ftime, console);
	if (!lib.build)
		return;
	// Its save folder under /torii/saves has a .sav (a saves beside its ROM
	// are marked after the walk)
	int has_save = lib.u.walk.saves && save_known(lib.u.walk.full);
	u16 folder = walk_folder();
	// The display name only for a game that has room: a custom name costs a
	// search of names.txt
	int room = lib.u.walk.b.records < LIBRARY_GAMES_MAX && folder != LIBRARY_NONE;
	lib.u.walk.custom_name = 0;
	const char *display = room ? walk_display(crc_name) : NULL;
	u32 rec_flags = (has_save ? LIB_F_SAVE : 0) | (lib.u.walk.custom_name ? LIB_F_CUSTOM : 0);
	if (lib_build_game(&lib.u.walk.b, folder, fi->fname, display, (u32)fi->fsize, console, rec_flags) ==
	    LIB_BAD_NAME) {
		lib.u.walk.too_long++;
		tlog("library: %s/%s: the name is too long to index", d ? lib.u.walk.path : "", fi->fname);
	}
}

// The folder at the walk's depth is done (or failed): back to its parent
static void walk_pop(DIR *dir)
{
	f_closedir(dir);
	// A mark in the hash, so a game moved to the next folder changes it
	hash_bytes("\x02", 1);
	lib.u.walk.folders_done++;
	if (lib.u.walk.depth == 0) {
		// Saves beside their ROMs can come after the ROM in a folder's order:
		// the games they belong to are marked now
		lib.phase = lib.build && lib.u.walk.beside ? W_FIXUP : W_END;
		return;
	}
	lib.u.walk.depth--;
	lib.u.walk.path[lib.u.walk.path_len[lib.u.walk.depth]] = 0;
}

static int same_as_active(void)
{
	const struct lib_header *h = lib_hdr(active_slot());
	return lib.valid && !lib.force && h->walk_hash == lib.u.walk.hash && h->volume_serial == lib.u.walk.serial;
}

static void walk_end(void)
{
	// The names the database gives: when they change, the index sorts again
	u32 names = names_identity();
	if (names)
		hash_bytes(&names, sizeof(names));
	// A walk that couldn't open a folder isn't whole: its games' CRC
	// records stay in crc.txt
	names_walk_end(!lib.u.walk.skipped && !lib.u.walk.unreadable);
	u32 ms = ms_since(lib.started);
	tlog("library: %s walk: %lu entries, %lu folders, %lu files, %lu games, %lu saves in %lu ms; %lu folders "
	     "not opened, %lu unreadable, %lu names too long",
	     lib.build ? "building" : "checking", (unsigned long)lib.u.walk.entries,
	     (unsigned long)lib.u.walk.folders_seen, (unsigned long)lib.u.walk.files, (unsigned long)lib.u.walk.games,
	     (unsigned long)lib.u.walk.saves, (unsigned long)ms, (unsigned long)lib.u.walk.skipped,
	     (unsigned long)lib.u.walk.unreadable, (unsigned long)lib.u.walk.too_long);
	if (same_as_active()) {
		tlog("library: unchanged (hash %08lX)", (unsigned long)lib.u.walk.hash);
		lib.state = S_IDLE;
		return;
	}
	if (!lib.build) {
		tlog("library: the card changed: building a new index");
		walk_begin(1);
		return;
	}
	struct lib_header h;
	lib_build_header(&lib.u.walk.b, &h, lib.u.walk.hash, lib.u.walk.serial, lib.u.walk.files);
	if (h.flags & LIB_H_TRUNCATED)
		tlog("library: the index holds %u of the card's %lu games", h.records, (unsigned long)h.games_seen);
	lib_sort_begin(&lib.u.walk.sort, lib.u.walk.b.slot, LIB_SORT_TMP, h.records, put);
	lib.state = S_SORT;
}

// Appends n bytes of names.txt's text (staged in lib.buf) to its PSRAM copy,
// keeping an odd last byte staged: psram_write takes halfwords
static u32 names_flush(u32 staged)
{
	u32 even = staged & ~1u;
	if (lib.u.walk.names_len + even > LIB_NAMES_MAX) {
		lib.u.walk.names_ok = 0;
		return 0;
	}
	put(LIB_NAMES_TEXT + lib.u.walk.names_len, lib.buf, even);
	lib.u.walk.names_len += even;
	lib.buf[0] = lib.buf[even];
	return staged - even;
}

// names.txt, through B5's safe file (the copy that holds), in one step: the
// module has one FIL, which the menu's own lists may use between steps. The
// lists sort by the name shown (R3), so its lines are part of the hash, and
// a building walk copies them to PSRAM for gamename_custom.
static void names_step(void)
{
	static u8 told;  // the walk says once a boot that names.txt is too large
	if (safefile_open(GAMENAME_FILE, LIB_NAMES_MAX) == SAFEFILE_NONE) {
		if (!told && safefile_failed() && f_stat(GAMENAME_FILE, &lib.u.walk.fi) == FR_OK &&
		    lib.u.walk.fi.fsize > LIB_NAMES_MAX) {
			tlog_error("library: %s is %lu bytes, over the %u the menu reads: its names aren't shown",
				   GAMENAME_FILE, (unsigned long)lib.u.walk.fi.fsize, (unsigned)LIB_NAMES_MAX);
			told = 1;
		}
		return;
	}
	char *line = (char *)lib.buf + LIB_IO_BYTES / 2;
	u32 staged = 0;
	lib.u.walk.names_ok = lib.build;
	for (;;) {
		int n = safefile_line(line, LIB_IO_BYTES / 2 - 2);  // with its '\n' and a staged byte, it fits
		if (n == SAFEFILE_END)
			break;
		if (n == SAFEFILE_LONG)
			continue;
		line[n++] = '\n';
		hash_bytes(line, (u32)n);
		if (!lib.u.walk.names_ok)
			continue;
		if (staged + (u32)n > LIB_IO_BYTES / 2)
			staged = names_flush(staged);
		memcpy(lib.buf + staged, line, (u32)n);
		staged += (u32)n;
	}
	if (safefile_failed())
		lib.u.walk.names_ok = 0;
	safefile_close();
	if (!lib.u.walk.names_ok)
		return;
	lib.buf[staged++] = 0;  // an even end; the parser stops at names_len
	u32 tail = staged & 1;
	lib.buf[staged] = 0;
	names_flush(staged + tail);
	lib.u.walk.names_len -= 1 + tail;
	// Parsed in steps (W_NAMES_PARSE): it sorts the lines unless the file
	// is sorted, as the kernel writes it
	lib.u.walk.parse_started = cycles_now();
	gamename_custom_begin(&lib.u.walk.custom, (const char *)LIB_NAMES_TEXT, lib.u.walk.names_len, lib.u.walk.lines,
			      GAMENAME_CUSTOM_MAX);
}

static void walk_step(void)
{
	FRESULT fr;
	DIR *dir = &lib.u.walk.dirs[lib.u.walk.depth > 0 ? lib.u.walk.depth : 0];
	switch (lib.phase) {
	case W_START:
		lib.started = cycles_now();
		TLOG_ASSERT(irq_handlers_in_ram());
		lib.u.walk.serial = volume_serial();
		if (lib.build) {
			lib_build_begin(&lib.u.walk.b, slot_at(lib.valid ? !lib.active : 0), put);
		}
		names_walk_begin();
		lib.phase = W_NAMESDB;
		return;
	case W_NAMESDB:
		// names.db and the CRC cache, once after the boot or a game
		if (names_load_step())
			lib.phase = lib.build ? W_CLEAR : W_NAMES;
		return;
	case W_CLEAR:
		// The save table emptied a chunk a step (32 KB at the slow WAITCNT
		// would take half a frame)
		memset(lib.buf, 0, sizeof(lib.buf));
		put((u8 *)LIB_SAVES + lib.u.walk.cleared, lib.buf, LIB_IO_BYTES);
		lib.u.walk.cleared += LIB_IO_BYTES;
		if (lib.u.walk.cleared >= LIB_SAVES_SLOTS * 4u)
			lib.phase = W_NAMES;
		return;
	case W_NAMES:
		names_step();
		lib.phase = lib.u.walk.names_ok ? W_NAMES_PARSE : W_SAVER_OPEN;
		return;
	case W_NAMES_PARSE:
		// A few lines a step: a large names.txt took a step of seconds
		if (!gamename_custom_step(&lib.u.walk.custom, LIB_PARSE_WORK))
			return;
		tlog("library: %lu custom names parsed in %lu ms", (unsigned long)lib.u.walk.custom.count,
		     (unsigned long)ms_since(lib.u.walk.parse_started));
		lib.u.walk.names_ok = lib.u.walk.custom.count != 0;
		lib.phase = W_SAVER_OPEN;
		return;
	case W_SAVER_OPEN:
		// /torii/saves/<rom path>/<slot>.sav: a game's folder with a .sav in
		// it marks the ROM path (the folder's, from /torii/saves) in the table
		if (f_opendir(dir, SAVESLOT_SAVES_DIR) != FR_OK) {
			lib.phase = W_ROOT;
			return;
		}
		lib.u.walk.depth = 0;
		lib.u.walk.path[0] = 0;
		lib.u.walk.path_len[0] = 0;
		lib.phase = W_SAVER;
		return;
	case W_SAVER: {
		s8 d = lib.u.walk.depth;
		if (d < 0 || d > LIB_DEPTH) {
			lib.phase = W_ROOT;  // (never)
			return;
		}
		DIR *sub = d < LIB_DEPTH ? &lib.u.walk.dirs[d + 1] : NULL;
		fr = f_readdir_sub(dir, &lib.u.walk.fi, sub);
		if (fr != FR_OK) {
			// The saves left out only lose their marks: the games are found
			// all the same, and the fingerprint says the read failed
			tlog_error("library: %s%s can't be read (FatFs error %d)", SAVESLOT_SAVES_DIR, lib.u.walk.path,
				   fr);
			hash_bytes("\x04", 1);
			for (s8 i = d; i >= 0; i--)
				f_closedir(&lib.u.walk.dirs[i]);
			lib.phase = W_ROOT;
			return;
		}
		FILINFO *fi = &lib.u.walk.fi;
		if (!fi->fname[0]) {
			f_closedir(dir);
			if (d == 0) {
				lib.phase = W_ROOT;
				return;
			}
			lib.u.walk.depth--;
			lib.u.walk.path[lib.u.walk.path_len[lib.u.walk.depth]] = 0;
			return;
		}
		if (fi->fattrib & 0x08)
			return;
		if (fi->fattrib & AM_DIR) {
			u32 len = lib.u.walk.path_len[d], name = (u32)strlen(fi->fname);
			if (!sub || !sub->obj.fs || len + 1 + name > LIBRARY_NAME_MAX)
				return;  // too deep: a game is never so deep
			lib.u.walk.path[len] = '/';
			memcpy(lib.u.walk.path + len + 1, fi->fname, name + 1);
			lib.u.walk.path_len[d + 1] = (u16)(len + 1 + name);
			lib.u.walk.depth = (s8)(d + 1);
			return;
		}
		if (d == 0 || !is_save_file(fi->fname))
			return;
		hash_bytes(lib.u.walk.path, lib.u.walk.path_len[d]);
		hash_bytes(fi->fname, strlen(fi->fname) + 1);
		if (lib.build)
			save_insert(lib.u.walk.path);
		lib.u.walk.saves++;
		return;
	}
	case W_ROOT:
		fr = f_opendir(&lib.u.walk.dirs[0], "/");
		if (fr != FR_OK) {
			walk_failed(fr);
			return;
		}
		lib.u.walk.depth = 0;
		lib.u.walk.path[0] = 0;
		lib.u.walk.folder[0] = LIBRARY_NONE;
		lib.u.walk.folders_seen = 1;
		lib.phase = W_TREE;
		return;
	case W_TREE: {
		s8 d = lib.u.walk.depth;
		DIR *sub = d + 1 < LIB_DEPTH ? &lib.u.walk.dirs[d + 1] : NULL;
		fr = f_readdir_sub(dir, &lib.u.walk.fi, sub);
		if (fr != FR_OK) {
			// A damaged folder (FR_INT_ERR) loses the rest of its entries and
			// the walk goes on. Anything else (the card not answering, the
			// root failing) ends the walk: the index stays as it was rather
			// than lose the games of a folder a glitch couldn't read
			if (fr != FR_INT_ERR || d == 0) {
				walk_failed(fr);
				return;
			}
			tlog_error("library: %s can't be read (FatFs error %d); its games are left out",
				   lib.u.walk.path, fr);
			lib.u.walk.unreadable++;
			hash_bytes("\x03", 1);
			walk_pop(dir);
			return;
		}
		if (lib.u.walk.fi.fname[0])
			walk_entry(sub);
		else
			walk_pop(dir);
		return;
	}
	case W_FIXUP: {
		// Marks the games with a save beside them: their path less the
		// extension is "<folder>/<stem>", which the .sav files put in the table
		const u8 *slot = lib.u.walk.b.slot;
		u32 end = lib.u.walk.fix_i + LIB_FIXUP_WORK;
		if (end > lib.u.walk.b.records)
			end = lib.u.walk.b.records;
		for (u32 i = lib.u.walk.fix_i; i < end; i++) {
			u16 flags = lib_rec_at(slot, i)->flags;
			char *path = lib.u.walk.full;
			if ((flags & LIB_F_SAVE) || !lib_path(slot, i, path, sizeof(lib.u.walk.full)))
				continue;
			char *dot = strrchr(path, '.');
			if (!dot || dot < strrchr(path, '/'))
				continue;
			*dot = 0;
			if (save_known(path)) {
				flags |= LIB_F_SAVE;
				put((u8 *)slot + LIB_OFF_RECORDS + i * sizeof(struct lib_rec) + 2, &flags, 2);
			}
		}
		lib.u.walk.fix_i = end;
		if (end >= lib.u.walk.b.records)
			lib.phase = W_END;
		return;
	}
	default:
		walk_end();
		return;
	}
}

static void sort_step(void)
{
	if (!lib_sort_step(&lib.u.walk.sort, LIB_SORT_BUDGET))
		return;
	const struct lib_header *h = lib_hdr(lib.u.walk.b.slot);
	lib.active = lib.u.walk.b.slot == slot_at(1);
	lib.valid = 1;
	lib.force = 0;
	lib.want_write = !lib.cant_keep;
	lib.state = S_IDLE;
	changed();
	tlog("library: new index: %u games (GBA %u, GBC %u, GB %u, NES %u), %lu files, built in %lu ms", h->records,
	     h->counts[LIB_GBA], h->counts[LIB_GBC], h->counts[LIB_GB], h->counts[LIB_NES],
	     (unsigned long)h->total_files, (unsigned long)ms_since(lib.started));
}

// --- Writing ------------------------------------------------------------------

// A write that failed leaves no half file to take the card's room: the boot
// would reject it anyway (the other file, if valid, stays)
static void write_failed(const char *what, FRESULT fr)
{
	int opened = lib.file_open;
	const char *file = lib_files[lib.u.io.target];
	close_file();
	if (opened)
		f_unlink(file);
	if (fr == FR_DENIED && opened) {
		tlog("library: the card is full; %s isn't written", file);
	} else if (!opened && (fr == FR_NO_PATH || fr == FR_EXIST || fr == FR_DENIED || fr == FR_WRITE_PROTECTED ||
			       fr == FR_INVALID_NAME)) {
		// It can't be made (/torii a file, a write-protected card): the index
		// stays in RAM, and isn't tried again this boot
		FRESULT st = f_stat(LIB_DIR, &lib.u.walk.fi);  // the write is over: io is free
		int not_dir = st == FR_OK && !(lib.u.walk.fi.fattrib & AM_DIR);
		tlog_error("library: can't make %s (FatFs error %d%s): the index is kept in RAM, and the next boot "
			   "scans the card again",
			   file, fr, not_dir ? "; " LIB_DIR " is a file" : "");
		lib.cant_keep = 1;
	} else {
		tlog_error("library: can't write %s: %s (FatFs error %d)", file, what, fr);
	}
	lib.want_write = 0;
	lib.state = S_IDLE;
}

static void write_begin(void)
{
	lib.state = S_WRITE;
	lib.phase = X_START;
}

static void write_step(void)
{
	UINT done = 0;
	FRESULT fr;
	if (lib.write_dirty) {
		// The index changed under the write: start it over (the file is
		// opened again, emptied)
		close_file();
		lib.write_dirty = 0;
		lib.phase = X_START;
	}
	switch (lib.phase) {
	case X_START: {
		lib.started = cycles_now();
		struct lib_header *h = &lib.u.io.hdr;
		*h = *lib_hdr(active_slot());
		// The file the newest valid index isn't in, one generation on
		u32 newest = lib.newest;
		lib.u.io.target = (u8)(newest == 0xFF ? 0 : !newest);
		h->generation = newest == 0xFF ? 1 : lib.file_gen[newest] + 1;
		lib_sections(h, lib.u.io.sec);
		for (u32 i = 0; i < LIB_SECTIONS; i++)
			lib.u.io.real[i] = lib.u.io.sec[i].bytes;
		lib.u.io.real[1] = 0;
		for (u32 i = 0; i < LIBRARY_CONSOLE_SLOTS; i++)
			lib.u.io.real[1] += h->counts[i] * 2u;
		lib.u.io.real[3] = h->pool_bytes;
		lib.u.io.bytes = lib_file_bytes(h);
		// The index is a cache: a card nearly full keeps its room for saves.
		// Only when FatFs knows the free space already: counting it can read
		// the whole FAT, far too long for a step
		const FATFS *fs = &EZcardFs;
		if (fs->free_clst <= fs->n_fatent - 2 &&
		    (unsigned long long)fs->free_clst * fs->csize * 512u < lib.u.io.bytes + LIB_KEEP_FREE) {
			tlog("library: the card is nearly full; the index isn't written");
			lib.want_write = 0;
			lib.state = S_IDLE;
			return;
		}
		const char *file = lib_files[lib.u.io.target];
		fr = f_open(&lib.u.io.fil, file, FA_WRITE | FA_CREATE_ALWAYS);
		if (fr == FR_NO_PATH) {
			f_mkdir(LIB_DIR);
			fr = f_open(&lib.u.io.fil, file, FA_WRITE | FA_CREATE_ALWAYS);
		}
		if (fr != FR_OK) {
			write_failed("open", fr);
			return;
		}
		lib.file_open = 1;
		// A blank header until the body is written: a write cut short
		// leaves a file the boot rejects
		memset(lib.buf, 0, sizeof(struct lib_header));
		fr = f_write(&lib.u.io.fil, lib.buf, sizeof(struct lib_header), &done);
		if (fr != FR_OK || done != sizeof(struct lib_header)) {
			write_failed("the header", fr);
			return;
		}
		lib.u.io.crc = 0;
		lib.u.io.sec_i = lib.u.io.sec_done = 0;
		lib.u.io.written = sizeof(struct lib_header);
		lib.phase = X_BODY;
		return;
	}
	case X_BODY: {
		if (lib.u.io.sec_i == LIB_SECTIONS) {
			lib.phase = X_END;
			return;
		}
		u32 i = lib.u.io.sec_i, at = lib.u.io.sec_done;
		const struct lib_section *s = &lib.u.io.sec[i];
		u32 n = s->bytes - at;
		if (n > LIB_IO_BYTES)
			n = LIB_IO_BYTES;
		memcpy(lib.buf, active_slot() + s->off + at, n);
		// The padding is zeros, whatever the slot holds past the data
		if (at + n > lib.u.io.real[i]) {
			u32 keep = lib.u.io.real[i] > at ? lib.u.io.real[i] - at : 0;
			memset(lib.buf + keep, 0, n - keep);
		}
		lib.u.io.crc = crc32_update(lib.u.io.crc, lib.buf, n);
		fr = f_write(&lib.u.io.fil, lib.buf, n, &done);
		if (fr != FR_OK || done != n) {
			write_failed("the body", fr != FR_OK ? fr : FR_DENIED);  // short: the card is full
			return;
		}
		lib.u.io.sec_done += n;
		lib.u.io.written += n;
		if (lib.u.io.sec_done == s->bytes) {
			lib.u.io.sec_i++;
			lib.u.io.sec_done = 0;
		}
		return;
	}
	default: {
		struct lib_header *h = &lib.u.io.hdr;
		h->body_crc = lib.u.io.crc;
		h->header_crc = lib_header_crc(h);
		memcpy(lib.buf, h, sizeof(*h));
		fr = f_lseek(&lib.u.io.fil, 0);
		if (fr == FR_OK)
			fr = f_write(&lib.u.io.fil, lib.buf, sizeof(*h), &done);
		if (fr != FR_OK || done != sizeof(*h)) {
			write_failed("the header", fr);
			return;
		}
		lib.file_open = 0;
		fr = f_close(&lib.u.io.fil);
		if (fr != FR_OK) {
			write_failed("close", fr);
			return;
		}
		// The new file is the newest now; the next write replaces the other
		lib.newest = lib.u.io.target;
		lib.file_gen[lib.newest] = h->generation;
		lib.want_write = 0;
		lib.state = S_IDLE;
		tlog("library: wrote %s (generation %lu), %lu bytes in %lu ms", lib_files[lib.newest],
		     (unsigned long)h->generation, (unsigned long)lib.u.io.bytes, (unsigned long)ms_since(lib.started));
		return;
	}
	}
}

// --- The state machine ----------------------------------------------------------

// One step; JOB_DONE when there is nothing left to do
static int library_step(void)
{
	switch (lib.state) {
	case S_IDLE:
		if (lib.want_write && lib.valid) {
			write_begin();
			return JOB_MORE;
		}
		if (lib.want_walk) {
			lib.want_walk = 0;
			walk_begin(!lib.valid || lib.force);
			return JOB_MORE;
		}
		return JOB_DONE;
	case S_LOAD:
		load_step();
		return JOB_MORE;
	case S_WALK:
		walk_step();
		return JOB_MORE;
	case S_SORT:
		sort_step();
		return JOB_MORE;
	case S_WRITE:
		write_step();
		return JOB_MORE;
	default:  // S_LOST: library_poll loads the index again
		return JOB_DONE;
	}
}

static int library_job(struct job *job)
{
	if (job->state == JOB_CANCEL)
		return JOB_DONE;  // the state stays: library_poll starts it again
	if (cover_loading())
		return JOB_WAIT;  // the covers first: they share FatFs's window
	// What the user moves keeps its frames: the job waits while keys act,
	// a screen changes or a menu or dialog is up (other motion, a marquee
	// say, runs on), and writes (a sector can take a few milliseconds) until the keys
	// have rested LIB_WRITE_IDLE frames
	const struct ui_view *top = ui_top();
	int writing = lib.state == S_WRITE || (lib.state == S_IDLE && lib.want_write && lib.valid);
	if (ui_idle_frames() < (writing ? LIB_WRITE_IDLE : LIB_KEY_REST) || ui_transitioning() ||
	    (top && (top->opening || (top->spec->flags & UI_VIEW_MODAL))))
		return JOB_WAIT;  // a menu or dialog up: another operation may be under way
	psram_begin();
	u32 start = cycles_now();
	int r;
	do
		r = library_step();
	while (r == JOB_MORE && cycles_since(start) < LIB_STEP_CYCLES);
	return r;
}

static int has_work(void)
{
	return lib.state != S_IDLE || lib.want_walk || (lib.want_write && lib.valid);
}

void library_load_now(void)
{
	if (lib.state == S_LOST) {
		load_begin();
		lib.want_walk = 1;
	}
	if (lib.state != S_LOAD || cover_loading())
		return;
	psram_begin();
	while (lib.state == S_LOAD)
		library_step();
}

void library_poll(void)
{
	if (lib.state == S_LOST) {
		load_begin();
		lib.want_walk = 1;
	}
	if (has_work() && !job_running(lib.job))
		lib.job = job_start(library_job, 0, JOB_OWNER_NONE);
	names_poll();
}

// The first scan's dialog: the share of folders walked, then the sort
static void first_scan_progress(void)
{
	char status[48];
	u32 value = 0;
	if (lib.state == S_WALK && lib.u.walk.folders_seen)
		value = lib.u.walk.folders_done * 900u / lib.u.walk.folders_seen;
	else if (lib.state == S_SORT)
		value = 900u + 100u * lib.u.walk.sort.width / (lib.u.walk.sort.n ? lib.u.walk.sort.n : 1);
	else if (lib.state == S_WRITE)
		value = 1000;
	if (value < lib.progress)
		value = lib.progress;  // folders found later never move the bar back
	lib.progress = value > 1000 ? 1000 : value;
	snprintf(status, sizeof(status), "%lu %s", (unsigned long)lib.u.walk.games, gl_library_found);
	dialog_progress_set(lib.progress, 1000, status);
}

void library_boot(void)
{
	names_boot();
	memset(&lib, 0, sizeof(lib));
	lib.job = JOB_NONE;
	lib.newest = 0xFF;
	psram_begin();
	load_begin();
	while (lib.state == S_LOAD)
		library_step();
	if (lib.valid && lib_hdr(active_slot())->volume_serial != volume_serial()) {
		// Another card's index (a card cloned or swapped): never show its games
		tlog("library: the index is another card's");
		lib.valid = 0;
	}
	if (lib.valid) {
		lib.want_walk = 1;  // the job, once the menu runs
		return;
	}
	// The first scan and its write, before the menu: a dialog once it is
	// slow. The write too: in the background it would wait for the menu to
	// rest, and a card powered off first would scan again.
	u32 frames = torii_frames;
	int shown = 0;
	walk_begin(1);
	while (lib.state == S_WALK || lib.state == S_SORT || lib.state == S_WRITE) {
		library_step();
		if (lib.state == S_IDLE && lib.want_write && lib.valid)
			write_begin();
		if (!shown && torii_frames - frames > LIB_PROGRESS_FRAMES) {
			dialog_progress_begin(gl_library_title, gl_library_scanning);
			shown = 1;
		}
		if (shown && torii_frames != frames) {
			frames = torii_frames;  // once a frame: it formats text
			first_scan_progress();
		}
	}
	if (shown)
		dialog_progress_end();
	if (!lib.valid)
		lib.want_walk = 1;  // the card failed: the menu's job tries again
}

void library_write_now(void)
{
	if (!lib.valid || !lib.want_write || lib.state != S_IDLE)
		return;
	psram_begin();
	write_begin();
	while (lib.state == S_WRITE)
		library_step();
}

void library_before_launch(void)
{
	if (lib.valid && (lib.state == S_WRITE || lib.want_write)) {
		// The new index goes to the card first: else the next boot scans
		// again. A big one takes a second or two: with its bar
		if (lib.state != S_WRITE)
			write_begin();
		psram_begin();
		dialog_progress_begin(gl_library_title, gl_library_saving);
		while (lib.state == S_WRITE) {
			library_step();
			if (lib.state == S_WRITE && lib.phase != X_START)
				dialog_progress_set(lib.u.io.written, lib.u.io.bytes, NULL);
		}
		dialog_progress_end();
	}
	if (job_running(lib.job))
		job_cancel(lib.job);
	lib.job = JOB_NONE;
	close_file();
	names_before_launch();
	lib.state = S_LOST;
	lib.valid = 0;
	lib.want_write = 0;
	lib.gen++;
}

void library_rescan(void)
{
	lib.force = 1;
	lib.want_walk = 1;
	if (lib.state == S_WALK || lib.state == S_SORT)
		lib.state = S_IDLE;  // that walk may have been only checking
}

// --- Queries ------------------------------------------------------------------

int library_ready(void)
{
	return lib.valid;
}

int library_busy(void)
{
	return has_work();
}

int library_walking(void)
{
	return lib.state == S_LOAD || lib.state == S_SORT || (lib.state == S_WALK && lib.build);
}

u32 library_generation(void)
{
	return lib.gen;
}

u32 library_count(u32 console)
{
	return lib.valid && console < LIBRARY_CONSOLE_SLOTS ? lib_hdr(active_slot())->counts[console] : 0;
}

u32 library_game(u32 console, u32 n)
{
	return lib.valid ? lib_nth(active_slot(), console, n) : LIBRARY_NONE;
}

static int rec_ok(u32 rec)
{
	return lib.valid && rec < lib_hdr(active_slot())->records;
}

u32 library_position(u32 rec)
{
	return rec_ok(rec) ? lib_position(active_slot(), rec) : LIBRARY_NONE;
}

const char *library_name(u32 rec)
{
	return rec_ok(rec) ? lib_name(active_slot(), rec) : "";
}

const char *library_display(u32 rec)
{
	return rec_ok(rec) ? lib_display(active_slot(), rec) : "";
}

const char *library_key(u32 rec)
{
	return rec_ok(rec) ? lib_key(active_slot(), rec) : "";
}

u32 library_size(u32 rec)
{
	return rec_ok(rec) ? lib_rec_at(active_slot(), rec)->size : 0;
}

u32 library_flags(u32 rec)
{
	return rec_ok(rec) ? lib_rec_at(active_slot(), rec)->flags : LIB_F_GONE;
}

u32 library_path(u32 rec, char *out, u32 size)
{
	return rec_ok(rec) ? lib_path(active_slot(), rec, out, size) : 0;
}

const u8 *library_slot(void)
{
	return lib.valid ? active_slot() : NULL;
}

u32 library_find(const char *path)
{
	return lib.valid ? lib_find(active_slot(), path) : LIBRARY_NONE;
}

void library_find_names(const char *const *names, const u32 *areas, u32 count, u16 *recs)
{
	if (lib.valid) {
		lib_find_names(active_slot(), names, areas, count, recs);
		return;
	}
	for (u32 j = 0; j < count; j++)
		recs[j] = LIBRARY_NONE;
}

u32 library_total_files(void)
{
	return lib.valid ? lib_hdr(active_slot())->total_files : 0;
}

int library_truncated(void)
{
	return lib.valid && (lib_hdr(active_slot())->flags & LIB_H_TRUNCATED);
}

u32 library_records(void)
{
	return lib.valid ? lib_hdr(active_slot())->records : 0;
}

// After the menu changed the card: a walk follows (a walk under way may
// have read the folder before the change, so it starts over), and a write
// under way starts over with the changed index
static void card_changed(void)
{
	lib.want_walk = 1;
	if (lib.state == S_WALK || lib.state == S_SORT)
		lib.state = S_IDLE;
	if (lib.state == S_WRITE)
		lib.write_dirty = 1;
}

void library_forget(const char *path)
{
	u32 rec = library_find(path);
	if (rec != LIBRARY_NONE) {
		psram_begin();
		lib_forget(slot_at(lib.active), put, rec);
		changed();
	}
	card_changed();
}

void library_changed(void)
{
	card_changed();
}

void library_named(const char *path, const char *display)
{
	u32 rec = library_find(path);
	if (rec != LIBRARY_NONE) {
		psram_begin();
		struct lib_build *scratch = (struct lib_build *)(pReadCache + READCACHE_LIBRARY);
		// Its file name from path (in RAM; the record's is in PSRAM, which the
		// rename writes)
		const char *slash = strrchr(path, '/');
		if (!lib_rename_shown(slot_at(lib.active), put, rec, slash ? slash + 1 : path, display, scratch))
			lib_forget(slot_at(lib.active), put, rec);  // the walk adds it back
		changed();
	}
	card_changed();
}

int library_name_found(const char *path, const char *crc_name)
{
	// Not while the index loads or is lost; a walk or sort building the other
	// slot goes on (the final walk of a run replaces it with the same names)
	if (!lib.valid || lib.state == S_LOAD || lib.state == S_LOST)
		return 0;
	u32 rec = library_find(path);
	if (rec == LIBRARY_NONE || (library_flags(rec) & LIB_F_CUSTOM))
		return 0;  // not indexed yet, or it has a name already (names.txt's wins)
	char display[GAMENAME_MAX + 1];
	if (gamename_display(display, path, NULL, crc_name) == GAMENAME_CLEAN)
		return 0;
	psram_begin();
	struct lib_build *scratch = (struct lib_build *)(pReadCache + READCACHE_LIBRARY);
	const char *slash = strrchr(path, '/');
	if (!lib_rename_shown(slot_at(lib.active), put, rec, slash ? slash + 1 : path, display, scratch))
		return 0;  // the pool is full: the walk after the run rebuilds it
	lib.names_applied++;
	return 1;
}

void library_names_applied(void)
{
	if (!lib.names_applied)
		return;
	tlog("library: %u database names put in the index in place", lib.names_applied);
	lib.names_applied = 0;
	if (!lib.valid)
		return;
	changed();
	// The active index changed in place: it goes to the card again
	lib.want_write = !lib.cant_keep;
	if (lib.state == S_WRITE)
		lib.write_dirty = 1;
}

void library_renamed(const char *old_path, const char *new_name)
{
	u32 rec = library_find(old_path);
	if (rec != LIBRARY_NONE) {
		psram_begin();
		struct lib_build *scratch = (struct lib_build *)(pReadCache + READCACHE_LIBRARY);
		// A custom name followed the file (gamename_store_move): it shows
		// at once, not the cleaned name until the walk. In RAM: the rename
		// writes the pool it is in
		char display[GAMENAME_MAX + 1] = "";
		int custom = (library_flags(rec) & LIB_F_CUSTOM) != 0;
		if (custom)
			snprintf(display, sizeof(display), "%s", library_display(rec));
		if (lib_rename_shown(slot_at(lib.active), put, rec, new_name, custom ? display : NULL, scratch))
			tlog_debug("library: %s is %s now, shown as %s", old_path, new_name, library_display(rec));
		else
			lib_forget(slot_at(lib.active), put, rec);  // the walk adds it back
		changed();
	}
	card_changed();
}
