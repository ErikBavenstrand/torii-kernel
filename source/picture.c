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
		n = PIC_BUF_BYTES / info->row_bytes;
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

// A line as the colours met so far (table: each one's index + 1); a run
// of one colour looks it up once
int PIC_HOT pic_emit_exact(const u16 *line, u16 *table, u8 *row, u16 *palette, u32 *colours)
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
						table[c] = (u16)t;
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

// A line through the table's indices, likewise
void PIC_HOT pic_emit_map(const u16 *line, const u16 *table, u8 *row, u32 mask)
{
	u32 last = 0x10000, index = 0;
	for (u32 tx = 0; tx < PIC_W / 8; tx++, row += 64)
		for (u32 k = 0; k < 2; k++) {
			u32 w = 0;
			for (u32 b = 0; b < 32; b += 8) {
				u32 c = *line++ & mask;
				if (c != last) {
					last = c;
					index = PIC_FIRST_COLOUR + table[c];
				}
				w |= index << b;
			}
			((u32 *)(void *)row)[k] = w;
		}
}

u32 PIC_HOT pic_emit_count(const u16 *line, u16 *table, u16 *list, u32 n, u32 max, u32 mask)
{
	for (u32 x = 0; x < PIC_W;) {
		u32 c = line[x] & mask, run = 1;
		for (x++; x < PIC_W && (line[x] & mask) == c; x++)
			run++;
		if (!table[c]) {
			if (n == max)
				return max + 1;
			list[n++] = (u16)c;
		}
		table[c] = (u16)(table[c] + run);  // at most PIC_W * PIC_H pixels: no overflow
	}
	return n;
}

// The file read once: each line converted, kept, and written as tiles in
// the colours met so far until there are more than PIC_COLOURS (then
// *colours is past it); PIC_OK or PIC_BROKEN
static int read_pass(const struct pic_src *src, const struct pic_info *info, struct pic_scratch *s, u16 *table,
		     u8 *tiles, u16 *palette, u32 *colours)
{
	u16 line[PIC_W];  // on the stack: IWRAM, quicker than pReadCache's EWRAM
	memset(line, 0, sizeof(line));
	u32 more = 0;
	// The black above and below a picture less tall than the screen
	for (u32 y = 0; y < PIC_H; y++)
		if (y < info->y0 || y >= info->y0 + info->out_h) {
			more = more || !pic_emit_exact(line, table, tile_row(tiles, y), palette, colours);
			if (!src->put(src->ctx, y, line))
				return PIC_BROKEN;
		}
	// The rows in the file's order, so its reads run forward
	u32 step = (info->h << 16) / info->out_h;
	s->rows = 0;
	for (u32 k = 0; k < info->out_h; k++) {
		u32 oy = info->top_down ? k : info->out_h - 1 - k;
		u32 sy = (step / 2 + oy * step) >> 16;
		const u8 *row = file_row(src, info, s, info->top_down ? sy : info->h - 1 - sy);
		if (!row)
			return PIC_BROKEN;
		row_rgb15(info, s, row, line + info->x0);
		more = more || !pic_emit_exact(line, table, tile_row(tiles, info->y0 + oy), palette, colours);
		if (!src->put(src->ctx, info->y0 + oy, line))
			return PIC_BROKEN;
	}
	*colours += more;
	return PIC_OK;
}

// The kept lines counted into the table, each colour listed; past max, max + 1
static u32 count_pass(const struct pic_src *src, u16 *table, u16 *list, u32 max, u32 mask)
{
	u16 line[PIC_W];
	u32 n = 0;
	memset(table, 0, PIC_TABLE_HALVES * 2);
	for (u32 y = 0; y < PIC_H && n <= max; y++) {
		src->get(src->ctx, y, line);
		n = pic_emit_count(line, table, list, n, max, mask);
	}
	return n;
}

int pic_decode(const struct pic_src *src, struct pic_info *info, u16 *table, struct pic_scratch *s, u8 *tiles,
	       u16 *palette, u32 *colours)
{
	if (info->pal_n) {
		u8 *p = s->buf;
		if (!src->read(src->ctx, info->pal_at, p, info->pal_n * 4))
			return PIC_BROKEN;
		for (u32 i = 0; i < info->pal_n; i++, p += 4)
			s->pal[i] = (u16)(p[2] >> 3 | (p[1] >> 3) << 5 | (p[0] >> 3) << 10);
	}
	memset(table, 0, PIC_TABLE_HALVES * 2);
	*colours = 0;
	int r = read_pass(src, info, s, table, tiles, palette, colours);
	info->exact = *colours <= PIC_COLOURS;
	if (r != PIC_OK || info->exact)
		return r;
	// More: counted from the kept lines at 4 bits a channel (4,096 colours
	// at most, so the cut stays quick), their colours listed where the
	// tiles go
	u16 *list = (u16 *)(void *)tiles, line[PIC_W];
	u32 mask = 0x7BDE, listed = count_pass(src, table, list, PIC_LIST_MAX, mask);
	*colours = pic_median_cut(table, list, listed, s->box, s->slice, PIC_COLOURS, palette);
	for (u32 y = 0; y < PIC_H; y++) {
		src->get(src->ctx, y, line);
		pic_emit_map(line, table, tile_row(tiles, y), mask);
	}
	return PIC_OK;
}

