// A BMP file as the screenshot viewer shows it (picture.h).
#include "picture.h"

#include <stdint.h>
#include <string.h>

static u32 le16(const u8 *p)
{
	return (u32)p[0] | (u32)p[1] << 8;
}

static u32 le32(const u8 *p)
{
	return le16(p) | le16(p + 2) << 16;
}

// A mask's shift and width; 0 if its bits aren't one run
static int mask_take(u32 m, u8 *shift, u8 *bits)
{
	u32 s = 0, b = 0;
	if (m) {
		while (!(m >> s & 1))
			s++;
		while (s + b < 32 && (m >> (s + b) & 1))
			b++;
		if (s + b < 32 && (m >> (s + b)))
			return 0;
	}
	*shift = (u8)s;
	*bits = (u8)b;
	return 1;
}

// One masked channel as 5 bits
static inline u32 channel(u32 px, u32 mask, u32 shift, u32 bits)
{
	u32 v = (px & mask) >> shift;
	if (bits >= 5)
		return v >> (bits - 5);
	if (!bits)
		return 0;
	u32 out = 0, have = 0;
	for (; have < 5; have += bits)  // the bits repeated, as 8 from 5 are
		out = out << bits | v;
	return out >> (have - 5);
}

int pic_head(const struct pic_src *src, struct pic_scratch *s, struct pic_info *info)
{
	u8 *h = s->buf;
	memset(info, 0, sizeof(*info));
	u32 n = src->size < PIC_HEAD_BYTES ? src->size : PIC_HEAD_BYTES;
	if (n < 54 || !src->read(src->ctx, 0, h, n) || h[0] != 'B' || h[1] != 'M')
		return PIC_BROKEN;
	u32 hsize = le32(h + 14);
	if (hsize == 12)
		return PIC_UNSUPPORTED;  // OS/2's
	if (hsize != 40 && hsize != 52 && hsize != 56 && hsize != 108 && hsize != 124)
		return hsize < 40 ? PIC_BROKEN : PIC_UNSUPPORTED;
	if (14 + hsize > n)
		return PIC_BROKEN;
	s32 w = (s32)le32(h + 18), ht = (s32)le32(h + 22);
	u32 planes = le16(h + 26), bpp = le16(h + 28), comp = le32(h + 30), used = le32(h + 46);
	info->data = le32(h + 10);
	if (w <= 0 || ht == 0 || ht == (s32)0x80000000 || planes != 1)
		return PIC_BROKEN;
	info->top_down = ht < 0;
	info->w = (u32)w;
	info->h = (u32)(ht < 0 ? -ht : ht);
	info->bpp = (u16)bpp;
	if (info->w > PIC_SIDE_MAX || info->h > PIC_SIDE_MAX)
		return PIC_UNSUPPORTED;
	if (bpp == 1 || bpp == 4 || bpp == 8) {
		if (comp != 0)
			return PIC_UNSUPPORTED;  // RLE
		u32 most = 1u << bpp;
		if (used > most)
			return PIC_BROKEN;
		info->pal_n = used ? used : most;
		info->pal_at = 14 + hsize;
		if (info->pal_at + info->pal_n * 4 > src->size)
			return PIC_BROKEN;
	} else if (bpp == 16 || bpp == 32) {
		if (comp == 0) {
			info->mask[0] = bpp == 16 ? 0x7C00 : 0xFF0000;
			info->mask[1] = bpp == 16 ? 0x03E0 : 0x00FF00;
			info->mask[2] = bpp == 16 ? 0x001F : 0x0000FF;
		} else if (comp == 3 || comp == 6) {
			// In the header from version 2 (52 bytes) on, else after it
			const u8 *m = h + 54;
			if (hsize == 40 && 66 > n)
				return PIC_BROKEN;
			for (u32 c = 0; c < 3; c++)
				info->mask[c] = le32(m + 4 * c);
		} else {
			return PIC_UNSUPPORTED;
		}
		for (u32 c = 0; c < 3; c++)
			if (!mask_take(info->mask[c], &info->shift[c], &info->bits[c]))
				return PIC_UNSUPPORTED;
		info->masked = 1;
	} else if (bpp == 24) {
		if (comp != 0)
			return PIC_UNSUPPORTED;
	} else {
		return PIC_UNSUPPORTED;
	}
	info->row_bytes = (info->w * bpp + 31) / 32 * 4;
	if (info->row_bytes > PIC_BUF_BYTES)
		return PIC_UNSUPPORTED;
	if (info->data > src->size || info->row_bytes * info->h > src->size - info->data)
		return PIC_BROKEN;
	// Fitted, keeping its shape: the side that fills the screen first sets the scale
	if (info->w * PIC_H <= info->h * PIC_W) {
		info->out_h = info->h < PIC_H ? info->h : PIC_H;
		info->out_w = info->w * info->out_h / info->h;
	} else {
		info->out_w = info->w < PIC_W ? info->w : PIC_W;
		info->out_h = info->h * info->out_w / info->w;
	}
	info->out_w += !info->out_w;
	info->out_h += !info->out_h;
	info->x0 = (PIC_W - info->out_w) / 2;
	info->y0 = (PIC_H - info->out_h) / 2;
	return PIC_OK;
}

