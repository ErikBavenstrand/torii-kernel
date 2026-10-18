// Hardware-free parts of the SD log (source/tlog.c): the RAM ring buffer
// that log lines wait in until a flush, and the rules for the rotating log
// files. tests/host runs them on the host.
#ifndef TORII_LOGBUF_H
#define TORII_LOGBUF_H

#include <gba_base.h>

// Bytes of log text the ring holds between flushes (a power of two).
#define LOG_RING_SIZE 4096

// Text in the ring is head - tail bytes, ending at head; the counters only
// grow, and data[i & (LOG_RING_SIZE - 1)] holds byte i.
typedef struct {
	char data[LOG_RING_SIZE];
	u32 head;
	u32 tail;
	u32 lost;  // bytes overwritten before a flush took them
} log_ring;

// Appends n bytes. When the ring is full the oldest text goes, up to the
// end of a line, so a flush never starts in the middle of one.
void log_ring_put(log_ring *r, const char *s, u32 n);

// Points *text at the oldest bytes a flush hasn't taken and returns how many
// follow in one piece (0 when the ring is empty). log_ring_drop takes them.
u32 log_ring_peek(const log_ring *r, const char **text);
void log_ring_drop(log_ring *r, u32 n);

// Copies the oldest text a flush hasn't taken, whole lines of it and at
// most max bytes, across the end of the buffer, into out; returns the count
// (log_ring_drop takes them). Whole lines, so a log file never ends inside
// one.
u32 log_ring_take_lines(const log_ring *r, char *out, u32 max);

// Copies the newest text, flushed or not, at most max bytes of it and
// starting at a line, into out; returns the count. The crash dump keeps it
// in cart RAM.
u32 log_ring_tail(const log_ring *r, char *out, u32 max);

// The log files: LOG_FILES of LOG_FILE_SIZE bytes each, written in turn.
// Each starts with a header holding its sequence number; text follows, and
// zero bytes fill the rest. A file is made full size once, so a flush only
// rewrites its data sectors and its entry in /torii/logs (the time it was
// written), and never changes the card's FAT.
#define LOG_FILES 4
#define LOG_FILE_SIZE 0x10000
#define LOG_HEADER_LEN 24  // "torii log 00000001\n" and padding

// Writes the header for sequence seq (LOG_HEADER_LEN bytes, no NUL).
void log_header_make(char *out, u32 seq);

// The sequence in a file's first n bytes, or 0 if they aren't a header (a
// new file, or one whose rotation was cut off).
u32 log_header_seq(const char *buf, u32 n);

// The file to append to: the one with the highest sequence, or -1 if none
// has a header.
int log_pick_current(const u32 seqs[LOG_FILES]);

// Where the text in a log file ends, from the whole file fed in order, in
// pieces of any size: the text has no zero bytes and zeros fill the rest,
// so the end is the first zero after the header. A nonzero byte after that
// is junk, as v0.2.0 wrote on the real cart by zero-filling from ROM while
// SD mode was on; the end found in such a file can't be trusted.
typedef struct {
	u32 seen;  // bytes fed so far
	u32 end;  // the first zero after the header, once found
	u8 found;
	u8 junk;  // a nonzero byte follows the end
} log_end;

void log_end_start(log_end *e);
void log_end_feed(log_end *e, const char *buf, u32 n);
// The end: the first zero after the header, or the bytes seen if there is
// none (a full file). From LOG_HEADER_LEN on.
u32 log_end_offset(const log_end *e);

#endif
