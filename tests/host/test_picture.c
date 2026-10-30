// The screenshot viewer's BMP decoder (source/picture.c) and its shots'
// names (source/shotname.c), on files built here.
#include <stdlib.h>
#include <string.h>

#include "../../source/picture.h"
#include "../../source/ppu_render.h"
#include "../../source/shotname.h"
#include "../kernel/torii_test.h"
#include "tests.h"

struct file {
	u8 *bytes;
	u32 size;
	u32 reads;
	u16 lines[PIC_H][PIC_W];
};

static int t_read(void *ctx, u32 offset, void *buf, u32 n)
{
	struct file *f = ctx;
	f->reads++;
	if (offset > f->size || n > f->size - offset)
		return 0;
	memcpy(buf, f->bytes + offset, n);
	return 1;
}

static int t_put(void *ctx, u32 y, const u16 *line)
{
	memcpy(((struct file *)ctx)->lines[y], line, PIC_W * 2);
	return 1;
}

static void t_get(void *ctx, u32 y, u16 *line)
{
	memcpy(line, ((struct file *)ctx)->lines[y], PIC_W * 2);
}

static void put16(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
}

static void put32(u8 *p, u32 v)
{
	put16(p, v);
	put16(p + 2, v >> 16);
}

// A BMP of w x h at bpp with a 40-byte header (masks after it for
// comp 3), pal_n colours, bottom-up unless top_down; pixel(x, y) gives each
// pixel's stored value (an index, or the packed pixel's bits). The pixels
// are written as stored: 24 bits as B, G, R.
static struct file bmp_make(u32 w, u32 h, u32 bpp, u32 comp, const u32 *masks, u32 pal_n, const u32 *pal, int top_down,
			    u32 (*pixel)(u32 x, u32 y))
{
	u32 row = (w * bpp + 31) / 32 * 4, extra = comp == 3 ? 12 : 0;
	u32 data = 54 + extra + pal_n * 4, size = data + row * h;
	struct file f;
	memset(&f, 0, sizeof(f));
	f.bytes = calloc(1, size);
	f.size = size;
	u8 *b = f.bytes;
	b[0] = 'B';
	b[1] = 'M';
	put32(b + 2, size);
	put32(b + 10, data);
	put32(b + 14, 40);
	put32(b + 18, w);
	put32(b + 22, top_down ? (u32) - (s32)h : h);
	put16(b + 26, 1);
	put16(b + 28, bpp);
	put32(b + 30, comp);
	put32(b + 46, pal_n);
	for (u32 c = 0; c < 3 && extra; c++)
		put32(b + 54 + 4 * c, masks[c]);
	for (u32 i = 0; i < pal_n; i++)
		put32(b + 54 + extra + 4 * i, pal[i]);
	for (u32 y = 0; y < h; y++) {
		u8 *r = b + data + (top_down ? y : h - 1 - y) * row;
		for (u32 x = 0; x < w; x++) {
			u32 v = pixel(x, y);
			switch (bpp) {
			case 1:
				r[x / 8] |= (u8)(v << (7 - x % 8));
				break;
			case 4:
				r[x / 2] |= (u8)(v << (x % 2 ? 0 : 4));
				break;
			case 8:
				r[x] = (u8)v;
				break;
			case 16:
				put16(r + 2 * x, v);
				break;
			case 24:
				r[3 * x] = (u8)v;
				r[3 * x + 1] = (u8)(v >> 8);
				r[3 * x + 2] = (u8)(v >> 16);
				break;
			default:
				put32(r + 4 * x, v);
				break;
			}
		}
	}
	return f;
}

struct decoded {
	struct pic_info info;
	struct pic_scratch s;
	u8 table[PIC_TABLE_BYTES];
	u8 tiles[PIC_TILES_BYTES];
	u16 pal[PIC_COLOURS];
	u32 colours;
	int head, result;
};

