// The GBA's picture unit in software (ppu_render.h).
#include "ppu_render.h"

#include <string.h>

// The registers, as halfword indexes from 0x04000000
#define R_DISPCNT 0x00
#define R_BG0CNT 0x04
#define R_BG0HOFS 0x08
#define R_BG2PA 0x10
#define R_WIN0H 0x20
#define R_WIN0V 0x22
#define R_WININ 0x24
#define R_WINOUT 0x25
#define R_BLDCNT 0x28
#define R_BLDALPHA 0x29
#define R_BLDY 0x2A

#define OBJ_TILES 0x10000  // OBJ VRAM's start
#define OBJ_TILES_BYTES 0x8000

// A pixel of a layer: RGB15, or LAYER_NONE where it is transparent
#define LAYER_NONE 0xFFFF
// The layers' numbers in BLDCNT: BG0-3, OBJ, the backdrop
#define L_OBJ 4
#define L_BD 5

static u16 rd16(const u8 *p)
{
	return (u16)(p[0] | p[1] << 8);
}

static s32 io32(const struct ppu_frame *f, u32 half)
{
	return (s32)((u32)f->io[half] | (u32)f->io[half + 1] << 16);
}

// A 28-bit signed reference point (20.8 fixed point)
static s32 ref28(const struct ppu_frame *f, u32 half)
{
	s32 v = io32(f, half);
	return (s32)((u32)v << 4) >> 4;
}

// --- Text BGs -----------------------------------------------------------------

static void bg_text(const struct ppu_frame *f, u32 n, u32 y, u16 *out)
{
	u16 cnt = f->io[R_BG0CNT + n];
	u32 char_base = ((cnt >> 2) & 3) * 0x4000, screen_base = ((cnt >> 8) & 0x1F) * 0x800;
	u32 bpp8 = cnt >> 7 & 1, size = cnt >> 14;
	u32 w = size & 1 ? 512 : 256, h = size & 2 ? 512 : 256;
	u32 hofs = f->io[R_BG0HOFS + n * 2] & 0x1FF, vofs = f->io[R_BG0HOFS + n * 2 + 1] & 0x1FF;
	u32 ty = (y + vofs) & (h - 1);
	const u8 *vram = f->vram;
	const u16 *pal = f->pal;
	// A tile's row of 8 pixels at a time: its map entry, then its pixels
	for (u32 x = 0; x < PPU_W;) {
		u32 tx = (x + hofs) & (w - 1);
		u32 block = (tx >> 8) + (ty >> 8) * (w >> 8);
		u32 at = screen_base + block * 0x800 + ((ty & 255) >> 3) * 64 + ((tx & 255) >> 3) * 2;
		u32 e = at + 1 < 0x10000 ? rd16(vram + at) : 0;
		u32 py = e & 0x800 ? 7 - (ty & 7) : ty & 7, flip = e & 0x400 ? 7 : 0;
		u32 row = bpp8 ? char_base + (e & 0x3FF) * 64 + py * 8 : char_base + (e & 0x3FF) * 32 + py * 4;
		u32 bank = (e >> 12) * 16;
		for (u32 px = tx & 7; px < 8 && x < PPU_W; px++, x++) {
			u32 p = px ^ flip, idx;
			if (bpp8) {
				idx = row + p < 0x10000 ? vram[row + p] : 0;
			} else {
				idx = row + p / 2 < 0x10000 ? (u32)(vram[row + p / 2] >> (p & 1) * 4) & 15 : 0;
				if (idx)
					idx += bank;
			}
			out[x] = idx ? pal[idx] & 0x7FFF : LAYER_NONE;
		}
	}
}

// --- Affine BGs and the bitmap modes ------------------------------------------

