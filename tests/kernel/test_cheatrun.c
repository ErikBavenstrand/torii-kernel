// The in-game cheat interpreter (source/cheatrun.s) against mGBA's semantics
// (tests/kernel/cheat_ref.h) on the CPU it runs on: random cheats of every op
// type, odd addresses included, run by both on the same memory; and its
// cycles against the budget's model (cheatcode.h's CHEAT_COST_*).
#include <string.h>

#include <gba_base.h>
#include <gba_interrupt.h>

#include "../../source/GBApatch.h"
#include "../../source/cheatcode.h"
#include "../../source/engine/cycles.h"
#include "../../source/waitcnt.h"
#include "torii_test.h"
#include "tests.h"

extern u8 pReadCache[];

static s32 ref_read(u32 a, u32 w)
{
	u32 v, r;
	if (w == 1)
		return *(vu8 *)a;
	if (w == 2) {
		v = *(vu16 *)(a & ~1u);
		r = (a & 1) * 8;
	} else {
		v = *(vu32 *)(a & ~3u);
		r = (a & 3) * 8;
	}
	return (s32)(r ? (v >> r) | (v << (32 - r)) : v);
}

static void ref_write(u32 a, u32 w, u32 v)
{
	if (w == 1)
		*(vu8 *)a = (u8)v;
	else if (w == 2)
		*(vu16 *)(a & ~1u) = (u16)v;
	else
		*(vu32 *)(a & ~3u) = v;
}

#define CHEAT_REF_READ ref_read
#define CHEAT_REF_WRITE ref_write
#include "cheat_ref.h"

// pReadCache, which the unit tests own: the memory cheats change, the
// pointers they follow, the snapshots and the table
#define T_BASE ((u32)pReadCache + 0x10000)
#define T_DATA T_BASE
#define T_DATA_LEN 0x400
#define T_PTRS (T_BASE + T_DATA_LEN)  // 4 pointers into the data
#define T_LEN (T_DATA_LEN + 16)
#define T_SNAP (T_BASE + 0x800)
#define T_WANT (T_BASE + 0xC40)
#define T_OPS ((struct cheat_op *)(T_BASE + 0x1400))  // 4 cheats of 16 ops
#define T_PROG ((u32 *)(T_BASE + 0x2000))
#define T_PROG_MAX 0x1800                     // words
#define T_PATCHES ((struct cheat_patch *)(T_BASE + 0x8000))
#define T_CHEAT_OPS 16

static u32 rng;

static u32 rnd(u32 n)
{
	rng ^= rng << 13;
	rng ^= rng >> 17;
	rng ^= rng << 5;
	return n ? rng % n : rng;
}

static u32 mask_of(u32 w)
{
	return w == 4 ? 0xFFFFFFFFu : (1u << (8 * w)) - 1;
}

static u8 rnd_width(void)
{
	static const u8 widths[3] = {1, 2, 4};
	return widths[rnd(3)];
}

// One random op
static void random_op(struct cheat_op *op)
{
	static const u8 conds[] = {CHEAT_OP_IF_EQ,    CHEAT_OP_IF_NE,    CHEAT_OP_IF_LT,  CHEAT_OP_IF_GT,
				   CHEAT_OP_IF_ULT,   CHEAT_OP_IF_UGT,   CHEAT_OP_IF_AND, CHEAT_OP_IF_NAND,
				   CHEAT_OP_IF_NEVER, CHEAT_OP_IF_BUTTON};
	u32 kind = rnd(20);
	memset(op, 0, sizeof(*op));
	op->width = rnd_width();
	op->address = T_DATA + 0x40 + rnd(T_DATA_LEN - 0x80);
	op->value = rnd(0) & mask_of(op->width);
	op->count = 1;
	if (kind < 6) {
		op->type = CHEAT_OP_WRITE;
		if (kind < 2) {  // a fill or slide
			op->count = rnd(5);
			op->addr_step = (s32)rnd(17) - 8;
			op->value_step = (s32)rnd(9) - 4;
		}
	} else if (kind < 8) {
		op->type = CHEAT_OP_WRITE_PTR;
		op->address = T_PTRS + 4 * rnd(4);
		op->addr_step = (s32)rnd(33) - 16;
	} else if (kind < 11) {
		static const u8 math[3] = {CHEAT_OP_ADD, CHEAT_OP_OR, CHEAT_OP_AND};
		op->type = math[kind - 8];
		op->count = 1 + rnd(3);
		op->addr_step = (s32)rnd(9) - 4;
		op->value_step = (s32)rnd(5) - 2;
	} else if (kind < 18) {
		op->type = conds[rnd(sizeof(conds))];
		op->count = rnd(5);
		op->else_count = (u16)(rnd(3) ? 0 : rnd(4));
		if (rnd(2))  // often true: the memory's own value
			op->value = (u32)ref_read(op->address, op->width) & mask_of(op->width);
		if (op->type == CHEAT_OP_IF_BUTTON || op->type == CHEAT_OP_IF_NEVER) {
			op->width = 0;
			op->address = 0;
			op->value = 0;
		}
	} else if (kind < 19) {
		op->type = CHEAT_OP_ROM_PATCH;
		op->width = 2;
		op->address = 0x08001000;
	} else {
		op->type = CHEAT_OP_SLOWDOWN;
		op->width = 0;
		op->address = 0;
	}
}