// File row fr's bytes, from buf or read into it: a run of rows when the
// picture reads densely, the one row when it skips rows; NULL if a read failed
static const u8 *file_row(const struct pic_src *src, const struct pic_info *info, struct pic_scratch *s, u32 fr)
{
	if (fr - s->first < s->rows)
		return s->buf + (fr - s->first) * info->row_bytes;
	u32 n = 1;
	if (info->h < 2 * info->out_h) {
		n = (s->read_max ? s->read_max : PIC_BUF_BYTES) / info->row_bytes;
		n += !n;  // a row wider than read_max: the one row
		if (n > info->h - fr)
			n = info->h - fr;
	}
	s->rows = 0;
	if (!src->read(src->ctx, info->data + fr * info->row_bytes, s->buf, n * info->row_bytes))
		return 0;
	s->first = fr;
	s->rows = n;
	return s->buf;
}

// Source column samples: 16.16 steps, from the middle of each output pixel
static u32 col_step(const struct pic_info *info)
{
	return (info->w << 16) / info->out_w;
}

void PIC_HOT pic_row24(const u8 *row, u32 at, u32 step, u32 n, u16 *out)
{
	if (step == 1u << 16) {  // as wide as it shows: every pixel, four from three words
		row += (at >> 16) * 3;
		if (!((uintptr_t)row & 3))
			for (; n >= 4; n -= 4, row += 12, out += 4) {
				const u32 *p = (const u32 *)(const void *)row;
				u32 a = p[0], b = p[1], c = p[2];  // B G R B, G R B G, R B G R
				out[0] = (u16)((a >> 19 & 31) | (a >> 11 & 31) << 5 | (a >> 3 & 31) << 10);
				out[1] = (u16)((b >> 11 & 31) | (b >> 3 & 31) << 5 | (a >> 27) << 10);
				out[2] = (u16)((c >> 3 & 31) | (b >> 27) << 5 | (b >> 19 & 31) << 10);
				out[3] = (u16)((c >> 27) | (c >> 19 & 31) << 5 | (c >> 11 & 31) << 10);
			}
		for (; n; n--, row += 3)
			*out++ = (u16)(row[2] >> 3 | (row[1] >> 3) << 5 | (row[0] >> 3) << 10);
		return;
	}
	for (; n; n--, at += step) {
		const u8 *p = row + (at >> 16) * 3;
		*out++ = (u16)(p[2] >> 3 | (p[1] >> 3) << 5 | (p[0] >> 3) << 10);
	}
}

