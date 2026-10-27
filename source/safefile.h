// Safe writes of small text files on the SD card (docs/research/v0.4/
// research-v04-library.md, section 3; v0.4 decision R2), for every file the
// user's data lives in: /torii/recent.txt, favorites.txt, names.txt.
//
// Each file has two copies, NAME and NAME.bak, each ending in a CRC line
// (safefile_scan.h). A write writes the whole text to one copy, closes it,
// then the other, so one complete copy is on the card whenever the power
// fails. Nothing is renamed or deleted: FatFs's f_rename can leave two names
// on one cluster chain when the power fails, which exFAT can't then free.
// The copy written first is the one a reader wouldn't take now: NAME.bak,
// unless NAME is the broken one.
//
// A reader takes NAME when its CRC line holds, else NAME edited by hand (no
// CRC line that holds, but text), else NAME.bak (safefile_pick). Users edit
// these files on a PC: a file without a CRC line is read as it is, and the
// next write adds the line.
//
// One file is open at a time, through the module's own FIL; call these
// only where FatFs calls may run (not inside another FatFs operation), with
// the kernel's ROM mapped. Lines are delivered between FatFs calls, so a
// caller may copy them to PSRAM as it reads.
#ifndef TORII_SAFEFILE_H
#define TORII_SAFEFILE_H

#include <gba_base.h>

#include "safefile_scan.h"

// The longest path the module takes, ".bak" included: a game's cheat file,
// /torii/cheats/<rom path>.cht (cheatstore.h), as long as its saves' paths.
// The module keeps its paths in EWRAM, off the stack.
#define SAFEFILE_PATH_MAX 288

// Opens the copy of path to read (SAFEFILE_MAIN, _HAND or _BAK), or returns
// SAFEFILE_NONE with nothing open (no copy, or none that holds up). A copy
// larger than max bytes counts as missing: the file's caller knows how large
// its text can be, and a huge hand edit would stall the menu.
int safefile_open(const char *path, u32 max);
// The next line of the open copy into buf (size bytes), without its '\r' and
// '\n', or a UTF-8 BOM before the first; the CRC line is left out. Returns its length, SAFEFILE_END after the
// last line, or SAFEFILE_LONG for a line that doesn't fit (skipped whole).
#define SAFEFILE_END (-1)
#define SAFEFILE_LONG (-2)
int safefile_line(char *buf, u32 size);
// The rest of the open copy's text in parts, byte for byte (line ends, a
// BOM), without its CRC line: up to size bytes of what is left into buf
// (word-aligned: f_read hands whole sectors to the card's DMA), for a text
// larger than any RAM buffer. Returns how many, 0 at its end, or -1 if the
// read failed.
int safefile_read_part(void *buf, u32 size);
void safefile_close(void);
// 1 if reading the file since the last safefile_open failed (a card error,
// not a missing file or a copy that doesn't hold up): what was read may be
// short, so its caller mustn't write the file back from it.
int safefile_failed(void);
// 1 if, since the last safefile_open, a copy was left alone as larger than
// its max (safefile_failed says 1 too: nothing may write over it), so a
// caller can tell a file too big from a card error.
int safefile_too_big(void);
// 1 if a reader of path (copies up to max bytes) would take NAME.bak, so a
// caller deleting both copies deletes the main copy first
int safefile_reads_bak(const char *path, u32 max);

// Writes the text fill gives to both copies of path, making its folder if
// needed. fill(ctx, pos, buf, max) puts up to max bytes of the text from
// byte pos into buf and returns how many, 0 at the end; it is called through
// the text twice, from pos 0 each time, and must give the same text both
// times (it may read PSRAM). max is safefile_open's. A copy whose write
// fails (a full card) is left empty, so the other one is read. Nothing is
// written when reading the copies to choose the order fails. Returns 1 once
// both copies are written.
typedef u32 (*safefile_fill)(void *ctx, u32 pos, char *buf, u32 max);
int safefile_write(const char *path, u32 max, safefile_fill fill, void *ctx);
// The same, with a cutpoint (cutpoint.h) after each copy's text and after
// each copy closes, for a power-cut sweep of the file's writes
// (tests/cheats.sh); other callers' sweeps keep their counts.
int safefile_write_cut(const char *path, u32 max, safefile_fill fill, void *ctx);

#endif