static void set_memory(void)
{
	for (u32 i = 0; i < T_DATA_LEN; i += 4)
		*(vu32 *)(T_DATA + i) = rnd(0);
	*(vu32 *)(T_PTRS + 0) = T_DATA + 0x100;
	*(vu32 *)(T_PTRS + 4) = T_DATA + 0x181;  // odd: an odd write's rules
	*(vu32 *)(T_PTRS + 8) = T_DATA + 0x202;
	*(vu32 *)(T_PTRS + 12) = T_DATA + 0x2F3;
}

void test_cheatrun_matches_mgba(void)
{
	u32 failures = 0, runs = 0, words = 0, n_patches, sets = 300;
	rng = 0x7019C0DE;
	for (u32 set = 0; set < sets && failures < 3; set++) {
		u32 cheats = 1 + rnd(4), n[4], pos = 0;
		set_memory();
		for (u32 c = 0; c < cheats; c++) {
			n[c] = 1 + rnd(T_CHEAT_OPS);
			for (u32 i = 0; i < n[c]; i++)
				random_op(&T_OPS[c * T_CHEAT_OPS + i]);
			u32 got, cost;
			u8 status = cheat_lower(&T_OPS[c * T_CHEAT_OPS], n[c], T_PROG + pos, T_PROG_MAX - 1 - pos, &got,
						&cost, T_PATCHES, 64, &n_patches);
			if (status != CHEAT_LOWER_OK) {
				tlog_error("FAIL cheatrun set %lu cheat %lu: lowered to %u", (unsigned long)set,
					   (unsigned long)c, status);
				failures++;
				got = 0;
				n[c] = 0;
			}
			pos += got;
		}
		T_PROG[pos] = 0;
		words += pos + 1;
		memcpy((void *)T_SNAP, (void *)T_DATA, T_LEN);
		for (u32 frame = 0; frame < 2; frame++)
			for (u32 c = 0; c < cheats; c++)
				cheat_ref_run(&T_OPS[c * T_CHEAT_OPS], n[c]);
		memcpy((void *)T_WANT, (void *)T_DATA, T_LEN);
		memcpy((void *)T_DATA, (void *)T_SNAP, T_LEN);
		for (u32 frame = 0; frame < 2; frame++)
			Cheat_run(T_PROG, 0, 0, 0);
		runs++;
		for (u32 i = 0; i < T_LEN; i++)
			if (*(vu8 *)(T_DATA + i) != *(vu8 *)(T_WANT + i)) {
				tlog_error("FAIL cheatrun set %lu: byte 0x%lX is %02X, mGBA's %02X", (unsigned long)set,
					   (unsigned long)i, *(vu8 *)(T_DATA + i), *(vu8 *)(T_WANT + i));
				for (u32 c = 0; c < cheats; c++)
					for (u32 k = 0; k < n[c]; k++) {
						const struct cheat_op *op = &T_OPS[c * T_CHEAT_OPS + k];
						tlog("cheatrun: cheat %lu op %lu: type %u w %u %08lX = %08lX x%lu step "
						     "%ld/%ld else %u",
						     (unsigned long)c, (unsigned long)k, op->type, op->width,
						     (unsigned long)(op->address - T_DATA), (unsigned long)op->value,
						     (unsigned long)op->count, (long)op->addr_step,
						     (long)op->value_step, op->else_count);
					}
				failures++;
				break;
			}
	}
	tlog("cheatrun: %lu random cheat sets (%lu table words) ran as mGBA's", (unsigned long)runs,
	     (unsigned long)words);
	CHECK_EQ(failures, 0);
}

