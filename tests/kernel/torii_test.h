// Kernel unit tests: built into a test kernel (`just test-kernel`) that runs
// them at boot, logs through tlog and stops. Each test is a void function
// listed in tests/kernel/run.c.
#ifndef TORII_TEST_H
#define TORII_TEST_H

#include "../../source/tlog.h"

extern int torii_test_failures;

#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			tlog_error("FAIL %s:%d: %s", __FILE__, __LINE__, #cond); \
			++torii_test_failures; \
		} \
	} while (0)

#define CHECK_EQ(actual, expected) \
	do { \
		unsigned long _a = (unsigned long)(actual), _e = (unsigned long)(expected); \
		if (_a != _e) { \
			tlog_error("FAIL %s:%d: %s is 0x%lX, expected 0x%lX", __FILE__, __LINE__, #actual, _a, _e); \
			++torii_test_failures; \
		} \
	} while (0)

#endif