static struct decoded *decode(struct file *f)
{
	struct decoded *d = calloc(1, sizeof(*d));
	struct pic_src src = {t_read, t_put, t_get, f, f->size};
	d->head = pic_head(&src, &d->s, &d->info);
	d->result = d->head == PIC_OK ? pic_decode(&src, &d->info, &d->s, d->table, d->tiles, d->pal, &d->colours)
				      : d->head;
	return d;
}

// The same decode in steps of lines lines, as the viewer's read-ahead runs it
static struct decoded *decode_steps(struct file *f, u32 lines, u32 *steps)
{
	struct decoded *d = calloc(1, sizeof(*d));
	struct pic_src src = {t_read, t_put, t_get, f, f->size};
	struct pic_dec dec;
	d->result = pic_begin(&dec, &src, &d->s, d->table, d->tiles, d->pal);
	*steps = 0;
	while (d->result == PIC_OK && (d->result = pic_step(&dec, lines)) == PIC_MORE) {
		d->result = PIC_OK;
		++*steps;
	}
	d->info = dec.info;
	d->colours = dec.colours;
	return d;
}

// The colour shown at screen pixel (x, y), RGB15; 0xFFFF for an index out of the palette
static u32 shown(const struct decoded *d, u32 x, u32 y)
{
	u32 i = d->tiles[((y / 8) * (PIC_W / 8) + x / 8) * 64 + (y % 8) * 8 + x % 8];
	if (i < PIC_FIRST_COLOUR || i - PIC_FIRST_COLOUR >= d->colours)
		return 0xFFFF;
	return d->pal[i - PIC_FIRST_COLOUR];
}

static u32 rgb15(u32 r, u32 g, u32 b)
{
	return r | g << 5 | b << 10;
}

// 24-bit test pattern: few colours (8 levels of red by 4 of blue)
static u32 px_few(u32 x, u32 y)
{
	return ((x / 30) * 32) << 16 | ((y / 40) * 64);
}

static void test_picture_exact_24(void)
{
	for (int td = 0; td < 2; td++) {
		struct file f = bmp_make(240, 160, 24, 0, NULL, 0, NULL, td, px_few);
		struct decoded *d = decode(&f);
		CHECK_EQ(d->result, PIC_OK);
		CHECK(d->info.exact);
		CHECK_EQ(d->colours, 32u);
		u32 bad = 0;
		for (u32 y = 0; y < 160; y++)
			for (u32 x = 0; x < 240; x++)
				bad += shown(d, x, y) != rgb15((x / 30) * 4, 0, (y / 40) * 8);
		CHECK_EQ(bad, 0u);
		free(d);
		free(f.bytes);
	}
}

// A Torii screenshot's lines (ppu_bmp_row) give their RGB15 back exactly
static u32 px_shot(u32 x, u32 y)
{
	u16 one[PPU_W] = {0};
	one[0] = (u16)((x * 7 + y * 3) % 150 * 163 & 0x7FFF);  // 150 colours spread over RGB15
	u8 row[PPU_BMP_ROW];
	ppu_bmp_row(one, row);
	return (u32)row[2] << 16 | (u32)row[1] << 8 | row[0];
}

static void test_picture_torii_shot(void)
{
	struct file f = bmp_make(240, 160, 24, 0, NULL, 0, NULL, 1, px_shot);
	struct decoded *d = decode(&f);
	CHECK_EQ(d->result, PIC_OK);
	CHECK(d->info.exact);
	u32 bad = 0;
	for (u32 y = 0; y < 160; y++)
		for (u32 x = 0; x < 240; x++)
			bad += shown(d, x, y) != ((x * 7 + y * 3) % 150 * 163 & 0x7FFF);
	CHECK_EQ(bad, 0u);
	CHECK(f.reads <= 1 + 160 * 720 / PIC_BUF_BYTES + 2);  // the header, then runs of rows
	free(d);
	free(f.bytes);
}

// Every RGB15 colour a pixel: median cut down to PIC_COLOURS
static u32 px_many(u32 x, u32 y)
{
	u32 c = (y * 240 + x) * 7 % 32768;
	return ((c & 31) << 3) << 16 | ((c >> 5 & 31) << 3) << 8 | ((c >> 10) << 3);
}