// The picture's part of line from file row row (out_w pixels at x0)
static void row_rgb15(const struct pic_info *info, const struct pic_scratch *s, const u8 *row, u16 *out)
{
	u32 step = col_step(info), at = step / 2, n = info->out_w;
	switch (info->bpp) {
	case 24:
		pic_row24(row, at, step, n, out);
		break;
	case 32:
	case 16:
		for (u32 x = 0; x < n; x++, at += step) {
			u32 sx = at >> 16;
			u32 px = info->bpp == 32 ? le32(row + sx * 4) : le16(row + sx * 2);
			out[x] = (u16)(channel(px, info->mask[0], info->shift[0], info->bits[0]) |
				       channel(px, info->mask[1], info->shift[1], info->bits[1]) << 5 |
				       channel(px, info->mask[2], info->shift[2], info->bits[2]) << 10);
		}
		break;
	default:  // 1, 4 or 8 bits: an index into the colour table
		for (u32 x = 0; x < n; x++, at += step) {
			u32 sx = at >> 16, i;
			if (info->bpp == 8)
				i = row[sx];
			else if (info->bpp == 4)
				i = row[sx >> 1] >> (sx & 1 ? 0 : 4) & 15;
			else
				i = row[sx >> 3] >> (7 - (sx & 7)) & 1;
			out[x] = i < info->pal_n ? s->pal[i] : 0;
		}
		break;
	}
}

// Line y's tiles' rows: the first of its 30 tiles' 8-byte rows
static u8 *tile_row(u8 *tiles, u32 y)
{
	return tiles + (y >> 3) * (PIC_W / 8) * 64 + (y & 7) * 8;
}

// RGB15 as its 4-bit cell, the cut's colour: blue, green, red from the top
static inline __attribute__((always_inline)) u32 cell(u32 c)
{
	return (c >> 1 & 15) | (c >> 2 & 0xF0) | (c >> 3 & 0xF00);
}

// A line as the colours met so far (table: each one's index + 1); a run
// of one colour looks it up once
int PIC_HOT pic_emit_exact(const u16 *line, u8 *table, u8 *row, u16 *palette, u32 *colours)
{
	u32 n = *colours, last = 0x10000, index = 0;
	for (u32 tx = 0; tx < PIC_W / 8; tx++, row += 64)
		for (u32 k = 0; k < 2; k++) {
			u32 w = 0;
			for (u32 b = 0; b < 32; b += 8) {
				u32 c = *line++;
				if (c != last) {
					u32 t = table[c];
					if (!t) {
						if (n == PIC_COLOURS) {
							*colours = n;
							return 0;
						}
						palette[n] = (u16)c;
						t = ++n;
						table[c] = (u8)t;
					}
					last = c;
					index = PIC_FIRST_COLOUR - 1 + t;
				}
				w |= index << b;
			}
			((u32 *)(void *)row)[k] = w;
		}
	*colours = n;
	return 1;
}

// A line through the cells' boxes (index), likewise
void PIC_HOT pic_emit_map(const u16 *line, const u8 *index, u8 *row)
{
	u32 last = 0x10000, i = 0;
	for (u32 tx = 0; tx < PIC_W / 8; tx++, row += 64)
		for (u32 k = 0; k < 2; k++) {
			u32 w = 0;
			for (u32 b = 0; b < 32; b += 8) {
				u32 c = *line++;
				if (c != last) {
					last = c;
					i = PIC_FIRST_COLOUR + index[cell(c)];
				}
				w |= i << b;
			}
			((u32 *)(void *)row)[k] = w;
		}
}