// BG n's matrix (2 or 3) and the point line y starts at; an all-zero matrix,
// which the dump has where the registers couldn't be read, as the identity
static void affine_of(const struct ppu_frame *f, u32 n, u32 y, s32 m[4], s32 *x0, s32 *y0)
{
	u32 base = R_BG2PA + (n - 2) * 8;
	for (u32 i = 0; i < 4; i++)
		m[i] = (s16)f->io[base + i];
	if (!m[0] && !m[1] && !m[2] && !m[3])
		m[0] = m[3] = 0x100;
	*x0 = ref28(f, base + 4) + m[1] * (s32)y;
	*y0 = ref28(f, base + 6) + m[3] * (s32)y;
}

static void bg_affine(const struct ppu_frame *f, u32 n, u32 y, u16 *out)
{
	u16 cnt = f->io[R_BG0CNT + n];
	u32 char_base = ((cnt >> 2) & 3) * 0x4000, screen_base = ((cnt >> 8) & 0x1F) * 0x800;
	s32 size = 128 << (cnt >> 14), wrap = cnt >> 13 & 1;
	s32 m[4], cx, cy;
	affine_of(f, n, y, m, &cx, &cy);
	for (u32 x = 0; x < PPU_W; x++, cx += m[0], cy += m[2]) {
		s32 tx = cx >> 8, ty = cy >> 8;
		out[x] = LAYER_NONE;
		if (wrap) {
			tx &= size - 1;
			ty &= size - 1;
		} else if (tx < 0 || ty < 0 || tx >= size || ty >= size) {
			continue;
		}
		u32 at = screen_base + (u32)(ty >> 3) * (u32)(size >> 3) + (u32)(tx >> 3);
		u32 tile = at < 0x10000 ? f->vram[at] : 0;
		u32 a = char_base + tile * 64 + (u32)(ty & 7) * 8 + (u32)(tx & 7);
		u32 idx = a < 0x10000 ? f->vram[a] : 0;
		if (idx)
			out[x] = f->pal[idx] & 0x7FFF;
	}
}

// Modes 3, 4 and 5: BG2 as a bitmap, through its matrix
static void bg_bitmap(const struct ppu_frame *f, u32 mode, u32 y, u16 *out)
{
	u32 frame = (f->io[R_DISPCNT] >> 4 & 1) && mode != 3 ? 0xA000 : 0;
	s32 w = mode == 5 ? 160 : 240, h = mode == 5 ? 128 : 160;
	s32 m[4], cx, cy;
	affine_of(f, 2, y, m, &cx, &cy);
	if (m[0] == 0x100 && m[2] == 0 && cx == 0 && cy >= 0 && (cy >> 8) < h) {  // no turn: a row as it is
		const u8 *row = f->vram + frame + (u32)((cy >> 8) * w) * (mode == 4 ? 1 : 2);
		for (u32 x = 0; x < PPU_W; x++) {
			u32 c = LAYER_NONE;
			if ((s32)x < w)
				c = mode == 4 ? (row[x] ? f->pal[row[x]] & 0x7FFF : LAYER_NONE) : rd16(row + x * 2) & 0x7FFF;
			out[x] = (u16)c;
		}
		return;
	}
	for (u32 x = 0; x < PPU_W; x++, cx += m[0], cy += m[2]) {
		s32 tx = cx >> 8, ty = cy >> 8;
		out[x] = LAYER_NONE;
		if (tx < 0 || ty < 0 || tx >= w || ty >= h)
			continue;
		u32 p = (u32)(ty * w + tx);
		if (mode == 4) {
			u32 idx = f->vram[frame + p];
			if (idx)
				out[x] = f->pal[idx] & 0x7FFF;
		} else {
			out[x] = rd16(f->vram + frame + p * 2) & 0x7FFF;
		}
	}
}

// --- Sprites ------------------------------------------------------------------

static const u8 obj_sizes[3][4][2] = {
	{{8, 8}, {16, 16}, {32, 32}, {64, 64}},
	{{16, 8}, {32, 8}, {32, 16}, {64, 32}},
	{{8, 16}, {8, 32}, {16, 32}, {32, 64}},
};

