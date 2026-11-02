// See cheatemu.h.
#include "cheatemu.h"

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

// The NES Game Genie's alphabet: a letter's place is its nibble
static const char nes_letters[] = "APZLGITYEOXUKSVN";

static int nes_nibble(char c)
{
	if (c >= 'a' && c <= 'z')
		c = (char)(c - 'a' + 'A');
	for (int i = 0; i < 16; i++)
		if (nes_letters[i] == c)
			return i;
	return -1;
}

// Line s (n bytes, trimmed) of a GB code: 1 and *e filled for a Game Genie
// (XXX-YYY or XXX-YYY-ZZZ); 2 for a GameShark line (8 digits) that is
// well formed, which doesn't run yet; 0 if it is neither.
static int gb_line(const char *s, u32 n, struct emucheat *e)
{
	int d[9];
	if (n == 7 || n == 11) {
		for (u32 i = 0, k = 0; i < n; i++) {
			if (i == 3 || i == 7) {
				if (s[i] != '-')
					return 0;
				continue;
			}
			d[k] = hex_digit(s[i]);
			if (d[k++] < 0)
				return 0;
		}
		// Digits 0, 1: the value; 2: address bits 8-11; 4, 5: its low byte;
		// 6: its bits 12-15, inverted. 8 and 10 (the 9-digit form's third group
		// 1st and 3rd digits) are the compare, rotated and XORed (as
		// gambatte reads them)
		u32 addr = (u32)d[2] << 8 | (u32)d[3] << 4 | (u32)d[4] | ((u32)d[5] ^ 0xF) << 12;
		if (addr >= 0x8000)
			return 0;  // a Game Genie patches the ROM: no such address
		e->kind = CHEATEMU_GENIE;
		e->addr = (u16)addr;
		e->value = (u8)(d[0] << 4 | d[1]);
		e->has_compare = n == 11;
		e->compare = 0;
		if (n == 11) {
			u32 c = ((u32)d[6] << 4 | (u32)d[8]) ^ 0xFF;
			c = ((c >> 2 | c << 6) ^ 0x45) & 0xFF;
			e->compare = (u8)c;
		}
		return 1;
	}
	if (n == 8) {
		for (u32 i = 0; i < n; i++)
			if (hex_digit(s[i]) < 0)
				return 0;
		return 2;
	}
	return 0;
}

// The same for a NES code: Game Genie letters (6 or 8), 1; a raw "AAAA:VV"
// or "AAAA?CC:VV" RAM write, 2; else 0
static int nes_line(const char *s, u32 n, struct emucheat *e)
{
	if (n == 6 || n == 8) {
		int d[8];
		for (u32 i = 0; i < n; i++) {
			d[i] = nes_nibble(s[i]);
			if (d[i] < 0)
				return 0;
		}
		u32 addr = 0x8000u | (u32)(d[3] & 7) << 12 | (u32)(d[5] & 7) << 8 | (u32)(d[4] & 8) << 8 |
			   (u32)(d[2] & 7) << 4 | (u32)(d[1] & 8) << 4 | (u32)(d[4] & 7) | (u32)(d[3] & 8);
		u32 value = (u32)(d[1] & 7) << 4 | (u32)(d[0] & 8) << 4 | (u32)(d[0] & 7);
		e->kind = CHEATEMU_GENIE;
		e->addr = (u16)addr;
		e->has_compare = n == 8;
		e->compare = 0;
		if (n == 6) {
			value |= (u32)(d[5] & 8);
		} else {
			value |= (u32)(d[7] & 8);
			e->compare = (u8)((d[7] & 7) << 4 | (d[6] & 8) << 4 | (d[6] & 7) | (d[5] & 8));
		}
		e->value = (u8)value;
		return 1;
	}
	if (n == 7 || n == 10) {
		u32 colon = n - 3;
		if (s[colon] != ':')
			return 0;
		for (u32 i = 0; i < n; i++)
			if (i != colon && !(n == 10 && i == 4 && s[i] == '?') && hex_digit(s[i]) < 0)
				return 0;
		return 2;
	}
	return 0;
}

u8 cheatemu_decode(u32 console, const char *code, u32 len, struct emucheat *out, u32 max, u32 *n)
{
	u32 count = 0;
	int unsupported = 0;
	*n = 0;
	if (console != CHEATEMU_GB && console != CHEATEMU_NES)
		return CHEATEMU_UNREADABLE;
	for (u32 i = 0; i <= len;) {
		while (i < len &&
		       (code[i] == '+' || code[i] == ' ' || code[i] == '\t' || code[i] == '\r' || code[i] == '\n'))
			i++;
		if (i >= len)
			break;
		u32 from = i;
		while (i < len && code[i] != '+' && code[i] != ' ' && code[i] != '\t' && code[i] != '\r' &&
		       code[i] != '\n')
			i++;
		struct emucheat e = {0, 0, 0, 0, 0};
		int kind = console == CHEATEMU_GB ? gb_line(code + from, i - from, &e)
						  : nes_line(code + from, i - from, &e);
		if (!kind)
			return CHEATEMU_UNREADABLE;
		if (kind == 2) {
			unsupported = 1;
			continue;
		}
		if (count >= max)
			return CHEATEMU_TOO_BIG;
		out[count++] = e;
	}
	if (!count && !unsupported)
		return CHEATEMU_UNREADABLE;
	*n = count;
	return unsupported ? CHEATEMU_CANT_RUN : CHEATEMU_OK;
}

void cheatemu_to_patch(const struct emucheat *e, struct cheat_patch *p)
{
	p->offset = (u32)e->kind << 16 | e->addr;
	p->value = (u16)(e->value | (u16)e->compare << 8);
	p->mask = e->has_compare ? 1 : 0;
}

