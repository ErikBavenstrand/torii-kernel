// Turns decoded cheat ops (source/cheatcode.h) into what the in-game
// interpreter (source/cheatrun.s) runs each frame, and ROM patches.
#include <stddef.h>

#include "cheatcode.h"

#define ROM_BASE 0x08000000u
#define ROM_SIZE 0x02000000u

static const struct {
	u32 base, size;
} regions[] = {
	{0x02000000, 0x1000000},  // EWRAM and its mirrors, as some codes write
	{0x03000000, 0x1000000},  // IWRAM and its mirrors
	{0x04000000, 0x400},     // IO
	{0x05000000, 0x400},     // palette
	{0x06000000, 0x18000},   // VRAM
	{0x07000000, 0x400},     // OAM
	{ROM_BASE, ROM_SIZE},    // ROM
	{0x0E000000, 0x10000},   // SRAM
};

// Whether [first, last] lies in one region
static int range_ok(unsigned long long first, unsigned long long last)
{
	u32 i;
	if (last < first)
		return 0;
	for (i = 0; i < sizeof(regions) / sizeof(regions[0]); ++i)
		if (first >= regions[i].base && last < (unsigned long long)regions[i].base + regions[i].size)
			return 1;
	return 0;
}

static int is_rom(u32 address)
{
	return address - ROM_BASE < ROM_SIZE;
}

int cheat_op_address_ok(const struct cheat_op *op)
{
	long long first, last;
	switch (op->type) {
	case CHEAT_OP_IF_BUTTON:
	case CHEAT_OP_IF_NEVER:
	case CHEAT_OP_SLOWDOWN:
		return 1;
	case CHEAT_OP_ROM_PATCH:
		return is_rom(op->address) && range_ok(op->address, (unsigned long long)op->address + op->width - 1);
	case CHEAT_OP_WRITE_PTR:  // the pointer is read; where it points is known only in game
		return range_ok(op->address, (unsigned long long)op->address + 3);
	case CHEAT_OP_KEYED:
		if (!range_ok(op->address, (unsigned long long)op->address +
						   (op->flags & CHEAT_KEYED_TARGET_PTR ? 3 : op->width - 1)))
			return 0;
		if (!(op->flags & CHEAT_KEYED_KEY))
			return 1;
		if (op->flags & CHEAT_KEYED_KEY_PTR)
			return range_ok(op->key, (unsigned long long)op->key + 3);
		return range_ok((unsigned long long)op->key + op->key_offset,
				(unsigned long long)op->key + op->key_offset + op->width - 1);
	case CHEAT_OP_WRITE:
	case CHEAT_OP_ADD:
	case CHEAT_OP_OR:
	case CHEAT_OP_AND:
		if (!op->count)
			return 1;
		first = op->address;
		last = first + (long long)(op->count - 1) * op->addr_step;
		if (last < first) {
			long long swap = first;
			first = last;
			last = swap;
		}
		return first >= 0 && range_ok(first, last + op->width - 1);
	default:
		return range_ok(op->address, (unsigned long long)op->address + op->width - 1);
	}
}

static int is_cond(u8 type)
{
	return type >= CHEAT_OP_IF_EQ && type <= CHEAT_OP_IF_NEVER;
}

int cheat_has_effect(const struct cheat_op *ops, u32 n_ops)
{
	for (u32 i = 0; i < n_ops; ++i)
		switch (ops[i].type) {
		case CHEAT_OP_ROM_PATCH:
			return 1;
		case CHEAT_OP_WRITE:
		case CHEAT_OP_WRITE_PTR:
		case CHEAT_OP_ADD:
		case CHEAT_OP_OR:
		case CHEAT_OP_AND:
		case CHEAT_OP_KEYED:
			if (ops[i].count)
				return 1;
			break;
		default:
			break;
		}
	return 0;
}

// An op Torii can't run as mGBA does (cheat_lower's CHEAT_LOWER_CANT_RUN):
// a write to the ROM in a cheat with conditionals, or an add, or or and on
// the ROM. The address checks have kept each op's elements in one region.
static int cant_run(const struct cheat_op *op, int conditional)
{
	if (!op->count)
		return 0;  // runs nowhere
	switch (op->type) {
	case CHEAT_OP_WRITE:
		return conditional && is_rom(op->address);
	case CHEAT_OP_ADD:
	case CHEAT_OP_OR:
	case CHEAT_OP_AND:
		return is_rom(op->address);
	case CHEAT_OP_KEYED:  // where a pointer points is known only in game
		return !(op->flags & CHEAT_KEYED_TARGET_PTR) && is_rom(op->address);
	default:
		return 0;
	}
}

