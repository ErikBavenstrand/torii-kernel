// A BMP file as the screenshot viewer shows it (docs/design/v0.9/shot-viewer.md):
// fitted to the screen, its colours cut to PIC_COLOURS, written as the
// 8bpp tiles of a UI_PICTURE (30 x 20 tiles in row order, colour n of a
// pixel being BG colour PIC_FIRST_COLOUR + n). Hardware-free: the file
// comes through a read callback, so the host tests feed it from memory.
//
// Taken: BITMAPINFOHEADER and its later versions, 1, 4, 8, 16, 24 and 32
// bits a pixel, uncompressed or with bit masks, top-down or bottom-up, up
// to PIC_SIDE_MAX pixels a side with rows of up to PIC_BUF_BYTES (4,096
// pixels at 24 bits). A smaller picture is centred on black; a larger one
// shrinks to fit, keeping its shape, by nearest sampling.
//
// Colours: RGB15 (8 bits to 5 by their top bits, which gives a Torii
// screenshot's colours back exactly). The file is read once: each line
// converted, kept through the caller's put, and written as tiles in the
// colours met so far. A picture of at most PIC_COLOURS is then done. One
// with more is counted from the kept lines by 4-bit cells (listed where
// the tiles go), which keeps the cut quick, cut into PIC_COLOURS boxes by
// median cut (the box with the most pixels times its longest side split at
// its cells' median), each its pixels' mean, and its kept lines written as
// tiles again; with PIC_DITHER_CELLS cells or more (a photo, not a game's
// frame) through Bayer's ordered dither, so gradients don't band.
//
// A decode can run in steps (pic_begin, pic_step), between frames.
#ifndef TORII_PICTURE_H
#define TORII_PICTURE_H

#include <gba_base.h>

#define PIC_W 240
#define PIC_H 160
#define PIC_TILES_BYTES (PIC_W * PIC_H)  // 600 8bpp tiles
#define PIC_FIRST_COLOUR 48  // past the UI's banks 0-2 (UI_PICTURE_FIRST)
#define PIC_COLOURS 176  // banks 3-13: 14 and 15 stay a confirmation's (UI_PICTURE_COLOURS)
#define PIC_TABLE_BYTES 0x8000  // a byte an RGB15 colour
#define PIC_CELLS 4096  // the cut's colours: 4 bits a channel
#define PIC_NO_BOX 0xFF  // a cell in no box
#define PIC_DITHER_CELLS 1024  // a picture with this many cells or more is dithered
#define PIC_BUF_BYTES 0x3000  // the rows read at a time; one row must fit
#define PIC_SIDE_MAX 8192
#define PIC_HEAD_BYTES 138  // the file header and the largest info header

enum {
	PIC_OK,
	PIC_BROKEN,  // not a BMP, cut short, or a read failed
	PIC_UNSUPPORTED,  // a BMP of a kind it doesn't take
	PIC_MORE,  // pic_step: not done yet
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
	u8 exact;  // once decoded: 1 if the colours were kept as they were
};

// A median cut's box: its cells in the list, and their bounds
struct pic_box {
	u16 start, end;  // [start, end) of the list
	u8 lo[3], hi[3];  // red, green, blue: 0-15 each, inclusive
	u32 count;  // pixels
	u32 sum[3];
	u32 score;  // which splits first
};