// --- Cycles -----------------------------------------------------------------

struct worst {
	const char *name;
	u32 cheats, ops;  // cheats of ops each
	void (*make)(struct cheat_op *op, u32 i, u32 n);
};

static void make_write(struct cheat_op *op, u32 i, u32 n)
{
	(void)n;
	*op = (struct cheat_op){T_DATA + (i & 0xFF) * 4, i, 1, 0, 0, 0, CHEAT_OP_WRITE, 1};
}

// A write of each width in turn: no runs
static void make_mixed(struct cheat_op *op, u32 i, u32 n)
{
	(void)n;
	*op = (struct cheat_op){T_DATA + (i & 0xFF) * 4, i, 1, 0, 0, 0, CHEAT_OP_WRITE, (u8)(1u << (i % 3))};
}

// A condition that holds, then writes it covers
static void make_cond_writes(struct cheat_op *op, u32 i, u32 n)
{
	if (i == 0)
		*op = (struct cheat_op){T_DATA, 0, n - 1, 0, 0, 0, CHEAT_OP_IF_NAND, 4};  // x & 0 is 0: true
	else
		make_write(op, i, n);
}

// Conditions each covering the rest, all true
static void make_nested(struct cheat_op *op, u32 i, u32 n)
{
	*op = (struct cheat_op){T_DATA, 0, n - 1 - i, 0, 0, 0, CHEAT_OP_IF_NAND, 4};
	if (i == n - 1)
		make_write(op, i, n);
}

// Conditions with ELSE blocks of one op: the else jumps
static void make_else(struct cheat_op *op, u32 i, u32 n)
{
	if (i % 3 == 0 && i + 2 < n)
		*op = (struct cheat_op){T_DATA, 0, 1, 0, 0, 1, CHEAT_OP_IF_NAND, 4};
	else
		make_write(op, i, n);
}

// A false condition skipping all but the last op: the walk
static void make_skip(struct cheat_op *op, u32 i, u32 n)
{
	if (i == 0)
		*op = (struct cheat_op){T_DATA, 0, n - 2, 0, 0, 0, CHEAT_OP_IF_NEVER, 0};  // the last op runs
	else
		make_write(op, i, n);
}

static void make_fill(struct cheat_op *op, u32 i, u32 n)
{
	(void)i;
	(void)n;
	*op = (struct cheat_op){T_DATA, 0x1234, 400, 2, 1, 0, CHEAT_OP_WRITE, 2};
}

static void make_fill_const(struct cheat_op *op, u32 i, u32 n)
{
	(void)i;
	(void)n;
	*op = (struct cheat_op){T_DATA, 0x63, 400, 1, 0, 0, CHEAT_OP_WRITE, 1};
}

static void make_add(struct cheat_op *op, u32 i, u32 n)
{
	(void)n;
	*op = (struct cheat_op){T_DATA + (i & 0xFF) * 4, 1, 1, 0, 0, 0, CHEAT_OP_ADD, 4};
}

static void make_add_slide(struct cheat_op *op, u32 i, u32 n)
{
	(void)i;
	(void)n;
	*op = (struct cheat_op){T_DATA, 1, 100, 4, 0, 0, CHEAT_OP_ADD, 4};
}

static void make_ptr(struct cheat_op *op, u32 i, u32 n)
{
	(void)n;
	*op = (struct cheat_op){T_PTRS, i, 1, 4, 0, 0, CHEAT_OP_WRITE_PTR, 2};
}

static const struct worst worsts[] = {
	{"plain byte writes", 3, 200, make_write},     {"one write each", 60, 1, make_write},
	{"mixed widths", 3, 150, make_mixed},          {"writes under a condition", 3, 150, make_cond_writes},
	{"nested conditions", 3, 50, make_nested},     {"else blocks", 3, 90, make_else},
	{"skipped by a condition", 3, 150, make_skip}, {"slides", 2, 1, make_fill},
	{"constant fills", 2, 1, make_fill_const},     {"adds", 3, 60, make_add},
	{"add slides", 2, 1, make_add_slide},          {"pointer writes", 3, 60, make_ptr},
};