// What runs in game: not ROM patches, writes to the ROM, slowdown codes, or
// ops whose count is 0 (mGBA's loop doesn't run them, conditionals included)
static int runs(const struct cheat_op *op)
{
	switch (op->type) {
	case CHEAT_OP_ROM_PATCH:
	case CHEAT_OP_SLOWDOWN:
		return 0;
	case CHEAT_OP_WRITE:
		return op->count && !is_rom(op->address);
	default:
		return op->count != 0;
	}
}

struct out {
	struct cheat_patch *patches;
	u32 patches_max, n_patches;
	u32 *prog;
	u32 prog_max, n;
	u32 cost;
	u8 status;
};

// One byte into the ROM
static void put_patch_byte(struct out *o, u32 address, u8 value)
{
	u32 offset = (address - ROM_BASE) & ~1u;
	u32 shift = (address & 1) * 8;
	struct cheat_patch *p = o->n_patches ? &o->patches[o->n_patches - 1] : NULL;
	if (!p || p->offset != offset) {
		if (o->n_patches >= o->patches_max) {
			o->status = CHEAT_LOWER_TOO_MANY_PATCHES;
			return;
		}
		p = &o->patches[o->n_patches++];
		p->offset = offset;
		p->value = 0;
		p->mask = 0;
	}
	p->value = (u16)((p->value & ~(0xFFu << shift)) | ((u32)value << shift));
	p->mask |= (u16)(0xFFu << shift);
}

static void patches_of(struct out *o, const struct cheat_op *op)
{
	u32 count, address = op->address, value = op->value, k, b;
	if (op->type == CHEAT_OP_ROM_PATCH)
		count = 1;
	else if (op->type == CHEAT_OP_WRITE && is_rom(op->address))
		count = op->count;
	else
		return;
	for (k = 0; k < count && o->status == CHEAT_LOWER_OK; ++k) {
		for (b = 0; b < op->width; ++b)
			// Aligned down, as a CPU store and mGBA's ROM patches are
			put_patch_byte(o, (address & ~(u32)(op->width - 1)) + b, (u8)(value >> (8 * b)));
		address += (u32)op->addr_step;
		value += (u32)op->value_step;
	}
}

static void word(struct out *o, u32 w)
{
	if (o->n >= o->prog_max) {
		o->status = CHEAT_LOWER_TOO_BIG;
		return;
	}
	o->prog[o->n++] = w;
}

static u32 log2_width(u32 width)
{
	return width == 4 ? 2 : width == 2 ? 1 : 0;
}

static const u8 prog_types[] = {
	[CHEAT_OP_WRITE] = CHEAT_PROG_WRITE,    [CHEAT_OP_WRITE_PTR] = CHEAT_PROG_PTR,
	[CHEAT_OP_ADD] = CHEAT_PROG_ADD,        [CHEAT_OP_OR] = CHEAT_PROG_OR,
	[CHEAT_OP_AND] = CHEAT_PROG_AND,        [CHEAT_OP_IF_EQ] = CHEAT_PROG_EQ,
	[CHEAT_OP_IF_NE] = CHEAT_PROG_NE,       [CHEAT_OP_IF_LT] = CHEAT_PROG_LT,
	[CHEAT_OP_IF_GT] = CHEAT_PROG_GT,       [CHEAT_OP_IF_ULT] = CHEAT_PROG_ULT,
	[CHEAT_OP_IF_UGT] = CHEAT_PROG_UGT,     [CHEAT_OP_IF_AND] = CHEAT_PROG_AND_TRUE,
	[CHEAT_OP_IF_NAND] = CHEAT_PROG_NAND,   [CHEAT_OP_IF_BUTTON] = CHEAT_PROG_NAND,
	[CHEAT_OP_IF_NEVER] = CHEAT_PROG_NEVER,
};

// A write of its own: two words
static int single(const struct cheat_op *op)
{
	return op->type == CHEAT_OP_WRITE && op->count == 1 && !is_rom(op->address);
}

