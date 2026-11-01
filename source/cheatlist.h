// The cheat screen's list (v0.10): every cheat of a game's cheat file, in
// file order, whatever its size, each with what the screen shows of it
// and what decoding found, so the screen lists, pages, greys and groups
// the whole file without reading or decoding its cheats again.
//
// A cheat file is still read as cheatstore.h says: a small one whole, a
// larger one a window at a time (its windows are the store's; the screen
// never shows them). The list is built once from the decoded file, a
// window after another, and kept beside it on the card as a cache,
// /torii/cheats/<stem>.cdc, with the key of the file it was built from: a
// small file's lines but its enable lines (a pick changes no entry): their
// bytes and CRC32, a larger file's bytes and its index's windows.
// A cache that doesn't fit the file as read is built again. The cache
// holds no picks: they stay where they were (cheatstore.h).
//
// Once the list is up, a window the store reads takes each cheat's
// decoded state from the list (cheatlist_apply) instead of decoding it, so
// any cheat is a read away. An edit decodes the window it changed again,
// and the list follows (cheatlist_from_model).
//
// The list lives on PSRAM page 3 (CHEATLIST_PAGE, free while the menu
// runs; Diagnostics' tests use its last 64 KB), selected only within the
// calls that copy to or from it. Its headings and counts are in struct
// cheatstore (pReadCache).
//
// Decoding carries from cheat to cheat (a CodeBreaker key, GameShark
// seeds, the format detected last: cheatcode.h), so a launch used to decode
// every cheat up to its last pick. The list keeps the decoder as it stood
// before every CHEATLIST_CKPT_EVERY-th entry (a checkpoint), noted as a
// window is decoded in order (cheatlist_ckpt_note); a launch and Back to
// Torii start each run of the cheats they need from the checkpoint before
// it (cheatlist_launch_*, cheatstore_sparse_*). An edit decodes its window
// again, renewing that window's checkpoints; the ones after it are unknown
// until the list is built again (the decoder there may have changed). A
// launch trusts a checkpoint only from a cache whose key and CRCs hold, and
// checks every cheat it decodes so against the cache's entry: one that
// differs decodes the file in full instead.
//
// The cache file: a header (struct cheatlist_head), the checkpoints, the
// headings (each CHEATLIST_HEADING_BYTES: its length, then its bytes), the
// entries, then the names, every part word-aligned; the header's crc is
// the CRC32 of all that follows it, ckpt_crc that of the checkpoints alone
// (a launch reads them without the rest). A cut write leaves a file whose
// CRC fails: it is built again.
#ifndef TORII_CHEATLIST_H
#define TORII_CHEATLIST_H

#include <gba_base.h>

#include "cheatfile.h"

#define CHEATLIST_EXT ".cdc"
#define CHEATLIST_MAGIC 0x33434443u  // "CDC3": entries' patches as cheatfile_patch_units
#define CHEATLIST_PAGE 0x3000
#define CHEATLIST_MAX 16384          // entries: every window's cheats (CHEATSTORE_WINDOWS of CHEATINDEX_CHEATS)
#define CHEATLIST_HEADINGS CHEATFILE_FOLDERS  // file-wide headings (cheatN_folder); past them a cheat is in none
#define CHEATLIST_NAME_MAX 255       // a name's bytes kept; a longer one is cut on a character
#define CHEATLIST_NAMES_BYTES 0x300000u
#define CHEATLIST_HEADING_BYTES (CHEATFILE_FOLDER_MAX + 4)

// One cheat
struct cheatlist_entry {
	u32 name_at;  // its name's offset in the names (none: name_len 0)
	u32 hash;     // cheatfile_cheat's
	u32 shape;    // a window's first and last cheat's shape (cheatfile_work), else 0: variants across windows
	u16 number;   // its N
	u16 cost;
	u16 bytes;
	u16 patches;
	u16 keys;
	u16 act;
	u8 status;    // enum cheatfile_status
	u8 heading;   // its heading's id + 1, 0 none
	u8 flags;     // CHEATLIST_*
	u8 name_len;
	u8 detected;
	u8 format;
	u16 pad;      // CHEATLIST_NAMED's
};
_Static_assert(sizeof(struct cheatlist_entry) == 32, "an entry is 32 bytes");

enum {
	CHEATLIST_PICKED = 1,   // picked now
	CHEATLIST_VARIANT = 2,  // a variant of the cheat before it (cheatfile_cheat.variant, across windows too)
	CHEATLIST_OWN = 4,      // typed on the cart
	CHEATLIST_NAMED = 8,    // a run's first: pad holds the bytes their names share, and own << 8 (the screen's)
};

struct cheatlist_head {
	u32 magic;
	u32 key_size, key_crc;  // the file's (cheatstore_key)
	u32 count, n_headings, names_bytes;
	u32 crc;       // of everything after the header
	u32 ckpt_crc;  // of the checkpoints (CHEATLIST_CKPTS_OF(count) of them)
};

