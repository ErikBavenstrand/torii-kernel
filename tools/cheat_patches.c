// Prints each cheat's status, table hash and ROM patches in libretro .cht
// files, as the kernel decodes a file (source/cheatfile.c: every cheat in
// file order with one decoder, cheatN_format honoured). Several files are
// read as the parts of one, in order: the decoder's keys and seeds and its
// record of the cheats seen so far (CHEATFILE_SAME) carry from each to the
// next, so a list past one file's limits (CHEATFILE_MAX cheats,
// CHEATFILE_TEXT_MAX bytes) reads as the kernel would read it whole.
// tools/cheats.py builds it for the host and runs it to sift out the cheats
// the kernel can't use and to tell which of a base game's cheats patch bytes
// a romhack changed.
//
// cheat_patches --index FILE.cht prints instead the index the kernel would
// build for the file (source/cheatindex.h): its windows cut as the kernel
// cuts them, each with its CRC and the decoder at its start, and its picks.
//
// Usage: cheat_patches [--raw] FILE.cht...
// One line per cheat, in order: its file's place among the arguments, its
// index in the file, its status (enum cheatfile_status), a hash of its table
// part and patches (0 unless OK) and its patches as OFFSET/VALUE/MASK in hex
// (offsets from 0x08000000), tab-separated. With --raw, after each a line
// "R", its format's name (cheatfile_format_names) and its code as the lines
// it decoded from, decrypted and written plain, key and seed lines left out
// ("-" and nothing for one that can't be: a VBA line, or formats mixed), so
// it decodes the same with no key (cheatcode.h's struct cheat_raw).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../source/cheatfile.h"
#include "../source/cheatindex.h"
#include "../source/crc32.h"

static struct cheatfile file;
static struct cheatfile_cheat cheats[CHEATFILE_MAX];
static struct cheatfile_work work;

// FNV-1a of the n words at p, on from h
static u32 hash_words(u32 h, const u32 *p, u32 n)
{
	for (u32 i = 0; i < n; i++)
		for (u32 b = 0; b < 32; b += 8)
			h = (h ^ ((p[i] >> b) & 0xFF)) * 16777619u;
	return h;
}

// The cheat just decoded, decrypted (cheat_raw): its format and plain code
static void print_raw(void)
{
	u8 format = CHEAT_FMT_AUTO;
	u32 kept = 0;
	if (cheat_raw.n > CHEAT_RAW_MAX) {
		printf("R\t-\t\n");
		return;
	}
	for (u32 i = 0; i < cheat_raw.n; i++) {
		if (cheat_raw.line[i].drop)
			continue;
		u8 f = cheat_raw.line[i].format;
		if (f == CHEAT_FMT_VBA || (format != CHEAT_FMT_AUTO && f != format)) {
			printf("R\t-\t\n");
			return;
		}
		format = f;
		kept++;
	}
	printf("R\t%s\t", kept ? cheatfile_format_names[format] : "-");
	for (u32 i = 0, n = 0; kept && i < cheat_raw.n; i++) {
		if (cheat_raw.line[i].drop)
			continue;
		if (format == CHEAT_FMT_CODEBREAKER)
			printf("%s%08X %04X", n++ ? "+" : "", (unsigned)cheat_raw.line[i].op1,
			       (unsigned)(cheat_raw.line[i].op2 & 0xFFFF));
		else
			printf("%s%08X %08X", n++ ? "+" : "", (unsigned)cheat_raw.line[i].op1,
			       (unsigned)cheat_raw.line[i].op2);
	}
	putchar('\n');
}