// One op as it runs; nothing (a NOP) for one that doesn't
static void put_op(struct out *o, const struct cheat_op *op)
{
	u32 type, width, address, value, count, k, extra[2], n_extra = 0;
	if (!runs(op)) {
		word(o, CHEAT_PROG_K_EXT << 28);
		word(o, CHEAT_PROG_NOP);
		word(o, 0);
		o->cost += CHEAT_COST_EXT;
		return;
	}
	if (single(op)) {
		word(o, op->address | (log2_width(op->width) << 28));
		word(o, op->value);
		o->cost += CHEAT_COST_SINGLE;
		return;
	}
	if (op->type == CHEAT_OP_KEYED) {
		// Once: the info word's count bits hold the key's offset
		word(o, op->address | ((CHEAT_PROG_K_EXT + 2) << 28));
		word(o, CHEAT_PROG_KEYED | (log2_width(op->width) << 4) | ((u32)op->flags << CHEAT_PROG_KEYED_SHIFT) |
				((u32)op->key_offset << 16));
		word(o, op->value);
		word(o, (u32)op->addr_step);
		word(o, op->key);
		o->cost += CHEAT_COST_KEYED;
		return;
	}
	type = op->type < sizeof(prog_types) ? prog_types[op->type] : CHEAT_PROG_NOP;
	width = op->width;
	address = op->address;
	value = op->value;
	count = op->count;
	if (op->type == CHEAT_OP_IF_BUTTON) {
		width = 2;
		address = CHEAT_KEYINPUT;
		value = CHEAT_BUTTON_KEYS;
	} else if (op->type == CHEAT_OP_IF_NEVER) {
		address = 0;
		width = 1;
		value = 0;
	}
	if (count > CHEAT_PROG_COUNT_MAX) {
		o->status = CHEAT_LOWER_TOO_BIG;
		return;
	}
	if (is_cond(op->type)) {
		if (op->else_count)
			extra[n_extra++] = op->else_count;
		o->cost += CHEAT_COST_COND;
	} else {
		// The steps matter past the first element; a pointer's offset always
		int ptr = op->type == CHEAT_OP_WRITE_PTR;
		if ((count > 1 && (op->addr_step || op->value_step)) || (ptr && op->addr_step))
			extra[n_extra++] = (u32)op->addr_step;
		if (count > 1 && op->value_step)
			extra[n_extra++] = (u32)op->value_step;
		o->cost += CHEAT_COST_EXT + (count - 1) * (op->type != CHEAT_OP_WRITE ? CHEAT_COST_GEN
							   : op->value_step           ? CHEAT_COST_FILL
										      : CHEAT_COST_FILL_SAME);
	}
	word(o, address | ((CHEAT_PROG_K_EXT + n_extra) << 28));
	word(o, type | (log2_width(width) << 4) | (count << 16));
	word(o, value);
	for (k = 0; k < n_extra; ++k)
		word(o, extra[k]);
}

// ROM patches take no place in the table: they are applied at launch, and
// a conditional's counts skip them (cheat_lower). One stays, as a step that
// does nothing, where it is the first op a conditional covers and every op
// that conditional covers is a ROM patch: a conditional covering no ops
// tests nothing (an ELSE after it would run every frame).
static int kept(const struct cheat_op *ops, u32 n_ops, u32 i)
{
	const struct cheat_op *cond = i ? &ops[i - 1] : NULL;
	u32 j;
	if (ops[i].type != CHEAT_OP_ROM_PATCH)
		return 1;
	if (!cond || !is_cond(cond->type) || !cond->count)
		return 0;
	for (j = i; j < n_ops && j < i + cond->count; ++j)
		if (ops[j].type != CHEAT_OP_ROM_PATCH)
			return 0;
	return 1;
}

// The ops of [from, from + count) the table keeps
static u32 kept_in(const struct cheat_op *ops, u32 n_ops, u32 from, u32 count)
{
	u32 n = 0;
	for (u32 j = from; j < n_ops && j - from < count; ++j)
		n += (u32)kept(ops, n_ops, j);
	return n;
}