void test_cheatrun_cycles(void)
{
	static const u16 waits[2] = {WAITCNT_BIOS, 0x4317};  // the BIOS's, and the commonest game setting
	u32 worst_ratio = 0;
	cycles_init();
	{
		u16 ime = REG_IME, saved = REG_WAITCNT_CART;
		REG_IME = 0;
		REG_WAITCNT_CART = WAITCNT_BIOS;
		T_PROG[0] = 0;
		u32 start = cycles_now();
		Cheat_run(T_PROG, 0, 0, 0);
		u32 took = cycles_since(start);
		REG_WAITCNT_CART = saved;
		REG_IME = ime;
		tlog("cheat cycles: an empty table: %lu cycles, model %u", (unsigned long)took, CHEAT_COST_CALL);
		CHECK(took <= CHEAT_COST_CALL);
	}
	for (u32 k = 0; k < sizeof(worsts) / sizeof(worsts[0]); k++) {
		const struct worst *w = &worsts[k];
		u32 pos = 0, model = CHEAT_COST_CALL, n_patches, ok = 1;
		set_memory();
		for (u32 c = 0; c < w->cheats; c++) {
			struct cheat_op *ops = (struct cheat_op *)(T_BASE + 0x9000);
			for (u32 i = 0; i < w->ops; i++)
				w->make(&ops[i], i, w->ops);
			u32 got, cost;
			if (cheat_lower(ops, w->ops, T_PROG + pos, T_PROG_MAX - 1 - pos, &got, &cost, T_PATCHES, 1,
					&n_patches) != CHEAT_LOWER_OK) {
				ok = 0;
				break;
			}
			pos += got;
			model += cost;
		}
		CHECK(ok);
		if (!ok)
			continue;
		T_PROG[pos] = 0;
		u32 took[2];
		u16 ime = REG_IME;
		REG_IME = 0;
		for (u32 v = 0; v < 2; v++) {
			u16 saved = REG_WAITCNT_CART;
			REG_WAITCNT_CART = waits[v];
			u32 start = cycles_now();
			Cheat_run(T_PROG, 0, 0, 0);
			took[v] = cycles_since(start);
			REG_WAITCNT_CART = saved;
		}
		REG_IME = ime;
		tlog("cheat cycles: %s: %lu ops, %lu bytes: %lu cycles (%lu at WAITCNT 4317), model %lu", w->name,
		     (unsigned long)(w->cheats * w->ops), (unsigned long)(pos + 1) * 4, (unsigned long)took[0],
		     (unsigned long)took[1], (unsigned long)model);
		CHECK(took[0] <= model);
		u32 ratio = model * 100 / (took[0] ? took[0] : 1);
		if (ratio > worst_ratio)
			worst_ratio = ratio;
	}
	tlog("cheat cycles: the model is at most %lu%% of the measured", (unsigned long)worst_ratio);
}