static void objs(const struct ppu_frame *f, u32 y, struct ppu_obj_px *out)
{
	u16 dispcnt = f->io[R_DISPCNT];
	u32 map1d = dispcnt >> 6 & 1, bitmap = (dispcnt & 7) >= 3;
	for (u32 x = 0; x < PPU_W; x++)
		out[x].colour = LAYER_NONE, out[x].win = 0;
	for (u32 i = 0; i < 128; i++) {
		const u16 *o = f->oam + i * 4;
		u16 a0 = o[0], a1 = o[1], a2 = o[2];
		u32 affine = a0 >> 8 & 1, mode = a0 >> 10 & 3, shape = a0 >> 14;
		if ((!affine && (a0 >> 9 & 1)) || mode == 3 || shape == 3)
			continue;  // hidden, or not a mode or shape
		u32 w = obj_sizes[shape][a1 >> 14][0], h = obj_sizes[shape][a1 >> 14][1];
		u32 bw = w, bh = h;  // the box on screen
		if (affine && (a0 >> 9 & 1)) {
			bw *= 2;
			bh *= 2;
		}
		u32 row = (y - (a0 & 0xFF)) & 0xFF;
		if (row >= bh)
			continue;
		s32 ox = a1 & 0x1FF;
		if (ox >= PPU_W)
			ox -= 512;
		u32 bpp8 = a0 >> 13 & 1, tile = a2 & 0x3FF, prio = a2 >> 10 & 3, bank = a2 >> 12;
		if (bitmap && tile < 512)
			continue;  // the bitmap modes' BG has those tiles
		u32 tiles_w = map1d ? w / 8 << bpp8 : 32;
		s32 pa = 0x100, pb = 0, pc = 0, pd = 0x100;
		if (affine) {
			const u16 *p = f->oam + ((a1 >> 9) & 31) * 16;
			pa = (s16)p[3];
			pb = (s16)p[7];
			pc = (s16)p[11];
			pd = (s16)p[15];
		}
		for (u32 bx = 0; bx < bw; bx++) {
			s32 sx = ox + (s32)bx;
			if (sx < 0 || sx >= PPU_W)
				continue;
			s32 tx, ty;
			if (affine) {
				s32 dx = (s32)bx - (s32)bw / 2, dy = (s32)row - (s32)bh / 2;
				tx = ((pa * dx + pb * dy) >> 8) + (s32)w / 2;
				ty = ((pc * dx + pd * dy) >> 8) + (s32)h / 2;
				if (tx < 0 || ty < 0 || tx >= (s32)w || ty >= (s32)h)
					continue;
			} else {
				tx = a1 & 0x1000 ? (s32)(w - 1 - bx) : (s32)bx;
				ty = a1 & 0x2000 ? (s32)(h - 1 - row) : (s32)row;
			}
			u32 t = tile + ((u32)ty >> 3) * tiles_w + (((u32)tx >> 3) << bpp8);
			u32 px = (u32)tx & 7, py = (u32)ty & 7, idx;
			if (bpp8) {
				idx = f->vram[OBJ_TILES + ((t * 32 + py * 8 + px) & (OBJ_TILES_BYTES - 1))];
			} else {
				idx = (u32)(f->vram[OBJ_TILES + ((t * 32 + py * 4 + px / 2) & (OBJ_TILES_BYTES - 1))] >>
					    (px & 1) * 4) &
				      15;
				if (idx)
					idx += bank * 16;
			}
			struct ppu_obj_px *d = &out[sx];
			if (mode == 2) {  // the OBJ window's shape, not drawn
				d->win |= idx != 0;
				continue;
			}
			// The first sprite in OAM order takes a pixel; a later one only
			// with a higher priority
			if (!idx || (d->colour != LAYER_NONE && d->prio <= prio))
				continue;
			d->colour = f->pal[256 + idx] & 0x7FFF;
			d->prio = (u8)prio;
			d->semi = mode == 1;
		}
	}
}