u8 cheat_lower(const struct cheat_op *ops, u32 n_ops, u32 *prog, u32 prog_max, u32 *prog_words, u32 *cost,
	       struct cheat_patch *patches, u32 patches_max, u32 *n_patches)
{
	struct out o = {patches, patches_max, 0, prog, prog_max, 0, 0, CHEAT_LOWER_OK};
	u32 i, j, n = 0, guarded = 0, walked = 0, until = 0;
	int plain = 1, any = 0;

	*prog_words = *cost = *n_patches = 0;
	for (i = 0; i < n_ops; ++i)
		if (!cheat_op_address_ok(&ops[i]))
			return CHEAT_LOWER_BAD_ADDRESS;
	if (n_ops > CHEAT_PROG_OPS_MAX)
		return CHEAT_LOWER_TOO_BIG;
	for (i = 0; i < n_ops; ++i)
		if (is_cond(ops[i].type))
			plain = 0;
	for (i = 0; i < n_ops; ++i)
		if (cant_run(&ops[i], !plain))
			return CHEAT_LOWER_CANT_RUN;
	for (i = 0; i < n_ops; ++i) {
		patches_of(&o, &ops[i]);
		if (runs(&ops[i]))
			any = 1;
	}
	if (o.status != CHEAT_LOWER_OK)
		return o.status;

	if (any) {
		word(&o, 0);  // the header, filled in below
		o.cost += CHEAT_COST_CHEAT;
		for (i = 0; i < n_ops && o.status == CHEAT_LOWER_OK; ++i) {
			const struct cheat_op *op = &ops[i];
			if (!plain) {
				// Ops a conditional without an ELSE covers run only while
				// it holds: guarded. With an ELSE, one branch always runs.
				// A conditional's counts are of the ops the table keeps
				struct cheat_op c = *op;
				u32 before = o.cost;
				if (!kept(ops, n_ops, i))
					continue;
				if (is_cond(op->type)) {
					c.count = kept_in(ops, n_ops, i + 1, op->count);
					c.else_count = (u16)kept_in(ops, n_ops, i + 1 + op->count, op->else_count);
				}
				put_op(&o, &c);
				if (i < until) {
					guarded += o.cost - before;
					walked += CHEAT_COST_WALK;  // passed over while the condition fails
				}
				if (is_cond(op->type) && !op->else_count && i + 1 + op->count > until)
					until = i + 1 + op->count;
				++n;
				continue;
			}
			if (!runs(op))
				continue;
			// A row of writes of one width: a run
			for (j = i; j < n_ops && single(&ops[j]) && ops[j].width == op->width; ++j)
				;
			if (j - i >= 2) {
				word(&o, (CHEAT_PROG_K_RUN + log2_width(op->width)) << 28 | (j - i));
				o.cost += CHEAT_COST_RUN;
				for (; i < j; ++i) {
					word(&o, ops[i].address);
					word(&o, ops[i].value);
					o.cost += CHEAT_COST_RUN_WRITE;
				}
				--i;
			} else {
				put_op(&o, op);
			}
			++n;
		}
		if (o.status == CHEAT_LOWER_OK && (o.n - 1) * 4 > 0xFFFF)
			o.status = CHEAT_LOWER_TOO_BIG;  // past the header's byte count
		if (o.status == CHEAT_LOWER_OK)
			prog[0] = n | (plain ? CHEAT_PROG_PLAIN : 0) | ((o.n - 1) * 4) << 16;
	}
	if (o.status == CHEAT_LOWER_OK) {
		// Its load, past which it can't run beside the call alone
		u32 always = o.cost - guarded + walked;
		if (always > CHEAT_CYCLES_MAX || o.cost > CHEAT_CYCLES_GATED_MAX)
			o.status = CHEAT_LOWER_TOO_BIG;
		else if (CHEAT_LOAD(always) > o.cost)
			o.cost = CHEAT_LOAD(always);
		if (o.cost > CHEAT_LOAD_MAX - CHEAT_LOAD(CHEAT_COST_CALL))
			o.status = CHEAT_LOWER_TOO_BIG;
	}
	if (o.status != CHEAT_LOWER_OK) {
		o.n = o.cost = o.n_patches = 0;
		*n_patches = 0;
		return o.status;
	}
	*prog_words = o.n;
	*cost = o.cost;
	*n_patches = o.n_patches;
	return CHEAT_LOWER_OK;
}