// --index: the file's index on stdout; 0 if it can't be cut into windows
static int print_index(const char *text, u32 len)
{
	static struct cheatindex_window win[64];
	static char out[CHEATINDEX_LINE_MAX];
	struct cheatindex_cut cut;
	u32 n = 1, pos = 0;
	cheatindex_cut_begin(&cut);
	memset(win, 0, sizeof(win));
	while (pos < len) {
		const char *nl = memchr(text + pos, '\n', len - pos);
		u32 end = nl ? (u32)(nl - text) : len, l = end - pos;
		if (l && text[end - 1] == '\r')
			l--;
		int r = cheatindex_cut_line(&cut, text + pos, l < 48 ? l : 48, pos);
		if (r == CHEATINDEX_BAD)
			return 0;
		if (r == CHEATINDEX_NEW) {
			if (n == 64)
				return 0;
			win[n - 1].len = pos - win[n - 1].at;
			win[n++].at = pos;
		}
		if (cut.any)
			win[n - 1].first = (u16)cut.first;
		pos = nl ? end + 1 : len;
	}
	win[n - 1].len = len - win[n - 1].at;
	cheatindex_head(out, len);
	fputs(out, stdout);
	// Each window decoded in turn, the decoder carrying on, as the kernel's
	// index_build does; the picks as its enable lines say
	cheatfile_begin(&work);
	static char picks[64 * 1024];
	u32 np = 0;
	for (u32 k = 0; k < n; k++) {
		struct cheat_decoder dec = work.dec;
		cheatfile_parse_part(&file, text + win[k].at, win[k].len, cheats, CHEATFILE_MAX, win[k].first, 1);
		cheatfile_begin(&work);
		work.dec = dec;
		win[k].dec = dec;
		win[k].count = (u16)file.count;
		win[k].crc = crc32(text + win[k].at, win[k].len);
		while (cheatfile_next(&file, &work) >= 0)
			;
		for (u32 i = 0; i < file.count; i++) {
			const struct cheatfile_cheat *c = &cheats[i];
			if (!c->picked)
				continue;
			int ok = c->status == CHEATFILE_OK;
			np += cheatindex_pick_line(picks + np, c->number, c->hash, ok ? c->bytes : 0, ok ? c->cost : 0,
						   ok ? c->patches : 0);
		}
		cheatindex_window_line(out, &win[k]);
		fputs(out, stdout);
	}
	fwrite(picks, 1, np, stdout);
	return 1;
}

int main(int argc, char **argv)
{
	if (argc == 3 && !strcmp(argv[1], "--index")) {
		FILE *f = fopen(argv[2], "rb");
		long len;
		if (!f || fseek(f, 0, SEEK_END) || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)) {
			perror(argv[2]);
			return 1;
		}
		char *text = malloc(len ? (size_t)len : 1);
		if (!text || fread(text, 1, (size_t)len, f) != (size_t)len)
			return 1;
		fclose(f);
		if (!print_index(text, (u32)len)) {
			fprintf(stderr, "%s: its cheats can't be cut into windows\n", argv[2]);
			return 1;
		}
		return 0;
	}
	int raw = argc > 1 && !strcmp(argv[1], "--raw");
	if (argc < 2 + raw) {
		fprintf(stderr, "usage: cheat_patches [--raw] FILE.cht...\n");
		return 2;
	}
	cheatfile_begin(&work);
	for (int a = 1 + raw; a < argc; a++) {
		FILE *f = fopen(argv[a], "rb");
		long len;
		if (!f || fseek(f, 0, SEEK_END) || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET)) {
			perror(argv[a]);
			return 1;
		}
		char *text = malloc(len ? (size_t)len : 1);
		if (!text || fread(text, 1, (size_t)len, f) != (size_t)len) {
			perror(argv[a]);
			return 1;
		}
		fclose(f);
		cheatfile_parse(&file, text, (u32)len, cheats, CHEATFILE_MAX);
		if (file.cut) {
			fprintf(stderr, "%s: over CHEATFILE_TEXT_MAX\n", argv[a]);
			return 1;
		}
		work.next = 0;  // this file's first cheat, the decoder as the last left it
		s32 i;
		while ((i = cheatfile_next(&file, &work)) >= 0) {
			u32 h = 0;
			if (cheats[i].status == CHEATFILE_OK) {
				h = hash_words(2166136261u, &work.n_prog, 1);
				h = hash_words(h, work.prog, work.n_prog);
				h = hash_words(h, (const u32 *)(const void *)work.patches,
					       work.n_patches * (u32)sizeof(struct cheat_patch) / 4);
			}
			printf("%d\t%ld\t%u\t%08X", a - 1 - raw, (long)i, (unsigned)cheats[i].status, (unsigned)h);
			for (u32 p = 0; cheats[i].status == CHEATFILE_OK && p < work.n_patches; ++p)
				printf("\t%X/%X/%X", (unsigned)work.patches[p].offset, (unsigned)work.patches[p].value,
				       (unsigned)work.patches[p].mask);
			putchar('\n');
			if (raw)
				print_raw();
		}
		free(text);
	}
	return 0;
}
