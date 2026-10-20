// What a game is called in the game lists, and which SD entries stay out of
// sight (docs/plans/v0.4-decisions.md, Library and data; the cleaning rules
// are docs/research/v0.4/research-v04-names-art.md, "Recommendations for B4").
// Hardware-free: tests/host runs it under ASan and UBSan.
//
// A game's display name is the first that exists (gamename_display):
//   1. your own name, from /torii/names.txt (struct gamename_custom);
//   2. the CRC database's name (v0.4.1; until then the caller passes NULL);
//   3. the cleaned file name (gamename_clean), and when two games of one
//      console clean to the same name, each keeps the tags that tell it apart
//      (gamename_dedupe).
// The SD card page shows raw file names and uses none of this but the hidden
// rule (gamename_hidden).
//
// Every name is UTF-8 bytes. The rules only look at ASCII punctuation, digits
// and letters, so multi-byte characters pass through whole.
#ifndef TORII_GAMENAME_H
#define TORII_GAMENAME_H

#include <gba_base.h>

// The longest display name, in bytes: a FatFs file name (GAME_NAME_MAX in
// savename.h). Output buffers are GAMENAME_MAX + 1 bytes.
#define GAMENAME_MAX 255

// --- Cleaning ---------------------------------------------------------------

// Writes the cleaned name of path (a path or a bare file name; only the part
// after the last '/' counts) into out (GAMENAME_MAX + 1 bytes) and returns
// its length. A longer name is cut at a character boundary first.
// "Legend of Zelda, The - A Link to the Past (USA).gba" cleans to "The
// Legend of Zelda - A Link to the Past". A name that cleans to nothing
// ("(Unlicensed).gba") keeps its name without the extension.
u32 gamename_clean(char *out, const char *path);

// The same for a name that has no extension to drop (a CRC database name).
u32 gamename_clean_title(char *out, const char *name);

// 1 if two cleaned names collide: equal but for ASCII case.
int gamename_same(const char *a, const char *b);
// The order that goes with it (strcmp's sign, ASCII case folded), so the
// colliding names of a console sort next to each other.
int gamename_cmp(const char *a, const char *b);

// Appends to out the tags that tell names[self] apart, when the file names
// names[0..count) (paths) all clean to the same name. out (GAMENAME_MAX + 1
// bytes) must hold names[self]'s cleaned name (gamename_display's
// GAMENAME_CLEAN result) on entry; returns its new length, never past
// GAMENAME_MAX. Each keeps the tags that tell it apart from the others
// ("Pokemon Emerald (USA)", "Pokemon Emerald (Europe)"), each in
// parentheses whatever its brackets ("[!]" shows as "(!)"); a name with no
// tag where the other has one keeps none; a tag in the kept-tags table
// (gamename.c: Rev, Beta, Proto, ...) is preferred over an earlier
// difference. Names with the same tags add their folder's name when the
// other is in another folder and their extension when it is in the same
// one; two folders of the same name (/A/X and /B/X) stay alike. The caller
// groups the colliding names of one console (never
// across consoles: sort with gamename_cmp, then take runs where
// gamename_same) and calls this once per member; with count 1 out stays as
// it is. One call scans the tags of all count names.
u32 gamename_dedupe(char *out, const char *const *names, u32 count, u32 self);

// --- Custom names: /torii/names.txt ------------------------------------------
//
// One game per line: its path from the card's root, a tab, its name, then
// LF (CRLF and a UTF-8 BOM are read too). Lines that don't start with '/',
// have no tab, or have an empty name are ignored, and when a path appears
// twice the later line wins. Paths compare without ASCII case, as FAT does.
// gamename_custom_set writes the file back with LF: first the lines that
// aren't names ('#' comments, notes) as they were, then one line per game
// sorted by path; blank lines, the BOM and lines with an empty name go.

#define GAMENAME_FILE "/torii/names.txt"
// Lines kept; a longer file loads but can't be changed (complete stays 0).
#define GAMENAME_CUSTOM_MAX 1024

struct gamename_custom {
	const char *text;  // the file's bytes, read-only while in use (may be PSRAM)
	u32 len;
	u32 *line;  // count offsets in text of the kept lines, sorted by path
	u32 count;
	u8 complete;  // 1 when every line was kept and the file read whole
};

// Indexes the len bytes at text (which must outlive c) into c, using line
// (line_cap u32s) for the offsets; returns c->count. text may be NULL with
// len 0 (no file yet).
u32 gamename_custom_parse(struct gamename_custom *c, const char *text, u32 len, u32 *line, u32 line_cap);