// --- Composing ----------------------------------------------------------------

static u16 blend(u16 a, u16 b, u32 eva, u32 evb)
{
	u32 c = 0;
	for (u32 s = 0; s < 15; s += 5) {
		u32 v = (((a >> s) & 31) * eva + ((b >> s) & 31) * evb) >> 4;
		c |= (v > 31 ? 31 : v) << s;
	}
	return (u16)c;
}

static u16 fade(u16 a, u32 evy, int up)
{
	u32 c = 0;
	for (u32 s = 0; s < 15; s += 5) {
		u32 v = (a >> s) & 31;
		v = up ? v + ((31 - v) * evy >> 4) : v - (v * evy >> 4);
		c |= v << s;
	}
	return (u16)c;
}

// The window control (WININ's or WINOUT's 6 bits) that applies at x on
// line y: window 0, window 1, the OBJ window, else outside. A window whose
// bounds read 0 (the dump's write-only registers) covers the screen.
static u32 in_range(u32 v, u32 a, u32 b)
{
	return a <= b ? v >= a && v < b : v >= a || v < b;
}

static u32 window_at(const struct ppu_frame *f, u32 x, u32 y, const struct ppu_obj_px *obj)
{
	u16 dispcnt = f->io[R_DISPCNT];
	for (u32 w = 0; w < 2; w++) {
		if (!(dispcnt >> (13 + w) & 1))
			continue;
		u16 h = f->io[R_WIN0H + w], v = f->io[R_WIN0V + w];
		if ((!h && !v) || (in_range(x, h >> 8, h & 0xFF) && in_range(y, v >> 8, v & 0xFF)))
			return f->io[R_WININ] >> (8 * w) & 0x3F;
	}
	if ((dispcnt & 0x8000) && (dispcnt & 0x1000) && obj[x].win)
		return f->io[R_WINOUT] >> 8 & 0x3F;
	return f->io[R_WINOUT] & 0x3F;
}

