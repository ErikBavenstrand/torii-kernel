// A reference for the in-game cheat interpreter (source/cheatrun.s): mGBA's
// mCheatRefresh (src/core/cheats.c) ported line for line onto Torii's ops
// (source/cheatcode.h), with Torii's choices for what mGBA leaves to the
// frontend: IF_BUTTON reads CHEAT_BUTTON_KEYS on KEYINPUT, and ROM patches,
// ROM writes and slowdown codes do nothing each frame (they are applied at
// launch, or not at all). Where the devices differ from mGBA, it follows
// them: IF_LT and IF_GT compare signed at the width. The tests compare the table cheat_lower builds,
// run by the interpreter, with this.
//
// The includer defines, before including it:
//   s32 CHEAT_REF_READ(u32 address, u32 width): a bus read, rotated as the
//       CPU's (and mGBA's) for an odd halfword or word address
//   void CHEAT_REF_WRITE(u32 address, u32 width, u32 value): a bus write,
//       aligned as the CPU's
#ifndef TORII_CHEAT_REF_H
#define TORII_CHEAT_REF_H

#include "../../source/cheatcode.h"

// One cheat's ops, as one of mGBA's cheat sets
static inline void cheat_ref_run(const struct cheat_op *ops, u32 n)
{
	u32 elseLoc = 0, endLoc = 0, i;
	for (i = 0; i < n; ++i) {
		const struct cheat_op *cheat = &ops[i];
		s32 value = 0;
		s32 operand = (s32)cheat->value;
		u32 operationsRemaining = cheat->count;
		u32 address = cheat->address;
		u32 width = cheat->width;
		int performAssignment = 0;
		int condition = 1;
		int conditionRemaining = 0;
		int negativeConditionRemaining = 0;
		u8 type = cheat->type;

		if (type == CHEAT_OP_ROM_PATCH || type == CHEAT_OP_SLOWDOWN ||
		    (type == CHEAT_OP_WRITE && address - 0x08000000u < 0x02000000u))
			operationsRemaining = 0;
		if (type == CHEAT_OP_IF_BUTTON) {
			type = CHEAT_OP_IF_NAND;
			address = CHEAT_KEYINPUT;
			width = 2;
			operand = CHEAT_BUTTON_KEYS;
		}
		for (; operationsRemaining; --operationsRemaining) {
			switch (type) {
			case CHEAT_OP_WRITE:
				value = operand;
				performAssignment = 1;
				break;
			case CHEAT_OP_WRITE_PTR:
				value = operand;
				address = (u32)CHEAT_REF_READ(address, 4) + (u32)cheat->addr_step;
				performAssignment = 1;
				break;
			case CHEAT_OP_AND:
				value = CHEAT_REF_READ(address, width) & operand;
				performAssignment = 1;
				break;
			case CHEAT_OP_ADD:
				value = (s32)((u32)CHEAT_REF_READ(address, width) + (u32)operand);
				performAssignment = 1;
				break;
			case CHEAT_OP_OR:
				value = CHEAT_REF_READ(address, width) | operand;
				performAssignment = 1;
				break;
			case CHEAT_OP_KEYED: {  // Torii's (AnyGBA's raw extensions): not mGBA's
				u32 key = cheat->key;
				if (cheat->flags & CHEAT_KEYED_TARGET_PTR)
					address = (u32)CHEAT_REF_READ(address, 4);
				address += (u32)cheat->addr_step;
				value = operand;
				if (cheat->flags & CHEAT_KEYED_KEY) {
					if (cheat->flags & CHEAT_KEYED_KEY_PTR)
						key = (u32)CHEAT_REF_READ(key, 4);
					value ^= CHEAT_REF_READ(key + cheat->key_offset, width);
				}
				if (cheat->flags & CHEAT_KEYED_OR)
					value |= CHEAT_REF_READ(address, width);
				CHEAT_REF_WRITE(address, width, (u32)value);
				operationsRemaining = 1;  // once, past the step below
				break;
			}
			case CHEAT_OP_IF_EQ:
			case CHEAT_OP_IF_NE:
			case CHEAT_OP_IF_LT:
			case CHEAT_OP_IF_GT:
			case CHEAT_OP_IF_ULT:
			case CHEAT_OP_IF_UGT:
			case CHEAT_OP_IF_AND:
			case CHEAT_OP_IF_NAND:
			case CHEAT_OP_IF_NEVER: {
				s32 r = type == CHEAT_OP_IF_NEVER ? 0 : CHEAT_REF_READ(address, width);
				if ((type == CHEAT_OP_IF_LT || type == CHEAT_OP_IF_GT) && width < 4) {
					// Torii's: signed at the width, as the devices compare
					u32 shift = 32 - 8 * width;
					r = (s32)((u32)r << shift);
					operand = (s32)((u32)operand << shift);
				}
				switch (type) {
				case CHEAT_OP_IF_EQ:
					condition = r == operand;
					break;
				case CHEAT_OP_IF_NE:
					condition = r != operand;
					break;
				case CHEAT_OP_IF_LT:
					condition = r < operand;
					break;
				case CHEAT_OP_IF_GT:
					condition = r > operand;
					break;
				case CHEAT_OP_IF_ULT:
					condition = (u32)r < (u32)operand;
					break;
				case CHEAT_OP_IF_UGT:
					condition = (u32)r > (u32)operand;
					break;
				case CHEAT_OP_IF_AND:
					condition = (r & operand) != 0;
					break;
				case CHEAT_OP_IF_NAND:
					condition = !(r & operand);
					break;
				default:
					condition = 0;
					break;
				}
				conditionRemaining = (int)cheat->count;
				negativeConditionRemaining = cheat->else_count;
				operationsRemaining = 1;
				break;
			}
			default:
				break;
			}

			if (performAssignment)
				CHEAT_REF_WRITE(address, width, (u32)value);

			address += (u32)cheat->addr_step;
			operand = (s32)((u32)operand + (u32)cheat->value_step);
		}

		if (elseLoc && i == elseLoc) {
			i = endLoc;
			endLoc = 0;
		}
		if (conditionRemaining > 0 && !condition) {
			i += (u32)conditionRemaining;
		} else if (negativeConditionRemaining > 0) {
			elseLoc = i + (u32)conditionRemaining;
			endLoc = elseLoc + (u32)negativeConditionRemaining;
		}
	}
}

#endif
