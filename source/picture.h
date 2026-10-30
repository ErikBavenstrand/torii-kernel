// A BMP file as the screenshot viewer shows it (docs/design/v0.9/shot-viewer.md):
// fitted to the screen, its colours cut to PIC_COLOURS, written as the
// 8bpp tiles of a UI_PICTURE (30 x 20 tiles in row order, colour n of a
// pixel being BG colour PIC_FIRST_COLOUR + n). Hardware-free: the file
// comes through a read callback, so the host tests feed it from memory.
//
// Taken: BITMAPINFOHEADER and its later versions, 1, 4, 8, 16, 24 and 32
// bits a pixel, uncompressed or with bit masks, top-down or bottom-up, up
// to PIC_SIDE_MAX pixels a side with rows of up to PIC_BUF_BYTES. A
// smaller picture is centred on black; a larger one shrinks to fit,
// keeping its shape, by nearest sampling.
//
// Colours: RGB15 (8 bits to 5 by their top bits, which gives a Torii
// screenshot's colours back exactly). The file is read once: each line
// converted, kept through the caller's put, and written as tiles in the
// colours met so far. A picture of at most PIC_COLOURS is then done. One
// with more is counted from the kept lines (its colours listed where the
// tiles go) at 4 bits a channel, which keeps the cut quick, cut into
// PIC_COLOURS boxes by median cut (the box with the most pixels times its
// longest side split at its colours' median), each its pixels' mean, and
// its kept lines written as tiles again.
#ifndef TORII_PICTURE_H
#define TORII_PICTURE_H

#include <gba_base.h>

#define PIC_W 240
#define PIC_H 160
#define PIC_TILES_BYTES (PIC_W * PIC_H)  // 600 8bpp tiles
#define PIC_FIRST_COLOUR 48  // past the UI's banks 0-2 (UI_PICTURE_FIRST)
#define PIC_COLOURS 176  // banks 3-13: 14 and 15 stay a confirmation's (UI_PICTURE_COLOURS)
#define PIC_TABLE_HALVES 0x8000  // a halfword an RGB15 colour
#define PIC_LIST_MAX 4096  // colours the median cut takes: 4 bits a channel
#define PIC_BUF_BYTES 0x4000  // the rows read at a time; one row must fit
#define PIC_SIDE_MAX 8192
#define PIC_HEAD_BYTES 138  // the file header and the largest info header

enum {
	PIC_OK,
	PIC_BROKEN,  // not a BMP, cut short, or a read failed
	PIC_UNSUPPORTED,  // a BMP of a kind it doesn't take
};

struct pic_info {
	u32 w, h;  // the file's picture
	u32 out_w, out_h, x0, y0;  // where it shows, fitted
	u32 data;  // the pixels' offset
	u32 row_bytes;
	u16 bpp;
	u8 top_down;
	u8 masked;  // 16 or 32 bits through masks
	u32 mask[3];  // red, green, blue
	u8 shift[3], bits[3];
	u32 pal_at, pal_n;  // 1, 4 and 8 bits: the colour table
	u8 exact;  // pic_decode: 1 if the colours were kept as they were
};

// A median cut's box: its colours in the list, and their bounds
struct pic_box {
	u16 start, end;  // [start, end) of the list
	u8 lo[3], hi[3];  // red, green, blue: 0-31 each, inclusive
	u32 count;  // pixels
	u32 sum[3];
	u32 score;  // which splits first
};

// Decode-only room, besides the colour table
struct pic_scratch {
	u16 pal[256];  // the file's colour table, as RGB15
	struct pic_box box[PIC_COLOURS];
	u32 slice[32];
	u32 first, rows;  // the file rows in buf
	u8 buf[PIC_BUF_BYTES] __attribute__((aligned(4)));
};

// The file, and room for the picture's PIC_H lines of RGB15 between its passes
struct pic_src {
	// Reads n bytes at offset into buf (word-aligned); 1 if all were read
	int (*read)(void *ctx, u32 offset, void *buf, u32 n);
	// Keeps line y (PIC_W halfwords); 0 if it couldn't
	int (*put)(void *ctx, u32 y, const u16 *line);
	// Gives line y back
	void (*get)(void *ctx, u32 y, u16 *line);
	void *ctx;
	u32 size;  // the file's bytes
};

// The header: PIC_OK and info filled in, or why not
int pic_head(const struct pic_src *src, struct pic_scratch *s, struct pic_info *info);

// The picture into tiles (PIC_TILES_BYTES, word-aligned) and its colours
// into palette (PIC_COLOURS), their count in *colours. table is
// PIC_TABLE_HALVES of room. info is pic_head's, then says whether the
// colours were exact. PIC_OK, or PIC_BROKEN if a read or a put failed.
int pic_decode(const struct pic_src *src, struct pic_info *info, u16 *table, struct pic_scratch *s, u8 *tiles,
	       u16 *palette, u32 *colours);

// The median cut alone, for tests: table holds each RGB15 colour's pixel
// count, and list the listed colours that have some. On return each of
// their entries is its box's index, and palette the boxes' colours (at
// most max); list is reordered. Returns the boxes.
u32 pic_median_cut(u16 *table, u16 *list, u32 listed, struct pic_box *box, u32 *slice, u32 max, u16 *palette);

// The hot loops, ARM code in IWRAM on the GBA (extern, so GCC keeps them there):
// a 24-bit row's n samples from 16.16 column at by step; a line of RGB15
// into its tiles' rows (row: the line's first tile's) through the table's
// indices; a line counted into the table, each new colour listed (max + 1
// past max); and a line as the colours met so far (0 once more than
// PIC_COLOURS are)
#ifdef __arm__
#define PIC_HOT IWRAM_CODE __attribute__((target("arm")))
#else
#define PIC_HOT
#endif
void PIC_HOT pic_row24(const u8 *row, u32 at, u32 step, u32 n, u16 *out);
void PIC_HOT pic_emit_map(const u16 *line, const u16 *table, u8 *row, u32 mask);
u32 PIC_HOT pic_emit_count(const u16 *line, u16 *table, u16 *list, u32 n, u32 max, u32 mask);
int PIC_HOT pic_emit_exact(const u16 *line, u16 *table, u8 *row, u16 *palette, u32 *colours);
// The median cut's: a box's pixels, sums and bounds from its colours; its
// colours in each of 32 slices along axis; its colours [i, j) partitioned
// in place by the channel at shift, those at most cut first (returns
// where the rest start)
void PIC_HOT pic_box_stats(const u16 *table, const u16 *list, struct pic_box *b);
void PIC_HOT pic_box_slices(const u16 *list, const struct pic_box *b, u32 axis, u32 *slice);
u32 PIC_HOT pic_box_part(u16 *list, u32 i, u32 j, u32 shift, u32 cut);

#endif
