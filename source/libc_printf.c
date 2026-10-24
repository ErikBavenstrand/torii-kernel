// snprintf and its family by the kernel's own formatting (fmt.h). Defined
// here, these are what the kernel links instead of newlib's, whose printf
// brings stdio, a heap and float code: 26 KB of ROM and 6.6 KB of IWRAM data
// (libsysbase's handle table, newlib's impure data, malloc's state).
#include <stdio.h>

#include "fmt.h"

int vsnprintf(char *out, size_t room, const char *format, va_list ap)
{
	return fmt_vsnprintf(out, room, format, ap);
}

int vsprintf(char *out, const char *format, va_list ap)
{
	return fmt_vsnprintf(out, (size_t)-1 >> 1, format, ap);
}

int snprintf(char *out, size_t room, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	int n = fmt_vsnprintf(out, room, format, ap);
	va_end(ap);
	return n;
}

int sprintf(char *out, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	int n = fmt_vsnprintf(out, (size_t)-1 >> 1, format, ap);
	va_end(ap);
	return n;
}