// The custom name of path: its bytes (not NUL-terminated) and length, or
// NULL when it has none.
const char *gamename_custom_get(const struct gamename_custom *c, const char *path, u32 *len);

// Writes the whole names file with path's name changed into out (cap
// bytes) and returns its length, or one of the codes below. name NULL or
// blank removes path's line; control characters in name become spaces, and
// it is trimmed and cut to GAMENAME_MAX bytes. out must not overlap
// c->text, which is read while out is written.
#define GAMENAME_TOO_BIG 0xFFFFFFFFu  // the file doesn't fit in cap bytes
#define GAMENAME_INCOMPLETE 0xFFFFFFFEu  // c isn't complete: writing would lose names
#define GAMENAME_BAD_PATH 0xFFFFFFFDu  // not from the root, or holds a tab or line break
#define GAMENAME_UNCHANGED 0xFFFFFFFCu  // gamename_custom_move: nothing to move
#define GAMENAME_FAILED(len) ((len) >= GAMENAME_UNCHANGED)
u32 gamename_custom_set(const struct gamename_custom *c, const char *path, const char *name, char *out, u32 cap);

// The same for a renamed file (B18): old_path's name moves to new_path (in
// one pass, replacing any name new_path had). GAMENAME_UNCHANGED when
// old_path has no custom name: then there is nothing to write.
u32 gamename_custom_move(const struct gamename_custom *c, const char *old_path, const char *new_path, char *out,
			 u32 cap);

// How the names file reaches the card, so the safe write (B5's temporary
// file and rename) plugs in without touching the format code.
struct gamename_io {
	// Reads the whole file into buf (cap bytes): its length, 0 when it
	// doesn't exist, GAMENAME_IO_FAIL when it can't be read or is longer.
	// A length of cap counts as a failure too (the file may go on), so give
	// it a buffer larger than any names file it accepts.
	u32 (*read)(char *buf, u32 cap);
	// Replaces the file with len bytes of buf: 1 when done.
	int (*write)(const char *buf, u32 len);
};
#define GAMENAME_IO_FAIL 0xFFFFFFFFu

// Reads the file into buf (cap bytes) and parses it into c. A failed read,
// or one that fills buf, leaves c empty and not complete, so no later set
// overwrites the file.
// Returns c->count.
u32 gamename_custom_load(struct gamename_custom *c, const struct gamename_io *io, char *buf, u32 cap, u32 *line,
			 u32 line_cap);

// gamename_custom_set into out, then writes it: 1 when written. c still
// indexes the old text; load again to see the change.
int gamename_custom_store(const struct gamename_custom *c, const struct gamename_io *io, const char *path,
			  const char *name, char *out, u32 cap);

// gamename_custom_move into out, then writes it: 1 when written or when
// there was nothing to move.
int gamename_custom_store_move(const struct gamename_custom *c, const struct gamename_io *io, const char *old_path,
			       const char *new_path, char *out, u32 cap);

// --- The display name --------------------------------------------------------

enum gamename_source {
	GAMENAME_CUSTOM,  // from names.txt
	GAMENAME_CRC,  // from the CRC database (v0.4.1)
	GAMENAME_CLEAN,  // the cleaned file name: the only kind gamename_dedupe applies to
};

// Writes path's display name into out (GAMENAME_MAX + 1 bytes) and says
// where it came from. custom may be NULL (no names file); crc_name is the
// CRC database's full name for the game, or NULL (always, until v0.4.1).
enum gamename_source gamename_display(char *out, const char *path, const struct gamename_custom *custom,
				      const char *crc_name);

// --- Hidden entries -------------------------------------------------------------

// What the caller knows about an SD entry (FILINFO), as flags.
#define GAMENAME_DIR 0x1  // a folder (AM_DIR)
#define GAMENAME_ATTR_HIDDEN 0x2  // FAT's hidden or system attribute (AM_HID, AM_SYS)
#define GAMENAME_OPENABLE 0x4  // a file the kernel starts (a game extension, library.h)

// 1 if the entry name in folder dir ("/" for the root) stays out of the SD
// card page and the library: the root's system folders (/torii, SAVER, RTS,
// SYSTEM, PATCH, CHEAT, System Volume Information, without ASCII case), dot
// files and folders anywhere, entries with the hidden or system attribute,
// and files the kernel can't start. show_all (the "Show all files" setting,
// SD card page only) shows everything. Volume labels are the caller's to
// skip: they are not entries.
int gamename_hidden(const char *dir, const char *name, u32 flags, int show_all);

#endif
