// Host helper of tools/cheat_emu_coverage.py: reads "CONSOLE CODE" lines (CONSOLE
// GB or NES, CODE one cheat's string, lines joined by '+') on stdin and prints, per
// line of input, the whole cheat's decoder answer, then each '+' part's own,
// as "R <answer> <part answers...>" with answers ok, unreadable, cantrun, big.
#include <stdio.h>
#include <string.h>

#include "cheatemu.h"

static const char *name(u8 r)
{
	switch (r) {
	case CHEATEMU_OK:
		return "ok";
	case CHEATEMU_UNREADABLE:
		return "unreadable";
	case CHEATEMU_CANT_RUN:
		return "cantrun";
	default:
		return "big";
	}
}

int main(void)
{
	char buf[8192];
	while (fgets(buf, sizeof buf, stdin)) {
		char *sp = strchr(buf, ' ');
		if (!sp)
			continue;
		*sp = 0;
		char *code = sp + 1;
		u32 len = (u32)strcspn(code, "\n");
		u32 con = strcmp(buf, "GB") == 0 ? CHEATEMU_GB : CHEATEMU_NES;
		struct emucheat out[64];
		u32 n;
		u8 r = cheatemu_decode(con, code, len, out, 64, &n);
		printf("R %s", name(r));
		u32 i = 0;
		while (i <= len) {
			u32 from = i;
			while (i < len && code[i] != '+')
				i++;
			if (i > from) {
				u8 rr = cheatemu_decode(con, code + from, i - from, out, 64, &n);
				printf(" %s", name(rr));
			}
			i++;
		}
		printf("\n");
	}
	return 0;
}
