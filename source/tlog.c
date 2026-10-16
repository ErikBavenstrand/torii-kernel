#include <stdarg.h>
#include <stdio.h>

#include <gba_base.h>

#include "tlog.h"

// mGBA's debug registers: write 0xC0DE to enable (it then reads 0x1DEA),
// fill the string, then write the level with the send bit (0x100).
#define REG_DEBUG_ENABLE (*(vu16 *)0x04FFF780)
#define REG_DEBUG_FLAGS (*(vu16 *)0x04FFF700)
#define REG_DEBUG_STRING ((char *)0x04FFF600)
#define DEBUG_SEND 0x100
#define LEVEL_ERROR 1
#define LEVEL_INFO 3

static int tlog_enabled;

void tlog_init(void)
{
	REG_DEBUG_ENABLE = 0xC0DE;
	tlog_enabled = REG_DEBUG_ENABLE == 0x1DEA;
}

static void tlog_send(int level, const char *format, va_list args)
{
	if (!tlog_enabled)
		return;
	vsnprintf(REG_DEBUG_STRING, 0x100, format, args);
	REG_DEBUG_FLAGS = level | DEBUG_SEND;
}

void tlog(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	tlog_send(LEVEL_INFO, format, args);
	va_end(args);
}

void tlog_error(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	tlog_send(LEVEL_ERROR, format, args);
	va_end(args);
}

// The test kernel (just test-kernel) replaces this to run tests/kernel at boot.
__attribute__((weak)) void torii_run_tests(void)
{
}