// --- Median cut ---------------------------------------------------------------

void PIC_HOT pic_box_slices(const u16 *list, const struct pic_box *b, u32 axis, u32 *slice)
{
	u32 shift = 5 * axis;
	for (u32 k = 0; k < 32; k++)
		slice[k] = 0;
	for (u32 i = b->start; i < b->end; i++)
		slice[list[i] >> shift & 31]++;
}

u32 PIC_HOT pic_box_part(u16 *list, u32 i, u32 j, u32 shift, u32 cut)
{
	while (i < j) {
		if ((list[i] >> shift & 31) <= cut) {
			i++;
		} else {
			u16 t = list[--j];
			list[j] = list[i];
			list[i] = t;
		}
	}
	return i;
}

void PIC_HOT pic_box_stats(const u16 *table, const u16 *list, struct pic_box *b)
{
	u32 count = 0, sum[3] = {0, 0, 0};
	u32 lr = 31, lg = 31, lb = 31, hr = 0, hg = 0, hb = 0;
	for (u32 i = b->start; i < b->end; i++) {
		u32 c = list[i], n = table[c], r = c & 31, g = c >> 5 & 31, bl = c >> 10;
		count += n;
		sum[0] += n * r;
		sum[1] += n * g;
		sum[2] += n * bl;
		lr = r < lr ? r : lr;
		hr = r > hr ? r : hr;
		lg = g < lg ? g : lg;
		hg = g > hg ? g : hg;
		lb = bl < lb ? bl : lb;
		hb = bl > hb ? bl : hb;
	}
	b->count = count;
	b->lo[0] = (u8)lr;
	b->lo[1] = (u8)lg;
	b->lo[2] = (u8)lb;
	b->hi[0] = (u8)hr;
	b->hi[1] = (u8)hg;
	b->hi[2] = (u8)hb;
	for (u32 c = 0; c < 3; c++)
		b->sum[c] = sum[c];
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

// Which box splits next: the most pixels times the longest side (0: one colour)
static void box_score(struct pic_box *b)
{
	u32 side;
	box_axis(b, &side);
	b->score = b->count * side;
}

// Splits b at its colours' median along its longest side into b and out:
// its colours partitioned in place, those below the cut first
static void box_split(const u16 *table, u16 *list, struct pic_box *b, struct pic_box *out, u32 *slice)
{
	u32 side, axis = box_axis(b, &side);
	pic_box_slices(list, b, axis, slice);
	// The first cut with half the colours before it; never past the last
	// slice but one, so both halves keep colours
	u32 cut = b->lo[axis], below = slice[cut];
	while (cut + 1 < b->hi[axis] && cut + 1 < 32 && 2 * below < (u32)(b->end - b->start))
		below += slice[++cut];
	u32 i = pic_box_part(list, b->start, b->end, 5 * axis, cut);
	*out = *b;
	out->start = (u16)i;
	b->end = (u16)i;
	pic_box_stats(table, list, b);
	pic_box_stats(table, list, out);
	box_score(b);
	box_score(out);
}

u32 pic_median_cut(u16 *table, u16 *list, u32 listed, struct pic_box *box, u32 *slice, u32 max, u16 *palette)
{
	memset(&box[0], 0, sizeof(box[0]));
	box[0].end = (u16)listed;
	pic_box_stats(table, list, &box[0]);
	box_score(&box[0]);
	u32 n = listed ? 1 : 0;
	while (n && n < max) {
		u32 best = n, score = 0;
		for (u32 i = 0; i < n; i++)
			if (box[i].score > score) {
				score = box[i].score;
				best = i;
			}
		if (best == n)
			break;  // every box is one colour
		box_split(table, list, &box[best], &box[n], slice);
		n++;
	}
	for (u32 i = 0; i < n; i++) {
		struct pic_box *b = &box[i];
		u32 c[3];
		for (u32 k = 0; k < 3; k++)
			c[k] = b->count ? (b->sum[k] + b->count / 2) / b->count : 0;
		palette[i] = (u16)(c[0] | c[1] << 5 | c[2] << 10);
		for (u32 k = b->start; k < b->end; k++)
			table[list[k]] = (u16)i;
	}
	return n;
}
