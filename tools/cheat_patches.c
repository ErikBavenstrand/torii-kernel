// Prints each cheat's ROM patches in a libretro .cht file, as the kernel
// decodes the file (source/cheatfile.c: every cheat in file order with one
// decoder, cheatN_format honoured). tools/cheats.py builds it for the host
// and runs it to tell which of a base game's cheats patch bytes a romhack
// changed.
//
// Usage: cheat_patches FILE.cht
// One line per cheat, in file order: its index, its status
// (enum cheatfile_status) and its patches as OFFSET/VALUE/MASK in hex
// (offsets from 0x08000000), tab-separated.
#include <stdio.h>
#include <stdlib.h>

#include "../source/cheatfile.h"

static struct cheatfile file;
static struct cheatfile_cheat cheats[CHEATFILE_MAX];
static struct cheatfile_work work;

int main(int argc, char **argv)
{
	FILE *f;
	char *text;
	long len;
	s32 i;
	u32 p;
	if (argc != 2) {
		fprintf(stderr, "usage: cheat_patches FILE.cht\n");
		return 2;
	}
	f = fopen(argv[1], "rb");
	if (!f || fseek(f, 0, SEEK_END) || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)) {
		perror(argv[1]);
		return 1;
	}
	text = malloc(len ? (size_t)len : 1);
	if (!text || fread(text, 1, (size_t)len, f) != (size_t)len) {
		perror(argv[1]);
		return 1;
	}
	fclose(f);
	cheatfile_parse(&file, text, (u32)len, cheats, CHEATFILE_MAX);
	if (file.cut) {
		fprintf(stderr, "%s: over CHEATFILE_TEXT_MAX\n", argv[1]);
		return 1;
	}
	cheatfile_begin(&work);
	while ((i = cheatfile_next(&file, &work)) >= 0) {
		printf("%ld\t%u", (long)i, (unsigned)cheats[i].status);
		for (p = 0; cheats[i].status == CHEATFILE_OK && p < work.n_patches; ++p)
			printf("\t%X/%X/%X", (unsigned)work.patches[p].offset, (unsigned)work.patches[p].value,
			       (unsigned)work.patches[p].mask);
		putchar('\n');
	}
	free(text);
	return 0;
}
