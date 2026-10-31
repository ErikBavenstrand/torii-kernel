// Decodes every cheat in libretro .cht files with Torii's decoders
// (source/cheatcode.c) and lowers it (source/cheatlower.c), printing one line
// per cheat and a summary by status. Built for the host under ASan and UBSan
// by tests/cheat_corpus.sh; each code is a heap copy without a NUL.
//
// Each cheat is also decoded as the kernel does (cheat_decode_split, its ROM
// patches outside the ops) and lowered (cheat_lower_onto): that has to give
// the same status, table, load and patches as the reading above, or the
// tool says so and exits 1.
//
// Usage: cheat_corpus FILE.cht... > rows.tsv
// Each row: file, cheat number, status, lowering, format, ops (in
// tools/cheat_oracle.c's form, to compare with mGBA's decoding).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../source/cheatcode.h"

#define OPS_MAX 4096
#define PROG_MAX 0x4000  // words
#define PATCHES_MAX 1024

static struct cheat_op ops[OPS_MAX];
static u32 prog[PROG_MAX];
static struct cheat_patch patches[PATCHES_MAX];
static struct cheat_op split_ops[OPS_MAX];
static u32 split_prog[PROG_MAX];
static struct cheat_patch split_patches[PATCHES_MAX];
static unsigned long split_differ, split_joined;

static int is_cond(u8 type);

// A cheat without conditionals whose ops, its ROM patches left out, are the
// split reading's n_split: its writes run alike, though rows of writes the
// patches parted may now be one run in the table
static int same_but_runs(u32 n_ops, u32 n_split)
{
	u32 i, k = 0;
	for (i = 0; i < n_ops; ++i) {
		if (is_cond(ops[i].type))
			return 0;
		if (ops[i].type == CHEAT_OP_ROM_PATCH)
			continue;
		if (k >= n_split || memcmp(&ops[i], &split_ops[k], sizeof(ops[i])))
			return 0;
		++k;
	}
	return k == n_split;
}

static const char *const status_names[] = {"OK", "MASTER_ONLY", "PLACEHOLDER", "UNREADABLE", "TOO_BIG"};
static const char *const lower_names[] = {"LOWERABLE", "TOO_BIG", "TOO_MANY_PATCHES", "BAD_ADDRESS", "CANT_RUN"};
static const char *const format_names[] = {"-", "GSAv1", "GSAv1-raw", "PARv3", "PARv3-raw", "CodeBreaker", "VBA"};

static unsigned long by_status[5], by_lower[5], by_format[7], lowered_bytes, lowered_cycles, lowered_patches,
	alone_reads;

static int is_cond(u8 type)
{
	return type >= CHEAT_OP_IF_EQ && type <= CHEAT_OP_IF_NEVER;
}

// How many ops that aren't ROM patches lie in ops[from, from + n)
static u32 real_ops(u32 from, u32 n, u32 n_ops)
{
	u32 i, count = 0;
	for (i = from; i < from + n && i < n_ops; ++i)
		count += ops[i].type != CHEAT_OP_ROM_PATCH;
	return count;
}

static u32 mask(u32 width)
{
	return width >= 4 ? 0xFFFFFFFF : (1u << (8 * width)) - 1;
}

// The ops as mGBA's cheat list and ROM patch list, normalized as
// tools/cheat_oracle.c prints them
static void dump(u32 n_ops)
{
	static const char *const names[] = {"ASSIGN", "IND",    "ADD",   "OR",    "AND",  "EQ",
					    "NE",     "LT",     "GT",    "ULT",   "UGT",  "IFAND",
					    "NAND",   "BUTTON", "NEVER", "PATCH", "SLOW", "KEYED"};
	u32 i;
	for (i = 0; i < n_ops; ++i) {
		const struct cheat_op *op = &ops[i];
		if (op->type == CHEAT_OP_ROM_PATCH)
			continue;
		if (op->type == CHEAT_OP_IF_BUTTON || op->type == CHEAT_OP_IF_NEVER || op->type == CHEAT_OP_SLOWDOWN) {
			u32 rep = real_ops(i + 1, op->count, n_ops);
			printf(" %s/%u/%u", names[op->type], rep, real_ops(i + 1 + op->count, op->else_count, n_ops));
			continue;
		}
		if (is_cond(op->type)) {
			u32 rep = real_ops(i + 1, op->count, n_ops);
			printf(" %s/%u/%08X/%X/%u/%u", names[op->type], op->width, op->address,
			       op->value & mask(op->width), rep, real_ops(i + 1 + op->count, op->else_count, n_ops));
			continue;
		}
		u32 rep = op->type == CHEAT_OP_WRITE ? op->count : 1;
		s32 aoff = rep > 1 || op->type == CHEAT_OP_WRITE_PTR ? op->addr_step : 0;
		s32 voff = rep > 1 ? op->value_step : 0;
		printf(" %s/%u/%08X/%X/%u/%d/%d", names[op->type], op->width, op->address, op->value & mask(op->width),
		       rep, (int)aoff, (int)voff);
		if (op->type == CHEAT_OP_KEYED)  // Torii's own: mGBA has none to match
			printf("/%d/%08X/%X/%X", (int)op->addr_step, op->key, op->key_offset, op->flags);
	}
	printf(" |");
	for (i = 0; i < n_ops; ++i)
		if (ops[i].type == CHEAT_OP_ROM_PATCH)
			printf(" %08X/%X/%u", ops[i].address, ops[i].value & mask(ops[i].width), ops[i].width);
}