// A line through the boxes, each channel first moved by Bayer's 4 x 4
// threshold at its pixel (-2 to +1 of 5 bits: about a 4-bit cell's width)
void PIC_HOT pic_emit_dither(const u16 *line, const u8 *index, u8 *row, u32 y)
{
	static const u8 order[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
	for (u32 tx = 0; tx < PIC_W / 8; tx++, row += 64)
		for (u32 k = 0; k < 2; k++) {
			u32 w = 0;
			for (u32 b = 0; b < 4; b++) {
				u32 x = tx * 8 + k * 4 + b, c = *line++;
				s32 d = order[(y & 3) * 4 + (x & 3)] / 4 - 2;
				s32 r = (s32)(c & 31) + d, g = (s32)(c >> 5 & 31) + d, bl = (s32)(c >> 10) + d;
				r = r < 0 ? 0 : r > 31 ? 31 : r;
				g = g < 0 ? 0 : g > 31 ? 31 : g;
				bl = bl < 0 ? 0 : bl > 31 ? 31 : bl;
				u32 i = index[cell((u32)(r | g << 5 | bl << 10))];
				if (i == PIC_NO_BOX)
					i = index[cell(c)];  // past every box: undithered
				w |= (PIC_FIRST_COLOUR + i) << (8 * b);
			}
			((u32 *)(void *)row)[k] = w;
		}
}

u32 PIC_HOT pic_emit_count(const u16 *line, u16 *counts, u16 *list, u32 n)
{
	for (u32 x = 0; x < PIC_W;) {
		u32 c = cell(line[x]), run = 1;
		for (x++; x < PIC_W && cell(line[x]) == c; x++)
			run++;
		if (!counts[c])
			list[n++] = (u16)c;  // 4,096 cells at most: the list's room
		counts[c] = (u16)(counts[c] + run);  // at most PIC_W * PIC_H pixels: no overflow
	}
	return n;
}

// The colour table's parts once the exact pass is over: the cells' pixel
// counts, then each cell's box
static u16 *counts(struct pic_dec *d)
{
	return (u16 *)(void *)d->table;
}

static u8 *boxes(struct pic_dec *d)
{
	return d->table + PIC_CELLS * 2;
}

// The colours are listed where the tiles go, until the map writes them
static u16 *list(struct pic_dec *d)
{
	return (u16 *)(void *)d->tiles;
}

_Static_assert(PIC_CELLS * 3 <= PIC_TABLE_BYTES && PIC_CELLS * 2 <= PIC_TILES_BYTES, "the cut's room");

int pic_begin(struct pic_dec *d, const struct pic_src *src, struct pic_scratch *s, u8 *table, u8 *tiles, u16 *palette)
{
	memset(d, 0, sizeof(*d));
	d->src = src;
	d->s = s;
	d->table = table;
	d->tiles = tiles;
	d->palette = palette;
	int r = pic_head(src, s, &d->info);
	if (r != PIC_OK)
		return r;
	if (d->info.pal_n) {
		u8 *p = s->buf;
		if (!src->read(src->ctx, d->info.pal_at, p, d->info.pal_n * 4))
			return PIC_BROKEN;
		for (u32 i = 0; i < d->info.pal_n; i++, p += 4)
			s->pal[i] = (u16)(p[2] >> 3 | (p[1] >> 3) << 5 | (p[0] >> 3) << 10);
	}
	memset(table, 0, PIC_TABLE_BYTES);
	s->rows = 0;
	d->info.exact = 1;
	return PIC_OK;
}

// Line k of the read (the black lines above and below a picture less tall
// than the screen first, then its rows in the file's order, so its reads
// run forward) into line, at the screen line it returns; ~0u if a read failed
static u32 read_line(struct pic_dec *d, u32 k, u16 *line)
{
	const struct pic_info *info = &d->info;
	u32 black = PIC_H - info->out_h;
	memset(line, 0, PIC_W * 2);
	if (k < black)
		return k < info->y0 ? k : k + info->out_h;
	k -= black;
	u32 oy = info->top_down ? k : info->out_h - 1 - k;
	u32 step = (info->h << 16) / info->out_h, sy = (step / 2 + oy * step) >> 16;
	const u8 *row = file_row(d->src, info, d->s, info->top_down ? sy : info->h - 1 - sy);
	if (!row)
		return ~0u;
	row_rgb15(info, d->s, row, line + info->x0);
	return info->y0 + oy;
}

int pic_step(struct pic_dec *d, u32 lines)
{
	const struct pic_src *src = d->src;
	u16 line[PIC_W];  // on the stack: IWRAM, quicker than pReadCache's EWRAM
	switch (d->phase) {
	case PIC_PHASE_READ:
		// Each line converted, kept, and written as tiles in the colours met so far
		for (; lines && d->at < PIC_H; lines--, d->at++) {
			u32 y = read_line(d, d->at, line);
			if (y == ~0u || !src->put(src->ctx, y, line))
				return PIC_BROKEN;
			if (d->info.exact &&
			    !pic_emit_exact(line, d->table, tile_row(d->tiles, y), d->palette, &d->colours))
				d->info.exact = 0;
		}
		if (d->at < PIC_H)
			return PIC_MORE;
		if (d->info.exact)
			return PIC_OK;
		// More than PIC_COLOURS: counted from the kept lines by 4-bit cells
		memset(d->table, 0, PIC_CELLS * 2);
		d->phase = PIC_PHASE_COUNT;
		d->at = 0;
		return PIC_MORE;
	case PIC_PHASE_COUNT:
		for (; lines && d->at < PIC_H; lines--, d->at++) {
			src->get(src->ctx, d->at, line);
			d->listed = pic_emit_count(line, counts(d), list(d), d->listed);
		}
		if (d->at < PIC_H)
			return PIC_MORE;
		pic_cut_begin(&d->cut, d->s->box, d->listed);
		d->phase = PIC_PHASE_CUT;
		d->at = 0;
		return PIC_MORE;
	case PIC_PHASE_CUT:
		// About PIC_CUT_CELLS cells' work a line; then the boxes' colours,
		// a few a step (at: the next box + 1)
		if (!d->at) {
			if (!pic_cut_work(&d->cut, d->s->box, counts(d), list(d), d->s->slice, PIC_COLOURS,
					  lines * PIC_CUT_CELLS)) {
				memset(boxes(d), PIC_NO_BOX, PIC_CELLS);
				d->at = 1;
			}
			return PIC_MORE;
		}
		d->colours = d->cut.boxes;
		for (u32 n = lines / 4 + 1; n && d->at <= d->colours; n--, d->at++)
			pic_cut_box(d->s->box, list(d), d->at - 1, d->palette, boxes(d));
		if (d->at <= d->colours)
			return PIC_MORE;
		// Photo-like pictures are dithered: their smooth gradients would band
		d->dither = d->listed >= PIC_DITHER_CELLS;
		d->phase = d->dither ? PIC_PHASE_FILL : PIC_PHASE_MAP;
		d->at = 0;
		return PIC_MORE;
	case PIC_PHASE_FILL:
		// Each box's whole volume, so a dithered colour finds one: a few a step
		for (u32 n = lines / 4 + 1; n && d->at < d->colours; n--, d->at++)
			pic_box_fill(&d->s->box[d->at], boxes(d), d->at);
		if (d->at < d->colours)
			return PIC_MORE;
		d->phase = PIC_PHASE_MAP;
		d->at = 0;
		return PIC_MORE;
	case PIC_PHASE_MAP:
		for (; lines && d->at < PIC_H; lines--, d->at++) {
			src->get(src->ctx, d->at, line);
			if (d->dither)
				pic_emit_dither(line, boxes(d), tile_row(d->tiles, d->at), d->at);
			else
				pic_emit_map(line, boxes(d), tile_row(d->tiles, d->at));
		}
		return d->at < PIC_H ? PIC_MORE : PIC_OK;
	default:
		return PIC_BROKEN;
	}
}

int pic_decode(const struct pic_src *src, struct pic_info *info, struct pic_scratch *s, u8 *table, u8 *tiles,
	       u16 *palette, u32 *colours)
{
	struct pic_dec d;
	int r = pic_begin(&d, src, s, table, tiles, palette);
	while (r == PIC_OK && (r = pic_step(&d, PIC_H)) == PIC_MORE)
		r = PIC_OK;
	*info = d.info;
	*colours = d.colours;
	return r;
}

// --- Median cut ---------------------------------------------------------------

#define PIC_CUT_PICK_COST 64  // a pick's work, as cells looked at (its look at every box)

// A cell's channel: 0 red, 1 green, 2 blue
static inline __attribute__((always_inline)) u32 chan(u32 c, u32 axis)
{
	return c >> (4 * axis) & 15;
}

// An empty box's sums and bounds, to add cells to
static void box_clear(struct pic_box *b)
{
	b->count = 0;
	for (u32 c = 0; c < 3; c++) {
		b->sum[c] = 0;
		b->lo[c] = 15;
		b->hi[c] = 0;
	}
}

// A box's sums and bounds, in registers while cells are added to it
struct acc {
	u32 n, r, g, b;  // pixels, and their channels' sums
	u32 r0, r1, g0, g1, b0, b1;  // bounds
};

static void acc_load(struct acc *a, const struct pic_box *b)
{
	*a = (struct acc){b->count, b->sum[0], b->sum[1], b->sum[2], b->lo[0],
			  b->hi[0], b->lo[1],  b->hi[1],  b->lo[2],  b->hi[2]};
}

static void acc_store(const struct acc *a, struct pic_box *b)
{
	b->count = a->n;
	b->sum[0] = a->r;
	b->sum[1] = a->g;
	b->sum[2] = a->b;
	b->lo[0] = (u8)a->r0;
	b->hi[0] = (u8)a->r1;
	b->lo[1] = (u8)a->g0;
	b->hi[1] = (u8)a->g1;
	b->lo[2] = (u8)a->b0;
	b->hi[2] = (u8)a->b1;
}

// Cell c's n pixels into a, each channel apart (a loop over them keeps the
// sums in memory)
static inline __attribute__((always_inline)) void acc_add(struct acc *a, u32 c, u32 n)
{
	u32 r = c & 15, g = c >> 4 & 15, b = c >> 8;
	a->n += n;
	a->r += n * r;
	a->g += n * g;
	a->b += n * b;
	if (r < a->r0)
		a->r0 = r;
	if (r > a->r1)
		a->r1 = r;
	if (g < a->g0)
		a->g0 = g;
	if (g > a->g1)
		a->g1 = g;
	if (b < a->b0)
		a->b0 = b;
	if (b > a->b1)
		a->b1 = b;
}

u32 PIC_HOT pic_box_slices(const u16 *list, u32 i, u32 end, u32 axis, u32 *slice, u32 budget)
{
	for (; i < end && budget; i++, budget--)
		slice[chan(list[i], axis)]++;
	return i;
}

u32 PIC_HOT pic_box_part(const u16 *counts, u16 *list, u32 *i, u32 *j, u32 axis, u32 cut, struct pic_box *lo,
			 struct pic_box *hi, u32 budget)
{
	u32 a = *i, b = *j;
	struct acc l, h;
	acc_load(&l, lo);
	acc_load(&h, hi);
	for (; a < b && budget; budget--) {
		u32 c = list[a];
		if (chan(c, axis) <= cut) {
			acc_add(&l, c, counts[c]);
			a++;
		} else {
			u32 t = list[--b];
			list[b] = (u16)c;
			list[a] = (u16)t;
			acc_add(&h, c, counts[c]);
		}
	}
	acc_store(&l, lo);
	acc_store(&h, hi);
	*i = a;
	*j = b;
	return a == b;
}

void PIC_HOT pic_box_fill(const struct pic_box *b, u8 *index, u32 i)
{
	for (u32 bl = b->lo[2]; bl <= b->hi[2]; bl++)
		for (u32 g = b->lo[1]; g <= b->hi[1]; g++) {
			u8 *p = index + (bl << 8 | g << 4);
			for (u32 r = b->lo[0]; r <= b->hi[0]; r++)
				if (p[r] == PIC_NO_BOX)
					p[r] = (u8)i;
		}
}

// Its longest side's channel, and the side's length in *side
static u32 box_axis(const struct pic_box *b, u32 *side)
{
	u32 axis = 0;
	for (u32 c = 1; c < 3; c++)
		if (b->hi[c] - b->lo[c] > b->hi[axis] - b->lo[axis])
			axis = c;
	*side = (u32)(b->hi[axis] - b->lo[axis]);
	return axis;
}

// Which box splits next: the most pixels times the longest side (0: one cell)
static void box_score(struct pic_box *b)
{
	u32 side;
	box_axis(b, &side);
	b->score = b->count * side;
}

void pic_cut_begin(struct pic_cut *cut, struct pic_box *box, u32 listed)
{
	memset(cut, 0, sizeof(*cut));
	memset(&box[0], 0, sizeof(box[0]));
	box_clear(&box[0]);
	box[0].end = (u16)listed;
	box_clear(&box[0]);
	box_clear(&box[1]);
	// The first box's sums and bounds come first, in pic_cut_work's steps
	cut->sub = PIC_CUT_FIRST;
	cut->i = 0;
	cut->j = listed;
	cut->boxes = listed ? 1 : 0;
}

int pic_cut_work(struct pic_cut *cut, struct pic_box *box, const u16 *counts, u16 *list, u32 *slice, u32 max,
		 u32 budget)
{
	while (budget) {
		if (cut->sub == PIC_CUT_FIRST) {
			// Every cell at most 15 along red: a partition that moves none
			u32 i = cut->i;
			u32 done = pic_box_part(counts, list, &cut->i, &cut->j, 0, 15, &box[0], &box[1], budget);
			budget -= cut->i - i;
			if (!done)
				return 1;
			box_score(&box[0]);
			cut->sub = PIC_CUT_PICK;
			continue;
		}
		if (cut->sub == PIC_CUT_PICK) {
			// The box to split, if any is left to
			u32 best = cut->boxes, score = 0;
			for (u32 i = 0; i < cut->boxes; i++)
				if (box[i].score > score) {
					score = box[i].score;
					best = i;
				}
			if (best == cut->boxes || cut->boxes >= max)
				return 0;  // every box is one cell, or there are enough
			u32 side;
			cut->best = (u16)best;
			cut->axis = (u8)box_axis(&box[best], &side);
			cut->i = box[best].start;
			memset(slice, 0, 16 * sizeof(*slice));
			cut->sub = PIC_CUT_SLICES;
			budget = budget > PIC_CUT_PICK_COST ? budget - PIC_CUT_PICK_COST : 1;  // the pick's own work
		}
		struct pic_box *b = &box[cut->best], *out = &box[cut->boxes];
		if (cut->sub == PIC_CUT_SLICES) {
			u32 to = pic_box_slices(list, cut->i, b->end, cut->axis, slice, budget);
			budget -= to - cut->i;
			cut->i = to;
			if (to < b->end)
				return 1;
			// At its cells' median along its longest side: the first cut
			// with half of them before it, never past the last slice but
			// one, so both halves keep cells
			u32 axis = cut->axis, c = b->lo[axis], below = slice[c];
			while (c + 1 < b->hi[axis] && c + 1 < 16 && 2 * below < (u32)(b->end - b->start))
				below += slice[++c];
			cut->cut = c;
			cut->i = b->start;
			cut->j = b->end;
			*out = *b;
			box_clear(b);
			box_clear(out);
			cut->sub = PIC_CUT_PART;
		}
		// Its cells split in place, each half's sums and bounds as they go
		u32 i = cut->i, j = cut->j;
		u32 done = pic_box_part(counts, list, &cut->i, &cut->j, cut->axis, cut->cut, b, out, budget);
		budget -= (cut->i - i) + (j - cut->j);  // a cell each
		if (!done)
			return 1;
		out->start = (u16)cut->i;
		b->end = (u16)cut->i;
		box_score(b);
		box_score(out);
		cut->boxes++;
		cut->sub = PIC_CUT_PICK;
	}
	return 1;
}

void pic_cut_box(const struct pic_box *box, const u16 *list, u32 i, u16 *palette, u8 *index)
{
	const struct pic_box *b = &box[i];
	// A cell's 5-bit colours are 2v and 2v + 1: their middle, 2v + 0.5
	u32 c[3];
	for (u32 k = 0; k < 3; k++) {
		c[k] = b->count ? (2 * b->sum[k] + b->count) / b->count : 0;
		c[k] = c[k] > 31 ? 31 : c[k];
	}
	palette[i] = (u16)(c[0] | c[1] << 5 | c[2] << 10);
	for (u32 k = b->start; k < b->end; k++)
		index[list[k]] = (u8)i;
}

u32 pic_median_cut(u16 *counts, u16 *list, u32 listed, struct pic_box *box, u32 *slice, u32 max, u16 *palette,
		   u8 *index)
{
	struct pic_cut cut;
	pic_cut_begin(&cut, box, listed);
	while (pic_cut_work(&cut, box, counts, list, slice, max, ~0u))
		;
	memset(index, PIC_NO_BOX, PIC_CELLS);
	for (u32 i = 0; i < cut.boxes; i++)
		pic_cut_box(box, list, i, palette, index);
	for (u32 i = 0; i < cut.boxes; i++)
		pic_box_fill(&box[i], index, i);
	return cut.boxes;
}
