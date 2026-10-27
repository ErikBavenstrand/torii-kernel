// Draws a screenshot dump as the kernel does (source/ppu_render.c), on the
// host: tests/ppu_render.sh compares it with mGBA's own screen.
//
// usage: ppu_shot DUMP OUT.bmp
//   DUMP is an unpacked screenshot, INGAME_DUMP_BYTES of it (the
//   header, palettes, OAM and VRAM; source/ingame_abi.h), as the in-game
//   menu leaves it or as the test builds it from mGBA's memory.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../source/ingame_abi.h"
#include "../source/ppu_render.h"

int main(int argc, char **argv)
{
	if (argc != 3) {
		fprintf(stderr, "usage: ppu_shot DUMP OUT.bmp\n");
		return 2;
	}
	static u8 dump[INGAME_DUMP_BYTES];
	FILE *in = fopen(argv[1], "rb");
	if (!in || fread(dump, 1, sizeof(dump), in) != sizeof(dump)) {
		fprintf(stderr, "ppu_shot: %s: not %d bytes\n", argv[1], INGAME_DUMP_BYTES);
		return 1;
	}
	fclose(in);
	static u16 pal[512], oam[512], io[INGAME_SHOT_IO];
	memcpy(pal, dump + INGAME_DUMP_PAL, sizeof(pal));
	memcpy(oam, dump + INGAME_DUMP_OAM, sizeof(oam));
	memcpy(io, dump + offsetof(struct ingame_shot, io), sizeof(io));
	struct ppu_frame f = {dump + INGAME_DUMP_VRAM, pal, oam, io};
	static struct ppu_scratch scratch;
	static u8 bmp[PPU_BMP_BYTES];
	u16 line[PPU_W];
	ppu_bmp_head(bmp);
	for (u32 y = 0; y < PPU_H; y++) {
		ppu_render_line(&f, y, line, &scratch);
		ppu_bmp_row(line, bmp + PPU_BMP_HEAD + y * PPU_BMP_ROW);
	}
	FILE *out = fopen(argv[2], "wb");
	if (!out || fwrite(bmp, 1, sizeof(bmp), out) != sizeof(bmp) || fclose(out)) {
		fprintf(stderr, "ppu_shot: can't write %s\n", argv[2]);
		return 1;
	}
	return 0;
}
