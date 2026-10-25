// Turns decoded cheat ops (source/cheatcode.h) into what v0.7's cheat handler
// runs: constant byte writes and ROM patches.
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
	case CHEAT_OP_WRITE:
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

static int needs_interpreter(u8 type)
{
	return type != CHEAT_OP_WRITE && type != CHEAT_OP_ROM_PATCH;
}

struct out {
	struct cheat_write *writes;
	u32 writes_max, n_writes;
	struct cheat_patch *patches;
	u32 patches_max, n_patches, first_patch;
};

// One byte; returns a status
static u8 put_byte(struct out *o, u32 address, u8 value)
{
	if (is_rom(address)) {
		u32 offset = (address - ROM_BASE) & ~1u;
		u32 shift = (address & 1) * 8;
		struct cheat_patch *p = o->n_patches > o->first_patch ? &o->patches[o->n_patches - 1] : NULL;
		if (!p || p->offset != offset) {
			if (o->n_patches >= o->patches_max)
				return CHEAT_LOWER_TOO_MANY_PATCHES;
			p = &o->patches[o->n_patches++];
			p->offset = offset;
			p->value = 0;
			p->mask = 0;
		}
		p->value = (u16)((p->value & ~(0xFFu << shift)) | ((u32)value << shift));
		p->mask |= (u16)(0xFFu << shift);
		return CHEAT_LOWER_OK;
	}
	if (o->n_writes >= o->writes_max)
		return CHEAT_LOWER_TOO_MANY_WRITES;
	o->writes[o->n_writes].address = address;
	o->writes[o->n_writes].value = value;
	++o->n_writes;
	return CHEAT_LOWER_OK;
}

u8 cheat_lower(const struct cheat_op *ops, u32 n_ops, struct cheat_write *writes, u32 writes_max, u32 *n_writes,
	       struct cheat_patch *patches, u32 patches_max, u32 *n_patches)
{
	struct out o = {writes, writes_max, *n_writes, patches, patches_max, *n_patches, *n_patches};
	u32 i, k, b;

	for (i = 0; i < n_ops; ++i)
		if (!cheat_op_address_ok(&ops[i]))
			return CHEAT_LOWER_BAD_ADDRESS;
	for (i = 0; i < n_ops; ++i)
		if (needs_interpreter(ops[i].type))
			return CHEAT_LOWER_NEEDS_INTERPRETER;

	for (i = 0; i < n_ops; ++i) {
		const struct cheat_op *op = &ops[i];
		u32 count = op->type == CHEAT_OP_WRITE ? op->count : 1;
		u32 address = op->address;
		u32 value = op->value;
		for (k = 0; k < count; ++k) {
			for (b = 0; b < op->width; ++b) {
				u8 status = put_byte(&o, address + b, (u8)(value >> (8 * b)));
				if (status != CHEAT_LOWER_OK)
					return status;
			}
			address += (u32)op->addr_step;
			value += (u32)op->value_step;
		}
	}
	*n_writes = o.n_writes;
	*n_patches = o.n_patches;
	return CHEAT_LOWER_OK;
}