// Decode-only room, besides the colour table
struct pic_scratch {
	u16 pal[256];  // the file's colour table, as RGB15
	struct pic_box box[PIC_COLOURS];
	u32 slice[16];
	u32 first, rows;  // the file rows in buf
	u32 read_max;  // bytes a read of rows takes at most (whole rows, one at least); 0: PIC_BUF_BYTES
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

// A median cut under way (pic_cut_begin, pic_cut_work)
enum {
	PIC_CUT_PICK,
	PIC_CUT_SLICES,
	PIC_CUT_PART,
	PIC_CUT_FIRST
};
struct pic_cut {
	u32 boxes;  // so far
	u32 i, j, cut;  // the split under way: its cursors and cut
	u16 best;  // the box it splits
	u8 axis, sub;  // its channel, and PIC_CUT_*
};
#define PIC_CUT_CELLS 256  // a step's cut work a line: cells looked at

// A decode under way (pic_begin, then pic_step until it isn't PIC_MORE):
// everything it keeps between steps
enum {
	PIC_PHASE_READ,
	PIC_PHASE_COUNT,
	PIC_PHASE_CUT,
	PIC_PHASE_FILL,
	PIC_PHASE_MAP
};
struct pic_dec {
	const struct pic_src *src;
	struct pic_scratch *s;
	u8 *table, *tiles;
	u16 *palette;
	struct pic_info info;
	u32 colours;  // the palette's, so far
	u32 phase, at;  // the phase, and its next line
	u32 listed;  // the cut's cells
	struct pic_cut cut;
	u8 dither;
};

// The header: PIC_OK and info filled in, or why not
int pic_head(const struct pic_src *src, struct pic_scratch *s, struct pic_info *info);

// Starts decoding src into tiles (PIC_TILES_BYTES, word-aligned) and its
// colours into palette (PIC_COLOURS), with table (PIC_TABLE_BYTES) and s:
// PIC_OK, or why it can't (the header's). Every pointer is kept until done.
int pic_begin(struct pic_dec *d, const struct pic_src *src, struct pic_scratch *s, u8 *table, u8 *tiles, u16 *palette);
// About lines lines' work more (a read of the file's rows, a pass over the
// kept lines, or some of the cut): PIC_MORE, then PIC_OK once done
// (d->colours the palette's), or PIC_BROKEN if a read or a put failed.
int pic_step(struct pic_dec *d, u32 lines);
// The whole decode at once; info gets pic_head's and whether it was exact
int pic_decode(const struct pic_src *src, struct pic_info *info, struct pic_scratch *s, u8 *table, u8 *tiles,
	       u16 *palette, u32 *colours);

// The median cut alone, for tests: counts holds each 4-bit cell's pixels,
// and list the listed cells that have some (reordered). Fills palette
// with the boxes' colours (at most max) and index with each cell's box
// (PIC_CELLS: the boxes' whole volumes, PIC_NO_BOX outside them). Returns
// the boxes.
u32 pic_median_cut(u16 *counts, u16 *list, u32 listed, struct pic_box *box, u32 *slice, u32 max, u16 *palette,
		   u8 *index);
// The same in steps: begin; work, about budget cells looked at a call,
// until it returns 0 (cut->boxes the boxes); then, with index all
// PIC_NO_BOX, each box's colour and its listed cells (pic_cut_box), and
// the rest of each box's volume (pic_box_fill, below).
void pic_cut_begin(struct pic_cut *cut, struct pic_box *box, u32 listed);
int pic_cut_work(struct pic_cut *cut, struct pic_box *box, const u16 *counts, u16 *list, u32 *slice, u32 max,
		 u32 budget);
void pic_cut_box(const struct pic_box *box, const u16 *list, u32 i, u16 *palette, u8 *index);

// The hot loops, ARM code in IWRAM on the GBA (extern, so GCC keeps them there):
// a 24-bit row's n samples from 16.16 column at by step; a line of RGB15
// into its tiles' rows (row: the line's first tile's) as the colours met
// so far (0 once more than PIC_COLOURS are), through the cells' boxes, or
// through them dithered (y the line's); and a line's cells counted, each
// new one listed. Then the median cut's, each over at most budget cells:
// cells [i, end) counted into 16 slices along axis (returns where it got
// to); cells [*i, *j) partitioned in place by axis, those at most cut
// first, each added to lo's or hi's sums and bounds (1 once *i meets *j);
// and the cells of a box's volume in no box yet given box i.
#ifdef __arm__
#define PIC_HOT IWRAM_CODE __attribute__((target("arm")))
#else
#define PIC_HOT
#endif
void PIC_HOT pic_row24(const u8 *row, u32 at, u32 step, u32 n, u16 *out);
int PIC_HOT pic_emit_exact(const u16 *line, u8 *table, u8 *row, u16 *palette, u32 *colours);
void PIC_HOT pic_emit_map(const u16 *line, const u8 *index, u8 *row);
void PIC_HOT pic_emit_dither(const u16 *line, const u8 *index, u8 *row, u32 y);
u32 PIC_HOT pic_emit_count(const u16 *line, u16 *counts, u16 *list, u32 n);
u32 PIC_HOT pic_box_slices(const u16 *list, u32 i, u32 end, u32 axis, u32 *slice, u32 budget);
u32 PIC_HOT pic_box_part(const u16 *counts, u16 *list, u32 *i, u32 *j, u32 axis, u32 cut, struct pic_box *lo,
			 struct pic_box *hi, u32 budget);
void PIC_HOT pic_box_fill(const struct pic_box *b, u8 *index, u32 i);

#endif
