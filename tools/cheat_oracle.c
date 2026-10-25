// Decodes every cheat in libretro .cht files with mGBA's own decoders (linked
// from emu/build-linux/libmgba.a), to check Torii's port against them
// (tests/cheat_corpus.sh). Each cheat gets a cheat set of its own that takes
// the previous one's properties, as mGBA's libretro reader does; its code is
// split at '+', a piece's spaces dropped, and an 8-digit piece paired with the
// next, then fed line by line to mGBA's autodetecting addLine.
//
// Usage: cheat_oracle FILE.cht... > rows.tsv
// Each row: file, cheat number, then SKIP (a piece that isn't hex), or the
// lines mGBA rejected and its cheat list and ROM patches, normalized as
// tools/cheat_corpus.c prints Torii's.
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mgba/core/cheats.h>
#include <mgba/core/log.h>
#include <mgba/internal/gba/cheats.h>

static const char *const names[] = {"ASSIGN", "IND", "AND", "ADD",   "OR",   "EQ",   "NE",     "LT",
				    "GT",     "ULT", "UGT", "IFAND", "LAND", "NAND", "BUTTON", "NEVER"};

static unsigned mask(int width)
{
	return width >= 4 ? 0xFFFFFFFFu : width <= 0 ? 0 : (1u << (8 * width)) - 1;
}

static void dump(struct mCheatSet *set)
{
	size_t i;
	for (i = 0; i < mCheatListSize(&set->list); ++i) {
		struct mCheat *c = mCheatListGetPointer(&set->list, i);
		unsigned rep = c->repeat;
		if (c->type >= CHEAT_IF_EQ) {
			// What a conditional covers stops at the list's end
			unsigned left = mCheatListSize(&set->list) - i - 1;
			unsigned neg = c->negativeRepeat;
			rep = rep < left ? rep : left;
			neg = neg < left - rep ? neg : left - rep;
			if (c->type == CHEAT_IF_BUTTON || c->type == CHEAT_NEVER)
				printf(" %s/%u/%u", names[c->type], rep, neg);
			else
				printf(" %s/%d/%08X/%X/%u/%u", names[c->type], c->width, c->address,
				       c->operand & mask(c->width), rep, neg);
		} else {
			int aoff = rep > 1 || c->type == CHEAT_ASSIGN_INDIRECT ? c->addressOffset : 0;
			int voff = rep > 1 ? c->operandOffset : 0;
			if (c->type != CHEAT_ASSIGN)
				rep = 1;
			printf(" %s/%d/%08X/%X/%u/%d/%d", names[c->type], c->width, c->address,
			       c->operand & mask(c->width), rep, aoff, voff);
		}
	}
	printf(" |");
	for (i = 0; i < mCheatPatchListSize(&set->romPatches); ++i) {
		struct mCheatPatch *p = mCheatPatchListGetPointer(&set->romPatches, i);
		printf(" %08X/%X/%d", p->address, p->value & mask(p->width), p->width);
	}
}

static int is_hex(const char *p, size_t n)
{
	size_t i;
	for (i = 0; i < n; ++i)
		if (!isxdigit((unsigned char)p[i]))
			return 0;
	return 1;
}

// Splits code into pieces at '+', spaces dropped; returns how many
static int pieces(char *code, char **out, int max)
{
	int n = 0;
	char *p = code;
	for (;;) {
		char *plus = strchr(p, '+');
		if (plus)
			*plus = '\0';
		char *w = p, *r;
		for (r = p; *r; ++r)
			if (!isspace((unsigned char)*r))
				*w++ = *r;
		*w = '\0';
		if (*p && n < max)
			out[n++] = p;
		if (!plus)
			return n;
		p = plus + 1;
	}
}

static void decode(struct mCheatDevice *device, struct mCheatSet **prev, const char *file, unsigned long n, char *code)
{
	char *piece[512];
	char line[32];
	int count = pieces(code, piece, 512), i, rejected = 0;
	struct mCheatSet *set = device->createSet(device, NULL);
	if (*prev) {
		set->copyProperties(set, *prev);
		((struct GBACheatSet *)set)->hook = NULL;  // so a hook line in each cheat is accepted
	}
	*prev = set;
	printf("%s\t%lu\t", file, n);
	for (i = 0; i < count; ++i) {
		size_t len = strlen(piece[i]);
		char *colon = strchr(piece[i], ':');
		if (colon ? !(colon - piece[i] == 8 && is_hex(piece[i], 8) && is_hex(colon + 1, strlen(colon + 1)))
			  : !is_hex(piece[i], len)) {
			printf("SKIP\n");
			return;
		}
	}
	for (i = 0; i < count; ++i) {
		size_t len = strlen(piece[i]);
		if (strchr(piece[i], ':') || len == 12 || len == 16) {
			snprintf(line, sizeof(line), "%s", piece[i]);
		} else if (len == 8 && i + 1 < count && (strlen(piece[i + 1]) == 4 || strlen(piece[i + 1]) == 8)) {
			snprintf(line, sizeof(line), "%s %s", piece[i], piece[i + 1]);
			++i;
		} else {
			printf("SKIP\n");
			return;
		}
		if (!set->addLine(set, line, GBA_CHEAT_AUTODETECT))
			++rejected;
	}
	printf("REJ%d\t", rejected);
	dump(set);
	putchar('\n');
}

static void read_file(struct mCheatDevice *device, const char *path)
{
	FILE *f = fopen(path, "rb");
	struct mCheatSet *prev = NULL;
	char line[8192];
	const char *file = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
	if (!f) {
		perror(path);
		exit(1);
	}
	while (fgets(line, sizeof(line), f)) {
		char *end;
		unsigned long n;
		if (strncmp(line, "cheat", 5) != 0)
			continue;
		n = strtoul(line + 5, &end, 10);
		if (end == line + 5 || strncmp(end, "_code", 5) != 0)
			continue;
		char *open = strchr(end, '"');
		if (!open) {
			printf("%s\t%lu\tSKIP\n", file, n);
			continue;
		}
		char *close = strrchr(open + 1, '"');
		if (!close)
			close = open + 1 + strcspn(open + 1, "\r\n");
		*close = '\0';
		decode(device, &prev, file, n, open + 1);
	}
	fclose(f);
}

// mGBA logs the codes it can't run to stdout
static void quiet(struct mLogger *logger, int category, enum mLogLevel level, const char *format, va_list args)
{
	(void)logger;
	(void)category;
	(void)level;
	(void)format;
	(void)args;
}

int main(int argc, char **argv)
{
	int i;
	static struct mLogger logger = {quiet, NULL};
	mLogSetDefaultLogger(&logger);
	struct mCheatDevice *device = GBACheatDeviceCreate();
	for (i = 1; i < argc; ++i)
		read_file(device, argv[i]);
	return 0;
}