// 1,000 colours: cut at 5 bits a channel
static u32 px_mid(u32 x, u32 y)
{
	u32 c = (x / 3 + y / 4 * 80) % 1000 * 29 % 32768;
	return ((c & 31) << 3) << 16 | ((c >> 5 & 31) << 3) << 8 | ((c >> 10) << 3);
}

static void test_picture_median_cut_mid(void)
{
	struct file f = bmp_make(240, 160, 24, 0, NULL, 0, NULL, 0, px_mid);
	struct decoded *d = decode(&f);
	CHECK_EQ(d->result, PIC_OK);
	CHECK(!d->info.exact);
	CHECK_EQ(d->colours, (u32)PIC_COLOURS);
	u32 bad = 0;
	for (u32 y = 0; y < 160; y++)
		for (u32 x = 0; x < 240; x++)
			bad += shown(d, x, y) == 0xFFFF;
	CHECK_EQ(bad, 0u);
	free(d);
	free(f.bytes);
}

static void test_picture_median_cut(void)
{
	struct file f = bmp_make(240, 160, 24, 0, NULL, 0, NULL, 0, px_many);
	struct decoded *d = decode(&f);
	CHECK_EQ(d->result, PIC_OK);
	CHECK(!d->info.exact);
	CHECK_EQ(d->colours, (u32)PIC_COLOURS);
	// Every pixel maps to a colour of the palette near its own
	u32 bad = 0, far = 0;
	for (u32 y = 0; y < 160; y++)
		for (u32 x = 0; x < 240; x++) {
			u32 s = shown(d, x, y), c = (y * 240 + x) * 7 % 32768;
			if (s == 0xFFFF) {
				bad++;
				continue;
			}
			for (u32 k = 0; k < 15; k += 5) {
				s32 diff = (s32)(s >> k & 31) - (s32)(c >> k & 31);
				far += diff > 8 || diff < -8;
			}
		}
	CHECK_EQ(bad, 0u);
	CHECK(far < 240 * 160 / 50);
	free(d);
	free(f.bytes);
}

// A 4-bit cell: red, green, blue
static u32 cellof(u32 r, u32 g, u32 b)
{
	return r | g << 4 | b << 8;
}

static void test_picture_median_cut_boxes(void)
{
	static u16 counts[PIC_CELLS];
	static u8 index[PIC_CELLS];
	static struct pic_box box[PIC_COLOURS];
	u32 slice[16];
	u16 pal[PIC_COLOURS], list[2] = {(u16)cellof(1, 2, 3), (u16)cellof(14, 10, 5)};
	memset(counts, 0, sizeof(counts));
	// Two cells: two boxes, each its cell's middle (5 bits: 2v + 1)
	counts[cellof(1, 2, 3)] = 10;
	counts[cellof(14, 10, 5)] = 5;
	CHECK_EQ(pic_median_cut(counts, list, 2, box, slice, PIC_COLOURS, pal, index), 2u);
	CHECK(pal[index[cellof(1, 2, 3)]] == rgb15(3, 5, 7));
	CHECK(pal[index[cellof(14, 10, 5)]] == rgb15(29, 21, 11));
	CHECK_EQ(index[cellof(0, 0, 0)], PIC_NO_BOX);  // outside both
	// One: one box
	memset(counts, 0, sizeof(counts));
	counts[cellof(5, 5, 5)] = 1;
	list[0] = (u16)cellof(5, 5, 5);
	CHECK_EQ(pic_median_cut(counts, list, 1, box, slice, 4, pal, index), 1u);
	CHECK_EQ(pal[0], rgb15(11, 11, 11));
}