// The kernel's split reading of the cheat (from d0, the decoder before it)
// against the one above (status, lowering, its outputs, and d after it)
static void check_split(const struct cheat_decoder *d0, const struct cheat_decoder *d, const char *file,
			unsigned long n, const char *code, u32 len, u8 status, u8 lower, u32 n_ops_old, u32 n_prog,
			u32 cost, u32 n_patches)
{
	struct cheat_decoder sd = *d0;
	struct cheat_patches ps = {split_patches, PATCHES_MAX, 0, NULL, 0};
	u32 n_ops, words = 0, scost = 0;
	u8 sstatus = cheat_decode_split(&sd, code, len, split_ops, OPS_MAX, &n_ops, &ps), slower = 0xFF;
	if (sstatus == CHEAT_OK)
		slower = cheat_lower_onto(split_ops, n_ops, split_prog, PROG_MAX, &words, &scost, &ps);
	const char *why = NULL;
	if (sstatus != status)
		why = "status";
	else if (memcmp(&sd, d, sizeof(sd)))
		why = "decoder state";
	else if (status == CHEAT_OK && slower != lower)
		why = "lowering";
	else if (status == CHEAT_OK && lower == CHEAT_LOWER_OK &&
		 (words != n_prog || scost != cost || memcmp(split_prog, prog, words * 4))) {
		if (same_but_runs(n_ops_old, n_ops))
			++split_joined;
		else
			why = "table";
	} else if (status == CHEAT_OK && lower == CHEAT_LOWER_OK &&
		   (ps.n != n_patches || memcmp(split_patches, patches, ps.n * sizeof(patches[0]))))
		why = "patches";
	if (why) {
		fprintf(stderr, "split reading differs (%s): %s cheat %lu\n", why, file, n);
		++split_differ;
	}
}

static void decode(struct cheat_decoder *d, const char *file, unsigned long n, const char *text, u32 len)
{
	char *code = malloc(len ? len : 1);
	u32 n_ops, n_prog = 0, cost = 0, n_patches = 0;
	struct cheat_decoder d0 = *d;
	u8 lower = 0xFF;
	memcpy(code, text, len);
	u8 status = cheat_decode(d, code, len, ops, OPS_MAX, &n_ops);
	++by_status[status];
	printf("%s\t%lu\t%s\t", file, n, status_names[status]);
	if (status == CHEAT_OK) {
		lower = cheat_lower(ops, n_ops, prog, PROG_MAX, &n_prog, &cost, patches, PATCHES_MAX, &n_patches);
		++by_lower[lower];
		++by_format[d->last];
		if (lower == CHEAT_LOWER_OK) {
			lowered_bytes += n_prog * 4;
			lowered_cycles += cost;
			lowered_patches += n_patches;
		}
		printf("%s\t%s%s\t", lower_names[lower], format_names[d->last], d->last_alone ? "-alone" : "");
		alone_reads += d->last_alone;
		dump(n_ops);
	} else {
		printf("-\t%s\t", status == CHEAT_MASTER_ONLY ? format_names[d->last] : "-");
	}
	putchar('\n');
	check_split(&d0, d, file, n, code, len, status, lower, status == CHEAT_OK ? n_ops : 0, n_prog, cost, n_patches);
	free(code);
}

// Reads a libretro .cht file's cheatN_code lines in order, sharing one decoder
static void read_file(const char *path)
{
	FILE *f = fopen(path, "rb");
	struct cheat_decoder d;
	char line[8192];
	const char *file = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
	if (!f) {
		perror(path);
		exit(1);
	}
	cheat_decoder_init(&d);
	while (fgets(line, sizeof(line), f)) {
		char *end;
		unsigned long n;
		if (strncmp(line, "cheat", 5) != 0)
			continue;
		n = strtoul(line + 5, &end, 10);
		if (end == line + 5 || strncmp(end, "_code", 5) != 0)
			continue;
		// A few files lose a code's closing quote: it then ends with the line
		char *open = strchr(end, '"');
		if (!open) {
			printf("%s\t%lu\tNOQUOTE\t-\t-\t\n", file, n);
			continue;
		}
		char *close = strrchr(open + 1, '"');
		if (!close)
			close = open + 1 + strcspn(open + 1, "\r\n");
		decode(&d, file, n, open + 1, (u32)(close - open - 1));
	}
	fclose(f);
}

int main(int argc, char **argv)
{
	int i;
	unsigned long total = 0;
	for (i = 1; i < argc; ++i)
		read_file(argv[i]);
	for (i = 0; i < 5; ++i)
		total += by_status[i];
	fprintf(stderr, "%lu cheats in %d files\n", total, argc - 1);
	for (i = 0; i < 5; ++i)
		fprintf(stderr, "  %-12s %5lu\n", status_names[i], by_status[i]);
	fprintf(stderr, "OK cheats by lowering:\n");
	for (i = 0; i < 5; ++i)
		fprintf(stderr, "  %-18s %5lu\n", lower_names[i], by_lower[i]);
	fprintf(stderr, "  (lowered: %lu table bytes, %lu cycles, %lu ROM patches)\n", lowered_bytes, lowered_cycles,
		lowered_patches);
	fprintf(stderr, "OK cheats by format:\n");
	for (i = 1; i < 7; ++i)
		fprintf(stderr, "  %-12s %5lu\n", format_names[i], by_format[i]);
	fprintf(stderr, "  (read alone (no key, or format detected again): %lu)\n", alone_reads);
	fprintf(stderr, "split readings that differ: %lu (and %lu whose writes join into runs)\n", split_differ,
		split_joined);
	return split_differ ? 1 : 0;
}