void cheatemu_from_patch(const struct cheat_patch *p, struct emucheat *e)
{
	e->kind = (u8)(p->offset >> 16);
	e->addr = (u16)p->offset;
	e->value = (u8)p->value;
	e->compare = (u8)(p->value >> 8);
	e->has_compare = p->mask != 0;
}

// --- The game's image ------------------------------------------------------

int cheatemu_rom_gb(struct cheatemu_rom *rom, u32 size)
{
	rom->console = CHEATEMU_GB;
	rom->mapper = 0;
	rom->banks = 0;
	rom->size = size;
	rom->prg_at = 0;
	return size >= 0x4000;
}

int cheatemu_rom_nes(struct cheatemu_rom *rom, const u8 *hdr, u32 size)
{
	rom->console = CHEATEMU_NES;
	rom->size = size;
	if (hdr[0] != 'N' || hdr[1] != 'E' || hdr[2] != 'S' || hdr[3] != 0x1A)
		return 0;
	// The mapper as PocketNES reads it (loadcart.c): the high nibble from
	// byte 7 only when it is a clean iNES 1 or a NES 2.0 header
	u8 f1 = hdr[6], f2 = hdr[7];
	if ((f2 & 0x0C) != 0x08 && (f2 & 0x0E))
		f2 = 0;
	rom->mapper = (u8)((f1 >> 4) + (f2 & 0xF0));
	rom->banks = hdr[4];
	rom->prg_at = 16u + ((f1 & 4) ? 512u : 0u);
	return rom->banks != 0 && rom->prg_at + (u32)rom->banks * 0x4000u <= size;
}

int cheatemu_sampled(const struct cheatemu_rom *rom, u32 offset)
{
	u32 base = rom->console == CHEATEMU_NES ? 16u : 0u;
	if (offset < base)
		return 0;
	offset -= base;
	return offset < 0x4000u && (offset & 127u) < 4u;
}

enum {
	BANKS_FIXED,
	BANKS_16_LAST,
	BANKS_8,
	BANKS_32,
	BANKS_UNKNOWN
};

static u32 nes_banking(u32 mapper)
{
	switch (mapper) {
	case 0:
	case 3:
		return BANKS_FIXED;
	case 1:
	case 2:
	case 71:
	case 94:
		return BANKS_16_LAST;
	case 4:
	case 118:
	case 119:
		return BANKS_8;
	case 7:
	case 11:
	case 34:
	case 66:
	case 79:
		return BANKS_32;
	default:
		return BANKS_UNKNOWN;
	}
}

// One place: the compare, then the checksum's samples, then the patch
static void place(const struct cheatemu_rom *rom, const struct emucheat *e, u32 at, cheatemu_get get, cheatemu_set set,
		  void *ctx, u32 max, struct cheatemu_done *done)
{
	if (at >= rom->size)
		return;
	if (e->has_compare && get(ctx, at) != e->compare) {
		done->other++;
		return;
	}
	if (cheatemu_sampled(rom, at)) {
		done->sampled++;
		return;
	}
	if (done->patched >= max) {
		done->limit++;
		return;
	}
	set(ctx, at, e->value);
	done->patched++;
}

void cheatemu_patch(const struct cheatemu_rom *rom, const struct emucheat *e, cheatemu_get get, cheatemu_set set,
		    void *ctx, u32 max, struct cheatemu_done *done)
{
	if (e->kind != CHEATEMU_GENIE)
		return;
	if (rom->console == CHEATEMU_GB) {
		u32 banks = (rom->size + 0x3FFF) / 0x4000;
		if (e->addr < 0x4000) {
			place(rom, e, e->addr, get, set, ctx, max, done);
			return;
		}
		for (u32 b = 1; b < banks; b++)
			place(rom, e, b * 0x4000 + (e->addr - 0x4000u), get, set, ctx, max, done);
		return;
	}
	if (rom->console != CHEATEMU_NES || e->addr < 0x8000)
		return;
	u32 a = e->addr - 0x8000u, prg = (u32)rom->banks * 0x4000u, at = rom->prg_at;
	switch (nes_banking(rom->mapper)) {
	case BANKS_FIXED:
		place(rom, e, at + a % prg, get, set, ctx, max, done);
		break;
	case BANKS_16_LAST:
		if (a >= 0x4000)
			place(rom, e, at + prg - 0x4000u + (a & 0x3FFFu), get, set, ctx, max, done);
		else
			for (u32 b = 0; b < rom->banks; b++)
				place(rom, e, at + b * 0x4000u + a, get, set, ctx, max, done);
		break;
	case BANKS_8:
		if (a >= 0x6000)
			place(rom, e, at + prg - 0x2000u + (a & 0x1FFFu), get, set, ctx, max, done);
		else
			for (u32 b = 0; b < prg / 0x2000u; b++)
				place(rom, e, at + b * 0x2000u + (a & 0x1FFFu), get, set, ctx, max, done);
		break;
	case BANKS_32:
		if (prg < 0x8000u) {
			place(rom, e, at + a % prg, get, set, ctx, max, done);
			break;
		}
		for (u32 b = 0; b < prg / 0x8000u; b++)
			place(rom, e, at + b * 0x8000u + a, get, set, ctx, max, done);
		break;
	default:
		// The banks aren't known: the compare byte alone says which, at any 8 KB bank's
		if (!e->has_compare) {
			done->unknown = 1;
			break;
		}
		for (u32 b = 0; b < prg / 0x2000u; b++)
			place(rom, e, at + b * 0x2000u + (a & 0x1FFFu), get, set, ctx, max, done);
		break;
	}
}