// The decoder before entry at - 1 (a multiple of CHEATLIST_CKPT_EVERY); at 0:
// not known
#define CHEATLIST_CKPT_EVERY 32
#define CHEATLIST_CKPTS_OF(count) (((count) + CHEATLIST_CKPT_EVERY - 1) / CHEATLIST_CKPT_EVERY)
struct cheatlist_ckpt {
	u32 at;
	struct cheat_decoder dec;
};
_Static_assert(sizeof(struct cheatlist_ckpt) % 4 == 0, "checkpoints keep the cache word-aligned");

struct cheatlist_heading {
	u8 len;
	char name[CHEATFILE_FOLDER_MAX];
};

// The list's state, in struct cheatstore
struct cheatlist {
	u32 count;         // entries
	u32 names_bytes;   // the names' bytes used (an edit adds; the cache is written compact)
	u32 key_size, key_crc;
	u8 valid;          // the entries are the file's, decoded
	u8 dirty;          // the cache on the card isn't the list: written as the screen leaves
	u8 n_headings;
	u8 building;       // being built: 1, 2 once that failed
	u8 names_full;     // a name was left out for room: the list isn't kept
	u8 on;             // the cheat screen's: a window read takes its decoded state from the list
	struct cheatlist_heading headings[CHEATLIST_HEADINGS];
};

// Entry g (or n from g) of the list, and back
void cheatlist_get(u32 g, struct cheatlist_entry *e);
void cheatlist_read(u32 g, u32 n, struct cheatlist_entry *out);
void cheatlist_put(u32 g, const struct cheatlist_entry *e);
// An entry's name into out (size bytes, ended): its bytes
u32 cheatlist_name(const struct cheatlist_entry *e, char *out, u32 size);
// Heading id + 1 h's name (NULL for 0), its bytes in *len
const char *cheatlist_heading(u32 h, u32 *len);
// 1 if entry g is a variant of entry g - 1 (both decoded OK, alike, under one heading)
int cheatlist_variant(const struct cheatlist_entry *before, const struct cheatlist_entry *e);
// Entry g's run of variants, from the first (returned) to *end
u32 cheatlist_run(u32 g, u32 *end);
// The picked entry of start to end, CHEATFILE_NONE if none is
u32 cheatlist_run_pick(u32 start, u32 end);

// Rows the screen keeps (u32 each) on the same page, and a word beside each
void cheatlist_row_put(u32 r, u32 value);
u32 cheatlist_row(u32 r);
void cheatlist_row_aux_put(u32 r, u32 value);
u32 cheatlist_row_aux(u32 r);

// The list of the file cheatstore_open read: the cache if it fits, else
// built (every window decoded, through cheatstore_progress) and the cache
// written. 1 once it is up; 0 if the file couldn't be read whole.
int cheatlist_open(void);
// Writes the cache if the list changed since: the cheat screen's last step
void cheatlist_save(void);
// The model read now (a window, or the whole small file) into entries from
// start on, which held old of them before: decoded, its picks as they are
void cheatlist_from_model(u32 start, u32 old);
// The decoder d before entry g, as a window decoded in order has it: kept as
// g's checkpoint if g is one's (the list being built, or up)
void cheatlist_ckpt_note(u32 g, const struct cheat_decoder *d);

// A launch's view of the cache of the file read now: 1 if it is that file's
// (its key and count) and its checkpoints' CRC holds, the cache then open
// until cheatlist_launch_end. The checkpoints known of entries start to
// start + n (each one's at says which) into out, at most max; how many.
// Entry g as the cache has it: 1 if read.
int cheatlist_launch_begin(void);
u32 cheatlist_launch_ckpts(u32 start, u32 n, struct cheatlist_ckpt *out, u32 max);
int cheatlist_launch_entry(u32 g, struct cheatlist_entry *e);
void cheatlist_launch_end(void);

// For the cache's key: CRC32 of a line (len bytes, its end left out) on
// from crc, and its bytes and end counted in *bytes, unless it is an enable line
u32 cheatlist_key_line(u32 crc, const char *line, u32 len, u32 *bytes);

// The model's decoded state from the entries from start on, if the list is
// up and fits it (else nothing: the caller decodes); 1 if taken
int cheatlist_apply(u32 start);
// Every entry's pick as the store has them now (cheatstore_picked)
void cheatlist_sync_picks(void);
// The list no longer fits the file (built again on the next open)
void cheatlist_drop(void);
// A build from the decoded models (cheatlist_from_model each from the
// first): the list empty first; up at the end, 1, unless it failed
void cheatlist_build_begin(void);
int cheatlist_build_end(void);

// Why A wouldn't pick entry e (cheatfile_why's reasons, the picks' budget the
// store's): for an entry of a window not read now
u32 cheatlist_why(const struct cheatlist_entry *e);

#endif