// Steps of any size make the same picture as one decode
static void test_picture_steps(void)
{
	u32 (*const kinds[])(u32, u32) = {px_few, px_many};
	for (u32 k = 0; k < 2; k++) {
		struct file f = bmp_make(240, 160, 24, 0, NULL, 0, NULL, 0, kinds[k]);
		struct decoded *one = decode(&f);
		u32 steps;
		struct decoded *some = decode_steps(&f, 7, &steps);
		CHECK_EQ(one->result, PIC_OK);
		CHECK_EQ(some->result, PIC_OK);
		CHECK(steps > 20);
		CHECK_EQ(one->colours, some->colours);
		CHECK(!memcmp(one->tiles, some->tiles, sizeof(one->tiles)));
		CHECK(!memcmp(one->pal, some->pal, one->colours * 2));
		free(one);
		free(some);
		free(f.bytes);
	}
}

static u32 px_index(u32 x, u32 y)
{
	return (x + y) % 2;
}
static u32 px_index4(u32 x, u32 y)
{
	return (x / 4 + y) % 16;
}
static u32 px_index8(u32 x, u32 y)
{
	return (x * 3 + y) % 150;  // at most PIC_COLOURS: exact
}

static void test_picture_indexed(void)
{
	u32 pal[256];
	for (u32 i = 0; i < 256; i++)
		pal[i] = (i * 8 % 256) << 16 | (i / 32 * 32) << 8 | (255 - i);
	struct {
		u32 bpp;
		u32 (*px)(u32, u32);
	} kinds[] = {{1, px_index}, {4, px_index4}, {8, px_index8}};
	for (u32 k = 0; k < 3; k++) {
		u32 n = kinds[k].bpp == 8 ? 150 : 1u << kinds[k].bpp;
		struct file f = bmp_make(240, 160, kinds[k].bpp, 0, NULL, n, pal, 0, kinds[k].px);
		struct decoded *d = decode(&f);
		CHECK_EQ(d->result, PIC_OK);
		u32 bad = 0;
		for (u32 y = 0; y < 160; y++)
			for (u32 x = 0; x < 240; x++) {
				u32 c = pal[kinds[k].px(x, y)];
				u32 want = rgb15((c >> 16 & 255) >> 3, (c >> 8 & 255) >> 3, (c & 255) >> 3);
				bad += shown(d, x, y) != want;
			}
		CHECK_EQ(bad, 0u);
		free(d);
		free(f.bytes);
	}
}

static u32 px_565(u32 x, u32 y)
{
	return (x % 32) << 11 | (y / 16 % 4) << 5 | 31;  // 128 colours: exact
}
static u32 px_555(u32 x, u32 y)
{
	return x << 10 | (y / 8) << 5 | 7;  // 32 colours: exact
}
static u32 px_x888(u32 x, u32 y)
{
	(void)y;
	return 0xFF000000u | (x % 256) << 16 | 0x2000 | 0x10;  // 30 colours: exact
}

static void test_picture_masks(void)
{
	const u32 m565[3] = {0xF800, 0x07E0, 0x001F};
	struct file f = bmp_make(32, 64, 16, 3, m565, 0, NULL, 1, px_565);
	struct decoded *d = decode(&f);
	CHECK_EQ(d->result, PIC_OK);
	CHECK_EQ(d->info.out_w, 32u);
	CHECK_EQ(d->info.x0, 104u);
	CHECK_EQ(d->info.y0, 48u);
	u32 bad = 0;
	for (u32 y = 0; y < 64; y++)
		for (u32 x = 0; x < 32; x++)
			bad += shown(d, 104 + x, 48 + y) != rgb15(x, y / 16 >> 1, 31);
	CHECK_EQ(bad, 0u);
	CHECK_EQ(shown(d, 0, 0), 0u);  // black around it
	CHECK_EQ(shown(d, 239, 159), 0u);
	free(d);
	free(f.bytes);
	// 32 bits, no masks: X8R8G8B8
	f = bmp_make(240, 160, 32, 0, NULL, 0, NULL, 0, px_x888);
	d = decode(&f);
	CHECK_EQ(d->result, PIC_OK);
	CHECK_EQ(shown(d, 100, 50), rgb15(100 >> 3, 0x20 >> 3, 0x10 >> 3));
	free(d);
	free(f.bytes);
	// 16 bits, no masks: X1R5G5B5
	f = bmp_make(16, 16, 16, 0, NULL, 0, NULL, 0, px_555);
	d = decode(&f);
	CHECK_EQ(d->result, PIC_OK);
	CHECK_EQ(shown(d, 112 + 3, 72 + 9), rgb15(3, 1, 7));
	free(d);
	free(f.bytes);
}