void ppu_render_line(const struct ppu_frame *f, u32 y, u16 *out, struct ppu_scratch *s)
{
	u16 dispcnt = f->io[R_DISPCNT];
	if (dispcnt & 0x80) {  // forced blank: white
		for (u32 x = 0; x < PPU_W; x++)
			out[x] = 0x7FFF;
		return;
	}
	u32 mode = dispcnt & 7;
	u16(*bg)[PPU_W] = s->bg;
	struct ppu_obj_px *obj = s->obj;
	u32 on = 0;  // the BGs drawn
	for (u32 n = 0; n < 4; n++) {
		if (!(dispcnt >> (8 + n) & 1))
			continue;
		if (mode == 0 || (mode == 1 && n < 2))
			bg_text(f, n, y, bg[n]);
		else if ((mode == 1 && n == 2) || (mode == 2 && n >= 2))
			bg_affine(f, n, y, bg[n]);
		else if (mode >= 3 && mode <= 5 && n == 2)
			bg_bitmap(f, mode, y, bg[n]);
		else
			continue;
		on |= 1u << n;
	}
	u32 sprites = dispcnt >> 12 & 1, windows = dispcnt >> 13 & 7;
	if (sprites)
		objs(f, y, obj);
	u16 bldcnt = f->io[R_BLDCNT], alpha = f->io[R_BLDALPHA];
	u32 eva = alpha & 31, evb = alpha >> 8 & 31, evy = f->io[R_BLDY] & 31;
	eva = eva > 16 ? 16 : eva;
	evb = evb > 16 ? 16 : evb;
	evy = evy > 16 ? 16 : evy;
	u32 effect = bldcnt >> 6 & 3;
	u16 backdrop = f->pal[0] & 0x7FFF;
	// The BGs drawn, front to back: by priority, then by number
	u32 order[4], prio[4], drawn = 0;
	for (u32 p = 0; p < 4; p++)
		for (u32 n = 0; n < 4; n++)
			if ((on >> n & 1) && (f->io[R_BG0CNT + n] & 3) == p) {
				order[drawn] = n;
				prio[drawn++] = p;
			}
	// The layer under the front one matters only to blending
	u32 wanted = effect == 1 || sprites ? 2 : 1;
	for (u32 x = 0; x < PPU_W; x++) {
		// The two front layers: a sprite before the BGs of its priority
		u16 c[2] = {backdrop, backdrop};
		u32 layer[2] = {L_BD, L_BD}, found = 0, semi = 0;
		u32 shown = windows ? window_at(f, x, y, obj) : 0x3F;  // layers, and bit 5 the effects
		u32 sprite = sprites && (shown & 0x10) && obj[x].colour != LAYER_NONE;
		for (u32 i = 0; i < drawn && found < wanted; i++) {
			if (sprite && obj[x].prio <= prio[i]) {
				semi = !found && obj[x].semi;
				c[found] = obj[x].colour;
				layer[found++] = L_OBJ;
				sprite = 0;
				if (found == wanted)
					break;
			}
			u32 n = order[i];
			if ((shown >> n & 1) && bg[n][x] != LAYER_NONE) {
				c[found] = bg[n][x];
				layer[found++] = n;
			}
		}
		if (sprite && found < wanted) {
			semi = !found && obj[x].semi;
			c[found] = obj[x].colour;
			layer[found++] = L_OBJ;
		}
		u32 first = bldcnt >> layer[0] & 1, second = bldcnt >> (8 + layer[1]) & 1;
		u16 px = c[0];
		if (!(shown & 0x20))
			first = second = semi = 0;
		if (semi && second)
			px = blend(c[0], c[1], eva, evb);
		else if (first && effect == 1 && second)
			px = blend(c[0], c[1], eva, evb);
		else if (first && effect >= 2)
			px = fade(c[0], evy, effect == 2);
		out[x] = px;
	}
}

void ppu_bmp_head(u8 *out)
{
	static const u8 head[PPU_BMP_HEAD] = {'B', 'M', PPU_BMP_BYTES & 0xFF, PPU_BMP_BYTES >> 8 & 0xFF,
					      PPU_BMP_BYTES >> 16 & 0xFF, 0, 0, 0, 0, 0, PPU_BMP_HEAD, 0, 0, 0,
					      // BITMAPINFOHEADER: 40 bytes, the width, the height negative (top
					      // down), 1 plane, 24 bits, no compression, the pixels' bytes, 72 dpi
					      40, 0, 0, 0, PPU_W, 0, 0, 0, (u8)(-PPU_H & 0xFF), 0xFF, 0xFF, 0xFF, 1, 0,
					      24, 0, 0, 0, 0, 0, (PPU_BMP_ROW * PPU_H) & 0xFF,
					      (PPU_BMP_ROW * PPU_H) >> 8 & 0xFF, (PPU_BMP_ROW * PPU_H) >> 16 & 0xFF, 0,
					      0x13, 0x0B, 0, 0, 0x13, 0x0B, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	memcpy(out, head, sizeof(head));
}

void ppu_bmp_row(const u16 *line, u8 *out)
{
	static const u8 eight[32] = {0,   8,   16,  24,  33,  41,  49,  57,  66,  74,  82,  90,  99,  107, 115, 123,
				     132, 140, 148, 156, 165, 173, 181, 189, 198, 206, 214, 222, 231, 239, 247, 255};
	for (u32 x = 0; x < PPU_W; x++) {
		u32 c = line[x];
		*out++ = eight[c >> 10 & 31];  // blue, green, red
		*out++ = eight[c >> 5 & 31];
		*out++ = eight[c & 31];
	}
}
