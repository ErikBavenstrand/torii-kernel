// Torii debug log: printf-style lines to mGBA's debug console, readable
// headlessly (mgba-shot, just compat) and in the mGBA window's log. On real
// hardware the registers don't exist, tlog_init finds no emulator, and every
// call returns at once.
#ifndef TORII_TLOG_H
#define TORII_TLOG_H

void tlog_init(void);
void tlog(const char *format, ...) __attribute__((format(printf, 1, 2)));
void tlog_error(const char *format, ...) __attribute__((format(printf, 1, 2)));

// Runs the kernel unit tests in the test kernel; does nothing otherwise.
void torii_run_tests(void);

// Logs the failed condition as an error; just compat and the tests fail on it.
#define TLOG_ASSERT(cond) \
	do { \
		if (!(cond)) \
			tlog_error("assert failed: %s:%d: %s", __FILE__, __LINE__, #cond); \
	} while (0)

#endif
