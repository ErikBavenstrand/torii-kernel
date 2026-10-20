// The checks behind safe file writes (safefile.h), with no hardware, so the
// host tests run them (tests/host/test_safefile.c).
//
// A safe file is text whose last line is a CRC line, "#crc32 1a2b3c4d": the
// CRC32 (zlib's) of every byte before that line. Two copies of it are kept,
// NAME and NAME.bak, written one after the other with the same text, and a
// reader takes the first that holds up (safefile_pick). Lines that start
// with '#' are comments to every reader, so the CRC line reads as one.
#ifndef TORII_SAFEFILE_SCAN_H
#define TORII_SAFEFILE_SCAN_H

#include <gba_base.h>

#define SAFEFILE_CRC_TAG "#crc32 "
// "#crc32 " and 8 hex digits, without the newline
#define SAFEFILE_CRC_LINE_LEN (sizeof(SAFEFILE_CRC_TAG) - 1 + 8)

// What a pass over one copy found. Fill it with safefile_scan_begin, the
// file's bytes in any pieces with safefile_scan_feed, then safefile_scan_end.
struct safefile_scan {
	u32 size;        // bytes fed
	u32 crc;         // CRC32 of all of them
	u32 prefix_len;  // the prefix whose CRC32 is wanted (safefile_scan_begin)
	u32 prefix_crc;  // CRC32 of the first prefix_len bytes, once size >= prefix_len
	u32 body;        // with a CRC line: the bytes before it
	u32 body_crc;    // and their CRC32
	u32 stated;      // the CRC32 the CRC line states
	u32 line_start;  // offset of the line being read
	u32 line_crc;    // CRC32 of the bytes before it
	u32 line_len;    // its length so far
	char line[SAFEFILE_CRC_LINE_LEN + 2];  // its first bytes
	u8 crc_line;     // the last line that isn't blank is a CRC line
	u8 valid;        // and the CRC it states is the body's (safefile_scan_end)
	u8 has_nul;      // a NUL byte: not text (a write cut short can leave zeros)
};

void safefile_scan_begin(struct safefile_scan *s, u32 prefix_len);
void safefile_scan_feed(struct safefile_scan *s, const void *data, u32 bytes);
void safefile_scan_end(struct safefile_scan *s);

// The copy a reader takes, from the scans of NAME and NAME.bak (NULL for a
// copy that isn't there):
// - SAFEFILE_MAIN: NAME, whose CRC line holds;
// - SAFEFILE_HAND: NAME without a CRC line that holds, taken as edited by
//   hand: it is text, not empty, and not the first bytes of a NAME.bak that
//   holds (which is what a write of NAME cut short leaves);
// - SAFEFILE_BAK: NAME.bak, whose CRC line holds;
// - SAFEFILE_NONE: neither: the list is empty.
// Scan NAME first, then NAME.bak with prefix_len = NAME's size.
enum {
	SAFEFILE_NONE,
	SAFEFILE_MAIN,
	SAFEFILE_HAND,
	SAFEFILE_BAK
};
int safefile_pick(const struct safefile_scan *main, const struct safefile_scan *bak);

// Formats the CRC line for crc, with its newline, into out (at least
// SAFEFILE_CRC_LINE_LEN + 2 bytes); returns its length.
u32 safefile_crc_line(char *out, u32 crc);
// 1 if line (len bytes, no newline; a trailing '\r' or blanks allowed) is a
// CRC line, with its CRC in *crc.
int safefile_parse_crc_line(const char *line, u32 len, u32 *crc);

// A file's text made one line at a time, for safefile_write: line(ctx, n,
// buf, size) writes line n (from 0) with its newline into buf and returns its
// length, 0 past the last line. A line it can't fit in size bytes is cut.
// Give safefile_write safefile_lines_fill and a struct safefile_lines.
#define SAFEFILE_LINE_MAX 640
struct safefile_lines {
	u32 (*line)(void *ctx, u32 n, char *buf, u32 size);
	void *ctx;
	u32 n;    // the line in buf
	u32 len;  // its length
	u32 off;  // bytes of it already given out
	u32 pos;  // bytes of the text given out
	char buf[SAFEFILE_LINE_MAX];
};
// Fills out with up to max bytes of the text from byte pos (0 starts over;
// otherwise pos must be where the last call ended); returns how many, 0 at
// the end.
u32 safefile_lines_fill(void *lines, u32 pos, char *out, u32 max);

#endif
