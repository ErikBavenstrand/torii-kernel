/* Copyright (c) 2013-2016 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// The GBA cheat decoders, ported from mGBA's src/gba/cheats.c (detection, VBA
// lines, the address plausibility), src/gba/cheats/gameshark.c (GameShark
// v1/v2: cipher, seed tables, reseed, decoding, plausibility), parv3.c (Action
// Replay v3 / GameShark SP) and codebreaker.c (CodeBreaker and its key), with
// the line splitting of src/core/cheats.c's libretro reader. The MPL-2.0
// applies to this file only; the rest of Torii is Apache-2.0.
//
// Torii's changes: ops go into the caller's struct cheat_op array instead of
// mGBA's cheat lists and ROM patch list, with no allocation; master lines
// (hooks, game IDs, keys, seeds, 001DC0DE markers, also in GameShark v1) are
// dropped, as Torii hooks the IRQ itself; a code's text is split by Torii's
// own reader, which also accepts "XXXXXXXX+YYYY" CodeBreaker lines and flags
// digit stand-ins; a conditional that covers the next code covers all of a
// multi-line code's ops (a button, a GameShark list, a CodeBreaker super
// code); an Action Replay v3 block still open at the cheat's end closes there;
// CodeBreaker's type 5 (super code) is decoded; GameShark v1's list writes its
// value to the listed addresses only; a CodeBreaker key line repeated in a
// later cheat keeps the key; a key whose table swap would index past the
// table is refused; a line mGBA rejects or silently skips makes the cheat
// unreadable; a cheat with no key or seed line of its own that decodes to
// addresses outside the GBA's memory is tried again without the file's
// CodeBreaker key and with its GameShark format detected again (libretro's
// files mix them); an Action Replay v3 "80000000 00000000" (add 0 to address
// 0) is padding; "AAAAAAAA:VVVV" with an address past 0x0FFFFFFF is a
// CodeBreaker line; conditional values are masked to their width.
//
// Where mGBA and the devices differ, the devices win (docs/research/cheats.md
// lists the sources): Action Replay v3's C6 and C7 write 16 and 32 bits, its
// "turn off all codes" conditionals and end line read, an empty THEN gets a
// step so its ELSE waits on the condition, and its padding is a code a
// conditional counts; GameShark v1/v2's E0 covers lines, its D and E
// conditionals have VBA-M's kinds, its type 3 VBA-M's adds, its button has a
// slowdown and a 32-bit write; CodeBreaker's D0000010 reads, and a second
// key line, typed raw, becomes the key.
#include <string.h>

#include "cheatcode.h"

static const u8 gsa1_t1[256] = {
	0x31, 0x1C, 0x23, 0xE5, 0x89, 0x8E, 0xA1, 0x37, 0x74, 0x6D, 0x67, 0xFC, 0x1F, 0xC0, 0xB1, 0x94, 0x3B, 0x05,
	0x56, 0x86, 0x00, 0x24, 0xF0, 0x17, 0x72, 0xA2, 0x3D, 0x1B, 0xE3, 0x17, 0xC5, 0x0B, 0xB9, 0xE2, 0xBD, 0x58,
	0x71, 0x1B, 0x2C, 0xFF, 0xE4, 0xC9, 0x4C, 0x5E, 0xC9, 0x55, 0x33, 0x45, 0x7C, 0x3F, 0xB2, 0x51, 0xFE, 0x10,
	0x7E, 0x75, 0x3C, 0x90, 0x8D, 0xDA, 0x94, 0x38, 0xC3, 0xE9, 0x95, 0xEA, 0xCE, 0xA6, 0x06, 0xE0, 0x4F, 0x3F,
	0x2A, 0xE3, 0x3A, 0xE4, 0x43, 0xBD, 0x7F, 0xDA, 0x55, 0xF0, 0xEA, 0xCB, 0x2C, 0xA8, 0x47, 0x61, 0xA0, 0xEF,
	0xCB, 0x13, 0x18, 0x20, 0xAF, 0x3E, 0x4D, 0x9E, 0x1E, 0x77, 0x51, 0xC5, 0x51, 0x20, 0xCF, 0x21, 0xF9, 0x39,
	0x94, 0xDE, 0xDD, 0x79, 0x4E, 0x80, 0xC4, 0x9D, 0x94, 0xD5, 0x95, 0x01, 0x27, 0x27, 0xBD, 0x6D, 0x78, 0xB5,
	0xD1, 0x31, 0x6A, 0x65, 0x74, 0x74, 0x58, 0xB3, 0x7C, 0xC9, 0x5A, 0xED, 0x50, 0x03, 0xC4, 0xA2, 0x94, 0x4B,
	0xF0, 0x58, 0x09, 0x6F, 0x3E, 0x7D, 0xAE, 0x7D, 0x58, 0xA0, 0x2C, 0x91, 0xBB, 0xE1, 0x70, 0xEB, 0x73, 0xA6,
	0x9A, 0x44, 0x25, 0x90, 0x16, 0x62, 0x53, 0xAE, 0x08, 0xEB, 0xDC, 0xF0, 0xEE, 0x77, 0xC2, 0xDE, 0x81, 0xE8,
	0x30, 0x89, 0xDB, 0xFE, 0xBC, 0xC2, 0xDF, 0x26, 0xE9, 0x8B, 0xD6, 0x93, 0xF0, 0xCB, 0x56, 0x90, 0xC0, 0x46,
	0x68, 0x15, 0x43, 0xCB, 0xE9, 0x98, 0xE3, 0xAF, 0x31, 0x25, 0x4D, 0x7B, 0xF3, 0xB1, 0x74, 0xE2, 0x64, 0xAC,
	0xD9, 0xF6, 0xA0, 0xD5, 0x0B, 0x9B, 0x49, 0x52, 0x69, 0x3B, 0x71, 0x00, 0x2F, 0xBB, 0xBA, 0x08, 0xB1, 0xAE,
	0xBB, 0xB3, 0xE1, 0xC9, 0xA6, 0x7F, 0x17, 0x97, 0x28, 0x72, 0x12, 0x6E, 0x91, 0xAE, 0x3A, 0xA2, 0x35, 0x46,
	0x27, 0xF8, 0x12, 0x50};

static const u8 gsa1_t2[256] = {
	0xD8, 0x65, 0x04, 0xC2, 0x65, 0xD5, 0xB0, 0x0C, 0xDF, 0x9D, 0xF0, 0xC3, 0x9A, 0x17, 0xC9, 0xA6, 0xE1, 0xAC,
	0x0D, 0x14, 0x2F, 0x3C, 0x2C, 0x87, 0xA2, 0xBF, 0x4D, 0x5F, 0xAC, 0x2D, 0x9D, 0xE1, 0x0C, 0x9C, 0xE7, 0x7F,
	0xFC, 0xA8, 0x66, 0x59, 0xAC, 0x18, 0xD7, 0x05, 0xF0, 0xBF, 0xD1, 0x8B, 0x35, 0x9F, 0x59, 0xB4, 0xBA, 0x55,
	0xB2, 0x85, 0xFD, 0xB1, 0x72, 0x06, 0x73, 0xA4, 0xDB, 0x48, 0x7B, 0x5F, 0x67, 0xA5, 0x95, 0xB9, 0xA5, 0x4A,
	0xCF, 0xD1, 0x44, 0xF3, 0x81, 0xF5, 0x6D, 0xF6, 0x3A, 0xC3, 0x57, 0x83, 0xFA, 0x8E, 0x15, 0x2A, 0xA2, 0x04,
	0xB2, 0x9D, 0xA8, 0x0D, 0x7F, 0xB8, 0x0F, 0xF6, 0xAC, 0xBE, 0x97, 0xCE, 0x16, 0xE6, 0x31, 0x10, 0x60, 0x16,
	0xB5, 0x83, 0x45, 0xEE, 0xD7, 0x5F, 0x2C, 0x08, 0x58, 0xB1, 0xFD, 0x7E, 0x79, 0x00, 0x34, 0xAD, 0xB5, 0x31,
	0x34, 0x39, 0xAF, 0xA8, 0xDD, 0x52, 0x6A, 0xB0, 0x60, 0x35, 0xB8, 0x1D, 0x52, 0xF5, 0xF5, 0x30, 0x00, 0x7B,
	0xF4, 0xBA, 0x03, 0xCB, 0x3A, 0x84, 0x14, 0x8A, 0x6A, 0xEF, 0x21, 0xBD, 0x01, 0xD8, 0xA0, 0xD4, 0x43, 0xBE,
	0x23, 0xE7, 0x76, 0x27, 0x2C, 0x3F, 0x4D, 0x3F, 0x43, 0x18, 0xA7, 0xC3, 0x47, 0xA5, 0x7A, 0x1D, 0x02, 0x55,
	0x09, 0xD1, 0xFF, 0x55, 0x5E, 0x17, 0xA0, 0x56, 0xF4, 0xC9, 0x6B, 0x90, 0xB4, 0x80, 0xA5, 0x07, 0x22, 0xFB,
	0x22, 0x0D, 0xD9, 0xC0, 0x5B, 0x08, 0x35, 0x05, 0xC1, 0x75, 0x4F, 0xD0, 0x51, 0x2D, 0x2E, 0x5E, 0x69, 0xE7,
	0x3B, 0xC2, 0xDA, 0xFF, 0xF6, 0xCE, 0x3E, 0x76, 0xE8, 0x36, 0x8C, 0x39, 0xD8, 0xF3, 0xE9, 0xA6, 0x42, 0xE6,
	0xC1, 0x4C, 0x05, 0xBE, 0x17, 0xF2, 0x5C, 0x1B, 0x19, 0xDB, 0x0F, 0xF3, 0xF8, 0x49, 0xEB, 0x36, 0xF6, 0x40,
	0x6F, 0xAD, 0xC1, 0x8C};

static const u8 par3_t1[256] = {
	0xD0, 0xFF, 0xBA, 0xE5, 0xC1, 0xC7, 0xDB, 0x5B, 0x16, 0xE3, 0x6E, 0x26, 0x62, 0x31, 0x2E, 0x2A, 0xD1, 0xBB,
	0x4A, 0xE6, 0xAE, 0x2F, 0x0A, 0x90, 0x29, 0x90, 0xB6, 0x67, 0x58, 0x2A, 0xB4, 0x45, 0x7B, 0xCB, 0xF0, 0x73,
	0x84, 0x30, 0x81, 0xC2, 0xD7, 0xBE, 0x89, 0xD7, 0x4E, 0x73, 0x5C, 0xC7, 0x80, 0x1B, 0xE5, 0xE4, 0x43, 0xC7,
	0x46, 0xD6, 0x6F, 0x7B, 0xBF, 0xED, 0xE5, 0x27, 0xD1, 0xB5, 0xD0, 0xD8, 0xA3, 0xCB, 0x2B, 0x30, 0xA4, 0xF0,
	0x84, 0x14, 0x72, 0x5C, 0xFF, 0xA4, 0xFB, 0x54, 0x9D, 0x70, 0xE2, 0xFF, 0xBE, 0xE8, 0x24, 0x76, 0xE5, 0x15,
	0xFB, 0x1A, 0xBC, 0x87, 0x02, 0x2A, 0x58, 0x8F, 0x9A, 0x95, 0xBD, 0xAE, 0x8D, 0x0C, 0xA5, 0x4C, 0xF2, 0x5C,
	0x7D, 0xAD, 0x51, 0xFB, 0xB1, 0x22, 0x07, 0xE0, 0x29, 0x7C, 0xEB, 0x98, 0x14, 0xC6, 0x31, 0x97, 0xE4, 0x34,
	0x8F, 0xCC, 0x99, 0x56, 0x9F, 0x78, 0x43, 0x91, 0x85, 0x3F, 0xC2, 0xD0, 0xD1, 0x80, 0xD1, 0x77, 0xA7, 0xE2,
	0x43, 0x99, 0x1D, 0x2F, 0x8B, 0x6A, 0xE4, 0x66, 0x82, 0xF7, 0x2B, 0x0B, 0x65, 0x14, 0xC0, 0xC2, 0x1D, 0x96,
	0x78, 0x1C, 0xC4, 0xC3, 0xD2, 0xB1, 0x64, 0x07, 0xD7, 0x6F, 0x02, 0xE9, 0x44, 0x31, 0xDB, 0x3C, 0xEB, 0x93,
	0xED, 0x9A, 0x57, 0x05, 0xB9, 0x0E, 0xAF, 0x1F, 0x48, 0x11, 0xDC, 0x35, 0x6C, 0xB8, 0xEE, 0x2A, 0x48, 0x2B,
	0xBC, 0x89, 0x12, 0x59, 0xCB, 0xD1, 0x18, 0xEA, 0x72, 0x11, 0x01, 0x75, 0x3B, 0xB5, 0x56, 0xF4, 0x8B, 0xA0,
	0x41, 0x75, 0x86, 0x7B, 0x94, 0x12, 0x2D, 0x4C, 0x0C, 0x22, 0xC9, 0x4A, 0xD8, 0xB1, 0x8D, 0xF0, 0x55, 0x2E,
	0x77, 0x50, 0x1C, 0x64, 0x77, 0xAA, 0x3E, 0xAC, 0xD3, 0x3D, 0xCE, 0x60, 0xCA, 0x5D, 0xA0, 0x92, 0x78, 0xC6,
	0x51, 0xFE, 0xF9, 0x30};

static const u8 par3_t2[256] = {
	0xAA, 0xAF, 0xF0, 0x72, 0x90, 0xF7, 0x71, 0x27, 0x06, 0x11, 0xEB, 0x9C, 0x37, 0x12, 0x72, 0xAA, 0x65, 0xBC,
	0x0D, 0x4A, 0x76, 0xF6, 0x5C, 0xAA, 0xB0, 0x7A, 0x7D, 0x81, 0xC1, 0xCE, 0x2F, 0x9F, 0x02, 0x75, 0x38, 0xC8,
	0xFC, 0x66, 0x05, 0xC2, 0x2C, 0xBD, 0x91, 0xAD, 0x03, 0xB1, 0x88, 0x93, 0x31, 0xC6, 0xAB, 0x40, 0x23, 0x43,
	0x76, 0x54, 0xCA, 0xE7, 0x00, 0x96, 0x9F, 0xD8, 0x24, 0x8B, 0xE4, 0xDC, 0xDE, 0x48, 0x2C, 0xCB, 0xF7, 0x84,
	0x1D, 0x45, 0xE5, 0xF1, 0x75, 0xA0, 0xED, 0xCD, 0x4B, 0x24, 0x8A, 0xB3, 0x98, 0x7B, 0x12, 0xB8, 0xF5, 0x63,
	0x97, 0xB3, 0xA6, 0xA6, 0x0B, 0xDC, 0xD8, 0x4C, 0xA8, 0x99, 0x27, 0x0F, 0x8F, 0x94, 0x63, 0x0F, 0xB0, 0x11,
	0x94, 0xC7, 0xE9, 0x7F, 0x3B, 0x40, 0x72, 0x4C, 0xDB, 0x84, 0x78, 0xFE, 0xB8, 0x56, 0x08, 0x80, 0xDF, 0x20,
	0x2F, 0xB9, 0x66, 0x2D, 0x60, 0x63, 0xF5, 0x18, 0x15, 0x1B, 0x86, 0x85, 0xB9, 0xB4, 0x68, 0x0E, 0xC6, 0xD1,
	0x8A, 0x81, 0x2B, 0xB3, 0xF6, 0x48, 0xF0, 0x4F, 0x9C, 0x28, 0x1C, 0xA4, 0x51, 0x2F, 0xD7, 0x4B, 0x17, 0xE7,
	0xCC, 0x50, 0x9F, 0xD0, 0xD1, 0x40, 0x0C, 0x0D, 0xCA, 0x83, 0xFA, 0x5E, 0xCA, 0xEC, 0xBF, 0x4E, 0x7C, 0x8F,
	0xF0, 0xAE, 0xC2, 0xD3, 0x28, 0x41, 0x9B, 0xC8, 0x04, 0xB9, 0x4A, 0xBA, 0x72, 0xE2, 0xB5, 0x06, 0x2C, 0x1E,
	0x0B, 0x2C, 0x7F, 0x11, 0xA9, 0x26, 0x51, 0x9D, 0x3F, 0xF8, 0x62, 0x11, 0x2E, 0x89, 0xD2, 0x9D, 0x35, 0xB1,
	0xE4, 0x0A, 0x4D, 0x93, 0x01, 0xA7, 0xD1, 0x2D, 0x00, 0x87, 0xE2, 0x2D, 0xA4, 0xE9, 0x0A, 0x06, 0x66, 0xF8,
	0x1F, 0x44, 0x75, 0xB5, 0x6B, 0x1C, 0xFC, 0x31, 0x09, 0x48, 0xA3, 0xFF, 0x92, 0x12, 0x58, 0xE9, 0xFA, 0xAE,
	0x4F, 0xE2, 0xB4, 0xCC};

static const u32 gsa1_seeds[4] = {0x09F4FBBD, 0x9681884A, 0x352027E9, 0xF3DEE5A7};
static const u32 par3_seeds[4] = {0x7AA9648F, 0x7FAE6994, 0xC0EFAAD5, 0x42712C57};

#define NONE 0xFFFFFFFFu
#define MAX_PENDING 8
#define DEADFACE 0xDEADFACEu
#define MARKER 0x001DC0DEu

// PARv3's fields (mGBA's enum GBAActionReplay3*)
#define PAR3_COND 0x38000000u
#define PAR3_WIDTH 0x06000000u
#define PAR3_ACTION 0xC0000000u
#define PAR3_BASE 0xC0000000u
#define PAR3_WIDTH_BASE 25
#define PAR3_ACTION_NEXT 0x00000000u
#define PAR3_ACTION_NEXT_TWO 0x40000000u
#define PAR3_ACTION_BLOCK 0x80000000u
#define PAR3_ACTION_DISABLE 0xC0000000u
#define PAR3_BASE_ASSIGN 0x00000000u
#define PAR3_BASE_INDIRECT 0x40000000u
#define PAR3_BASE_ADD 0x80000000u
#define PAR3_BASE_OTHER 0xC0000000u

// ------------------------------------------------------------------------
// Ciphers and plausibility (mGBA)

// http://en.wikipedia.org/wiki/Tiny_Encryption_Algorithm
static void gs_decrypt(u32 *op1, u32 *op2, const u32 *seeds)
{
	u32 sum = 0xC6EF3720;
	int i;
	for (i = 0; i < 32; ++i) {
		*op2 -= ((*op1 << 4) + seeds[2]) ^ (*op1 + sum) ^ ((*op1 >> 5) + seeds[3]);
		*op1 -= ((*op2 << 4) + seeds[0]) ^ (*op2 + sum) ^ ((*op2 >> 5) + seeds[1]);
		sum -= 0x9E3779B9;
	}
}

static void gs_reseed(u32 *seeds, u16 params, const u8 *t1, const u8 *t2)
{
	int x, y;
	int s0 = params >> 8;
	int s1 = params & 0xFF;
	for (y = 0; y < 4; ++y) {
		for (x = 0; x < 4; ++x) {
			u8 z = t1[(s0 + x) & 0xFF] + t2[(s1 + y) & 0xFF];
			seeds[y] <<= 8;
			seeds[y] |= z;
		}
	}
}

static void set_gs_version(struct cheat_decoder *d, u8 version)
{
	d->gs_version = version;
	if (version == CHEAT_FMT_GSAV1 || version == CHEAT_FMT_GSAV1_RAW)
		memcpy(d->gs_seeds, gsa1_seeds, sizeof(d->gs_seeds));
	else if (version == CHEAT_FMT_PARV3 || version == CHEAT_FMT_PARV3_RAW)
		memcpy(d->gs_seeds, par3_seeds, sizeof(d->gs_seeds));
}

// GBACheatAddressIsReal
static int address_is_real(u32 address)
{
	u32 offset = address & 0x00FFFFFF;
	switch (address >> 24) {
	case 0x0:
		return -0x80;
	case 0x2:
		return offset > 0x40000 ? -0x40 : 0x20;
	case 0x3:
		return offset > 0x8000 ? -0x40 : 0x20;
	case 0x4:
		return offset > 0x400 ? -0x80 : 0x10;
	case 0x5:
	case 0x7:
		return offset > 0x400 ? -0x80 : -0x8;
	case 0x6:
		return offset > 0x18000 ? -0x80 : -0x8;
	case 0x8:
	case 0x9:
	case 0xA:
	case 0xB:
	case 0xC:
	case 0xD:
		return -0x8;
	case 0xE:
	case 0xF:
		return offset > 0x10000 ? -0x80 : -0x8;
	default:
		return -0xC0;
	}
}

static int gsa_probability(u32 op1, u32 op2)
{
	int probability = 0;
	if (op2 == MARKER)
		return 0x100;
	u32 address = op1 & 0x0FFFFFFF;
	switch (op1 >> 28) {
	case 0x0:
		probability += 0x20;
		if (op2 & 0xFFFFFF00)
			probability -= 0x10;
		probability += address_is_real(address);
		break;
	case 0x1:
		probability += 0x20;
		if (op2 & 0xFFFF0000)
			probability -= 0x10;
		probability += address_is_real(address);
		break;
	case 0x2:
		probability += 0x20;
		probability += address_is_real(address);
		break;
	case 0x6:
		probability += 0x20;
		if (op2 & 0xCFFF0000)
			probability -= 0x10;
		break;
	case 0x8:
		probability += 0x10;
		break;
	case 0xD:
		probability += 0x20;
		if (op2 & 0xFFFF0000)
			probability -= 0x10;
		probability += address_is_real(address);
		break;
	case 0xE:
		probability += 0x20;
		probability += address_is_real(op2);
		if (op1 & 0x0F000000)
			probability -= 0x10;
		break;
	case 0xF:
		probability += 0x20;
		if (op2 & 0xFFFF0000)
			probability -= 0x10;
		break;
	default:
		probability -= 0x40;
	}
	return probability;
}

static u32 par_addr(u32 x)
{
	return (x & 0xFFFFF) | ((x << 4) & 0x0F000000);
}

static int par_probability(u32 op1, u32 op2)
{
	int probability = 0;
	if (op2 == MARKER)
		return 0x100;
	if (op1 == DEADFACE && !(op2 & 0xFFFF0000))
		return 0x100;
	if (!op1) {
		probability += 0x40;
		u32 address = par_addr(op2);
		switch (op2 & 0xFE000000) {
		case 0x80000000:  // fills
		case 0x82000000:
		case 0x84000000:
			probability += address_is_real(address);
			break;
		case 0x18000000:  // ROM patches
		case 0x1A000000:
		case 0x1C000000:
		case 0x1E000000:
			break;
		case 0x00000000:  // end, slowdown, buttons, endif, else
		case 0x08000000:
		case 0x10000000:
		case 0x12000000:
		case 0x14000000:
		case 0x40000000:
		case 0x60000000:
			if (op2 & 0x01000000)
				probability -= 0x40;
			break;
		default:
			probability -= 0x40;
			break;
		}
		return probability;
	}
	u32 width = (op1 & PAR3_WIDTH) >> (PAR3_WIDTH_BASE - 3);
	if (op1 & PAR3_COND) {
		probability += 0x20;
		if (width >= 24)
			return 0;
		if (op2 & ~((1u << width) - 1))
			probability -= 0x10;
	} else {
		u32 address = par_addr(op1);
		probability += 0x20;
		switch (op1 & PAR3_BASE) {
		case PAR3_BASE_ADD:
			if (op2 & ~((1u << width) - 1))
				probability -= 0x10;
			// fall through
		case PAR3_BASE_ASSIGN:
		case PAR3_BASE_INDIRECT:
			probability += address_is_real(address);
			if (op1 & 0x01000000)
				return 0;
			break;
		default:  // PAR3_BASE_OTHER: the hook, and IO writes of 16 (C6) or 32 bits (C7)
			if ((op1 >> 24) == 0xC6 && (op2 & 0xFFFF0000))
				probability -= 0x10;
			else if ((op1 >> 24) != 0xC4 && (op1 >> 24) != 0xC6 && (op1 >> 24) != 0xC7)
				probability -= 0x40;
			break;
		}
	}
	return probability;
}

// GBACheatAddAutodetect's choice, scored on the raw line with the default seeds
static u8 gs_detect(u32 op1, u32 op2, s32 *score)
{
	u32 o1 = op1, o2 = op2;
	int p, max;
	u8 version = CHEAT_FMT_GSAV1;

	gs_decrypt(&o1, &o2, gsa1_seeds);
	max = gsa_probability(o1, o2);
	o1 = op1;
	o2 = op2;
	gs_decrypt(&o1, &o2, par3_seeds);
	p = par_probability(o1, o2);
	if (p > max) {
		max = p;
		version = CHEAT_FMT_PARV3;
	}
	p = gsa_probability(op1, op2);
	if (p > max) {
		max = p;
		version = CHEAT_FMT_GSAV1_RAW;
	}
	p = par_probability(op1, op2);
	if (p > max) {
		max = p;
		version = CHEAT_FMT_PARV3_RAW;
	}
	if (score)
		*score = max;
	return version;
}

static void cb_load(u8 *buffer, u32 op1, u16 op2)
{
	buffer[0] = op1 >> 24;
	buffer[1] = op1 >> 16;
	buffer[2] = op1 >> 8;
	buffer[3] = op1;
	buffer[4] = op2 >> 8;
	buffer[5] = op2;
}

static void cb_store(const u8 *buffer, u32 *op1, u16 *op2)
{
	*op1 = ((u32)buffer[0] << 24) | ((u32)buffer[1] << 16) | ((u32)buffer[2] << 8) | buffer[3];
	*op2 = (u16)((buffer[4] << 8) | buffer[5]);
}

static void cb_decrypt(const struct cheat_decoder *d, u32 *op1, u16 *op2)
{
	u8 buffer[6];
	int i;

	cb_load(buffer, *op1, *op2);
	for (i = sizeof(d->cb_table) - 1; i >= 0; --i) {
		u32 offset_x = i >> 3;
		u32 offset_y = d->cb_table[i] >> 3;
		int bit_x = i & 7;
		int bit_y = d->cb_table[i] & 7;

		u8 x = (buffer[offset_x] >> bit_x) & 1;
		u8 y = (buffer[offset_y] >> bit_y) & 1;
		u8 x2 = buffer[offset_x] & ~(1 << bit_x);
		if (y)
			x2 |= 1 << bit_x;
		buffer[offset_x] = x2;

		// This can't be moved earlier due to pointer aliasing
		u8 y2 = buffer[offset_y] & ~(1 << bit_y);
		if (x)
			y2 |= 1 << bit_y;
		buffer[offset_y] = y2;
	}
	cb_store(buffer, op1, op2);

	*op1 ^= d->cb_seeds[0];
	*op2 ^= d->cb_seeds[1];

	cb_load(buffer, *op1, *op2);
	u32 master = d->cb_master;
	for (i = 0; i < 5; ++i)
		buffer[i] ^= (master >> 8) ^ buffer[i + 1];
	buffer[5] ^= master >> 8;

	for (i = 5; i > 0; --i)
		buffer[i] ^= master ^ buffer[i - 1];
	buffer[0] ^= master;
	cb_store(buffer, op1, op2);

	*op1 ^= d->cb_seeds[2];
	*op2 ^= d->cb_seeds[3];
}

static u32 cb_rand(struct cheat_decoder *d)
{
	// Roll LCG three times to get enough bits of entropy
	u32 roll = d->cb_rng * 0x41C64E6D + 0x3039;
	u32 roll2 = roll * 0x41C64E6D + 0x3039;
	u32 roll3 = roll2 * 0x41C64E6D + 0x3039;
	u32 mix = (roll << 14) & 0xC0000000;
	mix |= (roll2 >> 1) & 0x3FFF8000;
	mix |= (roll3 >> 16) & 0x7FFF;
	d->cb_rng = roll3;
	return mix;
}

static u32 ror(u32 x, int n)
{
	return (x >> n) | (x << (32 - n));
}

static u32 cb_swap_index(struct cheat_decoder *d)
{
	u32 roll = cb_rand(d);
	u32 count = sizeof(d->cb_table);

	if (roll == count)
		roll = 0;
	if (roll < count)
		return roll;

	u32 bit = 1;
	while (count < 0x10000000 && count < roll) {
		count <<= 4;
		bit <<= 4;
	}
	while (count < 0x80000000 && count < roll) {
		count <<= 1;
		bit <<= 1;
	}

	u32 mask;
	for (;;) {
		mask = 0;
		if (roll >= count)
			roll -= count;
		if (roll >= count >> 1) {
			roll -= count >> 1;
			mask |= ror(bit, 1);
		}
		if (roll >= count >> 2) {
			roll -= count >> 2;
			mask |= ror(bit, 2);
		}
		if (roll >= count >> 3) {
			roll -= count >> 3;
			mask |= ror(bit, 3);
		}
		if (!roll || !(bit >> 4))
			break;
		bit >>= 4;
		count >>= 4;
	}

	mask &= 0xE0000000;
	if (!mask || !(bit & 7))
		return roll;
	if (mask & ror(bit, 3))
		roll += count >> 3;
	if (mask & ror(bit, 2))
		roll += count >> 2;
	if (mask & ror(bit, 1))
		roll += count >> 1;
	return roll;
}

// Returns 0 for a key whose table swap would index past the table (mGBA reads
// past it)
static int cb_reseed(struct cheat_decoder *d, u32 op1, u16 op2)
{
	u32 i;
	d->cb_rng = (op2 & 0xFF) ^ 0x1111;
	// Populate the initial seed table
	for (i = 0; i < sizeof(d->cb_table); ++i)
		d->cb_table[i] = i;
	// Swap pseudo-random table entries based on the input code
	for (i = 0; i < 0x50; ++i) {
		u32 x = cb_swap_index(d);
		u32 y = cb_swap_index(d);
		if (x >= sizeof(d->cb_table) || y >= sizeof(d->cb_table))
			return 0;
		u8 swap = d->cb_table[x];
		d->cb_table[x] = d->cb_table[y];
		d->cb_table[y] = swap;
	}

	// Spin the RNG some to make the initial seed
	d->cb_rng = 0x4EFAD1C3;
	for (i = 0; i < ((op1 >> 24) & 0xF); ++i)
		d->cb_rng = cb_rand(d);
	d->cb_seeds[2] = cb_rand(d);
	d->cb_seeds[3] = cb_rand(d);

	d->cb_rng = (op2 >> 8) ^ 0xF254;
	for (i = 0; i < (u32)(op2 >> 8); ++i)
		d->cb_rng = cb_rand(d);
	d->cb_seeds[0] = cb_rand(d);
	d->cb_seeds[1] = cb_rand(d);

	d->cb_master = op1;
	d->cb_key2 = op2;
	return 1;
}

// ------------------------------------------------------------------------
// Splitting a code into lines (Torii's)

enum {
	TOK_NONE,
	TOK_BAD,
	TOK_H4,
	TOK_H8,
	TOK_H12,
	TOK_H16,
	TOK_VBA
};
enum {
	LINE_END,
	LINE_BAD,
	LINE_CB,
	LINE_GS,
	LINE_VBA
};

struct line {
	u8 kind;
	u8 marks;  // digit stand-ins seen
	u8 width;  // VBA: the value's bytes
	u32 op1;   // VBA: the address
	u32 op2;   // VBA: the value
};

struct token {
	const char *p;
	u32 len;
	u8 kind;
	u8 marks;
};

static int is_sep(char c)
{
	return c == '+' || c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int hex_digit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

// Digit stand-ins in codes left for the user to fill: 00??, 000X, 00YY
static int is_mark(char c)
{
	return c == '?' || c == 'X' || c == 'x' || c == 'Y' || c == 'y' || c == 'Z' || c == 'z';
}

// n digits at p, stand-ins as 0
static u32 hex_value(const char *p, u32 n)
{
	u32 value = 0;
	u32 i;
	for (i = 0; i < n; ++i) {
		int digit = hex_digit(p[i]);
		value = (value << 4) | (u32)(digit < 0 ? 0 : digit);
	}
	return value;
}

// Counts digits (or stand-ins) from p; returns how many, or -1 at another byte
static int digits(const char *p, u32 n, u8 *marks)
{
	u32 i;
	for (i = 0; i < n; ++i) {
		if (is_mark(p[i]))
			*marks = 1;
		else if (hex_digit(p[i]) < 0)
			return -1;
	}
	return (int)n;
}

static void next_token(const char *s, u32 len, u32 *pos, struct token *t)
{
	u32 i = *pos;
	while (i < len && is_sep(s[i]))
		++i;
	t->p = s + i;
	t->marks = 0;
	if (i == len) {
		t->len = 0;
		t->kind = TOK_NONE;
		*pos = i;
		return;
	}
	u32 start = i;
	while (i < len && !is_sep(s[i]))
		++i;
	*pos = i;
	t->len = i - start;
	t->kind = TOK_BAD;
	if (t->len > 8 && t->p[8] == ':') {
		u32 vlen = t->len - 9;
		if ((vlen == 2 || vlen == 4 || vlen == 8) && digits(t->p, 8, &t->marks) >= 0 &&
		    digits(t->p + 9, vlen, &t->marks) >= 0)
			t->kind = TOK_VBA;
		return;
	}
	if (digits(t->p, t->len, &t->marks) < 0)
		return;
	switch (t->len) {
	case 4:
		t->kind = TOK_H4;
		break;
	case 8:
		t->kind = TOK_H8;
		break;
	case 12:
		t->kind = TOK_H12;
		break;
	case 16:
		t->kind = TOK_H16;
		break;
	default:
		break;
	}
}

static void next_line(const char *s, u32 len, u32 *pos, struct line *l)
{
	struct token t, u;
	next_token(s, len, pos, &t);
	l->marks = t.marks;
	switch (t.kind) {
	case TOK_NONE:
		l->kind = LINE_END;
		return;
	case TOK_H12:
		l->kind = LINE_CB;
		l->op1 = hex_value(t.p, 8);
		l->op2 = hex_value(t.p + 8, 4);
		return;
	case TOK_H16:
		l->kind = LINE_GS;
		l->op1 = hex_value(t.p, 8);
		l->op2 = hex_value(t.p + 8, 8);
		return;
	case TOK_VBA:
		l->kind = LINE_VBA;
		l->op1 = hex_value(t.p, 8);
		l->width = (t.len - 9) / 2;
		l->op2 = hex_value(t.p + 9, t.len - 9);
		// No GBA address is that high: a CodeBreaker line typed with a ':'
		if (l->op1 >> 28 && l->width == 2)
			l->kind = LINE_CB;
		return;
	case TOK_H8:
		next_token(s, len, pos, &u);
		l->marks |= u.marks;
		l->op1 = hex_value(t.p, 8);
		if (u.kind == TOK_H4) {
			l->kind = LINE_CB;
			l->op2 = hex_value(u.p, 4);
			return;
		}
		if (u.kind == TOK_H8) {
			l->kind = LINE_GS;
			l->op2 = hex_value(u.p, 8);
			return;
		}
		break;
	default:
		break;
	}
	l->kind = LINE_BAD;
}

// ------------------------------------------------------------------------
// Decoding one cheat

enum {
	CONT_NONE,
	CONT_GSA_LIST,
	CONT_GSA_ADD,
	CONT_PAR_SLIDE,
	CONT_PAR_BUTTON,
	CONT_PAR_PATCH,
	CONT_CB_FILL,
	CONT_CB_SUPER
};

// A conditional waiting on the codes (or lines) it covers
struct pending {
	u32 op;
	u32 codes;  // ALL: the rest of the cheat
	u32 line;   // by lines: the conditional's own line, else NONE
};

#define ALL 0xFFFFFFFFu

struct dec {
	struct cheat_decoder *d;
	struct cheat_op *ops;
	u32 ops_max;
	u32 n;  // every op decoded, stored or not
	struct cheat_op sink;  // where ops past ops_max go

	u8 cont;  // the multi-line code the next line continues
	u8 cont_width;
	u32 cont_op;
	u32 cont_left;
	u32 cont_value;
	u32 cont_addr;

	u32 block;  // the open PARv3 block's conditional, or NONE
	u32 else_at;  // its ELSE block's first op, or NONE
	struct pending pend[MAX_PENDING];
	u32 n_pend;

	u8 bad;
	u8 lines;
	u8 keyed;    // the cheat has a key or seed line of its own
	u8 adopt;    // a raw CodeBreaker key line other than the file's is a new key
	u8 ended;    // an Action Replay v3 end line: no more codes
	u32 line_no; // the line being decoded
};

static u32 width_mask(u32 width)
{
	switch (width) {
	case 1:
		return 0xFF;
	case 2:
		return 0xFFFF;
	case 4:
		return 0xFFFFFFFF;
	default:
		return 0;
	}
}

static struct cheat_op *op_at(struct dec *s, u32 i)
{
	return i < s->ops_max ? &s->ops[i] : &s->sink;
}

static struct cheat_op *emit(struct dec *s, u8 type, u8 width, u32 address, u32 value)
{
	struct cheat_op *op = op_at(s, s->n++);
	op->type = type;
	op->width = width;
	op->address = address;
	op->value = value & width_mask(width);
	op->count = 1;
	op->addr_step = 0;
	op->value_step = 0;
	op->else_count = 0;
	return op;
}

// A code (lines 0) or a line (lines 1) is done: conditionals waiting on it
// may now count their ops
static void pend_done(struct dec *s, int lines)
{
	u32 i = 0;
	while (i < s->n_pend) {
		struct pending *p = &s->pend[i];
		if ((p->line != NONE) != lines || p->line == s->line_no || p->codes == ALL || --p->codes) {
			++i;
			continue;
		}
		op_at(s, p->op)->count = s->n - p->op - 1;
		*p = s->pend[--s->n_pend];
	}
}

// A code has all its lines
static void code_done(struct dec *s)
{
	pend_done(s, 0);
}

// The conditional just emitted covers the next codes (ALL: the rest of the
// cheat), or the next lines when by_line
static void cover(struct dec *s, u32 codes, int by_line)
{
	u32 at = s->n - 1;
	code_done(s);
	if (!codes) {
		op_at(s, at)->count = 0;
		return;
	}
	if (s->n_pend == MAX_PENDING) {
		s->bad = 1;
		return;
	}
	s->pend[s->n_pend].op = at;
	s->pend[s->n_pend].codes = codes;
	s->pend[s->n_pend].line = by_line ? s->line_no : NONE;
	++s->n_pend;
}

static void cover_next(struct dec *s, u32 codes)
{
	cover(s, codes, 0);
}

static void cont_start(struct dec *s, u8 kind, u8 width)
{
	s->cont = kind;
	s->cont_width = width;
	s->cont_op = s->n - 1;
}

static void cont_end(struct dec *s)
{
	s->cont = CONT_NONE;
	code_done(s);
}

static void par_end_block(struct dec *s)
{
	struct cheat_op *op = op_at(s, s->block);
	u32 size = s->n - s->block - 1;
	if (s->else_at != NONE) {
		op->count = s->else_at - s->block - 1;
		if (size - op->count > 0xFFFF)
			s->bad = 1;
		op->else_count = size - op->count;
	} else {
		op->count = size;
	}
	s->block = NONE;
	s->else_at = NONE;
}

// ------------------------------------------------------------------------
// GameShark v1/v2 (GBACheatAddGameSharkRaw)

// A 16-bit conditional of the D and E types, by its kind (VBA-M's reading;
// mGBA's is equal for all): 0 equal, 1 not equal, 2 at most and 3 at least
// (unsigned, as a bound one past the value). Returns 0 for another kind.
static int gsa_cond(struct dec *s, u32 kind, u32 address, u32 value)
{
	switch (kind) {
	case 0:
		emit(s, CHEAT_OP_IF_EQ, 2, address, value);
		return 1;
	case 1:
		emit(s, CHEAT_OP_IF_NE, 2, address, value);
		return 1;
	case 2:
		emit(s, CHEAT_OP_IF_ULT, 2, address, 0)->value = value + 1;
		return 1;
	case 3:
		if (value)
			emit(s, CHEAT_OP_IF_UGT, 2, address, value - 1);
		else  // always
			emit(s, CHEAT_OP_IF_ULT, 2, address, 0)->value = 0x10000;
		return 1;
	default:
		s->bad = 1;
		return 0;
	}
}

static void gsa_raw(struct dec *s, u32 op1, u32 op2)
{
	u32 width;
	if (s->cont == CONT_GSA_ADD) {
		emit(s, CHEAT_OP_ADD, 4, s->cont_addr, s->cont_value == 0x60 ? 0u - op1 : op1);
		cont_end(s);
		return;
	}
	if (s->cont == CONT_GSA_LIST) {
		// Two addresses a line; an odd count leaves op2 unused
		emit(s, CHEAT_OP_WRITE, 4, op1, s->cont_value);
		if (--s->cont_left) {
			emit(s, CHEAT_OP_WRITE, 4, op2, s->cont_value);
			--s->cont_left;
		}
		if (!s->cont_left)
			cont_end(s);
		return;
	}
	if (op2 == MARKER)
		return;

	switch (op1 >> 28) {
	case 0x0:
	case 0x1:
	case 0x2:
		width = 1u << (op1 >> 28);
		emit(s, CHEAT_OP_WRITE, width, op1 & 0x0FFFFFFF, op2);
		code_done(s);
		return;
	case 0x3:
		switch ((op1 >> 16) & 0xFF) {
		case 0x00:  // a list
			if (!(op1 & 0xFFFF)) {
				s->bad = 1;
				return;
			}
			s->cont = CONT_GSA_LIST;
			s->cont_left = op1 & 0xFFFF;
			s->cont_value = op2;
			return;
		// Adds to (subtracts from) the word at op2, as VBA-M reads them: the
		// amount a byte, a halfword, or the next line's first half
		case 0x10:
			emit(s, CHEAT_OP_ADD, 4, op2 & 0x0FFFFFFF, op1 & 0xFF);
			break;
		case 0x20:
			emit(s, CHEAT_OP_ADD, 4, op2 & 0x0FFFFFFF, 0u - (op1 & 0xFF));
			break;
		case 0x30:
			emit(s, CHEAT_OP_ADD, 4, op2 & 0x0FFFFFFF, op1 & 0xFFFF);
			break;
		case 0x40:
			emit(s, CHEAT_OP_ADD, 4, op2 & 0x0FFFFFFF, 0u - (op1 & 0xFFFF));
			break;
		case 0x50:
		case 0x60:
			s->cont = CONT_GSA_ADD;
			s->cont_value = (op1 >> 16) & 0xFF;
			s->cont_addr = op2 & 0x0FFFFFFF;
			return;
		default:
			s->bad = 1;
			return;
		}
		code_done(s);
		return;
	case 0x6:
		emit(s, CHEAT_OP_ROM_PATCH, 2, 0x08000000 | ((op1 & 0xFFFFFF) << 1), op2);
		code_done(s);
		return;
	case 0x8:
		switch (op1 & 0x00F00000) {
		case 0x00100000:
			width = 1;
			break;
		case 0x00200000:
			width = 2;
			break;
		case 0x00400000:  // 32 bits: the device writes 0 whatever the value (VBA-M)
			width = 4;
			op2 = 0;
			break;
		case 0x00F00000:  // slowdown while the button is held: read, not run
			if (op1 != 0x80F00000) {
				s->bad = 1;
				return;
			}
			emit(s, CHEAT_OP_SLOWDOWN, 0, 0, 0)->value = op2 & 0xFFFF;
			code_done(s);
			return;
		default:
			s->bad = 1;
			return;
		}
		emit(s, CHEAT_OP_IF_BUTTON, 0, 0, 0);
		emit(s, CHEAT_OP_WRITE, width, op1 & 0x0F0FFFFF, op2);
		code_done(s);
		return;
	case 0xD:
		if (op1 == DEADFACE) {
			s->keyed = 1;
			gs_reseed(s->d->gs_seeds, op2, gsa1_t1, gsa1_t2);
			return;
		}
		if (gsa_cond(s, (op2 >> 20) & 0xF, op1 & 0x0FFFFFFF, op2 & 0xFFFF))
			cover_next(s, 1);
		return;
	case 0xE:  // the next zz lines, as the device steps through them (VBA-M)
		if (gsa_cond(s, op2 >> 28, op2 & 0x0FFFFFFF, op1 & 0xFFFF))
			cover(s, (op1 >> 16) & 0xFF, 1);
		return;
	case 0xF:  // the hook
		return;
	default:
		s->bad = 1;
		return;
	}
}

// ------------------------------------------------------------------------
// Action Replay v3 (GBACheatAddProActionReplayRaw)

static void par_cond(struct dec *s, u32 op1, u32 op2)
{
	static const u8 types[8] = {
		CHEAT_OP_IF_NEVER, CHEAT_OP_IF_EQ,  CHEAT_OP_IF_NE,  CHEAT_OP_IF_LT,
		CHEAT_OP_IF_GT,    CHEAT_OP_IF_ULT, CHEAT_OP_IF_UGT, CHEAT_OP_IF_AND,
	};
	u32 width = 1u << ((op1 & PAR3_WIDTH) >> PAR3_WIDTH_BASE);
	u32 action = op1 & PAR3_ACTION;
	if (width > 4)
		emit(s, CHEAT_OP_IF_NEVER, 0, par_addr(op1), 0);
	else
		emit(s, types[(op1 & PAR3_COND) >> 27], width, par_addr(op1), op2);
	if (action == PAR3_ACTION_BLOCK) {
		u32 at = s->n - 1;
		code_done(s);
		if (s->block != NONE)
			par_end_block(s);
		s->block = at;
		return;
	}
	// "Turn off all codes" while false: the device spins in its handler until
	// the condition holds (GBATEK); Torii skips the rest of the cheat instead
	if (action == PAR3_ACTION_DISABLE) {
		cover_next(s, ALL);
		return;
	}
	cover_next(s, action == PAR3_ACTION_NEXT_TWO ? 2 : 1);
}

static void par_special(struct dec *s, u32 op2)
{
	u32 width;
	switch (op2 & 0xFF000000) {
	case 0x00000000:  // the end of the code list: no code after it runs
		s->ended = 1;
		return;
	case 0x08000000:
		emit(s, CHEAT_OP_SLOWDOWN, 0, 0, 0)->value = op2 & 0x00FFFFFF;
		code_done(s);
		return;
	case 0x10000000:
	case 0x12000000:
	case 0x14000000:
		width = 1u << ((op2 >> 25) & 3);
		emit(s, CHEAT_OP_IF_BUTTON, 0, 0, 0);
		emit(s, CHEAT_OP_WRITE, width, par_addr(op2), 0);
		cont_start(s, CONT_PAR_BUTTON, width);
		return;
	case 0x18000000:
	case 0x1A000000:
	case 0x1C000000:
	case 0x1E000000:
		emit(s, CHEAT_OP_ROM_PATCH, 2, 0x08000000 | ((op2 & 0xFFFFFF) << 1), 0);
		cont_start(s, CONT_PAR_PATCH, 2);
		return;
	case 0x40000000:  // ENDIF
		if (s->block == NONE)
			s->bad = 1;
		else
			par_end_block(s);
		return;
	case 0x60000000:  // ELSE
		if (s->block == NONE) {
			s->bad = 1;
			return;
		}
		// An empty THEN gets a step that does nothing: a conditional covering
		// no ops never tests (mGBA's loop), so its ELSE would run every frame
		if (s->n == s->block + 1)
			emit(s, CHEAT_OP_WRITE, 1, 0, 0)->count = 0;
		s->else_at = s->n;
		return;
	case 0x80000000:
	case 0x82000000:
	case 0x84000000:
		width = 1u << ((op2 >> 25) & 3);
		emit(s, CHEAT_OP_WRITE, width, par_addr(op2), 0);
		cont_start(s, CONT_PAR_SLIDE, width);
		return;
	default:
		s->bad = 1;
		return;
	}
}

static void par_raw(struct dec *s, u32 op1, u32 op2)
{
	struct cheat_op *op;
	u32 width;
	switch (s->cont) {
	case CONT_PAR_PATCH:
		op_at(s, s->cont_op)->value = op1 & 0xFFFF;
		cont_end(s);
		return;
	case CONT_PAR_BUTTON:
		op_at(s, s->cont_op)->value = op1 & width_mask(s->cont_width);
		cont_end(s);
		return;
	case CONT_PAR_SLIDE:
		op = op_at(s, s->cont_op);
		op->value = op1 & width_mask(s->cont_width);
		op->value_step = (s32)(op2 >> 24);
		op->count = (op2 >> 16) & 0xFF;
		op->addr_step = (s32)((op2 & 0xFFFF) * s->cont_width);
		cont_end(s);
		return;
	default:
		break;
	}

	if (op2 == MARKER)
		return;
	if (op1 == 0) {
		par_special(s, op2);
		return;
	}
	if (op1 == DEADFACE) {
		s->keyed = 1;
		gs_reseed(s->d->gs_seeds, op2, par3_t1, par3_t2);
		return;
	}
	if (op1 >> 24 == 0xC4)  // the hook
		return;
	if (op1 == 0x80000000 && op2 == 0) {  // adds 0 to address 0: padding, still a code a conditional counts
		code_done(s);
		return;
	}
	if (op1 & PAR3_COND) {
		par_cond(s, op1, op2);
		return;
	}

	width = 1u << ((op1 & PAR3_WIDTH) >> PAR3_WIDTH_BASE);
	if ((op1 & 0x01000000) && (op1 & 0xFE000000) != 0xC6000000) {
		s->bad = 1;
		return;
	}
	switch (op1 & PAR3_BASE) {
	case PAR3_BASE_ASSIGN:
		if (width > 4) {
			s->bad = 1;
			return;
		}
		op = emit(s, CHEAT_OP_WRITE, width, par_addr(op1), op2);
		op->addr_step = (s32)width;
		if (width < 4)
			op->count = (op2 >> (width * 8)) + 1;
		break;
	case PAR3_BASE_INDIRECT:
		if (width > 4) {
			s->bad = 1;
			return;
		}
		op = emit(s, CHEAT_OP_WRITE_PTR, width, par_addr(op1), op2);
		if (width < 4)
			op->addr_step = (s32)((op2 >> (width * 8)) * width);
		break;
	case PAR3_BASE_ADD:
		if (width > 4) {
			s->bad = 1;
			return;
		}
		emit(s, CHEAT_OP_ADD, width, par_addr(op1), op2);
		break;
	default:  // IO writes: C6 of 16 bits, C7 of 32 (GBATEK, VBA-M; mGBA writes 8 and 16)
		if ((op1 & 0xFE000000) != 0xC6000000) {
			s->bad = 1;
			return;
		}
		width = op1 & 0x01000000 ? 4 : 2;
		emit(s, CHEAT_OP_WRITE, width, 0x04000000 | (op1 & 0x00FFFFFF), op2);
		break;
	}
	code_done(s);
}

// GBACheatAddGameShark, GBACheatAddProActionReplay and the autodetection
static void gs_line(struct dec *s, u32 op1, u32 op2)
{
	struct cheat_decoder *d = s->d;
	if (d->forced == CHEAT_FMT_CODEBREAKER || d->forced == CHEAT_FMT_VBA || s->cont == CONT_CB_FILL ||
	    s->cont == CONT_CB_SUPER) {
		s->bad = 1;
		return;
	}
	if (d->gs_version == CHEAT_FMT_AUTO)
		set_gs_version(d, gs_detect(op1, op2, NULL));
	if (s->lines == 1)
		d->last = d->gs_version;
	if (d->gs_version == CHEAT_FMT_GSAV1 || d->gs_version == CHEAT_FMT_PARV3)
		gs_decrypt(&op1, &op2, d->gs_seeds);
	if (d->gs_version <= CHEAT_FMT_GSAV1_RAW)
		gsa_raw(s, op1, op2);
	else
		par_raw(s, op1, op2);
}

// ------------------------------------------------------------------------
// CodeBreaker (GBACheatAddCodeBreaker)

static void cb_line(struct dec *s, u32 op1, u16 op2)
{
	static const u8 types[16] = {
		0,
		0,
		CHEAT_OP_OR,
		CHEAT_OP_WRITE,
		CHEAT_OP_WRITE,
		0,
		CHEAT_OP_AND,
		CHEAT_OP_IF_EQ,
		CHEAT_OP_WRITE,
		0,
		CHEAT_OP_IF_NE,
		CHEAT_OP_IF_GT,
		CHEAT_OP_IF_LT,
		0,
		CHEAT_OP_ADD,
		CHEAT_OP_IF_AND,
	};
	struct cheat_decoder *d = s->d;
	struct cheat_op *op;
	u8 bytes[6];
	u32 i;

	if ((d->forced != CHEAT_FMT_AUTO && d->forced != CHEAT_FMT_CODEBREAKER) ||
	    (s->cont != CONT_NONE && s->cont != CONT_CB_FILL && s->cont != CONT_CB_SUPER)) {
		s->bad = 1;
		return;
	}
	if (s->lines == 1)
		d->last = CHEAT_FMT_CODEBREAKER;
	if (d->cb_master) {
		if (op1 == d->cb_master && op2 == d->cb_key2) {  // the key again
			s->keyed = 1;
			return;
		}
		if (s->adopt && op1 >> 28 == 0x9 && s->cont == CONT_NONE) {  // another key, as typed
			s->keyed = 1;
			if (!cb_reseed(d, op1, op2))
				s->bad = 1;
			return;
		}
		cb_decrypt(d, &op1, &op2);
	}

	switch (s->cont) {
	case CONT_CB_FILL:
		op = op_at(s, s->cont_op);
		op->count = op1 & 0xFFFF;
		op->addr_step = (s32)op2;
		op->value_step = (s32)(op1 >> 16);
		cont_end(s);
		return;
	case CONT_CB_SUPER:
		cb_load(bytes, op1, op2);
		for (i = 0; i < 6 && s->cont_left; ++i, --s->cont_left)
			emit(s, CHEAT_OP_WRITE, 1, s->cont_addr++, bytes[i]);
		if (!s->cont_left)
			cont_end(s);
		return;
	default:
		break;
	}

	u32 type = op1 >> 28;
	u32 address = op1 & 0x0FFFFFFF;
	switch (type) {
	case 0x0:  // the game ID
	case 0x1:  // the hook
		return;
	case 0x9:
		s->keyed = 1;
		if (!cb_reseed(d, op1, op2))
			s->bad = 1;
		return;
	case 0x3:
		emit(s, CHEAT_OP_WRITE, 1, address, op2);
		code_done(s);
		return;
	case 0x4:
		emit(s, CHEAT_OP_WRITE, 2, address, op2);
		cont_start(s, CONT_CB_FILL, 2);
		return;
	case 0x5:  // super code: op2 halfwords from the next lines' bytes
		if (!op2) {
			s->bad = 1;
			return;
		}
		s->cont = CONT_CB_SUPER;
		s->cont_left = 2 * (u32)op2;
		s->cont_addr = address;
		return;
	case 0xD:  // key tests on KEYINPUT, which reads 0 for a held key
		if (address == 0x20)  // every key in op2 held (GBATEK)
			emit(s, CHEAT_OP_IF_NAND, 2, CHEAT_KEYINPUT, op2);
		else if (address == 0x10)  // not every one held (VBA-M)
			emit(s, CHEAT_OP_IF_AND, 2, CHEAT_KEYINPUT, op2);
		else {
			s->bad = 1;
			return;
		}
		cover_next(s, 1);
		return;
	case 0x7:
	case 0xA:
	case 0xB:
	case 0xC:
	case 0xF:
		emit(s, types[type], 2, address, op2);
		cover_next(s, 1);
		return;
	default:  // 2, 6, 8, E
		emit(s, types[type], 2, address, op2);
		code_done(s);
		return;
	}
}

// GBACheatAddVBALine
static void vba_line(struct dec *s, const struct line *l)
{
	struct cheat_decoder *d = s->d;
	if (d->forced != CHEAT_FMT_AUTO && d->forced != CHEAT_FMT_VBA) {
		s->bad = 1;
		return;
	}
	if (s->lines == 1)
		d->last = CHEAT_FMT_VBA;
	if (s->cont != CONT_NONE) {
		s->bad = 1;
		return;
	}
	if (l->op1 < 0x08000000 || l->op1 >= 0x0E000000)
		emit(s, CHEAT_OP_WRITE, l->width, l->op1, l->op2);
	else
		emit(s, CHEAT_OP_ROM_PATCH, l->width, l->op1, l->op2);
	code_done(s);
}

void cheat_decoder_init(struct cheat_decoder *d)
{
	memset(d, 0, sizeof(*d));
}

void cheat_decoder_force(struct cheat_decoder *d, u8 format)
{
	cheat_decoder_init(d);
	d->forced = format;
	if (format >= CHEAT_FMT_GSAV1 && format <= CHEAT_FMT_PARV3_RAW)
		set_gs_version(d, format);
}

// Decodes code's lines with the state in work; returns a status. *keyed says
// whether the cheat holds a CodeBreaker key line of its own. With adopt, a
// raw CodeBreaker key line other than work's key is a new key.
static u8 decode_pass(struct cheat_decoder *work, const char *code, u32 len, struct cheat_op *ops, u32 ops_max,
		      u32 *n_ops, u8 *keyed, u8 adopt)
{
	struct dec s;
	struct line l;
	u32 pos = 0;
	u32 i;

	memset(&s, 0, sizeof(s));
	s.d = work;
	s.ops = ops;
	s.ops_max = ops_max;
	s.block = NONE;
	s.else_at = NONE;
	s.adopt = adopt;
	work->last = CHEAT_FMT_AUTO;
	work->last_alone = 0;
	for (;;) {
		next_line(code, len, &pos, &l);
		if (l.kind == LINE_END || s.ended)
			break;
		if (s.lines < 0xFF)
			++s.lines;
		++s.line_no;
		if (l.kind == LINE_CB)
			cb_line(&s, l.op1, l.op2);
		else if (l.kind == LINE_GS)
			gs_line(&s, l.op1, l.op2);
		else
			vba_line(&s, &l);
		if (s.bad)
			break;
		pend_done(&s, 1);
	}
	if (s.cont != CONT_NONE)
		s.bad = 1;
	for (i = 0; i < s.n_pend; ++i)
		op_at(&s, s.pend[i].op)->count = s.n - s.pend[i].op - 1;
	if (s.block != NONE)
		par_end_block(&s);
	*keyed = s.keyed;
	*n_ops = s.n;
	if (s.bad) {
		*n_ops = 0;
		return CHEAT_UNREADABLE;
	}
	if (!s.n)
		return CHEAT_MASTER_ONLY;
	return s.n > ops_max ? CHEAT_TOO_BIG : CHEAT_OK;
}

// Whether a decoding looks real: it reads, and every op is in the GBA's memory
static int plausible(u8 status, const struct cheat_op *ops, u32 n_ops)
{
	u32 i;
	if (status != CHEAT_OK)
		return status == CHEAT_TOO_BIG;
	for (i = 0; i < n_ops; ++i)
		if (!cheat_op_address_ok(&ops[i]))
			return 0;
	return 1;
}

// Whether a decoding does more than patch the ROM: any garbage of a ROM
// patch's shape reads as one, so a second reading must touch RAM to count
static int touches_ram(const struct cheat_op *ops, u32 n_ops)
{
	u32 i;
	for (i = 0; i < n_ops; ++i)
		if (ops[i].type != CHEAT_OP_ROM_PATCH)
			return 1;
	return 0;
}

u8 cheat_decode(struct cheat_decoder *d, const char *code, u32 len, struct cheat_op *ops, u32 ops_max, u32 *n_ops)
{
	struct cheat_decoder work = *d, alone;
	struct line l;
	u32 pos = 0, i;
	u8 marks = 0, keyed, status, new_key = 0;

	*n_ops = 0;
	// Every token must read before anything decodes
	for (i = 0;; ++i) {
		next_line(code, len, &pos, &l);
		if (l.kind == LINE_END)
			break;
		if (l.kind == LINE_BAD)
			return CHEAT_UNREADABLE;
		marks |= l.marks;
		if (d->cb_master && l.kind == LINE_CB && l.op1 >> 28 == 0x9 &&
		    !(l.op1 == d->cb_master && l.op2 == d->cb_key2))
			new_key = 1;
	}
	if (!i)
		return CHEAT_UNREADABLE;
	if (marks)
		return CHEAT_PLACEHOLDER;

	status = decode_pass(&work, code, len, ops, ops_max, n_ops, &keyed, 0);
	// A raw key line other than the file's: a file joining codes made under
	// two keys starts the second key's codes with its key line, as typed. It
	// is taken as the key when the file's key reads the cheat as nonsense and
	// the new one doesn't; the file's later cheats keep it. A key line alone
	// is no evidence: an encrypted code may look like one.
	if (new_key && i > 1 &&
	    !(status != CHEAT_MASTER_ONLY && plausible(status, ops, *n_ops < ops_max ? *n_ops : ops_max))) {
		u32 n_new;
		u8 new_status;
		alone = *d;
		new_status = decode_pass(&alone, code, len, ops, ops_max, &n_new, &keyed, 1);
		if (new_status == CHEAT_MASTER_ONLY || plausible(new_status, ops, n_new < ops_max ? n_new : ops_max)) {
			*d = alone;
			*n_ops = n_new;
			return new_status;
		}
		work = *d;
		status = decode_pass(&work, code, len, ops, ops_max, n_ops, &keyed, 0);
	}
	if (keyed || plausible(status, ops, *n_ops < ops_max ? *n_ops : ops_max)) {
		*d = work;
		return status;
	}
	// libretro's files mix plain CodeBreaker cheats with ones encrypted under
	// an earlier cheat's key, and raw GameShark cheats with encrypted ones. A
	// cheat with no key or seed line of its own that decodes to nonsense is
	// read without the key, or with its own format detected, or both, when
	// that reads as real. The file's key, format and seeds carry on.
	for (i = 1; i < 4; ++i) {
		u32 n_alone;
		u8 alone_status;
		if (((i & 1) && !d->cb_master) ||
		    ((i & 2) && (d->forced != CHEAT_FMT_AUTO || d->gs_version == CHEAT_FMT_AUTO)))
			continue;
		alone = *d;
		if (i & 1)
			alone.cb_master = 0;
		if (i & 2)
			alone.gs_version = CHEAT_FMT_AUTO;
		alone_status = decode_pass(&alone, code, len, ops, ops_max, &n_alone, &keyed, 0);
		if (plausible(alone_status, ops, n_alone < ops_max ? n_alone : ops_max) &&
		    touches_ram(ops, n_alone < ops_max ? n_alone : ops_max)) {
			u8 last = alone.last;
			*d = work;
			d->last = last;
			d->last_alone = 1;
			*n_ops = n_alone;
			return alone_status;
		}
	}
	// The file's first GameShark cheat: the detection scores each format on
	// its first line alone, and can pick one the cheat doesn't read in (a raw
	// ROM patch, "60018CFC 000046C0", scores higher decrypted). The others are
	// tried in turn; the first that reads as real sets the file's format. A
	// raw reading may only patch the ROM, as its text says so; a decrypted
	// one must touch RAM, as garbage of a ROM patch's shape always reads.
	if (status != CHEAT_MASTER_ONLY && d->forced == CHEAT_FMT_AUTO && d->gs_version == CHEAT_FMT_AUTO &&
	    work.last >= CHEAT_FMT_GSAV1 && work.last <= CHEAT_FMT_PARV3_RAW) {
		static const u8 versions[] = {CHEAT_FMT_GSAV1, CHEAT_FMT_PARV3, CHEAT_FMT_GSAV1_RAW,
					      CHEAT_FMT_PARV3_RAW};
		u8 detected = work.gs_version;
		for (i = 0; i < sizeof(versions); ++i) {
			u32 n_alone;
			u8 alone_status;
			if (versions[i] == detected)
				continue;
			alone = *d;
			set_gs_version(&alone, versions[i]);
			alone_status = decode_pass(&alone, code, len, ops, ops_max, &n_alone, &keyed, 0);
			if (plausible(alone_status, ops, n_alone < ops_max ? n_alone : ops_max) &&
			    (versions[i] == CHEAT_FMT_GSAV1_RAW || versions[i] == CHEAT_FMT_PARV3_RAW ||
			     touches_ram(ops, n_alone < ops_max ? n_alone : ops_max))) {
				*d = alone;
				*n_ops = n_alone;
				return alone_status;
			}
		}
	}
	// None reads: back to the file's own decoding
	work = *d;
	status = decode_pass(&work, code, len, ops, ops_max, n_ops, &keyed, 0);
	*d = work;
	return status;
}

u8 cheat_detect(const char *line, u32 len, s32 *score)
{
	struct line l, rest;
	u32 pos = 0;
	u8 format = CHEAT_FMT_AUTO;
	if (score)
		*score = 0;
	next_line(line, len, &pos, &l);
	next_line(line, len, &pos, &rest);
	if (rest.kind != LINE_END)
		return CHEAT_FMT_AUTO;
	switch (l.kind) {
	case LINE_CB:
		format = CHEAT_FMT_CODEBREAKER;
		break;
	case LINE_VBA:
		format = CHEAT_FMT_VBA;
		break;
	case LINE_GS:
		return gs_detect(l.op1, l.op2, score);
	default:
		return CHEAT_FMT_AUTO;
	}
	if (score)
		*score = 0x100;
	return format;
}
