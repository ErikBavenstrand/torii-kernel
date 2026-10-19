// Torii log. Lines go to two places:
// - mGBA's debug console, whenever the kernel runs in mGBA (readable
//   headlessly from mgba-shot and just compat, and in the mGBA window's log);
// - the SD card, at the level the LOGGING setting picks: they wait in a RAM
//   ring buffer (source/logbuf.c) and tlog_flush writes them to
//   /torii/logs/torii-N.log at safe points (after boot, on errors, when
//   settings close, before a launch).
// With LOGGING off on a real cart, every call returns at once and nothing
// touches the card, except to log a kernel crash dump (source/cartlog.c).
#ifndef TORII_TLOG_H
#define TORII_TLOG_H

#include <gba_base.h>

// LOGGING setting values (settings slot assress_logging). Erased NOR reads
// 0xFFFF, which counts as off.
enum {
	TLOG_OFF = 0,
	TLOG_ERRORS = 1,
	TLOG_INFO = 2,
	TLOG_DEBUG = 3,
};

// The level lines are kept at for the SD card.
extern u8 tlog_level;

// Reads the LOGGING setting and looks for mGBA's debug console.
void tlog_init(void);
// The setting's value, TLOG_OFF if the slot holds anything else.
u32 tlog_level_setting(void);
const char *tlog_level_name(u32 level);
// The line that starts a session on the card: why, the level, and the
// build. It goes to the card at any level but off, or with force at that
// too (tlog_force), as a crash report and Diagnostics do.
void tlog_marker(const char *why, int force);
// Changes the level for this run only (Diagnostics logs at TLOG_DEBUG).
void tlog_set_level(u32 level);
// True if a line at level would go anywhere: use it to skip work that only
// builds log lines.
int tlog_wants(u32 level);
// True when the kernel runs in mGBA (its debug console answers).
int tlog_in_emulator(void);

void tlog(const char *format, ...) __attribute__((format(printf, 1, 2)));
void tlog_error(const char *format, ...) __attribute__((format(printf, 1, 2)));
void tlog_debug(const char *format, ...) __attribute__((format(printf, 1, 2)));

// A line for mGBA's debug console only, never the card: figures logged
// every frame for the tests (frame-cycles), which would flood the card.
void tlog_console(const char *format, ...) __attribute__((format(printf, 1, 2)));

// The card is mounted: flushes may write to it from now on.
void tlog_sd_ready(void);
// Writes the lines waiting in RAM to the card, after the frame-cycles
// summary (cycles_summary). Call it only where the kernel's ROM is mapped and
// no other code is in the middle of a FatFs call.
void tlog_flush(void);
// tlog_flush for a screen of an engine session (SETTINGS turning LOGGING
// on): if the log isn't open on the card yet, a job opens it a sector at a
// time over the next frames (about 130 steps to make a new 64 KB file), so
// the screen goes on taking keys, and flushes wait for it while the session
// lasts; tlog_flush finishes it at once outside one, as before a launch.
// Otherwise, or with no job slot free, it flushes now.
void tlog_flush_soon(void);
// Finishes tlog_flush_soon's open at once, if one is under way (or stops it,
// with LOGGING off): before code that may delete a file in /torii/logs,
// which the open holds open (FatFs keeps no lock: FF_FS_LOCK 0).
void tlog_open_now(void);
// For tests, poked by name: nonzero, tlog_flush_soon's job makes at most
// this many steps less one, so a test can act while the open is under way.
extern u32 tlog_open_steps_test;
// A line at level that goes to the card even with LOGGING off (crash
// dumps); the next tlog_flush writes it.
void tlog_force(u32 level, const char *format, ...) __attribute__((format(printf, 2, 3)));

// The last line the previous session wrote to the card, read when the card
// was mounted with logging on; NULL if there is none.
const char *tlog_previous_line(void);

// Bytes of log text waiting in RAM for a flush.
u32 tlog_pending(void);

// True if every line so far reached the card (or none was kept).
int tlog_all_written(void);
// True once writing the card failed (no flush writes until the next boot),
// or while the card is full: then the text waits in RAM, and every flush
// but the lists' idle one tries again.
int tlog_card_failed(void);
// A file was deleted: the card may have room for the log again.
void tlog_card_freed(void);

// The newest log text, for the crash dump: see log_ring_tail.
u32 tlog_recent(char *out, u32 max);

// Frames since boot, counted by the kernel's IRQ handler (source/crash.s).
extern volatile u32 torii_frames;

// Runs the kernel unit tests in the test kernel; does nothing otherwise.
void torii_run_tests(void);

// Logs the failed condition as an error; just compat and the tests fail on it.
#define TLOG_ASSERT(cond) \
	do { \
		if (!(cond)) \
			tlog_error("assert failed: %s:%d: %s", __FILE__, __LINE__, #cond); \
	} while (0)

#endif
