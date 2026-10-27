// The screenshots' software renderer (source/ppu_render.c) on frames built
// by hand; tests/ppu_render.sh checks it against mGBA's screens.
#include <stdlib.h>
#include <string.h>

#include "../../source/ppu_render.h"
#include "../kernel/torii_test.h"
#include "tests.h"

struct frame {
	u8 vram[PPU_VRAM_BYTES];
	u16 pal[512], oam[512], io[PPU_IO_HALVES];
	struct ppu_frame f;
	struct ppu_scratch s;
	u16 line[PPU_W];
};

static struct frame *frame_new(void)
{
	struct frame *t = calloc(1, sizeof(*t));
	t->f = (struct ppu_frame){t->vram, t->pal, t->oam, t->io};
	for (u32 i = 0; i < 128; i++)
		t->oam[i * 4] = 0x200;  // every sprite hidden
	return t;
}

static const u16 *line(struct frame *t, u32 y)
{
	ppu_render_line(&t->f, y, t->line, &t->s);
	return t->line;
}

void test_ppu_render_text(void)
{
	struct frame *t = frame_new();
	t->io[0] = 0x0100;  // mode 0, BG0
	t->io[4] = 16 << 8 | 1 << 2;  // BG0: map at 0x8000, tiles at 0x4000, 4 bits
	t->pal[0] = 0x1111;
	t->pal[16 * 2 + 3] = 0x001F;  // bank 2, colour 3
	t->vram[0x4000 + 32] = 0x03;  // tile 1: its top left pixel colour 3
	u16 e = 1 | 2 << 12;
	memcpy(t->vram + 0x8000, &e, 2);  // map (0, 0): tile 1, bank 2
	e = 1 | 2 << 12 | 0x400;
	memcpy(t->vram + 0x8002, &e, 2);  // map (1, 0): flipped across
	const u16 *l = line(t, 0);
	CHECK_EQ(l[0], 0x001F);
	CHECK_EQ(l[1], 0x1111);  // transparent: the backdrop
	CHECK_EQ(l[15], 0x001F);
	CHECK_EQ(l[8], 0x1111);
	t->io[8] = 1;  // HOFS 1: (0, 0)'s pixel leaves the screen
	CHECK_EQ(line(t, 0)[0], 0x1111);
	t->io[0] |= 0x80;  // forced blank: white
	CHECK_EQ(line(t, 0)[100], 0x7FFF);
	free(t);
}

void test_ppu_render_sprites(void)
{
	struct frame *t = frame_new();
	t->io[0] = 0x1040;  // mode 0, OBJ, 1D
	t->pal[256 + 1] = 0x0001;
	t->pal[256 + 2] = 0x0002;
	// Tiles 0 and 1 of OBJ VRAM filled with colours 1 and 2
	memset(t->vram + 0x10000, 0x11, 32);
	memset(t->vram + 0x10020, 0x22, 32);
	// Sprite 0 (tile 0, priority 1) over sprite 1 (tile 1, priority 1) at (8, 8)
	u16 s0[3] = {8, 8, 0 | 1 << 10}, s1[3] = {8, 8, 1 | 1 << 10};
	memcpy(t->oam, s0, 6);
	memcpy(t->oam + 4, s1, 6);
	CHECK_EQ(line(t, 8)[8], 0x0001);  // the same priority: the first in OAM
	t->oam[4 + 2] = 1;  // sprite 1's priority 0 now: it wins
	CHECK_EQ(line(t, 8)[8], 0x0002);
	CHECK_EQ(line(t, 7)[8], 0);  // above them: the backdrop (colour 0)
	// An affine sprite with the identity matrix (parameters 0) draws as a plain one
	t->oam[4] = 0x200;
	t->oam[0] = 8 | 0x100;
	t->oam[3] = 0x100;
	t->oam[15] = 0x100;
	CHECK_EQ(line(t, 8)[8], 0x0001);
	CHECK_EQ(line(t, 15)[15], 0x0001);
	// Double size: the 8x8 picture in the middle of a 16x16 box
	t->oam[0] = 8 | 0x300;
	CHECK_EQ(line(t, 8)[8], 0);
	CHECK_EQ(line(t, 12)[12], 0x0001);
	CHECK_EQ(line(t, 19)[19], 0x0001);
	CHECK_EQ(line(t, 20)[20], 0);
	free(t);
}

void test_ppu_render_blend(void)
{
	struct frame *t = frame_new();
	t->io[0] = 0x0403;  // mode 3, BG2
	u16 c = 0x001F;
	for (u32 x = 0; x < PPU_W; x++)
		memcpy(t->vram + x * 2, &c, 2);
	t->pal[0] = 0x7C00;
	t->io[0x28] = 1 << 2 | 1 << 6 | 1 << 13;  // BG2 over the backdrop, alpha
	t->io[0x29] = 8 | 8 << 8;  // half and half
	CHECK_EQ(line(t, 0)[0], 0x3C0F);
	t->io[0x28] = 1 << 2 | 3 << 6;  // darken BG2 by half
	t->io[0x2A] = 8;
	CHECK_EQ(line(t, 0)[0], 0x0010);
	t->io[0x2A] = 0;  // BLDY unknown, as in a dump: no fade
	CHECK_EQ(line(t, 0)[0], 0x001F);
	free(t);
}

void test_ppu_render_bmp(void)
{
	u8 head[PPU_BMP_HEAD], row[PPU_BMP_ROW];
	ppu_bmp_head(head);
	CHECK(head[0] == 'B' && head[1] == 'M');
	CHECK_EQ(head[2] | head[3] << 8 | head[4] << 16, PPU_BMP_BYTES);
	CHECK_EQ(head[18], PPU_W);
	CHECK_EQ((s32)(head[22] | head[23] << 8 | head[24] << 16 | (u32)head[25] << 24), -PPU_H);
	CHECK_EQ(head[28], 24);
	u16 l[PPU_W];
	memset(l, 0, sizeof(l));
	l[0] = 0x7C1F;  // blue and red full
	ppu_bmp_row(l, row);
	CHECK(row[0] == 255 && row[1] == 0 && row[2] == 255 && row[3] == 0);
}