// The off mask (v0.8's in-game toggles): a cheat whose bit is set doesn't
// run, a cheat past the 64th always does, and a cheat that is off costs
// CHEAT_COST_SKIP at most
void test_cheatrun_off(void)
{
	enum {
		N = 70
	};  // past the mask's 64
	u32 pos = 0, n_patches;
	set_memory();
	for (u32 c = 0; c < N; c++) {
		struct cheat_op op = {T_DATA + c * 4, 0xA0 + c, 1, 0, 0, 0, CHEAT_OP_WRITE, 1};
		u32 got, cost;
		CHECK_EQ(cheat_lower(&op, 1, T_PROG + pos, T_PROG_MAX - 1 - pos, &got, &cost, T_PATCHES, 1, &n_patches),
			 CHEAT_LOWER_OK);
		pos += got;
	}
	T_PROG[pos] = 0;
	// Cheats 0, 33 and 63 off
	for (u32 i = 0; i < N * 4; i += 4)
		*(vu32 *)(T_DATA + i) = 0;
	Cheat_run(T_PROG, 1u << 0, (1u << 1) | (1u << 31), 0);
	u32 wrong = 0;
	for (u32 c = 0; c < N; c++) {
		u32 off = c == 0 || c == 33 || c == 63;
		wrong += *(vu8 *)(T_DATA + c * 4) != (off ? 0 : 0xA0 + c);
	}
	CHECK_EQ(wrong, 0);
	// Every one off: none of the first 64 runs, the rest do
	for (u32 i = 0; i < N * 4; i += 4)
		*(vu32 *)(T_DATA + i) = 0;
	u16 ime = REG_IME, saved = REG_WAITCNT_CART;
	REG_IME = 0;
	REG_WAITCNT_CART = WAITCNT_BIOS;
	u32 start = cycles_now();
	Cheat_run(T_PROG, ~0u, ~0u, 0);
	u32 took = cycles_since(start);
	REG_WAITCNT_CART = saved;
	REG_IME = ime;
	wrong = 0;
	for (u32 c = 0; c < N; c++)
		wrong += *(vu8 *)(T_DATA + c * 4) != (c < 64 ? 0 : 0xA0 + c);
	CHECK_EQ(wrong, 0);
	u32 model = CHEAT_COST_CALL + 64 * CHEAT_COST_SKIP + (N - 64) * (CHEAT_COST_CHEAT + CHEAT_COST_SINGLE);
	tlog("cheat cycles: 64 cheats off and %u on: %lu cycles, model %lu", N - 64, (unsigned long)took,
	     (unsigned long)model);
	CHECK(took <= model);
}

// Run once (the in-game menu's SELECT): the cheat runs whatever its off bit,
// past its gate's ops (the header's CHEAT_PROG_GATE), and no other with it
void test_cheatrun_once(void)
{
	const u32 keys = T_DATA + 0x200, now = T_DATA + 0x204;  // stand-ins for KEYINPUT and the keys before
	struct cheat_op ops[3][3] = {
		// Hold A: the gate, then the write
		{{keys, 1, 1, 0, 0, 0, CHEAT_OP_IF_NAND, 2}, {T_DATA + 0x100, 0x55, 1, 0, 0, 0, CHEAT_OP_WRITE, 1}},
		// Always: a plain write
		{{T_DATA + 0x104, 0x66, 1, 0, 0, 0, CHEAT_OP_WRITE, 1}},
		// Press A: two gate ops, then the write
		{{now, 1, 2, 0, 0, 0, CHEAT_OP_IF_NAND, 2},
		 {keys, 1, 1, 0, 0, 0, CHEAT_OP_IF_AND, 2},
		 {T_DATA + 0x108, 0x77, 1, 0, 0, 0, CHEAT_OP_WRITE, 1}},
	};
	const u32 n_ops[3] = {2, 1, 3}, gate[3] = {1, 0, 2}, at[3] = {0x100, 0x104, 0x108};
	const u8 value[3] = {0x55, 0x66, 0x77};
	u32 pos = 0, n_patches;
	set_memory();
	for (u32 c = 0; c < 3; c++) {
		u32 got, cost;
		CHECK_EQ(cheat_lower(ops[c], n_ops[c], T_PROG + pos, T_PROG_MAX - 1 - pos, &got, &cost, T_PATCHES, 1,
				     &n_patches),
			 CHEAT_LOWER_OK);
		T_PROG[pos] |= gate[c] << CHEAT_PROG_GATE_SHIFT;
		pos += got;
	}
	T_PROG[pos] = 0;
	*(vu16 *)keys = 1;  // A not held, now or before
	*(vu16 *)now = 1;
	// once 0, all on: the gates hold their cheats back
	for (u32 i = 0; i < 3; i++)
		*(vu8 *)(T_DATA + at[i]) = 0;
	Cheat_run(T_PROG, 0, 0, 0);
	CHECK_EQ(*(vu8 *)(T_DATA + 0x100), 0);
	CHECK_EQ(*(vu8 *)(T_DATA + 0x104), 0x66);
	CHECK_EQ(*(vu8 *)(T_DATA + 0x108), 0);
	// Each one alone, all off: only it, past its gate
	for (u32 c = 0; c < 3; c++) {
		for (u32 i = 0; i < 3; i++)
			*(vu8 *)(T_DATA + at[i]) = 0;
		Cheat_run(T_PROG, ~0u, ~0u, c + 1);
		for (u32 i = 0; i < 3; i++)
			CHECK_EQ(*(vu8 *)(T_DATA + at[i]), i == c ? value[i] : 0);
	}
}
