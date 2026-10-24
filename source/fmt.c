// The kernel's printf formatting (fmt.h)
#include "fmt.h"

struct sink {
	char *out;
	size_t room;  // bytes out can take, its terminator included
	size_t len;   // the result's length so far, written or not
};

static void put(struct sink *s, char c)
{
	if (s->len + 1 < s->room)
		s->out[s->len] = c;
	s->len++;
}

static void pad(struct sink *s, char c, int n)
{
	for (; n > 0; n--)
		put(s, c);
}

int fmt_vsnprintf(char *out, size_t room, const char *f, va_list ap)
{
	struct sink s = {out, room, 0};
	for (; *f; f++) {
		if (*f != '%') {
			put(&s, *f);
			continue;
		}
		f++;
		int left = 0, zero = 0;
		for (;; f++) {
			if (*f == '-')
				left = 1;
			else if (*f == '0')
				zero = 1;
			else if (*f != ' ' && *f != '+' && *f != '#')
				break;
		}
		int width = 0;
		if (*f == '*') {
			width = va_arg(ap, int);
			if (width < 0) {
				left = 1;
				width = -width;
			}
			f++;
		} else {
			for (; *f >= '0' && *f <= '9'; f++)
				width = width * 10 + (*f - '0');
		}
		int prec = -1;
		if (*f == '.') {
			f++;
			prec = 0;
			if (*f == '*') {
				prec = va_arg(ap, int);
				f++;
			} else {
				for (; *f >= '0' && *f <= '9'; f++)
					prec = prec * 10 + (*f - '0');
			}
		}
		int lng = 0;  // 1: long, 2: long long, 3: size_t, ptrdiff_t, intmax_t
		for (;; f++) {
			if (*f == 'l')
				lng++;
			else if (*f == 'z' || *f == 'j' || *f == 't')
				lng = 3;
			else if (*f != 'h')
				break;
		}
		char c = *f;
		if (!c)
			break;
		if (c == 's' || c == 'c') {
			char one = 0;
			const char *str;
			size_t n;
			if (c == 'c') {
				one = (char)va_arg(ap, int);
				str = &one;
				n = 1;
			} else {
				str = va_arg(ap, const char *);
				if (!str)
					str = "(null)";
				for (n = 0; str[n] && (prec < 0 || n < (size_t)prec); n++)
					;
			}
			if (!left)
				pad(&s, ' ', width - (int)n);
			for (size_t i = 0; i < n; i++)
				put(&s, str[i]);
			if (left)
				pad(&s, ' ', width - (int)n);
			continue;
		}
		unsigned long long v;
		int neg = 0;
		unsigned base = 10;
		const char *digits = "0123456789abcdef";
		if (c == 'd' || c == 'i') {
			long long sv;
			if (lng == 2 || lng == 3)
				sv = lng == 2 ? va_arg(ap, long long) : (long long)va_arg(ap, ptrdiff_t);
			else if (lng == 1)
				sv = va_arg(ap, long);
			else
				sv = va_arg(ap, int);
			neg = sv < 0;
			v = neg ? 0ULL - (unsigned long long)sv : (unsigned long long)sv;
		} else if (c == 'u' || c == 'x' || c == 'X' || c == 'p') {
			if (c == 'p')
				v = (unsigned long long)(size_t)va_arg(ap, void *);
			else if (lng == 2)
				v = va_arg(ap, unsigned long long);
			else if (lng == 3)
				v = va_arg(ap, size_t);
			else if (lng == 1)
				v = va_arg(ap, unsigned long);
			else
				v = va_arg(ap, unsigned);
			if (c != 'u')
				base = 16;
			if (c == 'X')
				digits = "0123456789ABCDEF";
		} else {
			// %% and anything unknown print as themselves
			put(&s, c);
			continue;
		}
		char buf[24];
		int n = 0;
		do {
			buf[n++] = digits[v % base];
			v /= base;
		} while (v);
		int zeros = prec > n ? prec - n : 0;
		if (prec < 0 && zero && !left)
			zeros = width - n - neg - (c == 'p') * 2;
		if (prec == 0 && n == 1 && buf[0] == '0')
			n = 0;  // "%.0d" of 0 is empty
		int total = n + (zeros > 0 ? zeros : 0) + neg + (c == 'p') * 2;
		if (!left)
			pad(&s, ' ', width - total);
		if (neg)
			put(&s, '-');
		if (c == 'p') {
			put(&s, '0');
			put(&s, 'x');
		}
		pad(&s, '0', zeros);
		while (n)
			put(&s, buf[--n]);
		if (left)
			pad(&s, ' ', width - total);
	}
	if (room)
		out[s.len < room ? s.len : room - 1] = 0;
	return (int)s.len;
}
