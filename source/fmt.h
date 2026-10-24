// The kernel's printf formatting (fmt.c): integers and strings only, as
// every format in the kernel is. libc_printf.c gives it snprintf's names, so
// newlib's printf, with its stdio, heap and float code, isn't linked.
//
// Conversions: d i u x X c s p %, with the flags - and 0, a width and a
// precision (numbers or *), and the lengths hh h l ll z j t. Returns what
// C99's vsnprintf does: the length the whole result has, written or not.
#ifndef TORII_FMT_H
#define TORII_FMT_H

#include <stdarg.h>
#include <stddef.h>

int fmt_vsnprintf(char *out, size_t room, const char *format, va_list ap);

// C's strtoul, for bases 2 to 36 (0 isn't taken): spaces, a sign and, in base
// 16, 0x skipped; ULONG_MAX past it. newlib's sets errno, and that links its
// reentrancy block (impure_data, 1 KB of IWRAM).
unsigned long fmt_strtoul(const char *s, char **end, int base);

#endif