// Twice the screen: half size, every other pixel; a wide one keeps its shape
static u32 px_big(u32 x, u32 y)
{
	return (x % 2 || y % 2) ? 0xFFFFFF : (x / 2 % 32 * 8) << 16 | (y / 2 % 32 * 8);
}

static void test_picture_fit(void)
{
	struct file f = bmp_make(480, 320, 24, 0, NULL, 0, NULL, 0, px_big);
	struct decoded *d = decode(&f);
	CHECK_EQ(d->result, PIC_OK);
	CHECK_EQ(d->info.out_w, 240u);
	CHECK_EQ(d->info.out_h, 160u);
	// Output pixel x's middle is source column 2x + 1, an odd one: white
	CHECK_EQ(shown(d, 10, 10), rgb15(31, 31, 31));
	free(d);
	free(f.bytes);
	f = bmp_make(256, 192, 8, 0, NULL, 2, (const u32[]){0, 0xFFFFFF}, 0, px_index);
	d = decode(&f);
	CHECK_EQ(d->result, PIC_OK);
	CHECK_EQ(d->info.out_w, 213u);
	CHECK_EQ(d->info.out_h, 160u);
	CHECK_EQ(d->info.x0, 13u);
	free(d);
	free(f.bytes);
	// Sparse rows read one at a time: 2,000 rows, 160 shown
	f = bmp_make(100, 2000, 8, 0, NULL, 2, (const u32[]){0, 0xFFFFFF}, 0, px_index);
	d = decode(&f);
	CHECK_EQ(d->result, PIC_OK);
	CHECK_EQ(d->info.out_h, 160u);
	CHECK_EQ(d->info.out_w, 8u);
	CHECK(f.reads <= 3 + 160);  // the header twice (the test reads it too), the colours, the rows
	free(d);
	free(f.bytes);
}

static void test_picture_refused(void)
{
	struct file f = bmp_make(240, 160, 8, 0, NULL, 2, (const u32[]){0, 0xFFFFFF}, 0, px_index);
	struct decoded *d;
	// Not a BMP
	f.bytes[0] = 'P';
	d = decode(&f);
	CHECK_EQ(d->result, PIC_BROKEN);
	free(d);
	f.bytes[0] = 'B';
	// RLE
	put32(f.bytes + 30, 1);
	d = decode(&f);
	CHECK_EQ(d->result, PIC_UNSUPPORTED);
	free(d);
	put32(f.bytes + 30, 0);
	// Cut short
	f.size -= 100;
	d = decode(&f);
	CHECK_EQ(d->result, PIC_BROKEN);
	free(d);
	f.size += 100;
	// An OS/2 header, and a picture too wide
	put32(f.bytes + 14, 12);
	d = decode(&f);
	CHECK_EQ(d->result, PIC_UNSUPPORTED);
	free(d);
	put32(f.bytes + 14, 40);
	put32(f.bytes + 18, 9000);
	d = decode(&f);
	CHECK_EQ(d->result, PIC_UNSUPPORTED);
	free(d);
	put32(f.bytes + 18, 240);
	// A mask in two runs
	free(f.bytes);
	const u32 split[3] = {0xF00F, 0x07E0, 0x0010};
	f = bmp_make(8, 8, 16, 3, split, 0, NULL, 0, px_565);
	d = decode(&f);
	CHECK_EQ(d->result, PIC_UNSUPPORTED);
	free(d);
	free(f.bytes);
	// Whole again: it shows
	f = bmp_make(8, 8, 8, 0, NULL, 2, (const u32[]){0, 0xFFFFFF}, 0, px_index);
	d = decode(&f);
	CHECK_EQ(d->result, PIC_OK);
	free(d);
	free(f.bytes);
}

static void test_shotname(void)
{
	struct shotname_when w;
	char stem[SHOTNAME_STEM_BYTES];
	shotname_stem(stem, sizeof(stem), "/GBA/RPG/Golden Sun.gba");
	CHECK(strcmp(stem, "Golden Sun") == 0);
	shotname_stem(stem, sizeof(stem), "Metroid.Fusion.gba");
	CHECK(strcmp(stem, "Metroid.Fusion") == 0);
	shotname_stem(stem, sizeof(stem), "/x/.hidden");
	CHECK(strcmp(stem, ".hidden") == 0);
	// Cut at 47 bytes, never inside a character (é is 2 bytes at 46-47)
	shotname_stem(stem, sizeof(stem), "/0123456789012345678901234567890123456789012345\xC3\xA9xyz.gba");
	CHECK_EQ(strlen(stem), 46u);

	CHECK(shotname_match("Golden Sun 2026-10-28 14-03-22.bmp", "Golden Sun", &w));
	CHECK(w.year == 2026 && w.month == 10 && w.day == 28 && w.hour == 14 && w.min == 3 && w.sec == 22 && w.n == 1);
	CHECK(shotname_match("Golden Sun 2026-10-28 14-03-22-3.BMP", "Golden Sun", &w));
	CHECK_EQ(w.n, 3u);
	CHECK(shotname_match("Golden Sun-2.bmp", "Golden Sun", &w));
	CHECK(w.year == 0 && w.n == 2);
	CHECK(!shotname_match("Golden Sun 2 2026-10-28 14-03-22.bmp", "Golden Sun", &w));
	CHECK(!shotname_match("Golden Sun-2-2.bmp", "Golden Sun", &w));
	CHECK(!shotname_match("Golden Sun-1.bmp", "Golden Sun", &w));
	CHECK(!shotname_match("Golden Sun-02.bmp", "Golden Sun", &w));
	CHECK(!shotname_match("Golden Sun.bmp", "Golden Sun", &w));
	CHECK(!shotname_match("Golden Sun 2026-13-28 14-03-22.bmp", "Golden Sun", &w));
	CHECK(!shotname_match("Golden Sun 2026-10-28 14-03-22.png", "Golden Sun", &w));
	CHECK(shotname_match("Golden Sun-2-2.bmp", "Golden Sun-2", &w));

	CHECK_EQ(shotname_split("Golden Sun 2026-10-28 14-03-22.bmp", &w), 10u);
	CHECK_EQ(shotname_split("Golden Sun 2026-10-28 14-03-22-12.bmp", &w), 10u);
	CHECK_EQ(w.n, 12u);
	CHECK_EQ(shotname_split("Golden Sun-7.bmp", &w), 10u);
	CHECK_EQ(shotname_split("Holiday.bmp", &w), 0u);
	CHECK_EQ(shotname_split("a.txt", &w), 0u);

	struct shotname_when a = {2026, 10, 28, 14, 3, 22, 1}, b = a, none = {0, 0, 0, 0, 0, 0, 5};
	b.n = 2;
	CHECK(shotname_cmp(&a, &b) < 0);
	b = a;
	b.day = 27;
	CHECK(shotname_cmp(&a, &b) > 0);
	b = a;
	b.year = 2027;
	b.month = 1;
	CHECK(shotname_cmp(&a, &b) < 0);
	CHECK(shotname_cmp(&none, &a) < 0);
	CHECK_EQ(shotname_cmp(&a, &a), 0);
}

void test_picture(void)
{
	test_picture_exact_24();
	test_picture_torii_shot();
	test_picture_median_cut();
	test_picture_median_cut_mid();
	test_picture_median_cut_boxes();
	test_picture_steps();
	test_picture_indexed();
	test_picture_masks();
	test_picture_fit();
	test_picture_refused();
	test_shotname();
}
